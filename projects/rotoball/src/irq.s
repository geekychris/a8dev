; Rotoball: the split screen's registers. The deferred VBI sets the top
; pane's (and shows the display list the frame made: dl_next), a DLI on the
; top pane's last floor line the bottom pane's: where the players and
; missiles are, their widths, the other skimmer's colour, the posts' colour
; (missiles are the fifth player: PF3) and the sky.
;
; rg_top / rg_bot: HPOSP0-3, HPOSM0-2, SIZEP0-3, COLPM2, COLPF3, COLBK

        .export _rg_top, _rg_bot, _dl_next, _irq_install

WSYNC   = $D40A
NMIEN   = $D40E
DLISTL  = $D402
VDSLST  = $0200
SDLSTL  = $0230
SETVBV  = $E45C
XITVBV  = $E462
NREG    = 14

        .bss
_rg_top: .res NREG
_rg_bot: .res NREG
_dl_next: .res 1                        ; the display list's page to show (0: none)

        .code
.macro  regs tab
        lda tab+0
        sta $D000
        lda tab+1
        sta $D001
        lda tab+2
        sta $D002
        lda tab+3
        sta $D003
        lda tab+4
        sta $D004
        lda tab+5
        sta $D005
        lda tab+6
        sta $D006
        lda tab+7
        sta $D008
        lda tab+8
        sta $D009
        lda tab+9
        sta $D00A
        lda tab+10
        sta $D00B
        lda tab+11
        sta $D014
        lda tab+12
        sta $D019
        lda tab+13
        sta $D01A
.endmacro

dli:    pha
        lda _rg_bot+13                  ; the sky first (the divider is black)
        sta WSYNC
        sta $D01A
        regs _rg_bot
        pla
        rti

vbi:    regs _rg_top
        lda _dl_next
        beq :+
        sta DLISTL+1
        sta SDLSTL+1
        lda #0
        sta DLISTL
        sta SDLSTL
        sta _dl_next
:       jmp XITVBV

_irq_install:
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
