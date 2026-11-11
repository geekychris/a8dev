; ScAIry Fun - the display list's DLI bits and the colour table for the
; bands just drawn (render.c's loop, in asm: ~25,000 cycles in C).
;
; In: the bands (rows.s: bt_n, bt_top, bt_b; project.s: bseg), the
; frame's colours (co_*), the display list's view rows (co_dl), the table
; (co_tb: dlt_bk + 0 or 32; the other three tables follow it 64 bytes
; apart), the rows given a DLI last time with this list (co_set, co_nset).
; DLI 0 (the line above the view) and the sky's steps are set by render.c
; first (co_ndl entries); the bands follow from the top down, then the text.

        .export _band_colours
        .export _co_g1, _co_g2, _co_road, _co_line, _co_water, _co_shadow, _co_path
        .export _co_dl, _co_tb, _co_set, _co_nset, _co_ndl
        .import _bt_n, _bt_top, _bt_b, _bseg, _seg_flags, _dloff
        .importzp ptr1, ptr2, ptr3

SF_FLOOD    = $01
SF_OVERPASS = $04
SF_FINISH   = $40
VROWS       = 56

.segment "SCRBSS"
_co_g1:     .res 1
_co_g2:     .res 1
_co_road:   .res 1
_co_line:   .res 1
_co_water:  .res 1
_co_shadow: .res 1
_co_path:   .res 1
_co_dl:     .res 2
_co_tb:     .res 2
_co_set:    .res 2              ; the rows set (indices into the list), 24 at most
_co_nset:   .res 1              ; in: how many set last time (cleared first); out: now
_co_ndl:    .res 1              ; in: DLI entries already made; out: total
n:          .res 1
fl:         .res 1
par:        .res 1
e:          .res 1
ins:        .res 1

.segment "HIA2"
.proc _band_colours
        lda _co_dl
        sta ptr1
        lda _co_dl+1
        sta ptr1+1
        lda _co_tb
        sta ptr2
        lda _co_tb+1
        sta ptr2+1
        lda _co_set
        sta ptr3
        lda _co_set+1
        sta ptr3+1
        ; nothing from the bands yet: the sky's DLIs were added by render.c
        ; to co_set after the old ones were cleared there
        lda _bt_n
        sta n
loop:   dec n
        bpl :+
        jmp text
:       ldx n
        lda _bt_b,x             ; boundary j
        tay
        asl a
        tax
        lda _bseg,x             ; (the low byte: parity and flags)
        sta par
        lda _bseg+1,x
        sta e
        ; the segment's flags
        clc
        lda #<_seg_flags
        adc _bseg,x
        sta ptr3
        lda #>_seg_flags
        adc _bseg+1,x
        sta ptr3+1
        tya
        pha
        ldy #0
        lda (ptr3),y
        sta fl
        lda _co_set             ; (ptr3 back)
        sta ptr3
        lda _co_set+1
        sta ptr3+1
        pla
        ; parity: seg >> (j < 9 ? 0 : j < 13 ? 1 : 2), the low bit
        cmp #7
        bcc p0
        cmp #10
        lda par
        bcc p1
        lsr a
p1:     lsr a
        sta par
p0:     lda par
        and #1
        sta par
        ; the entry: DLI 0 for a band from the top, else the next
        ldx n
        lda _bt_top,x
        bne mid
        lda #0
        sta e
        jmp col
mid:    tay                     ; row top - 1's instruction
        lda _dloff-1,y
        sta ins
        tay
        lda (ptr1),y
        ora #$80
        sta (ptr1),y
        ldy _co_nset
        lda ins
        sta (ptr3),y
        inc _co_nset
        lda _co_ndl
        sta e
        inc _co_ndl
col:    ldy e
        lda par                 ; the ground
        beq :+
        lda _co_g1
        jmp :++
:       lda _co_g2
:       sta (ptr2),y
        tya                     ; the road (+64)
        clc
        adc #64
        tay
        lda fl
        and #SF_FLOOD
        beq :+
        lda _co_water
        jmp rset
:       lda fl
        and #SF_OVERPASS
        beq :+
        lda _co_shadow
        jmp rset
:       lda _co_road
rset:   sta (ptr2),y
        tya                     ; the lines (+128)
        clc
        adc #64
        tay
        lda fl
        and #SF_FINISH
        beq :+
        lda #$0E
        jmp lset
:       lda par
        beq :+
        lda _co_line
        jmp lset
:       lda fl
        and #SF_FLOOD
        beq :+
        lda _co_water
        jmp lset
:       lda _co_road
lset:   sta (ptr2),y
        tya                     ; the water and the path (+192)
        clc
        adc #64
        tay
        lda _co_path
        sta (ptr2),y
        jmp loop
text:   ; the last view row: the text's colours
        ldy _dloff+VROWS-1
        lda (ptr1),y
        ora #$80
        sta (ptr1),y
        ldy _co_ndl
        lda #0
        sta (ptr2),y
        tya
        clc
        adc #64
        tay
        lda #0
        sta (ptr2),y
        tya
        clc
        adc #64
        tay
        lda #$0C
        sta (ptr2),y
        tya
        clc
        adc #64
        tay
        lda #$82
        sta (ptr2),y
        rts
.endproc
