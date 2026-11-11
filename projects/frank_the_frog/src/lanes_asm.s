; Frank the Frog - the per-frame lane work in 6502 (cc65's C for these took
; most of a frame). The C they implement is beside each in lanes.c/render.c.

        .export _lanes_tick, _render_scroll
        .import _lane_dir, _lane_speed, _lane_period, _lane_shift, _lane_row
        .import _pend_lo, _pend_hi, _pend_hs
        .import _lane_len, _lane_tpx, _lane_buf_lo, _lane_buf_hi, _scroll_k

NUM_LANES   = 10
LANE_STRIDE = 100

        .zeropage
tmp:    .res 2
        .code

; void lanes_tick(void): shift += speed (right) or -= speed (left), mod period
.proc _lanes_tick
        ldx #NUM_LANES-1
lane:   txa
        asl a
        tay                     ; y = 2 * lane (16-bit arrays)
        lda _lane_dir,x
        bmi left
        clc
        lda _lane_shift,y
        adc _lane_speed,x
        sta _lane_shift,y
        lda _lane_shift+1,y
        adc #0
        sta _lane_shift+1,y
        ; if shift >= period: shift -= period
        lda _lane_shift,y
        cmp _lane_period,y
        lda _lane_shift+1,y
        sbc _lane_period+1,y
        bcc next
        lda _lane_shift,y
        sbc _lane_period,y      ; (carry set)
        sta _lane_shift,y
        lda _lane_shift+1,y
        sbc _lane_period+1,y
        sta _lane_shift+1,y
        jmp next
left:   sec
        lda _lane_shift,y
        sbc _lane_speed,x
        sta _lane_shift,y
        lda _lane_shift+1,y
        sbc #0
        sta _lane_shift+1,y
        bpl next
        clc
        lda _lane_shift,y
        adc _lane_period,y
        sta _lane_shift,y
        lda _lane_shift+1,y
        adc _lane_period+1,y
        sta _lane_shift+1,y
next:   dex
        bpl lane
        rts
.endproc

; void render_scroll(void): each lane's display-list address and HSCROL for
; its scroll position s = shift / 2 (Atari pixels):
;   q = (-s - scroll_k) mod tpx;  h = -q & 3;  i = (q + h) / 4 (mod len)
;   both lines' LMS = buffer + i (+ LANE_STRIDE),  HSCROL of the row = h
.proc _render_scroll
        ldx #NUM_LANES-1
lane:   txa
        asl a
        tay
        lda _lane_shift+1,y     ; s = shift >> 1 (shift < 512: fits a byte)
        lsr a
        lda _lane_shift,y
        ror a
        clc
        adc _scroll_k           ; s + k (k > 0, < 256 - s: a byte, see render.c)
        sta tmp                 ; t = s + k; q = (-t) mod tpx = tpx * n - t
        lda #0
        sta tmp+1
        ; reduce t below tpx
@red:   lda tmp
        cmp _lane_tpx,x
        bcc @lt
        sbc _lane_tpx,x
        sta tmp
        jmp @red
@lt:    ; q = t ? tpx - t : 0
        lda tmp
        beq @q0
        lda _lane_tpx,x
        sec
        sbc tmp
@q0:    sta tmp                 ; q
        eor #$FF                ; h = -q & 3
        clc
        adc #1
        and #3
        pha
        ldy _lane_row,x
        sta _pend_hs-1,y        ; row r: DLI index r - 1
        pla
        clc
        adc tmp                 ; q + h  (< 256 + 3: carry is the 9th bit)
        ror a
        lsr a                   ; i = (q + h) >> 2
        cmp _lane_len,x
        bcc @in
        sbc _lane_len,x
@in:    sta tmp
        txa
        asl a
        tay                     ; y = 2 * lane: the lane's two LMS slots
        lda _lane_buf_lo,x
        clc
        adc tmp
        sta _pend_lo,y
        lda _lane_buf_hi,x
        adc #0
        sta _pend_hi,y
        lda _pend_lo,y
        clc
        adc #LANE_STRIDE
        sta _pend_lo+1,y
        lda _pend_hi,y
        adc #0
        sta _pend_hi+1,y
        dex
        bpl lane
        rts
.endproc
