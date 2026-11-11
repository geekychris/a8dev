; a8log.h: a8_log_s / a8_fail - strings to the harness log port $D6FF.
        .export         _a8_log_s, _a8_fail
        .importzp       ptr1

LOGPORT = $D6FF

.code

; void __fastcall__ a8_log_s(const char *s)
_a8_log_s:
        sta     ptr1
        stx     ptr1+1
        ldy     #0
@loop:  lda     (ptr1),y
        beq     @done
        sta     LOGPORT
        iny
        bne     @loop
        inc     ptr1+1
        bne     @loop
@done:  rts

; void __fastcall__ a8_fail(const char *why)  ->  "A8:FAIL <why>\n"
_a8_fail:
        pha
        txa
        pha
        lda     #<failmsg
        ldx     #>failmsg
        jsr     _a8_log_s
        pla
        tax
        pla
        jsr     _a8_log_s
        lda     #10
        sta     LOGPORT
        rts

.rodata
failmsg: .byte  "A8:FAIL ", 0
