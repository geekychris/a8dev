; xbanks.s - the 130XE banks' contents and the second part of main memory,
; loaded by the XEX itself (spectral_keep.cfg). An XEX is a list of chunks
; (start, end, data); a chunk that writes INITAD ($02E2) makes the loader
; call that address when the chunk is in. So: a few bytes of code at $0600
; that select a bank for the CPU (BASIC off too), then for each bank an
; INITAD chunk calling it and the bank's chunk at $4000, and an INITAD
; chunk that maps main memory back before the program loads. On a 64K
; machine the bank chunks land in main memory and the program loads over
; them: the game checks each bank's signature ("SK0".."SK3" at $7FFC).

        .import __XB0_START__, __XB0_LAST__, __XB1_START__, __XB1_LAST__
        .import __XB2_START__, __XB2_LAST__, __XB3_START__, __XB3_LAST__
        .import __MAIN2_START__, __FRONTBSS_RUN__

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
        chunk   __XB0_START__, __XB0_LAST__ - 1
.segment "XB1H"
        ini     sel1
        chunk   __XB1_START__, __XB1_LAST__ - 1
.segment "XB2H"
        ini     sel2
        chunk   __XB2_START__, __XB2_LAST__ - 1
.segment "XB3H"
        ini     sel3
        chunk   __XB3_START__, __XB3_LAST__ - 1
.macro  sig n
        chunk   $7FFC, $7FFF
        .byte   "SK", '0' + n, $A5
.endmacro
.segment "XB0S"
        sig     0
.segment "XB1S"
        sig     1
.segment "XB2S"
        sig     2
.segment "XB3S"
        sig     3
.segment "XEND"
        ini     selmain
.segment "MAIN2HDR"
        chunk   __MAIN2_START__, __FRONTBSS_RUN__ - 1     ; (the BSS isn't in the file)
