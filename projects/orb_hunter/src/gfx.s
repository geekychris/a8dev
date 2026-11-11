; Orb Hunter: the room's pixels, in 6502.
;
; The room is ANTIC E, 160 x 180 (tile rows 1..15 of the original's 16;
; row 0 is under the HUD): line L at $A010 + 40 L. A tile is 8 pixels
; (2 bytes) x 12 lines.
;
; draw_tile    tile dt_tile at tile (dt_x 0..19, dt_row 1..15)
; draw_room    every tile of room[][] (rows 1..15)
; dots_flush   the XORed points (bombs, enemy shots, sparks): erases each
;              old one that moved, draws the new; C fills dn_px / dn_ln
;              (pixel 0..159, line 0..179; $FF: none), dn_w (1 or 2 pixels)
; ply_put      a player: pp_n at HPOS pp_x, PM line pp_line, pp_h lines of
;              pp_shape; rewritten only when line / height / key change

        .export _draw_tile, _draw_room, _dt_tile, _dt_x, _dt_row
        .export _dots_flush, _dn_px, _dn_ln, _dn_w, _dots_reset
        .export _ply_put, _pp_n, _pp_x, _pp_line, _pp_h, _pp_key, _pp_shape
        .export _p_line, _p_h, _p_key
        .import _room, _tile_gfx

BITMAP  = $A010
LINES   = 180
NDOTS   = 40
PLAYERS = $9C

        .segment "ZEROPAGE"
ptr:    .res 2
src:    .res 2
tmp:    .res 1
cnt:    .res 1
px:     .res 1
col:    .res 1

        .bss
_dt_tile: .res 1
_dt_x:    .res 1
_dt_row:  .res 1
_dn_px:   .res NDOTS
_dn_ln:   .res NDOTS
_dn_w:    .res NDOTS
do_px:    .res NDOTS            ; drawn
do_ln:    .res NDOTS
do_w:     .res NDOTS
_pp_n:    .res 1
_pp_x:    .res 1
_pp_line: .res 1
_pp_h:    .res 1
_pp_key:  .res 1
_pp_shape: .res 2
_p_line:  .res 4
_p_h:     .res 4
_p_key:   .res 4

        .rodata
line_lo: .repeat LINES, l
        .byte <(BITMAP + l * 40)
        .endrepeat
line_hi: .repeat LINES, l
        .byte >(BITMAP + l * 40)
        .endrepeat
tile_lo: .repeat 14, t           ; 10 tiles + lava's 4 frames, 24 bytes each
        .byte <(_tile_gfx + t * 24)
        .endrepeat
tile_hi: .repeat 14, t
        .byte >(_tile_gfx + t * 24)
        .endrepeat
mask3:  .byte $C0, $30, $0C, $03

        .code

; ---------------------------------------------------------------- tiles
; a tile: dst is set so that (dst),y with the source's y (2 L, 2 L + 1 on
; line L) is the right byte: 40 bytes a line, minus the 2 y moved on
_draw_tile:
        ldx _dt_tile
        lda tile_lo,x
        sta src
        lda tile_hi,x
        sta src+1
        ldx _dt_row             ; first line: (row - 1) * 12
        dex
        txa
        asl
        sta tmp                 ; 2 (row - 1)
        asl
        clc
        adc tmp                 ; 6 (row - 1)
        asl                     ; 12 (row - 1)
        tax
        lda _dt_x
        asl
        clc
        adc line_lo,x
        sta ptr
        lda line_hi,x
        adc #0
        sta ptr+1
        ldy #0
        ldx #12
@line:  lda (src),y
        sta (ptr),y
        iny
        lda (src),y
        sta (ptr),y
        iny
        clc                     ; the next line: + 40 - 2
        lda ptr
        adc #38
        sta ptr
        bcc :+
        inc ptr+1
:       dex
        bne @line
        rts

; the whole room: the bitmap cleared (unrolled: 29 pages, x down), then
; the tiles that aren't empty
_draw_room:
        lda #0
        ldx #0
