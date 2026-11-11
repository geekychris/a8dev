; a3d_fill.s - convex polygons, spans, clears and lines in the back buffer.
;
; Polygons: a3d_px/a3d_py (int16), clockwise on the screen. a3d_poly
; rejects polygons outside the 3D area and hands those crossing its top or
; bottom to C (a3d_poly_clip); a3d_fill draws one whose y is inside
; 0..a3d_h. Each edge is walked from its top: edges going down are the
; right side, edges going up the left, so the sides need no sorting and an
; anticlockwise polygon has left >= right on every line and draws nothing.
; Edge x is 16.8 fixed point (slope = |dx| * (65536/dy) >> 8), clamped to
; 0..160 when stored.
; Spans x0 <= x < x1: a masked byte at each end (tables by x), whole bytes
; between by an unrolled `sta (p),y / iny` entered at the right place.
; Each buffer keeps the rectangle drawn into it since it was last cleared
; (a3d_clear_dirty).

        .importzp a3d_bsel, a3d_p, a3d_q, a3d_t, a3d_m
        .import   _a3d_px, _a3d_py, mul_t_p, a3d_rec_lo, a3d_rec_hi
        .import   tx, pl, ph, r0, r1, r2, mulset_s
        .import   _a3d_sx, _a3d_sy, _a3d_vclip, _a3d_h, _a3d_poly_clip
        .export   _a3d_fill, _a3d_poly, _a3d_face_, _a3d_gather_, _a3d_front_, _a3d_fn
        .export   _a3d_span_, _a3d_clear_rows_, _a3d_clear_dirty_, _a3d_line_
        .export   _a3d_ylo, _a3d_yhi, _a3d_pat_e, _a3d_pat_o, _a3d_sx0, _a3d_sx1
        .export   _a3d_cy0, _a3d_cy1, _a3d_lx0, _a3d_ly0, _a3d_lx1, _a3d_ly1
        .export   _a3d_el, _a3d_er, _a3d_dirty

; zero page scratch (a3d_t)
pat     = a3d_t+0
lb      = a3d_t+1
rb      = a3d_t+2
nml     = a3d_t+3
nmr     = a3d_t+4
yy      = a3d_t+5
ymin    = a3d_t+6
ymax    = a3d_t+7
xf      = a3d_t+8
xl      = a3d_t+9
xh      = a3d_t+10
sl0     = a3d_t+11
sl1     = a3d_t+12
sl2     = a3d_t+13
; (front test)
dax     = a3d_t+0
day     = a3d_t+2
dbx     = a3d_t+4
dby     = a3d_t+6
dp1     = a3d_t+8

.segment "BSS"
_a3d_ylo:   .res 96          ; line addresses of buffer 0 (buffer 1: high | $10)
_a3d_yhi:   .res 96
_a3d_el:    .res 97          ; left edge per line
_a3d_er:    .res 97          ; right edge per line
_a3d_pat_e: .res 1           ; pattern on even lines
_a3d_pat_o: .res 1           ; ... odd lines (must follow pat_e)
_a3d_sx0:   .res 1
_a3d_sx1:   .res 1
_a3d_cy0:   .res 1
_a3d_cy1:   .res 1
_a3d_lx0:   .res 1
_a3d_ly0:   .res 1
_a3d_lx1:   .res 1
_a3d_ly1:   .res 1
_a3d_fn:    .res 1
; dirty rectangles per buffer: y0, y1 (exclusive), first byte, last byte
_a3d_dirty:
d_y0:   .res 2
d_y1:   .res 2
d_b0:   .res 2
d_b1:   .res 2
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
ry0:    .res 1               ; a rectangle for the dirty update
ry1:    .res 1
rb0:    .res 1
rb1:    .res 1
fi:     .res 1
fi2:    .res 1
; clear
cval:   .res 1
clen:   .res 2
cpe:    .res 2
; line
lpat:   .res 1
lmask:  .res 1
ldx_:   .res 1
ldy_:   .res 1
lerr:   .res 1
lcnt:   .res 1
lstep:  .res 1

.segment "RODATA"
lbyte:  .repeat 160, I
        .byte   I / 4
        .endrepeat
keepl:  .repeat 160, I                  ; bits to keep left of x0
        .byte   <($FF00 >> ((I & 3) * 2))
        .endrepeat
