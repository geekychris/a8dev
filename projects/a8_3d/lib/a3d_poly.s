; a3d_poly.s - convex polygons: rejection, gather + back-face test, the
; edge walker and the span loop (see the header comment in a3d.h).

        .importzp a3d_bsel, a3d_p, a3d_q, a3d_t, a3d_m
        .import   _a3d_px, _a3d_py, mul_t_p, a3d_rec_lo, a3d_rec_hi
        .import   tx, pl, ph, r0, r1, r2, mulset_s
        .import   _a3d_sx, _a3d_sy, _a3d_vclip, _a3d_h, _a3d_poly_clip
        .import   _a3d_ylo, _a3d_yhi, _a3d_pat_e, _a3d_pat_o
        .import   lbyte, keepl, rbyte, keepr, dirty_add, ry0, ry1, rb0, rb1
        .export   _a3d_fill, _a3d_poly, _a3d_face_, _a3d_gather_, _a3d_front_, _a3d_fn
        .export   _a3d_el, _a3d_er

        .include "a3d_span.inc"

.segment "BSS"
_a3d_el:    .res 97          ; left edge per line
_a3d_er:    .res 97          ; right edge per line
_a3d_fn:    .res 1
n2:     .res 1               ; 2 * vertex count
ei:     .res 1
ia:     .res 1               ; edge top / bottom vertex (x2)
ib:     .res 1
dyc:    .res 1
sgn2:   .res 1
bx0:    .res 2               ; polygon bounding box (int16)
bx1:    .res 2
by0:    .res 2
by1:    .res 2
fi:     .res 1
fi2:    .res 1

.segment "CODE"
; ------------------------------------------------------------------ polygons
; signed 16-bit compare of the int16 at A_lo/A_hi with mem: N = (a < mem)
.macro  SLT     lo, hi, mlo, mhi
        lda     lo
        cmp     mlo
        lda     hi
        sbc     mhi
        bvc     :+
        eor     #$80
:
.endmacro

; void a3d_poly(uint8 n): reject, clip in C when crossing top/bottom, fill
_a3d_poly:
        sta     _a3d_fn
        asl     a
        sta     n2
        cmp     #6
        bcs     :+
        rts
:       lda     _a3d_px
        sta     bx0
        sta     bx1
        lda     _a3d_px+1
        sta     bx0+1
        sta     bx1+1
        lda     _a3d_py
        sta     by0
        sta     by1
        lda     _a3d_py+1
        sta     by0+1
        sta     by1+1
        ldx     #2
@bb:    SLT     {_a3d_px,x}, {_a3d_px+1,x}, bx0, bx0+1
        bpl     :+
        lda     _a3d_px,x
        sta     bx0
        lda     _a3d_px+1,x
        sta     bx0+1
:       SLT     bx1, bx1+1, {_a3d_px,x}, {_a3d_px+1,x}
        bpl     :+
        lda     _a3d_px,x
        sta     bx1
        lda     _a3d_px+1,x
        sta     bx1+1
:       SLT     {_a3d_py,x}, {_a3d_py+1,x}, by0, by0+1
        bpl     :+
        lda     _a3d_py,x
        sta     by0
        lda     _a3d_py+1,x
        sta     by0+1
:       SLT     by1, by1+1, {_a3d_py,x}, {_a3d_py+1,x}
        bpl     :+
        lda     _a3d_py,x
        sta     by1
        lda     _a3d_py+1,x
        sta     by1+1
:       inx
        inx
        cpx     n2
        beq     :+
        jmp     @bb
:       ; reject: y1 <= 0, x1 <= 0, y0 >= h, x0 >= 160
        lda     by1+1
        bmi     @out
        bne     :+
        lda     by1
        beq     @out
:       lda     bx1+1
        bmi     @out
        bne     :+
        lda     bx1
        beq     @out
:       lda     by0+1
        bmi     @clip               ; y0 < 0: clip
        bne     @out
        lda     by0
        cmp     _a3d_h
        bcs     @out
        lda     bx0+1
        bmi     :+
        bne     @out
        lda     bx0
        cmp     #160
        bcs     @out
:       lda     by1+1               ; y1 > h: clip
        bne     @clip
        lda     _a3d_h
        cmp     by1
        bcc     @clip
        lda     _a3d_fn
        jmp     _a3d_fill
@clip:  lda     _a3d_fn
        jmp     _a3d_poly_clip
@out:   rts

; uint8 a3d_gather_(const uint8 *idx): a3d_fn vertices into a3d_px/py.
; 0 = back-facing, 1 = front, 2 = a vertex is behind the near plane
_a3d_gather_:
        sta     a3d_q
        stx     a3d_q+1
        ldy     #0
        sty     fi2
