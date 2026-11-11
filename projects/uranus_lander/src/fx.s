; Uranus Lander: the per-pixel work, in 6502.
;
; terrain_fill   the world's 176 lines from tline[] (the terrain's top line
;                in each pixel column): sky (0) above, the surface line (2),
;                a dotted highlight 2 lines down (2), the ground (1)
; stars_step     the original's star scroll (accum += speed, a pixel left
;                each 4) and their XORed points (colour 2): a star only shows
;                over open sky, and is only redrawn when its pixel changes
; particles_update  the original's sparks: x += vx, y += vy, vy += 600,
;                life-- (16.16, 32 of them)
; particles_draw XORed points (colour 3) at the sparks' rounded positions
; ply_put        a player: pp_n at HPOS pp_x, PM line pp_line, pp_h lines of
;                pp_shape; only rewritten when the line, height or key change
;                (pp_h = 0: hidden)
;
; The world: line L of the bitmap is at $A010 + 40 L; pixel x (0..159) is
; bits 7-6 (x & 3 = 0) .. 1-0 of byte x / 4.

        .export _terrain_fill, _stars_step, _particles_update, _particles_draw
        .export _tline, _st_px, _st_line, _pt_px, _pt_line
        .export _ply_put, _pp_n, _pp_x, _pp_line, _pp_h, _pp_key, _pp_shape
        .export _p_line, _p_h, _p_key
        .import _st_x, _st_speed, _st_accum
        .import _pt_x, _pt_y, _pt_vx, _pt_vy, _pt_life, _pt_on, _pt_live

BITMAP  = $A010
LINES   = 176
NSTARS  = 60
NPART   = 32
PLAYERS = $9C                   ; page of player 0

        .segment "ZEROPAGE"
ptr:    .res 2
src:    .res 2
v:      .res 1
tmp:    .res 1
tmp2:   .res 1
parity: .res 1
parity2: .res 1
cnt:    .res 1
lin:    .res 1
px:     .res 1
ix:     .res 2
iy:     .res 2

        .bss
_tline:   .res 160
bmin:     .res 40                ; the highest / lowest top in each byte
bmax:     .res 40
_st_px:   .res NSTARS
_st_line: .res NSTARS
_pt_px:   .res NPART
_pt_line: .res NPART
_pp_n:    .res 1
_pp_x:    .res 1
_pp_line: .res 1
_pp_h:    .res 1
_pp_key:  .res 1
_pp_shape: .res 2
_p_line:  .res 4
_p_h:     .res 4
_p_key:   .res 4

        .rodata
line_lo: .repeat LINES, l
        .byte <(BITMAP + l * 40)
        .endrepeat
line_hi: .repeat LINES, l
        .byte >(BITMAP + l * 40)
        .endrepeat
mask2:  .byte $80, $20, $08, $02        ; colour 2 at pixel x & 3
mask3:  .byte $C0, $30, $0C, $03        ; colour 3

        .code

; ---------------------------------------------------------------- terrain
; lines above the highest top are sky, those 3 below the lowest ground: only
; the lines between are worked out pixel by pixel
_terrain_fill:
        lda #$FF                ; v = min, cnt = max of tline
        sta v
        lda #0
        sta cnt
        ldx #159
:       lda _tline,x
        cmp v
        bcs :+
        sta v
:       cmp cnt
        bcc :+
        sta cnt
:       dex
        cpx #$FF
        bne :---
        lda cnt
        clc
        adc #3
        sta px                  ; first all-ground line
        ldx #0                  ; bmin / bmax + 3 of each byte's 4 pixels
        ldy #0
@mm:    lda _tline,x
        sta bmin,y
        sta bmax,y
        lda #3
        sta cnt
:       inx
        lda _tline,x
        cmp bmin,y
        bcs :+
        sta bmin,y
:       cmp bmax,y
        bcc :+
        sta bmax,y
:       dec cnt
        bne :---
        inx
        lda bmax,y
        clc
        adc #3
        sta bmax,y
        iny
        cpy #40
        bne @mm
        lda #0
        sta lin
@line:  ldx lin
        lda line_lo,x
        sta ptr
        lda line_hi,x
        sta ptr+1
        lda lin
        cmp v
        bcc @fill0              ; above the terrain: sky
        cmp px
        bcc @mixed
        lda #$55                ; under it: ground
        bne @fill
@fill0: lda #0
@fill:  ldy #39
:       sta (ptr),y
        dey
        bpl :-
        jmp @nextl
@mixed: ldx #0                  ; pixel
        ldy #0                  ; byte