rbyte:  .byte   0
        .repeat 160, I                  ; [x1]: byte of x1-1
        .byte   I / 4
        .endrepeat
keepr:  .byte   0
        .repeat 160, I                  ; [x1]: bits to keep right of x1-1
        .byte   $3F >> ((I & 3) * 2)
        .endrepeat
pmask:  .byte   $C0, $30, $0C, $03
patc:   .byte   $00, $55, $AA, $FF

UNROLL  = 38

; the middle of a span: whole bytes lb+1..rb-1, then the right byte, then
; `next`. Y = lb + 1, pat, nmr, rb set; X = count (1..38)
.macro  SPAN_MIDDLE next
        .local  ent_lo, ent_hi, jv, unroll
.pushseg
.segment "RODATA"
ent_lo: .byte   0
        .repeat UNROLL, I
        .byte   <(unroll + (UNROLL - 1 - I) * 3)
        .endrepeat
ent_hi: .byte   0
        .repeat UNROLL, I
        .byte   >(unroll + (UNROLL - 1 - I) * 3)
        .endrepeat
.popseg
        lda     ent_lo,x
        sta     jv+1
        lda     ent_hi,x
        sta     jv+2
        lda     pat
jv:     jmp     unroll
unroll: .repeat UNROLL
        sta     (a3d_p),y
        iny
        .endrepeat
.endmacro

; a whole span: p, pat, lb, rb, nml, nmr set; then jmp `next`
.macro  SPAN    next
        .local  multi, mid, right
        ldy     lb
        lda     rb
        cmp     lb
        bne     multi
        lda     nml                 ; one byte
        ora     nmr
        sta     nml
        lda     (a3d_p),y
        eor     pat
        and     nml
        eor     pat
        sta     (a3d_p),y
        jmp     next
multi:  lda     (a3d_p),y
        eor     pat
        and     nml
        eor     pat
        sta     (a3d_p),y
        iny
        lda     rb
        clc                         ; rb - lb - 1
        sbc     lb
        bne     mid
        jmp     right
mid:    tax
        SPAN_MIDDLE
right:  lda     (a3d_p),y           ; Y = rb
        eor     pat
        and     nmr
        eor     pat
        sta     (a3d_p),y
.endmacro

.segment "CODE"

; ------------------------------------------------------------------ spans
; void a3d_span_(uint8 y): a3d_sx0 <= x < a3d_sx1 on line y
_a3d_span_:
        tay
        ldx     _a3d_sx1
        cpx     _a3d_sx0
        beq     :+
        bcs     :++
:       rts
:       lda     keepr,x
        sta     nmr
        lda     rbyte,x
        sta     rb
        ldx     _a3d_sx0
        lda     keepl,x
        sta     nml
        lda     lbyte,x
        sta     lb
        lda     lb
        sta     rb0
        lda     rb
        sta     rb1
        sty     ry0
        iny
        sty     ry1
        dey
        jsr     dirty_add
        ldy     ry0
        ; fall into do_span
do_span:                            ; Y = line
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
        SPAN    sp_out
sp_out: rts

; ------------------------------------------------------------------ dirty
; grow the back buffer's rectangle by ry0..ry1, rb0..rb1
dirty_add:
        lda     a3d_bsel
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        tax
        lda     d_y0,x
        cmp     d_y1,x
        bcc     @grow
        lda     ry0                 ; empty: set
        sta     d_y0,x
        lda     ry1
        sta     d_y1,x
        lda     rb0
        sta     d_b0,x
        lda     rb1
        sta     d_b1,x
        rts
@grow:  lda     ry0
        cmp     d_y0,x
        bcs     :+
        sta     d_y0,x
:       lda     ry1
        cmp     d_y1,x
        bcc     :+
        sta     d_y1,x
:       lda     rb0
        cmp     d_b0,x
        bcs     :+
        sta     d_b0,x
:       lda     rb1
        cmp     d_b1,x
        bcc     :+
        sta     d_b1,x
:       rts

; void a3d_clear_dirty_(uint8 pattern): clear the back buffer's rectangle
_a3d_clear_dirty_:
        sta     pat
        lda     a3d_bsel
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        tax
        lda     d_y0,x
        cmp     d_y1,x
        bcc     :+
        rts
