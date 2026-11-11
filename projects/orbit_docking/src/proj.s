; proj.s - the station's corners on the screen. Each corner is (plane,
; radius, angle a) (tools/gen_view.py); the camera only rolls, so a corner
; is at
;     x = pl_cx[plane] - cr_x[radius] * cos(a + phi) / 128
;     y = pl_cy[plane] - cr_y[radius] * sin(a + phi) / 128
; where phi is the station's turn less the craft's roll (256 a turn),
; cr_x/cr_y the radius scaled to pixels / lines on its plane and pl_cx/cy
; where the axis crosses the plane (view.c works those out each frame).
; Two products a corner, each two 8 x 8 from a3d's quarter-square tables.
        .importzp a3d_m, a3d_p, a3d_bsel
        .import   _ell_w, _a3d_ylo, _a3d_yhi, _a3d_pat_e, _a3d_pat_o
        .import   _a3d_sin8, _mv_a, _mv_plane, _cls_v0, _cls_half, _cls_plane, mulset_u
        .export   _pl_phi, _pj_ncls, _pl_cx, _pl_cy, _cr_x, _cr_y, _pv_x, _pv_y, _proj_all
        .include  "a3d_mul.inc"

LINES = 88
NPL   = 12                              ; room for the planes (view_tab.h NPLANES)
NCL   = 24                              ; and the radius classes (NCLS)

.segment "LOWRAM"
_pl_phi: .res NPL
_pj_ncls: .res 1
cls:    .res 1
phi:    .res 1
vend:   .res 1
vv:     .res 1
aofs:   .res 1
bl:     .res 1
bh:     .res 1
_pl_cx: .res 2 * NPL
_pl_cy: .res 2 * NPL
_cr_x:  .res 2 * NCL
_cr_y:  .res 2 * NCL
_pv_x:  .res 216
_pv_y:  .res 216
vi:     .res 1                          ; the corner * 2
pp:     .res 1                          ; its plane * 2
cc:     .res 1                          ; its radius * 2
ang:    .res 1
sg:     .res 1
p0:     .res 1
p1:     .res 1
p2:     .res 1
rl:     .res 1
rh:     .res 1
hh:     .res 1                          ; corners mirrored (cls_half)
vm:     .res 1
b2l:    .res 1
b2h:    .res 1

.segment "ZEROPAGE"
dst:    .res 2

.segment "CODE"
; (rh:rl) * (signed Q7 in A) / 128 -> A (low), X (high)
m16s8:  sta     sg
        cmp     #$80
        bcc     :+
        eor     #$FF
        adc     #0                      ; carry set: + 1
:       jsr     mulset_u
        ldy     rl
        MUL                             ; X = low, A = high
        stx     p0
        sta     p1
        ldy     rh
        MUL
        sta     p2
        txa
        clc
        adc     p1
        sta     p1
        bcc     :+
        inc     p2
:       asl     p0                      ; (p2:p1:p0) >> 7 = (p2:p1:p0) << 1 >> 8
        rol     p1
        rol     p2
        lda     sg
        bmi     :+
        lda     p1
        ldx     p2
        rts
:       lda     #0                      ; negative
        sec
        sbc     p1
        tay
        lda     #0
        sbc     p2
        tax
        tya
        rts

; void proj_all(void): class by class (the corners of a class share a plane
; and a radius); a radius under 256 pixels sets the multiplier once for
; the class and costs one product a corner, a bigger one two (m16s8)
_proj_all:
        lda     #0
        sta     cls
@cls:   ldx     cls
        ldy     _cls_plane,x
        lda     _pl_on,y
        bne     :+
        jmp     @nextc                  ; the plane is behind us: its polygons aren't drawn
:       lda     _pl_phi,y
        sta     phi
        tya
        asl     a
        sta     pp
        lda     _cls_half,x             ; pairs half a turn apart: the first
        sta     hh                      ; half worked out, the rest mirrored
        beq     :+
        clc
        adc     _cls_v0,x
        bne     :++
