; Sky Knights: XOR sprites in the ANTIC E field (enemies and pods).
;
; xs_draw: XORs xs_h lines of xs_w bytes from xs_img (2-bit masks, already
; shifted for x & 3) ANDed with xs_col (the colour in all four pixels:
; $55, $AA or $FF) at byte column xs_bx (signed: knights slide off both
; edges, as they wrap) and line xs_ln (signed) of the field. Bytes and lines
; outside the field are skipped. Drawing twice in the same place rubs it out.
;
; The field: 176 lines of 40 bytes from $A010 (line 102 is $B000).

;
; The fast path (all of it in the field: nearly always): the unrolled blits
; generated in xfast.inc, from xs_ci (the colour: 0 $AA, 1 $FF, 2 $55) and
; xs_ii (the image), ~17 cycles a byte instead of ~40.

        .export _xs_draw, _xs_img, _xs_bx, _xs_ln, _xs_h, _xs_w, _xs_col
        .export _xs_ci, _xs_ii

FIELD   = $A010
LINES   = 176

        .segment "ZEROPAGE"
src:    .res 2
dst:    .res 2
ln:     .res 1                  ; (ln, n: also the blit's vector)
n:      .res 1
k0:     .res 1                  ; the first and last+1 byte of a row drawn
k1:     .res 1

        .bss
_xs_img: .res 2
_xs_bx:  .res 1                 ; signed
_xs_ln:  .res 1                 ; signed (-20..175)
_xs_h:   .res 1
_xs_w:   .res 1
_xs_col: .res 1
_xs_ci:  .res 1
_xs_ii:  .res 1

        .rodata
line_lo: .repeat LINES, l
        .byte <(FIELD + l * 40)
        .endrepeat
line_hi: .repeat LINES, l
        .byte >(FIELD + l * 40)
        .endrepeat
colx_en:  .byte 0, EN_N, EN_N * 2
en_jlo: .byte <en0_blit, <en1_blit, <en2_blit, <en3_blit
en_jhi: .byte >en0_blit, >en1_blit, >en2_blit, >en3_blit
colx_egg: .byte 0, EGG_N, EGG_N * 2

        .include "xfast.inc"

        .code
_xs_draw:
        ldx _xs_ln              ; the fast path: in the field?
        lda _xs_w
        cmp #3
        bne @egg
        cpx #LINES - 17 + 1
        bcs @slow
        lda _xs_bx
        cmp #40 - 3 + 1
        bcs @slow
        jsr @dst
        lda _xs_ii              ; X = colour * 8 + frame (image / 4)
        lsr a
        lsr a
        ldx _xs_ci
        clc
        adc colx_en,x
        tax
        lda _xs_ii              ; the blit for its shift (image & 3)
        and #3
        tay
        lda en_jlo,y
        sta ln
        lda en_jhi,y
        sta n
        jmp (ln)
@egg:   cpx #LINES - 6 + 1
        bcs @slow
        lda _xs_bx
        cmp #40 - 2 + 1
        bcs @slow
        jsr @dst
        ldx _xs_ci
        lda colx_egg,x
        clc
        adc _xs_ii
        tax
        jmp egg_blit
@dst:   clc                     ; dst = line X + bx (A)
        adc line_lo,x
        sta dst
        lda line_hi,x
        adc #0
        sta dst+1
        rts

@slow:
        lda _xs_img
        sta src
        lda _xs_img+1
        sta src+1
        ; the bytes of a row in the field: k from max(0, -bx) to
        ; min(w, 40 - bx)
        lda #0
        sta k0
        lda _xs_w
        sta k1
        lda _xs_bx
        bpl @pos
        eor #$FF                ; -bx
        clc
        adc #1
        cmp _xs_w
        bcs @done               ; all off the left
        sta k0
        jmp @rows
@pos:   cmp #40
        bcs @done               ; all off the right
        lda #40
        sec
        sbc _xs_bx              ; 40 - bx
        cmp k1
        bcs @rows
        sta k1
@rows:  lda _xs_ln
        sta ln
        lda _xs_h
        sta n
@row:   lda ln                  ; a line in the field?
        cmp #LINES              ; (negative lines are >= 176 unsigned)
        bcs @skip
        tax
        clc
        lda line_lo,x
        adc _xs_bx              ; bx signed: the row's start (may be before
        sta dst                 ; the line: only k0.. is used)
        lda _xs_bx
        and #$80
        beq :+
        lda #$FF
