; fmath.s - the front end's own multiply (FRONT: main memory)
;   int16_t mulq12(int16_t a, int16_t b): (a * b) >> 12, the low 16 bits
;   (cc65's (int32_t) >> 12 shifts 32 bits twelve times)
        .export _mulq12
        .import smul_mm, ma, mb, pr, popax
        .importzp tmp1

.segment "FRONT"
_mulq12:
        sta     mb
        stx     mb+1
        jsr     popax
        sta     ma
        stx     ma+1
        jsr     smul_mm
        ; bits 12..27: (pr+3:pr+2:pr+1) >> 4
        lda     pr+3
        sta     tmp1
        lda     pr+2
        ldx     #4
@s:     lsr     tmp1
        ror
        dex
        bne     @s
        tax                     ; (high byte)
        lda     pr+2
        asl
        asl
        asl
        asl
        sta     tmp1
        lda     pr+1
        lsr
        lsr
        lsr
        lsr
        ora     tmp1
        rts