:       sta     yy
        lda     d_b0,x
        sta     lb
        lda     d_b1,x
        sta     rb
        lda     #0
        sta     nml
        sta     nmr
        lda     d_y1,x
        sta     ymax
        lda     #0                  ; now empty
        sta     d_y0,x
        sta     d_y1,x
cd_line:  ldy     yy
        lda     _a3d_ylo,y
        sta     a3d_p
        lda     _a3d_yhi,y
        ora     a3d_bsel
        sta     a3d_p+1
        SPAN    cd_next
cd_next:  inc     yy
        lda     yy
        cmp     ymax
        beq     cd_empty
        jmp     cd_line
cd_empty: rts

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

; ------------------------------------------------------------------ clear
; void a3d_clear_rows_(uint8 pattern): lines a3d_cy0..a3d_cy1-1
_a3d_clear_rows_:
        sta     cval
        ldy     _a3d_cy0
        cpy     _a3d_cy1
        bcc     :+
        rts
:       lda     _a3d_ylo,y
        sta     a3d_q
        lda     _a3d_yhi,y
        ora     a3d_bsel
        sta     a3d_q+1
        lda     _a3d_cy1            ; len = lines * 40 = lines*32 + lines*8
        sec
        sbc     _a3d_cy0
        sta     clen
        lda     #0
        sta     clen+1
        asl     clen                ; *8
        rol     clen+1
        asl     clen
        rol     clen+1
        asl     clen
        rol     clen+1
        lda     clen
        sta     cpe
        lda     clen+1
        sta     cpe+1
        asl     clen                ; *32
        rol     clen+1
        asl     clen
        rol     clen+1
        clc
        lda     clen
        adc     cpe
        sta     clen
        lda     clen+1
        adc     cpe+1
        sta     clen+1
        ; ---- whole pages: one pass of X = 0..255 with a store per page
        lda     clen+1
        beq     clr_tail
        ; patch the stores: code index i is page (14 - i)
        ldx     #(14*3)
        ldy     #0
clr_pat:
        lda     a3d_q
        sta     cl_un+1,x
        tya
        clc
        adc     a3d_q+1
        sta     cl_un+2,x
        iny
        txa
        sec
        sbc     #3
        tax
        bcs     clr_pat
        ; entry: the store of page (pages - 1) = code index 15 - pages
        lda     #15
        sec
        sbc     clen+1
        sta     cpe
        asl     a
        adc     cpe                 ; *3
        clc
        adc     #<cl_un
        sta     clr_je+1
        lda     #>cl_un
        adc     #0
        sta     clr_je+2
        ; branch back to the entry: offset = entry - (cl_br + 2)
        lda     clr_je+1
        sec
        sbc     #<(cl_br+2)
        sta     cl_br+1
        lda     cval
        ldx     #0
clr_je: jmp     cl_un
clr_tail:
        ; the rest (< 256 bytes) at q + pages*256
        lda     clen
        beq     clr_done
        ; base = q + pages*256 + rest - 256, X = 256 - rest
        clc
        lda     a3d_q
        adc     clen
        sta     cl_t+1
        lda     a3d_q+1
        adc     clen+1
        sta     cl_t+2
        dec     cl_t+2
        sec
        lda     #0
        sbc     clen
        tax
        lda     cval
cl_t:   sta     $FFFF,x
        inx
        bne     cl_t
clr_done:
        rts

cl_un:  .repeat 15
        sta     $FFFF,x
        .endrepeat
        inx
cl_br:  bne     cl_un
        jmp     clr_tail

; ------------------------------------------------------------------ lines
; void a3d_line_(uint8 c): a3d_lx0,ly0 -> lx1,ly1 inside the 3D area
_a3d_line_:
        and     #3
        tax
        lda     patc,x
        sta     lpat
        ; dirty: the endpoints' box
        lda     _a3d_ly0
        ldx     _a3d_ly1
        cmp     _a3d_ly1
        bcc     :+
        stx     ry0
        sta     ry1
        jmp     :++
:       sta     ry0
        stx     ry1
:       inc     ry1
        ldx     _a3d_lx0
        ldy     _a3d_lx1
        cpx     _a3d_lx1
        bcc     :+
        lda     lbyte,y
        sta     rb0
        lda     lbyte,x
        sta     rb1
        jmp     :++
