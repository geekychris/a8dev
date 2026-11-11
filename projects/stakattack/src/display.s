; StakAttack - display interrupts.
;
; The well and the gems are ANTIC 2 characters shown in GTIA mode 10
; (9 colours, two 2-colour-clock pixels a character byte, 4 bits each:
; 0-3 = COLPM0-3, 4-7 = COLPF0-3, 8 = COLBK). The text is ANTIC 6 in the
; normal mode. Display-list interrupts switch between the two, in a fixed
; cycle of four counted from the vertical blank:
;   0 on:     GTIA colours 4-7 (gem_col+4) into COLPF0-3, then GTIA mode 10
;             from the next line
;   1 copper: COLPM0 = the walls' copper (gem_col+0)
;   2 prep:   COLPM0 = the background (gem_col+8)
;   3 off:    the normal mode from the next line, the text colours back from
;             the OS shadows
; In GTIA 10 a blank line (and the border) shows colour 0, and a switch can
; land a line early or late (the OS's NMI dispatch and ANTIC's DMA leave
; little of a scan line). So colour 0 is the background from "prep" to
; "copper", and every switch is next to blank lines (or a text row's blank
; bottom line): the lines around it look the same in either mode. Colours
; 0-3 are the players' (PCOLR0-3; no players are shown), so the OS sets them
; each frame and "on" fits in a scan line.

        .export _display_install, _gem_col

WSYNC   = $D40A
PRIOR   = $D01B
COLPF0  = $D016
COLPM0  = $D012
NMIEN   = $D40E
VDSLST  = $0200
COLOR0  = $02C4
SETVBV  = $E45C
SYSVBV  = $E45F

        .bss
_gem_col: .res 9               ; GTIA colours 0-7, and the background
dli_n:    .res 1

        .code
.proc _display_install
        lda #0
        sta NMIEN
        sta dli_n
        lda #<dli
        sta VDSLST
        lda #>dli
        sta VDSLST+1
        lda #6                  ; immediate VBI: runs every frame
        ldy #<vbi
        ldx #>vbi
        jsr SETVBV
        lda #$C0
        sta NMIEN
        rts
.endproc

.proc dli
        pha
        txa
        pha
        lda dli_n
        inc dli_n
        and #3
        bne notOn
        lda _gem_col+8
        sta COLPM0
        lda _gem_col+4
        sta COLPF0
        lda _gem_col+5
        sta COLPF0+1
        lda _gem_col+6
        sta COLPF0+2
        lda _gem_col+7
        sta COLPF0+3
        lda #$80                ; GTIA mode 10
        sta WSYNC
        sta PRIOR
        jmp done
notOn:  cmp #2
        beq prep
        bcs off
        lda _gem_col            ; copper
        sta COLPM0
        jmp done
prep:   lda _gem_col+8
        sta COLPM0
        jmp done
off:    lda #0
        sta WSYNC
        sta PRIOR
        ldx #3
@t:     lda COLOR0,x
        sta COLPF0,x
        dex
        bpl @t
done:   pla
        tax
        pla
        rti
.endproc

.proc vbi
        lda #0
        sta dli_n
        jmp SYSVBV
.endproc
