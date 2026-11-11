; proj.s - a row of the ground's grid points onto the screen (view.c):
;   sx = 80 + (x16 >> 4), x16 += step       (1/16 pixel, the row's x step)
;   sy = horizon + ((dy - h) * ky) >> 8 >> 4 (dy: the camera's height, m)
; port: in C it was 2.5K cycles a point.
        .export _proj_row, _pr_h, _pr_n, _pr_x, _pr_step, _pr_dy, _pr_ky, _pr_sx, _pr_sy
        .import mul_t_p, tx, pl, ph, r0, r1
        .importzp a3d_t

HORIZON = 7

hp      = a3d_t+0           ; heights
sxp     = a3d_t+2
syp     = a3d_t+4
sg      = a3d_t+6
k       = a3d_t+7

.segment "LOWRAM"
_pr_h:      .res 2
_pr_n:      .res 1
_pr_x:      .res 2
_pr_step:   .res 2
_pr_dy:     .res 2
_pr_ky:     .res 2
_pr_sx:     .res 2          ; where to put them
_pr_sy:     .res 2

.segment "CODE"
_proj_row:
        lda     _pr_h
        sta     hp
        lda     _pr_h+1
        sta     hp+1
        lda     _pr_sx
        sta     sxp
        lda     _pr_sx+1
        sta     sxp+1
        lda     _pr_sy
        sta     syp
        lda     _pr_sy+1
        sta     syp+1
        lda     #0
        sta     k
@v:     ; ---- sx = 80 + (x >> 4)
        lda     _pr_x+1
        sta     tx+1
        lda     _pr_x
        ldx     #4
:       pha
        lda     tx+1
        cmp     #$80
        ror     tx+1
        pla
        ror     a
        dex
        bne     :-
        clc
        adc     #80
        ldy     #0
        sta     (sxp),y
        lda     tx+1
        adc     #0
        iny
        sta     (sxp),y
        clc
        lda     _pr_x
        adc     _pr_step
        sta     _pr_x
        lda     _pr_x+1
        adc     _pr_step+1
        sta     _pr_x+1
        ; ---- d = dy - h (signed 16)
        ldy     k
        lda     (hp),y
        ldx     #0
        cmp     #$80
        bcc     :+
        dex
:       sta     tx
        stx     tx+1
        sec
        lda     _pr_dy
        sbc     tx
        sta     tx
        lda     _pr_dy+1
        sbc     tx+1
        sta     tx+1
        sta     sg
        bpl     :+
        sec
        lda     #0
        sbc     tx
        sta     tx
        lda     #0
        sbc     tx+1
        sta     tx+1
:       lda     tx+1                ; |d| <= 255 (the camera is never that high above it)
        beq     :+
        lda     #$FF
        sta     tx
:       lda     tx                  ; r = ky * |d| >> 8: |d| the 8-bit multiplier
        sta     pl
        lda     #0
        sta     ph
        lda     _pr_ky
        sta     tx
        lda     _pr_ky+1
        sta     tx+1
        jsr     mul_t_p
        ; >> 4, the sign, + the horizon
        ldx     #4
:       lsr     r1
        ror     r0
        dex
        bne     :-
        bit     sg
        bpl     :+
        sec
        lda     #0
        sbc     r0
        sta     r0
        lda     #0
        sbc     r1
        sta     r1
:       clc
        lda     r0
        adc     #HORIZON
        ldy     #0
        sta     (syp),y
        lda     r1
        adc     #0
        iny
        sta     (syp),y
        ; next
        clc
        lda     sxp
        adc     #2
        sta     sxp
        bcc     :+
        inc     sxp+1
:       clc
        lda     syp
        adc     #2
        sta     syp
        bcc     :+
        inc     syp+1
:       inc     k
        lda     k
        cmp     _pr_n
        beq     :+
        jmp     @v
:       rts
