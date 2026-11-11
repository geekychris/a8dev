; vbi.s - the deferred vertical blank: the SDK's sound player every frame
; (os.s calls (VVBLKD); the sound player uses ptr1, tmp1, tmp2: kept)
        .export _vbi_install
        .import _sound_tick, _os_vbi_exit
        .importzp ptr1, tmp1, tmp2
VVBLKD = $0224

.segment "CODE"
vbi:    lda     ptr1
        pha
        lda     ptr1+1
        pha
        lda     tmp1
        pha
        lda     tmp2
        pha
        jsr     _sound_tick
        pla
        sta     tmp2
        pla
        sta     tmp1
        pla
        sta     ptr1+1
        pla
        sta     ptr1
        jmp     _os_vbi_exit

_vbi_install:
        sei
        lda     #<vbi
        sta     VVBLKD
        lda     #>vbi
        sta     VVBLKD+1
        cli
        rts
