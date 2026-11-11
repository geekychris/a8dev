; xchunks.s - the XEX's first chunks: a few bytes at $0600 that switch
; BASIC off (an INITAD chunk runs them), so the maze's data can load at
; $9800-$BFFF; os.s copies it under the OS ROM at start-up.
        .import __IMG_START__, __IMG_LAST__
PORTB   = $D301
INITAD  = $02E2
.segment "XSEL"
        .word   $0600, stop - 1
        .org    $0600
basoff: lda     PORTB
        ora     #$02
        sta     PORTB
        rts
stop:
        .reloc
        .word   INITAD, INITAD + 1
        .word   basoff
.segment "IMGHDR"
        .word   __IMG_START__, __IMG_LAST__ - 1
