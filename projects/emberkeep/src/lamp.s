; Emberkeep: the lantern. The chamber is drawn whole into an image (in a
; 130XE bank, or main memory on a 64K machine); the screen shows only the
; lit part of it, everything else black. Each frame lamp_update compares
; every line's lit pixels with last frame's and writes only the bytes at
; the edges that changed: copied from
; the image (masked to the lit pixels) or blacked out. The lit square is
; lr_*: x X0..X1, lines Y0..Y1, its corners cut by ch pixels and cv lines
; where lr_fl says (1 top left, 2 top right, 4 bottom left, 8 bottom
; right); a line's range is worked out from it (and from last frame's).
; lr_dirty[row]: a tile row whose image changed (its lit lines recopied).
; lamp_tile draws a tile into the image.
;
; Both run with the image's bank mapped at $4000-$7FFF, so they, their
; tables and the interrupts' code are below it (LOWCODE / LOWDATA / LOWBSS).

        .export _lamp_update, _lamp_tile, _lamp_reset, _lr, _lr_dirty
        .export _lp_img, _lp_bank, _lt_col, _lt_row, _lt_tile, _lamp_chamber, _lc_open
        .import _xe_bank_select, _tile_gfx, _grid

FIELD   = $A010
LINES   = 180

        .segment "ZEROPAGE"
src:    .res 2
dst:    .res 2
n0:     .res 1                          ; this line's new pixel range
n1:     .res 1
o0:     .res 1
o1:     .res 1
nb0:    .res 1                          ; its bytes (nb1 < nb0: none)
nb1:    .res 1
bb:     .res 1
bend:   .res 1
ly:     .res 1
full:   .res 1
pm:     .res 2
tb24:   .res 1
zn:     .res 1
segend: .res 1
t0:     .res 1
t1:     .res 1

        .segment "LOWBSS"
_lr:    .res 7                          ; X0, X1, Y0, Y1, ch, cv, fl (new)
lro:    .res 7                          ; last frame's
_lr_dirty: .res 15
zb:     .res 24                         ; a run of lines' bytes to write,
zm:     .res 24                         ; their masks
_lp_img: .res 2                         ; the image (its line 0)
_lp_bank: .res 1                        ; its bank, $FF: main memory
ylast:  .res 1
trow:   .res 1                          ; the line's tile row, its line in it
tline:  .res 1
_lt_col: .res 1
_lt_row: .res 1
_lt_tile: .res 1
_lc_open: .res 1                        ; the portal drawn open
rowoff: .res 20                         ; a tile row's tiles (x 24)
crow:   .res 1

        .segment "LOWDATA"
lmask:  .byte $FF, $3F, $0F, $03        ; pixels n .. 3 of a byte
rmask:  .byte $C0, $F0, $FC, $FF        ; pixels 0 .. n

        .segment "LOWCODE"
map:    lda _lp_bank
        cmp #$FF
        beq :+
        jmp _xe_bank_select
:       rts
unmap:  lda _lp_bank
        cmp #$FF
        beq :+
        lda #$FF
        jmp _xe_bank_select
:       rts

; list the bytes bb .. bend of a line with their masks (zb / zm, zn of
; them): the lit pixels' (bytes nb0 .. nb1; the first masked from pixel
; n0, the last to pixel n1: both & 3 by now), 0 (black) outside them
list:   ldy bb
@byte:  cpy nb0
        bcc @black
        cpy nb1
        beq @in
        bcs @black
@in:    lda #$FF
        cpy nb0
        bne :+
        ldx n0
        lda lmask,x
:       cpy nb1
        bne :+
        ldx n1
        and rmask,x
:       jmp @st
@black: lda #0
@st:    ldx zn
        sta zm,x
        tya
        sta zb,x
        inc zn
        cpy bend
        beq @done
        iny
        jmp @byte
@done:  rts

