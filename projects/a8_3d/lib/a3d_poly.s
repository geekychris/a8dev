; a3d_poly.s - convex polygons: rejection, gather + back-face test, the
; edge walker and the span loop (see the header comment in a3d.h).

        .importzp a3d_bsel, a3d_p, a3d_q, a3d_t, a3d_m
        .import   _a3d_px, _a3d_py, mul_t_p, a3d_rec_lo, a3d_rec_hi
        .import   tx, pl, ph, r0, r1, r2, mulset_s, mulset_u
        .include "a3d_mul.inc"
        .import   _a3d_h
        .import   _a3d_ylo, _a3d_yhi, _a3d_pat_e, _a3d_pat_o
        .import   lbyte, keepl, rbyte, keepr, dirty_add, ry0, ry1, rb0, rb1
        .export   _a3d_fill, _a3d_poly, _a3d_fn
        .export   _a3d_el, _a3d_er

        .include "a3d_span.inc"

.ifdef A3D_LOWTAB
.segment "LOWRAM"
.else
.segment "BSS"
.endif
_a3d_el:    .res 97          ; left edge per line
_a3d_er:    .res 97          ; right edge per line
_a3d_fn:    .res 1
n2:     .res 1               ; 2 * vertex count
ei:     .res 1
ia:     .res 1               ; edge top / bottom vertex (x2)
ib:     .res 1
dyc:    .res 1
wye:    .res 1
wys:    .res 1
wdy:    .res 2
wsk:    .res 2
wp:     .res 3
wq:     .res 1
sgn2:   .res 1
bx0:    .res 2               ; polygon bounding box (int16)
bx1:    .res 2
by0:    .res 2
by1:    .res 2
.ifdef A3D_FFILL
lcur:   .res 1               ; the chains' current corners (x2)
rcur:   .res 1
lend:   .res 1               ; the line each chain's edge ends on
rend:   .res 1
pxr:    .res 1               ; pat_e ^ pat_o
fdx:    .res 1
fdy:    .res 1
fsg:    .res 1
fa:     .res 1
fsl:    .res 2
.endif

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

; void a3d_poly(uint8 n): the fill rejects and clips by itself now
_a3d_poly:
        sta     _a3d_fn
        jmp     _a3d_fill

; void a3d_fill(uint8 n): a3d_px/py (int16): x is clamped per line, y is
; clipped to 0..a3d_h by the edge walker (an edge starting above line 0 is
; advanced by slope x the lines skipped)
_a3d_fill:
        asl     a
        sta     n2
        cmp     #6
        bcs     :+
        rts
:       lda     _a3d_py
        sta     by0
        sta     by1
        lda     _a3d_py+1
        sta     by0+1
        sta     by1+1
        lda     _a3d_px
        sta     bx0
        sta     bx1
        lda     _a3d_px+1
        sta     bx0+1
        sta     bx1+1
        ldx     #2
@mm:    SLT     {_a3d_py,x}, {_a3d_py+1,x}, by0, by0+1
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
        beq     :+
        jmp     @mm
:       ; ymin = max(y0, 0), ymax = min(y1, h)
        lda     #0
        ldx     by0+1
        bmi     :+
        lda     by0
        cpx     #0
        beq     :+
        rts                         ; y0 >= 256: below
:       sta     ymin
        lda     _a3d_h
        ldx     by1+1
        bmi     @out                ; y1 < 0: above
        bne     :+
        cmp     by1
        bcc     :+
        lda     by1
:       sta     ymax
        cmp     ymin
        beq     @out
        bcs     :+
@out:   rts
:       ; wholly left (x1 <= 0) or right (x0 >= 160): nothing
        lda     bx1+1
        bmi     @out2
        bne     :+
        lda     bx1
        beq     @out2
:       lda     bx0+1
        bmi     :+
        bne     @out2
        lda     bx0
        cmp     #160
        bcc     :+
