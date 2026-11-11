; Lunar Rider: drawing the players and missiles (the C took 1,000 to 4,000
; cycles a sprite: cc65 keeps locals on its software stack).
;
; Coordinates are the original's: x 0..319, y 0..255 (signed 16-bit).
;   horizontal: HPOS = 48 + x / 2      (16 + (x + 64) / 2)
;   vertical:   PM line = 32 + y * 3 / 4 (2 + (y + 40) * 3 / 4)
;
; ply_draw: player pd_n at (pd_x, pd_y), pd_h lines of pd_shape. The
; player's memory is only rewritten when the PM line, the height or the
; shape key (pd_key) changed; off screen it is cleared and parked at HPOS 0.
;
; missiles_draw: clears last frame's missile lines, then draws M0/M1 (the
; forward bullets, 2 lines), M2 (both upward bullets, 3 lines each, they
; share x) and M3 (the bomb mis_bomb, 3 lines; $FF = none).

        .export _ply_draw_asm, _pd_n, _pd_x, _pd_y, _pd_shape, _pd_h, _pd_key
        .export _p_y, _p_h, _p_key, _ply_clear_asm
        .export _missiles_draw, _mis_bomb
        .import _fb_x, _fb_y, _fb_on, _ub_x, _ub_y, _ub_on, _bm_x, _bm_y

PLAYERS = $9C                   ; page of player 0 ($9800 + $400)
MIS     = $9B00
HPOSP0  = $D000
HPOSM0  = $D004

        .segment "ZEROPAGE"
src:    .res 2
dst:    .res 2
t:      .res 2
cx:     .res 1                  ; colour-clock x
ly:     .res 1                  ; PM line
hh:     .res 1
cnt:    .res 1
msk:    .res 1
mm:     .res 1
ix:     .res 1

        .bss
_pd_n:     .res 1
_pd_x:     .res 2
_pd_y:     .res 2
_pd_shape: .res 2
_pd_h:     .res 1
_pd_key:   .res 1
_p_y:      .res 4
_p_h:      .res 4
_p_key:    .res 4
_mis_bomb: .res 1
nseg:      .res 1
seg_y:     .res 6
seg_h:     .res 6
seg_m:     .res 6
mx:        .res 2               ; the missile being drawn
my:        .res 2

        .rodata
mbits:  .byte $03, $0C, $30, $C0

        .code

; t = (A/X + 40): carry clear and result < 296 when y is on the screen
; (-40..255); then ly = 2 + t * 3 / 4. Returns C set when off screen.
pm_line:
        clc
        adc #40
        sta t
        txa
        adc #0
        sta t+1
        bne @hi                 ; t >= 256: on screen only if < 296
