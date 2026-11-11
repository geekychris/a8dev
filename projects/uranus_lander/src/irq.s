; Uranus Lander: the display list interrupts and the deferred VBI.
;
; DLI n (n-th of the frame) waits for the end of its line, then sets the
; background (COLBK) and the four playfield colours from the tables: the
; sky gradient and the ground's strata, a band of 8 lines each, and the
; bottom HUD's colours. The background and PF0 first (they change from band
; to band); the others after.
; VBI (deferred): starts the chain again.

        .export _dli_bk, _dli_pf0, _dli_pf1, _dli_pf2, _dli_pf3, _irq_install
        .export _irq_title

WSYNC   = $D40A
COLPF0  = $D016
COLPF1  = $D017
COLPF2  = $D018
COLPF3  = $D019
COLBK   = $D01A
NMIEN   = $D40E
VDSLST  = $0200
SETVBV  = $E45C
XITVBV  = $E462
NDLI    = 64                     ; the title has 55, the game 23

        .bss
_dli_bk:  .res NDLI
_dli_pf0: .res NDLI
_dli_pf1: .res NDLI
_dli_pf2: .res NDLI
_dli_pf3: .res NDLI
idx:      .res 1

        .code

dli:    pha
        txa
        pha
        tya
        pha
        ldx idx
        lda _dli_bk,x
        ldy _dli_pf0,x
        sta WSYNC
        sta COLBK
        sty COLPF0
        lda _dli_pf1,x
        sta COLPF1
        lda _dli_pf2,x
        sta COLPF2
        lda _dli_pf3,x
        sta COLPF3
        cpx #NDLI - 1
        bcs :+
        inc idx
:       pla
        tay
        pla
        tax
        pla
        rti

; the title: one DLI (at the end of LANDER) colours the planet's 54 lines
; itself, a WSYNC each: a DLI a line would outlast its line and nest. The
; tables hold line i's colours at i, then the text's at 54.
PLANET_LINES = 54
tdli:   pha
        txa
        pha
        tya
        pha
        ldx #0
:       lda _dli_pf0,x
        ldy _dli_pf1,x
        sta WSYNC
        sta COLPF0
        sty COLPF1
        lda _dli_pf2,x
        sta COLPF2
        inx
        cpx #PLANET_LINES + 1
        bne :-
        lda _dli_pf3+PLANET_LINES
        sta COLPF3
        pla
        tay
        pla
        tax
        pla
        rti

; void irq_title(uint8_t on): the title's DLI (1) or the game's chain (0)
_irq_title:
        tax
        lda #$40                ; DLIs off while the vector changes
        sta NMIEN
        txa
        beq :+
        lda #<tdli
        ldx #>tdli
        bne :++
:       lda #<dli
        ldx #>dli
:       sta VDSLST
        stx VDSLST+1
        lda #$C0
        sta NMIEN
        rts

vbi:    lda #0
        sta idx
        jmp XITVBV

_irq_install:
        lda #0
        sta idx
        lda #<dli
        sta VDSLST
        lda #>dli
        sta VDSLST+1
        lda #7
        ldx #>vbi
        ldy #<vbi
        jsr SETVBV
        lda #$C0
        sta NMIEN
        rts
