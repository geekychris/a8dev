; Frank the Frog - display interrupts.
;
; The game screen is 13 rows of 16 scan lines (2 ANTIC mode lines each) and
; a status line. A display-list interrupt at the end of each row loads the
; next row's colours and fine-scroll value (HSCROL), so every lane has its own
; palette and scrolls on its own. The deferred vertical-blank interrupt
; copies the lanes' new scroll positions (computed by the game during the
; frame: pend_*) into the display list and resets the DLI row counter.

        .export _display_install, _display_remove
        .export _pend_lo, _pend_hi, _pend_hs, _pend_ready, _dl_off
        .export _row_hs, _row_bk, _row_pf0, _row_pf1, _row_pf2, _row_pf3
        .export _dl_base, _pend_hpos

WSYNC   = $D40A
HSCROL  = $D404
COLPF0  = $D016
COLPF1  = $D017
COLPF2  = $D018
COLPF3  = $D019
COLBK   = $D01A
NMIEN   = $D40E
VDSLST  = $0200
SETVBV  = $E45C
XITVBV  = $E462

NLMS    = 20                    ; scrolled mode lines (10 lanes x 2)
NROWS   = 13                    ; DLIs: rows 1..12 and the status line

        .bss
_pend_lo:   .res NLMS
_pend_hi:   .res NLMS
_pend_hs:   .res NROWS
_dl_off:    .res NLMS           ; offset of each LMS address in the display list
_row_hs:    .res NROWS
_row_bk:    .res NROWS
_row_pf0:   .res NROWS
_row_pf1:   .res NROWS
_row_pf2:   .res NROWS
_row_pf3:   .res NROWS
_dl_base:   .res 2
_pend_ready: .res 1
_pend_hpos: .res 1             ; Frank's players' x, set with the lanes
dli_idx:    .res 1

        .code

; void display_install(void): DLI vector + deferred VBI, DLIs on
.proc _display_install
        lda #0
        sta NMIEN               ; no NMIs while the vectors change (VBI too)
        lda #<dli
        sta VDSLST
        lda #>dli
        sta VDSLST+1
        lda #7                  ; deferred VBI
        ldy #<vbi
        ldx #>vbi
        jsr SETVBV
        lda #$C0                ; DLI + VBI
        sta NMIEN
        rts
.endproc

; void display_remove(void): back to the OS's deferred VBI, DLIs off
.proc _display_remove
        lda #$40
        sta NMIEN
        lda #7
        ldy #<XITVBV
        ldx #>XITVBV
        jmp SETVBV
.endproc

.proc dli
        pha
        txa
        pha
        ldx dli_idx
        lda _row_hs,x
        sta WSYNC
        sta HSCROL
        lda _row_bk,x           ; background and COLPF2 first: the first line of
        sta COLBK               ; an ANTIC 2 row (the strips, the status line)
        lda _row_pf2,x          ; loses many cycles to ANTIC's fetches, and the
        sta COLPF2              ; lanes' tiles are blank on their first lines
        lda _row_pf1,x
        sta COLPF1
        lda _row_pf0,x
        sta COLPF0
        lda _row_pf3,x
        sta COLPF3
        inc dli_idx
        pla
        tax
        pla
        rti
.endproc

.proc vbi
        lda #0
        sta dli_idx
        lda _pend_ready
        beq done
        lda _dl_base
        sta ptr
        lda _dl_base+1
        sta ptr+1
        ldx #NLMS-1
@lms:   ldy _dl_off,x
        lda _pend_lo,x
        sta (ptr),y
        iny
        lda _pend_hi,x
        sta (ptr),y
        dex
        bpl @lms
        lda _pend_hpos
        sta $D000
        sta $D001
        ldx #NROWS-1
@hs:    lda _pend_hs,x
        sta _row_hs,x
        dex
        bpl @hs
        lda #0
        sta _pend_ready
done:   jmp XITVBV
.endproc

        .zeropage
ptr:    .res 2