@g:     lda     (a3d_q),y
        tax
        lda     _a3d_vclip,x
        bne     @nearc
        sty     fi
        txa
        asl     a
        tax
        ldy     fi2
        lda     _a3d_sx,x
        sta     _a3d_px,y
        lda     _a3d_sx+1,x
        sta     _a3d_px+1,y
        lda     _a3d_sy,x
        sta     _a3d_py,y
        lda     _a3d_sy+1,x
        sta     _a3d_py+1,y
        iny
        iny
        sty     fi2
        ldy     fi
        iny
        cpy     _a3d_fn
        bne     @g
        jsr     front
        lda     #0
        tax
        rol     a
        rts
@nearc: lda     #2
        ldx     #0
        rts

; uint8 a3d_front_(void): 1 if a3d_px/py 0, 1, 2 turn clockwise
_a3d_front_:
        jsr     front
        lda     #0
        tax
        rol     a
        rts

; uint8 a3d_face_(const uint8 *idx): gather, cull, draw; 0/1 drawn, 2 = near
_a3d_face_:
        jsr     _a3d_gather_
        cmp     #1
        bne     :+
        lda     _a3d_fn
        jsr     _a3d_poly
        lda     #1
:       ldx     #0
        rts

; carry set if px/py 0, 1, 2 turn clockwise on the screen (cross > 0)
front:
        sec
        lda     _a3d_px+2
        sbc     _a3d_px
        sta     dax
        lda     _a3d_px+3
        sbc     _a3d_px+1
        sta     dax+1
        sec
        lda     _a3d_py+2
        sbc     _a3d_py
        sta     day
        lda     _a3d_py+3
        sbc     _a3d_py+1
        sta     day+1
        sec
        lda     _a3d_px+4
        sbc     _a3d_px
        sta     dbx
        lda     _a3d_px+5
        sbc     _a3d_px+1
        sta     dbx+1
        sec
        lda     _a3d_py+4
        sbc     _a3d_py
        sta     dby
        lda     _a3d_py+5
        sbc     _a3d_py+1
        sta     dby+1
@fit:   ldx     #6                  ; every delta in -128..127?
@f1:    lda     dax,x
        asl     a                   ; carry = sign of the low byte
        lda     dax+1,x
        adc     #0                  ; high + sign: 0 if it fits
        bne     @shrink
        dex
        dex
        bpl     @f1
        ; cross = ax*by - ay*bx
        lda     dax
        jsr     mulset_s
        lda     dby
        eor     #$80
        tay
        sec
        lda     (a3d_m),y
        sbc     (a3d_m+2),y
        sta     dp1
        lda     (a3d_m+4),y
        sbc     (a3d_m+6),y
        sta     dp1+1
        lda     day
        jsr     mulset_s
        lda     dbx
        eor     #$80
        tay
        sec
        lda     (a3d_m),y
        sbc     (a3d_m+2),y
        sta     dax                 ; (reuse) P2
        lda     (a3d_m+4),y
        sbc     (a3d_m+6),y
        sta     dax+1
        ; front if P1 > P2, i.e. P2 < P1
        SLT     dax, dax+1, dp1, dp1+1
        bmi     @yes
        clc
        rts
@yes:   sec
        rts
@shrink:
        ldx     #6
@s1:    lda     dax+1,x
        cmp     #$80
        ror     dax+1,x
        ror     dax,x
        dex
        dex
        bpl     @s1
        jmp     @fit

; void a3d_fill(uint8 n)
_a3d_fill:
        asl     a
        sta     n2
        cmp     #6
        bcs     :+
        rts
:       lda     _a3d_py
        sta     ymin
        sta     ymax
        lda     _a3d_px
        sta     bx0
        sta     bx1
        lda     _a3d_px+1
        sta     bx0+1
        sta     bx1+1
        ldx     #2
@mm:    lda     _a3d_py,x
        cmp     ymin
        bcs     :+
        sta     ymin
:       cmp     ymax
        bcc     :+
        sta     ymax
:       SLT     {_a3d_px,x}, {_a3d_px+1,x}, bx0, bx0+1
        bpl     :+
        lda     _a3d_px,x
        sta     bx0
        lda     _a3d_px+1,x
        sta     bx0+1
:       SLT     bx1, bx1+1, {_a3d_px,x}, {_a3d_px+1,x}
        bpl     :+
        lda     _a3d_px,x
        sta     bx1
        lda     _a3d_px+1,x
        sta     bx1+1
:       inx
        inx
        cpx     n2
        bne     @mm
        lda     ymin
        cmp     ymax
        bne     :+
        rts
:       ; ---- dirty rectangle: bytes of clamp(x0), clamp(x1 - 1)
        sta     ry0
        lda     ymax
        sta     ry1
        lda     bx0+1
        bmi     @x0z
        bne     @x0m
        ldx     bx0
        cpx     #160
        bcc     :+
@x0m:   ldx     #159
        bne     :+
@x0z:   ldx     #0
:       lda     lbyte,x
        sta     rb0
        lda     bx1+1
        bmi     @x1z
        bne     @x1m
        ldx     bx1
        cpx     #160
        bcc     :+