:       adc line_hi,x
        sta dst+1
        ldy k0
@byte:  lda (src),y
        and _xs_col
        eor (dst),y
        sta (dst),y
        iny
        cpy k1
        bcc @byte
@skip:  clc                     ; the next row of the image
        lda src
        adc _xs_w
        sta src
        bcc :+
        inc src+1
:       inc ln
        dec n
        bne @row
@done:  rts

; ---- the objects: what each one has drawn, and ob_set
;
; ob_set: object o_k (0-7 enemies, 8-15 pods) to be shown (o_on) at byte
; column o_bx, line o_ln, image o_img, colour index o_col. Nothing is done
; when that is what is drawn; otherwise the old one is rubbed out (XORed
; again) and the new one drawn. (In C it was ~1.7K cycles besides the blits.)

        .export _ob_set, _ob_on, _o_k, _o_on, _o_bx, _o_ln, _o_img, _o_col
        .import _en_img, _egg_img

NOBJ    = 16
NEN     = 8

        .bss
_o_k:   .res 1
_o_on:  .res 1
_o_bx:  .res 1
_o_ln:  .res 1
_o_img: .res 1
_o_col: .res 1
_ob_on: .res NOBJ
ob_bx:  .res NOBJ
ob_ln:  .res NOBJ
ob_img: .res NOBJ
ob_col: .res NOBJ

        .rodata
en_lo:  .repeat 32, i
        .byte <(_en_img + i * 51)
        .endrepeat
en_hi:  .repeat 32, i
        .byte >(_en_img + i * 51)
        .endrepeat
egg_lo: .repeat 4, i
        .byte <(_egg_img + i * 12)
        .endrepeat
egg_hi: .repeat 4, i
        .byte >(_egg_img + i * 12)
        .endrepeat
col_mask: .byte $AA, $FF, $55

        .code
_ob_set:
        ldx _o_k
        lda _ob_on,x
        beq @new
        lda _o_on
        beq @rub
        lda ob_bx,x                     ; drawn there already?
        cmp _o_bx
        bne @rub
        lda ob_ln,x
        cmp _o_ln
        bne @rub
        lda ob_img,x
        cmp _o_img
        bne @rub
        lda ob_col,x
        cmp _o_col
        bne @rub
        rts
@rub:   jsr ob_xor
        ldx _o_k
@new:   lda _o_on
        sta _ob_on,x
        beq @done
        lda _o_bx
        sta ob_bx,x
        lda _o_ln
        sta ob_ln,x
        lda _o_img
        sta ob_img,x
        lda _o_col
        sta ob_col,x
        jmp ob_xor
@done:  rts

; XOR object X as it is drawn
ob_xor: lda ob_bx,x
        sta _xs_bx
        lda ob_ln,x
        sta _xs_ln
        ldy ob_col,x
        sty _xs_ci
        lda col_mask,y
        sta _xs_col
        ldy ob_img,x
        sty _xs_ii
        cpx #NEN
        bcs @egg
        lda en_lo,y
        sta _xs_img
        lda en_hi,y
        sta _xs_img+1
        lda #17
        sta _xs_h
        lda #3
        sta _xs_w
        jmp _xs_draw
@egg:   lda egg_lo,y
        sta _xs_img
        lda egg_hi,y
        sta _xs_img+1
        lda #6
        sta _xs_h
        lda #2
        sta _xs_w
        jmp _xs_draw

