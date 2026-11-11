; Bullion Dash - the guards' pathfinding search in 6502 (gridbfs.c calls it;
; the C it replaces is the ZX Spectrum SDK's gridbfs.c host version):
;
; gb_run: take cells off the queue (a ring of 2-byte cell offsets in one
; 256-byte page, gb_qhead/gb_qtail byte indexes) until it is empty or the
; wanted cell has its distance; label each neighbour the cell may be left
; to (L, R, U, D) and that can be entered, and queue it.
;
; gb_clear(carry): every distance unknown (255); with carry, cells known to
; have no path (>= FAR) keep FAR.

        .export _gb_run, _gb_clear
        .import _gb_cells, _gb_stride2, _gb_qhead, _gb_qtail, _gb_want
        .import _gb_searched, _gb_overflow, _gb_queue

GB_L    = $01
GB_R    = $02
GB_U    = $04
GB_D    = $08
GB_IN   = $10
FAR     = 253
NCELLS  = 29 * 18               ; GB_CELLS(28, 16) / 2

        .zeropage
mp:     .res 2                  ; the cell taken
vp:     .res 2                  ; the neighbour
qp:     .res 2                  ; the queue's page
wp:     .res 2                  ; the wanted cell
        .bss
dist:   .res 1
        .code

.proc _gb_run
        lda _gb_queue
        sta qp
        lda _gb_queue+1
        sta qp+1
        lda _gb_want
        sta wp
        lda _gb_want+1
        sta wp+1
loop:   lda _gb_qhead
        cmp _gb_qtail
        beq done
        ldy #1
        lda (wp),y
        jmp body
done:   rts
body:   cmp #255
        bne done
        ldy _gb_qhead           ; m = cells + queue[qhead]
        clc
        lda (qp),y
        adc _gb_cells
        sta mp
        iny
        lda (qp),y
        adc _gb_cells+1
        sta mp+1
        iny
        sty _gb_qhead
        inc _gb_searched
        bne @s
        inc _gb_searched+1
@s:     ldy #1
        lda (mp),y
        clc
        adc #1
        sta dist
        cmp #FAR
        bne go
        lda _gb_qtail
        sta _gb_qhead
        rts
go:     ; L: m + 2
        clc
        lda mp
        adc #2
        sta vp
        lda mp+1
        adc #0
        sta vp+1
        lda #GB_L
        jsr visit
        ; R: m - 2
        sec
        lda mp
        sbc #2
        sta vp
        lda mp+1
        sbc #0
        sta vp+1
        lda #GB_R
        jsr visit
        ; U: m + stride
        clc
        lda mp
        adc _gb_stride2
        sta vp
        lda mp+1
        adc _gb_stride2+1
        sta vp+1
        lda #GB_U
        jsr visit
        ; D: m - stride
        sec
        lda mp
        sbc _gb_stride2
        sta vp
        lda mp+1
        sbc _gb_stride2+1
        sta vp+1
        lda #GB_D
        jsr visit
        jmp loop

; the neighbour at vp: may it be left that way (A = bit), not labelled yet?
visit:  ldy #0
        and (vp),y
        beq @r
        iny
        lda (vp),y
        cmp #255
        bne @r
        lda dist
        sta (vp),y
        dey
        lda (vp),y
        and #GB_IN
        beq @r
        ldy _gb_qtail           ; queue it: vp - cells
        sec
        lda vp
        sbc _gb_cells
        sta (qp),y
        iny
        lda vp+1
        sbc _gb_cells+1
        sta (qp),y
        iny
        sty _gb_qtail
        cpy _gb_qhead
        bne @r
        lda #1
        sta _gb_overflow
@r:     rts
.endproc

; void __fastcall__ gb_clear(unsigned char carry)
.proc _gb_clear
        sta carry
        clc
        lda _gb_cells
        adc #1
        sta vp
        lda _gb_cells+1
        adc #0
        sta vp+1
        lda #<NCELLS
        sta n
        lda #>NCELLS
        sta n+1
        ldy #0
@l:     lda carry
        beq @u
        lda (vp),y
        cmp #FAR
        bcc @u
        lda #FAR
        bne @w
@u:     lda #255
@w:     sta (vp),y
        clc
        lda vp
        adc #2
        sta vp
        bcc @n
        inc vp+1
@n:     lda n
        bne @d
        dec n+1
@d:     dec n
        lda n
        ora n+1
        bne @l
        rts
        .bss
carry:  .res 1
n:      .res 2
.endproc