:       lda     _cls_v0+1,x
:       sta     vend
        txa
        asl     a
        sta     cc
        ; ---- x: pl_cx - r cos
        lda     #64
        sta     aofs
        ldy     cc
        lda     _cr_x,y
        sta     rl
        lda     _cr_x+1,y
        sta     rh
        lda     #<_pv_x
        sta     dst
        lda     #>_pv_x
        sta     dst+1
        ldy     pp
        lda     _pl_cx,y
        sta     bl
        lda     _pl_cx+1,y
        sta     bh
        jsr     axis
        jsr     mirror
        ; ---- y: pl_cy - r sin
        lda     #0
        sta     aofs
        ldy     cc
        lda     _cr_y,y
        sta     rl
        lda     _cr_y+1,y
        sta     rh
        lda     #<_pv_y
        sta     dst
        lda     #>_pv_y
        sta     dst+1
        ldy     pp
        lda     _pl_cy,y
        sta     bl
        lda     _pl_cy+1,y
        sta     bh
        jsr     axis
        jsr     mirror
@nextc: inc     cls
        lda     cls
        cmp     _pj_ncls
        bcs     :+
        jmp     @cls
:       rts

; the mirrored half: dst[v + hh] = 2 (bh:bl) - dst[v] (the corner half a turn round)
mirror: lda     hh
        beq     @done
        lda     bl
        asl     a
        sta     b2l
        lda     bh
        rol     a
        sta     b2h
        ldx     cls
        lda     _cls_v0,x
        sta     vm
@m:     lda     vm
        asl     a
        tay
        sec
        lda     b2l
        sbc     (dst),y
        tax
        iny
        lda     b2h
        sbc     (dst),y
        pha
        lda     vm                      ; the partner
        clc
        adc     hh
        asl     a
        tay
        txa
        sta     (dst),y
        iny
        pla
        sta     (dst),y
        inc     vm
        lda     vm
        cmp     vend
        bcc     @m
@done:  rts

; the class's corners on one axis: dst[v] = (bh:bl) - (rh:rl) * sin(a + aofs) / 128
axis:   ldx     cls
        lda     _cls_v0,x
        sta     vv
        lda     rh
        bne     @big
        lda     rl
        jsr     mulset_u                ; the radius: once for the class
@small: ldx     vv
        lda     _mv_a,x
        clc
        adc     phi
        clc
        adc     aofs
        tax
        lda     _a3d_sin8,x
        sta     sg
        bpl     :+
        eor     #$FF
        clc
        adc     #1
:       tay
        MUL                             ; X = low, A = high: r * |s|
        stx     p0
        asl     p0
        rol     a                       ; (r * |s|) >> 7, < 256
        bit     sg
        bmi     @plus                   ; base - (-v) = base + v
        sta     p1
        lda     bl
        sec
        sbc     p1
        tay
        lda     bh
        sbc     #0
        jmp     @st
@plus:  clc
        adc     bl
        tay
        lda     bh
        adc     #0
@st:    tax
        lda     vv
        asl     a
        sty     p1
        tay
        lda     p1
        sta     (dst),y
        iny
        txa
        sta     (dst),y
        inc     vv
        lda     vv
        cmp     vend
        bcc     @small
        rts
@big:   ldx     vv                      ; a radius of 256 or more: two products a corner
        lda     _mv_a,x
        clc
        adc     phi
        clc
        adc     aofs
        tay
        lda     _a3d_sin8,y
        jsr     m16s8
        sta     p1
        stx     p2
        lda     bl
        sec
        sbc     p1
        pha
        lda     bh
        sbc     p2
        tax
        lda     vv
        asl     a
        tay
        pla
        sta     (dst),y
        iny
        txa
        sta     (dst),y
        inc     vv
        lda     vv
        cmp     vend
        bcc     @big
        rts

; void rot_stars(void): the stars (star_r, star_a: polar round the middle of
; the view) turned by -st_roll into star_sx/star_sy (0xFF: off the screen),
; y at 7/8 for mode D's taller pixels
        .import   _star_r, _star_a
        .export   _rot_stars, _st_roll

.segment "LOWRAM"
_st_roll: .res 1
sx_:    .res 1

.segment "CODE"
; (A unsigned) * (signed Q7 sine at Y) / 128 -> A (signed, |r| < 255 so it fits)
rmul:   pha
        lda     _a3d_sin8,y
        sta     sg
        bpl     :+
        eor     #$FF
        clc
        adc     #1
