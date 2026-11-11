; Emberkeep: the DLI chain (each tile row's accent colour: the rune's
; yellow, the oil's orange, the portal's green; then the HUD's colours)
; and the deferred VBI that restarts it. In LOWCODE / LOWBSS: they must
; still be there while lamp.s has a 130XE bank mapped at $4000-$7FFF.

        .export _dli_pf0, _dli_pf1, _dli_pf2, _dli_pf3, _irq_install

WSYNC   = $D40A
COLPF0  = $D016
COLPF1  = $D017
COLPF2  = $D018
COLPF3  = $D019
NMIEN   = $D40E
VDSLST  = $0200
SETVBV  = $E45C
XITVBV  = $E462
NDLI    = 16

        .segment "LOWBSS"
_dli_pf0: .res NDLI
_dli_pf1: .res NDLI
_dli_pf2: .res NDLI
_dli_pf3: .res NDLI
idx:      .res 1

        .segment "LOWCODE"
dli:    pha
        txa
        pha
        ldx idx
        lda _dli_pf2,x
        sta WSYNC
        sta COLPF2
        lda _dli_pf0,x
        sta COLPF0
        lda _dli_pf1,x
        sta COLPF1
        lda _dli_pf3,x
        sta COLPF3
        cpx #NDLI - 1
        bcs :+
        inc idx
:       pla
        tax
        pla
        rti

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
