; Rock Blaster - drawing into the 160 x 108 ANTIC D bitmap (4 pixels a
; byte, rows of 2 scan lines), all with XOR: drawing a thing again takes it
; off, so nothing else has to be redrawn, whatever overlaps.
;
; Tables (render.c fills them): for a pixel x (colour clocks) at index
; x + 32, x taken mod 160: xbyte = its byte in the row, pm1/pm2/pm3 = its
; bits in colour 1/2/3. For a row y at index y + 16, y mod 108: ylo/yhi =
; the row's address.

        .export _rock_draw, _particles_draw, _bullets_draw
        .export _rd_data, _rd_ox, _rd_oy, _rd_nx, _rd_ny, _rd_col, _rd_mode
        .export _xbyte, _pm1, _pm2, _pm3, _ylo, _yhi
        .export _pd_on, _bd_on
        .import _pt_x, _pt_y, _pt_active, _bl_x, _bl_y, _bl_active

        .zeropage
lp:     .res 2                  ; the line being drawn
dp:     .res 2                  ; the shape's data
m:      .res 1
cnt:    .res 1

        .bss
_xbyte: .res 224
_pm1:   .res 224
_pm2:   .res 224
_pm3:   .res 224
_ylo:   .res 256
_yhi:   .res 256
_rd_data: .res 2                ; rock outline (shapes.c: rows, dy0, rows of count + dx+32)
_rd_ox: .res 1                  ; old position (colour clocks, lines): erased
_rd_oy: .res 1
_rd_nx: .res 1                  ; new position: drawn
_rd_ny: .res 1
_rd_col: .res 1                 ; 1..3
_rd_mode: .res 1                ; bit 0: erase at old, bit 1: draw at new
rows:   .res 1
rowx:   .res 1
yo:     .res 1
yn:     .res 1
_pd_on: .res 64                 ; particles: drawn, where
pd_x:   .res 64
pd_y:   .res 64
_bd_on: .res 8                  ; bullets
bd_x:   .res 8
bd_y:   .res 8
nx:     .res 1
ny:     .res 1

        .code

; set a self-modified absolute operand to base + offset
.macro PATCH site, base, off
        clc
        lda #<base
        adc off
        sta site+1
        lda #>base
        adc #0
        sta site+2
.endmacro

; void rock_draw(void): erase the outline at (rd_ox, rd_oy) and/or draw it
; at (rd_nx, rd_ny), row by row, top to bottom, both in the same sweep, so
; the picture is never without the rock (at worst the rows above the one
; being done are at the new place and those below at the old).
.proc _rock_draw
        lda _rd_data
        sta dp
        sta ro_c+1
        sta ro_p+1
        sta rn_c+1
        sta rn_p+1
        lda _rd_data+1
        sta dp+1
        sta ro_c+2
        sta ro_p+2
        sta rn_c+2
        sta rn_p+2
        ; the colour's mask table, offset by each position
        lda _rd_col
        cmp #2
        beq c2
        bcs c3
        PATCH ro_m, _pm1, _rd_ox
        PATCH rn_m, _pm1, _rd_nx
        jmp cdone
c2:     PATCH ro_m, _pm2, _rd_ox
        PATCH rn_m, _pm2, _rd_nx
        jmp cdone
c3:     PATCH ro_m, _pm3, _rd_ox
        PATCH rn_m, _pm3, _rd_nx
cdone:  PATCH ro_b, _xbyte, _rd_ox
        PATCH rn_b, _xbyte, _rd_nx
        ldy #0
        lda (dp),y
        sta rows
        iny
        lda (dp),y              ; dy of the first row
        clc
        adc #16
        tax
        clc
        adc _rd_oy
        sta yo
        txa
        clc
        adc _rd_ny
        sta yn
        ldx #2
row:    stx rowx
        lda _rd_mode
        lsr a
        bcc noold
        ldy yo
        lda _ylo,y
        sta lp
        lda _yhi,y
        sta lp+1
        jsr row_old
noold:  lda _rd_mode
        and #2
        beq nonew
        ldy yn
        lda _ylo,y
        sta lp
        lda _yhi,y
        sta lp+1
        ldx rowx
        jsr row_new
nonew:  inc yo
        inc yn
        dec rows
        bne row
        rts

; one row of pixels at the old / new place: X = the row's count byte; on
; return X = the next row's
row_old:
ro_c:   lda $FFFF,x
        sta cnt
        inx
ro_p:   ldy $FFFF,x             ; dx + 32
ro_m:   lda $FFFF,y             ; pmN + ox
        sta m
ro_b:   lda $FFFF,y             ; xbyte + ox
        tay
        lda (lp),y
        eor m
        sta (lp),y
        inx
        dec cnt
        bne ro_p
        rts

row_new:
rn_c:   lda $FFFF,x
        sta cnt
        inx
rn_p:   ldy $FFFF,x
rn_m:   lda $FFFF,y
        sta m
