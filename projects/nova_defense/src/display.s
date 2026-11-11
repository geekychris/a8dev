; Nova Defense - display interrupts.
;
; Each display list (two, built in turn by render.c and flipped in the
; vertical blank, so ANTIC never sees half of one) comes with a table of
; what its display-list interrupts set, one entry an interrupt, in order:
; HSCROL, CHBASE, COLPF0, COLPF1, COLPF3. The comet's row, the swarm's
; rows (body and accent colours per alien type) and the status lines each
; get their own.

        .export _display_install, _dli_set, _dl_pending
        .export _t_hs, _t_chb, _t_pf0, _t_pf1, _t_pf3
        .export _vb_ufo_lms, _vb_ufo_hs, _vb_sw_lms, _vb_sw_hs, _vb_chb, _vb_ready
        .export _lay_n, _lay_off, _lay_ufo, _lay_dufo, _lay_dsw, _lay_dn
        .export _sw_lo, _sw_hi

WSYNC   = $D40A
HSCROL  = $D404
CHBASE  = $D409
COLPF0  = $D016
COLPF1  = $D017
COLPF3  = $D019
NMIEN   = $D40E
DLISTH  = $D403
SDLSTH  = $0231
VDSLST  = $0200
SETVBV  = $E45C
XITVBV  = $E462

NT      = 16                    ; entries a table (two tables: 0..15, 16..31)

        .bss
_t_hs:  .res NT * 2
_t_chb: .res NT * 2
_t_pf0: .res NT * 2
_t_pf1: .res NT * 2
_t_pf3: .res NT * 2
_dl_pending: .res 1             ; high byte of the display list to show next, 0 = none
_dli_set: .res 1                ; table (0 / NT) for that list
dli_base: .res 1                ; the table now shown
dli_idx: .res 1
; what changes every frame (render.c sets them, vb_ready = 1 when done)
_vb_ufo_lms: .res 1
_vb_ufo_hs:  .res 1
_vb_sw_lms:  .res 1
_vb_sw_hs:   .res 1
_vb_chb:     .res 1
_vb_ready:   .res 1
; where it goes, per display list (set 0 = $AE00, 1 = $AF00): the swarm
; rows' LMS operands (offsets), the comet row's (0 = a message there), the
; DLI entries with the comet's / the swarm's scroll and charset
_lay_n:   .res 2
_lay_off: .res 24
_lay_ufo: .res 2
_lay_dufo: .res 2               ; $FF = none
_lay_dsw: .res 6
_lay_dn:  .res 2
_sw_lo:  .res 12
_sw_hi:  .res 12
cur:    .res 1                  ; the set shown

        .zeropage
dlp:    .res 2

        .code

.proc _display_install
        lda #0
        sta NMIEN
        lda #<dli
        sta VDSLST
        lda #>dli
        sta VDSLST+1
        lda #7
        ldy #<vbi
        ldx #>vbi
        jsr SETVBV
        lda #$C0
        sta NMIEN
        rts
.endproc

.proc dli
        pha
        txa
        pha
        ldx dli_idx
        lda _t_hs,x
        sta WSYNC
        sta HSCROL
        lda _t_chb,x
        sta CHBASE
        lda _t_pf0,x
        sta COLPF0
        lda _t_pf1,x
        sta COLPF1
        lda _t_pf3,x
        sta COLPF3
        inc dli_idx
        pla
        tax
        pla
        rti
.endproc

.proc vbi
        lda _dl_pending
        beq @same
        sta SDLSTH              ; the OS has copied the shadow already:
        sta DLISTH              ; the chip too (the low byte is always 0)
        and #1                  ; $AE -> set 0, $AF -> set 1
        sta cur
        lda _dli_set
        sta dli_base
        lda #0
        sta _dl_pending
@same:  lda dli_base
        sta dli_idx
        lda _vb_ready
        bne patch
        jmp XITVBV
patch:  ; the shown list: $AE00 + 256 * cur
        lda SDLSTH
        and #1
        sta cur
        lda #0
        sta dlp
        lda cur
        clc
        adc #$AE
        sta dlp+1
        ldx cur
        ; the comet row's LMS, its DLI entry
        ldy _lay_ufo,x
        beq @noufo
        lda _vb_ufo_lms
        clc
        adc #<UFO_ROW
        sta (dlp),y
        iny
        lda #>UFO_ROW
        adc #0
        sta (dlp),y
        ldy _lay_dufo,x
        bmi @noufo
        lda _vb_ufo_hs
        sta _t_hs,y
        lda _vb_chb
        sta _t_chb,y