@out2:  rts
:       ; ---- dirty rectangle: bytes of clamp(x0), clamp(x1 - 1)
        lda     ymin
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
.ifdef A3D_FFILL
        ; ---- all on the screen: the fast path (ffill: no tables, no clipping)
        lda     bx0+1
        ora     by0+1
        bne     @gen
        lda     bx1+1
        bne     @gen
        lda     bx1
        cmp     #161
        bcs     @gen
        lda     by1+1
        bne     @gen
        lda     _a3d_h
        cmp     by1
        bcc     @gen
        jmp     ffill
@gen:
.endif
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
        lda     _a3d_py,y           ; compare y[j] with y[i] (16 bits)
        cmp     _a3d_py,x
        bne     :+
        lda     _a3d_py+1,y
        cmp     _a3d_py+1,x
        beq     @next               ; flat: no edge
:       lda     _a3d_py,y
        cmp     _a3d_py,x
        lda     _a3d_py+1,y
        sbc     _a3d_py+1,x
        bvc     :+
        eor     #$80
:       bmi     @up
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
.ifdef A3D_FFILL
        jsr     pspan
.else
        SPAN    fl_skip
.endif
fl_skip:  ldy     yy
        iny
        cpy     ymax
        beq     :+
        jmp     fl_line
:       rts

.ifdef A3D_FFILL
; the span on the current line (p, pat, lb, rb, nml, nmr set): shared by
; the edge-table fill and ffill
pspan:  SPAN    psp_out
psp_out:
        rts
.endif

; walk the edge ia (top) -> ib (16-bit y) into the table at A/X, the lines
; of it inside 0..a3d_h
walk:
        sta     @fs+1
        stx     @fs+2
        sta     @vs+1
        stx     @vs+2
        sta     @ss+1
        stx     @ss+2
        ldx     ia
        ldy     ib
        ; the visible lines: ys = max(ya, 0), ye = min(yb, h)
        lda     _a3d_py+1,y         ; yb <= 0: none
        bmi     @none0
        bne     :+
        lda     _a3d_py,y
        beq     @none0
:       lda     _a3d_py+1,x         ; ya >= h: none
        bmi     :+
        bne     @none0
        lda     _a3d_py,x
        cmp     _a3d_h
        bcc     :+
@none0: rts
:
:       lda     _a3d_h              ; ye
        ldx     _a3d_py+1,y
        bne     :+
        cmp     _a3d_py,y
        bcc     :+
        lda     _a3d_py,y
:       sta     wye
        ; dy = yb - ya (16 bits)
        ldx     ia
        sec
        lda     _a3d_py,y
        sbc     _a3d_py,x
        sta     wdy
        lda     _a3d_py+1,y
        sbc     _a3d_py+1,x
        sta     wdy+1
        ; dx = xb - xa, its sign and size
        sec
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
:       lda     wdy+1               ; the slope: |dx| * 65536/dy >> 8, or |dx|*256 / dy
        bne     @long
        lda     wdy
        cmp     #97
        bcs     @long
        tay
        lda     a3d_rec_lo,y
        sta     pl
        lda     a3d_rec_hi,y
        sta     ph
        jsr     mul_t_p
        jmp     @slope
@none:  rts
@long:  jsr     div24               ; r = (|dx| << 8) / dy
@slope: lda     r0
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
        ; above line 0: x += slope * -ya, start at 0
        lda     _a3d_py+1,x
        bpl     @ys
        sec
        lda     #0
        sbc     _a3d_py,x
        sta     wsk
        lda     #0
        sbc     _a3d_py+1,x
        sta     wsk+1
        jsr     skip
        lda     #0
        sta     wys
        jmp     @range
@ys:    lda     _a3d_py,x
        sta     wys
