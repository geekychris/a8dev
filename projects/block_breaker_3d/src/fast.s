; fast.s - Block Breaker 3D's drawing loops: rectangles filled with a
; pattern (the bricks into the clean box, the shadow, the ring and the
; tunnel into the back buffer) and rectangles copied from the clean box
; into the back buffer. The clean box's lines are found through
; cl_lo/cl_hi (view.c fills them).
        .importzp a3d_p, a3d_q, a3d_bsel
        .import   _a3d_ylo, _a3d_yhi, _cl_lo, _cl_hi
        .export   _fr_x0, _fr_x1, _fr_y0, _fr_y1, _fr_pe, _fr_po, _fr_tgt, _cr_b0, _cr_b1
        .export   _fr_cx0, _fr_cx1, _fr_cy0, _fr_cy1
        .export   _fill_rect, _copy_rect

.segment "LOWRAM"
_fr_x0: .res 1          ; x0 <= x < x1, y0 <= y < y1
_fr_x1: .res 1
_fr_y0: .res 1
_fr_y1: .res 1
_fr_pe: .res 1          ; pattern on even lines
_fr_po: .res 1          ; ... odd lines (must follow fr_pe)
_fr_tgt: .res 1         ; 0 = the clean box, else the back buffer
_fr_cx0: .res 1         ; the clip window: cx0 <= x < cx1, cy0 <= y < cy1
_fr_cx1: .res 1
_fr_cy0: .res 1
_fr_cy1: .res 1
_cr_b0: .res 1          ; copy: bytes b0..b1 (inclusive)
_cr_b1: .res 1
yy:     .res 1
b0:     .res 1
b1:     .res 1
ml:     .res 1
mr:     .res 1
pt:     .res 1

.segment "RODATA"
setl:   .byte $FF, $3F, $0F, $03        ; [x0 & 3]: the pixels from x0 on
setr:   .byte $C0, $F0, $FC, $FF        ; [(x1 - 1) & 3]: the pixels up to x1 - 1

.segment "CODE"
; void fill_rect(void): clipped to the window
_fill_rect:
        lda     _fr_x0
        cmp     _fr_cx0
        bcs     :+
        lda     _fr_cx0
        sta     _fr_x0
:       lda     _fr_x1
        cmp     _fr_cx1
        bcc     :+
        lda     _fr_cx1
        sta     _fr_x1
:       lda     _fr_y0
        cmp     _fr_cy0
        bcs     :+
        lda     _fr_cy0
        sta     _fr_y0
:       lda     _fr_y1
        cmp     _fr_cy1
        bcc     :+
        lda     _fr_cy1
        sta     _fr_y1
:       lda     _fr_x0
        cmp     _fr_x1
        bcc     :+
        rts                             ; empty
:       tax
        lsr     a
        lsr     a
        sta     b0
        txa
        and     #3
        tax
        lda     setl,x
        sta     ml
        ldx     _fr_x1
        dex
        txa
        lsr     a
        lsr     a
        sta     b1
        txa
        and     #3
        tax
        lda     setr,x
        sta     mr
        lda     b0
        cmp     b1
        bne     :+
        lda     ml                      ; one byte: both masks
        and     mr
        sta     ml
:       ldy     _fr_y0
@line:  cpy     _fr_y1
        bcs     @done
        sty     yy
        lda     _fr_tgt
        bne     @back
        lda     _cl_lo,y
        sta     a3d_p
        lda     _cl_hi,y
        sta     a3d_p+1
        jmp     @ptr
@back:  lda     _a3d_ylo,y
        sta     a3d_p
        lda     _a3d_yhi,y
        ora     a3d_bsel
        sta     a3d_p+1
@ptr:   tya
        and     #1
        tax
        lda     _fr_pe,x
        sta     pt
        ldy     b0
        lda     (a3d_p),y               ; new = old ^ ((old ^ pt) & mask)
        eor     pt
        and     ml
        eor     (a3d_p),y
        sta     (a3d_p),y
        cpy     b1
        beq     @next
        iny
        lda     pt
@mid:   cpy     b1
        beq     @last
        sta     (a3d_p),y
        iny
        bne     @mid
@last:  lda     (a3d_p),y
        eor     pt
        and     mr
        eor     (a3d_p),y
        sta     (a3d_p),y
@next:  ldy     yy
        iny
        bne     @line
@done:  rts

; void copy_rect(void): lines fr_y0..fr_y1-1, bytes cr_b0..cr_b1, the clean box -> back buffer
_copy_rect:
        ldx     _fr_y0
