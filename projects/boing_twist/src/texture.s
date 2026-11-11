; texture.s - the ball's texture rows from the puzzle's state (ball.c has
; the C model and the tables). For texel (u, v): the slot (b, s) it is in,
; the piece there (home tile, turned or not), the texel at the same place
; in the home tile (16ths across and down, backwards if turned), from the
; packed picture; colour << 6 into TEX row v, and again at u + 64 for u < 32.
; The first texel column / row of a slot is the groove (0) where the tile
; is wide enough (ball.c).

        .export _tex_rows
        .export _tx_v0, _tx_v1, _tx_ident, _tx_slots, _tx_S
        .import _tb_colslot, _tb_fracu, _tb_groove_u, _tb_rowslot, _tb_fracv, _tb_groove_v
        .import _tb_srcu, _tb_srcv, _tb_home_hb16, _tb_home_hs16, _tb_band_base
        .importzp ptr1, ptr2, ptr3, tmp1, tmp2, tmp3, tmp4

TEX     = $D800
PIC     = $AA60

.segment "LOWDATA"
_tx_v0:    .byte 0
_tx_v1:    .byte 0
_tx_ident: .byte 0
_tx_slots: .word 0              ; &P.slot[0]
_tx_S:     .byte 0
v:      .byte 0
fv:     .byte 0
gv:     .byte 0
s_cur:  .byte 0
xsrc:   .byte 0
ou:     .res 16                 ; 0 or 15 per slot of this row
hsx:    .res 16                 ; home seg * 16
plo:    .res 16                 ; the picture row's address per slot
phi:    .res 16

.segment "CODE"

_tex_rows:
        lda     _tx_v0
        sta     v
@row:   lda     v
        cmp     _tx_v1
        bcc     @go
        rts
@go:    tax
        lda     _tb_fracv,x
        sta     fv
        lda     _tb_groove_v,x
        sta     gv
        ldy     _tb_rowslot,x   ; b
        ; ptr1 = &slot[b * S]
        lda     _tb_band_base,y ; b * S * 2 (< 256)
        clc
        adc     _tx_slots
        sta     ptr1
        lda     _tx_slots+1
        adc     #0
        sta     ptr1+1
        sty     tmp3            ; b
        ; each slot of the row
        ldx     #0
@slot:  cpx     _tx_S
        bcs     @texels
        lda     _tx_ident
        beq     @state
        ; solved: home = b * S + s, upright
        lda     _tb_band_base,y
        lsr
        stx     tmp1
        clc
        adc     tmp1
        tay                     ; home
        lda     #0
        beq     @have
@state: txa
        asl
        tay
        iny
        lda     (ptr1),y        ; orient
        dey
        pha
        lda     (ptr1),y        ; home
        tay
        pla
@have:  beq     @up
        lda     #15
@up:    sta     ou,x
        lda     _tb_home_hs16,y
        sta     hsx,x
        ; picture row = srcv[hb * 16 + (fv ^ ou)] * 16
        lda     fv
        eor     ou,x
        clc
        adc     _tb_home_hb16,y
        tay
        lda     _tb_srcv,y      ; the picture's texel row (0..31)
        sta     tmp1
        lda     #0
        asl     tmp1            ; * 16
        rol
        asl     tmp1
        rol
        asl     tmp1
        rol
        asl     tmp1
        rol
        sta     tmp2
        lda     tmp1
        clc
        adc     #<PIC
        sta     plo,x
        lda     tmp2
        adc     #>PIC
        sta     phi,x
        ldy     tmp3
        inx
        bne     @slot
@texels:
        ; ptr2 = TEX + v * 96
        lda     v
        sta     ptr2
        lda     #0
        sta     ptr2+1
        asl     ptr2
        rol     ptr2+1
        asl     ptr2
        rol     ptr2+1
        asl     ptr2
        rol     ptr2+1
        asl     ptr2
        rol     ptr2+1
        asl     ptr2
        rol     ptr2+1          ; v * 32
        lda     ptr2
        sta     tmp1
        lda     ptr2+1
        sta     tmp2
        asl     ptr2
        rol     ptr2+1          ; v * 64
        lda     ptr2
        clc
        adc     tmp1
        sta     ptr2
        lda     ptr2+1
        adc     tmp2
        adc     #>TEX
        sta     ptr2+1
        lda     #$FF
        sta     s_cur
        ldx     #0              ; u
@tex:   lda     _tb_groove_u,x
        ora     gv
        bne     @zero
        ldy     _tb_colslot,x
        cpy     s_cur
        beq     @same
        sty     s_cur
        lda     plo,y
        sta     ptr3
        lda     phi,y
        sta     ptr3+1
@same:  lda     _tb_fracu,x
        eor     ou,y
        clc
        adc     hsx,y
        tay
        lda     _tb_srcu,y      ; the picture's texel column (0..63)
        sta     xsrc
        lsr
        lsr
        tay
        lda     (ptr3),y        ; 4 texels, the first in bits 7-6
        sta     tmp4
        lda     xsrc
        and     #3
        beq     @s0
        tay
        lda     tmp4
@sh:    asl
        asl
        dey
        bne     @sh
        .byte   $2C             ; bit abs: skip the next 2 bytes
@s0:    lda     tmp4
        and     #$C0
        jmp     @put
@zero:  lda     #0
@put:   stx     tmp1
        ldy     tmp1
        sta     (ptr2),y
        cpy     #32
        bcs     @nx
        tax
        tya
        adc     #64             ; carry clear
        tay
        txa
        sta     (ptr2),y
        ldx     tmp1
@nx:    inx
        cpx     #64
        bne     @tex
        inc     v
        jmp     @row
