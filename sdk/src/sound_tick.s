; a8game.h: sound_tick() in asm, and the sound state it shares with sound.c.
; One byte array per field (no structs); a whole tick with nothing playing is
; one test (snd_active = 0). Called once per frame by frame_wait().
        .export         _sound_tick, _snd_active
        .export         _trk_on, _trk_start_lo, _trk_start_hi, _trk_pos_lo, _trk_pos_hi
        .export         _trk_left, _trk_vol
        .export         _sfx_on, _sfx_freq, _sfx_dist, _sfx_vol, _sfx_decay, _sfx_sweep
        .import         _snd_note_freq
        .importzp       ptr1, tmp1, tmp2

AUDF1   = $D200
AUDC1   = $D201

.bss
_snd_active:    .res 1          ; bit n = channel n has a track or an effect
_trk_on:        .res 4
_trk_start_lo:  .res 4
_trk_start_hi:  .res 4
_trk_pos_lo:    .res 4
_trk_pos_hi:    .res 4
_trk_left:      .res 4
_trk_vol:       .res 4
_sfx_on:        .res 4
_sfx_freq:      .res 4
_sfx_dist:      .res 4
_sfx_vol:       .res 4
_sfx_decay:     .res 4
_sfx_sweep:     .res 4

.code
_sound_tick:
        lda     _snd_active
        bne     :+
        rts                             ; nothing playing: done
:       ldx     #3
chan:   lda     _trk_on,x
        beq     sfx
        lda     _trk_left,x
        bne     dec_left
        ; next note: ptr1 = pos; a {_, 0} entry loops to the start
        lda     _trk_pos_lo,x
        sta     ptr1
        lda     _trk_pos_hi,x
        sta     ptr1+1
        ldy     #1
        lda     (ptr1),y
        bne     have
        lda     _trk_start_lo,x
        sta     ptr1
        lda     _trk_start_hi,x
        sta     ptr1+1
        lda     (ptr1),y
have:   sta     _trk_left,x
        lda     _sfx_on,x
        bne     skipnote                ; an effect owns the channel
        ldy     #0
        lda     (ptr1),y                ; MIDI note
        jsr     play_note
skipnote:
        clc
        lda     ptr1
        adc     #2
        sta     _trk_pos_lo,x
        lda     ptr1+1
        adc     #0
        sta     _trk_pos_hi,x
dec_left:
        dec     _trk_left,x
sfx:    lda     _sfx_on,x
        beq     next
        txa
        asl
        tay                             ; Y = 2 * channel (AUDFn offset)
        lda     _sfx_vol,x
        cmp     _sfx_decay,x
        beq     sfx_end
        bcc     sfx_end                 ; vol <= decay: the effect is over
        sbc     _sfx_decay,x            ; (carry set)
        sta     _sfx_vol,x
        lda     _sfx_freq,x
        clc
        adc     _sfx_sweep,x
        sta     _sfx_freq,x
        sta     AUDF1,y
        lda     _sfx_vol,x
        ora     _sfx_dist,x
        sta     AUDC1,y
        jmp     next
sfx_end:
        lda     #0
        sta     _sfx_on,x
        sta     AUDC1,y
        lda     _trk_on,x
        bne     next
        lda     bit_clr,x
        and     _snd_active
        sta     _snd_active
next:   dex
        bmi     done
        jmp     chan
done:   rts

; A = MIDI note, X = channel: AUDF from the table, pure tone at trk_vol (48..107, else silent)
play_note:
        sta     tmp1
        txa
        asl
        tay
        lda     tmp1
        sec
        sbc     #48
        bcc     @off
        cmp     #60
        bcs     @off
        stx     tmp2
        tax
        lda     _snd_note_freq,x
        sta     AUDF1,y
        ldx     tmp2
        lda     _trk_vol,x
        and     #15
        ora     #$A0
        sta     AUDC1,y
        rts
@off:   lda     #0
        sta     AUDC1,y
        rts

.rodata
bit_clr: .byte  $FE, $FD, $FB, $F7