@line:  cpx     _fr_y1
        bcs     @done
        lda     _cl_lo,x
        sta     a3d_q
        lda     _cl_hi,x
        sta     a3d_q+1
        lda     _a3d_ylo,x
        sta     a3d_p
        lda     _a3d_yhi,x
        ora     a3d_bsel
        sta     a3d_p+1
        ldy     _cr_b0
@b:     lda     (a3d_q),y
        sta     (a3d_p),y
        cpy     _cr_b1
        beq     @n
        iny
        bne     @b
@n:     inx
        bne     @line
@done:  rts

; void tun_line(void): the tunnel on line tl_y, bytes tl_b0..tl_b1, into the
; target (fr_tgt): the side walls' row (tun_row) where the line isn't all
; middle run, then the middle run (tun_xl..tun_xr, pattern tun_mid)
        .import   _tun_row, _tun_xl, _tun_xr, _tun_mid, _tun_pats
        .export   _tl_y, _tl_b0, _tl_b1, _tun_line_

.segment "LOWRAM"
_tl_y:  .res 1
_tl_b0: .res 1
_tl_b1: .res 1
e1:     .res 1
e2:     .res 1

.segment "CODE"
_tun_line_:
        ldy     _tl_y
        lda     _fr_tgt
        beq     @cl
        lda     _a3d_ylo,y
        sta     a3d_p
        lda     _a3d_yhi,y
        ora     a3d_bsel
        sta     a3d_p+1
        jmp     @row
@cl:    lda     _cl_lo,y
        sta     a3d_p
        lda     _cl_hi,y
        sta     a3d_p+1
@row:   tya
        and     #1
        beq     @ev
        lda     #<(_tun_row+40)
        ldx     #>(_tun_row+40)
        jmp     @rr
@ev:    lda     #<_tun_row
        ldx     #>_tun_row
@rr:    sta     a3d_q
        stx     a3d_q+1
        lda     _tun_xl,y
        sta     _fr_x0
        clc
        adc     #3
        lsr     a
        lsr     a
        sta     e1                      ; the first byte all in the middle run
        lda     _tun_xr,y
        sta     _fr_x1
        lsr     a
        lsr     a
        sta     e2                      ; the first byte not all in it
        ldy     _tl_b0                  ; left: b0 .. min(b1, e1 - 1)
@l:     cpy     e1
        bcs     @ld
        lda     (a3d_q),y
        sta     (a3d_p),y
        cpy     _tl_b1
        beq     @ld
        iny
        bne     @l
@ld:    ldy     e2                      ; right: max(e2, b0) .. b1
        cpy     _tl_b0
        bcs     @r
        ldy     _tl_b0
@r:     cpy     _tl_b1
        beq     @rc
        bcs     @rd
@rc:    lda     (a3d_q),y
        sta     (a3d_p),y
        iny
        bne     @r
@rd:    lda     _tl_b0                  ; the middle run within b0..b1
        asl     a
        asl     a
        cmp     _fr_x0
        bcc     :+
        sta     _fr_x0
:       lda     _tl_b1
        asl     a
        asl     a
        clc
        adc     #4
        cmp     _fr_x1
        bcs     :+
        sta     _fr_x1
:       lda     _tl_y
        sta     _fr_y0
        clc
        adc     #1
        sta     _fr_y1
        ldy     _tl_y
        lda     _tun_mid,y
        asl     a
        tax
        lda     _tun_pats,x
        sta     _fr_pe
        lda     _tun_pats+1,x
        sta     _fr_po
        jmp     _fill_rect

; void proj(void): a point (pj_x, pj_y, pj_z, 1/32 units) on the screen:
; pj_k = the 1/z table index ((z + 320 - 256) / 4, 0..216), pj_sx = 80 +
; x * kx_tab[k] / 8192 (pixels), pj_sy = 120 - y * ky_tab[k] / 8192 (scan
; lines). Products by shift-and-add, 16 x 8 bits.
        .import   _kx_tab, _ky_tab, mulset_u
        .importzp a3d_m
        .include  "a3d_mul.inc"
        .export   _pj_x, _pj_y, _pj_z, _pj_k, _pj_sx, _pj_sy, _proj

.segment "LOWRAM"
_pj_x:  .res 2
_pj_y:  .res 2
_pj_z:  .res 2
_pj_k:  .res 1
_pj_sx: .res 1
_pj_sy: .res 1
mlo:    .res 1                          ; |v|
mhi:    .res 1
mf:     .res 1                          ; the factor
r0:     .res 1                          ; the product, 24 bits
r1:     .res 1
r2:     .res 1
neg:    .res 1

