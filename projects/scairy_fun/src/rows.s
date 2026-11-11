; ScAIry Fun - the road's rows (ANTIC D: 160 x 2-scan-line pixels, 40
; bytes a row, 4 pixels a byte, pixel 0 in bits 7-6).
;
; The bitmap holds only the road's shape; its colours (the stripes, the
; flood, the overpass shadow) are set per segment band by display-list
; interrupts. Pixel values: 0 = the ground (COLBK), 1 = road (COLPF0),
; 2 = rumble strips and lines (COLPF1), 3 = water and the autopilot's path
; (COLPF2).
;
; draw_bands takes the projected boundaries (render.c: per boundary the
; centre line c, lane width lw, the ghost lines' offset g and the path's
; offset p, all 12.4 pixels, and its row) and draws the bands between
; them, near to far, each over the rows a nearer one (a hill's crest)
; doesn't cover. A band's ten edges (the road's shape: far side | near
; side | rumble | road | rumble | near side | far side; the three marks:
; two lines and the path; and the lane width, for the marks' widths) are
; derived from c, lw, g and p at its bottom row and stepped row by row
; (12.4 fixed point, one 2-byte add each). Each row's descriptor (16 bytes:
; the six edges, the four side colours, three marks' x and width) is
; compared with the one drawn there before: only a changed row is redrawn.

        .export _draw_bands, _rows_reset, _sky_rows, _rows_drawn
        .export _bd_c, _bd_lw, _bd_g, _bd_p, _bd_row, _bd_mode, _bd_side, _bd_fw, _bd_n
        .export _bt_top, _bt_b, _bt_n, _bt_hor
        .import _divn, _mult, _dn, _mk
        .importzp ptr1, ptr2

BITMAP  = $A000
ROWS    = 56
DSZ     = 16
NB      = 20                    ; boundaries at most
NE      = 10                    ; edges a band

; band modes
M_THREE = $01                   ; 3 lanes our way, none oncoming (the freeway)
M_GHOST = $02                   ; old lines: the centre line moved by g, and an edge line
M_PATH  = $04                   ; the autopilot's path at c + p
M_NOLINE = $08                  ; no centre line (worn away, or water)

; edge indices
E_LF    = 0                     ; far left side
E_LN    = 1                     ; near left side (the rumble's start)
E_L     = 2                     ; the road's left edge
E_R     = 3
E_RN    = 4
E_RF    = 5
E_M0    = 6                     ; marks: their centres
E_M1    = 7
E_M2    = 8
E_LW    = 9                     ; the lane width (for the marks' widths)

.segment "SCRBSS"
; in: per boundary (render.c)
_bd_c:   .res NB * 2
_bd_lw:  .res NB * 2
_bd_g:   .res NB * 2
_bd_p:   .res NB * 2
_bd_row: .res NB * 2
_bd_mode: .res NB
_bd_side: .res NB * 4           ; colours: left far, left near, right near, right far
_bd_fw:  .res NB * 2            ; far side widths (left, right): 0, sh/2, sh, 2sh
_bd_n:   .res 1                 ; boundaries
; out: the bands drawn, near to far (their top rows, boundary indices)
_bt_top: .res NB
_bt_b:   .res NB
_bt_n:   .res 1
_bt_hor: .res 1                 ; the top row of the road (rows above: sky)
_rows_drawn: .res 2
prev:   .res ROWS * DSZ
desc:   .res DSZ
; the band being drawn
nb:     .res 1                  ; its near boundary
top:    .res 1
bot:    .res 1
ymax:   .res 1
row:    .res 1
mode:   .res 1
yn:     .res 2
yf:     .res 2
rows:   .res 2
skip:   .res 1
qc:     .res 2                  ; 12.4 values at the bottom row
qlw:    .res 2
qg:     .res 2
qp:     .res 2
sc:     .res 2                  ; 12.4 steps a row (up): 8 bytes after qc
slw:    .res 2
sg:     .res 2
sp:     .res 2
sh:     .res 2
lft:    .res 2
rgt:    .res 2
t0:     .res 2
t1:     .res 2
fwl:    .res 1
fwr:    .res 1
e16:    .res NE * 2             ; edges (12.4), as made by edges
v16:    .res NE * 2             ; the band's edges at the current row (12.4)
d16:    .res NE * 2             ; their steps a row
lwpx:   .res 1
mw:     .res 1
cur:    .res 1                  ; the span emitter: the current pixel
acc:    .res 1                  ;   the byte being made
pat:    .res 1
ecol:   .res 1
idx:    .res 1
tt:     .res 2
xs:     .res 1
vc:     .res 2
vlw:    .res 2
vg:     .res 2
vp:     .res 2

.segment "HIA"
rowlo:
        .repeat ROWS, I
        .byte <(BITMAP + I * 40)
        .endrepeat
rowhi:
        .repeat ROWS, I
        .byte >(BITMAP + I * 40)
        .endrepeat
prvlo:
        .repeat ROWS, I
        .byte <(prev + I * DSZ)
        .endrepeat
prvhi:
        .repeat ROWS, I
        .byte >(prev + I * DSZ)
        .endrepeat
pats:   .byte $00, $55, $AA, $FF
tailm:  .byte $FF, $3F, $0F, $03 ; pixels k..3
headm:  .byte $00, $C0, $F0, $FC ; pixels 0..k-1
pixm:   .byte $C0, $30, $0C, $03

.macro ADD16 dst, s1, s2
        clc
        lda s1
        adc s2
        sta dst
        lda s1+1
        adc s2+1
        sta dst+1
.endmacro

.macro SUB16 dst, s1, s2
        sec
        lda s1
        sbc s2
        sta dst
        lda s1+1
        sbc s2+1
        sta dst+1
.endmacro

.macro MOV16 dst, src
        lda src
        sta dst
        lda src+1
        sta dst+1
.endmacro

; forget what was drawn (every row will be redrawn)
.proc _rows_reset
        ldx #0
@r:     lda prvlo,x
        sta ptr1
        lda prvhi,x
        sta ptr1+1
        lda #$FE
        ldy #DSZ - 1
@b:     sta (ptr1),y
        dey
        bpl @b
        inx
        cpx #ROWS
        bne @r
        rts
.endproc

; rows 0 .. A-1 are sky: cleared to 0 unless they already are
.proc _sky_rows
        sta t1
        ldx #0
@r:     cpx t1
        bcs done
        lda prvlo,x
        sta ptr2
        lda prvhi,x
        sta ptr2+1
        ldy #0
        lda (ptr2),y
        cmp #$FD
        beq @n
        lda #$FD
        sta (ptr2),y
        lda rowlo,x
        sta ptr1
        lda rowhi,x
        sta ptr1+1
        lda #0
        ldy #39
@z:     sta (ptr1),y
        dey
        bpl @z
@n:     inx
        bne @r
done:   rts
.endproc

; ------------------------------------------------------------ band setup
; t0 = (t0 * 7) >> 4 (the shoulder from a lane width), signed
.proc sh_of
        MOV16 t1, t0
        asl t0
        rol t0+1
        asl t0
        rol t0+1
        asl t0
        rol t0+1
        SUB16 t0, t0, t1
        ldy #4
@s:     lda t0+1
        cmp #$80
        ror t0+1
        ror t0
        dey
        bne @s
        rts
.endproc

; t1 = far side width code A applied to sh (0, sh/2, sh, 2sh)
.proc farw
        tay
        lda #0
        sta t1
        sta t1+1
        cpy #0
        beq done
        MOV16 t1, sh
        cpy #2
        beq done
        bcs dbl
        lda t1+1
        cmp #$80
        ror t1+1
        ror t1
        rts
dbl:    asl t1
        rol t1+1
done:   rts
.endproc

; the ten edges (12.4, into e16) from c, lw, g, p: X = 0 the values (qc..),
; X = 8 the steps (sc..): the same linear combinations
.proc edges
        ldy #0                  ; the four values (or steps) to vc.. vp
@cp:    lda qc,x
        sta vc,y
        inx
        iny
        cpy #8
        bne @cp
        lda vlw                 ; sh = lw * 7/16
        sta t0
        lda vlw+1
        sta t0+1
        jsr sh_of
        MOV16 sh, t0
        lda mode
        and #M_THREE
        bne three
        SUB16 lft, vc, vlw      ; one lane each way: c - lw .. c + lw
        ADD16 rgt, vc, vlw
        jmp have
three:  lda vlw                 ; the freeway: c - lw/8 .. c + 3 lw
        sta t0
        lda vlw+1
        cmp #$80
        ror a
        ror t0
        cmp #$80
        ror a
        ror t0
        cmp #$80
        ror a
        ror t0
        sta t0+1
        SUB16 lft, vc, t0
        ADD16 t0, vlw, vlw
        ADD16 t0, t0, vlw
        ADD16 rgt, vc, t0
have:   MOV16 e16+E_L*2, lft
        MOV16 e16+E_R*2, rgt
        SUB16 e16+E_LN*2, lft, sh
        ADD16 e16+E_RN*2, rgt, sh
        lda fwl
        jsr farw
        SUB16 e16+E_LF*2, e16+E_LN*2, t1
        lda fwr
        jsr farw
        ADD16 e16+E_RF*2, e16+E_RN*2, t1
        lda mode
        and #M_THREE
        beq one
        ADD16 e16+E_M0*2, vc, vlw       ; c + lw, c + 2 lw
        ADD16 e16+E_M1*2, e16+E_M0*2, vlw
        jmp path
one:    lda mode
        and #M_GHOST
        beq noghost
        ADD16 e16+E_M0*2, vc, vg        ; the old centre and edge lines
        ADD16 e16+E_M1*2, rgt, vg
        jmp path
noghost:
        MOV16 e16+E_M0*2, vc                ; the centre line
        MOV16 e16+E_M1*2, vc
path:   ADD16 e16+E_M2*2, vc, vp
        MOV16 e16+E_LW*2, vlw
        rts
.endproc

; e16 -> the steps d16 (after edges for the steps)
.proc keep_steps
        ldx #NE * 2 - 1
@c:     lda e16,x
        sta d16,x
        dex
        bpl @c
        rts
.endproc

; e16 -> the values v16
.proc keep_vals
        ldx #NE * 2 - 1
@c:     lda e16,x
        sta v16,x
        dex
        bpl @c
        rts
.endproc

; ------------------------------------------------------------ drawing
; pixel of edge X (X = index * 2), clamped 0..160, in A: v16 >> 4
.macro PX
        .local zero, full, ok
        lda v16+1,x
        bmi zero
        cmp #$0A                ; >= 160 px (160 * 16 = $0A00)
        bcs full
        asl a
        asl a
        asl a
        asl a
        sta tt
        lda v16,x
        lsr a
        lsr a
        lsr a
        lsr a
        ora tt
        jmp ok
full:   lda #160
        bne ok
zero:   lda #0
ok:
.endmacro

; a span from cur to A (pixel, exclusive) in colour ecol
.proc span
        cmp cur
        beq out
        bcc out
        sta t1                  ; e
        ldx ecol
        lda pats,x
        sta pat
        lda cur
        lsr a
        lsr a
        sta idx                 ; b0
        lda t1
        lsr a
        lsr a                   ; b1
        cmp idx
        bne multi
        lda cur
        and #3
        tax
        lda t1
        and #3
        tay
        lda tailm,x
        and headm,y
        and pat
        ora acc
        sta acc
        lda t1
        sta cur
out:    rts
multi:  sta t1+1                ; b1
        lda cur
        and #3
        tax
        lda tailm,x
        and pat
        ora acc
        ldy idx
        sta (ptr1),y
        iny
        lda pat
@f:     cpy t1+1
        bcs @e
        sta (ptr1),y
        iny
        bne @f
@e:     lda t1
        and #3
        tax
        lda headm,x
        and pat
        sta acc
        lda t1
        sta cur
        rts
.endproc

; draw the row (ptr1) from desc
.proc draw
        lda #0
        sta cur
        sta acc
        lda desc+6
        sta ecol
        lda desc+0
        jsr span
        lda desc+7
        sta ecol
        lda desc+1
        jsr span
        lda #2
        sta ecol
        lda desc+2
        jsr span
        lda #1
        sta ecol
        lda desc+3
        jsr span
        lda #2
        sta ecol
        lda desc+4
        jsr span
        lda desc+8
        sta ecol
        lda desc+5
        jsr span
        lda desc+9
        sta ecol
        lda #160
        jsr span
        ldx #10
        lda #2
        jsr pmark
        ldx #12
        lda #2
        jsr pmark
        ldx #14
        lda #3
        ; fall through
.endproc

; mark desc[x] (x, width) in colour A
.proc pmark
        tay
        lda pats,y
        sta pat
        lda desc,x
        cmp #160
        bcs done
        sta cur
        lda desc+1,x
        beq done
        sta idx
@p:     lda cur
        cmp #160
        bcs done
        and #3
        tax
        lda cur
        lsr a
        lsr a
        tay
        lda pixm,x
        eor #$FF
        and (ptr1),y
        sta acc
        lda pixm,x
        and pat
        ora acc
        sta (ptr1),y
        inc cur
        dec idx
        bne @p
done:   rts
.endproc

; a mark: edge X's (X = index * 2) pixel less half the width mw, into
; desc,y ($FF: none)
.proc mark
        lda v16+1,x
        bmi none
        cmp #$0A
        bcs none
        sty idx
        PX
        ldy idx
        sta tt+1
        lda mw
        lsr a
        sta tt
        lda tt+1
        sec
        sbc tt
        bcc none
        sta desc,y
        lda mw
        sta desc+1,y
        rts
none:   lda #$FF
        sta desc,y
        lda #0
        sta desc+1,y
        rts
.endproc

; the row: descriptor from the edges, compare, draw if changed
.proc do_row
        ldx #E_LF*2
        PX
        sta desc+0
        ldx #E_LN*2
        PX
        sta desc+1
        ldx #E_L*2
        PX
        sta desc+2
        ldx #E_R*2
        PX
        sta desc+3
        ldx #E_RN*2
        PX
        sta desc+4
        ldx #E_RF*2
        PX
        sta desc+5
        ldx #E_LW               ; marks 1 + lw/16 pixels wide, the path 1 + lw/8
        PX
        sta lwpx
        lsr a
        lsr a
        lsr a
        lsr a
        clc
        adc #1
        sta mw
        lda mode
        and #M_NOLINE
        beq @m0
        lda #$FF
        sta desc+10
        lda #0
        sta desc+11
        jmp @m1
@m0:    ldx #E_M0
        ldy #10
        jsr mark
@m1:    lda mode
        and #(M_THREE | M_GHOST)
        bne @m1y
        lda #$FF
        sta desc+12
        lda #0
        sta desc+13
        jmp @m2
@m1y:   ldx #E_M1
        ldy #12
        jsr mark
@m2:    lda mode
        and #M_PATH
        bne @m2y
        lda #$FF
        sta desc+14
        lda #0
        sta desc+15
        jmp cmpr
@m2y:   lda lwpx
        lsr a
        lsr a
        lsr a
        clc
        adc #1
        sta mw
        ldx #E_M2*2
        ldy #14
        jsr mark
cmpr:   ldx row
        lda prvlo,x
        sta ptr2
        lda prvhi,x
        sta ptr2+1
        ldy #DSZ - 1
@c:     lda desc,y
        cmp (ptr2),y
        bne changed
        dey
        bpl @c
        rts
changed:
        inc _rows_drawn
        bne :+
        inc _rows_drawn+1
:       ldy #DSZ - 1
@k:     lda desc,y
        sta (ptr2),y
        dey
        bpl @k
        ldx row
        lda rowlo,x
        sta ptr1
        lda rowhi,x
        sta ptr1+1
        jmp draw
.endproc

; ------------------------------------------------------------ the bands
.proc _draw_bands
        lda #ROWS
        sta ymax
        lda #0
        sta _bt_n
        sta nb
loop:   lda nb
        clc
        adc #1
        cmp _bd_n
        bcc :+
        jmp done
:       lda nb
        asl a
        tay
        lda _bd_row,y
        sta yn
        lda _bd_row+1,y
        sta yn+1
        lda _bd_row+2,y
        sta yf
        lda _bd_row+3,y
        sta yf+1
        ; yf >= ymax: behind a crest
        lda yf+1
        bmi vis
        bne next
        lda yf
        cmp ymax
        bcs next
vis:    ; yf >= yn: not a band
        lda yf
        cmp yn
        lda yf+1
        sbc yn+1
        bvc :+
        eor #$80
:       bpl next
        ; top = max(yf, 0), bot = min(yn, ymax) - 1
        lda yf+1
        bmi t0z
        lda yf
        jmp tset
t0z:    lda #0
tset:   sta top
        lda yn+1
        bmi next
        bne bmax
        lda yn
        cmp ymax
        bcc bset
bmax:   lda ymax
bset:   sec
        sbc #1
        sta bot
        cmp top
        bcs band
next:   inc nb
        jmp loop
band:   jsr do_band
        ldx _bt_n
        lda top
        sta _bt_top,x
        lda nb
        sta _bt_b,x
        inc _bt_n
        lda top
        sta ymax
        beq done
        inc nb
        jmp loop
done:   lda ymax
        sta _bt_hor
        rts
.endproc

; X = the offset in a 2-byte table for nb (nb * 2), in xs
.proc do_band
        lda nb
        asl a
        tay
        lda _bd_c,y
        sta qc
        lda _bd_c+1,y
        sta qc+1
        lda _bd_lw,y
        sta qlw
        lda _bd_lw+1,y
        sta qlw+1
        lda _bd_g,y
        sta qg
        lda _bd_g+1,y
        sta qg+1
        lda _bd_p,y
        sta qp
        lda _bd_p+1,y
        sta qp+1
        lda _bd_fw,y
        sta fwl
        lda _bd_fw+1,y
        sta fwr
        ldx nb
        lda _bd_mode,x
        sta mode
        ; rows = yn - yf (clamped 255), skip = yn - 1 - bot (clamped)
        sec
        lda yn
        sbc yf
        sta rows
        lda yn+1
        sbc yf+1
        beq :+
        lda #255
        sta rows
:       sec
        lda yn
        sbc #1
        sta tt
        lda yn+1
        sbc #0
        sta tt+1
        sec
        lda tt
        sbc bot
        sta skip
        lda tt+1
        sbc #0
        beq :+
        lda #255
        sta skip
:       lda #0                  ; no steps yet
        ldx #7
@z:     sta sc,x
        dex
        bpl @z
        lda bot                 ; one row at the bottom boundary: no steps
        cmp top
        bne steps
        lda skip
        bne steps
        jmp edges0
steps:  lda rows
        sta _dn
        lda nb
        asl a
        sta xs
        tay                     ; sc = (c[n+1] - c[n]) / rows, and lw, g, p
        sec
        lda _bd_c+2,y
        sbc _bd_c,y
        pha
        lda _bd_c+3,y
        sbc _bd_c+1,y
        tax
        pla
        jsr _divn
        sta sc
        stx sc+1
        ldy xs
        sec
        lda _bd_lw+2,y
        sbc _bd_lw,y
        pha
        lda _bd_lw+3,y
        sbc _bd_lw+1,y
        tax
        pla
        jsr _divn
        sta slw
        stx slw+1
        ldy xs
        sec
        lda _bd_g+2,y
        sbc _bd_g,y
        pha
        lda _bd_g+3,y
        sbc _bd_g+1,y
        tax
        pla
        jsr divz
        sta sg
        stx sg+1
        ldy xs
        sec
        lda _bd_p+2,y
        sbc _bd_p,y
        pha
        lda _bd_p+3,y
        sbc _bd_p+1,y
        tax
        pla
        jsr divz
        sta sp
        stx sp+1
        ; the rows below the screen: values += steps * skip
        lda skip
        beq edges0
        sta _mk
        ldy #0
@sk:    sty xs
        lda sc,y
        ldx sc+1,y
        jsr _mult
        ldy xs
        clc
        adc qc,y
        sta qc,y
        txa
        adc qc+1,y
        sta qc+1,y
        iny
        iny
        cpy #8
        bne @sk
edges0: ; the edges at the bottom row, and their steps (if any)
        lda bot
        cmp top
        beq @one
        ldx #8
        jsr edges
        jsr keep_steps
@one:   ldx #0
        jsr edges
        jsr keep_vals
        ; the side colours (the same on every row of the band)
        lda nb
        asl a
        asl a
        tax
        lda _bd_side,x
        sta desc+6
        lda _bd_side+1,x
        sta desc+7
        lda _bd_side+2,x
        sta desc+8
        lda _bd_side+3,x
        sta desc+9
        ; the rows, bottom to top
        lda bot
        sta row
rl:     jsr do_row
        lda row
        cmp top
        beq out
        dec row
        ldx #NE * 2 - 2
@a:     clc
        lda v16,x
        adc d16,x
        sta v16,x
        lda v16+1,x
        adc d16+1,x
        sta v16+1,x
        dex
        dex
        bpl @a
        jmp rl
out:    rts
.endproc

; divn, or 0 for 0
.proc divz
        cmp #0
        bne :+
        cpx #0
        bne :+
        rts
:       jmp _divn
.endproc
