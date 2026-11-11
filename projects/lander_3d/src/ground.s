; ground.s - ld_ground: the ground's height under a point, bilinear between
; the grid's four corners (port: the C version cost 4.5K cycles a call).
;   in:  lg_x, lg_z (whole metres, int16), lg_xf, lg_zf (1/256 m)
;   out: A/X = height in 1/16 m
        .export _ld_ground_, _lg_x, _lg_z, _lg_xf, _lg_zf
        .import _L, mulset_u, mul_t_p, tx, pl, ph, r0, r1
        .importzp a3d_t, a3d_m
        .include "a3d_mul.inc"

L_HALF   = _L + 3               ; offsets in Level (checked in ld_core.c)
L_CELL   = _L + 5
L_CSHIFT = _L + 6
L_H      = _L + 7
N1       = 30                   ; LD_N - 1

hp      = a3d_t+0
fx      = a3d_t+2
fz      = a3d_t+3
top     = a3d_t+4               ; (2)
bot     = a3d_t+6               ; (2)
sg      = a3d_t+8
gi      = a3d_t+9
gj      = a3d_t+10

.segment "LOWRAM"
_lg_x:  .res 2
_lg_z:  .res 2
_lg_xf: .res 1
_lg_zf: .res 1

.segment "CODE"

; A/X = metres + half -> gi (cell), the fraction (0..255) -> A
; (Y = the 1/256 fraction)
cellfrac:
        sty     sg
        clc
        adc     L_HALF
        sta     top
        txa
        adc     L_HALF+1
        sta     top+1
        bpl     :+
        lda     #0                  ; < 0: the near edge
        sta     gi
        rts
:       ; gi = m >> cshift; frac = ((m & (cell-1)) << 8 | f) >> cshift
        lda     top
        sta     bot
        lda     top+1
        sta     bot+1
        ldy     L_CSHIFT
:       lsr     bot+1
        ror     bot
        dey
        bne     :-
        lda     bot+1
        bne     @far
        lda     bot
        cmp     #N1
        bcs     @far
        sta     gi
        lda     L_CELL
        sec
        sbc     #1
        and     top                 ; m & (cell - 1)
        sta     bot+1
        lda     sg
        sta     bot
        ldy     L_CSHIFT
:       lsr     bot+1
        ror     bot
        dey
        bne     :-
        lda     bot
        rts
@far:   lda     #N1-1               ; the far edge
        sta     gi
        lda     #$FF
        rts

; top/bot = c0*16 + (c1 - c0) * f >> 4   (c0, c1 heights in m; f in A ... via fx)
; in: Y = offset of c0 in the row at hp; out: A/X
lerp4:  lda     (hp),y
        sta     tx                  ; c0
        iny
        lda     (hp),y              ; c1
        sec
        sbc     tx                  ; c1 - c0 (fits: heights are within +-127, a cell's
        sta     sg                  ;  difference within +-127 in practice)
        bpl     :+
        eor     #$FF
        clc
        adc     #1
:       jsr     mulset_u            ; |c1 - c0| * fx
        ldy     fx
        MUL                         ; X low, A high
        sta     r1
        txa
        ldx     #4                  ; >> 4
:       lsr     r1
        ror     a
        dex
        bne     :-
        bit     sg
        bpl     :+
        eor     #$FF                ; negate (16 bits: r1:A)
        clc
        adc     #1
        pha
        lda     r1
        eor     #$FF
        adc     #0
        sta     r1
        pla
:       sta     r0
        ; + c0 * 16
        lda     tx
        ldx     #0
        cmp     #$80
        bcc     :+
        dex
:       stx     ph                  ; (sign extension, borrowed)
        asl     a
        rol     ph
        asl     a
        rol     ph
        asl     a
        rol     ph
        asl     a
        rol     ph
        clc
        adc     r0
        pha
        lda     ph
        adc     r1
        tax
        pla
        rts

; int16 ld_ground_(void)
_ld_ground_:
        lda     _lg_x
        ldx     _lg_x+1
        ldy     _lg_xf
        jsr     cellfrac
        sta     fx
        lda     gi
        sta     gj                  ; (keep the column)
        lda     _lg_z
        ldx     _lg_z+1
        ldy     _lg_zf
        jsr     cellfrac
        sta     fz
        ; hp = L.h + (row << 5) + column
        lda     gi                  ; the row
        ldx     #0
        stx     hp+1
        asl     a
        asl     a
        asl     a
        asl     a
        rol     hp+1
        asl     a
        rol     hp+1
        clc
        adc     gj
        bcc     :+
        inc     hp+1
:       clc
        adc     #<L_H
        sta     hp
        lda     hp+1
        adc     #>L_H
        sta     hp+1
        ldy     #0
        jsr     lerp4
        sta     top
        stx     top+1
        ldy     #32
        jsr     lerp4
        sta     bot
        stx     bot+1
        ; top + (bot - top) * fz >> 8
        sec
        lda     bot
        sbc     top
        sta     tx
        lda     bot+1
        sbc     top+1
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
:       lda     fz
        sta     pl
        lda     #0
        sta     ph
        jsr     mul_t_p
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
        lda     top
        adc     r0
        pha
        lda     top+1
        adc     r1
        tax
        pla
        rts
