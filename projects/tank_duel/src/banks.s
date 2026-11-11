; banks.s - calls into the 130XE's bank 0 (the rules: td_core.c), from
; spectral_keep's (and boing_pinball's) banks.s. The rules run in the bank
; window $4000-$7FFF; each function the front end calls has a trampoline
; here (LOWCODE, outside the window) that maps the bank, calls it with A/X
; (cc65's last argument) and the C stack as they were, maps main memory
; back and returns A/X. The banked code is compiled with its functions
; renamed b0_* (Makefile), so these take the original names.

        .export _bank_ok
        .importzp ptr1

PORTB   = $D301

.segment "LOWDATA"
t_a:    .byte 0
t_x:    .byte 0
jvec:   .word 0

.segment "LOWCODE"

; the CPU sees bank 0 / main memory (ANTIC always main memory: bit 5 = 1;
; BASIC and the OS ROM as they are)
bank0:  lda     PORTB
        and     #$C3
        ora     #$20
        sta     PORTB
        rts
bmain:  lda     PORTB
        ora     #$30
        sta     PORTB
        rts

; each trampoline is `jsr tcom` and its target (2 bytes): tcom takes the
; target from its return address, maps the bank, calls it with A/X as they
; came, maps main memory back and returns A/X to the trampoline's caller
.macro  tramp pub, impl
        .export pub
        .import impl
pub:    jsr     tcom
        .word   impl
.endmacro

tcom:   sta     t_a
        stx     t_x
        pla                     ; the stub's data - 1
        sta     ptr1
        pla
        sta     ptr1+1
        ldy     #1
        lda     (ptr1),y
        sta     jvec
        iny
        lda     (ptr1),y
        sta     jvec+1
        jsr     bank0
        lda     t_a
        ldx     t_x
        jsr     @call
        sta     t_a
        stx     t_x
        jsr     bmain
        lda     t_a
        ldx     t_x
        rts
@call:  jmp     (jvec)

        tramp   _td_new_game,      _b0_td_new_game
        tramp   _td_tick,          _b0_td_tick
        tramp   _td_bot,           _b0_td_bot
        tramp   _td_pop_event,     _b0_td_pop_event
        tramp   _td_enemies_left,  _b0_td_enemies_left

; uint8_t bank_ok(void): bank 0 holds xbank.s's signature (a 130XE). Main
; memory's $7FFF is zeroed first (and put back), so a machine without the
; banks, where the loader left the signature in main memory, says no
_bank_ok:
        lda     $7FFF
        sta     t_a
        lda     #0
        sta     $7FFF
        jsr     bank0
        ldx     #0
        lda     $7FFC
        cmp     #'T'
        bne     @no
        lda     $7FFD
        cmp     #'D'
        bne     @no
        lda     $7FFE
        cmp     #'0'
        bne     @no
        lda     $7FFF
        cmp     #$A5
        bne     @no
        inx
@no:    stx     t_x
        jsr     bmain
        lda     t_a
        sta     $7FFF
        lda     t_x
        ldx     #0
        rts