@range: lda     wye                 ; lines to walk
        sec
        sbc     wys
        beq     @none
        bcc     @none
        sta     dyc
        ; fast path: both ends inside 0..160 (x is monotonic between them)
        ldx     ia                  ; (skip's products leave X changed)
        lda     _a3d_px+1,x
        bne     @slow
        lda     _a3d_px,x
        cmp     #161
        bcs     @slow
        ldy     ib
        lda     _a3d_px+1,y
        bne     @slow
        lda     _a3d_px,y
        cmp     #161
        bcs     @slow
        ldy     wys                 ; Y = ys .. ye-1
        lda     wye
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
@slow:  ldy     wys
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

; x (xh:xl:xf) += slope (24) * wsk (16), the low 24 bits (two's complement:
; the unsigned product's low 24 bits are right for a negative slope too)
skip:   lda     sl0
        jsr     mulset_u
        ldy     wsk
        MUL                         ; s0*k0 -> bytes 0, 1
        stx     wp
        sta     wp+1
        lda     #0
        sta     wp+2
        ldy     wsk+1
        MUL                         ; s0*k1 -> bytes 1, 2
        jsr     add12
        lda     sl1
        jsr     mulset_u
        ldy     wsk
        MUL                         ; s1*k0 -> bytes 1, 2
        jsr     add12
        ldy     wsk+1
        MUL                         ; s1*k1 -> byte 2
        txa
        clc
        adc     wp+2
        sta     wp+2
        lda     sl2
        jsr     mulset_u
        ldy     wsk
        MUL                         ; s2*k0 -> byte 2
        txa
        clc
        adc     wp+2
        sta     wp+2
        clc
        lda     xf
        adc     wp
        sta     xf
        lda     xl
        adc     wp+1
        sta     xl
        lda     xh
        adc     wp+2
        sta     xh
        rts

; wp+1 += X (low), wp+2 += A (high) + carry
add12:  sta     wq
        txa
        clc
        adc     wp+1
        sta     wp+1
        lda     wq
        adc     wp+2
        sta     wp+2
        rts

; r2:r1:r0 = (tx << 8) / wdy (24 / 16 bits, restoring)
div24:  lda     #0
        sta     r0                  ; the dividend tx:0 shifted into r (the quotient)
        lda     tx
        sta     r1
        lda     tx+1
        sta     r2
        lda     #0
        sta     wp                  ; the remainder
        sta     wp+1
        ldx     #24
@d:     asl     r0
        rol     r1
        rol     r2
        rol     wp
        rol     wp+1
        lda     wp
        sec
        sbc     wdy
        tay
        lda     wp+1
        sbc     wdy+1
        bcc     :+
        sta     wp+1
        sty     wp
        inc     r0
:       dex
        bne     @d
        rts

.ifdef A3D_FFILL
; ------------------------------------------------------------------ fast path
; a polygon wholly on the screen (0 <= x <= 160, 0 <= y <= a3d_h): its two
; chains walked down from the top corner together, a span a line, the same
; x on each line as the edge walker's (x + 0.5 at the corner, slope |dx| *
; (65536 / dy) >> 8). ymin, ymax set; the dirty rectangle done.
lxl     = a3d_t+8
lxh     = a3d_t+9
rxl     = a3d_t+10
rxh     = a3d_t+11
lsl     = a3d_t+12
rsl     = a3d_t+14

ffill:  ldx     #0                  ; the top corner
        stx     lcur
        lda     _a3d_py
        sta     yy
        ldx     #2
@ft:    lda     _a3d_py,x
        cmp     yy
        bcs     :+
        sta     yy
        stx     lcur
:       inx
        inx
        cpx     n2
        bne     @ft
        lda     lcur
        sta     rcur
        jsr     lnext
        bcc     :+
        rts
:       jsr     rnext
        bcc     :+
        rts
:       lda     _a3d_pat_e
        eor     _a3d_pat_o
        sta     pxr
        lda     yy
        lsr     a
        lda     _a3d_pat_e
        bcc     :+
        lda     _a3d_pat_o
:       sta     pat
ff_line:
        ldy     yy
        lda     _a3d_ylo,y
        sta     a3d_p
        lda     _a3d_yhi,y
        ora     a3d_bsel
        sta     a3d_p+1
        ldx     rxh                 ; the span lxh .. rxh - 1
        cpx     lxh
        beq     ff_skip
        bcc     ff_skip
        lda     keepr,x
        sta     nmr
        lda     rbyte,x
        sta     rb
        ldx     lxh
        lda     keepl,x
        sta     nml
        lda     lbyte,x
        sta     lb
        jsr     pspan
ff_skip:
        inc     yy
        lda     yy
        cmp     ymax
        beq     ff_out
        lda     pat
        eor     pxr
        sta     pat
        lda     yy                  ; the left edge: on, or the next
        cmp     lend
        bne     :+
        jsr     lnext
        bcs     ff_out
        jmp     @r
:       clc
        lda     lxl
        adc     lsl
        sta     lxl
        lda     lxh
        adc     lsl+1
        sta     lxh
@r:     lda     yy                  ; the right edge
        cmp     rend
        bne     :+
        jsr     rnext
        bcs     ff_out
        jmp     ff_line
:       clc
        lda     rxl
        adc     rsl
        sta     rxl
        lda     rxh
        adc     rsl+1
        sta     rxh
        jmp     ff_line
ff_out: rts

; the left chain's next edge down (the corner before lcur); carry set if it
; doesn't go down (not convex and clockwise: give up)
lnext:  ldx     lcur
        stx     fa
        txa
        bne     :+
        lda     n2
:       sec
        sbc     #2
        tay                         ; Y = the next corner
        sty     lcur
        lda     _a3d_py,y
        sec
        sbc     _a3d_py,x
        beq     lnext               ; flat: on along the chain
        bcc     @bad
        sta     fdy
        lda     _a3d_py,y
        sta     lend
        jsr     eslope
        lda     fsl
        sta     lsl
        lda     fsl+1
        sta     lsl+1
        ldx     fa
        lda     _a3d_px,x
        sta     lxh
        lda     #$80
        sta     lxl
        clc
        rts
@bad:   sec
        rts

; the right chain's next edge down (the corner after rcur)
rnext:  ldx     rcur
        stx     fa
        txa
        clc
        adc     #2
        cmp     n2
        bcc     :+
        lda     #0
:       tay
        sty     rcur
        lda     _a3d_py,y
        sec
        sbc     _a3d_py,x
        beq     rnext
        bcc     @bad
        sta     fdy
        lda     _a3d_py,y
        sta     rend
        jsr     eslope
        lda     fsl
        sta     rsl
        lda     fsl+1
        sta     rsl+1
        ldx     fa
        lda     _a3d_px,x
        sta     rxh
        lda     #$80
        sta     rxl
        clc
        rts
@bad:   sec
        rts

; fsl = (px[Y] - px[X]) * (65536 / fdy) >> 8, signed 8.8 (|dx| <= 160)
eslope: lda     _a3d_px,y
        sec
        sbc     _a3d_px,x
        sta     fdx
        lda     _a3d_px+1,y
        sbc     _a3d_px+1,x
        sta     fsg
        bpl     :+
        lda     #0
        sec
        sbc     fdx
        sta     fdx
:       lda     fdx
        bne     :+
        sta     fsl
        sta     fsl+1
        rts
:       jsr     mulset_u
        ldx     fdy
        ldy     a3d_rec_lo,x
        MUL                         ; A: (|dx| * rec_lo) >> 8
        sta     fsl
        ldx     fdy
        ldy     a3d_rec_hi,x
        MUL                         ; X low, A high: |dx| * rec_hi
        sta     fsl+1
        txa
        clc
        adc     fsl
        sta     fsl
        bcc     :+
        inc     fsl+1
:       bit     fsg
        bpl     :+
        sec
        lda     #0
        sbc     fsl
        sta     fsl
        lda     #0
        sbc     fsl+1
        sta     fsl+1
:       rts
.endif
