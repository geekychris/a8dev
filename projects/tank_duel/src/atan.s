
; uint16_t __fastcall__ td_atan2_(void): the angle of (at_x, at_z) from +z
; towards +x, 1/65536 turn (td_fix.c's, without its division: the larger of
; |x|, |z| is brought into 64..127, then t = smaller * 128 / larger by a
; reciprocal table, then atan(t / 128) from a table)
        .export _td_atan2_, _at_x, _at_z

.segment "BSS"
_at_x:  .res 2
_at_z:  .res 2
aax:    .res 2
aaz:    .res 2
lo_:    .res 2
hi_:    .res 2
swap:   .res 1
tt:     .res 2

.segment "RODATA"
rlo:    .byte $00, $F8, $F0, $E9, $E2, $DB, $D4, $CE, $C7, $C1, $BB, $B5, $AF, $AA, $A4, $9F, $9A, $95, $90, $8B, $86, $82, $7D, $79, $74, $70, $6C, $68, $64, $60, $5D, $59, $55, $52, $4E, $4B, $48, $44, $41, $3E, $3B, $38, $35, $32, $2F, $2D, $2A, $27, $25, $22, $1F, $1D, $1A, $18, $16, $13, $11, $0F, $0D, $0A, $08, $06, $04, $02
rhi:    .byte $02, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01, $01
atlo:   .byte $00, $51, $A3, $F4, $46, $97, $E9, $3A, $8B, $DC, $2D, $7E, $CF, $20, $70, $C1, $11, $61, $B1, $01, $51, $A0, $EF, $3E, $8D, $DC, $2A, $78, $C6, $14, $61, $AE, $FB, $48, $94, $E0, $2C, $77, $C2, $0D, $57, $A1, $EB, $34, $7D, $C6, $0F, $56, $9E, $E5, $2C, $73, $B9, $FF, $44, $89, $CE, $12, $56, $99, $DC, $1F, $61, $A3, $E4, $25, $66, $A6, $E6, $25, $64, $A2, $E0, $1E, $5B, $98, $D5, $11, $4C, $88, $C2, $FD, $37, $70, $AA, $E2, $1B, $53, $8A, $C1, $F8, $2E, $64, $9A, $CF, $04, $38, $6C, $9F, $D3, $05, $38, $6A, $9C, $CD, $FE, $2E, $5E, $8E, $BE, $ED, $1B, $4A, $78, $A5, $D3, $FF, $2C, $58, $84, $B0, $DB, $06, $30, $5A, $84, $AE, $D7, $00
athi:   .byte $00, $00, $00, $00, $01, $01, $01, $02, $02, $02, $03, $03, $03, $04, $04, $04, $05, $05, $05, $06, $06, $06, $06, $07, $07, $07, $08, $08, $08, $09, $09, $09, $09, $0A, $0A, $0A, $0B, $0B, $0B, $0C, $0C, $0C, $0C, $0D, $0D, $0D, $0E, $0E, $0E, $0E, $0F, $0F, $0F, $0F, $10, $10, $10, $11, $11, $11, $11, $12, $12, $12, $12, $13, $13, $13, $13, $14, $14, $14, $14, $15, $15, $15, $15, $16, $16, $16, $16, $16, $17, $17, $17, $17, $18, $18, $18, $18, $18, $19, $19, $19, $19, $1A, $1A, $1A, $1A, $1A, $1B, $1B, $1B, $1B, $1B, $1B, $1C, $1C, $1C, $1C, $1C, $1D, $1D, $1D, $1D, $1D, $1D, $1E, $1E, $1E, $1E, $1E, $1F, $1F, $1F, $1F, $1F, $1F, $20

.segment "CODE"
_td_atan2_:
        ; |x|, |z|
        lda     _at_x+1
        bpl     :+
        sec
        lda     #0
        sbc     _at_x
        sta     aax
        lda     #0
        sbc     _at_x+1
        sta     aax+1
        jmp     @z
:       sta     aax+1
        lda     _at_x
        sta     aax
@z:     lda     _at_z+1
        bpl     :+
        sec
        lda     #0
        sbc     _at_z
        sta     aaz
        lda     #0
        sbc     _at_z+1
        sta     aaz+1
        jmp     @ord
:       sta     aaz+1
        lda     _at_z
        sta     aaz
@ord:   lda     aax
        ora     aax+1
        ora     aaz
        ora     aaz+1
        bne     :+
        tax                     ; (0, 0): 0
        rts
:       ; lo = min, hi = max; swap = |x| > |z|
        lda     #0
        sta     swap
        lda     aaz             ; ax <= az ?
        cmp     aax
        lda     aaz+1
        sbc     aax+1
        bcs     @xz             ; az >= ax: lo = ax, hi = az
        inc     swap
        lda     aaz
        sta     lo_
        lda     aaz+1
        sta     lo_+1
        lda     aax
        sta     hi_
        lda     aax+1
        sta     hi_+1
        jmp     @norm
@xz:    lda     aax
        sta     lo_
        lda     aax+1
        sta     lo_+1
        lda     aaz
        sta     hi_
        lda     aaz+1
        sta     hi_+1
@norm:  ; hi into 64..127: shift both down while hi >= 128, up while < 64
@dn:    lda     hi_+1
        bne     @shr
        lda     hi_
        bpl     @up
@shr:   lsr     hi_+1
        ror     hi_
        lsr     lo_+1
        ror     lo_
        jmp     @dn
@up:    lda     hi_
        cmp     #64
        bcs     @ok
        asl     hi_
        asl     lo_
        jmp     @up
@ok:    ; t = lo * r[hi - 64] >> 8   (lo <= 127, r <= 512): shift-add
        lda     hi_
        sec
        sbc     #64
        tay
        lda     rlo,y
        sta     m0
        lda     rhi,y
        sta     m1
        lda     #0
        sta     m2
        sta     p0
        sta     p1
        sta     p2
        ldx     #7
@m:     lsr     lo_
        bcc     @n
        clc
        lda     p0
        adc     m0
        sta     p0
        lda     p1
        adc     m1
        sta     p1
        lda     p2
        adc     m2
        sta     p2
@n:     asl     m0
        rol     m1
        rol     m2
        dex
        bne     @m
        ; t = (p1, p2) clamped to 128
        ldy     p1
        lda     p2
        beq     :+
        ldy     #128
:       cpy     #129
        bcc     :+
        ldy     #128
:       lda     atlo,y
        sta     tt
        lda     athi,y
        sta     tt+1
        ; |x| > |z|: 16384 - a
        lda     swap
        beq     :+
        sec
        lda     #<16384
        sbc     tt
        sta     tt
        lda     #>16384
        sbc     tt+1
        sta     tt+1
:       ; z < 0: 32768 - a
        lda     _at_z+1
        bpl     :+
        sec
        lda     #0
        sbc     tt
        sta     tt
        lda     #$80
        sbc     tt+1
        sta     tt+1
:       ; x < 0: -a
        lda     _at_x+1
        bpl     :+
        sec
        lda     #0
        sbc     tt
        sta     tt
        lda     #0
        sbc     tt+1
        sta     tt+1
:       lda     tt
        ldx     tt+1
        rts

.segment "BSS"
m0:     .res 1
m1:     .res 1
m2:     .res 1
p0:     .res 1
p1:     .res 1
p2:     .res 1
