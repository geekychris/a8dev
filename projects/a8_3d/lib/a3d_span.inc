; a3d_span.inc - shared by a3d_span.s and a3d_poly.s: zero-page scratch
; names and the span macros.
; zero page scratch (a3d_t)
pat     = a3d_t+0
lb      = a3d_t+1
rb      = a3d_t+2
nml     = a3d_t+3
nmr     = a3d_t+4
yy      = a3d_t+5
ymin    = a3d_t+6
ymax    = a3d_t+7
xf      = a3d_t+8
xl      = a3d_t+9
xh      = a3d_t+10
sl0     = a3d_t+11
sl1     = a3d_t+12
sl2     = a3d_t+13
; (front test)
dax     = a3d_t+0
day     = a3d_t+2
dbx     = a3d_t+4
dby     = a3d_t+6
dp1     = a3d_t+8


UNROLL  = 38

; the middle of a span: whole bytes lb+1..rb-1, then the right byte, then
; `next`. Y = lb + 1, pat, nmr, rb set; X = count (1..38)
.macro  SPAN_MIDDLE next
        .local  ent_lo, ent_hi, jv, unroll
.pushseg
.segment "RODATA"
ent_lo: .byte   0
        .repeat UNROLL, I
        .byte   <(unroll + (UNROLL - 1 - I) * 3)
        .endrepeat
ent_hi: .byte   0
        .repeat UNROLL, I
        .byte   >(unroll + (UNROLL - 1 - I) * 3)
        .endrepeat
.popseg
        lda     ent_lo,x
        sta     jv+1
        lda     ent_hi,x
        sta     jv+2
        lda     pat
jv:     jmp     unroll
unroll: .repeat UNROLL
        sta     (a3d_p),y
        iny
        .endrepeat
.endmacro

; a whole span: p, pat, lb, rb, nml, nmr set; then jmp `next`
.macro  SPAN    next
        .local  multi, mid, right
        ldy     lb
        lda     rb
        cmp     lb
        bne     multi
        lda     nml                 ; one byte
        ora     nmr
        sta     nml
        lda     (a3d_p),y
        eor     pat
        and     nml
        eor     pat
        sta     (a3d_p),y
        jmp     next
multi:  lda     (a3d_p),y
        eor     pat
        and     nml
        eor     pat
        sta     (a3d_p),y
        iny
        lda     rb
        clc                         ; rb - lb - 1
        sbc     lb
        bne     mid
        jmp     right
mid:    tax
        SPAN_MIDDLE
right:  lda     (a3d_p),y           ; Y = rb
        eor     pat
        and     nmr
        eor     pat
        sta     (a3d_p),y
.endmacro

