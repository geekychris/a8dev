; Lunar Rider: draw one byte column (8 world pixels, 4 colour clocks) of
; the ground band. The C version took ~26,000 cycles a column; this ~3,000.
;
; In:  _dc_wx   world x of the column's first pixel (low 16 bits)
;      _dc_slot its byte in the ground ring (0..47)
; Each of the 4 pixels (world x, x+2, x+4, x+6) gets a kind (see view.c):
;   0..8  ground with its surface at y 192..200
;   9     a crater
;   10    a rock (rocks are one world pixel: drawn where one is within -2..+3)
;   11    a mine (within -1..+2)
; and the column's 38 lines are the pixels' patterns (GPAT, 38 bytes a
; kind, each byte the pixel value in all four positions) masked together,
; stored at the ring slot and slot + 48 (the ring is stored twice).

        .export _draw_col, _draw_col_kinds, _draw_col_lines, _dc_wx, _dc_slot, _dc_first, _dc_end
        .import _terrain

GPAT    = $9800
GROUND  = $B000
GSTRIDE = 96
GLINES  = 38
GW      = 48
; only lines 7..25 (world y 190..215) differ between kinds: above is sky
; for all of them, below regolith (view.c fills those once)
GFIRST  = 7
GLAST   = 25

        .segment "ZEROPAGE"
tp:     .res 2
pp:     .res 8                  ; the four pixels' pattern pointers
dst:    .res 2
wx:     .res 2
n:      .res 2
kx:     .res 1
lin:    .res 1
tmp:    .res 1

        .bss
_dc_wx:   .res 2
_dc_slot: .res 1
_dc_first: .res 1               ; draw_col_lines: lines dc_first..dc_end-1
_dc_end:  .res 1

        .rodata
gp_lo:  .repeat 12, k
        .byte <(GPAT + k * GLINES)
        .endrepeat
line_lo: .repeat GLINES, l
        .byte <(GROUND + l * GSTRIDE)
        .endrepeat
line_hi: .repeat GLINES, l
        .byte >(GROUND + l * GSTRIDE)
        .endrepeat
gp_hi:  .repeat 12, k
        .byte >(GPAT + k * GLINES)
        .endrepeat

        .code

; the whole column
_draw_col:
        jsr _draw_col_kinds
        lda #GFIRST
        sta _dc_first
        lda #GLAST + 1
        sta _dc_end
        jmp _draw_col_lines

; the pixels' kinds (kept in pp for draw_col_lines)
_draw_col_kinds:
        lda _dc_wx
        sta wx
        lda _dc_wx+1
        sta wx+1
        ldx #0
@pix:   jsr kind
        tay
        lda gp_lo,y
        sta pp,x
        lda gp_hi,y
        sta pp+1,x
        clc
        lda wx
        adc #2
        sta wx
        bcc :+
        inc wx+1
:       inx
        inx
        cpx #8
        bne @pix
        rts

; lines dc_first..dc_end-1 of the column whose kinds draw_col_kinds found
; (a column can be drawn over two frames)
_draw_col_lines:
        ldx _dc_first           ; dst = line dc_first + slot
        stx lin
        clc
        lda line_lo,x
        adc _dc_slot
        sta dst
        lda line_hi,x
        adc #0
        sta dst+1
        ldy lin

line:   lda (pp),y
        and #$C0
        sta tmp
        lda (pp+2),y
        and #$30
        ora tmp
        sta tmp
        lda (pp+4),y
        and #$0C
        ora tmp
        sta tmp
        lda (pp+6),y
        and #$03
        ora tmp
        ldy #0
        sta (dst),y
        ldy #GW
        sta (dst),y
        clc
        lda dst
        adc #GSTRIDE
        sta dst
        bcc :+
        inc dst+1
:       inc lin
        ldy lin
        cpy _dc_end
        bne line
        rts

; A = the kind of the pixel at wx (X kept)
kind:   stx kx
        clc
        lda wx
        adc #<_terrain
        sta tp
        lda wx+1
        and #3
        adc #>_terrain
        sta tp+1
        ldy #0
        lda (tp),y
        cmp #1                  ; TERRAIN_CRATER_SM
        beq @crater
        cmp #2                  ; TERRAIN_CRATER_LG
        beq @crater
        sec                     ; n = (wx - 2) & 1023
        lda wx
        sbc #2
        sta n
        lda wx+1
        sbc #0
        and #3
        sta n+1
        ldx #0                  ; d = 0..5 (offsets -2..+3)
@nb:    clc
        lda n
        adc #<_terrain
        sta tp
        lda n+1
        adc #>_terrain
        sta tp+1
        lda (tp),y
        cmp #3                  ; TERRAIN_ROCK
        beq @rock
        cmp #5                  ; TERRAIN_MINE, offsets -1..+2
        bne @next
        cpx #1
        bcc @next
        cpx #5
        bcc @mine
@next:  inc n
        bne :+
        inc n+1
        lda n+1
        and #3
        sta n+1
:       inx
        cpx #6
        bne @nb
        ldx kx                  ; the ground: surface 200 - bump, kind
        lda wx                  ; 8 - bump (a hill's bump is in bits 4-7)
        clc
        adc #<_terrain
        sta tp
        lda wx+1
        and #3
        adc #>_terrain
        sta tp+1
        lda (tp),y
        lsr
        lsr
        lsr
        lsr
        eor #$FF
        sec
        adc #8                  ; 8 - bump
        rts
@crater:
        lda #9
        ldx kx
        rts
@rock:  lda #10
        ldx kx
        rts
@mine:  lda #11
        ldx kx
        rts