; a line's lit range from lr (X = 0) or lro (X = 7): t0, t1 (t1 <= t0:
; none). Its corner cuts: in the top cv lines (left: fl 1, right: 2) and
; the bottom cv lines (4, 8).
X0 = 0
X1 = 1
Y0 = 2
Y1 = 3
CH = 4
CV = 5
FL = 6
rng:    lda ly
        cmp _lr+Y0,x
        bcc @none
        cmp _lr+Y1,x
        bcs @none
        lda _lr+X0,x
        sta t0
        lda _lr+X1,x
        sta t1
        lda _lr+Y0,x                    ; the top band: ly < Y0 + cv
        clc
        adc _lr+CV,x
        cmp ly
        beq @bot
        bcc @bot
        lda _lr+FL,x
        lsr a
        bcc :+
        jsr cutl
:       lsr a
        bcc @bot
        jsr cutr
@bot:   lda _lr+Y1,x                    ; the bottom band: ly >= Y1 - cv
        sec
        sbc _lr+CV,x
        bcc :+                          ; (cv beyond the square: all of it)
        cmp ly
        beq :+
        bcs @done
:       lda _lr+FL,x
        lsr a
        lsr a
        lsr a
        bcc :+
        jsr cutl
:       lsr a
        bcc @done
        jsr cutr
@done:  rts
@none:  lda #0
        sta t0
        sta t1
        rts
cutl:   pha                             ; t0 = X0 + ch (once)
        lda _lr+X0,x
        clc
        adc _lr+CH,x
        sta t0
        pla
        rts
cutr:   pha                             ; t1 = X1 - ch
        lda _lr+X1,x
        sec
        sbc _lr+CH,x
        sta t1
        pla
        rts

; lamp_reset: the screen is black (a new chamber): nothing lit last frame
_lamp_reset:
        ldx #6
        lda #0
:       sta lro,x
        dex
        bpl :-
        ldx #14
:       sta _lr_dirty,x
        dex
        bpl :-
        rts

_lamp_update:
        ; nothing moved and no tile changed: nothing to do
        ldx #6
:       lda _lr,x
        cmp lro,x
        bne @go
        dex
        bpl :-
        ldx #14
:       lda _lr_dirty,x
        bne @go
        dex
        bpl :-
        rts
