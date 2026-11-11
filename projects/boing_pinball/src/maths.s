; maths.s - fixmath.h in 6502 assembly for the physics: cc65 builds
; (int32_t)a * b as a 32 x 32 multiply (~1,500 cycles); these are 16 x 16.
; They replace the SDK's fixmath.c (same names; the linker then doesn't
; pull it in). Also div_q14 (pbz.h).
;
;   uint32_t mulu16(uint16_t a, uint16_t b)    a * b
;   int32_t  mul16(int16_t a, int16_t b)       a * b
;   int16_t  mulq14(int16_t a, int16_t b)      (a * b) >> 14 (floor)
;   int16_t  mulq8(int16_t a, int16_t b)       (a * b) >> 8 (floor)
;   uint16_t isqrt32(uint32_t v)               floor(sqrt(v))
;   int16_t  div_q14(int16_t a, int16_t d)     (a << 14) / d (towards 0)

        .export _mulu16, _mul16, _mulq14, _mulq8, _isqrt32, _div_q14, _udiv32_16
        .import popeax
        .export smul_mm, ma, mb, pr     ; for physasm.s: pr = ma * mb, signed
        .import popax
        .importzp sreg, tmp1, tmp2, tmp3, tmp4, ptr1, ptr2

.segment "HIBSS"
ma:     .res 2                 ; multiplicand
mb:     .res 2                 ; multiplier (shifted out)
pr:     .res 4                  ; product
sgn:    .res 1
.segment "HICODE"

; pr = ma * mb (unsigned 16 x 16) by quarter squares, with zero-page
; pointers into the tables so an 8 x 8 product is two subtractions:
;   x * y = f(x + y) - f(|x - y|), f(n) = n * n / 4 (floor; exact, as
;   x + y and x - y have the same parity)
; With a pointer at SQ + x and one at SN + (x ^ 255), Y = y:
;   (SQ + x),y = f(x + y);  (SN + 255 - x),y = f(|y - x|)
; Four 8 x 8 products (two pointer set-ups) make the 16 x 16.
; The tables (2K, page-aligned) are made by maths_init, under the OS ROM.
SQ_LO   = $F000                 ; f(n) & 255, n = 0..511
SQ_HI   = $F200                 ; f(n) >> 8
SN_LO   = $F400                 ; f(|n - 255|) & 255, n = 0..511
SN_HI   = $F600

.segment "ZEROPAGE"
q_sl:   .res 2
q_sh:   .res 2
q_nl:   .res 2
q_nh:   .res 2
.segment "HICODE"

; the pointers for multiplier x = A
.macro  setx
        sta     q_sl
        sta     q_sh
        eor     #$FF
        sta     q_nl
        sta     q_nh
.endmacro

umul:   lda     ma+1            ; the common case: both under 256
        ora     mb+1
        bne     @full
        lda     ma
        setx
        ldy     mb
        sec
        lda     (q_sl),y
        sbc     (q_nl),y
        sta     pr
        lda     (q_sh),y
        sbc     (q_nh),y
        sta     pr+1
        lda     #0
        sta     pr+2
        sta     pr+3
        rts
@full:  lda     ma              ; ma.lo * mb.lo, ma.lo * mb.hi
        setx
        ldy     mb
        sec
        lda     (q_sl),y
        sbc     (q_nl),y
        sta     pr
        lda     (q_sh),y
        sbc     (q_nh),y
        sta     pr+1
        ldy     mb+1
        sec
        lda     (q_sl),y
        sbc     (q_nl),y
        sta     tmp1            ; lo * hi: at byte 1
        lda     (q_sh),y
        sbc     (q_nh),y
        sta     tmp2
        lda     ma+1            ; ma.hi * mb.lo, ma.hi * mb.hi
        setx
        ldy     mb+1
        sec
        lda     (q_sl),y
        sbc     (q_nl),y
        sta     pr+2
        lda     (q_sh),y
        sbc     (q_nh),y
        sta     pr+3
        ldy     mb
        sec
        lda     (q_sl),y
        sbc     (q_nl),y
        sta     tmp3            ; hi * lo: at byte 1
        lda     (q_sh),y
        sbc     (q_nh),y
        sta     tmp4
        ; pr += (tmp2:tmp1 + tmp4:tmp3) << 8
        lda     pr+1
        clc
        adc     tmp1
        sta     pr+1
        lda     pr+2
        adc     tmp2
        sta     pr+2
        bcc     @c1
        inc     pr+3
        clc
@c1:    lda     pr+1
        adc     tmp3
        sta     pr+1
        lda     pr+2
        adc     tmp4
        sta     pr+2
        bcc     @c2
        inc     pr+3
@c2:    rts

; the quarter squares (call once, with the OS ROM off): f(n) for n = 0..511
; built by f(n + 1) = f(n) + floor(n / 2) + (n & 1); SN(n) = f(|n - 255|)
        .export _maths_init
