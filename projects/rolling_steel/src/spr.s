; spr.s - the sprites: players 0-3 and the missiles as a fifth player (the
; marble's shadow), single-line resolution at $C800 (rolling_steel.cfg).
; The game sets spr_x/y/h and a shape pointer per sprite, and calls
; spr_commit() just after the vertical blank, when the view those positions
; were worked out against has just been put on screen (disp.s).

        .export _spr_x, _spr_y, _spr_h, _spr_lo, _spr_hi, _spr_commit, _spr_init
        .importzp ptr1, ptr2, tmp1

PMBASE  = $D407
GRACTL  = $D01D
HPOSP0  = $D000
HPOSM0  = $D004
SIZEP0  = $D008
SIZEM   = $D00C
PM      = $C800

.segment "BSS"
_spr_x: .res 5
_spr_y: .res 5                  ; the top scan line
_spr_h: .res 5                  ; 0: hidden
_spr_lo: .res 5
_spr_hi: .res 5
old_y:  .res 5
old_h:  .res 5

.segment "RODATA"
base_hi: .byte >(PM + $400), >(PM + $500), >(PM + $600), >(PM + $700), >(PM + $300)

.segment "CODE"

_spr_init:
        lda     #0
        tax
@c:     sta     PM + $300,x
        sta     PM + $400,x
        sta     PM + $500,x
        sta     PM + $600,x
        sta     PM + $700,x
        inx
        bne     @c
        ldx     #4
@z:     sta     old_h,x
        sta     _spr_h,x
        sta     SIZEP0,x
        dex
        bpl     @z
        sta     SIZEM
        lda     #>PM
        sta     PMBASE
        lda     #3
        sta     GRACTL
        rts

_spr_commit:
        ldx     #4
@n:     lda     #0
        sta     ptr1
        lda     base_hi,x
        sta     ptr1+1
        ; the old shape off
        lda     old_h,x
        beq     @new
        sta     tmp1
        ldy     old_y,x
        lda     #0
@clr:   sta     (ptr1),y
        iny
        dec     tmp1
        bne     @clr
@new:   lda     _spr_h,x
        sta     old_h,x
        beq     @next
        sta     tmp1
        lda     _spr_y,x
        sta     old_y,x
        lda     _spr_lo,x
        sta     ptr2
        lda     _spr_hi,x
        sta     ptr2+1
        ; ptr1 += y, so the shape and the column share Y
        lda     _spr_y,x
        sta     ptr1
        ldy     #0
@cp:    lda     (ptr2),y
        sta     (ptr1),y
        iny
        cpy     tmp1
        bne     @cp
@next:  dex
        bpl     @n
        ldx     #3
@h:     lda     _spr_x,x
        sta     HPOSP0,x
        dex
        bpl     @h
        lda     _spr_x+4        ; the shadow: missiles 0 and 1 side by side
        sta     HPOSM0
        clc
        adc     #2
        sta     HPOSM0+1
        rts
