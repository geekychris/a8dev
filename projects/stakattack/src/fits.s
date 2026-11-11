; StakAttack - the collision test's inner part (game.c's pm_fits).
; The well's rows are bitmasks (fm_lo: columns 0-7, fm_hi: 8-11; the four
; rows below the floor are full), the piece's four rows shifted to its x in
; pm_lo/pm_hi. The piece fits at row y when no row overlaps: 8 ANDs, about
; 70 cycles (cc65's C for the same took ~500).

        .export _fm_lo, _fm_hi, _pm_lo, _pm_hi, _pm_fits_y

FIELD_H = 19

        .bss
_fm_lo: .res FIELD_H + 4
_fm_hi: .res FIELD_H + 4
_pm_lo: .res 4
_pm_hi: .res 4

        .code
; uint8_t __fastcall__ pm_fits_y(uint8_t y): 1 = fits
.proc _pm_fits_y
        tay
        lda _fm_lo,y
        and _pm_lo
        bne no
        lda _fm_hi,y
        and _pm_hi
        bne no
        lda _fm_lo+1,y
        and _pm_lo+1
        bne no
        lda _fm_hi+1,y
        and _pm_hi+1
        bne no
        lda _fm_lo+2,y
        and _pm_lo+2
        bne no
        lda _fm_hi+2,y
        and _pm_hi+2
        bne no
        lda _fm_lo+3,y
        and _pm_lo+3
        bne no
        lda _fm_hi+3,y
        and _pm_hi+3
        bne no
        ldx #0
        lda #1
        rts
no:     ldx #0
        lda #0
        rts
.endproc