; ---- view_enemies: the enemies' place, image and colour from the bodies
; (view.c's loop in C was ~2K cycles an enemy before the blits):
;   column: q = (px + 66) / 2 (pixel x / 2 + 1, + 32), byte q / 4 - 8, image
;           (facing * 4 + flapping * 3) * 4 + q & 3
;   line:   (py - 2) * 3 / 4 - 8, at least 0 (2 lines low at the ceiling,
;           where the blit would clip)
; ve_hide set: rub them all out (the title). ve_par 0 or 1: only the even
; or the odd ones are moved this time ($FF: all).

        .export _view_enemies, _ve_hide, _ve_par
        .import _e_active, _e_type, _b_px, _b_py, _b_facing, _b_anim_frame

        .segment "ZEROPAGE"
vi:     .res 1
vq:     .res 1
vt:     .res 1

        .bss
_ve_hide: .res 1
_ve_par: .res 1

        .code
_view_enemies:
        lda #0
        sta vi
@loop:  ldx vi
        stx _o_k
        lda _ve_hide
        bne @off_
        lda _e_active,x
        bne :+
@off_:  jmp @off
:       lda _ve_par                     ; only half of them this time?
        bmi :+
        eor vi
        and #1
        beq :+
        jmp @next
:       txa
        asl a
        tay                             ; Y = i * 2 (b_px of body i + 2)
        clc
        lda _b_px+4,y
        adc #66
        sta vq
        lda _b_px+5,y
        adc #0
        lsr a
        ror vq                          ; q
        lda vq
        lsr a
        lsr a
        sec
        sbc #8
        sta _o_bx
        lda _b_facing+2,x               ; f = facing * 4 + flapping * 3
        asl a
        asl a
        ldy _b_anim_frame+2,x
        beq :+
        clc
        adc #3
:       asl a                           ; f * 4 + q & 3
        asl a
        sta vt
        lda vq
        and #3
        ora vt
        sta _o_img
        lda _b_py+2,x                   ; (py - 2) * 3 / 4 - 8
        sec
        sbc #2
        sta vt
        asl a
        sta vq
        lda #0
        rol a
        tay
        lda vq
        clc
        adc vt
        sta vq
        tya
        adc #0
        lsr a
        ror vq
        lsr a
        ror vq
        lda vq
        sec
        sbc #8
        bcs :+                          ; at the ceiling (y 10..12): line 0,
        lda #0                          ; not -2..-1 (the fast blit)
:       sta _o_ln
        lda _e_type,x
        sta _o_col
        lda #1
        sta _o_on
        jsr _ob_set
        jmp @next
@off:   lda _ob_on,x
        beq @next
        lda #0
        sta _o_on
        jsr _ob_set
@next:  inc vi
        lda vi
        cmp #NEN
        beq :+
        jmp @loop
:       rts

; ---- player_put: player pp_n's shape (17 lines from pp_shape) at line
; pp_line, its horizontal place pp_hx; the shape is copied only when its
; line or frame (pp_frame) changed, the old lines cleared

        .export _pp_put, _pp_n, _pp_shape, _pp_line, _pp_frame, _pp_hx, _pl_line, _pl_frame

        .segment "ZEROPAGE"
pm:     .res 2
_pp_shape: .res 2

        .data
_pl_frame: .byte $FF, $FF, $FF, $FF
        .bss
_pl_line: .res 4
_pp_n:  .res 1
_pp_line: .res 1
_pp_frame: .res 1
_pp_hx: .res 1

        .code
_pp_put:
        ldx _pp_n
        lda _pl_line,x
        cmp _pp_line
        bne @copy
        lda _pl_frame,x
        cmp _pp_frame
        beq @hx
@copy:  txa
        clc
        adc #$9C
        sta pm+1
        lda _pl_line,x                  ; clear the old lines
        sta pm
        lda #0
        ldy #16
:       sta (pm),y
        dey
        bpl :-
        lda _pp_line
        sta pm
        sta _pl_line,x
        lda _pp_frame
        sta _pl_frame,x
        ldy #16
:       lda (_pp_shape),y
        sta (pm),y
        dey
        bpl :-
@hx:    lda _pp_hx
        sta $D000,x
        rts
