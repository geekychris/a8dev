; sprites.s - the players and the ring's missiles, placed by the vertical
; blank (called from vbi.s) so what moves moves every frame (50 Hz) while
; the main loop draws at 25.
;
; The main loop fills two slots (the next frame and the one after: halfway
; between the last two ticks, then at the tick), then sets spr_pend = 1;
; each VBI applies one slot and the second clears spr_pend. Per player:
; HPOS, SIZE, colour, and the shape (h rows from a pointer) at scan line y;
; the old shape is wiped only when the shape or its line changed. The ring
; (missiles 0 and 1, scan lines y0..y1-1) is set from slot 0 only, the
; frame the new bitmap (with the ring's floor and ceiling lines) is shown.
        .export _spr_pend, _sl_x, _sl_y, _sl_h, _sl_lo, _sl_hi, _sl_size, _sl_col
        .export _sl_mx0, _sl_mx1, _sl_my0, _sl_my1, spr_vbi

HPOSP0 = $D000
HPOSM0 = $D004
SIZEP0 = $D008
COLPM0 = $D012
PCOLR0 = $02C0
MISS   = $9B00
PLAY   = $9C00

.segment "ZEROPAGE"
src:    .res 2
dst:    .res 2

.segment "DATA"
_spr_pend: .byte 0
; [slot * 4 + player]
_sl_x:    .res 8
_sl_y:    .res 8
_sl_h:    .res 8
_sl_lo:   .res 8
_sl_hi:   .res 8
_sl_size: .res 8
_sl_col:  .res 8
_sl_mx0:  .byte 0
_sl_mx1:  .byte 0
_sl_my0:  .byte 0
_sl_my1:  .byte 0
; what is in player memory now
cur_y:  .res 4
cur_h:  .byte 0, 0, 0, 0
cur_lo: .res 4
cur_hi: .res 4
my0:    .byte 0
my1:    .byte 0
pl:     .byte 0
cnt:    .byte 0

.segment "CODE"
spr_vbi:
        lda     _spr_pend
        bne     :+
        rts
:       sec
        sbc     #1
        asl     a
        asl     a
        tax                             ; X = slot * 4
        ldy     #0
@pl:    sty     pl
        lda     _sl_x,x
        sta     HPOSP0,y
        lda     _sl_size,x
        sta     SIZEP0,y
        lda     _sl_col,x
        sta     PCOLR0,y
        sta     COLPM0,y
        ; the shape: changed?
        lda     _sl_y,x
        cmp     cur_y,y
        bne     @redraw
        lda     _sl_h,x
        cmp     cur_h,y
        bne     @redraw
        lda     _sl_lo,x
        cmp     cur_lo,y
        bne     @redraw
        lda     _sl_hi,x
        cmp     cur_hi,y
        beq     @next
@redraw:
        tya
        clc
        adc     #>PLAY
        sta     dst+1
        lda     cur_y,y                 ; wipe the old one
        sta     dst
        lda     cur_h,y
        beq     @new
        sta     cnt
        tya
        pha
        ldy     #0
        tya
@wipe:  sta     (dst),y
        iny
        cpy     cnt
        bne     @wipe
        pla
        tay
@new:   lda     _sl_y,x
        sta     cur_y,y
        sta     dst
        lda     _sl_h,x
        sta     cur_h,y
        sta     cnt
        lda     _sl_lo,x
        sta     cur_lo,y
        sta     src
        lda     _sl_hi,x
        sta     cur_hi,y
        sta     src+1
        lda     cnt
        beq     @next
        ldy     #0
@copy:  lda     (src),y
        sta     (dst),y
        iny
        cpy     cnt
        bne     @copy
@next:  inx
        ldy     pl
        iny
        cpy     #4
        beq     :+
        jmp     @pl
:
        ; the ring, on the first frame
        lda     _spr_pend
        cmp     #1
        beq     :+
        jmp     @done
:       lda     _sl_mx0
        sta     HPOSM0
        lda     _sl_mx1
        sta     HPOSM0+1
        ; the missiles' lines: only the ends that moved (all of it when
        ; the ranges don't overlap)
        lda     _sl_my1
        cmp     my0
        bcc     @all
        beq     @all
        lda     _sl_my0
        cmp     my1
        bcs     @all
        ldy     _sl_my0                 ; the top: set y0..my0-1 or wipe my0..y0-1
        cpy     my0
        bcs     @tw
@ts:    lda     #$05
        sta     MISS,y
        iny
        cpy     my0
        bcc     @ts
        bcs     @bot
@tw:    ldy     my0
@tw1:   cpy     _sl_my0
        bcs     @bot
        lda     #0
        sta     MISS,y
        iny
        bne     @tw1
@bot:   ldy     my1                     ; the bottom: set my1..y1-1 or wipe y1..my1-1
        cpy     _sl_my1
        bcs     @bw
@bs:    lda     #$05
        sta     MISS,y
        iny
        cpy     _sl_my1
        bcc     @bs
        bcs     @mdone
@bw:    ldy     _sl_my1
@bw1:   cpy     my1
        bcs     @mdone
        lda     #0
        sta     MISS,y
        iny
        bne     @bw1
        beq     @mdone
@all:   ldy     my0                     ; wipe the old range
@mw:    cpy     my1
        bcs     @ms
        lda     #0
        sta     MISS,y
        iny
        bne     @mw
@ms:    ldy     _sl_my0                 ; set the new one
@mset:  cpy     _sl_my1
        bcs     @mdone
        lda     #$05
        sta     MISS,y
        iny
        bne     @mset
@mdone: lda     _sl_my0
        sta     my0
        lda     _sl_my1
        sta     my1
@done:  inc     _spr_pend
        lda     _spr_pend
        cmp     #3
        bcc     :+
        lda     #0
        sta     _spr_pend
:       rts
