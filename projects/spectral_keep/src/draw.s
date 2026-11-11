; draw.s - drawing into the room's picture (ANTIC E, 40 bytes a line,
; line addresses in line_lo / line_hi), from the banks. In LOWCODE: it runs
; with bank 1 or 2 mapped.
;
; A piece is h, wb, then h rows of wb bytes (4 pixels a byte, left in the
; high bits; pixel value 0 is transparent). It is drawn at any pixel x
; (shifted on the way), lines clipped to 0..207, bytes to 0..39.
;
;   void draw_ops(void)      the room's draw list: dr_n ops of (piece, x,
;                            y + 8, kind) at dr_ops (bank 1), the pieces
;                            from bank 2 (_gfx_pieces); kind 2 (a gate) is
;                            skipped when dr_nogates
;   void draw_bmp(void)      bitmap sprite db_id (bank 2, _gfx_bmp: h, wb,
;                            x offset, y offset, rows) at db_x, db_y (the
;                            point it's anchored to; db_y signed)
;   void bmp_box(void)       its box: db_x0 (byte), db_w (bytes), db_y0, db_h

        .macpack longbranch
        .export _draw_ops, _draw_bmp, _bmp_box
        .export _dr_ops, _dr_n, _dr_nogates, _db_id, _db_x, _db_y
        .export _db_x0, _db_w, _db_y0, _db_h
        .import _gfx_pieces, _gfx_bmp, _bank_select
        .importzp ptr1, ptr2, ptr3, ptr4, tmp1, tmp2, tmp3, tmp4

LINE_LO = $A100                 ; (view.h: the line address tables)
LINE_HI = $A1D0
LINES   = 208

.segment "SCRBSS"
_dr_ops:     .res 2
_dr_n:       .res 2
_dr_nogates: .res 1
_db_id:  .res 1
_db_x:   .res 2                ; signed
_db_y:   .res 2                ; signed
_db_x0:  .res 1
_db_w:   .res 1
_db_y0:  .res 1
_db_h:   .res 1
bx:     .res 2                 ; the piece's left pixel (signed)
by:     .res 2                 ; its top line (signed)
h:      .res 1
wb:     .res 1
sh:     .res 1                 ; the shift: 0..3 pixels
col:    .res 1                 ; the first byte (bx >> 2, clipped later)
coll:   .res 1                 ; (signed)
row:    .res 1
k:      .res 1
sd:     .res 1                 ; the spill into the next byte: data, mask
sm:     .res 1
od:     .res 1
om:     .res 1
cur:    .res 1
opn:    .res 2

.segment "XCODE2"

; the piece at ptr1 (h, wb, rows): its rows at bx, by
blit_hdr:
        ldy     #0
        lda     (ptr1),y
        sta     h
        iny
        lda     (ptr1),y
        sta     wb
        clc
        lda     ptr1
        adc     #2
        sta     ptr1
        bcc     blit
        inc     ptr1+1
; h rows of wb bytes at ptr1, at bx, by. Bank 2 is mapped: its first
; pages are the tables (gen_gfx.py): $4000 each byte's mask (11 where a
; pixel isn't 0), then for a shift of 1..3 pixels the byte's part in its
; own byte (>> 2s) and the part spilt into the next (<< 8 - 2s).
; Lines are clipped; a piece that would cross the left or right edge is
; left out (no room's does).
MASKT   = $4000
blit:   lda     bx
        and     #3
        sta     sh
        lda     bx+1            ; col = bx >> 2 (0..255 only)
        beq     :+
        rts
:       lda     bx
        lsr
        lsr
        sta     col
        clc                     ; col + wb (+ 1 when shifted) <= 40
        adc     wb
        ldx     sh
        beq     :+
        adc     #1
:       cmp     #41
        bcc     :+
        rts
:       txa
        beq     bl_noshift
        asl
        clc
        adc     #>MASKT - 1     ; hi: $41, $43, $45; lo: + 1
        sta     op_hi1 + 2
        sta     op_hi2 + 2
        adc     #1
        sta     op_lo1 + 2
        sta     op_lo2 + 2
bl_noshift:
        lda     #0
        sta     row
bl_row:   lda     row
        cmp     h
        bcc     :+
        rts
:       clc                     ; the line: by + row, 0..207
        lda     by
        adc     row
        tax
        lda     by+1
        adc     #0
        jne     bl_skip           ; negative or >= 256
        cpx     #LINES
        jcs     bl_skip
        clc
        lda     LINE_LO,x
        adc     col
        sta     ptr2
        lda     LINE_HI,x
        adc     #0
        sta     ptr2+1
        ldy     #0
        lda     sh
        bne     bl_shifted
bl_b0:    lda     (ptr1),y        ; not shifted: (screen & ~mask) | byte
        tax
        lda     MASKT,x
        eor     #$FF
        and     (ptr2),y
        ora     (ptr1),y
        sta     (ptr2),y
        iny
        cpy     wb
        bne     bl_b0
        jmp     bl_skip
bl_shifted:
        sty     sd
        sty     sm
bl_bs:    lda     (ptr1),y
        tax
op_hi1: lda     $4100,x
        ora     sd
        sta     od
op_lo1: lda     $4200,x
        sta     sd
        lda     MASKT,x
        tax
op_hi2: lda     $4100,x
        ora     sm
        eor     #$FF
        and     (ptr2),y
        ora     od
        sta     (ptr2),y
op_lo2: lda     $4200,x
        sta     sm
        iny
        cpy     wb
        bne     bl_bs
        lda     sm              ; the spill into the next byte
        eor     #$FF
        and     (ptr2),y
        ora     sd
        sta     (ptr2),y
bl_skip:  clc                     ; the next row of the piece
        lda     ptr1
        adc     wb
        sta     ptr1
        bcc     :+
        inc     ptr1+1
:       inc     row
        jmp     bl_row

_draw_ops:
        lda     _dr_ops
        sta     ptr3
        lda     _dr_ops+1
        sta     ptr3+1
        lda     _dr_n
        sta     opn
        lda     _dr_n+1
        sta     opn+1
@op:    lda     opn
        ora     opn+1
        beq     @done
        lda     #1              ; the op (bank 1)
        jsr     _bank_select
        ldy     #3
        lda     (ptr3),y        ; kind 2: a gate
        cmp     #2
        bne     @draw
        lda     _dr_nogates
        bne     @nextop
@draw:  ldy     #1
        lda     (ptr3),y
        sta     bx
        lda     #0
        sta     bx+1
        iny
        sec
        lda     (ptr3),y        ; y + 8
        sbc     #8
        sta     by
        lda     #0
        sbc     #0
        sta     by+1
        ldy     #0
        lda     (ptr3),y
        asl
        tax
        lda     #2              ; the piece (bank 2)
        jsr     _bank_select
        lda     _gfx_pieces,x
        sta     ptr1
        lda     _gfx_pieces+1,x
        sta     ptr1+1
        lda     ptr3            ; (blit uses ptr1, ptr2)
        pha
        lda     ptr3+1
        pha
        jsr     blit_hdr
        pla
        sta     ptr3+1
        pla
        sta     ptr3
@nextop:
        clc
        lda     ptr3
        adc     #4
        sta     ptr3
        bcc     :+
        inc     ptr3+1
:       lda     opn
        bne     :+
        dec     opn+1
:       dec     opn
        jmp     @op
@done:  lda     #$FF
        jmp     _bank_select

; the bitmap sprite's header into ptr1 / bx / by (bank 2 mapped)
bmp_head:
        lda     #2
        jsr     _bank_select
        lda     _db_id
        asl
        tax
        lda     _gfx_bmp,x
        sta     ptr1
        lda     _gfx_bmp+1,x
        sta     ptr1+1
        ldy     #2              ; x offset (signed)
        lda     (ptr1),y
        tax
        clc
        adc     _db_x
        sta     bx
        txa
        and     #$80
        beq     :+
        lda     #$FF
:       adc     _db_x+1
        sta     bx+1
        iny                     ; y offset (signed)
        lda     (ptr1),y
        tax
        clc
        adc     _db_y
        sta     by
        txa
        and     #$80
        beq     :+
        lda     #$FF
:       adc     _db_y+1
        sta     by+1
        rts

_draw_bmp:
        jsr     bmp_head
        ldy     #0
        lda     (ptr1),y
        sta     h
        iny
        lda     (ptr1),y
        sta     wb
        clc                     ; the rows: after h, wb, x offset, y offset
        lda     ptr1
        adc     #4
        sta     ptr1
        bcc     :+
        inc     ptr1+1
:       jsr     blit
        lda     #$FF
        jmp     _bank_select

; its box, for restoring: bytes db_x0 .. +db_w, lines db_y0 .. +db_h (clipped)
_bmp_box:
        jsr     bmp_head
        ldy     #0
        lda     (ptr1),y
        sta     _db_h
        iny
        lda     (ptr1),y
        clc
        adc     #1              ; (the shift spills into one more byte)
        sta     _db_w
        lda     bx+1            ; x0 = bx >> 2 (clipped at 0)
        bmi     @x0
        lda     bx+1
        lsr
        lda     bx
        ror
        lsr
        sta     _db_x0
        jmp     @y
@x0:    lda     #0
        sta     _db_x0
@y:     lda     by+1
        bmi     @y0
        bne     @off
        lda     by
        sta     _db_y0
        jmp     @done
@y0:    clc                     ; above the top: h += by, y0 = 0
        lda     _db_h
        adc     by
        bcs     :+
        lda     #0
:       sta     _db_h
        lda     #0
        sta     _db_y0
        jmp     @done
@off:   lda     #0
        sta     _db_h
@done:  lda     #$FF
        jmp     _bank_select

; ---- the bare room's picture in bank 3 (line L at $4000 + L * 40)

        .export _save_bg, _restore_box

; addr of bank 3's line X into ptr3: $4000 + X * 40
bg_line:
        lda     #0
        sta     ptr3+1
        txa
        asl                     ; X * 8 (16 bits)
        rol     ptr3+1
        asl
        rol     ptr3+1
        asl
        rol     ptr3+1
        sta     ptr3
        lda     ptr3+1
        sta     tmp1
        lda     ptr3            ; + X * 32 = X * 8 * 4
        asl
        rol     tmp1
        asl
        rol     tmp1
        clc
        adc     ptr3
        sta     ptr3
        lda     tmp1
        adc     ptr3+1
        adc     #$40
        sta     ptr3+1
        rts

; void save_bg(void): the screen's 208 lines into bank 3
_save_bg:
        lda     #3
        jsr     _bank_select
        ldx     #0
@l:     jsr     bg_line
        lda     LINE_LO,x
        sta     ptr2
        lda     LINE_HI,x
        sta     ptr2+1
        ldy     #39
@b:     lda     (ptr2),y
        sta     (ptr3),y
        dey
        bpl     @b
        inx
        cpx     #LINES
        bne     @l
        lda     #$FF
        jmp     _bank_select

; void restore_box(void): bytes db_x0 .. + db_w, lines db_y0 .. + db_h
; from bank 3 to the screen (clipped to the screen)
_restore_box:
        lda     _db_h
        beq     @done2
        lda     #3
        jsr     _bank_select
        lda     _db_y0
        sta     row
@l:     ldx     row
        cpx     #LINES
        bcs     @done
        jsr     bg_line
        lda     LINE_LO,x
        sta     ptr2
        lda     LINE_HI,x
        sta     ptr2+1
        ldy     _db_x0
        lda     _db_w
        sta     k
@b:     cpy     #40
        bcs     @nb
        lda     (ptr3),y
        sta     (ptr2),y
        iny
        dec     k
        bne     @b
@nb:    inc     row
        sec
        lda     row
        sbc     _db_y0
        cmp     _db_h
        bcc     @l
@done:  lda     #$FF
        jmp     _bank_select
@done2: rts
