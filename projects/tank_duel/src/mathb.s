; mathb.s - the rules' small sums in asm (td_core.c; in bank 0 with them):
; the angle to a point, an octagonal distance and a step along a heading.
; Arguments and results through M_a, M_b, M_d (td_core.c's macros), results
; in A/X as cc65 returns them.
        .export   _M_a, _M_b, _M_d, _angle_to_, _odist_, _along_, _along8_

.segment "LOWRAM"
_M_a:   .res 2
_M_b:   .res 2
_M_d:   .res 1
ax_:    .res 2                          ; |dx|
az_:    .res 2                          ; |dz|
mn:     .res 1
mx:     .res 1
q:      .res 1
r:      .res 1
sgn:    .res 1
pl:     .res 1
shn:    .res 1
ph:     .res 1

.segment "B0RO"
atl:    .byte <0, <163, <326, <489, <651, <813, <975, <1136, <1297, <1457, <1617, <1775, <1933, <2090, <2246, <2401, <2555, <2708, <2860, <3010, <3159, <3307, <3453, <3599, <3742, <3884, <4025, <4164, <4302, <4438, <4573, <4706, <4838, <4968, <5097, <5223, <5349, <5473, <5595, <5715, <5834, <5952, <6068, <6182, <6295, <6406, <6516, <6624, <6731, <6837, <6941, <7043, <7144, <7244, <7342, <7439, <7534, <7628, <7721, <7813, <7903, <7991, <8079, <8165, <8192
ath:    .byte >0, >163, >326, >489, >651, >813, >975, >1136, >1297, >1457, >1617, >1775, >1933, >2090, >2246, >2401, >2555, >2708, >2860, >3010, >3159, >3307, >3453, >3599, >3742, >3884, >4025, >4164, >4302, >4438, >4573, >4706, >4838, >4968, >5097, >5223, >5349, >5473, >5595, >5715, >5834, >5952, >6068, >6182, >6295, >6406, >6516, >6624, >6731, >6837, >6941, >7043, >7144, >7244, >7342, >7439, >7534, >7628, >7721, >7813, >7903, >7991, >8079, >8165, >8192

.segment "B0CODE"
; |M_a| -> ax_, |M_b| -> az_
absab:  lda     _M_a
        ldx     _M_a+1
        jsr     abs16
        sta     ax_
        stx     ax_+1
        lda     _M_b
        ldx     _M_b+1
        jsr     abs16
        sta     az_
        stx     az_+1
        rts
abs16:  cpx     #$80
        bcc     :+
        eor     #$FF
        clc
        adc     #1
        pha
        txa
        eor     #$FF
        adc     #0
        tax
        pla
:       rts

; uint16 angle_to_(void): the heading (0 = +z, $4000 = +x) from 0 to
; (M_a, M_b) = (dx, dz): atan(min / max) from a table of 65, the two
; shifted down until both fit a byte (port: the original divides at 10 bits)
_angle_to_:
        jsr     absab
        lda     ax_
        ora     ax_+1
        ora     az_
        ora     az_+1
        bne     :+
        tax
        rts
:       lda     ax_+1                   ; down to a byte
        ora     az_+1
        beq     @byte
        lsr     ax_+1
        ror     ax_
        lsr     az_+1
        ror     az_
        jmp     :-
@byte:  lda     ax_
        cmp     az_
        bcc     @xlo                    ; ax < az
        beq     @xlo
        sta     mx                      ; ax > az: 16384 - atan(az / ax)
        lda     az_
        sta     mn
        jsr     ratio
        sec
        lda     #<16384
        sbc     pl
        sta     pl
        lda     #>16384
        sbc     ph
        sta     ph
        jmp     @oct
@xlo:   sta     mn                      ; ax <= az: atan(ax / az)
        lda     az_
        sta     mx
        jsr     ratio
@oct:   lda     _M_b+1                  ; dz < 0: 32768 - a
        bpl     :+
        sec
        lda     #0
        sbc     pl
        sta     pl
        lda     #$80
        sbc     ph
        sta     ph
:       lda     _M_a+1                  ; dx < 0: -a
        bpl     :+
        sec
        lda     #0
        sbc     pl
        sta     pl
        lda     #0
        sbc     ph
        sta     ph
:       lda     pl
        ldx     ph
        rts

; atan(mn / mx) (mn <= mx, mx > 0) -> pl, ph: the table at mn * 64 / mx
ratio:  lda     #0
        sta     q
        lda     mn
        cmp     mx
        bcc     :+
        lda     #0                      ; mn = mx: 1, no remainder
        inc     q
:       sta     r
        ldx     #6
@d:     asl     q
        asl     r                       ; (a 9-bit remainder: the carry)
        bcs     @sub
        lda     r
        cmp     mx
        bcc     @n
@sub:   lda     r
        sbc     mx                      ; (carry set either way)
        sta     r
        inc     q
@n:     dex
        bne     @d
        ldx     q
        lda     atl,x
        sta     pl
        lda     ath,x
        sta     ph
        rts

; uint16 odist_(void): max + 3/8 min of |M_a| >> 4, |M_b| >> 4 (whole units)
_odist_:
        jsr     absab
        ldx     #4
:       lsr     ax_+1
        ror     ax_
        lsr     az_+1
        ror     az_
        dex
        bne     :-
        lda     ax_                     ; ax_ = the larger, az_ = the smaller
        cmp     az_
        lda     ax_+1
        sbc     az_+1
        bcs     :+
        ldx     ax_
        lda     az_
        sta     ax_
        stx     az_
        ldx     ax_+1
        lda     az_+1
        sta     ax_+1
        stx     az_+1
:       lsr     az_+1                   ; b >> 2
        ror     az_
        lsr     az_+1
        ror     az_
        lda     az_
        sta     pl
        lda     az_+1
        sta     ph
        lsr     ph                      ; b >> 3 (from b >> 2)
        ror     pl
        clc
        lda     ax_
        adc     az_
        tay
        lda     ax_+1
        adc     az_+1
        tax
        tya
        clc
        adc     pl
        pha
        txa
        adc     ph
        tax
        pla
        rts

; int16 along_(void): M_d units along heading M_a in positions (1/16):
; (M_d * sin8[M_a >> 8]) >> 4, sin8 the rules' table (Q8, -256..256);
; along8_: the same >> 8 (a speed's step)
        .import   _sin8
_along8_:
        lda     #8
        bne     :+
_along_:
        lda     #4
:       sta     shn
        lda     _M_a+1                  ; sin(a + 180) = -sin(a): the sign from
        sta     sgn                     ; the top bit, |s| from the first half
        asl     a
        tay
        lda     _sin8,y
        ldx     _sin8+1,y               ; (1 for 256)
        cpx     #0
        beq     @mul
        lda     #0                      ; 256: d << 8
        sta     pl
        lda     _M_d
        sta     ph
        jmp     @sg
@mul:   sta     r                       ; d * |s| (8 x 8)
        lda     #0
        sta     ph
        ldx     #8
        lsr     r
@m:     bcc     :+
        clc
        adc     _M_d
:       ror     a
        ror     r
        dex
        bne     @m
        sta     ph
        lda     r
        sta     pl
@sg:    lda     sgn
        bpl     @sh
        sec
        lda     #0
        sbc     pl
        sta     pl
        lda     #0
        sbc     ph
        sta     ph
@sh:    lda     ph                      ; >> shn, arithmetic
        ldx     shn
:       cmp     #$80
        ror     a
        ror     pl
        dex
        bne     :-
        tax
        lda     pl
        rts
