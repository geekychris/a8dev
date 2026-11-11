; xbank.s - the 130XE's bank 0 (the rules) and the front end's part of main
; memory, loaded by the XEX itself (tank_duel.cfg; from spectral_keep's
; xbanks.s). An XEX is a list of chunks (start, end, data); a chunk that
; writes INITAD ($02E2) makes the loader call that address when the chunk
; is in. So: a few bytes of code at $0600 that select bank 0 for the CPU,
; an INITAD chunk calling them, the bank's chunk at $4000, its signature,
; and an INITAD chunk that maps main memory back before the program loads.
; On a 64K machine the bank's chunk lands in main memory and the front end
; loads over it: the game checks the signature ("TD0" at $7FFC).

        .import __XB0_START__, __XB0_LAST__, __MAIN2_START__, __FRONTBSS_RUN__

PORTB   = $D301
INITAD  = $02E2
SEL     = $0600

.macro  chunk start, end
        .word   start, end
.endmacro
.macro  ini addr
        chunk   INITAD, INITAD + 1
        .word   addr
.endmacro

.segment "XSEL"
        chunk   SEL, selstop - 1
        .org    SEL
sel0:   lda     PORTB
        and     #$C3
        ora     #$22            ; ANTIC main memory, BASIC off; CPU: bank 0
        sta     PORTB
        rts
selmain: lda    PORTB
        ora     #$32
        sta     PORTB
        rts
selstop:
        .reloc

.segment "XB0H"
        ini     sel0
        chunk   __XB0_START__, __XB0_LAST__ - 1
.segment "XB0S"
        chunk   $7FFC, $7FFF
        .byte   "TD0", $A5
.segment "XEND"
        ini     selmain
.segment "MAIN2HDR"
        chunk   __MAIN2_START__, __FRONTBSS_RUN__ - 1     ; (the BSS isn't in the file)
.segment "FRONTBSS"
        .res    1               ; (the front end's variables are in LOWRAM and BSS; this ends the chunk)
