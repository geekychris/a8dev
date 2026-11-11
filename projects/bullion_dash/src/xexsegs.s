; Bullion Dash - the XEX's extra segments (bullion.cfg): before the main
; program, a tiny routine run while loading switches BASIC off (PORTB bit 1
; on XL/XE; on a 400/800 PORTB is a joystick port set as input, so the
; write does nothing) and the screen off (the OS screen's display list is
; at $BC20, where the levels go), then the levels load into $A000.

        .import __BOFF_START__, __LEVELS_LOAD__, __LEVELS_SIZE__

PORTB   = $D301
SDMCTL  = $022F
DMACTL  = $D400

        .segment "BOFFHDR"
        .word   __BOFF_START__, boff_end - 1

        .segment "BOFF"
boff:   lda     #0
        sta     SDMCTL
        sta     DMACTL
        lda     PORTB
        ora     #2
        sta     PORTB
        rts
boff_end:

        .segment "BOFFTRL"
        .word   $02E2, $02E3, boff

        .segment "LVHDR"
        .word   __LEVELS_LOAD__, __LEVELS_LOAD__ + __LEVELS_SIZE__ - 1
