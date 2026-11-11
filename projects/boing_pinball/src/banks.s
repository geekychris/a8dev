; banks.s - calls into the 130XE banks. The physics (bank 0) and the rules
; (bank 1) run in the bank window $4000-$7FFF; each function the rest of
; the game calls has a trampoline here (LOWCODE, outside the window) that
; maps its bank, calls it with A/X (cc65's last argument) and the C stack
; as they were, maps the bank that was there back and returns A/X (and
; sreg). The banked code is compiled with its functions renamed b0_* /
; b1_* (Makefile), so these take the original names.

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

        ; the physics: hot (bank 0), cold (bank 2)
        tramp   _pb_world_step,     0, _b0_world_step
        tramp   _pb_flipper_tip,    0, _b0_flipper_tip
        tramp   _pb_world_init,     2, _b2_world_init
        tramp   _pb_add_ball,       2, _b2_add_ball
        tramp   _pb_plunge,         2, _b2_plunge
        tramp   _pb_kick_saucer,    2, _b2_kick_saucer
        tramp   _pb_nudge,          2, _b2_nudge
        tramp   _pb_remove_drained, 2, _b2_remove_drained
        tramp   _pb_ramp_step,      2, _b2_ramp_step
        tramp   _pb_mb_prepare,     2, _b2_mb_prepare
        tramp   _pb_hit_balls,      2, _b2_hit_balls
        ; the rules
        tramp   _pb_game_init,      1, _b1_game_init
        tramp   _pb_game_start,     1, _b1_game_start
        tramp   _pb_game_tick,      1, _b1_game_tick
        tramp   _pb_live_balls,     1, _b1_live_balls
        tramp   _pb_say,            1, _b1_say

.ifdef FLIPCHECK
        tramp   _flip_ref,          $FF, _c_flip_ref
.endif
.ifdef SEGCHECK
        tramp   _seg_ref,           $FF, _c_seg_ref
        tramp   _seg_mismatch,      $FF, _c_seg_mismatch
.endif

; uint8_t __fastcall__ bank_ok(uint8_t bank): the bank holds xbanks.s's signature
_bank_ok:
        sta     t_a
        jsr     _bank_select
        ldx     #0
        lda     $7FFC
        cmp     #'P'
        bne     @no
        lda     $7FFD
        cmp     #'B'
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
