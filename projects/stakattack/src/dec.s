; StakAttack - a score (up to 9,999,999) as 7 decimal digits, by repeated
; subtraction of 24-bit powers of ten: ~1,500 cycles, against ~15,000 for
; cc65's 32-bit C (the HUD and the log did it at every lock).

        .export _dec7, _dec_out
        .importzp sreg

        .bss
_dec_out: .res 7                ; '0'..'9' (ASCII), most significant first
v0:     .res 1
v1:     .res 1
v2:     .res 1
t0:     .res 1
t1:     .res 1

        .rodata
p_lo:   .byte <1000000, <100000, <10000, <1000, <100, <10, <1
p_mid:  .byte >1000000, >100000, >10000, >1000, >100, >10, >1
p_hi:   .byte ^1000000, ^100000, ^10000, ^1000, ^100, ^10, ^1

        .code
; void __fastcall__ dec7(uint32_t v)   (v <= 9999999: the caller clamps)
.proc _dec7
        sta v0
        stx v1
        lda sreg
        sta v2
        ldy #0
dig:    ldx #'0'
sub:    sec
        lda v0
        sbc p_lo,y
        sta t0
        lda v1
        sbc p_mid,y
        sta t1
        lda v2
        sbc p_hi,y
        bcc next
        sta v2
        lda t1
        sta v1
        lda t0
        sta v0
        inx
        bne sub
next:   txa
        sta _dec_out,y
        iny
        cpy #7
        bne dig
        rts
.endproc