@x1m:   ldx     #159
        bne     :+
@x1z:   ldx     #0
:       lda     lbyte,x
        sta     rb1
        jsr     dirty_add
        ; ---- edges
        lda     #0
        sta     ei
@edge:  ldx     ei
        txa
        clc
        adc     #2
        cmp     n2
        bcc     :+
        lda     #0
:       tay                         ; X = i, Y = j (both x2)
        lda     _a3d_py,y
        cmp     _a3d_py,x
        beq     @next
        bcc     @up
        stx     ia                  ; down: right side, i at the top
        sty     ib
        lda     #<_a3d_er
        ldx     #>_a3d_er
        jmp     @walk
@up:    sty     ia                  ; up: left side, j at the top
        stx     ib
        lda     #<_a3d_el
        ldx     #>_a3d_el
@walk:  jsr     walk
@next:  lda     ei
        clc
        adc     #2
        sta     ei
        cmp     n2
        bne     @edge
        ; ---- spans
        ldy     ymin
fl_line:  sty     yy
        lda     _a3d_er,y
        cmp     _a3d_el,y
        beq     :+
        bcs     :++
:       jmp     fl_skip
:       tax                         ; R
        lda     keepr,x
        sta     nmr
        lda     rbyte,x
        sta     rb
        ldx     _a3d_el,y           ; L
        lda     keepl,x
        sta     nml
        lda     lbyte,x
        sta     lb
        lda     _a3d_ylo,y
        sta     a3d_p
        lda     _a3d_yhi,y
        ora     a3d_bsel
        sta     a3d_p+1
        tya
        lsr     a
        lda     _a3d_pat_e
        bcc     :+
        lda     _a3d_pat_o
:       sta     pat
        SPAN    fl_skip
fl_skip:  ldy     yy
        iny
        cpy     ymax
        beq     :+
        jmp     fl_line
:       rts

; walk the edge ia (top) -> ib into the table at A/X
walk:
        sta     @fs+1
        stx     @fs+2
        sta     @vs+1
        stx     @vs+2
        sta     @ss+1
        stx     @ss+2
        ldx     ia
        ldy     ib
        lda     _a3d_py,y
        sec
        sbc     _a3d_py,x
        sta     dyc
        tay
        lda     a3d_rec_lo,y
        sta     pl
        lda     a3d_rec_hi,y
        sta     ph
        ldy     ib
        sec                         ; dx = xb - xa
        lda     _a3d_px,y
        sbc     _a3d_px,x
        sta     tx
        lda     _a3d_px+1,y
        sbc     _a3d_px+1,x
        sta     tx+1
        sta     sgn2
        bpl     :+
        sec
        lda     #0
        sbc     tx
        sta     tx
        lda     #0
        sbc     tx+1
        sta     tx+1
:       jsr     mul_t_p             ; r = |dx| * 65536/dy >> 8
        lda     r0
        sta     sl0
        lda     r1
        sta     sl1
        lda     r2
        sta     sl2
        bit     sgn2
        bpl     :+
        sec
        lda     #0
        sbc     sl0
        sta     sl0
        lda     #0
        sbc     sl1
        sta     sl1
        lda     #0
        sbc     sl2
        sta     sl2
:       lda     #$80
        sta     xf
        ldx     ia
        lda     _a3d_px,x
        sta     xl
        lda     _a3d_px+1,x
        sta     xh
        ; fast path: both ends inside 0..160
        bne     @slow
        lda     xl
        cmp     #161
        bcs     @slow
        ldy     ib
        lda     _a3d_px+1,y
        bne     @slow
        lda     _a3d_px,y
        cmp     #161
        bcs     @slow
        lda     _a3d_py,x           ; y range: Y = ya .. yb-1
        tay
        clc
        adc     dyc
        sta     @fe+1
        lda     sl1                 ; vertical edge: the same x on every line
        ora     sl0
        bne     :+
        lda     xl
@vl:
@vs:    sta     $FFFF,y
        iny
        cpy     @fe+1
        bne     @vl
        rts
:       ldx     xl
        clc
@fast:  txa
@fs:    sta     $FFFF,y
        lda     xf
        adc     sl0
        sta     xf
        txa
        adc     sl1
        tax
        iny
@fe:    cpy     #$FF                ; carry clear while y < end
        bne     @fast
        rts
@slow:  ldx     ia
        ldy     _a3d_py,x
        ldx     dyc
@sl:    lda     xh
        bmi     @neg
        bne     @big
        lda     xl
        cmp     #161
        bcc     @ss
@big:   lda     #160
        bne     @ss
@neg:   lda     #0
@ss:    sta     $FFFF,y
        clc
        lda     xf
        adc     sl0
        sta     xf
        lda     xl
        adc     sl1
        sta     xl
        lda     xh
        adc     sl2
        sta     xh
        iny
        dex
        bne     @sl
        rts

