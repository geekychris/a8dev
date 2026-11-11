; xbanks.s - the 130XE banks' contents, loaded by the XEX itself.
;
; An XEX is a list of chunks (start, end, data); a chunk that writes
; INITAD ($02E2) makes the loader call that address when the chunk is in.
; So: a few bytes of code at $0600 that select a bank for the CPU, then
; for each bank an INITAD chunk calling it and the bank's 16K at $4000,
; and last an INITAD chunk that maps main memory back before the program
; itself loads. On a 64K machine the bank chunks land in main memory and
; the program loads over them: the game checks each bank's signature
; (gen_data.py: "BT0".."BT3" at $7FFC) and says it needs a 130XE.

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
; A = PORTB bits 2-3 for the bank, ANTIC keeps main memory (bit 5 = 1),
; the CPU gets the bank (bit 4 = 0)
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
        ora     #$22            ; and BASIC off
        ora     $CB
        sta     PORTB
        rts
selmain: lda    PORTB
        ora     #$32
        sta     PORTB
        rts
selstop:
        .reloc

.macro  bank n, file, entry
        ini     entry
        chunk   $4000, $7FFF
        .incbin file
.endmacro

.segment "XB0"
        bank    0, "gfx/bank0.bin", sel0
.segment "XB1"
        bank    1, "gfx/bank1.bin", sel1
.segment "XB2"
        bank    2, "gfx/bank2.bin", sel2
; the rules' code (HICODE, run under the OS ROM) after the title's view in
; bank 2, while bank 2 is still selected; src/os.s copies it up
        .import __XB2HI_START__, __XB2HI_LAST__
.segment "XB2HIH"
        chunk   __XB2HI_START__, __XB2HI_LAST__ - 1
.segment "XB3"
        bank    3, "gfx/bank3.bin", sel3
.segment "XEND"
        ini     selmain
