; vbi.s - input latched every frame in the deferred VBI, so a press shorter
; than a slow main-loop iteration is never lost. in_held = what is down now,
; in_pressed = what went down since the game last took it (in_take).
; Bits as the SDK's IN_*: LEFT 1, RIGHT 2, UP 4, DOWN 8, FIRE 16, START 32,
; SELECT 64, OPTION 128. Keys: the arrows move as the stick, SPACE = FIRE,
; RETURN = START; key_held: W 1 (in), S 2 (out), A 4 (roll left), D 8
; (roll right) (POKEY: the one key held).
        .export _in_install, _in_take, _in_held, _key_take, _key_held

STICK0 = $0278
STRIG0 = $0284
CONSOL = $D01F
SKSTAT = $D20F
KBCODE = $D209
SETVBV = $E45C
XITVBV = $E462

.segment "BSS"
_in_held:   .res 1
_key_held:  .res 1
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
        lda     #0
        sta     _key_held
        lda     SKSTAT
        and     #$04
        bne     @nokey
        lda     KBCODE
        and     #$3F
        ldx     #0
        ldy     #0
        cmp     #6                  ; + (left arrow)
        bne     :+
        ldx     #$01
:       cmp     #7                  ; * (right)
        bne     :+
        ldx     #$02
:       cmp     #14                 ; - (up)
        bne     :+
        ldx     #$04
:       cmp     #15                 ; = (down)
        bne     :+
        ldx     #$08
:       cmp     #33                 ; SPACE
        bne     :+
        ldx     #$10
:       cmp     #12                 ; RETURN
        bne     :+
        ldx     #$20
:       cmp     #46                 ; W: in
        bne     :+
        ldy     #$01
:       cmp     #62                 ; S: out
        bne     :+
        ldy     #$02
:       cmp     #63                 ; A: roll left
        bne     :+
        ldy     #$04
:       cmp     #58                 ; D: roll right
        bne     :+
        ldy     #$08
:       sty     _key_held
        txa
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