:       lda     lbyte,x
        sta     rb0
        lda     lbyte,y
        sta     rb1
:       jsr     dirty_add
        ; |dx|, |dy|
        lda     _a3d_lx1
        sec
        sbc     _a3d_lx0
        bcs     :+
        eor     #$FF
        adc     #1
:       sta     ldx_
        lda     _a3d_ly1
        sec
        sbc     _a3d_ly0
        bcs     :+
        eor     #$FF
        adc     #1
:       sta     ldy_
        cmp     ldx_
        bcc     @xmaj
        beq     @xmaj
        jmp     @ymaj
@xmaj:  ; ---- x-major: from the left end
        lda     _a3d_lx1
        cmp     _a3d_lx0
        bcs     :+
        jsr     swap_ends
:       lda     #40                 ; y step +40 or -40
        ldx     _a3d_ly1
        cpx     _a3d_ly0
        bcs     :+
        lda     #<-40
:       sta     lstep
        jsr     line_start
        lda     ldx_
        sta     lcnt
        inc     lcnt                ; dx + 1 pixels
        lsr     a
        sta     lerr
@xl:    lda     (a3d_p),y           ; plot
        eor     lpat
        and     lmask
        eor     (a3d_p),y
        sta     (a3d_p),y
        dec     lcnt
        beq     @xdone
        lda     lerr
        sec
        sbc     ldy_
        bcs     @xnoy
        adc     ldx_
        sta     lerr
        jsr     ystep
        jmp     @xx
@xnoy:  sta     lerr
@xx:    lsr     lmask               ; x + 1
        lsr     lmask
        bne     @xl
        lda     #$C0
        sta     lmask
        iny
        bne     @xl
@xdone: rts
@ymaj:  ; ---- y-major: from the top
        lda     _a3d_ly1
        cmp     _a3d_ly0
        bcs     :+
        jsr     swap_ends
:       lda     #0                  ; x direction: 0 = right, 1 = left
        ldx     _a3d_lx1
        cpx     _a3d_lx0
        bcs     :+
        lda     #1
:       sta     lstep
        jsr     line_start
        lda     ldy_
        sta     lcnt
        inc     lcnt
        lsr     a
        sta     lerr
@yl:    lda     (a3d_p),y
        eor     lpat
        and     lmask
        eor     (a3d_p),y
        sta     (a3d_p),y
        dec     lcnt
        beq     @ydone
        clc                         ; y + 1
        lda     a3d_p
        adc     #40
        sta     a3d_p
        bcc     :+
        inc     a3d_p+1
:       lda     lerr
        sec
        sbc     ldx_
        bcs     @ynox
        adc     ldy_
        sta     lerr
        lda     lstep
        bne     @left
        lsr     lmask
        lsr     lmask
        bne     @yl
        lda     #$C0
        sta     lmask
        iny
        bne     @yl
@left:  asl     lmask
        asl     lmask
        bne     @yl
        lda     #$03
        sta     lmask
        dey
        jmp     @yl
@ynox:  sta     lerr
        jmp     @yl
@ydone: rts

; p += lstep (+-40), keeping Y
ystep:  lda     lstep
        bmi     @m
        clc
        adc     a3d_p
        sta     a3d_p
        bcc     :+
        inc     a3d_p+1
:       rts
@m:     clc
        adc     a3d_p
        sta     a3d_p
        bcs     :+
        dec     a3d_p+1
:       rts

; p = line ly0, Y = lx0 >> 2, lmask = pixel lx0's mask
line_start:
        ldx     _a3d_ly0
        lda     _a3d_ylo,x
        sta     a3d_p
        lda     _a3d_yhi,x
        ora     a3d_bsel
        sta     a3d_p+1
        lda     _a3d_lx0
        and     #3
        tax
        lda     pmask,x
        sta     lmask
        lda     _a3d_lx0
        lsr     a
        lsr     a
        tay
        rts

swap_ends:
        lda     _a3d_lx0
        ldx     _a3d_lx1
        sta     _a3d_lx1
        stx     _a3d_lx0
        lda     _a3d_ly0
        ldx     _a3d_ly1
        sta     _a3d_ly1
        stx     _a3d_ly0
        rts