.segment "CODE"
; |a,x| * mf (a = low, x = high) >> 13 in A, neg = the sign: two 8 x 8
; products from a3d's quarter-square tables
mul13:  stx     neg
        cpx     #$80
        bcc     :+
        eor     #$FF                    ; negate
        clc
        adc     #1
        pha
        txa
        eor     #$FF
        adc     #0
        tax
        pla
:       sta     mlo
        stx     mhi
        lda     mf
        jsr     mulset_u
        ldy     mlo
        MUL                             ; X = low, A = high
        sta     r1
        ldy     mhi
        MUL
        sta     r2
        txa
        clc
        adc     r1
        sta     r1
        bcc     :+
        inc     r2
:       lda     r1                      ; (r2:r1) >> 5
        ldx     #5
:       lsr     r2
        ror     a
        dex
        bne     :-
        rts

_proj:  lda     _pj_z+1                 ; k = ((z >> 5) + 64) >> 2 = (z + 2048) >> 7
        tax
        lda     _pj_z
        clc
        adc     #<2048
        sta     r0
        txa
        adc     #>2048
        bmi     @k0                     ; z < -2048: 0
        asl     r0                      ; >> 7 = << 1 then the high byte
        rol     a
        cmp     #217
        bcc     @kk
        lda     #216
        bne     @kk
@k0:    lda     #0
@kk:    sta     _pj_k
        tay
        lda     _kx_tab,y
        sta     mf
        lda     _pj_x
        ldx     _pj_x+1
        jsr     mul13
        bit     neg
        bpl     :+
        eor     #$FF
        sec
        adc     #80 - 1                 ; 80 - p = 80 + ~p + 1
        jmp     @sx
:       clc
        adc     #80
@sx:    sta     _pj_sx
        ldy     _pj_k
        lda     _ky_tab,y
        sta     mf
        lda     _pj_y
        ldx     _pj_y+1
        jsr     mul13
        bit     neg
        bmi     :+
        eor     #$FF                    ; 120 - p
        sec
        adc     #120
        sta     _pj_sy
        rts
:       clc
        adc     #120                    ; 120 + p
        sta     _pj_sy
        rts

; void draw_cells(void): the bricks in columns dc_c0..dc_c1, rows
; dc_r0..dc_r1, every layer far to near, into the clean box (clipped to
; the window): the sides that face the camera with no brick against them
; (1-2 pixel strips), and the front (with a dark rim right and below)
; where no brick is in front. dc_cell = G.cell, dc_hits = G.hits, dc_pat =
; the level's patterns [row][BK_*][even, odd].
        .import   _bk_sx, _bk_sy
        .export   _dc_c0, _dc_c1, _dc_r0, _dc_r1, _dc_cell, _dc_hits, _dc_pat, _draw_cells_

.segment "ZEROPAGE"
cellp:  .res 2
patp:   .res 2
hitp:   .res 2

BK_TOUGH = 7

.segment "LOWRAM"
_dc_c0: .res 1
_dc_c1: .res 1
_dc_r0: .res 1
_dc_r1: .res 1
_dc_cell: .res 2
_dc_hits: .res 2
_dc_pat: .res 2
lyr:    .res 1                          ; layer
row:    .res 1
col:    .res 1
ci:     .res 1                          ; the cell's index
sx0:    .res 1                          ; bk_sx row offsets of this layer and the next
sx1:    .res 1
sy0:    .res 1
sy1:    .res 1
pe:     .res 1
po:     .res 1

.segment "RODATA"
mul7:   .byte   0, 7, 14, 21
mul5:   .byte   0, 5, 10, 15
mul20:  .byte   0, 20, 40, 60

.segment "CODE"
; fill x0 = A .. x1 = X (exclusive), lines Y .. fr_y1 with pe/po
dc_rect:
        sta     _fr_x0
        stx     _fr_x1
        sty     _fr_y0
        lda     pe
        sta     _fr_pe
        lda     po
        sta     _fr_po
        jmp     _fill_rect

_draw_cells_:
        lda     #0
        sta     _fr_tgt
        lda     _dc_cell
        sta     cellp
        lda     _dc_cell+1
        sta     cellp+1
        lda     _dc_hits
        sta     hitp
        lda     _dc_hits+1
        sta     hitp+1
        lda     #2
        sta     lyr
