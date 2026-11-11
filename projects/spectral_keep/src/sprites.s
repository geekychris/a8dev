; sprites.s - the moving characters as players, moved every frame in the
; vertical blank between their places at the last two logic steps (the
; rules step 12.5 times a second, a step taking ~3 frames: the VBI runs
; while bank 0 is mapped, so all of this is in LOWCODE / LOWDATA).
;
; Players 0 + 1 are the explorer (multicolour: their overlap is the third
; colour), 2 and 3 a room's two other characters. For each player the front
; end fills the spare set (spr_set ^ 1) after a step: its rows, height, the
; place it moves from (the one shown now) and to, its width; then sets
; spr_flip. The VBI takes the new set and restarts the 4 frames.

        .export _spr_vbi, _spr_rows, _spr_h, _spr_x0, _spr_x1, _spr_y0, _spr_y1
        .export _spr_size, _spr_flip, _spr_set, _spr_shown_x, _spr_shown_y, _spr_reset
        .import _os_vbi_exit

PM      = $B400                 ; players (PMBASE $B0, single line)
HPOSP0  = $D000
SIZEP0  = $D008
MAXH    = 48

.segment "SCRBSS"
_spr_rows:  .res 2 * 4 * MAXH   ; [set][player][row]
_spr_h:     .res 2 * 4          ; [set][player]: rows (0: hidden)
_spr_x0:    .res 2 * 4          ; HPOS from, to
_spr_x1:    .res 2 * 4
_spr_y0:    .res 2 * 4          ; the top row (PM row) from, to
_spr_y1:    .res 2 * 4
_spr_size:  .res 2 * 4          ; SIZEP
_spr_flip:  .res 1             ; 1: a new set is ready
_spr_set:   .res 1             ; the set shown
_spr_shown_x: .res 4            ; where each player is now
_spr_shown_y: .res 4
sf:     .res 1                 ; frames into the step (0..4)
cur_y:  .res 4                  ; the rows drawn now: from, height
cur_h:  .res 4
redraw: .res 4
sp_:    .res 1
sq:     .res 1
t16:    .res 2

.segment "XCODE1"

; void spr_reset(void): nothing shown
_spr_reset:
        ldx     #3
@c:     lda     #0
        sta     cur_h,x
        sta     _spr_h,x
        sta     _spr_h+4,x
        sta     HPOSP0,x
        dex
        bpl     @c
        ldx     #0
        txa
@z:     sta     PM,x
        sta     PM+$100,x
        sta     PM+$200,x
        sta     PM+$300,x
        inx
        bne     @z
        lda     #4
        sta     sf
        rts

; the interpolation: A = from + (to - from) * sf / 4  (from at X+set*4.. in
; tables given by self-modified addresses would be neater; this takes the
; two values in t_from, t_to)
interp: lda     sf
        cmp     #4
        bcs     @to
        cmp     #0
        beq     @from
        ; d = to - from (signed 9 bits in t16)
        sec
        lda     t_to
        sbc     t_from
        sta     t16
        lda     #0
        sbc     #0
        sta     t16+1           ; (0 or $FF)
        ldx     sf
@m:     ; d * f: add d sf times into acc
        lda     #0
        sta     acc
        sta     acc+1
@add:   clc
        lda     acc
        adc     t16
        sta     acc
        lda     acc+1
        adc     t16+1
        sta     acc+1
        dex
        bne     @add
        ; / 4 (arithmetic)
        lda     acc+1
        cmp     #$80
        ror     acc+1
        ror     acc
        lda     acc+1
        cmp     #$80
        ror     acc+1
        ror     acc
        clc
        lda     t_from
        adc     acc
        rts
@to:    lda     t_to
        rts
@from:  lda     t_from
        rts

.segment "SCRBSS"
t_from: .res 1
t_to:   .res 1
acc:    .res 2
set4:   .res 1                 ; the shown set * 4
setr:   .res 2                 ; its rows' offset (set * 4 * MAXH)

.segment "XCODE1"

; the deferred VBI (os.s jumps here through VVBLKD)
_spr_vbi:
        lda     _spr_flip
        beq     @noflip
        lda     #0
        sta     _spr_flip
        lda     _spr_set
        eor     #1
        sta     _spr_set
        lda     #0
        sta     sf
        ldx     #3
        lda     #1
@r:     sta     redraw,x        ; new rows: draw them all again
        dex
        bpl     @r
        jmp     @go
@noflip:
        lda     sf
        cmp     #4
        bcc     @inc
        jmp     _os_vbi_exit    ; (settled)
@inc:   inc     sf
@go:    lda     _spr_set
        asl
        asl
        sta     set4
        lda     #0
        sta     setr+1
        lda     _spr_set        ; set * 4 * 48 = set * 192
        beq     :+
        lda     #192
:       sta     setr
        lda     #0
        sta     sp_
@player:
        lda     sp_
        clc
        adc     set4
        tax                     ; X = set * 4 + sp_
        lda     _spr_x0,x
        sta     t_from
        lda     _spr_x1,x
        sta     t_to
        txa
        pha
        jsr     interp
        ldx     sp_
        sta     HPOSP0,x
        sta     _spr_shown_x,x
        pla
        tax
        lda     _spr_size,x
        ldy     sp_
        sta     SIZEP0,y
        lda     _spr_y0,x
        sta     t_from
        lda     _spr_y1,x
        sta     t_to
        txa
        pha
        jsr     interp
        sta     sq               ; the new top row
        pla
        tax
        ldy     sp_
        lda     sq
        sta     _spr_shown_y,y
        lda     sq
        cmp     cur_y,y
        bne     @move
        lda     redraw,y
        beq     @nextp
@move:  ; clear the old rows: PM + sp_ * 256 + cur_y .. + cur_h
        lda     #0
        sta     redraw,y
        lda     #<PM
        sta     dst
        tya
        clc
        adc     #>PM
        sta     dst+1
        lda     cur_h,y
        beq     @draw
        sta     cnt
        ldx     cur_y,y
        lda     #0
@clr:   jsr     st_x
        inx
        dec     cnt
        bne     @clr
@draw:  ldy     sp_
        lda     sq
        sta     cur_y,y
        tya
        clc
        adc     set4
        tax
        lda     _spr_h,x
        sta     cur_h,y
        beq     @nextp
        sta     cnt
        ; the rows: _spr_rows + setr + sp_ * 48
        lda     #<_spr_rows
        clc
        adc     setr
        sta     src
        lda     #>_spr_rows
        adc     setr+1
        sta     src+1
        ldx     sp_
        beq     @r0
@rp:    clc
        lda     src
        adc     #MAXH
        sta     src
        bcc     :+
        inc     src+1
:       dex
        bne     @rp
@r0:    ldy     #0
        ldx     sq
@cp:    lda     (src),y         ; (zero-page pointer below)
        jsr     st_x
        iny
        inx
        dec     cnt
        bne     @cp
@nextp: inc     sp_
        lda     sp_
        cmp     #4
        beq     @end
        jmp     @player
@end:   jmp     _os_vbi_exit

; store A at dst + X (dst: a player's 256 bytes)
st_x:   sta     $FFFF,x         ; (self-modified: dst)
        rts
dst     = st_x + 1
src     = $F0                   ; (zero page: free with the OS ROM off)

.segment "SCRBSS"
cnt:    .res 1