@noufo: ; the swarm rows' LMS
        lda _lay_n,x
        beq @nosw
        sta cnt
        txa                     ; offsets of set x: _lay_off + 12 * x
        beq @s0
        lda #12
@s0:    sta oi
        lda #0
        sta k
@row:   ldx oi
        ldy _lay_off,x
        ldx k
        lda _sw_lo,x
        clc
        adc _vb_sw_lms
        sta (dlp),y
        iny
        lda _sw_hi,x
        adc #0
        sta (dlp),y
        inc oi
        inc k
        dec cnt
        bne @row
@nosw:  ; the swarm's DLI entries
        ldx cur
        lda _lay_dn,x
        beq @done
        sta cnt
        txa
        beq @d0
        lda #3
@d0:    tax
@dl:    ldy _lay_dsw,x
        lda _vb_sw_hs
        sta _t_hs,y
        lda _vb_chb
        sta _t_chb,y
        inx
        dec cnt
        bne @dl
@done:  jmp XITVBV
.endproc

UFO_ROW = $AD40

        .bss
cnt:    .res 1
oi:     .res 1
k:      .res 1

; unsigned char __fastcall__ row_bits(const unsigned char *p): a shield
; row's 32 pixels -> 8 bits, a bit set when any of its 4 pixels is left
        .export _row_bits
        .zeropage
rp:     .res 2
        .bss
rv:     .res 1
        .code
.proc _row_bits
        sta rp
        stx rp+1
        lda #0
        sta rv
        ldy #0
@g:     lda (rp),y
        iny
        ora (rp),y
        iny
        ora (rp),y
        iny
        ora (rp),y
        iny
        cmp #1                  ; C = any pixel
        rol rv
        cpy #32
        bne @g
        lda rv
        ldx #0
        rts
.endproc

; ---------------------------------------------------------------- the list
; void build_dl(void): the game's display list for a swarm whose top is at
; scan line bd_top (>= 38) and a message or not (bd_msg), into the list not
; shown (set bd_set: 0 = $AE00, 1 = $AF00), with its DLI entries in table
; bd_set * 16 and the places the vertical blank patches (lay_*). From the
; top: 8 blank, the status line (DLI), 4 blank, the comet's row (or a
; message line), blank lines, the swarm's rows (12, or as many as fit
; above 240; DLIs where the alien type changes), blank lines, the bottom
; status line. C in render.c (build_game_dl_c, kept for reference).
        .export _build_dl, _bd_top, _bd_msg, _bd_set
        .import _type_body, _type_accent

HUD_MEM = $ADC0
MSG_MEM = $ADE8
C_COMET = $26
C_BULLET = $4E
C_TEXT  = $0E

        .bss
_bd_top: .res 1
_bd_msg: .res 1
_bd_set: .res 1
nrows:  .res 1
line:   .res 1
kk:     .res 1
tbase:  .res 1                  ; this set's first table entry
ent:    .res 1                  ; the next entry
        .zeropage
op:     .res 2

        .code
; one byte to the list
.macro PUT v
        lda v
        sta (op),y
        iny
.endmacro

; a DLI table entry: hs, chb, pf0, pf1, pf3 (immediates or memory)
.macro ENTRY hs, chb, p0, p1, p3
        ldx ent
        lda hs
        sta _t_hs,x
        lda chb
        sta _t_chb,x
        lda p0
        sta _t_pf0,x
        lda p1
        sta _t_pf1,x
        lda p3
        sta _t_pf3,x
        inc ent
.endmacro

.proc _build_dl
        lda #0
        sta op
        lda _bd_set
        clc
        adc #$AE
        sta op+1
        lda _bd_set
        asl a
        asl a
        asl a
        asl a
        sta tbase
        sta ent
        ldx _bd_set
        lda #0
        sta _lay_dn,x
        ldy #0
        PUT #$70                ; 8-15
        PUT #$C6                ; 16-23: the status line, DLI
        PUT #<HUD_MEM
        PUT #>HUD_MEM
        PUT #$30                ; 24-27
        lda _bd_msg
        beq comet
        PUT #$46                ; 28-35: a message
        PUT #<MSG_MEM
        PUT #>MSG_MEM
        ldx _bd_set
        lda #0
        sta _lay_ufo,x
        lda #$FF
        sta _lay_dufo,x
        ENTRY #0, #$E0, #C_COMET, #$0E, #C_TEXT
        jmp swarm
comet:  PUT #$54                ; ... or the comet's row (LMS patched)
        PUT #0
        PUT #0
        ldx _bd_set
        lda #6
        sta _lay_ufo,x
        lda ent
        sta _lay_dufo,x
        ENTRY #0, #$A0, #C_COMET, #$0E, #C_BULLET
