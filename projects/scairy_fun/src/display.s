; ScAIry Fun - display interrupts.
;
; Every DLI sets the ground (COLBK), road (COLPF0), lines (COLPF1) and water
; (COLPF2) colours for the rows below it from the next entry of a table:
; the sky's gradient, then one entry per band of road (the stripes, the
; flood, the overpass shadow), then the text rows' colours. The renderer
; makes a display list and a table for the next frame (two of each) and
; sets dl_next; the vertical blank switches to them, so a frame never mixes
; two. (The OS ROM is off: os.s's NMI handler calls dli via VDSLST and vbi
; via VVBLKD.)

        .export _display_install, _dl_next, _dl_base_next, _dlt_bk, _dlt_p0, _dlt_p1, _dlt_p2
        .export _vbi_count
        .import _os_vbi_exit

WSYNC   = $D40A
COLPF0  = $D016
COLPF1  = $D017
COLPF2  = $D018
COLBK   = $D01A
NMIEN   = $D40E
VDSLST  = $0200
SDLSTL  = $0230
VVBLKD  = $0224
DLISTL  = $D402

TABN    = 32                    ; entries a table (two tables: 0 and 32)

        .bss
_dl_next:      .res 2           ; a display list to show from the next frame (0: none)
_dl_base_next: .res 1           ; ... and its table (0 or TABN)
_dlt_bk:  .res TABN * 2
_dlt_p0:  .res TABN * 2
_dlt_p1:  .res TABN * 2
_dlt_p2:  .res TABN * 2
idx:      .res 1
base:     .res 1
_vbi_count: .res 1

        .code
.proc _display_install
        lda #0
        sta NMIEN
        sta idx
        sta base
        sta _dl_next
        sta _dl_next+1
        lda #<dli
        sta VDSLST
        lda #>dli
        sta VDSLST+1
        lda #<vbi               ; (os.s: the vertical blank's last step)
        sta VVBLKD
        lda #>vbi
        sta VVBLKD+1
        lda #$C0
        sta NMIEN
        rts
.endproc

.proc dli
        pha
        txa
        pha
        tya
        pha
        ldy idx
        inc idx
        lda _dlt_p1,y
        pha
        lda _dlt_bk,y
        ldx _dlt_p0,y
        sta WSYNC
        sta COLBK
        stx COLPF0
        pla
        sta COLPF1
        lda _dlt_p2,y
        sta COLPF2
        pla
        tay
        pla
        tax
        pla
        rti
.endproc

.proc vbi
        inc _vbi_count
        lda _dl_next+1
        beq same
        sta SDLSTL+1            ; (os.s copied the shadow already: ANTIC too)
        sta DLISTL+1
        lda _dl_next
        sta SDLSTL
        sta DLISTL
        lda _dl_base_next
        sta base
        lda #0
        sta _dl_next+1
same:   lda base
        sta idx
        jmp _os_vbi_exit
.endproc
