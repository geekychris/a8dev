; vbi.s - input latched every frame in the deferred VBI, so a press shorter
; than a slow main-loop iteration is never lost. in_held = what is down now,
; in_pressed = what went down since the game last took it (in_take).
; Bits as the SDK's IN_*: LEFT 1, RIGHT 2, UP 4, DOWN 8, FIRE 16, START 32,
; SELECT 64, OPTION 128. Keys: A/D/W/S or the arrows, SPACE = FIRE,
; RETURN = START (POKEY: the one key held).
        .export _in_install, _in_take, _in_held, _key_take

STICK0 = $0278
STRIG0 = $0284
CONSOL = $D01F
SKSTAT = $D20F
KBCODE = $D209
SETVBV = $E45C
XITVBV = $E462

.segment "BSS"
_in_held:   .res 1
pressed:    .res 1

.segment "RODATA"
; ~STICK0 & 15 (bit 0 up, 1 down, 2 left, 3 right) -> IN bits
stick:  .repeat 16, I
        .byte ((I & 1) << 2) | ((I & 2) << 2) | ((I & 4) >> 2) | ((I & 8) >> 2)
        .endrepeat

.segment "CODE"
vbi:    lda     STICK0
        eor     #$0F
        and     #$0F
        tax
        lda     stick,x
        ldx     STRIG0
        bne     :+
        ora     #$10
:       sta     _in_held
        lda     CONSOL
        eor     #$07
        and     #$07
        asl     a
        asl     a
        asl     a
        asl     a
        asl     a
        ora     _in_held
        sta     _in_held
        lda     SKSTAT
        and     #$04
        bne     @nokey
        lda     KBCODE
        and     #$3F
        ldx     #0
        cmp     #63                 ; A
        beq     @l
        cmp     #6                  ; + (left arrow)
        bne     :+
@l:     ldx     #$01
:       cmp     #58                 ; D
        beq     @r
        cmp     #7                  ; *
        bne     :+
@r:     ldx     #$02
:       cmp     #46                 ; W
        beq     @u
        cmp     #14                 ; -
        bne     :+
@u:     ldx     #$04
:       cmp     #62                 ; S
        beq     @d
        cmp     #15                 ; =
        bne     :+
@d:     ldx     #$08
:       cmp     #33                 ; SPACE
        bne     :+
        ldx     #$10
:       cmp     #12                 ; RETURN
        bne     :+
        ldx     #$20
:       txa
        ora     _in_held
        sta     _in_held
@nokey: lda     prev
        eor     #$FF
        and     _in_held
        ora     pressed
        sta     pressed
        lda     _in_held
        sta     prev
        jmp     XITVBV

prev:   .byte   0

; void in_install(void)
_in_install:
        lda     #0
        sta     pressed
        ldy     #<vbi
        ldx     #>vbi
        lda     #7                  ; deferred VBI
        jmp     SETVBV

; uint8 in_take(void): the presses since the last call
_in_take:
        sei
        lda     pressed
        ldx     #0
        stx     pressed
        cli
        rts

; uint8 key_take(void): the OS's last key code (CH), or $FF; taken
_key_take:
        lda     $02FC
        ldx     #$FF
        stx     $02FC
        ldx     #0
        rts