rn_b:   lda $FFFF,y
        tay
        lda (lp),y
        eor m
        sta (lp),y
        inx
        dec cnt
        bne rn_p
        rts
.endproc

; XOR one colour-3 pixel at (A = x, Y = y + 16): X kept
.proc xor_pixel
        pha
        lda _ylo,y
        sta lp
        lda _yhi,y
        sta lp+1
        pla
        clc
        adc #32
        tay
        lda _pm3,y
        sta m
        lda _xbyte,y
        tay
        lda (lp),y
        eor m
        sta (lp),y
        rts
.endproc

; screen position of a pos7 pair: nx = x >> 8, ny = y >> 8 (the high
; bytes: 2 world pixels a bitmap pixel).  Y = 2 * index
.macro SCREEN_POS xs, ys
        lda xs+1,y
        sta nx
        lda ys+1,y
        sta ny
.endmacro

; void particles_draw(void): take off the particles that moved or went,
; draw the ones that are new or moved
.proc _particles_draw
        ldx #63
loop:   lda _pt_active,x
        beq off
        txa
        asl a
        tay
        SCREEN_POS _pt_x, _pt_y
        lda _pd_on,x
        beq draw
        lda nx
        cmp pd_x,x
        bne move
        lda ny
        cmp pd_y,x
        beq next
move:   jsr erase
draw:   lda ny
        sta pd_y,x
        clc
        adc #16
        tay
        lda nx
        sta pd_x,x
        jsr xor_pixel
        lda #1
        sta _pd_on,x
        bne next
off:    lda _pd_on,x
        beq next
        jsr erase
        lda #0
        sta _pd_on,x
next:   dex
        bpl loop
        rts
erase:  lda pd_y,x
        clc
        adc #16
        tay
        lda pd_x,x
        jmp xor_pixel
.endproc

; void bullets_draw(void): the same for the bullets
.proc _bullets_draw
        ldx #7
loop:   lda _bl_active,x
        beq off
        txa
        asl a
        tay
        SCREEN_POS _bl_x, _bl_y
        lda _bd_on,x
        beq draw
        lda nx
        cmp bd_x,x
        bne move
        lda ny
        cmp bd_y,x
        beq next
move:   jsr erase
draw:   lda ny
        sta bd_y,x
        lda nx
        sta bd_x,x
        jsr plot2
        lda #1
        sta _bd_on,x
        bne next
off:    lda _bd_on,x
        beq next
        jsr erase
        lda #0
        sta _bd_on,x
next:   dex
        bpl loop
        rts
erase:
plot2:  lda bd_y,x
        clc
        adc #16
        tay
        lda bd_x,x
        jmp xor_pixel
.endproc

; ---------------------------------------------------------------- the ship
; void pl_poly(const unsigned char *verts, unsigned char n): the closed
; outline through n corners (column 0..15, line 0..24) ORed into plbuf
; (25 bytes of the left player, then 25 of the right), Bresenham with both
; ends drawn, as graphics.library's Draw.
        .export _pl_poly, _pl_verts, _plbuf
        .bss
_plbuf: .res 50
_pl_verts: .res 2
pn:     .res 1
pk:     .res 1
x0:     .res 1
y0:     .res 1
x1:     .res 1
y1:     .res 1
ddx:    .res 1
ddy:    .res 1
sx:     .res 1
sy:     .res 1
err:    .res 1
e2:     .res 1
        .code
.proc _pl_poly
        sta pn
        lda _pl_verts
        sta dp
        lda _pl_verts+1
        sta dp+1
        lda #0
        sta pk
edge:   lda pk                  ; corner k -> corner k+1 (mod n)
        asl a
        tay
        lda (dp),y
        sta x0
        iny
        lda (dp),y
        sta y0
        iny
        ldx pk
        inx
        cpx pn
        bne @nw
        ldy #0
@nw:    lda (dp),y
        sta x1
        iny
        lda (dp),y
        sta y1
        jsr line
        inc pk
        lda pk
        cmp pn
        bne edge
        rts

line:   ; ddx = |x1 - x0|, sx = sign; ddy = -|y1 - y0|, sy = sign
        lda #1
        sta sx
        sec
        lda x1
        sbc x0
        bcs @xp
        eor #$FF
        adc #1
        ldx #$FF
        stx sx
@xp:    sta ddx
        lda #1
        sta sy
        sec
        lda y1
        sbc y0
        bcs @yp
        eor #$FF
        adc #1
        ldx #$FF
        stx sy
@yp:    eor #$FF                ; ddy = -|dy|
        clc
        adc #1
        sta ddy
        clc
        adc ddx
        sta err
@pix:   lda x0                  ; plot
        tax
        lda y0
        cpx #8
        bcc @l
        adc #24                 ; (+25: carry set)
@l:     tay
        txa
        and #7
        tax
        lda _plbuf,y
        ora bits,x
        sta _plbuf,y
        lda x0
        cmp x1
        bne @go
        lda y0
        cmp y1
        beq @end
