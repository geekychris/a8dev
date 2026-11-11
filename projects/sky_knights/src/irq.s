; Sky Knights: the display list interrupts and the deferred VBI.
;
; DLI n (n-th of the frame) waits for the end of its line, then sets the
; background (COLBK) and the four playfield colours from the tables: the
; colours around each ledge (field colour 1 is the ledge there, the wraiths
; elsewhere), and the HUDs. The background and PF0 first (they change from band
; to band); the others after.
; VBI (deferred): starts the chain again.

        .export _dli_bk, _dli_pf0, _dli_pf1, _dli_pf2, _dli_pf3, _irq_install

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
NDLI    = 16                     ; around the ledges (and the HUDs)

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
