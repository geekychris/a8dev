; ScAIry Fun - the road's rows (ANTIC D: 160 x 2-scan-line pixels, 40
; bytes a row, 4 pixels a byte, pixel 0 in bits 7-6).
;
; The bitmap holds only the road's shape; its colours (the stripes, the
; flood, the overpass shadow) are set per segment band by display-list
; interrupts. Pixel values: 0 = the ground (COLBK), 1 = road (COLPF0),
; 2 = rumble strips and lines (COLPF1), 3 = water and the autopilot's path
; (COLPF2).
;
; draw_bands takes the projected boundaries (project.s: per boundary the
; centre line c, lane width lw, the ghost lines' offset g and the path's
; offset p, 12.4 pixels, and its row) and draws the bands between them,
; near to far, each over the rows a nearer one (a hill's crest) doesn't
; cover. In a band, c, lw, g and p are stepped row by row in 16.8 fixed
; point; each row's edges (far side | near side | rumble | road | rumble |
; near side | far side) and marks (two lines, the path) follow in whole
; pixels. A row's descriptor (13 bytes: six edges, the four side colours
; packed, three marks' x and width) is compared with the one drawn there
; before: only a changed row is redrawn.

        .export _draw_bands, _rows_reset, _sky_rows, _rows_drawn, _rows_init
        .export _bd_c, _bd_lw, _bd_g, _bd_p, _bd_row, _bd_mode, _bd_side, _bd_fw, _bd_n
        .export _bt_top, _bt_b, _bt_n, _bt_hor
        .import _divn, _mult, _dn, _mk
        .importzp ptr1, ptr2

BITMAP  = $A000
ROWS    = 56
DSZ     = 13
NB      = 20

M_THREE = $01                   ; 3 lanes our way, none oncoming (the freeway)
M_GHOST = $02                   ; old lines: the centre line moved by g, and an edge line
M_PATH  = $04                   ; the autopilot's path at c + p
M_NOLINE = $08                  ; no centre line (worn away, or water)

.segment "SCRBSS"
; in: per boundary (project.s, render.c); c, lw, g, p: consecutive tables
_bd_c:   .res NB * 2
_bd_lw:  .res NB * 2
_bd_g:   .res NB * 2
_bd_p:   .res NB * 2
_bd_row: .res NB * 2
_bd_mode: .res NB
_bd_side: .res NB * 4           ; colours: left far, left near, right near, right far
_bd_fw:  .res NB * 2            ; far side widths (left, right): 0, sh/2, sh, 2sh
_bd_n:   .res 1
; out: the bands drawn, near to far
_bt_top: .res NB
_bt_b:   .res NB
_bt_n:   .res 1
_bt_hor: .res 1
_rows_drawn: .res 2
prev:   .res ROWS * DSZ
desc:   .res DSZ
nb:     .res 1
top:    .res 1
bot:    .res 1
ymax:   .res 1
row:    .res 1
mode:   .res 1
sides:  .res 1                  ; packed: LF << 6 | LN << 4 | RN << 2 | RF
fwl:    .res 1
fwr:    .res 1
yn:     .res 2
yf:     .res 2
rows:   .res 1
skip:   .res 1
tt:     .res 2
t0:     .res 2
xs:     .res 1
qv:     .res 8                  ; c, lw, g, p (12.4)
sv:     .res 8                  ; their steps
; the stepped values, 16.8 (f = fraction, l = low, h = high); lw 8.8
cf:     .res 1
cl:     .res 1
ch:     .res 1
lf:     .res 1
ll:     .res 1
gf:     .res 1
gl:     .res 1
gh:     .res 1
pf:     .res 1
pl:     .res 1
ph:     .res 1
dcf:    .res 1
dcl:    .res 1
dch:    .res 1
dlf:    .res 1
dll:    .res 1
dgf:    .res 1
dgl:    .res 1
dgh:    .res 1
dpf:    .res 1
dpl:    .res 1
dph:    .res 1
; a row, in pixels
lw:     .res 1
sh:     .res 1
lft:    .res 2
rgt:    .res 2
ex:     .res 2
fwlp:   .res 1
fwrp:   .res 1
mw:     .res 1
cur:    .res 1
acc:    .res 1
pat:    .res 1
ecol:   .res 1
idx:    .res 1
fillv:  .res 2

