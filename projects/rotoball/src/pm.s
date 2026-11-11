; Rotoball: the players and missiles of both panes (single-line PMG at
; $9800: missiles $9B00, players $9C00-$9FFF).
;
; pm_put: player pp_n's shape for pane pp_pane: rubs out what it drew
; there last, copies the shape (pp_h lines from pp_src, its top at pane line
; pp_top, signed; pp_h 0: none) clipped to the pane (lines 0-94 of the top,
; 2-95 of the bottom: the DLI moves the players between them). pm_post: missile po_m's post in pane po_pane,
; PM lines po_dst .. + po_len (its bits only). ~15 cycles a line.

        .export _pm_put, _pp_n, _pp_pane, _pp_src, _pp_top, _pp_h
        .export _pm_post, _po_m, _po_pane, _po_dst, _po_len

        .segment "ZEROPAGE"
_pp_src: .res 2
pm:     .res 2

        .bss
_pp_n:   .res 1
_pp_pane: .res 1
_pp_top: .res 1
_pp_h:   .res 1
pp_dst:  .res 1
pp_len:  .res 1
_po_m:   .res 1
_po_pane: .res 1
_po_dst: .res 1
_po_len: .res 1
pstart:  .res 8                         ; player n, pane p: [n * 2 + p]
plen:    .res 8
mstart:  .res 6                         ; missile m, pane p
mlen:    .res 6
mbits:   .res 1
slot:    .res 1
j0:      .res 1
lh:      .res 8                         ; the last shape: lines, top, source
ltop:    .res 8
lsrc:    .res 16

        .rodata
plo:    .byte 0, 2                      ; a pane's lines: first,
phi:    .byte 95, 96                    ; last + 1
pbase:  .byte 32, 128                   ; its line 0 in PM lines

        .rodata
mmask:  .byte $03, $0C, $30

        .code
_pm_put:
        lda _pp_n                       ; the same as last time: nothing to do
        asl a
        ora _pp_pane
        tax
        lda _pp_h
        cmp lh,x
        bne @chg
        lda _pp_top
        cmp ltop,x
        bne @chg
        lda _pp_src
        cmp lsrc,x
        bne @chg
        lda _pp_src+1
        cmp lsrc+8,x
        bne @chg
        rts
@chg:   lda _pp_h
        sta lh,x
        lda _pp_top
        sta ltop,x
        lda _pp_src
        sta lsrc,x
        lda _pp_src+1
        sta lsrc+8,x
        ldx _pp_pane                    ; clip: j0 = max(0, lo - top),
        lda #0                          ; j1 = min(h, hi - top)
        sta j0
        lda _pp_h
        beq @none
        lda _pp_top
        sec
        sbc plo,x
        bvc :+
        eor #$80
:       bpl @lo_ok                      ; top >= lo
        lda plo,x
        sec
        sbc _pp_top
        sta j0
@lo_ok: lda phi,x
        sec
        sbc _pp_top                     ; (1..188)
        cmp _pp_h
        bcc :+
        lda _pp_h
:       sec
        sbc j0                          ; the lines shown
        beq @none
        bcc @none
        sta pp_len
        lda _pp_top
        clc
        adc j0
        clc
        adc pbase,x
        sta pp_dst
        lda _pp_src
        clc
        adc j0
        sta _pp_src
        bcc @go
        inc _pp_src+1
        jmp @go
@none:  lda #0
        sta pp_len
@go:    lda _pp_n
        asl a
        ora _pp_pane
        sta slot
        lda _pp_n
        clc
        adc #$9C
        sta pm+1
        lda #0
        sta pm
        ldx slot                        ; rub out the last lines
        ldy pstart,x
        lda plen,x
        beq @new
        tax
        lda #0
:       sta (pm),y
        iny
        dex
        bne :-
@new:   ldx slot
        lda pp_dst
        sta pstart,x
        lda pp_len
        sta plen,x
        beq @done
        tax                             ; the new: pm + dst, len lines
        lda pp_dst
        sta pm
        ldy #0
:       lda (_pp_src),y
        sta (pm),y
        iny
        dex
        bne :-
@done:  rts

_pm_post:
        lda _po_m
        asl a
        ora _po_pane
        sta slot
        ldx _po_m
        lda mmask,x
        sta mbits
        lda #$9B
        sta pm+1
        lda #0
        sta pm
        ldx slot
        ldy mstart,x
        lda mlen,x
        beq @new
        tax
        lda mbits
        eor #$FF
        sta mbits
:       lda (pm),y
        and mbits
        sta (pm),y
        iny
        dex
        bne :-
        lda mbits
        eor #$FF
        sta mbits
@new:   ldx slot
        lda _po_dst
        sta mstart,x
        lda _po_len
        sta mlen,x
        beq @done
        tax
        ldy _po_dst
:       lda (pm),y
        ora mbits
        sta (pm),y
        iny
        dex
        bne :-
@done:  rts
