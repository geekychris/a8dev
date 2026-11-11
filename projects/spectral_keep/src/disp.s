; disp.s - the room's colour bands: a DLI at the end of every 8th line
; sets PF0-PF2 for the next band (the first band's come from the shadows,
; COLOR0-2); the last one sets the HUD's (text: PF1 luminance on PF2).
; In LOWCODE: it runs whatever bank is mapped.

        .export _dli_c0, _dli_c1, _dli_c2, _disp_install
        .import _spr_vbi

WSYNC   = $D40A
COLPF0  = $D016
COLPF1  = $D017
COLPF2  = $D018
NMIEN   = $D40E
VDSLST  = $0200
VVBLKD  = $0224
NBAND   = 26

.segment "SCRBSS"
_dli_c0: .res NBAND             ; [0..24]: bands 1..25; [25]: the HUD
_dli_c1: .res NBAND
_dli_c2: .res NBAND
idx:     .res 1

.segment "XCODE1"
dli:    pha
        txa
        pha
        ldx     idx
        lda     _dli_c0,x
        sta     WSYNC
        sta     COLPF0
        lda     _dli_c1,x
        sta     COLPF1
        lda     _dli_c2,x
        sta     COLPF2
        inc     idx
        pla
        tax
        pla
        rti

vbi:    lda     #0
        sta     idx
        jmp     _spr_vbi

; void disp_install(void)
_disp_install:
        sei
        lda     #<dli
        sta     VDSLST
        lda     #>dli
        sta     VDSLST+1
        lda     #<vbi
        sta     VVBLKD
        lda     #>vbi
        sta     VVBLKD+1
        lda     #$C0
        sta     NMIEN
        cli
        rts
