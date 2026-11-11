; boxes.s - world.c's per-axis box set-up in asm (cc65 spent ~1,000 cycles
; on each BODY_BOX through pointers and as much on the copies):
;
;   void box_of(void)        bx_box = the box of the body bx_body
;   void axis_prep(void)     cs_start = the box of bx_body, cs_swept = it
;                            extended by cs_d along cs_axis (0 x, 1 y, 2 z)
;
; A Body starts px py pz sx sy sz (fix); a Box is x0 y0 z0 x1 y1 z1.

        .macpack longbranch
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

; uint8_t box_overlap(const Box *a, const Box *b, fix eps)   (world.c's, in asm:
; the hazards' loop calls it up to 42 times a step)
;   a.lo < b.hi - eps && a.hi > b.lo + eps on each axis
        .export _box_overlap, _box_shrink80
        .import popax

.segment "BSS"
eps:    .res 2
tq:     .res 2
k:      .res 1

.segment "HICODE"
_box_overlap:
        sta     eps
        stx     eps+1
        jsr     popax           ; b
        sta     ptr2
        stx     ptr2+1
        jsr     popax           ; a
        sta     ptr1
        stx     ptr1+1
        ldy     #0
@axis:  sty     k
        ; tq = b.hi - eps
        tya
        clc
        adc     #6
        tay
        sec
        lda     (ptr2),y
        sbc     eps
        sta     tq
        iny
        lda     (ptr2),y
        sbc     eps+1
        sta     tq+1
        ; a.lo < tq ?
        ldy     k
        sec
        lda     (ptr1),y
        sbc     tq
        iny
        lda     (ptr1),y
        sbc     tq+1
        bvc     :+
        eor     #$80
:       bpl     @no             ; a.lo >= tq
        ; tq = b.lo + eps
        ldy     k
        clc
        lda     (ptr2),y
        adc     eps
        sta     tq
        iny
        lda     (ptr2),y
        adc     eps+1
        sta     tq+1
        ; tq < a.hi ?
        lda     k
        clc
        adc     #6
        tay
        sec
        lda     tq
        sbc     (ptr1),y
        iny
        lda     tq+1
        sbc     (ptr1),y
        bvc     :+
        eor     #$80
:       bpl     @no             ; tq >= a.hi
        ldy     k
        iny
        iny
        cpy     #6
        bne     @axis
        lda     #1
        ldx     #0
        rts
@no:    lda     #0
        tax
        rts

; void box_shrink80(Box *b): about its centre to 0.4 w each side
; (w / 4 + w / 8 + w / 64 + w / 128, as world.c)
_box_shrink80:
        sta     ptr1
        stx     ptr1+1
        ldy     #0
@ax:    sty     k
        ; w = hi - lo (unsigned)
        tya
        clc
        adc     #6
        tay
        sec
        lda     (ptr1),y
        ldy     k
        sbc     (ptr1),y
        sta     w
        lda     k
        clc
        adc     #7
        tay
        lda     (ptr1),y
        ldy     k
        iny
        sbc     (ptr1),y
        sta     w+1
        ; c = lo + w / 2
        lda     w+1
        lsr
        sta     cc+1
        lda     w
        ror
        ldy     k
        clc
        adc     (ptr1),y
        sta     cc
        iny
        lda     cc+1
        adc     (ptr1),y
        sta     cc+1
        ; hh = w>>2 + w>>3 + w>>6 + w>>7: shift w right, adding at 2, 3, 6, 7
        lda     #0
        sta     hh
        sta     hh+1
        ldx     #1
@sh:    lsr     w+1
        ror     w
        cpx     #2
        beq     @addw
        cpx     #3
        beq     @addw
        cpx     #6
        beq     @addw
        cpx     #7
        bne     @nx
@addw:  clc
        lda     hh
        adc     w
        sta     hh
        lda     hh+1
        adc     w+1
        sta     hh+1
@nx:    inx
        cpx     #8
        bne     @sh
        ; lo = c - hh, hi = c + hh
        ldy     k
        sec
        lda     cc
        sbc     hh
        sta     (ptr1),y
        iny
        lda     cc+1
        sbc     hh+1
        sta     (ptr1),y
        lda     k
        clc
        adc     #6
        tay
        clc
        lda     cc
        adc     hh
        sta     (ptr1),y
        iny
        lda     cc+1
        adc     hh+1
        sta     (ptr1),y
        ldy     k
        iny
        iny
        cpy     #6
        jne     @ax
        rts

.segment "BSS"
w:      .res 2
cc:     .res 2
hh:     .res 2
