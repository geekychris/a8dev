; xbanks.s - the 130XE banks' contents, loaded by the XEX itself
; (rolling_steel.cfg): a few bytes at $0600 select a bank for the CPU (and
; switch BASIC off); for each bank an INITAD chunk calls them, then the
; bank's 16K at $4000 (gfx/bank0-3.bin: the six courses, packed, with the
; signature "RS0".."RS3" at $7FFC); a last INITAD chunk maps main memory
; back before the program loads. On a 64K machine the banks land in main
; memory and the program loads over them: the game checks the signatures.

        .import __XB0_START__, __XB1_START__, __XB2_START__, __XB3_START__

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
sel0:   lda     #$00
        beq     sel
sel1:   lda     #$04
        bne     sel
sel2:   lda     #$08
        bne     sel
sel3:   lda     #$0C
sel:    sta     $CB
        lda     PORTB
        and     #$C3
        ora     #$22            ; ANTIC main memory, BASIC off; CPU: the bank
        ora     $CB
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
        chunk   __XB0_START__, __XB0_START__ + $3FFF
.segment "BANK0"
        .incbin "gfx/bank0.bin"
.segment "XB1H"
        ini     sel1
        chunk   __XB1_START__, __XB1_START__ + $3FFF
.segment "BANK1"
        .incbin "gfx/bank1.bin"
.segment "XB2H"
        ini     sel2
        chunk   __XB2_START__, __XB2_START__ + $3FFF
.segment "BANK2"
        .incbin "gfx/bank2.bin"
.segment "XB3H"
        ini     sel3
        chunk   __XB3_START__, __XB3_START__ + $3FFF
.segment "BANK3"
        .incbin "gfx/bank3.bin"
.segment "XEND"
        ini     selmain

; CODE2 (rolling_steel.cfg): code loaded low, at $1440, its own XEX chunk
        .import __CODE2_LOAD__, __CODE2_SIZE__
.segment "LOW2HDR"
        .word   __CODE2_LOAD__, __CODE2_LOAD__ + __CODE2_SIZE__ - 1