@byte:  lda lin                 ; the whole byte sky or ground?
        cmp bmin,y
        bcs :+
        lda #0
        beq @whole
:       cmp bmax,y
        bcc @part
        lda #$55
@whole: sta (ptr),y
        inx
        inx
        inx
        inx
        jmp @nextb
@part:  lda #0
        sta tmp2
        lda #4
        sta cnt
@pix:   lda _tline,x
        cmp lin
        beq @surf               ; the surface line
        bcs @sky                ; above it
        clc                     ; below: the highlight 2 lines down, dotted
        adc #2
        cmp lin
        bne @body
        txa
        and #1
        bne @body
@surf:  lda #2
        bne @put
@body:  lda #1
        bne @put
@sky:   lda #0
@put:   sta tmp
        lda tmp2
        asl
        asl
        ora tmp
        sta tmp2
        inx
        dec cnt
        bne @pix
        lda tmp2
        sta (ptr),y
@nextb: iny
        cpy #40
        bne @byte
@nextl: inc lin
        lda lin
        cmp #LINES
        beq :+
        jmp @line
:
        rts

; XOR the pixel (px, A = line) with mask table at X+... : in: px, A = line,
; tmp = 0 for colour 2, 4 for colour 3 (the tables are 4 apart)
xorpix: tax
        lda line_lo,x
        sta ptr
        lda line_hi,x
        sta ptr+1
        lda px
        and #3
        clc
        adc tmp
        tax
        lda px
        lsr
        lsr
        tay
        lda (ptr),y
        eor mask2,x
        sta (ptr),y
        rts

; ---------------------------------------------------------------- stars
_stars_step:
        lda #0
        sta tmp                 ; colour 2
        inc parity              ; half the stars are redrawn a frame
        ldx #0
@star:  stx cnt
        lda _st_accum,x         ; accum += speed; a pixel left each 4
        clc
        adc _st_speed,x
        cmp #4
        bcc @nomove
        sbc #4
        sta _st_accum,x
        txa
        asl
        tay
        lda _st_x,y             ; x-- (wraps to 319)
        bne :+
        lda _st_x+1,y
        beq @wrap
        lda #0
:       sec
        lda _st_x,y
        sbc #1
        sta _st_x,y
        lda _st_x+1,y
        sbc #0
        sta _st_x+1,y
        jmp @moved
@wrap:  lda #<319
        sta _st_x,y
        lda #>319
        sta _st_x+1,y
        jmp @moved
@nomove:
        sta _st_accum,x
        lda _st_px,x            ; not moved: drawn where it should be
        cmp #$FF
        bne @next
        txa                     ; (not drawn: maybe it can be now)
        asl
        tay
@moved: txa                     ; (redrawn every other frame: odd / even)
        eor parity
        lsr
        bcc :+
        jmp @next
:       lda _st_x+1,y           ; px = x / 2
        lsr
        lda _st_x,y
        ror
        ldx cnt
        cmp _st_px,x
        beq @next
        sta v                   ; the new pixel
        lda _st_px,x            ; erase the old one
        cmp #$FF
        beq @draw
        sta px
        lda _st_line,x
        jsr xorpix
        ldx cnt
@draw:  lda #$FF
        sta _st_px,x
        lda _st_line,x
        cmp #$FF
        beq @next
        ldy v                   ; over open sky only: line + 1 < tline[px]
        clc
        adc #1
        cmp _tline,y
        bcs @next
        lda v
        sta _st_px,x
        sta px
        lda _st_line,x
        jsr xorpix
@next:  ldx cnt
        inx
        cpx #NSTARS
        bcs :+
        jmp @star
:
        rts

; ---------------------------------------------------------------- sparks
_particles_update:
        ldy #0                  ; byte index; X = 4 * index
@p:     lda _pt_on,y
        bne :+
        jmp @next
