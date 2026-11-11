; emutest - the emulator harness's test program (tools/a8dev/tests/test_emulator.py).
;
; Logs through the a8h log port ($D1F0) and exercises what the tests check:
; keyboard (CH), sticks/triggers, console keys, POKEY RANDOM, 130XE banks,
; a JSR'd routine (breakpoints, watchpoints, step-over, backtrace), a signal
; ($D1F1) and a CPU jam on demand. Every frame it changes the background
; colour so per-frame screen hashes differ.
;
; Build: cl65 -t atari -C atari-asm.cfg -Ln emutest.lbl -o emutest.xex emutest.s

        .export start, main_loop, bump, frame_hook, counter, watched, readme, banksig
        .export log_hex, log_str, hexbuf

LOGPORT = $D1F0
SIGPORT = $D1F1
RTCLOK3 = $14
SDMCTL  = $22F
COLOR2  = $2C6          ; playfield shadow: the last key
COLOR4  = $2C8          ; background shadow
CH      = $2FC
STICK0  = $278
STRIG0  = $284
CONSOL  = $D01F
RANDOM  = $D20A
PORTB   = $D301
NMIEN   = $D40E

        .zeropage
ptr:    .res 2

        .bss
counter:  .res 2
watched:  .res 1
readme:   .res 1
last_ch:  .res 1
last_st:  .res 1
last_tr:  .res 1
last_co:  .res 1
nbanks:   .res 1
frames:   .res 1
hexbuf:   .res 3

        .code
start:
        lda     #0
        sta     counter
        sta     counter+1
        sta     watched
        sta     frames
        lda     #$5A
        sta     readme
        lda     #$FF
        sta     CH
        sta     last_ch
        sta     last_st
        sta     last_tr
        sta     last_co
        ; RANDOM right after start: deterministic from power-on in the harness
        lda     #<s_rnd
        ldx     #>s_rnd
        jsr     log_str
        lda     RANDOM
        jsr     log_hex
        jsr     log_nl
        jsr     banks
        lda     #<s_ready
        ldx     #>s_ready
        jsr     log_str

main_loop:
        lda     RTCLOK3
@wait:  cmp     RTCLOK3         ; one pass per frame (the OS VBI counts)
        beq     @wait
        jsr     frame_hook
        jmp     main_loop

; once per frame
frame_hook:
        inc     counter
        bne     :+
        inc     counter+1
:       lda     counter
        asl                     ; (bit 0 of a colour is ignored)
        sta     COLOR4          ; the screen changes every frame
        jsr     bump
        lda     readme          ; a read of 'readme' (read watchpoints)
        inc     frames
        lda     frames
        cmp     #100
        bne     :+
        lda     #42
        sta     SIGPORT         ; signal 42 at frame 100 of the loop
:       ; keyboard
        lda     CH
        cmp     #$FF
        beq     @nokey
        pha
        lda     #$FF
        sta     CH
        pla
        cmp     #$01            ; J: jam the CPU
        bne     :+
        .byte   $02             ; JAM / CIM
:       pha
        asl
        sta     COLOR2          ; keys show on the screen (replays compare it)
        pla
        pha
        lda     #<s_key
        ldx     #>s_key
        jsr     log_str
        pla
        jsr     log_hex
        jsr     log_nl
@nokey: lda     STICK0
        cmp     last_st
        beq     :+
        sta     last_st
        lda     #<s_stick
        ldx     #>s_stick
        jsr     log_str
        lda     last_st
        jsr     log_hex
        jsr     log_nl
:       lda     STRIG0
        cmp     last_tr
        beq     :+
        sta     last_tr
        lda     #<s_trig
        ldx     #>s_trig
        jsr     log_str
        lda     last_tr
        jsr     log_hex
        jsr     log_nl
:       lda     CONSOL
        and     #7
        cmp     last_co
        beq     :+
        sta     last_co
        lda     #<s_consol
        ldx     #>s_consol
        jsr     log_str
        lda     last_co
        jsr     log_hex
        jsr     log_nl
:       rts

; a JSR'd routine that writes 'watched'
bump:
        lda     watched
        clc
        adc     #3
        sta     watched
        rts

; 130XE: bank n gets $B0+n at its first byte and "BANK" at $4001
banks:
        lda     PORTB
        pha
        ldx     #0
@b:     txa
        asl
        asl
        sta     ptr
        lda     #$E3            ; OS on, BASIC off, CPU -> bank, ANTIC -> main
        ora     ptr
        sta     PORTB
        txa
        clc
        adc     #$B0
        sta     $4000
        lda     #'B'
        sta     $4001
        lda     #'A'
        sta     $4002
        lda     #'N'
        sta     $4003
        lda     #'K'
        sta     $4004
        inx
        cpx     #4
        bne     @b
        pla
        sta     PORTB
        lda     #<s_banks
        ldx     #>s_banks
        jsr     log_str
        rts

; log the string at A/X (low/high), zero-terminated
log_str:
        sta     ptr
        stx     ptr+1
        ldy     #0
:       lda     (ptr),y
        beq     :+
        sta     LOGPORT
        iny
        bne     :-
:       rts

log_nl: lda     #$0A
        sta     LOGPORT
        rts

; log A as two hex digits
log_hex:
        pha
        lsr
        lsr
        lsr
        lsr
        jsr     @digit
        pla
        and     #$0F
@digit: cmp     #10
        bcc     :+
        adc     #6              ; carry set: +7
:       adc     #'0'
        sta     LOGPORT
        rts

        .rodata
s_rnd:    .byte "emutest: rnd=", 0
s_ready:  .byte "emutest: ready", $0A, 0
s_banks:  .byte "emutest: banks done", $0A, 0
s_key:    .byte "emutest: key=", 0
s_stick:  .byte "emutest: stick=", 0
s_trig:   .byte "emutest: trig=", 0
s_consol: .byte "emutest: consol=", 0
banksig:  .byte "BANK"