@go:    lda err
        asl a
        sta e2
        sec                     ; e2 >= ddy ?
        sbc ddy
        bmi @noy
        lda err
        clc
        adc ddy
        sta err
        lda x0
        clc
        adc sx
        sta x0
@noy:   lda ddx                 ; e2 <= ddx ?
        sec
        sbc e2
        bmi @pix
        lda err
        clc
        adc ddx
        sta err
        lda y0
        clc
        adc sy
        sta y0
        jmp @pix
@end:   rts
.endproc

        .rodata
bits:   .byte $80, $40, $20, $10, $08, $04, $02, $01

; ---------------------------------------------------------------- rocks
; void rocks_sync(void): bring the rocks on the screen up to date with the
; game: erase the ones that went, draw the new ones, move the ones whose
; pixel position changed (one sweep). Round robin from rr_next; a rock is
; only started when its cost (shapes.c rock_cost, in VCOUNT units) fits
; before the end of the frame's time (VCOUNT_END units after the vertical
; blank); the rest wait for the next frame (rocks_pending counts them).
        .export _rocks_sync, _rocks_pending, _rr_next, _d_on
        .import _rk_active, _rk_size, _rk_shape, _rk_x, _rk_y
        .import _rock_data, _rock_shape_off, _rock_cost

VCOUNT     = $D40B
VCOUNT_VBI = 124                ; the vertical blank starts at line 248
VCOUNT_END = 148                ; stop starting rocks this far after it

        .bss
_d_on:  .res 24                 ; what is on the screen
d_s:    .res 24
d_x:    .res 24
d_y:    .res 24
_rocks_pending: .res 1
_rr_next: .res 1
ri:     .res 1
kc:     .res 1
on:     .res 1
s:      .res 1
tx:     .res 1
ty:     .res 1

        .code
.proc _rocks_sync
        lda #0
        sta _rocks_pending
        lda #24
        sta kc
        ldx _rr_next
loop:   stx ri
        lda _rk_active,x
        sta on
        beq gone
        lda _rk_size,x          ; the shape it should have
        asl a
        asl a
        ora _rk_shape,x
        sta s
        txa
        asl a
        tay
        lda _rk_x+1,y           ; x >> 8, y >> 8: the bitmap pixel
        sta tx
        lda _rk_y+1,y
        sta ty
        lda _d_on,x
        beq work
        lda d_s,x
        cmp s
        bne work
        lda d_x,x
        cmp tx
        bne work
        lda d_y,x
        cmp ty
        bne work
        jmp next
gone:   lda _d_on,x
        bne work
        jmp next
work:   ; time? (cost of the shape on screen, or of the new one)
        ldy s
        lda _d_on,x
        beq @c
        ldy d_s,x
@c:     lda VCOUNT
        sec
        sbc #VCOUNT_VBI
        bcs @t
        adc #156                ; PAL: 156 counts a frame
@t:     clc
        adc _rock_cost,y
        cmp #VCOUNT_END
        bcc go
        lda _rocks_pending      ; no: next frame starts here
        bne @p
        stx _rr_next
@p:     inc _rocks_pending
        jmp next
go:     lda _d_on,x
        beq draw
        ; erase at the old place (and draw at the new in the same sweep)
        lda d_x,x
        sta _rd_ox
        lda d_y,x
        sta _rd_oy
        lda #1
        sta _rd_mode
        lda on
        beq @e
        lda d_s,x
        cmp s
        bne @e
        lda tx
        sta _rd_nx
        lda ty
        sta _rd_ny
        lda #3
        sta _rd_mode
@e:     lda d_s,x
        jsr setshape
        jsr _rock_draw
        ldx ri
        lda #0
        sta _d_on,x
        lda _rd_mode
        cmp #3
        beq placed
        lda on
        beq next
draw:   lda tx
        sta _rd_nx
        lda ty
        sta _rd_ny
        lda #2
        sta _rd_mode
        lda s
        jsr setshape
        jsr _rock_draw
        ldx ri
placed: lda #1
        sta _d_on,x
        lda s
        sta d_s,x
        lda tx
        sta d_x,x
        lda ty
        sta d_y,x
next:   ldx ri
        inx
        cpx #24
        bne @w
        ldx #0
@w:     dec kc
        beq done
        jmp loop
done:   lda _rocks_pending
        bne @r
        sta _rr_next
@r:     rts

; rd_data = rock_data + rock_shape_off[A], rd_col = 1 (large) / 2
setshape:
        pha
        asl a
        tay
        clc
        lda #<_rock_data
        adc _rock_shape_off,y
        sta _rd_data
        lda #>_rock_data
        adc _rock_shape_off+1,y
        sta _rd_data+1
        pla
        cmp #4
        lda #1
        bcc @l
        lda #2
@l:     sta _rd_col
        rts
.endproc
