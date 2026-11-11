; tower_row.s - one ring of the tower into the back buffer.
;
; A row is 4 mode D lines: a gap line (the core shows through: pixel 0 is
; transparent to the core's players behind the playfield; it is never
; drawn, so it stays 0) and 3 lines of bricks. The row's bricks are composed once into a 28-byte line buffer,
; then copied to the 3 lines. Each visible column is a segment of pixels
; [x0+1, x1) (x0 stays 0: the gap between columns) filled with the
; pattern of what is in that cell.

        .importzp a3d_bsel, a3d_t, a3d_q
        .import   _a3d_ylo, _a3d_yhi
        .export   _tw_row_, _tw_nseg, _tw_x0, _tw_x1, _tw_col, _tw_pat, _tw_disp, _tw_dim

TW_BYTES = 28
B0       = 3                ; the silhouette's bytes (x 12..99)
B1       = 24
NSEG     = 12

p1      = a3d_t+0
p2      = a3d_t+2
p3      = a3d_t+4
p0      = a3d_t+6
pat     = a3d_t+8
lb      = a3d_t+9
rb      = a3d_t+10
nm      = a3d_t+11
si      = a3d_t+12
dp      = a3d_q

.segment "BSS"
_tw_nseg:   .res 1
_tw_x0:     .res NSEG
_tw_x1:     .res NSEG
_tw_col:    .res NSEG
_tw_dim:    .res NSEG               ; $FF lit, $CC dim (the core shows through)
_tw_pat:    .res 16                 ; pattern byte by cell code
_tw_disp:   .res 22*16              ; cell codes, row 0 (bottom) first
linebuf:    .res TW_BYTES

.segment "RODATA"
keepl:  .byte   $00, $C0, $F0, $FC
keepr:  .byte   $3F, $0F, $03, $00

.segment "CODE"

; void tw_row_(uint8 r): ring r (0..21) at lines 84-4r .. 87-4r
_tw_row_:
        sta     a3d_t+14
        ldx     #0                  ; dp = tw_disp + r*16
        stx     dp+1
        asl     a
        asl     a
        asl     a
        asl     a
        rol     dp+1
        clc
        adc     #<_tw_disp
        sta     dp
        lda     dp+1
        adc     #>_tw_disp
        sta     dp+1
        lda     a3d_t+14            ; first line = 84 - 4r
        asl     a
        asl     a
        eor     #$FF
        sec
        adc     #84
        tax
        lda     _a3d_ylo,x
        sta     p0
        lda     _a3d_yhi,x
        ora     a3d_bsel
        sta     p0+1
        lda     _a3d_ylo+1,x
        sta     p1
        lda     _a3d_yhi+1,x
        ora     a3d_bsel
        sta     p1+1
        lda     _a3d_ylo+2,x
        sta     p2
        lda     _a3d_yhi+2,x
        ora     a3d_bsel
        sta     p2+1
        lda     _a3d_ylo+3,x
        sta     p3
        lda     _a3d_yhi+3,x
        ora     a3d_bsel
        sta     p3+1
        ; ---- compose
        lda     #0
        ldx     #B1
:       sta     linebuf,x
        dex
        cpx     #B0-1
        bne     :-
        lda     #0
        sta     si
@seg:   ldx     si
        cpx     _tw_nseg
        bne     :+
        jmp     @copy
:       inc     si
        ldy     _tw_col,x
        lda     (dp),y
        beq     @seg                ; empty: the core
        tay
        lda     _tw_pat,y
        and     _tw_dim,x
        sta     pat
        ldy     _tw_x0,x
        iny                         ; x0 + 1 (x0: the gap)
        tya
        cmp     _tw_x1,x
        bcs     @seg
        lsr     a
        lsr     a
        sta     lb
        tya
        and     #3
        tay
        lda     keepl,y
        sta     nm
        ldy     _tw_x1,x
        dey
        tya
        and     #3
        tax                         ; (x1-1) & 3
        tya
        lsr     a
        lsr     a
        sta     rb
        ldy     lb
        cmp     lb
        bne     @multi
        lda     keepr,x             ; one byte
        ora     nm
        sta     nm
        lda     linebuf,y
        eor     pat
        and     nm
        eor     pat
        sta     linebuf,y
        jmp     @seg
@multi: lda     keepr,x
        pha
        lda     linebuf,y           ; left byte
        eor     pat
        and     nm
        eor     pat
        sta     linebuf,y
        iny
        lda     pat
@mid:   cpy     rb
        beq     @right
        sta     linebuf,y
        iny
        bne     @mid
@right: pla
        sta     nm
        lda     linebuf,y
        eor     pat
        and     nm
        eor     pat
        sta     linebuf,y
        jmp     @seg
        ; ---- the gap line and 3 brick lines
@copy:  ldy     #B1                 ; the gap line stays 0 (nothing else draws there)
@cl:    lda     linebuf,y
        sta     (p1),y
        sta     (p2),y
        sta     (p3),y
        dey
        cpy     #B0-1
        bne     @cl
        rts
