; unwrap.s - view.c's unwrap in assembly (view.c has the C it follows):
; the maze round the chomper laid onto the 15 x 9 grid, drawn into a map
; as it goes, a cell as 3 x 3 characters. The middle row out from the
; centre both ways, then each column up and down from it; each step
; carries the cell's orientation (bits 0-1: the screen direction of exit
; 0; bit 2: exits run the other way). Within a face the orientation stays
; (the cube's grid is the same across a face), so only a step onto another
; face works it out. A cell's first place and orientation go into gpos /
; grot (near the cube's corners a cell can show twice).
;
;   uw_center, uw_rot, uw_map, uw_gpos, uw_grot: set by view.c; jsr _unwrap_asm

        .export _unwrap_asm, _uw_center, _uw_rot, _uw_map, _uw_gpos, _uw_grot, _uw_open, _uw_content
        .export _uw_wallmap, _uw_ch
        .import _mz_nbl, _mz_nbh, _mz_rev, _mz_face
        .importzp ptr1, ptr2, ptr3, tmp1, tmp2

GW      = 15
GH      = 9
CX      = 7
CY      = 4
MAPW    = 48
ROW3    = 3 * MAPW

.segment "DATA"
_uw_center: .word 0
_uw_rot:    .byte 0
_uw_map:    .word 0
_uw_gpos:   .word 0
_uw_grot:   .word 0
_uw_open:   .word 0             ; &g.open
_uw_content: .word 0            ; view.c's content[]: 0 none, 1 crumb, 2 key, 3 nest
.segment "BSS"
_uw_wallmap: .res 128           ; [orientation * 16 + closed exits] -> walls by screen side
_uw_ch:  .res 128               ; [part * 16 + walls]: the characters of the 8 outer parts
rowcl:  .res GW
rowch:  .res GW
rowr:   .res GW
cl:     .res 1                  ; the walk's cell and orientation
ch:     .res 1
cr:     .res 1
sdir:   .res 1
gx:     .res 1
gy:     .res 1
k:      .res 1
back:   .res 1
face:   .res 1
pl:     .res 1                  ; the map place's address
ph:     .res 1
.segment "RODATA"
; the exit towards screen direction s for orientation r: [r * 4 + s]
ktab:
        .repeat 8, r
        .repeat 4, s
        .if r & 4
        .byte   ((r & 3) - s) & 3
        .else
        .byte   (s - (r & 3)) & 3
        .endif
        .endrepeat
        .endrepeat

.segment "CODE"

; ptr2 = table + cell
.macro  cellp tab
        lda     cl
        clc
        adc     #<tab
        sta     ptr2
        lda     ch
        adc     #>tab
        sta     ptr2+1
.endmacro

; one step from (cl:ch, cr) towards screen direction sdir
step:   lda     cr
        asl
        asl
        ora     sdir
        tax
        lda     ktab,x
        sta     k
        cellp   _mz_face
        ldy     #0
        lda     (ptr2),y
        and     #7
        sta     face
        ; ptr1 = nbl + c * 4 + k
        lda     cl
        sta     ptr1
        lda     ch
        sta     ptr1+1
        asl     ptr1
        rol     ptr1+1
        asl     ptr1
        rol     ptr1+1
        lda     ptr1
        clc
        adc     k
        adc     #<_mz_nbl
        sta     ptr1
        lda     ptr1+1
        adc     #>_mz_nbl
        sta     ptr1+1
        cellp   _mz_nbh
        lda     (ptr2),y        ; the 9th bits
        ldx     k
        beq     @h0
@hs:    lsr
        dex
        bne     @hs
@h0:    and     #1
        sta     tmp1
        cellp   _mz_rev
        lda     (ptr2),y        ; (the exit back, if the face changes)
        sta     tmp2
        lda     (ptr1),y
        sta     cl
        lda     tmp1
        sta     ch
        cellp   _mz_face
        lda     (ptr2),y
        sta     tmp1
        and     #7
        cmp     face
        bne     @edge
        rts                     ; the same face: the same orientation
@edge:  lda     tmp2            ; back = (rev >> 2k) & 3
        ldx     k
        beq     @r0
@rs:    lsr
        lsr
        dex
        bne     @rs
@r0:    and     #3
        sta     back
        lda     tmp1
        bmi     @nflip
        lda     sdir            ; nr = (s + 2 - back) & 3
        clc
        adc     #2
        sec
        sbc     back
        and     #3
        sta     cr
        rts
@nflip: lda     sdir            ; nr = ((s + 2 + back) & 3) | 4
        clc
        adc     #2
        clc
        adc     back
        and     #3
        ora     #4
        sta     cr
        rts

; draw (cl:ch, cr) at the place pl:ph (row gy, column gx) and record its first place
place:  lda     pl
        sta     ptr3
        lda     ph
        sta     ptr3+1
        lda     cl
        clc
        adc     _uw_gpos
        sta     ptr2
        lda     ch
        adc     _uw_gpos+1
        sta     ptr2+1
        ldy     #0
        lda     (ptr2),y
        cmp     #$FF
        bne     @drawn
        lda     gy              ; y * 16 + x
        asl
        asl
        asl
        asl
        ora     gx
        sta     (ptr2),y
        lda     cl
        clc
        adc     _uw_grot
        sta     ptr2
        lda     ch
        adc     _uw_grot+1
        sta     ptr2+1
        lda     cr
        sta     (ptr2),y
@drawn: ; closed exits -> walls by screen side
        lda     ch
        lsr
        lda     cl
        ror
        clc
        adc     _uw_open
        sta     ptr2
        lda     _uw_open+1
        adc     #0
        sta     ptr2+1
        lda     (ptr2),y
        tax
        lda     cl
        lsr
        txa
        bcc     @even
        lsr
        lsr
        lsr
        lsr
@even:  and     #15
        eor     #15
        sta     tmp1
        lda     cr
        asl
        asl
        asl
        asl
        ora     tmp1
        tax
        lda     _uw_wallmap,x
        sta     tmp1            ; walls
        ; the middle: 24 + content
        lda     cl
        clc
        adc     _uw_content
        sta     ptr2
        lda     ch
        adc     _uw_content+1
        sta     ptr2+1
        lda     (ptr2),y
        clc
        adc     #24
        ldy     #MAPW + 1
        sta     (ptr3),y
        ; the 8 outer parts: _uw_ch[part * 16 + walls] (unrolled)
        ldx     tmp1
        ldy     #0
        lda     _uw_ch+0*16,x
        sta     (ptr3),y
        iny
        lda     _uw_ch+1*16,x
        sta     (ptr3),y
        iny
        lda     _uw_ch+2*16,x
        sta     (ptr3),y
        ldy     #MAPW
        lda     _uw_ch+3*16,x
        sta     (ptr3),y
        ldy     #MAPW+2
        lda     _uw_ch+4*16,x
        sta     (ptr3),y
        ldy     #2*MAPW
        lda     _uw_ch+5*16,x
        sta     (ptr3),y
        iny
        lda     _uw_ch+6*16,x
        sta     (ptr3),y
        iny
        lda     _uw_ch+7*16,x
        sta     (ptr3),y
        rts

; pl:ph = map + gy * 144 + gx * 3
mapat:  lda     gy
        sta     pl
        lda     #0
        sta     ph
        asl     pl              ; * 16
        rol     ph
        asl     pl
        rol     ph
        asl     pl
        rol     ph
        asl     pl
        rol     ph
        lda     pl
        sta     tmp1
        lda     ph
        sta     tmp2
        asl     pl              ; * 128
        rol     ph
        asl     pl
        rol     ph
        asl     pl
        rol     ph
        lda     pl
        clc
        adc     tmp1
        sta     pl
        lda     ph
        adc     tmp2
        sta     ph              ; * 144
        lda     gx
        asl
        adc     gx              ; * 3 (gx < 16: no carry out)
        clc
        adc     pl
        sta     pl
        bcc     @n
        inc     ph
@n:     lda     pl
        clc
        adc     _uw_map
        sta     pl
        lda     ph
        adc     _uw_map+1
        sta     ph
        rts

_unwrap_asm:
        ; gpos[] to $FF (486 bytes)
        lda     _uw_gpos
        sta     ptr1
        lda     _uw_gpos+1
        sta     ptr1+1
        lda     #$FF
        ldy     #0
@f:     sta     (ptr1),y
        iny
        bne     @f
        inc     ptr1+1
        ldy     #486 - 256 - 1
@f2:    sta     (ptr1),y
        dey
        cpy     #$FF
        bne     @f2
        ; the middle row
        lda     _uw_center
        sta     rowcl+CX
        lda     _uw_center+1
        sta     rowch+CX
        lda     _uw_rot
        sta     rowr+CX
        ldx     #CX
@right: inx
        cpx     #GW
        beq     @left0
        lda     rowcl-1,x
        sta     cl
        lda     rowch-1,x
        sta     ch
        lda     rowr-1,x
        sta     cr
        lda     #1
        sta     sdir
        stx     gx
        jsr     step
        ldx     gx
        lda     cl
        sta     rowcl,x
        lda     ch
        sta     rowch,x
        lda     cr
        sta     rowr,x
        jmp     @right
@left0: ldx     #CX
@left:  dex
        bmi     @cols
        lda     rowcl+1,x
        sta     cl
        lda     rowch+1,x
        sta     ch
        lda     rowr+1,x
        sta     cr
        lda     #3
        sta     sdir
        stx     gx
        jsr     step
        ldx     gx
        lda     cl
        sta     rowcl,x
        lda     ch
        sta     rowch,x
        lda     cr
        sta     rowr,x
        jmp     @left
@cols:  ; the centre first (its place wins), then each column
        lda     #CX
        sta     gx
        lda     #CY
        sta     gy
        jsr     mapat
        lda     _uw_center
        sta     cl
        lda     _uw_center+1
        sta     ch
        lda     _uw_rot
        sta     cr
        jsr     place
        lda     #0
        sta     gx
@col:   lda     #CY
        sta     gy
        jsr     mapat
        ldx     gx
        lda     rowcl,x
        sta     cl
        lda     rowch,x
        sta     ch
        lda     rowr,x
        sta     cr
        jsr     place
@up:    dec     gy
        bmi     @down0
        lda     #0
        sta     sdir
        jsr     step
        lda     pl
        sec
        sbc     #ROW3
        sta     pl
        bcs     @u1
        dec     ph
@u1:    jsr     place
        jmp     @up
@down0: lda     #CY
        sta     gy
        jsr     mapat
        ldx     gx
        lda     rowcl,x
        sta     cl
        lda     rowch,x
        sta     ch
        lda     rowr,x
        sta     cr
@down:  inc     gy
        lda     gy
        cmp     #GH
        beq     @next
        lda     #2
        sta     sdir
        jsr     step
        lda     pl
        clc
        adc     #ROW3
        sta     pl
        bcc     @d1
        inc     ph
@d1:    jsr     place
        jmp     @down
@next:  inc     gx
        lda     gx
        cmp     #GW
        beq     @done
        jmp     @col
@done:  rts