@clr:   .repeat 28, k
        sta BITMAP + k * 256,x
        .endrepeat
        inx
        bne @clr
        ldx #(LINES * 40 - 28 * 256) - 1
@tail:  sta BITMAP + 28 * 256,x
        dex
        cpx #$FF
        bne @tail
        lda #1
        sta _dt_row
@row:   lda #0
        sta _dt_x
@col:   lda _dt_row             ; room[row][x]: _room + row * 20 + x
        asl
        asl
        sta tmp                 ; 4 row
        asl
        asl                     ; 16 row
        clc
        adc tmp                 ; 20 row (row <= 15: < 256)
        adc _dt_x
        tax
        lda _room,x
        beq @skip               ; empty (and the open door: 7)
        cmp #7
        beq @skip
        sta _dt_tile
        jsr _draw_tile
@skip:  inc _dt_x
        lda _dt_x
        cmp #20
        bne @col
        inc _dt_row
        lda _dt_row
        cmp #16
        bne @row
        rts

; ---------------------------------------------------------------- dots
; XOR colour 3 at (px, A = line), width tmp (1 or 2)
xordot: tax
        lda line_lo,x
        sta ptr
        lda line_hi,x
        sta ptr+1
        lda px
        lsr
        lsr
        tay
        lda px
        and #3
        tax
        lda (ptr),y
        eor mask3,x
        sta (ptr),y
        lda tmp
        cmp #2
        bne @one
        inx                     ; the next pixel (maybe in the next byte)
        cpx #4
        bne :+
        ldx #0
        iny
:       lda (ptr),y
        eor mask3,x
        sta (ptr),y
@one:   rts

_dots_flush:
        ldx #0
@d:     stx cnt
        lda _dn_px,x            ; unchanged: nothing to do
        cmp do_px,x
        bne @chg
        lda _dn_ln,x
        cmp do_ln,x
        bne @chg
        jmp @next
@chg:   lda do_px,x             ; erase the old
        cmp #$FF
        beq @new
        sta px
        lda do_w,x
        sta tmp
        lda do_ln,x
        jsr xordot
        ldx cnt
@new:   lda _dn_px,x
        sta do_px,x
        lda _dn_ln,x
        sta do_ln,x
        lda _dn_w,x
        sta do_w,x
        lda _dn_px,x
        cmp #$FF
        beq @next
        sta px
        lda _dn_w,x
        sta tmp
        lda _dn_ln,x
        jsr xordot
@next:  ldx cnt
        inx
        cpx #NDOTS
        bne @d
        rts

; forget the drawn dots (the room was redrawn)
_dots_reset:
        ldx #NDOTS - 1
        lda #$FF
:       sta do_px,x
        sta _dn_px,x
        dex
        bpl :-
        rts

; ---------------------------------------------------------------- players
_ply_put:
        ldx _pp_n
        lda _pp_h
        beq @clear
        lda _pp_key
        cmp _p_key,x
        bne @redraw
        lda _pp_line
        cmp _p_line,x
        bne @redraw
        lda _pp_h
        cmp _p_h,x
        beq @pos
@redraw:
        jsr clr
        lda _pp_line
        sta _p_line,x
        sta ptr
        txa
        clc
        adc #PLAYERS
        sta ptr+1
        lda _pp_h
        sta _p_h,x
        sta cnt
        lda _pp_key
        sta _p_key,x
        lda _pp_shape
        sta src
        lda _pp_shape+1
        sta src+1
        ldy #0
:       lda (src),y
        sta (ptr),y
        iny
        cpy cnt
        bne :-
@pos:   lda _pp_x
        sta $D000,x
        rts
@clear: jsr clr
        lda #0
        sta $D000,x
        rts

clr:    lda _p_h,x
        beq @z
        sta cnt
        lda _p_line,x
        sta ptr
        txa
        clc
        adc #PLAYERS
        sta ptr+1
        ldy #0
        tya
:       sta (ptr),y
        iny
        dec cnt
        bne :-
@z:     lda #0
        sta _p_h,x
        sta _p_key,x
        rts
