; disp.s - the course on screen: two text rows (the HUD), then VIS_ROWS
; rows of ANTIC mode 4 from the course's map, scrolled both ways (LMS per
; row, HSCROL / VSCROL). Each map row has its own colours and font: a DLI
; at the end of each row sets the next one's from tables disp_course()
; fills (at $C800, below the players: the PMG area's unused first 768
; bytes). The VBI points the rows at the view (view_x, view_y) and starts
; the DLIs' row count. In LOWCODE / LOWRAM (it runs while a bank is mapped).

        .export _disp_init, _disp_course, _view_x, _view_y, _hud
        .import _c_w, _c_rows, _c_map, _c_cols, _c_rowfont, os_vbi

.segment "ZEROPAGE"
zp1:    .res 2                  ; the interrupts' own pointer (not zp1: they interrupt C)
row:    .res 1                  ; the next row the DLI sets up

T_CH    = $C800                 ; per map row: the font (CHBASE), colours
T_BK    = $C860
T_PF0   = $C8C0
T_PF1   = $C920
T_PF2   = $C980
ROWTAB  = $C9E0                 ; each map row's address (lo, hi), 96 rows: to $CAA0
NT      = 96

WSYNC   = $D40A
COLPF0  = $D016
COLPF1  = $D017
COLPF2  = $D018
COLBK   = $D01A
CHBASE  = $D409
HSCROL  = $D404
VSCROL  = $D405
NMIEN   = $D40E
NMIST   = $D40F
VDSLST  = $0200
VVBLKD  = $0224
SDLSTL  = $0230
VIS     = 23

.segment "LOWRAM"
_hud:   .res 80
dl:     .res 3 + 3 + 1 + VIS * 3 + 3
.segment "LOWDATA"
_view_x: .word 0
_view_y: .word 0
old_vbi: .word 0
t1:     .byte 0
t2:     .byte 0

.segment "LOWCODE"

; The NMI vector points here while the course is shown: on a mode 4 line
; with HSCROL's wide fetch the CPU gets ~40 cycles before WSYNC's cut-off,
; so the way in is short (os.s's dispatcher and pushes made the stores land
; two lines late: the first line of the next row in the old font).
dli:    bit     NMIST
        bmi     @d
        jmp     os_vbi
@d:     pha                     ; on the stack: a one-line first row's DLI
        txa                     ; comes while the HUD's is still running
        pha
        ldx     row
        lda     T_CH,x
        sta     WSYNC
        sta     CHBASE
        lda     T_PF0,x
        sta     COLPF0
        lda     T_PF1,x
        sta     COLPF1
        lda     T_BK,x
        sta     COLBK
        lda     T_PF2,x
        sta     COLPF2
        inc     row
        pla
        tax
        pla
        rti

vbi:    ; the view: map row view_y / 8, line view_y & 7; column view_x / 4
        lda     _view_y
        and     #7
        sta     VSCROL
        lda     _view_y+1
        sta     t2
        lda     _view_y
        lsr     t2
        ror
        lsr     t2
        ror
        lsr     t2
        ror
        sta     row             ; the first row shown
        ; HSCROL scrolling fetches 48 bytes a row and shows the middle 40:
        ; the left edge is LMS * 4 + 16 - HSCROL colour clocks, so for
        ; pixel view_x: LMS = view_x / 4 - 3, HSCROL = 4 - (view_x & 3)
        lda     _view_x
        and     #3
        eor     #$FF
        sec
        adc     #4
        sta     HSCROL
        lda     _view_x+1
        sta     t2
        lda     _view_x
        lsr     t2
        ror
        lsr     t2
        ror
        sec
        sbc     #3
        sta     t1              ; the column - 3 (view_x >= 12)
        ; the rows' LMS: rowtab[row + r] + column
        lda     row
        asl
        tax
        ldy     #8              ; dl + 7: the first course row's LMS (lo)
@r:     lda     ROWTAB,x
        clc
        adc     t1
        sta     dl,y
        lda     ROWTAB+1,x
        adc     #0
        sta     dl+1,y
        inx
        inx
        iny
        iny
        iny
        cpy     #8 + VIS * 3
        bne     @r
        jmp     (old_vbi)

; disp_course(): the loaded course's row addresses, fonts and colours
; (rows past the map: its last row's address, the background)
_disp_course:
        lda     _c_map
        sta     zp1
        lda     _c_map+1
        sta     zp1+1
        ldx     #0
        ldy     #0
@r:     lda     zp1
        sta     ROWTAB,x
        lda     zp1+1
        sta     ROWTAB+1,x
        iny
        cpy     _c_rows
        bcs     @n
        lda     zp1
        clc
        adc     _c_w
        sta     zp1
        bcc     @n
        inc     zp1+1
@n:     inx
        inx
        cpx     #2 * NT
        bne     @r
        ; fonts: $80 + 4 x the row's font
        ldy     #0
@f:     lda     #0
        sta     T_BK,y
        sta     T_PF0,y
        sta     T_PF1,y
        sta     T_PF2,y
        lda     #$80
        sta     T_CH,y
        iny
        cpy     #NT
        bne     @f
        lda     _c_rowfont
        sta     zp1
        lda     _c_rowfont+1
        sta     zp1+1
        ldy     #0
@g:     lda     (zp1),y
        asl
        asl
        clc
        adc     #$80
        sta     T_CH,y
        iny
        cpy     _c_rows
        bne     @g
        ; colours: c_cols, 4 a row
        lda     _c_cols
        sta     zp1
        lda     _c_cols+1
        sta     zp1+1
        ldx     #0
@c:     ldy     #0
        lda     (zp1),y
        sta     T_BK,x
        iny
        lda     (zp1),y
        sta     T_PF0,x
        iny
        lda     (zp1),y
        sta     T_PF1,x
        iny
        lda     (zp1),y
        sta     T_PF2,x
        lda     zp1
        clc
        adc     #4
        sta     zp1
        bcc     @c1
        inc     zp1+1
@c1:    inx
        cpx     _c_rows
        bne     @c
        rts

; disp_init(): the display list, the DLI and the VBI
_disp_init:
        ldx     #0
@b:     lda     dltop,x
        sta     dl,x
        inx
        cpx     #7
        bne     @b
        ldy     #7
        ldx     #VIS - 1
@c:     lda     #$F4            ; DLI, LMS, VSCROL, HSCROL, mode 4
        cpx     #0
        bne     @c1
        lda     #$54            ; the last: no DLI, no VSCROL
@c1:    sta     dl,y
        iny
        iny
        iny
        dex
        bpl     @c
        lda     #$41
        sta     dl,y
        lda     #<dl
        sta     dl+1,y
        lda     #>dl
        sta     dl+2,y
        lda     #<_hud
        sta     dl+4
        lda     #>_hud
        sta     dl+5
        lda     #<dl
        sta     SDLSTL
        lda     #>dl
        sta     SDLSTL+1
        lda     #0
        sta     NMIEN
        lda     #<dli
        sta     VDSLST
        sta     $FFFA
        lda     #>dli
        sta     VDSLST+1
        sta     $FFFB
        lda     VVBLKD
        sta     old_vbi
        lda     VVBLKD+1
        sta     old_vbi+1
        lda     #<vbi
        sta     VVBLKD
        lda     #>vbi
        sta     VVBLKD+1
        lda     #$C0
        sta     NMIEN
        rts

dltop:  .byte   $70, $70, $70, $42, 0, 0, $82   ; 24 blank lines, the HUD (its 2nd row's DLI: course row 0)
