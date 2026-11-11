; unlz.s - unpacks tools/lz.py's format from the 130XE banks:
;   0lllllll        l + 1 literal bytes follow
;   1lllllll oo oo  copy l + 3 bytes from offset + 1 bytes back in the output
;   0x7F            the end
; The packed data runs through the banks' windows ($4000-$7FFB, each bank's
; signature after it): at $7FFC the next bank's $4000 follows. The output
; must be outside the window. In LOWCODE: it runs with a bank mapped.

        .export _unlz, _bank_peek
        .import popax, popa
        .importzp ptr1, ptr2, ptr3, tmp1, tmp2

PORTB   = $D301

.segment "LOWDATA"
bank:   .byte 0
cnt:    .byte 0

.segment "LOWCODE"

; map bank A (0-3) for the CPU (ANTIC stays on main memory)
mapbank:
        asl
        asl
        sta     tmp2
        lda     PORTB
        and     #$E3            ; the bank bits and the CPU's enable
        ora     tmp2
        sta     PORTB
        rts
mapmain:
        lda     PORTB
        ora     #$10
        sta     PORTB
        rts

; A = the next packed byte (ptr1, crossing into the next bank at $7FFC)
getb:   ldy     #0
        lda     (ptr1),y
        inc     ptr1
        bne     @n
        inc     ptr1+1
@n:     ldy     ptr1+1
        cpy     #$7F
        bne     @r
        ldy     ptr1
        cpy     #$FC
        bne     @r
        pha
        lda     #$40
        sta     ptr1+1
        lda     #0
        sta     ptr1
        inc     bank
        lda     bank
        jsr     mapbank
        pla
@r:     rts

; unlz(bank, src, dst)
_unlz:  sta     ptr2
        stx     ptr2+1
        jsr     popax
        sta     ptr1
        stx     ptr1+1
        jsr     popa
        sta     bank
        sei                     ; (the NMI's code and tables are outside the window)
        jsr     mapbank
        cli
@tok:   jsr     getb
        cmp     #$7F
        beq     @end
        bcs     @copy
        ; literals
        tax
        inx
        stx     cnt
@lit:   jsr     getb
        ldy     #0
        sta     (ptr2),y
        inc     ptr2
        bne     @l1
        inc     ptr2+1
@l1:    dec     cnt
        bne     @lit
        jmp     @tok
@copy:  and     #$7F
        clc
        adc     #3
        sta     cnt
        jsr     getb            ; ptr3 = dst - (offset + 1)
        sta     tmp1
        jsr     getb
        tax
        sec
        lda     ptr2
        sbc     tmp1
        sta     ptr3
        txa
        eor     #$FF            ; - (hi) - 1: the + 1 of the offset
        adc     ptr2+1
        sta     ptr3+1
        ; ptr3 = ptr2 - offset - 1: lo done with borrow; hi = ptr2+1 - x - 1 + carry...
        lda     ptr3
        sec
        sbc     #1
        sta     ptr3
        bcs     @c0
        dec     ptr3+1
@c0:    ldy     #0
@cp:    lda     (ptr3),y
        sta     (ptr2),y
        iny
        cpy     cnt
        bne     @cp
        tya
        clc
        adc     ptr2
        sta     ptr2
        bcc     @tok
        inc     ptr2+1
        jmp     @tok
@end:   jmp     mapmain

; bank_peek(bank, addr): a byte from a bank
_bank_peek:
        sta     ptr1
        stx     ptr1+1
        jsr     popa
        jsr     mapbank
        ldy     #0
        lda     (ptr1),y
        pha
        jsr     mapmain
        pla
        ldx     #0
        rts