:
        tya
        asl
        asl
        tax
        clc                     ; x += vx
        lda _pt_x,x
        adc _pt_vx,x
        sta _pt_x,x
        lda _pt_x+1,x
        adc _pt_vx+1,x
        sta _pt_x+1,x
        lda _pt_x+2,x
        adc _pt_vx+2,x
        sta _pt_x+2,x
        lda _pt_x+3,x
        adc _pt_vx+3,x
        sta _pt_x+3,x
        clc                     ; y += vy
        lda _pt_y,x
        adc _pt_vy,x
        sta _pt_y,x
        lda _pt_y+1,x
        adc _pt_vy+1,x
        sta _pt_y+1,x
        lda _pt_y+2,x
        adc _pt_vy+2,x
        sta _pt_y+2,x
        lda _pt_y+3,x
        adc _pt_vy+3,x
        sta _pt_y+3,x
        clc                     ; vy += 600
        lda _pt_vy,x
        adc #<600
        sta _pt_vy,x
        lda _pt_vy+1,x
        adc #>600
        sta _pt_vy+1,x
        lda _pt_vy+2,x
        adc #0
        sta _pt_vy+2,x
        lda _pt_vy+3,x
        adc #0
        sta _pt_vy+3,x
        lda _pt_life,y          ; life--, off at 0
        sec
        sbc #1
        sta _pt_life,y
        bne @next
        lda #0
        sta _pt_on,y
        dec _pt_live
@next:  iny
        cpy #NPART
        bcs :+
        jmp @p
:
        rts

_particles_draw:
        lda #4
        sta tmp                 ; colour 3
        inc parity2             ; half of them are redrawn a frame
        ldy #0
@p:     sty cnt
        tya
        eor parity2
        lsr
        bcc :+
        jmp @next
:
        lda #$FF                ; where it should be: v = px, lin = line
        sta v
        lda _pt_on,y
        beq @cmp
        tya
        asl
        asl
        tax
        lda _pt_x+1,x           ; ix = round(x)
        clc
        adc #$80
        lda _pt_x+2,x
        adc #0
        sta ix
        lda _pt_x+3,x
        adc #0
        sta ix+1                ; x 0..319: high byte 0, or 1 with low < 64
        beq :+
        lda ix+1
        cmp #1
        bne @cmp
        lda ix
        cmp #64
        bcs @cmp
:
        lda _pt_y+1,x           ; iy = round(y)
        clc
        adc #$80
        lda _pt_y+2,x
        adc #0
        sta iy
        lda _pt_y+3,x
        adc #0
        sta iy+1
        bne @cmp                ; y < 0 or >= 256: off the world
        lda iy                  ; line = (y * 3) >> 2 - 8 (y 0..255)
        asl                     ; y * 2 in lin (low) and iy+1 (high, was 0)
        sta lin
        rol iy+1
        clc
        lda lin
        adc iy
        sta lin
        lda iy+1
        adc #0
        lsr                     ; >> 2
        ror lin
        lsr
        ror lin
        lda lin
        sec
        sbc #8
        bcc @cmp                ; above the world (under the top HUD)
        cmp #LINES
        bcs @cmp
        sta lin
        lda ix+1                ; px = x / 2
        lsr
        lda ix
        ror
        sta v
@cmp:   ldy cnt                 ; same place as drawn: nothing to do
        lda v
        cmp _pt_px,y
        bne @move
        cmp #$FF
        beq @next
        lda lin
        cmp _pt_line,y
        beq @next
@move:  lda _pt_px,y            ; erase the old point
        cmp #$FF
        beq @new
        sta px
        lda _pt_line,y
        jsr xorpix
        ldy cnt
@new:   lda v
        sta _pt_px,y
        cmp #$FF
        beq @next
        sta px
        lda lin
        sta _pt_line,y
        jsr xorpix
@next:  ldy cnt
        iny
        cpy #NPART
        bcs :+
        jmp @p
:
        rts

; ---------------------------------------------------------------- players
_ply_put:
        ldx _pp_n
        lda _pp_h
        beq @clear
        lda _pp_key
        cmp _p_key,x
        bne @redraw
        lda _pp_line
        cmp _p_line,x
        bne @redraw
        lda _pp_h
        cmp _p_h,x
        beq @pos
@redraw:
        jsr clr
        lda _pp_line
        sta _p_line,x
        sta ptr
        txa
        clc
        adc #PLAYERS
        sta ptr+1
        lda _pp_h
        sta _p_h,x
        sta cnt
        lda _pp_key
        sta _p_key,x
        lda _pp_shape
        sta src
        lda _pp_shape+1
        sta src+1
        ldy #0
:       lda (src),y
        sta (ptr),y
        iny
        cpy cnt
        bne :-
@pos:   lda _pp_x
        sta $D000,x
        rts
@clear: jsr clr
        lda #0
        sta $D000,x
        rts

clr:    lda _p_h,x
        beq @z
        sta cnt
        lda _p_line,x
        sta ptr
        txa
        clc
        adc #PLAYERS
        sta ptr+1
        ldy #0
        tya
:       sta (ptr),y
        iny
        dec cnt
        bne :-
@z:     lda #0
        sta _p_h,x
        sta _p_key,x
        rts
