; xchunks.s - the XEX's first chunk: a few bytes at $0600 that switch BASIC
; off (an INITAD chunk runs them) before the rest loads, so the program can
; load into $A000-$BFFF.
; (It must come before cc65's system-check chunk: see measure.cfg.)
PORTB   = $D301
INITAD  = $02E2
RAMTOP  = $006A
.segment "XSEL"
        .word   $0600, stop - 1
        .org    $0600
basoff: lda     PORTB
        ora     #$02
        sta     PORTB
        lda     #$C0            ; and say so (cc65's system check reads RAMTOP)
        sta     RAMTOP
        rts
stop:
        .reloc
        .word   INITAD, INITAD + 1
        .word   basoff
