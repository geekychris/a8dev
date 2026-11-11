; fastmath.s - game.c's hot arithmetic without cc65's 32-bit runtime
;
;   int32_t dot3(const int16_t *a, const int8_t *b)   a . b (|a| < 2^14)
;   int16_t lerp8(int8_t a, int8_t b, uint8_t t)      a + ((b - a) * t >> 8)

        .export _dot3, _lerp8
        .import popax, popa
        .importzp ptr1, ptr2, sreg, tmp1, tmp2, tmp3

.segment "BSS"
m:      .res 3                  ; the multiplicand, 24 bits
acc:    .res 3
la:     .res 1

.segment "CODE"

; acc += m * tmp1 (tmp1 unsigned; m two's complement: the sum wraps right)
madd:   lsr     tmp1
        bcc     @n
        clc
        lda     acc
        adc     m
        sta     acc
        lda     acc+1
        adc     m+1
        sta     acc+1
        lda     acc+2
        adc     m+2
        sta     acc+2
@n:     lda     tmp1
        beq     @x
        asl     m
        rol     m+1
        rol     m+2
        jmp     madd
@x:     rts

; m = -m, tmp1 = -tmp1 (when tmp1, the signed factor, is negative)
negboth:
        lda     tmp1
        eor     #$FF
        clc
        adc     #1
        sta     tmp1
        sec
        lda     #0
        sbc     m
        sta     m
        lda     #0
        sbc     m+1
        sta     m+1
        lda     #0
        sbc     m+2
        sta     m+2
        rts

_dot3:  sta     ptr2
        stx     ptr2+1
        jsr     popax
        sta     ptr1
        stx     ptr1+1
        lda     #0
        sta     acc
        sta     acc+1
        sta     acc+2
        sta     tmp3
@lp:    ldy     tmp3
        lda     (ptr2),y
        sta     tmp1
        tya
        asl
        tay
        lda     (ptr1),y
        sta     m
        iny
        lda     (ptr1),y
        sta     m+1
        ldx     #0
        ora     #0
        bpl     @p
        dex
@p:     stx     m+2
        lda     tmp1
        bpl     @pos
        jsr     negboth
@pos:   jsr     madd
        inc     tmp3
        lda     tmp3
        cmp     #3
        bne     @lp
        ldx     #0
        lda     acc+2
        sta     sreg
        bpl     @s
        dex
@s:     stx     sreg+1
        lda     acc
        ldx     acc+1
        rts

; fastcall: t in A; b, a on the C stack (a pushed first)
_lerp8: sta     tmp1
        jsr     popa
        sta     tmp2            ; b
        jsr     popa
        sta     la              ; a
        ; m = b - a (sign-extended, -255..255)
        lda     tmp2
        ldx     #0
        ora     #0
        bpl     @b
        dex
@b:     stx     m+1
        sec
        sbc     la
        sta     m
        lda     m+1
        ldy     la
        bpl     @a
        sbc     #$FF
        jmp     @c
@a:     sbc     #0
@c:     sta     m+1
        ldx     #0
        ora     #0
        bpl     @d
        dex
@d:     stx     m+2
        lda     #0
        sta     acc
        sta     acc+1
        sta     acc+2
        jsr     madd            ; t unsigned
        ; a + (acc >> 8)
        ldx     #0
        lda     la
        bpl     @e
        dex
@e:     clc
        adc     acc+1
        pha
        txa
        adc     acc+2
        tax
        pla
        rts

; char *fmt_num(uint8_t w): fmt_v (0 .. 9 999 999) in decimal, at least w
; digits (main.c's num; by subtraction, 24 bits)
        .export _fmt_num, _fmt_v
.segment "BSS"
_fmt_v: .res 4
fbuf:   .res 8
fw:     .res 1
fn:     .res 1
.segment "RODATA"
p10l:   .byte <1000000, <100000, <10000, <1000, <100, <10, <1
p10m:   .byte >1000000, >100000, >10000, >1000, >100, >10, >1
p10h:   .byte ^1000000, ^100000, ^10000, ^1000, ^100, ^10, ^1
.segment "CODE"
_fmt_num:
        eor     #$FF            ; the first digit always written: 7 - w
        sec
        adc     #7
        sta     fw
        lda     #0
        sta     fn
        ldx     #0              ; the power
@dig:   ldy     #'0'
@sub:   lda     _fmt_v
        sec
        sbc     p10l,x
        sta     tmp1
        lda     _fmt_v+1
        sbc     p10m,x
        sta     tmp2
        lda     _fmt_v+2
        sbc     p10h,x
        bcc     @done
        sta     _fmt_v+2
        lda     tmp2
        sta     _fmt_v+1
        lda     tmp1
        sta     _fmt_v
        iny
        bne     @sub
@done:  cpy     #'0'
        bne     @put
        lda     fn
        bne     @put
        cpx     fw
        bcs     @put
        cpx     #6
        bne     @next
@put:   tya
        ldy     fn
        sta     fbuf,y
        inc     fn
@next:  inx
        cpx     #7
        bne     @dig
        ldy     fn
        lda     #0
        sta     fbuf,y
        lda     #<fbuf
        ldx     #>fbuf
        rts