@layer: ldx     lyr
        lda     mul7,x
        sta     sx0
        lda     mul7+1,x
        sta     sx1
        lda     mul5,x
        sta     sy0
        lda     mul5+1,x
        sta     sy1
        lda     _dc_r0
        sta     row
@row:   ldx     row                     ; patterns of this row
        lda     mul20,x
        clc
        adc     _dc_pat
        sta     patp
        lda     _dc_pat+1
        adc     #0
        sta     patp+1
        lda     _dc_c0
        sta     col
@col:   lda     lyr                     ; ci = lyr * 24 + row * 6 + col
        asl     a
        adc     lyr                     ; * 3 (carry clear: lyr <= 2)
        asl     a
        asl     a
        asl     a                       ; * 24
        sta     ci
        lda     row
        asl     a
        adc     row                     ; * 3
        asl     a                       ; * 6
        adc     ci
        adc     col
        sta     ci
        tay
        lda     (cellp),y
        bne     :+
        jmp     @next
:       asl     a                       ; the pattern
        tay
        lda     (patp),y
        sta     pe
        iny
        lda     (patp),y
        sta     po
        cpy     #BK_TOUGH * 2 + 1
        bne     @lit
        ldy     ci                      ; a cracked tough brick: darker
        lda     (hitp),y
        cmp     #1
        bne     @lit
        lda     pe
        and     #$CC
        sta     pe
        lda     po
        and     #$33
        sta     po
@lit:   ; the sides: lines bk_sy[l+1][r+1] .. bk_sy[l+1][r]
        lda     sy1
        clc
        adc     row
        tax
        lda     _bk_sy,x
        sta     _fr_y1
        lda     _bk_sy+1,x
        sta     yy                      ; y0
        lda     col
        cmp     #4
        bcc     @right
        ldy     ci                      ; c >= 4, the cell to the left empty: its left side
        dey
        lda     (cellp),y
        bne     @front
        lda     sx1
        clc
        adc     col
        tax
        lda     _bk_sx,x                ; bk_sx[l+1][c] .. bk_sx[l][c]
        pha
        lda     sx0
        clc
        adc     col
        tax
        lda     _bk_sx,x
        tax
        pla
        ldy     yy
        jsr     dc_rect
        jmp     @front
@right: cmp     #2
        bcs     @front
        ldy     ci                      ; c <= 1, the cell to the right empty: its right side
        iny
        lda     (cellp),y
        bne     @front
        lda     sx1
        sec                             ; + c + 1
        adc     col
        tax
        lda     _bk_sx,x                ; bk_sx[l][c+1] .. bk_sx[l+1][c+1]
        pha
        lda     sx0
        sec
        adc     col
        tax
        lda     _bk_sx,x
        tay
        pla
        tax
        tya
        ldy     yy
        jsr     dc_rect
@front: lda     lyr                     ; layer 0, or nothing in front
        beq     @face
        lda     ci
        sec
        sbc     #24
        tay
        lda     (cellp),y
        bne     @next
@face:  lda     sy0
        clc
        adc     row
        tax
        lda     _bk_sy,x                ; y1 = bk_sy[l][r]
        sta     y1s
        lda     _bk_sy+1,x              ; y0 = bk_sy[l][r+1]
        sta     y0s
        lda     sx0
        clc
        adc     col
        tax
        lda     _bk_sx,x
        sta     x0s
        lda     _bk_sx+1,x
        sta     x1s
        dec     x1s                     ; the face: x0 .. x1-1, y0 .. y1-1
        dec     y1s
        lda     y1s
        sta     _fr_y1
        lda     x0s
        ldx     x1s
        ldy     y0s
        jsr     dc_rect
        lda     #0                      ; the rim
        sta     pe
        sta     po
        inc     y1s
        lda     y1s                     ; right: x1-1, y0 .. y1
        sta     _fr_y1
        lda     x1s
        ldx     x1s
        inx
        ldy     y0s
        jsr     dc_rect
        lda     y1s                     ; below: x0 .. x1-1, y1-1
        sta     _fr_y1
        lda     x0s
        ldx     x1s
        ldy     y1s
        dey
        jsr     dc_rect
@next:  lda     col
        cmp     _dc_c1
        inc     col
        bcs     :+
        jmp     @col
:       lda     row
        cmp     _dc_r1
        inc     row
        bcs     :+
        jmp     @row
:       dec     lyr
        bmi     :+
        jmp     @layer
:       rts

.segment "LOWRAM"
x0s:    .res 1
x1s:    .res 1
y0s:    .res 1
y1s:    .res 1
