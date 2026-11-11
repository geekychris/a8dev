; Rock Blaster - sound, played by the deferred vertical-blank interrupt:
; two music channels (POKEY 3 and 4: the original's lead and bass) and two
; effect channels (1 and 2) with a falling volume and a pitch sweep. In the
; interrupt, the music keeps time even when the game loop runs late. An effect is
; set by writing fx_freq/dist/decay/sweep[ch] and then fx_vol[ch] (0 = off).

        .export _snd_start, _snd_music, _snd_silence
        .export _fx_freq, _fx_dist, _fx_vol, _fx_decay, _fx_sweep
        .import _snd_note_freq

AUDF1   = $D200
AUDC1   = $D201
AUDCTL  = $D208
SKCTL   = $D20F
SETVBV  = $E45C
XITVBV  = $E462

        .bss
_fx_freq:  .res 2
_fx_dist:  .res 2
_fx_vol:   .res 2               ; 0 = off
_fx_decay: .res 2
_fx_sweep: .res 2
mu_ptr:    .res 4               ; per music channel: the next note
mu_start:  .res 4
mu_left:   .res 2
mu_vol:    .res 2
mu_on:     .res 1

        .zeropage
np:     .res 2

        .code

; void snd_start(void): POKEY on, the player in the deferred VBI
.proc _snd_start
        lda #0
        sta AUDCTL
        sta mu_on
        sta _fx_vol
        sta _fx_vol+1
        lda #3
        sta SKCTL
        lda #7
        ldy #<vbi
        ldx #>vbi
        jmp SETVBV
.endproc

; void __fastcall__ snd_music(unsigned char on): the tune (music.c) from
; the start, or off
.proc _snd_music
        sta mu_on
        cmp #0
        bne @go
        sta AUDC1+4
        sta AUDC1+6
        rts
@go:    lda #0
        sta mu_on               ; off while it is set up
        lda #<_music_lead
        sta mu_ptr
        sta mu_start
        lda #>_music_lead
        sta mu_ptr+1
        sta mu_start+1
        lda #<_music_bass
        sta mu_ptr+2
        sta mu_start+2
        lda #>_music_bass
        sta mu_ptr+3
        sta mu_start+3
        lda #0
        sta mu_left
        sta mu_left+1
        lda #6
        sta mu_vol
        lda #7
        sta mu_vol+1
        lda #1
        sta mu_on
        rts
.endproc
        .import _music_lead, _music_bass

.proc _snd_silence
        lda #0
        sta mu_on
        sta _fx_vol
        sta _fx_vol+1
        sta AUDC1
        sta AUDC1+2
        sta AUDC1+4
        sta AUDC1+6
        rts
.endproc

.proc vbi
        ; effects on channels 1, 2
        ldx #1
fx:     lda _fx_vol,x
        beq fxoff
        txa
        asl a
        tay                     ; Y = 2 * ch: AUDF / AUDC offset
        lda _fx_freq,x
        sta AUDF1,y
        lda _fx_vol,x
        ora _fx_dist,x
        sta AUDC1,y
        sec                     ; volume falls by decay
        lda _fx_vol,x
        sbc _fx_decay,x
        bcs @v
        lda #0
@v:     sta _fx_vol,x
        clc
        lda _fx_freq,x
        adc _fx_sweep,x
        sta _fx_freq,x
        jmp fxnext
fxoff:  txa
        asl a
        tay
        lda #0
        sta AUDC1,y
fxnext: dex
        bpl fx
        ; music on channels 3, 4
        lda mu_on
        beq done
        ldx #1
mu:     lda mu_left,x
        bne @dec
        txa
        asl a
        tay
        lda mu_ptr,y
        sta np
        lda mu_ptr+1,y
        sta np+1
        ldy #1
        lda (np),y              ; len 0: back to the start
        bne @note
        txa
        asl a
        tay
        lda mu_start,y
        sta mu_ptr,y
        sta np
        lda mu_start+1,y
        sta mu_ptr+1,y
        sta np+1
        ldy #1
        lda (np),y
@note:  sta mu_left,x
        dey
        lda (np),y              ; MIDI note, 0 = rest
        beq @rest
        sec
        sbc #48
        tay
        lda _snd_note_freq,y
        pha
        txa
        asl a
        tay
        pla
        sta AUDF1+4,y
        lda mu_vol,x
        ora #$A0
        sta AUDC1+4,y
        jmp @adv
@rest:  txa
        asl a
        tay
        lda #0
        sta AUDC1+4,y
@adv:   txa                     ; ptr += 2
        asl a
        tay
        clc
        lda mu_ptr,y
        adc #2
        sta mu_ptr,y
        lda mu_ptr+1,y
        adc #0
        sta mu_ptr+1,y
@dec:   dec mu_left,x
        dex
        bpl mu
done:   jmp XITVBV
.endproc
