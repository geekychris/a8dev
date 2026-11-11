; a3d_span.s - spans, clears, lines and the dirty rectangles in the back
; buffer (see a3d_poly.s for polygons).

        .importzp a3d_bsel, a3d_p, a3d_q, a3d_t
        .constructor a3d_span_init, 20
        .import   _a3d_ylo, _a3d_yhi
        .export   _a3d_span_, _a3d_clear_rows_, _a3d_clear_dirty_
        .export   _a3d_pat_e, _a3d_pat_o, _a3d_sx0, _a3d_sx1
        .export   _a3d_cy0, _a3d_cy1, _a3d_dirty
        .export   lbyte, keepl, rbyte, keepr, dirty_add, ry0, ry1, rb0, rb1

        .include "a3d_span.inc"

.ifdef A3D_LOWTAB
.segment "LOWRAM"
.else
.segment "BSS"
.endif
_a3d_pat_e: .res 1           ; pattern on even lines
_a3d_pat_o: .res 1           ; ... odd lines (must follow pat_e)
_a3d_sx0:   .res 1
_a3d_sx1:   .res 1
_a3d_cy0:   .res 1
_a3d_cy1:   .res 1
; dirty rectangles per buffer: y0, y1 (exclusive), first byte, last byte
_a3d_dirty:
d_y0:   .res 2
d_y1:   .res 2
d_b0:   .res 2
d_b1:   .res 2
ry0:    .res 1               ; a rectangle for the dirty update
ry1:    .res 1
rb0:    .res 1
rb1:    .res 1
; clear
cval:   .res 1
clen:   .res 2
cpe:    .res 2
; line

.ifdef A3D_LOWTAB
.segment "LOWRAM"
.else
.segment "BSS"
.endif
lbyte:  .res    160                     ; x / 4
keepl:  .res    160                     ; bits to keep left of x0
rbyte:  .res    161                     ; [x1]: byte of x1-1
keepr:  .res    161                     ; [x1]: bits to keep right of x1-1

.segment "RODATA"
keep4l: .byte   $00, $C0, $F0, $FC
keep4r: .byte   $3F, $0F, $03, $00

; made at start-up (a constructor)
.segment "ONCE"
a3d_span_init:
        ldx     #0
@l:     txa
        lsr     a
        lsr     a
        sta     lbyte,x
        sta     rbyte+1,x
        txa
        and     #3
        tay
        lda     keep4l,y
        sta     keepl,x
        lda     keep4r,y
        sta     keepr+1,x
        inx
        cpx     #160
        bne     @l
        lda     #0
        sta     rbyte
        sta     keepr
        rts

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