:       jsr     mulset_u
        pla
        tay
        MUL                             ; X = low, A = high: (r * |s|) >> 7
        stx     p0
        asl     p0
        rol     a
        bit     sg
        bpl     :+
        eor     #$FF
        clc
        adc     #1
:       rts

_rot_stars:
        ldx     _star_n
        dex
@s:     stx     sk
        lda     _star_a,x
        sec
        sbc     _st_roll
        sta     ang
        clc
        adc     #64                     ; cos
        tay
        lda     _star_r,x
        jsr     rmul
        clc
        adc     #80
        sta     sx_
        cmp     #160
        bcs     @off                    ; also catches x < 0 (wrapped): radius <= 120
        ldx     sk
        ldy     ang
        lda     _star_r,x
        jsr     rmul
        sta     p1                      ; y = 44 - v * 7/8
        cmp     #$80
        ror     a
        cmp     #$80
        ror     a
        cmp     #$80
        ror     a                       ; v >> 3 (signed)
        sta     p2
        lda     p1
        sec
        sbc     p2
        sta     p1
        lda     #44
        sec
        sbc     p1
        cmp     #LINES
        bcs     @off
        ldx     sk
        sta     _star_sy,x
        lda     sx_
        sta     _star_sx,x
        jmp     @next
@off:   ldx     sk
        lda     #$FF
        sta     _star_sx,x
@next:  ldx     sk
        dex
        bpl     @s
        rts

; void plot_stars(void): star_sx/star_sy (0xFF: off the screen) into the back
; buffer, ORed in: every fourth in light grey, the rest mid grey
        .import   _star_sx, _star_sy
        .export   _plot_stars, _star_n

.segment "LOWRAM"
_star_n: .res 1
sk:     .res 1

.segment "RODATA"
smask:  .byte   $40, $10, $04, $01      ; colour 1 at each pixel of a byte

.segment "CODE"
_plot_stars:
        ldx     _star_n
        beq     @done
        dex
@s:     stx     sk
        lda     _star_sx,x
        cmp     #$FF
        beq     @next
        pha
        ldy     _star_sy,x
        lda     _a3d_ylo,y
        sta     a3d_p
        lda     _a3d_yhi,y
        ora     a3d_bsel
        sta     a3d_p+1
        pla
        pha
        and     #3
        tay
        lda     smask,y
        sta     sm
        txa
        and     #3
        bne     :+
        asl     sm                      ; every fourth: colour 2
:       pla
        lsr     a
        lsr     a
        tay
        lda     (a3d_p),y
        ora     sm
        sta     (a3d_p),y
@next:  ldx     sk
        dex
        bpl     @s
@done:  rts

.segment "LOWRAM"
sm:     .res 1