@go:    jsr map
        lda #0
        sta segend
        ; the lines: from the higher top to the lower bottom of the two
        lda _lr+Y0
        cmp lro+Y0
        bcc :+
        ldx lro+Y1                      ; (last frame's empty: the new only)
        cpx lro+Y0
        beq :+
        lda lro+Y0
:       sta ly
        lda _lr+Y1
        ldx lro+Y1
        cpx lro+Y0
        beq :+
        cmp lro+Y1
        bcs :+
        lda lro+Y1
:       sta ylast
        ; its tile row and line
        lda ly
        ldx #0
:       cmp #12
        bcc :+
        sbc #12
        inx
        bne :-
:       stx trow
        sta tline
        ; src / dst = line ly
        lda #<FIELD
        sta dst
        lda #>FIELD
        sta dst+1
        lda #0                          ; y * 40 = y * 32 + y * 8
        sta src+1
        lda ly
        asl a
        rol src+1
        asl a
        rol src+1
        asl a
        rol src+1                       ; y * 8
        sta src
        ldx src+1
        asl a
        rol src+1
        asl a
        rol src+1                       ; y * 32
        clc
        adc src
        sta src
        txa
        adc src+1
        sta src+1
        clc                             ; dst = FIELD + y * 40, src = image + y * 40
        lda src
        adc #<FIELD
        sta dst
        lda src+1
        adc #>FIELD
        sta dst+1
        clc
        lda src
        adc _lp_img
        sta src
        lda src+1
        adc _lp_img+1
        sta src+1
@line:  lda ly
        cmp ylast
        bcc :+
        jmp @end
:       ldx trow                        ; a changed tile row: old and new ranges
        lda _lr_dirty,x                 ; together, recopied (worked out here)
        beq :+
        jsr ranges
        lda #1
        sta full
        jsr bytes
        lda #0                          ; (the next clean line works its run out again)
        sta segend
        jmp @run
:       lda ly                          ; a new run of lines: its bytes
        cmp segend
        bcc @run
        jsr ranges
        lda #0
        sta full
        jsr bytes
        jsr seg
@run:   ldx zn                          ; write them
        beq @next
        dex
:       ldy zb,x
        lda (src),y
        and zm,x
        sta (dst),y
        dex
        bpl :-
@next:  jsr next
        inc ly
        inc tline
        lda tline
        cmp #12
        bcc :+
        lda #0
        sta tline
        inc trow
:       jmp @line
@end:   ldx #6                          ; this frame's square is last frame's now
:       lda _lr,x
        sta lro,x
        dex
        bpl :-
        ldx #14
        lda #0
:       sta _lr_dirty,x
        dex
        bpl :-
        jmp unmap

; this line's new and old ranges (n0, n1, o0, o1)
ranges: ldx #0
        jsr rng
        lda t0
        sta n0
        lda t1
        sta n1
        ldx #7
        jsr rng
        lda t0
        sta o0
        lda t1
        sta o1
        rts

; the bytes to write for this line (zb, zm, zn): none when nothing changed;
; with full set, the old and new ranges together
bytes:  lda #0
        sta zn
        lda full
        bne :+
        lda n0                          ; the same as last frame: nothing
        cmp o0
        bne :+
        lda n1
        cmp o1
        bne :+
        rts
:       ; the new bytes: nb0 = n0 / 4, nb1 = (n1 - 1) / 4; n0, n1 -> n0 & 3, (n1 - 1) & 3
        lda #$FF
        sta nb0
        lda #0
        sta nb1
        lda n0
        cmp n1
        bcs @empty
        lsr a
        lsr a
        sta nb0
        lda n1
        sec
        sbc #1
        tay
        lsr a
        lsr a
        sta nb1
        tya
        and #3
        sta n1
        lda n0
        and #3
        sta n0
@empty: lda full
        beq @notfull
        ; the union of the two ranges (either may be empty)
        lda #$FF
        sta bb
        lda #0
        sta bend
        lda nb0
        cmp nb1
        beq :+
        bcs @u2
:       lda nb0
        sta bb
        lda nb1
        sta bend
@u2:    lda o0
        cmp o1
        bcs @u3
        lsr a
        lsr a
        cmp bb
        bcs :+
        sta bb
:       lda o1
        sec
        sbc #1
        lsr a
        lsr a
        cmp bend
        bcc @u3
        sta bend
@u3:    lda bend
        cmp bb
        bcc :+
        jmp list
:       rts
@notfull:
        lda o0                          ; the old range empty: all the new bytes
        cmp o1
        bcc @old
        lda nb0
        sta bb
        lda nb1
        sta bend
        cmp nb0
        bcc :+                          ; (both empty)
        jmp list
:       rts
@old:   lda nb0                         ; the new range empty: black out the old
        cmp nb1
        beq :+
        bcc :+
        lda o0
        lsr a
        lsr a
        sta bb
        lda o1
        sec
        sbc #1
        lsr a
        lsr a
        sta bend
        jmp list
:       ; both: the left edges' bytes, then the right edges'
        lda o0
        lsr a
        lsr a
        cmp nb0
        bcc :+
        lda nb0
        sta bb
        lda o0
        lsr a
        lsr a
        sta bend
        jmp @l
:       sta bb
        lda nb0
        sta bend
@l:     jsr list
        lda o1
        sec
        sbc #1
        lsr a
        lsr a
        cmp nb1
        bcc :+
        sta bend
        lda nb1
        sta bb
        jmp list
:       sta bb
        lda nb1
        sta bend
        jmp list

; segend: the next line where either square's line kind changes (its top,
; the end of its top cut, the start of its bottom cut, its bottom), or
; ylast
seg:    lda ylast
        sta segend
        ldx #0
        jsr segsq
        ldx #7
segsq:  lda _lr+Y0,x
        jsr segmin
        lda _lr+Y0,x
        clc
        adc _lr+CV,x
        jsr segmin
        lda _lr+Y1,x
        sec
        sbc _lr+CV,x
        bcc :+
        jsr segmin
