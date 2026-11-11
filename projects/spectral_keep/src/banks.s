; banks.s - calls into the 130XE banks (from boing_pinball). The rules and
; the rooms (bank 0) run in the bank window $4000-$7FFF; each function the rest of
; the game calls has a trampoline here (LOWCODE, outside the window) that
; maps its bank, calls it with A/X (cc65's last argument) and the C stack
; as they were, maps the bank that was there back and returns A/X (and
; sreg). The banked code is compiled with its functions renamed b0_* /
; (Makefile), so these take the original names.

        .export _bank_select, _bank_cur, _bank_ok

PORTB   = $D301

.segment "LOWDATA"
_bank_cur:  .byte $FF           ; the bank mapped for the CPU ($FF: main memory)
t_a:    .byte 0
t_x:    .byte 0

.segment "LOWCODE"

; A = bank 0..3 or $FF. The OS ROM stays off (bit 0), BASIC off (bit 1),
; ANTIC always sees main memory (bit 5 = 1).
_bank_select:
        sta     _bank_cur
        cmp     #$FF
        beq     @main
        asl
        asl
        and     #$0C
        sta     t_sel
        lda     PORTB
        and     #$C3
        ora     #$20
        ora     t_sel
        sta     PORTB
        rts
@main:  lda     PORTB
        ora     #$30
        sta     PORTB
        rts
t_sel:  .byte 0

; each trampoline is `jsr tcom` and its bank and target (6 bytes): tcom
; takes them from its return address, maps the bank (keeping the one that
; was mapped on the stack), calls the target with A/X as they came, maps
; the old bank back and returns A/X to the trampoline's caller.
.macro  tramp pub, bank, impl
        .export pub
        .import impl
pub:    jsr     tcom
        .byte   bank
        .word   impl
.endmacro

        .importzp ptr1
jvec:   .word   0
tcom:   sta     t_a
        stx     t_x
        pla                     ; the stub's data - 1
        sta     ptr1
        pla
        sta     ptr1+1
        ldy     #2
        lda     (ptr1),y
        sta     jvec
        iny
        lda     (ptr1),y
        sta     jvec+1
        lda     _bank_cur
        pha
        ldy     #1
        lda     (ptr1),y
        jsr     _bank_select
        lda     t_a
        ldx     t_x
        jsr     @call
        sta     t_a
        stx     t_x
        pla
        jsr     _bank_select
        lda     t_a
        ldx     t_x
        rts
@call:  jmp     (jvec)

        ; the rules and the rooms (bank 0)
        tramp   _game_init,         0, _b0_game_init
        tramp   _game_step,         0, _b0_game_step
        tramp   _room_grounds,      0, _b0_room_grounds

; uint8_t __fastcall__ bank_ok(uint8_t bank): the bank holds xbanks.s's signature
_bank_ok:
        sta     t_a
        jsr     _bank_select
        ldx     #0
        lda     $7FFC
        cmp     #'S'
        bne     @no
        lda     $7FFD
        cmp     #'K'
        bne     @no
        lda     t_a
        ora     #$30
        cmp     $7FFE
        bne     @no
        lda     $7FFF
        cmp     #$A5
        bne     @no
        inx
@no:    stx     t_x
        lda     #$FF
        jsr     _bank_select
        lda     t_x
        ldx     #0
        rts
