; render.s - Boing Twist's display: the DLI that colours each row, the
; ball drawn by a bank's code, the flip's texture, a twisting band's rows.
;
; All of it is in LOWCODE/LOWDATA (below $4000) because the ball's code
; runs from a 130XE bank mapped at $4000-$7FFF: the DLI and VBI must not
; be in the window, nor the tables they read (those are in the screen area,
; see bt.h).

        .export _disp_start, _disp_stop, _ball_draw, _flip_build, _band_rotate
        .export _bank_ok, _ball_turn, _dli_rows, _hscrol_next
        .export _show_pending, _show_dl, _show_base, _shown
        .import _xe_bank_select
        .importzp ptr1, ptr2, ptr3, ptr4, tmp1, tmp2, tmp3, tmp4

WSYNC   = $D40A
COLPM2  = $D014
COLPM3  = $D015
COLPF0  = $D016
COLPF1  = $D017
COLPF2  = $D018
COLPF3  = $D019
COLBK   = $D01A
NMIEN   = $D40E
HSCROL  = $D404
VDSLST  = $0200
VVBLKD  = $0224
XITVBV  = $E462

; bt.h
; two sets of colour tables (0-87 and 88-175 in each array) for the two
; display lists: the game builds one while the other is shown (bt.h)
TAB_BK  = $AC60
TAB_C0  = TAB_BK + 176
TAB_C1  = TAB_BK + 352
TAB_C2  = TAB_BK + 528
TAB_SH  = TAB_BK + 704
DLISTL  = $D402
DLISTH  = $D403
SDLSTL  = $0230
TEX_A   = $D800                 ; under the OS ROM (boing_twist.cfg)
FT_A    = $B000
SAVED   = $BE00
FLIPTAB = $4000                 ; bank 3

.segment "LOWDATA"
_ball_turn:     .byte 0         ; Y for the bank's code: the turn (or the flip's angle)
_dli_rows:      .byte 0         ; rows the DLI colours (for the profiler)
_hscrol_next:   .byte 0
dli_row:        .byte 0
dli_base:       .byte 0
_show_pending:  .byte 0         ; set by the game: show _show_dl with tables from _show_base
_show_dl:       .word 0
_show_base:     .byte 0
_shown:         .byte 0         ; the set being shown (0 or 1)
t1:             .byte 0
old_vbi:        .word 0

.segment "LOWCODE"

; each display-list entry with the DLI bit sets the next row's colours
dli:    pha
        txa
        pha
        tya
        pha
        ldx     dli_row
        lda     TAB_C1,x
        sta     t1
        lda     TAB_BK,x
        ldy     TAB_C0,x
        sta     WSYNC
        sta     COLBK
        sty     COLPF0
        lda     t1
        sta     COLPF1
        lda     TAB_C2,x
        sta     COLPF2
        lda     TAB_SH,x
        sta     COLPM2
        sta     COLPM3
        inc     dli_row
        pla
        tay
        pla
        tax
        pla
        rti

vbi:    lda     _show_pending
        beq     @same
        lda     _show_dl
        sta     DLISTL
        sta     SDLSTL
        lda     _show_dl+1
        sta     DLISTH
        sta     SDLSTL+1
        lda     _show_base
        sta     dli_base
        lda     #0
        sta     _show_pending
        lda     _shown
        eor     #1
        sta     _shown
@same:  lda     dli_base
        sta     dli_row
        lda     _hscrol_next
        sta     HSCROL
        jmp     (old_vbi)

_disp_start:
        lda     #0
        sta     NMIEN
        lda     #<dli
        sta     VDSLST
        lda     #>dli
        sta     VDSLST+1
        lda     VVBLKD
        cmp     #<vbi
        bne     @hook
        lda     VVBLKD+1
        cmp     #>vbi
        beq     @on
@hook:  lda     VVBLKD
        sta     old_vbi
        lda     VVBLKD+1
        sta     old_vbi+1
        lda     #<vbi
        sta     VVBLKD
        lda     #>vbi
        sta     VVBLKD+1
@on:    lda     #$C0
        sta     NMIEN
        rts

_disp_stop:
        lda     #$40
        sta     NMIEN
        rts

; void __fastcall__ ball_draw(uint8_t bank): run the bank's view code
_ball_draw:
        jsr     _xe_bank_select
        ldy     _ball_turn
        jsr     $4000
        lda     #$FF
        jmp     _xe_bank_select