_maths_init:
        lda     #>SQ_LO
        sta     q_sl+1
        lda     #>SQ_HI
        sta     q_sh+1
        lda     #>SN_LO
        sta     q_nl+1
        lda     #>SN_HI
        sta     q_nh+1
        lda     #0
        sta     tmp1            ; f lo
        sta     tmp2            ; f hi
        sta     tmp3            ; n's ninth bit
        tax                     ; n low
@n:     lda     tmp3
        bne     @hi
        lda     tmp1
        sta     SQ_LO,x
        lda     tmp2
        sta     SQ_HI,x
        jmp     @next
@hi:    lda     tmp1
        sta     SQ_LO+256,x
        lda     tmp2
        sta     SQ_HI+256,x
@next:  txa
        lsr                     ; n >> 1 (C = n & 1)
        sta     tmp4
        lda     tmp3
        beq     @nb
        lda     tmp4
        ora     #$80
        sta     tmp4
@nb:    txa
        and     #1
        clc
        adc     tmp4
        clc
        adc     tmp1
        sta     tmp1
        bcc     @nc
        inc     tmp2
@nc:    inx
        bne     @n
        lda     tmp3
        bne     @sn
        inc     tmp3
        jmp     @n
@sn:    ; SN(n) = f(|n - 255|): n = 0..255 -> f(255 - n), n = 256..511 -> f(n - 255)
        ldx     #0
@s1:    txa
        eor     #$FF            ; 255 - n
        tay
        lda     SQ_LO,y
        sta     SN_LO,x
        lda     SQ_HI,y
        sta     SN_HI,x
        txa                     ; n + 256 - 255 = n + 1 (<= 256)
        clc
        adc     #1
        tay
        bcs     @s256
        lda     SQ_LO,y
        sta     SN_LO+256,x
        lda     SQ_HI,y
        sta     SN_HI+256,x
        jmp     @s2
@s256:  lda     SQ_LO+256       ; n = 255: f(256)
        sta     SN_LO+256,x
        lda     SQ_HI+256
        sta     SN_HI+256,x
@s2:    inx
        bne     @s1
        rts

_mulu16:
        sta     mb
        stx     mb+1
        jsr     popax
        sta     ma
        stx     ma+1
        jsr     umul
ret32:  lda     pr+2
        sta     sreg
        lda     pr+3
        sta     sreg+1
        lda     pr
        ldx     pr+1
        rts

; signed: b in A/X, a from the C stack
smul:   sta     mb
        stx     mb+1
        jsr     popax
        sta     ma
        stx     ma+1
; pr = ma * mb, signed (ma, mb are destroyed)
smul_mm:
        lda     ma+1
        eor     mb+1
        sta     sgn
        lda     mb+1
        bpl     @bp
        lda     #0
        sec
        sbc     mb
        sta     mb
        lda     #0
        sbc     mb+1
        sta     mb+1
@bp:    lda     ma+1
        bpl     @ap
        lda     #0
        sec
        sbc     ma
        sta     ma
        lda     #0
        sbc     ma+1
        sta     ma+1
@ap:    jsr     umul
        lda     sgn
        bpl     @done
        lda     #0
        sec
        sbc     pr
        sta     pr
        lda     #0
        sbc     pr+1
        sta     pr+1
        lda     #0
        sbc     pr+2
        sta     pr+2
        lda     #0
        sbc     pr+3
        sta     pr+3
@done:  rts

_mul16: jsr     smul
        jmp     ret32

; (a * b) >> 14: bits 14..29 of the product
_mulq14:
        jsr     smul
        lda     pr+2            ; (pr >> 14): hi = pr+3 << 2 | pr+2 >> 6,
        sta     tmp1            ;             lo = pr+2 << 2 | pr+1 >> 6
        lda     pr+3
        sta     tmp2
        asl     tmp1
        rol     tmp2
        asl     tmp1
        rol     tmp2
        lda     pr+1
        lsr
        lsr
        lsr
        lsr
        lsr
        lsr
        ora     tmp1
        ldx     tmp2
        rts

_mulq8: jsr     smul
        lda     pr+1
        ldx     pr+2
        rts

; floor(sqrt(v)), v in A/X/sreg: the bit-by-bit method on 32 bits
_isqrt32:
        sta     ptr1            ; v (4 bytes: ptr1, ptr1+1, ptr2, ptr2+1)
        stx     ptr1+1
        lda     sreg
        sta     ptr2
        lda     sreg+1
        sta     ptr2+1
        lda     #0              ; r (32 bits: pr..pr+3), bit (tmp1..tmp4) = 1 << 30
        sta     pr
        sta     pr+1
        sta     pr+2
        sta     pr+3
        sta     tmp1
        sta     tmp2
        sta     tmp3
        lda     #$40
        sta     tmp4
        ldx     #16