.segment "LOWRAM"
shtab:  .res 256                ; lw -> the shoulder, lw * 7 / 16

.segment "HIA"
DBL     = 22                    ; from here down, pairs of rows share one (render.c's list)
rowlo:
        .repeat ROWS, I
        .if I < DBL
        .byte <(BITMAP + I * 40)
        .else
        .byte <(BITMAP + (I & $FE) * 40)
        .endif
        .endrepeat
rowhi:
        .repeat ROWS, I
        .if I < DBL
        .byte >(BITMAP + I * 40)
        .else
        .byte >(BITMAP + (I & $FE) * 40)
        .endif
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
tol:    .byte 1, 1, 1, 1, 1, 1, 0, 1, 0, 1, 0, 1, 0  ; per descriptor byte: +-1 allowed
qoff:   .byte 0, 0, NB * 2, 0, NB * 4, 0, NB * 6   ; c, lw, g, p tables' offsets

; the shoulder table
.proc _rows_init
        ldx #0
@t:     stx tt                  ; x * 7 / 16 = (x * 8 - x) / 16
        lda #0
        sta tt+1
        txa
        asl a
        rol tt+1
        asl a
        rol tt+1
        asl a
        rol tt+1
        sec
        sbc tt
        sta t0
        lda tt+1
        sbc #0
        lsr a
        ror t0
        lsr a
        ror t0
        lsr a
        ror t0
        lsr a
        ror t0
        lda t0
        sta shtab,x
        inx
        bne @t
        rts
.endproc

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
        sta tt
        ldx #0
@r:     cpx tt
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

; ------------------------------------------------------------ drawing
; full bytes: Y = the first, X = how many, A = the pattern: an unrolled run
; of "sta (ptr1),y / iny" entered X pairs from its end
.proc fill
        sta tt
        txa
        beq out
        sta tt+1                ; vector = fend - X * 3
        asl a
        clc
        adc tt+1
        sta tt+1
        sec
        lda #<fend
        sbc tt+1
        sta fillv
        lda #>fend
        sbc #0
        sta fillv+1
        lda tt
        jmp (fillv)
out:    rts
.endproc
fstart:
        .repeat 40
        sta (ptr1),y
        iny
        .endrepeat
fend:   rts

; a span from cur to A (pixel, exclusive) in colour ecol
.proc span
        cmp cur
        beq out
        bcc out
        sta t0                  ; e
        ldx ecol
        lda pats,x
        sta pat
        lda cur
        lsr a
        lsr a
        sta idx                 ; b0
        lda t0
        lsr a
        lsr a                   ; b1
        cmp idx
        bne multi
        lda cur
        and #3
        tax
        lda t0
        and #3
        tay
        lda tailm,x
        and headm,y
        and pat
        ora acc
        sta acc
        lda t0
        sta cur
out:    rts
multi:  sta t0+1                ; b1
        lda cur
        and #3
        tax
        lda tailm,x
        and pat
        ora acc
        ldy idx
        sta (ptr1),y
        iny
        lda t0+1                ; the bytes b0 + 1 .. b1 - 1
        sec
        sbc idx
        tax
        dex
        lda pat
        jsr fill
        lda t0
        and #3
        tax
        lda headm,x
        and pat
        sta acc
        lda t0
        sta cur
        rts
.endproc

; draw the row (ptr1) from desc
.proc draw
        lda #0
        sta cur
        sta acc
        lda desc+6
        rol a
        rol a
        rol a
        and #3
        sta ecol                ; LF
        lda desc+0
        jsr span
        lda desc+6
        lsr a
        lsr a
        lsr a
        lsr a
        and #3
        sta ecol                ; LN
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
        lda desc+6
        lsr a
        lsr a
        and #3
        sta ecol                ; RN
        lda desc+5
        jsr span
        lda desc+6
        and #3
        sta ecol                ; RF
        lda #160
        jsr span
        ldx #7
        lda #2
        jsr pmark
        ldx #9
        lda #2
        jsr pmark
        ldx #11
        lda #3
        ; fall through
.endproc

; mark desc[x] (x, width) in colour A: a partial byte at each end, whole
; bytes between (a mark can be 14 pixels wide near the car)
.proc pmark
        tay
        lda pats,y
        sta pat
        lda desc,x
        cmp #160
        bcc :+
        rts
:       sta cur                 ; x0
        clc
        adc desc+1,x
        bcs clip
        cmp #161
        bcc :+
clip:   lda #160
:       sta t0                  ; x1 (exclusive)
        cmp cur
        bne :+
        rts
:       bcs :+
        rts
:
        lda cur
        lsr a
        lsr a
        sta idx                 ; b0
        lda t0
        lsr a
        lsr a
        sta t0+1                ; b1
        cmp idx
        bne multi
        ldx cur                 ; one byte: pixels x0&3 .. x1&3 - 1
        lda cur
        and #3
        tax
        lda t0
        and #3
        tay
        lda tailm,x
        and headm,y
        jmp rmw1
multi:  lda cur                 ; the first byte from x0
        and #3
        tax
        lda tailm,x
        ldy idx
        jsr rmw
        iny                     ; whole bytes
        lda t0+1
        sec
        sbc idx
        tax
        dex
        lda pat
        jsr fill
        lda t0                  ; the last byte to x1
        and #3
        beq done
        tax
        lda headm,x
        ldy t0+1
        jmp rmw
rmw1:   ldy idx
rmw:    sta acc                 ; byte y: mask A takes the pattern
        and pat
        sta ecol
        lda acc
        eor #$FF
        and (ptr1),y
        ora ecol
        sta (ptr1),y
done:   rts
.endproc

; ex (16-bit pixel) clamped to 0..160 -> A
.macro CLAMP
        .local zero, full, ok
        lda ex+1
        bmi zero
        bne full
        lda ex
        cmp #160
        bcc ok
full:   lda #160
        bne ok
zero:   lda #0
ok:
.endmacro

; a mark at pixel ex (its centre) of width mw -> desc,y ($FF: none)
.proc mark
        lda mw
        lsr a
        sta tt
        sec
        lda ex
        sbc tt
        sta tt
        lda ex+1
        sbc #0
        bne none
        lda tt
        cmp #160
        bcs none
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

.macro ADD8 dst, s1, v        ; dst = s1 (16-bit) + v (unsigned byte)
        clc
        lda s1
        adc v
        sta dst
        lda s1+1
        adc #0
        sta dst+1
.endmacro
.macro SUB8 dst, s1, v
        sec
        lda s1
        sbc v
        sta dst
        lda s1+1
        sbc #0
        sta dst+1
.endmacro

; the row's descriptor from c, lw, g, p; compare; draw if changed
.proc do_row
        lda ll                  ; lw (pixels), the shoulder, the far widths
        sta lw
        tax
        lda shtab,x
        sta sh
        lda fwl
        jsr farw
        sta fwlp
        lda fwr
        jsr farw
        sta fwrp
        lda mode                ; the road's edges
        and #M_THREE
        bne three
        SUB8 lft, cl, lw        ; c - lw .. c + lw
        ADD8 rgt, cl, lw
        jmp have
three:  lda lw                  ; c - lw/8 .. c + 3 lw
        lsr a
        lsr a
        lsr a
        sta tt
        SUB8 lft, cl, tt
        lda lw
        asl a
        sta tt
        lda #0
        rol a
        sta tt+1
        clc
        lda tt
        adc lw
        sta tt
        lda tt+1
        adc #0
        sta tt+1
        clc
        lda cl
        adc tt
        sta rgt
        lda ch
        adc tt+1
        sta rgt+1
have:   lda lft
        sta ex
        lda lft+1
        sta ex+1
        CLAMP
        sta desc+2
        SUB8 ex, lft, sh
        CLAMP
        sta desc+1
        SUB8 ex, ex, fwlp
        CLAMP
        sta desc+0
        lda rgt
        sta ex
        lda rgt+1
        sta ex+1
        CLAMP
        sta desc+3
        ADD8 ex, rgt, sh
        CLAMP
        sta desc+4
        ADD8 ex, ex, fwrp
        CLAMP
        sta desc+5
        lda sides
        sta desc+6
        lda lw                  ; marks 1 + lw/16 pixels wide (the path 1 + lw/8)
        lsr a
        lsr a
        lsr a
        lsr a
        clc
        adc #1
        sta mw
        lda mode
        and #M_THREE
        beq one
        ADD8 ex, cl, lw         ; freeway: c + lw, c + 2 lw
        ldy #7
        jsr mark
        ADD8 ex, ex, lw
        ldy #9
        jsr mark
        jmp path
one:    lda mode
        and #M_GHOST
        beq noghost
        clc                     ; the old centre line, and the old edge line
        lda cl
        adc gl
        sta ex
        lda ch
        adc gh
        sta ex+1
        ldy #7
        jsr mark
        clc
        lda rgt
        adc gl
        sta ex
        lda rgt+1
        adc gh
        sta ex+1
        ldy #9
        jsr mark
        jmp path
noghost:
        lda #$FF
        sta desc+9
        lda #0
        sta desc+10
        lda mode
        and #M_NOLINE
        bne noline
        lda cl                  ; the centre line
        sta ex
        lda ch
        sta ex+1
        ldy #7
        jsr mark
        jmp path
noline: lda #$FF
        sta desc+7
        lda #0
        sta desc+8
path:   lda mode
        and #M_PATH
        bne pathy
        lda #$FF
        sta desc+11
        lda #0
        sta desc+12
        jmp cmpr
pathy:  lda lw
        lsr a
        lsr a
        lsr a
        clc
        adc #1
        sta mw
        clc
        lda cl
        adc pl
        sta ex
        lda ch
        adc ph
        sta ex+1
        ldy #11
        jsr mark
cmpr:   ldx row
        lda prvlo,x
        sta ptr2
        lda prvhi,x
        sta ptr2+1
        ; unchanged if the edges and the marks' places are within a pixel of
        ; what is drawn (the boundaries' rounding makes rows wobble by one
        ; as the bands move) and the rest is the same
        ldy #DSZ - 1
@c:     lda tol,y
        bne @t
        lda desc,y
        cmp (ptr2),y
        bne changed
        dey
        bpl @c
        rts
@t:     lda desc,y
        sec
        sbc (ptr2),y
        clc
        adc #1
        cmp #3
        bcs changed
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

; far side width code A (0, sh/2, sh, 2sh) -> A (clamped 255)
.proc farw
        cmp #0
        beq out
        cmp #2
        beq sh1
        bcs sh2
        lda sh
        lsr a
        rts
sh1:    lda sh
        rts
sh2:    lda sh
        asl a
        bcc out
        lda #255
out:    rts
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
        lda yf+1                ; yf >= ymax: behind a crest
        bmi vis
        bne next
        lda yf
        cmp ymax
        bcs next
vis:    lda yf                  ; yf >= yn: not a band
        cmp yn
        lda yf+1
        sbc yn+1
        bvc :+
        eor #$80
:       bpl next
        lda yf+1                ; top = max(yf, 0), bot = min(yn, ymax) - 1
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

; 12.4 (A lo, X hi) -> 16.8 at cf+Y (f, l, h)
.proc to168
        sta tt
        stx tt+1
        asl a
        asl a
        asl a
        asl a
        sta cf,y
        lda tt
        lsr a
        lsr a
        lsr a
        lsr a
        sta t0
        lda tt+1
        asl a
        asl a
        asl a
        asl a
        ora t0
        sta cf+1,y
        lda tt+1
        cmp #$80
        ror a
        cmp #$80
        ror a
        cmp #$80
        ror a
        cmp #$80
        ror a
        sta cf+2,y
        rts
.endproc

.proc do_band
        lda nb
        asl a
        sta xs
        tay
        ldx #0                  ; qv = c, lw, g, p (12.4)
@q:     lda _bd_c,y
        sta qv,x
        lda _bd_c+1,y
        sta qv+1,x
        tya
        clc
        adc #NB * 2
        tay
        inx
        inx
        cpx #8
        bne @q
        ldy xs
        lda _bd_fw,y
        sta fwl
        lda _bd_fw+1,y
        sta fwr
        ldx nb
        lda _bd_mode,x
        sta mode
        txa                     ; the side colours, packed
        asl a
        asl a
        tax
        lda _bd_side,x
        asl a
        asl a
        ora _bd_side+1,x
        asl a
        asl a
        ora _bd_side+2,x
        asl a
        asl a
        ora _bd_side+3,x
        sta sides
        sec                     ; rows = yn - yf, skip = yn - 1 - bot (clamped)
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
:       lda #0
        ldx #7
@z:     sta sv,x
        dex
        bpl @z
        lda bot                 ; one row at the bottom boundary: no steps
        cmp top
        bne steps
        lda skip
        bne steps
        jmp conv
steps:  lda rows
        sta _dn
        ldx #0                  ; sv = (next - this) / rows for c, lw, g, p
@s:     stx tt+1                ;   (0 when they don't change)
        lda qoff,x
        clc
        adc xs
        tay
        sec
        lda _bd_c+2,y
        sbc _bd_c,y
        sta t0
        lda _bd_c+3,y
        sbc _bd_c+1,y
        tax
        ora t0
        beq @zero
        lda t0
        jsr _divn
        ldy tt+1
        sta sv,y
        txa
        sta sv+1,y
@zero:  ldx tt+1
        inx
        inx
        cpx #8
        bne @s
        lda skip                ; the rows below the screen: values += steps * skip
        beq conv
        sta _mk
        ldy #0
@sk:    sty xs
        lda sv,y
        ldx sv+1,y
        jsr _mult
        ldy xs
        clc
        adc qv,y
        sta qv,y
        txa
        adc qv+1,y
        sta qv+1,y
        iny
        iny
        cpy #8
        bne @sk
conv:   ; to 16.8: lw first (its third byte lands on gf), then c, g, p
        lda qv+2
        ldx qv+3
        ldy #lf - cf
        jsr to168
        lda qv
        ldx qv+1
        ldy #0
        jsr to168
        lda mode
        and #M_GHOST
        beq :+
        lda qv+4
        ldx qv+5
        ldy #gf - cf
        jsr to168
:       lda mode
        and #M_PATH
        beq :+
        lda qv+6
        ldx qv+7
        ldy #pf - cf
        jsr to168
:       lda sv+2
        ldx sv+3
        ldy #dlf - cf
        jsr to168
        lda sv
        ldx sv+1
        ldy #dcf - cf
        jsr to168
        lda mode
        and #M_GHOST
        beq :+
        lda sv+4
        ldx sv+5
        ldy #dgf - cf
        jsr to168
:       lda mode
        and #M_PATH
        beq :+
        lda sv+6
        ldx sv+7
        ldy #dpf - cf
        jsr to168
:       lda bot                 ; the rows, bottom to top
        sta row
rl:     lda row                 ; (the near half: the second of each pair shows the first)
        cmp #DBL
        bcc :+
        and #1
        bne :++
:       jsr do_row
:
        lda row
        cmp top
        beq out
        dec row
        clc                     ; c += dc
        lda cf
        adc dcf
        sta cf
        lda cl
        adc dcl
        sta cl
        lda ch
        adc dch
        sta ch
        clc                     ; lw += dlw
        lda lf
        adc dlf
        sta lf
        lda ll
        adc dll
        sta ll
        lda mode
        and #M_GHOST
        beq :+
        clc                     ; g += dg
        lda gf
        adc dgf
        sta gf
        lda gl
        adc dgl
        sta gl
        lda gh
        adc dgh
        sta gh
:       lda mode
        and #M_PATH
        beq :+
        clc                     ; p += dp
        lda pf
        adc dpf
        sta pf
        lda pl
        adc dpl
        sta pl
        lda ph
        adc dph
        sta ph
:       jmp rl
out:    rts
.endproc