; void draw_polys(void): the station's polygons far to near: each a count
; and its corners (mp_list); skipped when its level of detail isn't dp_lod
; (mp_lod), a corner's plane is too near (pl_on), or it's all off one side
; of the screen (its corners' outcodes); else gathered into a3d_px/py and
; filled by a3d_poly with its pattern (mp_pat -> pats)
        .import   _a3d_px, _a3d_py, _a3d_poly
        .import   _mp_list, _mp_lod, _mp_pat, _pats, _pl_on, mulset_s
        .export   _draw_polys, _dp_lod, _dp_npolys, _dp_first

.segment "ZEROPAGE"
lp:     .res 2

.segment "LOWRAM"
_dp_lod: .res 1
_dp_npolys: .res 1
_dp_first: .res 1
pk:     .res 1                          ; the polygon
pn:     .res 1                          ; its corners
pj:     .res 1
oca:    .res 1                          ; the outcodes ANDed
oc:     .res 1
bad:    .res 1
lps:    .res 2                          ; lp, kept across a3d_poly

.segment "CODE"
_draw_polys:
        lda     #<_mp_list
        sta     lps
        lda     #>_mp_list
        sta     lps+1
        lda     #0
        sta     pk
@first: lda     pk                      ; skip to dp_first
        cmp     _dp_first
        bcs     @poly
        lda     lps
        sta     lp
        lda     lps+1
        sta     lp+1
        ldy     #0
        lda     (lp),y
        sec
        adc     lps
        sta     lps
        bcc     :+
        inc     lps+1
:       inc     pk
        jmp     @first
@poly:  lda     lps
        sta     lp
        lda     lps+1
        sta     lp+1
        ldy     #0
        lda     (lp),y
        sta     pn
        inc     lp                      ; lp -> the corners
        bne     :+
        inc     lp+1
:       ldx     pk
        ldy     _mp_lod,x
        lda     lodbit,y
        and     _dp_lod                 ; the levels of detail drawn (a bit each)
        bne     @use
        jmp     @skip
@use:   lda     #$0F
        sta     oca
        lda     #0
        sta     bad
        sta     pj
@corner:
        ldy     pj
        lda     (lp),y                  ; v
        tax
        lda     _mv_plane,x
        tay
        lda     _pl_on,y
        bne     :+
        inc     bad
:       txa
        asl     a
        tax                             ; v * 2
        lda     pj
        asl     a
        tay                             ; j * 2
        lda     #0
        sta     oc
        lda     _pv_x,x
        sta     _a3d_px,y
        lda     _pv_x+1,x
        sta     _a3d_px+1,y
        bpl     :+
        lda     #1                      ; x < 0
        bne     @ocx
:       bne     @right                  ; x >= 256
        lda     _pv_x,x
        cmp     #160
        bcc     @ocy
@right: lda     #2
@ocx:   sta     oc
@ocy:   lda     _pv_y,x
        sta     _a3d_py,y
        lda     _pv_y+1,x
        sta     _a3d_py+1,y
        bpl     :+
        lda     #4                      ; y < 0
        bne     @ocy2
:       bne     @below
        lda     _pv_y,x
        cmp     #LINES
        bcc     @occ
@below: lda     #8
@ocy2:  ora     oc
        sta     oc
@occ:   lda     oca
        and     oc
        sta     oca
        inc     pj
        lda     pj
        cmp     pn
        bcc     @corner
        lda     bad
        ora     oca
        bne     @skip
        jsr     front                   ; facing away (or edge-on): not drawn
        beq     @skip
        ldx     pk                      ; the pattern
        lda     _mp_pat,x
        asl     a
        tax
        lda     _pats,x
        sta     _a3d_pat_e
        lda     _pats+1,x
        sta     _a3d_pat_o
        lda     pn
        jsr     _a3d_poly
@skip:  lda     lps                     ; the next: lp += n + 1
        sec
        adc     pn
        sta     lps
        bcc     :+
        inc     lps+1
:       inc     pk
        lda     pk
        cmp     _dp_npolys
        bcs     :+
        jmp     @poly
:       rts

; A = 1 (Z clear) if a3d_px/py 0, 1, 2 turn clockwise on the screen (the
; front), 0 if anticlockwise or in a line: the cross product of the first
; two edges, the deltas halved until they fit 8 bits
MINAREA = 12
front:  ldx     #0
        lda     _a3d_px+2
        sec
        sbc     _a3d_px
        sta     dd
        lda     _a3d_px+3
        sbc     _a3d_px+1
        sta     dd+1
        lda     _a3d_py+2
        sec
        sbc     _a3d_py
        sta     dd+2
        lda     _a3d_py+3
        sbc     _a3d_py+1
        sta     dd+3
        lda     _a3d_px+4
        sec
        sbc     _a3d_px+2
        sta     dd+4
        lda     _a3d_px+5
        sbc     _a3d_px+3
        sta     dd+5
        lda     _a3d_py+4
        sec
        sbc     _a3d_py+2
        sta     dd+6
        lda     _a3d_py+5
        sbc     _a3d_py+3
        sta     dd+7
@fit:   ldx     #6                      ; all within -128..127?
@chk:   lda     dd,x
        asl     a                       ; bit 7 of the low byte into carry
        lda     dd+1,x
        adc     #0                      ; high byte + bit 7: 0 if it fits
        bne     @half
        dex
        dex
        bpl     @chk
        ; d1x * d2y - d1y * d2x
        lda     dd
        jsr     mulset_s
        lda     dd+6
        eor     #$80
        tay
        MUL
        stx     cr
        sta     cr+1
        lda     dd+2
        jsr     mulset_s
        lda     dd+4
        eor     #$80
        tay
        MUL                             ; X low, A high
        stx     cr+2
        sta     cr+3
        lda     cr                      ; cr - that: twice the first triangle's area
        sec
        sbc     cr+2
        sta     cr
        lda     cr+1
        sbc     cr+3
        bvs     @ovf
        bmi     @no                     ; facing away
        bne     @yes
        lda     cr
        cmp     #MINAREA                ; port: slivers under 6 pixels (an arm's
        bcc     @no                     ; side seen edge-on) aren't drawn