swarm:  ; rows that fit: (240 - top) / 8, at most 12
        lda #240
        sec
        sbc _bd_top
        lsr a
        lsr a
        lsr a
        cmp #12
        bcc @n
        lda #12
@n:     sta nrows
        ldx _bd_set
        sta _lay_n,x
        lda #36
        sta line
        lda nrows
        bne @rows
        jmp below
@rows:  lda _bd_top             ; blank lines above it, DLI on the last
        sec
        sbc #36
        jsr blanks
        jsr swarm_entry
        lda _bd_top
        sta line
        lda #0
        sta kk
row:    ; last row before the status line?
        lda kk
        clc
        adc #1
        cmp nrows
        bne @mid
        lda line
        cmp #232
        bne @mid
        PUT #$D4
        ENTRY #0, #$E0, #C_TEXT, #C_TEXT, #C_TEXT
        jmp @lms
@mid:   lda kk                  ; rows 3 and 7 (with a row after): next type
        cmp #3
        beq @ty
        cmp #7
        bne @plain
@ty:    lda kk
        clc
        adc #1
        cmp nrows
        bcs @plain
        PUT #$D4
        jsr swarm_entry
        jmp @lms
@plain: PUT #$54
@lms:   tya                     ; where its LMS is
        pha
        lda _bd_set             ; lay_off[set * 12 + k]
        beq @o0
        lda #12
@o0:    clc
        adc kk
        tax
        pla
        sta _lay_off,x
        ldx kk
        PUT {_sw_lo,x}
        PUT {_sw_hi,x}
        lda line
        clc
        adc #8
        sta line
        inc kk
        lda kk
        cmp nrows
        beq below
        jmp row
below:  lda line
        cmp #240
        bcs status
        lda #240
        sec
        sbc line
        jsr blanks
        ENTRY #0, #$E0, #C_TEXT, #C_TEXT, #C_TEXT
status: PUT #$46                ; 240-247
        PUT #<(HUD_MEM + 20)
        PUT #>(HUD_MEM + 20)
        PUT #$41
        PUT #0
        PUT op+1
        rts

; A blank lines (>= 1): 8 at a time, the rest in one, DLI on the last
blanks: tax
@l:     cpx #9
        bcc @r
        lda #$70
        sta (op),y
        iny
        txa
        sec
        sbc #8
        tax
        jmp @l
@r:     dex
        txa
        asl a
        asl a
        asl a
        asl a
        ora #$80
        sta (op),y
        iny
        rts

; a DLI entry for the swarm (its type's colours: the n-th swarm entry), and
; its index in lay_dsw[set * 3 + n]
swarm_entry:
        sty ysave
        ldx _bd_set
        lda _lay_dn,x
        sta tn
        inc _lay_dn,x
        txa                     ; set * 3 + n
        asl a
        adc _bd_set
        adc tn
        tax
        lda ent
        sta _lay_dsw,x
        ldx tn
        lda _type_body,x
        sta tb
        lda _type_accent,x
        sta ta
        ENTRY #0, #$A0, tb, ta, #C_BULLET
        ldy ysave
        rts
.endproc

        .bss
ysave:  .res 1
tn:     .res 1
tb:     .res 1
ta:     .res 1

; unsigned char __fastcall__ clear_span(unsigned char n): zero n bytes at
; span_ptr; returns 1 if any of them was not zero (the shield changed)
        .export _clear_span, _span_ptr
        .zeropage
_span_ptr: .res 2
        .bss
any:    .res 1
        .code
.proc _clear_span
        tax
        beq @none
        lda #0
        sta any
        ldy #0
@l:     lda (_span_ptr),y
        ora any
        sta any
        lda #0
        sta (_span_ptr),y
        iny
        dex
        bne @l
        lda any
        beq @none
        lda #1
@none:  ldx #0
        rts
.endproc

; unsigned char __fastcall__ clear_rect(unsigned char h): zero a rectangle
; of rect_w bytes x h rows (rows 32 bytes apart) from span_ptr; 1 if any
; byte was not zero
        .export _clear_rect, _rect_w
        .bss
_rect_w: .res 1
rh:     .res 1
        .code
.proc _clear_rect
        sta rh
        lda #0
        sta any
@row:   ldy _rect_w
        beq @next
        dey
@l:     lda (_span_ptr),y
        ora any
        sta any
        lda #0
        sta (_span_ptr),y
        dey
        bpl @l
@next:  clc
        lda _span_ptr
        adc #32
        sta _span_ptr
        bcc @c
        inc _span_ptr+1
@c:     dec rh
        bne @row
        lda any
        beq @z
        lda #1
@z:     ldx #0
        rts
.endproc
