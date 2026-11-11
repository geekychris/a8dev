; xload.s - the XEX loads the code that runs under the OS ROM: each part
; is a chunk at $9800 (the players' and the screen's area, unused while
; loading), then a chunk that sets $06F0 (where to, how long) and INITAD to
; the copy routine at $0600, which the loader calls: it switches the ROM
; off (interrupts off) for the copy and back on. tank_duel.cfg.

        .import __HI1CODE_LOAD__, __HI1CODE_RUN__, __HI1CODE_SIZE__
        .import __HI2CODE_LOAD__, __HI2CODE_RUN__, __HI2CODE_SIZE__
        .import __STG1_START__, __STG1_LAST__, __STG2_START__, __STG2_LAST__
        .import __XLC_START__, __XLC_LAST__

PORTB   = $D301
NMIEN   = $D40E
INITAD  = $02E2
PARM    = $06F0                 ; to (2), length (2)

.macro  chunk start, end
        .word   start, end
.endmacro

        .import __LOWX_START__, __LOWX_LAST__
.segment "LOWXH"
        chunk   __LOWX_START__, __LOWX_LAST__ - 1
.segment "XLCH"
        chunk   __XLC_START__, __XLC_LAST__ - 1
.segment "XLCOPY"
        .org    $0600
copy:   sei
        lda     #0
        sta     NMIEN
        lda     PORTB
        and     #$FE
        sta     PORTB
        lda     #<$9800
        sta     $CB
        lda     #>$9800
        sta     $CC
        lda     PARM
        sta     $CD
        lda     PARM+1
        sta     $CE
        ldx     PARM+3          ; whole pages, then the rest
        ldy     #0
@pg:    cpx     #0
        beq     @rest
@b:     lda     ($CB),y
        sta     ($CD),y
        iny
        bne     @b
        inc     $CC
        inc     $CE
        dex
        jmp     @pg
@rest:  cpy     PARM+2
        beq     @done
        lda     ($CB),y
        sta     ($CD),y
        iny
        jmp     @rest
@done:  lda     PORTB
        ora     #$01
        sta     PORTB
        lda     #$40
        sta     NMIEN
        cli
        rts
        .reloc

.segment "XS1H"
        chunk   __STG1_START__, __STG1_LAST__ - 1
.segment "XS1I"
        chunk   PARM, PARM + 3
        .word   __HI1CODE_RUN__, __HI1CODE_SIZE__
        chunk   INITAD, INITAD + 1
        .word   copy

.segment "XS2H"
        chunk   __STG2_START__, __STG2_LAST__ - 1
.segment "XS2I"
        chunk   PARM, PARM + 3
        .word   __HI2CODE_RUN__, __HI2CODE_SIZE__
        chunk   INITAD, INITAD + 1
        .word   copy