:       lda _lr+Y1,x
segmin: cmp ly                          ; a boundary after this line, before segend
        beq :+
        bcc :+
        cmp segend
        bcs :+
        sta segend
:       rts

next:   clc                             ; the next line of the screen and image
        lda dst
        adc #40
        sta dst
        bcc :+
        inc dst+1
:       clc
        lda src
        adc #40
        sta src
        bcc :+
        inc src+1
:       rts

; lamp_tile: tile lt_tile at lt_col, lt_row of the image (12 lines of 2)
_lamp_tile:
        jsr map
        lda _lt_tile                    ; the tile's 24 bytes
        asl a
        asl a
        asl a
        sta bb                           ; x 8
        asl a                           ; x 16
        clc
        adc bb                           ; x 24
        tax
        lda _lp_img                     ; the image + row * 480 + col * 2
        sta dst
        lda _lp_img+1
        sta dst+1
        ldy _lt_row
        beq @col
:       clc
        lda dst
        adc #<480
        sta dst
        lda dst+1
        adc #>480
        sta dst+1
        dey
        bne :-
@col:   lda _lt_col
        asl a
        clc
        adc dst
        sta dst
        bcc :+
        inc dst+1
:       lda #12
        sta ly
@ln:    ldy #0
        lda _tile_gfx,x
        sta (dst),y
        inx
        iny
        lda _tile_gfx,x
        sta (dst),y
        inx
        clc
        lda dst
        adc #40
        sta dst
        bcc :+
        inc dst+1
:       dec ly
        bne @ln
        jmp unmap

; lamp_chamber: the whole chamber into the image, row by row (the grid is
; in main memory, maybe under the bank: each row's tiles are looked up
; first, then the bank mapped and the row drawn). The original's tiles:
; walls in a checker of its two (WALL_A where row + col is odd), the
; portal open or locked (lc_open).
;   kinds: FLOOR WALL RUNE PORTAL OIL HAZARD -> tiles 0, 1/2, 3, 6/7, 4, 5
kindtile: .byte 0, 2, 3, 6, 4, 5
_lamp_chamber:
        lda #<_grid
        sta src
        lda #>_grid
        sta src+1
        lda _lp_img
        sta dst
        lda _lp_img+1
        sta dst+1
        lda #0
        sta crow
@row:   ldy #19                         ; the row's tiles
@kind:  lda (src),y
        tax
        lda kindtile,x
        cpx #1                          ; a wall: A or B by (row + col) & 1
        bne :+
        tya
        clc
        adc crow
        and #1
        eor #1
        clc
        adc #1                          ; odd: 1 (A), even: 2 (B)
        jmp @t
:       cpx #3                          ; the portal: open or locked
        bne @t
        clc
        adc _lc_open
@t:     sta tb24
        asl a                           ; x 24
        asl a
        asl a
        sta t0
        asl a
        clc
        adc t0
        sta rowoff,y
        dey
        bpl @kind
        jsr map
        lda dst                         ; this row's tiles: 12 lines of 2 bytes
        sta pm
        lda dst+1
        sta pm+1
        lda #0
        sta ly                          ; (the column x 2)
@tile:  ldx ly
        txa
        lsr a
        tax
        lda rowoff,x
        tax
        ldy ly
        lda #12
        sta t1
@ln:    lda _tile_gfx,x
        sta (pm),y
        iny
        lda _tile_gfx+1,x
        sta (pm),y
        inx
        inx
        tya
        clc
        adc #39                         ; the next line, same column
        tay
        bcc :+
        inc pm+1                        ; (y wrapped: the page after)
:       dec t1
        bne @ln
        lda dst                         ; back to the row's first line
        sta pm
        lda dst+1
        sta pm+1
        inc ly
        inc ly
        lda ly
        cmp #40
        bcc @tile
        jsr unmap
        clc                             ; the next row: grid + 20, image + 480
        lda src
        adc #20
        sta src
        bcc :+
        inc src+1
:       clc
        lda dst
        adc #<480
        sta dst
        lda dst+1
        adc #>480
        sta dst+1
        inc crow
        lda crow
        cmp #15
        bcs :+
        jmp @row
:       rts
