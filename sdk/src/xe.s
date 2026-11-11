; a8xe.h: 130XE bank switching. Everything here is in LOWCODE/LOWDATA (below
; $4000, outside the bank window). PORTB: bits 2-3 bank, bit 4 = 0 CPU sees
; the bank at $4000-$7FFF, bit 5 = 0 ANTIC sees it (we keep 1: main memory).
        .export         _xe_bank_select, _xe_bank_current, _xe_bank_count
        .export         _xe_bank_copy
        .importzp       ptr1, ptr2, ptr3

PORTB   = $D301

.segment "LOWDATA"
_xe_bank_current: .byte $FF
        .export _xe_byte
_xe_byte:       .byte 0
; xe_bank_copy arguments (set by xebank.c)
        .export _xe_cp_bank, _xe_cp_dir, _xe_cp_win, _xe_cp_mem, _xe_cp_len
_xe_cp_bank:    .byte 0
_xe_cp_dir:     .byte 0         ; 0 = main -> bank, 1 = bank -> main
_xe_cp_win:     .word 0         ; address in the window ($4000 + offset)
_xe_cp_mem:     .word 0         ; main memory address
_xe_cp_len:     .word 0
saved:          .byte 0
sig:            .res 5

.segment "LOWCODE"

; void __fastcall__ xe_bank_select(uint8_t bank)   bank 0..3, $FF = main
_xe_bank_select:
        sta     _xe_bank_current
        cmp     #$FF
        beq     @main
        and     #3
        asl
        asl
        sta     ptr1
        lda     PORTB
        and     #$C3            ; keep OS, BASIC, bit 6, self test
        ora     #$20            ; ANTIC: main memory; CPU: extended (bit 4 = 0)
        ora     ptr1
        sta     PORTB
        rts
@main:  lda     PORTB
        ora     #$30
        sta     PORTB
        rts

; copy _xe_cp_len bytes between the window and main memory with the bank
; mapped (interrupts stay on: the OS handlers live in ROM, page 2/3 vectors)
_xe_bank_copy:
        lda     _xe_cp_bank
        jsr     _xe_bank_select
        lda     _xe_cp_dir
        bne     @in
        lda     _xe_cp_mem              ; src = mem, dst = window
        sta     ptr1
        lda     _xe_cp_mem+1
        sta     ptr1+1
        lda     _xe_cp_win
        sta     ptr2
        lda     _xe_cp_win+1
        sta     ptr2+1
        jmp     @go
@in:    lda     _xe_cp_win              ; src = window, dst = mem
        sta     ptr1
        lda     _xe_cp_win+1
        sta     ptr1+1
        lda     _xe_cp_mem
        sta     ptr2
        lda     _xe_cp_mem+1
        sta     ptr2+1
@go:    lda     _xe_cp_len
        sta     ptr3
        lda     _xe_cp_len+1
        sta     ptr3+1
        ldy     #0
        ldx     ptr3+1
        beq     @tail
@page:  lda     (ptr1),y
        sta     (ptr2),y
        iny
        bne     @page
        inc     ptr1+1
        inc     ptr2+1
        dex
        bne     @page
@tail:  ldx     ptr3
        beq     @done
@byte:  lda     (ptr1),y
        sta     (ptr2),y
        iny
        dex
        bne     @byte
@done:  lda     #$FF
        jmp     _xe_bank_select

; uint8_t xe_bank_count(void): write a signature to $4000 in main and each
; bank, then check each bank still holds its own (aliases on smaller
; machines overwrite each other).
_xe_bank_count:
        lda     $4000
        sta     saved
        ldx     #3
@w:     txa
        jsr     _xe_bank_select_x
        txa
        ora     #$A0
        sta     $4000
        dex
        bpl     @w
        lda     #$FF
        jsr     _xe_bank_select
        lda     #$5A
        sta     $4000                   ; main memory's own byte
        ldx     #0
@r:     txa
        jsr     _xe_bank_select_x
        txa
        ora     #$A0
        cmp     $4000
        bne     @no
        inx
        cpx     #4
        bne     @r
@no:    stx     ptr1                    ; banks that kept their signature
        lda     #$FF
        jsr     _xe_bank_select
        lda     $4000
        cmp     #$5A                    ; main overwritten -> no extended RAM
        beq     @ok
        ldx     #0
        stx     ptr1
@ok:    lda     saved
        sta     $4000
        lda     ptr1
        ldx     #0
        rts

_xe_bank_select_x:                      ; select bank A, keep X
        stx     sig
        jsr     _xe_bank_select
        ldx     sig
        rts