@loop:  ; t = r + bit
        lda     pr
        clc
        adc     tmp1
        sta     ma
        lda     pr+1
        adc     tmp2
        sta     ma+1
        lda     pr+2
        adc     tmp3
        sta     mb
        lda     pr+3
        adc     tmp4
        sta     mb+1
        ; r >>= 1
        lsr     pr+3
        ror     pr+2
        ror     pr+1
        ror     pr
        ; if v >= t: v -= t, r += bit
        lda     ptr1
        cmp     ma
        lda     ptr1+1
        sbc     ma+1
        lda     ptr2
        sbc     mb
        lda     ptr2+1
        sbc     mb+1
        bcc     @next
        lda     ptr1
        sbc     ma              ; carry set
        sta     ptr1
        lda     ptr1+1
        sbc     ma+1
        sta     ptr1+1
        lda     ptr2
        sbc     mb
        sta     ptr2
        lda     ptr2+1
        sbc     mb+1
        sta     ptr2+1
        lda     pr
        ora     tmp1
        sta     pr
        lda     pr+1
        ora     tmp2
        sta     pr+1
        lda     pr+2
        ora     tmp3
        sta     pr+2
        lda     pr+3
        ora     tmp4
        sta     pr+3
@next:  ; bit >>= 2
        lsr     tmp4
        ror     tmp3
        ror     tmp2
        ror     tmp1
        lsr     tmp4
        ror     tmp3
        ror     tmp2
        ror     tmp1
        dex
        beq     @end
        jmp     @loop
@end:   lda     pr
        ldx     pr+1
        rts

; uint16_t udiv32_16(uint32_t n, uint16_t d): n / d, the low 16 bits
_udiv32_16:
        sta     mb
        stx     mb+1
        jsr     popeax          ; n: A/X/sreg
        sta     pr
        stx     pr+1
        lda     sreg
        sta     pr+2
        lda     sreg+1
        sta     pr+3
        jmp     div16q

; (a << 14) / d, truncated towards 0, to 16 bits (as C's (int16_t) of it)
_div_q14:
        sta     mb              ; d
        stx     mb+1
        stx     sgn
        txa
        bpl     @dp
        lda     #0
        sec
        sbc     mb
        sta     mb
        lda     #0
        sbc     mb+1
        sta     mb+1
@dp:    jsr     popax           ; a
        sta     ma
        stx     ma+1
        txa
        eor     sgn
        sta     sgn
        txa
        bpl     @ap
        lda     #0
        sec
        sbc     ma
        sta     ma
        lda     #0
        sbc     ma+1
        sta     ma+1
@ap:    ; dividend |a| << 14 in pr (32 bits): |a| << 16 >> 2
        lda     #0
        sta     pr
        lda     ma
        sta     pr+2
        lda     ma+1
        sta     pr+3
        lda     #0
        sta     pr+1
        lsr     pr+3
        ror     pr+2
        ror     pr+1
        lsr     pr+3
        ror     pr+2
        ror     pr+1
        jsr     div16q
        jmp     dq_sign
; pr = pr / mb when the quotient fits 16 bits (pr's high word < mb): the
; remainder starts as the high word and 16 steps bring in the low word
; (the same quotient and remainder as div32's 32 steps, in half the time);
; returns the quotient in A/X
div16q: lda     pr+2            ; (a quotient past 16 bits: the long way)
        cmp     mb
        lda     pr+3
        sbc     mb+1
        bcc     @fits
        jmp     div32
@fits:  lda     pr+2
        sta     tmp1
        lda     pr+3
        sta     tmp2
        lda     #0
        sta     pr+2
        sta     pr+3
        ldx     #16
@div:   asl     pr
        rol     pr+1
        rol     tmp1
        rol     tmp2
        bcs     @sub
        lda     tmp1
        cmp     mb
        lda     tmp2
        sbc     mb+1
        bcc     @nos
@sub:   lda     tmp1
        sbc     mb
        sta     tmp1
        lda     tmp2
        sbc     mb+1
        sta     tmp2
        inc     pr
@nos:   dex
        bne     @div
        lda     pr
        ldx     pr+1
        rts
; pr = pr / mb (32 / 16 -> 32), restoring division, remainder in tmp1/tmp2;
; returns the low 16 bits in A/X
div32:  lda     #0
        sta     tmp1
        sta     tmp2
        ldx     #32
@div:   asl     pr
        rol     pr+1
        rol     pr+2
        rol     pr+3
        rol     tmp1
        rol     tmp2
        bcs     @sub            ; the remainder went past 16 bits: it's >= d
        lda     tmp1
        cmp     mb
        lda     tmp2
        sbc     mb+1
        bcc     @nos
@sub:   lda     tmp1
        sbc     mb              ; (carry set either way)
        sta     tmp1
        lda     tmp2
        sbc     mb+1
        sta     tmp2
        inc     pr              ; the quotient's bit
@nos:   dex
        bne     @div
        lda     pr
        ldx     pr+1
        rts
dq_sign: lda    sgn
        bpl     @pos
        lda     #0
        sec
        sbc     pr
        tay
        lda     #0
        sbc     pr+1
        tax
        tya
        rts
@pos:   lda     pr
        ldx     pr+1
        rts