@yes:   lda     #1
        rts
@ovf:   bmi     @yes                    ; (overflowed: the true sign is the other)
@no:    lda     #0
        rts
@half:  ldx     #6                      ; all four halved
:       lda     dd+1,x
        cmp     #$80
        ror     dd+1,x
        ror     dd,x
        dex
        dex
        bpl     :-
        jmp     @fit

.segment "LOWRAM"
dd:     .res 8
cr:     .res 4

.segment "RODATA"
lodbit: .byte   1, 2, 4, 8

; void ring_draw(void): the station's ring, an elliptical annulus round
; (rg_cx, rg_cy): outer half sizes rg_ox (pixels) x rg_oy (lines), inner
; rg_ix x rg_iy, filled a line at a time in a3d's pattern. The half widths
; for each line from the middle (ell_w: 255 sqrt(1 - t^2)) are worked out
; only when the sizes change (they don't while the craft rolls or waits),
; into rw_o / rw_i (255: wider than the screen; rw_i 0xFF.. none: past the
; hole). rg_so / rg_si = 65536 / rg_oy / rg_iy step down ell_w.
; port: the original's 16 quads, alternately shaded; turned, it looks the same
        .export   _rg_cx, _rg_cy, _rg_ox, _rg_oy, _rg_ix, _rg_iy, _rg_so, _rg_si, _ring_draw

RWN = 128                               ; lines from the middle kept

.segment "LOWRAM"
_rg_cx: .res 2
_rg_cy: .res 2
_rg_ox: .res 2
_rg_oy: .res 2
_rg_ix: .res 2
_rg_iy: .res 2
_rg_so: .res 2
_rg_si: .res 2
rkey:   .res 8                          ; the sizes rw_o/rw_i were made for
rw_o:   .res RWN
rw_i:   .res RWN
rwn:    .res 1                          ; lines in rw_o
ao:     .res 2
ai:     .res 2
rdy:    .res 1
ly:     .res 2
xa:     .res 2
xb:     .res 2
wtmp:   .res 2

.segment "CODE"
; (A:X = r, 16 bits) * ell_w[Y] / 256 -> A (low), X (high)
wmul:   sta     rl
        stx     rh
        lda     _ell_w,y
        jsr     mulset_u
        ldy     rl
        MUL                             ; X low, A high
        sta     p1
        ldy     rh
        MUL
        stx     p2
        sta     p0
        lda     p1
        clc
        adc     p2
        pha
        lda     p0
        adc     #0
        tax
        pla
        rts

; the half widths, one a line out from the middle, while the line is inside
widths: lda     #0
        sta     ao
        sta     ao+1
        sta     ai
        sta     ai+1
        sta     rdy
@w:     ldy     ao+1
        lda     _rg_ox
        ldx     _rg_ox+1
        jsr     wmul
        cpx     #0
        beq     :+
        lda     #255
:       ldx     rdy
        sta     rw_o,x
        lda     ai+1                    ; inside the hole? (ai stops at $FFFF)
        cmp     #$FF
        bne     :+
        lda     ai
        cmp     #$FF
        beq     @nohole
:       ldy     ai+1
        lda     _rg_ix
        ldx     _rg_ix+1
        jsr     wmul
        cpx     #0
        beq     :+
        lda     #254
:       ldx     rdy
        sta     rw_i,x
        lda     ai
        clc
        adc     _rg_si
        sta     ai
        lda     ai+1
        adc     _rg_si+1
        sta     ai+1
        bcc     @next
        lda     #$FF
        sta     ai
        sta     ai+1
        bne     @next
@nohole:
        ldx     rdy
        lda     #$FF
        sta     rw_i,x
@next:  inc     rdy
        lda     rdy
        cmp     #RWN
        bcs     @done
        lda     ao
        clc
        adc     _rg_so
        sta     ao
        lda     ao+1
        adc     _rg_so+1
        sta     ao+1
        bcs     @done
        jmp     @w
@done:  lda     rdy
        sta     rwn
        rts

; one span x0 = xa .. x1 = xb (exclusive), clipped to 0..160, on line ly
span:   lda     xb+1
        bmi     @no
        bne     :+
        lda     xb
        beq     @no
        cmp     #161
        bcc     :++
:       lda     #160
        sta     xb
        lda     #0
        sta     xb+1
:       lda     xa+1
        bmi     @zero
        bne     @no
        lda     xa
        cmp     #160
        bcs     @no
        bcc     @set
@zero:  lda     #0
@set:   cmp     xb
        bcs     @no
        jmp     hspan                   ; A = x0, xb = x1
@no:    rts

; the lines cy - dy and cy + dy (the same spans; la_ok / lb_ok: on the
; screen): one span cx - wo .. cx + wo + 1, or two round the hole
line:   lda     _rg_cx
        sec
        sbc     wtmp
        sta     xa
        lda     _rg_cx+1
        sbc     #0
        sta     xa+1
        ldx     rdy
        lda     rw_i,x
        cmp     #$FF
        beq     @one
        lda     _rg_cx                  ; left: .. cx - wi
        sec
        sbc     rw_i,x
        sta     xb
        lda     _rg_cx+1
        sbc     #0
        sta     xb+1
        jsr     span
        ldx     rdy                     ; right: cx + wi + 1 ..
        lda     _rg_cx
        sec
        adc     rw_i,x
        sta     xa
        lda     _rg_cx+1
        adc     #0
        sta     xa+1
@one:   lda     _rg_cx                  ; .. cx + wo + 1
        sec
        adc     wtmp
        sta     xb
        lda     _rg_cx+1
        adc     #0
        sta     xb+1
        jmp     span

; is line (A low, X high) on the screen? carry clear if it is
onscr:  cpx     #0
        bne     @no
        cmp     #LINES
        rts
@no:    sec
        rts

_ring_draw:
        ldx     #7                      ; the same sizes as last time?
:       lda     _rg_ox,x
        cmp     rkey,x
        bne     @new
        dex
        bpl     :-
        bmi     @draw
@new:   ldx     #7
:       lda     _rg_ox,x
        sta     rkey,x
        dex
        bpl     :-
        jsr     widths
@draw:  lda     #0
        sta     rdy
@dy:    ldx     rdy
        lda     rw_o,x
        sta     wtmp
        lda     #0                      ; the two lines: cy - dy, cy + dy
        sta     la_ok
        sta     lb_ok
        lda     _rg_cy
        sec
        sbc     rdy
        sta     la
        lda     _rg_cy+1
        sbc     #0
        tax
        lda     la
        jsr     onscr
        rol     la_ok                   ; 0 = on the screen
        lda     rdy
        beq     @nob                    ; dy = 0: one line
        lda     _rg_cy
        clc
        adc     rdy
        sta     lb
        lda     _rg_cy+1
        adc     #0
        tax
        lda     lb
        jsr     onscr
        rol     lb_ok
        jmp     :+
@nob:   inc     lb_ok
:       lda     la_ok
        and     lb_ok
        bne     :+                      ; neither: nothing to draw
        jsr     line
:       inc     rdy
        lda     rdy
        cmp     rwn
        bcc     @dy
        rts

; hspan: pixels A .. xb-1 on lines la (if la_ok = 0) and lb (if lb_ok = 0)
; of the back buffer in a3d's pattern (the two lines have the same parity)
hspan:  sta     hx0
        tax
        lsr     a
        lsr     a
        sta     hb0
        txa
        and     #3
        tax
        lda     hsetl,x
        sta     hml
        ldx     xb
        dex
        txa
        lsr     a
        lsr     a
        sta     hb1
        txa
        and     #3
        tax
        lda     hsetr,x
        sta     hmr
        lda     hb0
        cmp     hb1
        bne     :+
        lda     hml                     ; one byte
        and     hmr
        sta     hml
:       lda     la_ok
        bne     :+
        ldy     la
        jsr     hline
:       lda     lb_ok
        bne     :+
        ldy     lb
        jmp     hline
:       rts

; the span on line Y
hline:  lda     _a3d_ylo,y
        sta     a3d_p
        lda     _a3d_yhi,y
        ora     a3d_bsel
        sta     a3d_p+1
        tya
        lsr     a
        lda     _a3d_pat_e
        bcc     :+
        lda     _a3d_pat_o
:       sta     hpat
        ldy     hb0
        lda     (a3d_p),y
        eor     hpat
        and     hml
        eor     (a3d_p),y
        sta     (a3d_p),y
        cpy     hb1
        beq     hl_done
        iny
        lda     hpat
@mid:   cpy     hb1
        beq     hl_last
        sta     (a3d_p),y
        iny
        bne     @mid
hl_last:  lda     (a3d_p),y
        eor     hpat
        and     hmr
        eor     (a3d_p),y
        sta     (a3d_p),y
hl_done:  rts

.segment "RODATA"
hsetl:  .byte   $FF, $3F, $0F, $03
hsetr:  .byte   $C0, $F0, $FC, $FF

.segment "LOWRAM"
hx0:    .res 1
hpat:   .res 1
hml:    .res 1
hmr:    .res 1
hb0:    .res 1
hb1:    .res 1
la:     .res 1
lb:     .res 1
la_ok:  .res 1
lb_ok:  .res 1

; void proj_setup(void): each plane's scale and where the axis crosses it,
; each radius class scaled on its plane: for plane k at depth dz = ps_cz -
; plane_z[k] (too near: pl_on[k] = 0), pl_kx = 115 * 256 / dz, pl_ky = 98 *
; 256 / dz, pl_cx = 80 + ps_ox * pl_kx / 256, pl_cy = 44 - ps_oy * pl_ky /
; 256; cr_x[c] = cls_r[c] * pl_kx[plane] / 256, cr_y likewise.
; The scales from a reciprocal table (pk_tab: 115 * 65536 / d for d 128..255,
; dz normalised into that range) and the products by a3d's quarter squares:
; no divisions, no a3d_mulf
        .import   _plane_z, _cls_r
        .export   _proj_setup, _ps_cz, _ps_ox, _ps_oy, _ps_nplanes, _pl_kx, _pl_ky, _pk_tab
NEARZ = 12
.segment "LOWRAM"
_ps_cz: .res 2
_ps_ox: .res 2
_ps_oy: .res 2
_ps_nplanes: .res 1
_pl_kx: .res 2 * NPL
_pl_ky: .res 2 * NPL
_pk_tab: .res 256                       ; (view_init)
dden:   .res 2
pk2:    .res 1
um:     .res 2                          ; umul: um * uk >> 8 -> ur
uk:     .res 2
ur:     .res 2
usg:    .res 1
uth:    .res 1
.segment "CODE"
; ur = um * uk >> 8 (unsigned 16 x 16, the result in 16 bits)
umul:   lda     uk
        jsr     mulset_u
        ldy     um
        MUL
        sta     ur                      ; (m_lo k_lo) >> 8
        lda     #0
        sta     ur+1
        ldy     um+1
        beq     @k1
        MUL                             ; + m_hi k_lo
        sta     uth
        txa
        clc
        adc     ur
        sta     ur
        lda     uth
        adc     ur+1
        sta     ur+1
@k1:    lda     uk+1
        beq     @done
        jsr     mulset_u
        ldy     um
        MUL                             ; + m_lo k_hi
        sta     uth
        txa
        clc
        adc     ur
        sta     ur
        lda     uth
        adc     ur+1
        sta     ur+1
        ldy     um+1
        beq     @done
        MUL                             ; + (m_hi k_hi) << 8
        txa
        clc
        adc     ur+1
        sta     ur+1
@done:  rts

; the same, um signed (A:X), the result signed in ur
smul:   sta     um
        stx     um+1
        stx     usg
        txa
        bpl     :+
        sec
        lda     #0
        sbc     um
        sta     um
        lda     #0
        sbc     um+1
        sta     um+1
:       jsr     umul
        bit     usg
        bpl     :+
        sec
        lda     #0
        sbc     ur
        sta     ur
        lda     #0
        sbc     ur+1
        sta     ur+1
:       rts

_proj_setup:
        ldx     #0
@pl:    stx     pk2
        txa
        asl     a
        tay                             ; Y = k * 2
        lda     _ps_cz
        sec
        sbc     _plane_z,y
        sta     dden
        lda     _ps_cz+1
        sbc     _plane_z+1,y
        sta     dden+1
        bmi     @off                    ; dz < NEARZ: off
        bne     @on
        lda     dden
        cmp     #NEARZ
        bcs     @on
@off:   lda     #0
        sta     _pl_on,x
        jmp     @nextp
@on:    lda     #1
        sta     _pl_on,x
        ; kx = pk_tab[d] >> s: dz normalised to d in 128..255, s = 8 + the shift
        ldx     #8
        lda     dden+1
        beq     @lo
@rs:    lsr     dden+1                  ; dz > 255: down
        ror     dden
        inx
        lda     dden+1
        bne     @rs
        lda     dden
        bmi     @norm
@lo:    lda     dden                    ; dz < 128: up
@ls:    bmi     @norm
        dex
        asl     a
        jmp     @ls
@norm:  asl     a                       ; (d - 128) * 2
        tay
        lda     _pk_tab,y
        sta     ur
        lda     _pk_tab+1,y
@sh:    lsr     a
        ror     ur
        dex
        bne     @sh
        sta     ur+1
        lda     pk2
        asl     a
        tay
        lda     ur
        sta     _pl_kx,y
        sta     uk
        lda     ur+1
        sta     _pl_kx+1,y
        sta     uk+1
        lda     #<218                   ; ky = kx * 98 / 115 (218 / 256)
        sta     um
        lda     #0
        sta     um+1
        jsr     umul
        lda     pk2
        asl     a
        tay
        lda     ur
        sta     _pl_ky,y
        lda     ur+1
        sta     _pl_ky+1,y
        ; pl_cx = 80 + ox * kx / 256 (uk = kx still)
        lda     _ps_ox
        ldx     _ps_ox+1
        jsr     smul
        lda     pk2
        asl     a
        tay
        clc
        lda     ur
        adc     #80
        sta     _pl_cx,y
        lda     ur+1
        adc     #0
        sta     _pl_cx+1,y
        ; pl_cy = 44 - oy * ky / 256
        lda     _pl_ky,y
        sta     uk
        lda     _pl_ky+1,y
        sta     uk+1
        lda     _ps_oy
        ldx     _ps_oy+1
        jsr     smul
        lda     pk2
        asl     a
        tay
        sec
        lda     #44
        sbc     ur
        sta     _pl_cy,y
        lda     #0
        sbc     ur+1
        sta     _pl_cy+1,y
@nextp: ldx     pk2
        inx
        cpx     _ps_nplanes
        bcs     :+
        jmp     @pl
:       ; the classes
        ldx     #0
@cl:    stx     pk2
        ldy     _cls_plane,x
        lda     _pl_on,y
        beq     @nextc
        tya
        asl     a
        sta     pp
        txa
        asl     a
        sta     cc
        tay
        lda     _cls_r,y
        sta     um
        lda     _cls_r+1,y
        sta     um+1
        ldy     pp                      ; cr_x = r * kx / 256
        lda     _pl_kx,y
        sta     uk
        lda     _pl_kx+1,y
        sta     uk+1
        jsr     umul
        ldy     cc
        lda     ur
        sta     _cr_x,y
        lda     ur+1
        sta     _cr_x+1,y
        ldy     pp                      ; cr_y = r * ky / 256
        lda     _pl_ky,y
        sta     uk
        lda     _pl_ky+1,y
        sta     uk+1
        jsr     umul
        ldy     cc
        lda     ur
        sta     _cr_y,y
        lda     ur+1
        sta     _cr_y+1,y
@nextc: ldx     pk2
        inx
        cpx     _pj_ncls
        bcc     @cl
        rts