; uint8_t __fastcall__ bank_ok(uint8_t bank): the bank holds gen_data.py's signature
_bank_ok:
        sta     tmp1
        jsr     _xe_bank_select
        ldx     #0
        lda     $7FFC
        cmp     #'B'
        bne     @no
        lda     $7FFD
        cmp     #'T'
        bne     @no
        lda     tmp1
        ora     #$30
        cmp     $7FFE
        bne     @no
        lda     $7FFF
        cmp     #$A5
        bne     @no
        inx
@no:    stx     tmp2
        lda     #$FF
        jsr     _xe_bank_select
        lda     tmp2
        ldx     #0
        rts

; void flip_build(void): the flip's texture from the ball's at turn
; _ball_turn: FT[r][a] = TEX[tab[r][a] + turn], a = 0..63, and a < 32 again
; at a + 64 (so the flip's code can add its angle in Y)
_flip_build:
        lda     #3
        jsr     _xe_bank_select
        lda     #<FLIPTAB
        sta     ptr1
        lda     #>FLIPTAB
        sta     ptr1+1
        lda     #<FT_A
        sta     ptr2
        lda     #>FT_A
        sta     ptr2+1
        lda     #16
        sta     tmp3            ; rows left
@row:   lda     #0
        sta     tmp4            ; a
@ent:   ldy     #0
        lda     (ptr1),y
        clc
        adc     _ball_turn
        sta     ptr4
        iny
        lda     (ptr1),y
        adc     #>TEX_A
        sta     ptr4+1
        ldy     #0
        lda     (ptr4),y
        ldy     tmp4
        sta     (ptr2),y
        cpy     #32
        bcs     @a1
        tax
        tya
        adc     #64             ; carry clear
        tay
        txa
        sta     (ptr2),y
@a1:    lda     ptr1
        clc
        adc     #2
        sta     ptr1
        bcc     @n1
        inc     ptr1+1
@n1:    inc     tmp4
        lda     tmp4
        cmp     #64
        bne     @ent
        lda     ptr2            ; next row: +96
        clc
        adc     #96
        sta     ptr2
        bcc     @n3
        inc     ptr2+1
@n3:    dec     tmp3
        bne     @row
        lda     #$FF
        jmp     _xe_bank_select

; void __fastcall__ band_rotate(uint16_t arg): rows v0..v0+n-1 of the
; texture from SAVED (their A copy, 64 bytes a row), turned by `shift`
; texels east: TEX[v][u] = SAVED[(u - shift) & 63]. Arguments in
; _rot_v0, _rot_n, _rot_shift.
        .export _rot_v0, _rot_n, _rot_shift
.segment "LOWDATA"
_rot_v0:        .byte 0
_rot_n:         .byte 0
_rot_shift:     .byte 0
.segment "LOWCODE"
_band_rotate:
        lda     #<SAVED
        sta     ptr1
        lda     #>SAVED
        sta     ptr1+1
        ; ptr2 = TEX_A + v0 * 96
        lda     _rot_v0
        sta     ptr2
        lda     #0
        sta     ptr2+1
        asl     ptr2            ; *2
        rol     ptr2+1
        lda     ptr2            ; save v*2
        sta     tmp1
        lda     ptr2+1
        sta     tmp2
        asl     ptr2            ; *4 .. *32 (v*32)
        rol     ptr2+1
        asl     ptr2
        rol     ptr2+1
        asl     ptr2
        rol     ptr2+1
        asl     ptr2
        rol     ptr2+1
        lda     ptr2            ; v*32 + v*64 = v*96: v*32*3 -> v*32 + v*64
        sta     tmp3
        lda     ptr2+1
        sta     tmp4
        asl     ptr2
        rol     ptr2+1          ; v*64
        lda     ptr2
        clc
        adc     tmp3
        sta     ptr2
        lda     ptr2+1
        adc     tmp4
        adc     #>TEX_A
        sta     ptr2+1
        lda     _rot_n
        sta     tmp3
@row:   ldx     #0              ; u
@u:     txa
        sec
        sbc     _rot_shift
        and     #63
        tay
        lda     (ptr1),y
        stx     tmp4
        ldy     tmp4
        sta     (ptr2),y
        cpy     #32
        bcs     @nd
        pha
        tya
        adc     #64
        tay
        pla
        sta     (ptr2),y
@nd:    inx
        cpx     #64
        bne     @u
        lda     ptr1
        clc
        adc     #64
        sta     ptr1
        bcc     @s1
        inc     ptr1+1
@s1:    lda     ptr2
        clc
        adc     #96
        sta     ptr2
        bcc     @s2
        inc     ptr2+1
@s2:    dec     tmp3
        bne     @row
        rts
