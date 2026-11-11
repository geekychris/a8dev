; Lunar Rider: display list interrupts and the deferred vertical blank.
;
; DLI: a chain of up to 8 colour/scroll changes down the screen. Each one
; waits for the end of its scan line (WSYNC) and then writes HSCROL and
; COLPF0..2 from the tables below (entry n for the n-th DLI of the frame).
; That gives each scrolling band its own fine scroll and palette.
;
; VBI (deferred): resets the DLI chain and, when the game asks (lms_req:
; bit 7 = new fine scroll values, bits 0..2 = bands that moved), copies the
; fine scroll values and rewrites the LMS address of every line of the
; bands that moved:
; band b has band_n[b] lines, 3 display list bytes each starting at
; band_dl[b] (the LMS low byte), line i showing band_addr[b] + i*stride.
; Done in the vertical blank so the screen never shows half an update.

        .export _dli_hs, _dli_pf0, _dli_pf1, _dli_pf2
        .export _band_dl_lo, _band_dl_hi, _band_addr_lo, _band_addr_hi
        .export _band_n, _band_stride, _lms_req, _pend_hs
        .export _irq_install, _irq_remove

WSYNC   = $D40A
HSCROL  = $D404
COLPF0  = $D016
COLPF1  = $D017
COLPF2  = $D018
NMIEN   = $D40E
VDSLST  = $0200
SETVBV  = $E45C
XITVBV  = $E462

NBANDS  = 3

        .segment "ZEROPAGE"
ptr:    .res 2
adr:    .res 2
cnt:    .res 1
mask:   .res 1

        .bss
_dli_hs:      .res 8
_dli_pf0:     .res 8
_dli_pf1:     .res 8
_dli_pf2:     .res 8
_band_dl_lo:  .res NBANDS
_band_dl_hi:  .res NBANDS
_band_addr_lo:.res NBANDS
_band_addr_hi:.res NBANDS
_band_n:      .res NBANDS
_band_stride: .res NBANDS
_lms_req:     .res 1
_pend_hs:     .res 8
dli_idx:      .res 1

        .code

dli:    pha
        txa
        pha
        tya
        pha
        ldx dli_idx
        ldy _dli_hs,x
        lda _dli_pf2,x
        sta WSYNC               ; to the end of this line, then quickly:
        sty HSCROL              ; the fine scroll and the background (PF2)
        sta COLPF2              ; first: an ANTIC 2 row's first line halts the
        lda _dli_pf1,x          ; CPU soon after, so these two come last
        sta COLPF1
        lda _dli_pf0,x
        sta COLPF0
        inc dli_idx
        pla
        tay
        pla
        tax
        pla
        rti

vbi:    lda #0
        sta dli_idx
        lda _lms_req
        beq done
        ldx #7                  ; the fine scroll that goes with the new LMS
:       lda _pend_hs,x
        sta _dli_hs,x
        dex
        bpl :-
        lda _lms_req            ; bits 0..2: the bands whose LMS moved
        and #7
        sta mask
        ldx #0
band:   lsr mask
        bcc next
        lda _band_dl_lo,x
        sta ptr
        lda _band_dl_hi,x
        sta ptr+1
        lda _band_addr_lo,x
        sta adr
        lda _band_addr_hi,x
        sta adr+1
        lda _band_n,x
        sta cnt
line:   ldy #0
        lda adr
        sta (ptr),y
        iny
        lda adr+1
        sta (ptr),y
        lda adr
        clc
        adc _band_stride,x
        sta adr
        bcc :+
        inc adr+1
:       lda ptr
        clc
        adc #3
        sta ptr
        bcc :+
        inc ptr+1
:       dec cnt
        bne line
next:   inx
        cpx #NBANDS
        bne band
        lda #0
        sta _lms_req
done:   jmp XITVBV

; void irq_install(void): DLI vector, DLIs on, the deferred VBI
_irq_install:
        lda #0
        sta dli_idx
        sta _lms_req
        lda #<dli
        sta VDSLST
        lda #>dli
        sta VDSLST+1
        lda #7
        ldx #>vbi
        ldy #<vbi
        jsr SETVBV
        lda #$C0
        sta NMIEN
        rts

; void irq_remove(void): back to the OS's deferred VBI, DLIs off
_irq_remove:
        lda #$40
        sta NMIEN
        lda #7
        ldx #>XITVBV
        ldy #<XITVBV
        jmp SETVBV
