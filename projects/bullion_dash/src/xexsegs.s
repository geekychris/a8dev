; Bullion Dash - the XEX's extra segments (bullion.cfg): before the main
; program, a tiny routine run while loading switches BASIC off (PORTB bit 1
; on XL/XE; on a 400/800 PORTB is a joystick port set as input, so the
; write does nothing) and the screen off (the OS screen's display list is
; at $BC20, where the levels go), then the levels load into $A000.

        .import __BOFF_START__, __LEVELS_LOAD__, __LEVELS_SIZE__
        .import __SCRBSS_RUN__, __SCRBSS_SIZE__

; level.c's gridbfs queue is the page at $9500, the top of SCRMEM: the
; screen's variables must end below it
        .assert __SCRBSS_RUN__ + __SCRBSS_SIZE__ <= $9500, error, "SCRBSS runs into gridbfs's queue at $9500"

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

; level.c's gold_home bits (56 bytes) live right after the levels
        .assert __LEVELS_LOAD__ + __LEVELS_SIZE__ <= $BFC8, error, "the levels run into gold_home at $BFC8"

        .segment "LVHDR"
        .word   __LEVELS_LOAD__, __LEVELS_LOAD__ + __LEVELS_SIZE__ - 1