@ok:    lda t                   ; t * 3 / 4 = t / 2 + t / 4 (rounded as cc65's)
        sta cnt                 ; (exact: (t*3)>>2, t < 296 -> t*3 < 888)
        lda t+1
        sta msk
        asl t                   ; t*2 in t
        rol t+1
        clc
        lda t
        adc cnt
        sta t
        lda t+1
        adc msk
        lsr                     ; >> 2
        ror t
        lsr
        ror t
        lda t
        clc
        adc #2
        sta ly
        clc
        rts
@hi:    cmp #1
        bne @off
        lda t
        cmp #296 - 256
        bcc @ok
@off:   sec
        rts

; cx = 16 + (A/X + 64) / 2; C set when x is outside -32..319
pm_col:
        clc
        adc #32                 ; x + 32 must be < 352
        sta t
        txa
        adc #0
        sta t+1
        beq @in
        cmp #1
        bne @off
        lda t
        cmp #352 - 256
        bcs @off
@in:    lda t                   ; (x + 32 + 32) / 2 + 16
        clc
        adc #32
        sta t
        lda t+1
        adc #0
        lsr
        lda t
        ror
        clc
        adc #16
        sta cx
        clc
        rts
@off:   sec
        rts

; void ply_clear(void): clear player pd_n, park it
_ply_clear_asm:
        ldx _pd_n
clr:    lda _p_h,x
        beq @z
        sta cnt
        lda _p_y,x
        sta dst
        txa
        clc
        adc #PLAYERS
        sta dst+1
        ldy #0
        tya
:       sta (dst),y
        iny
        dec cnt
        bne :-
@z:     lda #0
        sta _p_h,x
        sta _p_key,x
        sta HPOSP0,x
        rts

_ply_draw_asm:
        lda _pd_x
        ldx _pd_x+1
        jsr pm_col
        bcs @gone
        lda _pd_y
        ldx _pd_y+1
        jsr pm_line
        bcs @gone
        lda _pd_h               ; clip to line 250
        sta hh
        clc
        adc ly
        bcs @clip
        cmp #251
        bcc @fits
@clip:  lda #250
        sec
        sbc ly
        sta hh
@fits:  ldx _pd_n
        lda _pd_key
        cmp _p_key,x
        bne @redraw
        lda ly
        cmp _p_y,x
        bne @redraw
        lda hh
        cmp _p_h,x
        beq @pos
@redraw:
        jsr clr                 ; X = n (kept)
        lda ly
        sta _p_y,x
        sta dst
        txa
        clc
        adc #PLAYERS
        sta dst+1
        lda hh
        sta _p_h,x
        beq @pos
        lda _pd_key
        sta _p_key,x
        lda _pd_shape
        sta src
        lda _pd_shape+1
        sta src+1
        ldy #0
:       lda (src),y
        sta (dst),y
        iny
        cpy hh
        bne :-
@pos:   lda cx
        sta HPOSP0,x
        rts
@gone:  ldx _pd_n
        jmp clr

; one missile: mm = number, hh = lines, mx/my = position
mis_put:
        lda mx
        ldx mx+1
        jsr pm_col
        bcs @no
        lda my
        ldx my+1
        jsr pm_line
        bcs @no
        ldx nseg
        cpx #6
        bcs @no
        lda ly
        sta seg_y,x
        lda hh
        sta seg_h,x
        sta cnt
        ldy mm
        lda mbits,y
        sta msk
        eor #$FF
        sta seg_m,x
        inc nseg
        ldy ly
:       lda MIS,y
        ora msk
        sta MIS,y
        iny
        dec cnt
        bne :-
        ldx mm
        lda cx
        sta HPOSM0,x
@no:    rts

_missiles_draw:
        ldx nseg                ; clear last frame's lines
        beq @drawn
@seg:   dex
        lda seg_y,x
        tay
        lda seg_h,x
        sta cnt
        lda seg_m,x
        sta msk
:       lda MIS,y
        and msk
        sta MIS,y
        iny
        dec cnt
        bne :-
        txa
        bne @seg
@drawn: lda #0
        sta nseg
        sta HPOSM0
        sta HPOSM0+1
        sta HPOSM0+2
        sta HPOSM0+3

        lda #0                  ; M0, M1: forward bullets
        sta mm
@fb:    ldx mm
        lda _fb_on,x
        beq @fbn
        txa
        asl
        tay
        lda _fb_x,y
        sta mx
        lda _fb_x+1,y
        sta mx+1
        lda _fb_y,y
        sta my
        lda _fb_y+1,y
        sta my+1
        lda #2
        sta hh
        jsr mis_put
@fbn:   inc mm
        lda mm
        cmp #2
        bcc @fb

        ldx #0                  ; M2: the upward bullets
@ub:    lda _ub_on,x
        beq @ubn
        stx ix
        txa
        asl
        tay
        lda _ub_x,y
        sta mx
        lda _ub_x+1,y
        sta mx+1
        lda _ub_y,y
        sta my
        lda _ub_y+1,y
        sta my+1
        lda #2
        sta mm
        lda #3
        sta hh
        jsr mis_put
        ldx ix
@ubn:   inx
        cpx #2
        bcc @ub

        ldx _mis_bomb           ; M3: one bomb, 1 left and up of its x, y
        bmi @done
        txa
        asl
        tay
        sec
        lda _bm_x,y
        sbc #1
        sta mx
        lda _bm_x+1,y
        sbc #0
        sta mx+1
        sec
        lda _bm_y,y
        sbc #1
        sta my
        lda _bm_y+1,y
        sbc #0
        sta my+1
        lda #3
        sta mm
        sta hh
        jsr mis_put
@done:  rts
