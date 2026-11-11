; Warden Keep: the HUD's DLI (white, the clock's colour, gold; the room's
; shades come back from the OS shadows at the next vertical blank)

        .export _dli_clock, _irq_install

WSYNC   = $D40A
COLPF0  = $D016
COLPF1  = $D017
COLPF2  = $D018
NMIEN   = $D40E
VDSLST  = $0200

        .segment "DATA"
_dli_clock: .byte $0E

        .segment "CODE"
dli:    pha
        lda #$0E
        sta WSYNC
        sta COLPF0
        lda _dli_clock
        sta COLPF1
        lda #$1A
        sta COLPF2
        pla
        rti

_irq_install:
        lda #<dli
        sta VDSLST
        lda #>dli
        sta VDSLST+1
        lda #$C0
        sta NMIEN
        rts
