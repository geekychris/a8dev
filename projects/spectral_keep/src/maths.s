; maths.s - the logic's arithmetic without cc65's 32-bit runtime
;
;   uint16_t isqrt32(uint32_t v)            floor(sqrt(v))
;   uint32_t sqr16(int16_t a)               a * a
;   int32_t  smul16(int16_t a, int16_t b)   a * b
;   (cc65 fastcall: the last argument in A/X (+ sreg), the others on the C stack)

        .export _isqrt32, _sqr16, _smul16
        .import popax
        .importzp sreg, tmp1, tmp2, tmp3, tmp4, ptr1, ptr2

.segment "BSS"
mA:     .res 2
mB:     .res 2
prod:   .res 4
sgn:    .res 1
rv:     .res 4                  ; isqrt: the value, the root, the bit
rr:     .res 4
rb:     .res 4
rt:     .res 4

.segment "CODE"

; prod = mA * mB (unsigned 16 x 16)
umul:   lda     #0
        sta     prod+2
        sta     prod+3
        ldx     #16
@l:     lsr     mB+1
        ror     mB
        bcc     @n
        clc
        lda     prod+2
        adc     mA
        sta     prod+2
        lda     prod+3
        adc     mA+1
        sta     prod+3
@n:     ror     prod+3
        ror     prod+2
        ror     prod+1
        ror     prod
        dex
        bne     @l
        rts

; A/X -> |A/X| in mA, the sign into sgn (eor)
absA:   cpx     #$80
        bcc     @p
        pha
        lda     sgn
        eor     #$FF
        sta     sgn
        pla
        eor     #$FF
        clc
        adc     #1
        pha
        txa
        eor     #$FF
        adc     #0
        tax
        pla
@p:     rts

retprod:
        lda     prod+2
        sta     sreg
        lda     prod+3
        sta     sreg+1
        lda     prod
        ldx     prod+1
        rts

_sqr16: ldy     #0
        sty     sgn
        jsr     absA
        sta     mA
        stx     mA+1
        sta     mB
        stx     mB+1
        jsr     umul
        jmp     retprod

_smul16:
        ldy     #0
        sty     sgn
        jsr     absA
        sta     mB
        stx     mB+1
        jsr     popax
        jsr     absA
        sta     mA
        stx     mA+1
        jsr     umul
        lda     sgn
        beq     retprod
        sec                     ; negate
        ldx     #0
@ng:    lda     #0
        sbc     prod,x
        sta     prod,x
        inx
        txa
        eor     #4
        bne     @ng
        jmp     retprod

; the bit-by-bit root: r = 0, b = 1 << 30 (down to <= v); while b:
; if v >= r + b: v -= r + b, r = r / 2 + b else r /= 2; b /= 4
_isqrt32:
        sta     rv
        stx     rv+1
        lda     sreg
        sta     rv+2
        lda     sreg+1
        sta     rv+3
        lda     #0
        sta     rr
        sta     rr+1
        sta     rr+2
        sta     rr+3
        sta     rb
        sta     rb+1
        sta     rb+2
        lda     #$40
        sta     rb+3
        ; b down to <= v
@down:  lda     rb+3
        cmp     rv+3
        bcc     @go
        bne     @shr
        lda     rb+2
        cmp     rv+2
        bcc     @go
        bne     @shr
        lda     rb+1
        cmp     rv+1
        bcc     @go
        bne     @shr
        lda     rb
        cmp     rv
        bcc     @go
        beq     @go
@shr:   ldx     #2
@s2:    lsr     rb+3
        ror     rb+2
        ror     rb+1
        ror     rb
        dex
        bne     @s2
        lda     rb
        ora     rb+1
        ora     rb+2
        ora     rb+3
        bne     @down
        jmp     @ret
@go:
@loop:  ; t = r + b
        clc
        lda     rr
        adc     rb
        sta     rt
        lda     rr+1
        adc     rb+1
        sta     rt+1
        lda     rr+2
        adc     rb+2
        sta     rt+2
        lda     rr+3
        adc     rb+3
        sta     rt+3
        ; r /= 2
        lsr     rr+3
        ror     rr+2
        ror     rr+1
        ror     rr
        ; v >= t ?
        lda     rv
        cmp     rt
        lda     rv+1
        sbc     rt+1
        lda     rv+2
        sbc     rt+2
        lda     rv+3
        sbc     rt+3
        bcc     @no
        ; v -= t, r += b
        sec
        lda     rv
        sbc     rt
        sta     rv
        lda     rv+1
        sbc     rt+1
        sta     rv+1
        lda     rv+2
        sbc     rt+2
        sta     rv+2
        lda     rv+3
        sbc     rt+3
        sta     rv+3
        clc
        lda     rr
        adc     rb
        sta     rr
        lda     rr+1
        adc     rb+1
        sta     rr+1
        lda     rr+2
        adc     rb+2
        sta     rr+2
        lda     rr+3
        adc     rb+3
        sta     rr+3
@no:    ; b /= 4
        ldx     #2
@s4:    lsr     rb+3
        ror     rb+2
        ror     rb+1
        ror     rb
        dex
        bne     @s4
        lda     rb
        ora     rb+1
        ora     rb+2
        ora     rb+3
        beq     @ret
        jmp     @loop
@ret:   lda     rr
        ldx     rr+1
        rts
