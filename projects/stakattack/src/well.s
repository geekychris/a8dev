; StakAttack - the well's 19 x 12 cells (gs.field, 12 bytes a row) to the
; screen (ANTIC 2 rows of 48 bytes from $A400, the well at column 18): a
; cell's character is $40 + its value. ~4,000 cycles (C: ~10,000), at the
; start of a game and after a line clear.

        .export _well_copy
        .importzp ptr1, ptr2

WELL    = $A400
RB      = 48
COL     = 18

        .code
; void __fastcall__ well_copy(const uint8_t *field)
.proc _well_copy
        sta ptr1
        stx ptr1+1
        lda #<(WELL + COL)
        sta ptr2
        lda #>(WELL + COL)
        sta ptr2+1
        ldx #19
row:    ldy #11
cell:   lda (ptr1),y
        ora #$40
        sta (ptr2),y
        dey
        bpl cell
        clc
        lda ptr1
        adc #12
        sta ptr1
        bcc :+
        inc ptr1+1
:       clc
        lda ptr2
        adc #RB
        sta ptr2
        bcc :+
        inc ptr2+1
:       dex
        bne row
        rts
.endproc
