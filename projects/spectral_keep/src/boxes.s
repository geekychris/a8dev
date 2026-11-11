; boxes.s - world.c's per-axis box set-up in asm (cc65 spent ~1,000 cycles
; on each BODY_BOX through pointers and as much on the copies):
;
;   void box_of(void)        bx_box = the box of the body bx_body
;   void axis_prep(void)     cs_start = the box of bx_body, cs_swept = it
;                            extended by cs_d along cs_axis (0 x, 1 y, 2 z)
;
; A Body starts px py pz sx sy sz (fix); a Box is x0 y0 z0 x1 y1 z1.

        .export _box_of, _axis_prep, _bx_body, _bx_box
        .import _cs_start, _cs_swept, _cs_axis, _cs_d
        .importzp ptr1, ptr2

.segment "BSS"
_bx_body: .res 2
_bx_box:  .res 2

.segment "HICODE"

; the box of (ptr1) into (ptr2)
; x0 = px - (sx >> 1), x1 = x0 + sx (z the same, +4); y0 = py, y1 = py + sy
.macro  axis_xz off
        ldy     #off + 7        ; sx >> 1 -> A:X (hi, lo)
        lda     (ptr1),y
        lsr
        tax                     ; (hi)
        dey
        lda     (ptr1),y
        ror
        sta     t_lo
        stx     t_hi
        ldy     #off            ; x0 = px - that
        sec
        lda     (ptr1),y
        sbc     t_lo
        sta     (ptr2),y
        sta     t_lo
        iny
        lda     (ptr1),y
        sbc     t_hi
        sta     (ptr2),y
        sta     t_hi
        ldy     #off + 6        ; x1 = x0 + sx
        clc
        lda     (ptr1),y
        adc     t_lo
        sta     (ptr2),y
        iny
        lda     (ptr1),y
        adc     t_hi
        sta     (ptr2),y
.endmacro

box_ptr:
        axis_xz 0
        axis_xz 4
        ldy     #2              ; y0 = py
        lda     (ptr1),y
        sta     (ptr2),y
        sta     t_lo
        iny
        lda     (ptr1),y
        sta     (ptr2),y
        sta     t_hi
        ldy     #8              ; y1 = py + sy
        clc
        lda     (ptr1),y
        adc     t_lo
        sta     (ptr2),y
        iny
        lda     (ptr1),y
        adc     t_hi
        sta     (ptr2),y
        rts

_box_of:
        lda     _bx_body
        sta     ptr1
        lda     _bx_body+1
        sta     ptr1+1
        lda     _bx_box
        sta     ptr2
        lda     _bx_box+1
        sta     ptr2+1
        jmp     box_ptr

_axis_prep:
        lda     _bx_body
        sta     ptr1
        lda     _bx_body+1
        sta     ptr1+1
        lda     #<_cs_start
        sta     ptr2
        lda     #>_cs_start
        sta     ptr2+1
        jsr     box_ptr
        ldx     #11
@c:     lda     _cs_start,x
        sta     _cs_swept,x
        dex
        bpl     @c
        lda     _cs_axis        ; the offset: axis * 2, + 6 when d > 0
        asl
        tax
        lda     _cs_d+1
        bmi     @neg
        txa
        clc
        adc     #6
        tax
@neg:   clc
        lda     _cs_swept,x
        adc     _cs_d
        sta     _cs_swept,x
        lda     _cs_swept+1,x
        adc     _cs_d+1
        sta     _cs_swept+1,x
        rts

.segment "BSS"
t_lo:   .res 1
t_hi:   .res 1
