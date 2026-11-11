; ScAIry Fun - projecting the road's segment boundaries (render.c's loop,
; in asm: in C it took ~90,000 cycles a frame).
;
; For the boundaries ahead (every segment to 8 out, every second to 16,
; every fourth to 32): the distance zr from the camera, the scale (ktab8:
; k / 2 = 45 * 256 / zr), and in 12.4 pixels the centre line (relative to
; the camera's line of sight, which drifts with its heading), the lane
; width, the ghost lines' and the path's offsets; the row; the segment's
; mode and side colours (rows.s's inputs, bd_*).

        .export _project, _ktab8, _bseg
        .export _pj_s0, _pj_nb, _pj_segmax, _pj_lfrac, _pj_three, _pj_pathok, _pj_planfrom, _pj_plan
        .import _mulk, _mult, _mk
        .import _seg_cx, _seg_y, _seg_flags, _seg_ghost, _seg_side
        .import _cam_z, _cam_x, _cam_y, _cam_dir16
        .import _bd_c, _bd_lw, _bd_g, _bd_p, _bd_row, _bd_mode, _bd_side, _bd_fw, _bd_n
        .importzp ptr1

HOR     = 14
NB      = 20
SF_FLOOD = $01
SF_GHOST = $02
M_GHOST = $02
M_PATH  = $04
M_NOLINE = $08

.segment "HIABSS"
_ktab8: .res 513

.segment "SCRBSS"
_bseg:      .res NB * 2
_pj_s0:     .res 2              ; the camera's segment
_pj_nb:     .res 1              ; the first boundary to make (boundary 0: render.c)
_pj_segmax: .res 2              ; nseg - 1
_pj_lfrac:  .res 2              ; the heading's drift already behind (1/16 units)
_pj_three:  .res 1              ; M_THREE or 0
_pj_pathok: .res 1              ; the path is shown (autopilot on, not blinking)
_pj_planfrom: .res 2
_pj_plan:   .res 2              ; the plan's address (16 int16, fine units)
i:      .res 1
nb:     .res 1
nb2:    .res 1
seg:    .res 2
zr:     .res 2
k8:     .res 1
xr:     .res 2
fl:     .res 1
pi:     .res 2
mode:   .res 1
t:      .res 2

.segment "HIA"
side_near: .byte 0, 0, 0, 1, 0, 2, 0, 0
side_far:  .byte 0, 3, 0, 1, 0, 0, 1, 3
side_fw:   .byte 0, 3, 0, 0, 0, 1, 2, 2

; ptr1 = base + seg (bytes) / + seg * 2 (words)
.macro BYTEAT base
        clc
        lda #<base
        adc seg
        sta ptr1
        lda #>base
        adc seg+1
        sta ptr1+1
.endmacro
.macro WORDAT base
        lda seg
        asl a
        sta ptr1
        lda seg+1
        rol a
        sta ptr1+1
        clc
        lda ptr1
        adc #<base
        sta ptr1
        lda ptr1+1
        adc #>base
        sta ptr1+1
.endmacro

.proc _project
        lda #1
        sta i
        lda _pj_nb
        sta nb
loop:   ; seg = s0 + i; past the end: done
        clc
        lda _pj_s0
        adc i
        sta seg
        lda _pj_s0+1
        adc #0
        sta seg+1
        lda seg
        cmp _pj_segmax
        lda seg+1
        sbc _pj_segmax+1
        bcc :+
        jmp done
:       ; zr = (seg << 7) - cam_z
        lda seg+1
        lsr a
        lda seg
        ror a                   ; (seg << 7) high byte = seg >> 1 (low 8 bits)
        sta t+1
        lda #0
        ror a
        sta t                   ; low byte = (seg & 1) << 7
        sec
        lda t
        sbc _cam_z
        sta zr
        lda t+1
        sbc _cam_z+1
        sta zr+1
        ; zr <= 48: too near
        bmi near
        bne far
        lda zr
        cmp #49
        bcs far
near:   jmp nexti
far:    ; k8 = ktab8[zr >> 3]
        lda zr+1
        sta t+1
        lda zr
        lsr t+1
        ror a
        lsr t+1
        ror a
        lsr t+1
        ror a
        clc
        adc #<_ktab8
        sta ptr1
        lda t+1
        adc #>_ktab8
        sta ptr1+1
        ldy #0
        lda (ptr1),y
        sta k8
        ; line16 = dir16 * i - lfrac
        lda i
        sta _mk
        lda _cam_dir16
        ldx _cam_dir16+1
        jsr _mult
        sec
        sbc _pj_lfrac
        sta t
        txa
        sbc _pj_lfrac+1
        ; >> 4 (arithmetic)
        cmp #$80
        ror a
        ror t
        cmp #$80
        ror a
        ror t
        cmp #$80
        ror a
        ror t
        cmp #$80
        ror a
        ror t
        sta t+1
        ; xr = seg_cx[seg] - cam_x - line
        WORDAT _seg_cx
        ldy #0
        sec
        lda (ptr1),y
        sbc _cam_x
        sta xr
        iny
        lda (ptr1),y
        sbc _cam_x+1
        sta xr+1
        sec
        lda xr
        sbc t
        sta xr
        lda xr+1
        sbc t+1
        sta xr+1
        ; centre: 1280 + mulk(xr)
        lda k8
        sta _mk
        lda nb
        asl a
        sta nb2
        lda xr
        ldx xr+1
        jsr _mulk
        ldy nb2
        clc
        adc #<1280
        sta _bd_c,y
        txa
        adc #>1280
        sta _bd_c+1,y
        ; the row: HOR + (mulk(cam_y - seg_y) >> 4)
        WORDAT _seg_y
        ldy #0
        sec
        lda _cam_y
        sbc (ptr1),y
        pha
        iny
        lda _cam_y+1
        sbc (ptr1),y
        tax
        pla
        jsr _mulk
        sta t
        txa
        cmp #$80
        ror a
        ror t
        cmp #$80
        ror a
        ror t
        cmp #$80
        ror a
        ror t
        cmp #$80
        ror a
        ror t
        sta t+1
        ldy nb2
        clc
        lda t
        adc #HOR
        sta _bd_row,y
        lda t+1
        adc #0
        sta _bd_row+1,y
        ; the lane width: 56 * k / 16 = 7 * k8
        lda #0
        sta t+1
        lda k8
        asl a
        rol t+1
        asl a
        rol t+1
        asl a
        rol t+1                 ; k8 * 8
        sec
        sbc k8
        sta _bd_lw,y
        lda t+1
        sbc #0
        sta _bd_lw+1,y
        lda seg
        sta _bseg,y
        lda seg+1
        sta _bseg+1,y
        ; the segment's flags: ghost lines, water
        BYTEAT _seg_flags
        ldy #0
        lda (ptr1),y
        sta fl
        lda _pj_three
        sta mode
        lda #0
        ldy nb2
        sta _bd_g,y
        sta _bd_g+1,y
        sta _bd_p,y
        sta _bd_p+1,y
        lda fl
        and #SF_FLOOD
        beq :+
        lda mode
        ora #M_NOLINE
        sta mode
:       lda fl
        and #SF_GHOST
        beq noghost
        lda mode
        ora #(M_GHOST | M_NOLINE)
        sta mode
        BYTEAT _seg_ghost
        ldy #0
        lda (ptr1),y
        ldx #0
        jsr _mulk
        ldy nb2
        sta _bd_g,y
        txa
        sta _bd_g+1,y
noghost:
        ; the path: plan[seg - plan_from] >> 4, if 0 <= that < 16
        lda _pj_pathok
        beq nopath
        sec
        lda seg
        sbc _pj_planfrom
        sta pi
        lda seg+1
        sbc _pj_planfrom+1
        bne nopath
        lda pi
        cmp #16
        bcs nopath
        cmp #12
        bcs :+
        lda mode
        ora #M_PATH
        sta mode
:       lda pi
        asl a
        clc
        adc _pj_plan
        sta ptr1
        lda _pj_plan+1
        adc #0
        sta ptr1+1
        ldy #1
        lda (ptr1),y
        sta t+1
        dey
        lda (ptr1),y
        ; >> 4 arithmetic
        lsr t+1
        ror a
        lsr t+1
        ror a
        lsr t+1
        ror a
        lsr t+1
        ror a
        sta t
        lda t+1
        and #$08                ; (sign of the 12-bit result)
        beq :+
        lda t+1
        ora #$F0
        sta t+1
:       lda t
        ldx t+1
        jsr _mulk
        ldy nb2
        sta _bd_p,y
        txa
        sta _bd_p+1,y
nopath: ldx nb
        lda mode
        sta _bd_mode,x
        ; what lies beside: colours far/near left, near/far right, widths
        BYTEAT _seg_side
        ldy #0
        lda (ptr1),y
        pha
        and #15
        tay
        lda nb
        asl a
        asl a
        tax
        lda side_far,y
        sta _bd_side,x
        lda side_near,y
        sta _bd_side+1,x
        lda side_fw,y
        pha
        ldx nb2
        pla
        sta _bd_fw,x
        pla
        lsr a
        lsr a
        lsr a
        lsr a
        tay
        lda nb
        asl a
        asl a
        tax
        lda side_near,y
        sta _bd_side+2,x
        lda side_far,y
        sta _bd_side+3,x
        ldx nb2
        lda side_fw,y
        sta _bd_fw+1,x
        ; next boundary
        inc nb
        lda nb
        cmp #NB
        beq done
nexti:  lda i
        cmp #8
        bcc s1
        cmp #16
        bcc s2
        clc
        adc #4
        jmp sset
s2:     clc
        adc #2
        jmp sset
s1:     clc
        adc #1
sset:   sta i
        cmp #33
        bcs done
        jmp loop
done:   lda nb
        sta _bd_n
        rts
.endproc
