; apart.s - td_core.c's tanks_apart() in asm: every pair of live tanks
; nearer than two radii (56 units: 896) is pushed apart along the line
; between them, half the overlap each way:
;   d = |(dx, dz)| (min 16; then dx = 16), push = (896 - d) / 2,
;   a -= (dx, dz) * push / d, b += the same.
; Each tank it moves gets its bit in apart_moved (td_tick pushes only those
; out of the obstacles again).
;
; minds_(tick): td_tick's loop over the enemies: each thinks (think(k), C)
; on its tick of every 2 (every 4 with more than 3 enemies), and between
; them counts its reload down and drives on its last decision (drive_).
;
; Tank (20 bytes; G.tk at G + 12): alive +1, x +3, z +5, reload +11,
; held +15, want +16, speed +18.

        .macpack longbranch
        .export _tanks_apart, _apart_moved, _minds_
        .import _G, _td_len_, _ln_x, _ln_z, _a3d_muldiv_, _a3d_mda, _a3d_mdb
        .import _think, _drive_, _dr_want, _dr_speed

TK      = 12
TSIZE   = 20
NTANKS  = 12 + 8 * TSIZE                ; G.ntanks
REACH   = 896

.segment "ZEROPAGE"
ap:     .res 2
bp:     .res 2
mp:     .res 2

.segment "BSS"
ni:     .res 1
nj:     .res 1
dx:     .res 2
dz:     .res 2
dd:     .res 2
push:   .res 2
mx:     .res 2
mz:     .res 2
_apart_moved: .res 1
bi:     .res 1                          ; a's bit
bj:     .res 1                          ; b's bit
mk:     .res 1
mtick:  .res 1
mmask:  .res 1

.segment "HI1CODE"

_tanks_apart:
        lda     #0
        sta     _apart_moved
        lda     #1
        sta     bi
        lda     _G + NTANKS
        sta     ni
        lda     #<(_G + TK)
        sta     ap
        lda     #>(_G + TK)
        sta     ap+1
@ai:    dec     ni
        bpl     :+
        rts
:       ldy     #1
        lda     (ap),y
        beq     @anext
        lda     ni
        sta     nj
        lda     bi
        asl
        sta     bj
        clc
        lda     ap
        adc     #TSIZE
        sta     bp
        lda     ap+1
        adc     #0
        sta     bp+1
@bj:    lda     nj
        beq     @anext
        dec     nj
        ldy     #1
        lda     (bp),y
        beq     @bnext
        ; dx = b.x - a.x
        ldy     #3
        sec
        lda     (bp),y
        sbc     (ap),y
        sta     dx
        iny
        lda     (bp),y
        sbc     (ap),y
        sta     dx+1
        tax
        lda     dx
        jsr     near
        bcs     @bnext
        ; dz = b.z - a.z
        ldy     #5
        sec
        lda     (bp),y
        sbc     (ap),y
        sta     dz
        iny
        lda     (bp),y
        sbc     (ap),y
        sta     dz+1
        tax
        lda     dz
        jsr     near
        bcs     @bnext
        jsr     pair
@bnext: asl     bj
        clc
        lda     bp
        adc     #TSIZE
        sta     bp
        jcc     @bj
        inc     bp+1
        jne     @bj
@anext: asl     bi
        clc
        lda     ap
        adc     #TSIZE
        sta     ap
        jcc     @ai
        inc     ap+1
        jne     @ai

; carry clear if -REACH < v < REACH (A lo, X hi)
near:   cpx     #$80
        bcs     @n
        cmp     #<REACH                 ; v >= REACH: carry set
        txa
        sbc     #>REACH
        rts
@n:     ; v <= -REACH  <=>  v + REACH <= 0  <=>  v + REACH - 1 < 0
        clc
        adc     #<(REACH - 1)
        txa
        adc     #>(REACH - 1)
        asl                             ; sign -> carry: negative = too far
        rts

pair:   lda     dx
        sta     _ln_x
        lda     dx+1
        sta     _ln_x+1
        lda     dz
        sta     _ln_z
        lda     dz+1
        sta     _ln_z+1
        jsr     _td_len_
        sta     dd
        stx     dd+1
        cmp     #<REACH                 ; d >= REACH (unsigned): not touching
        txa
        sbc     #>REACH
        bcc     :+
        rts
:       lda     dd+1                    ; d < 16: d = dx = 16
        bne     :+
        lda     dd
        cmp     #16
        bcs     :+
        lda     #16
        sta     dd
        sta     dx
        lda     #0
        sta     dx+1
:       sec                             ; push = (REACH - d) / 2
        lda     #<REACH
        sbc     dd
        sta     push
        lda     #>REACH
        sbc     dd+1
        lsr
        sta     push+1
        ror     push
        ; mx = dx * push / d, mz = dz * push / d
        lda     dx
        sta     _a3d_mda
        lda     dx+1
        sta     _a3d_mda+1
        lda     push
        sta     _a3d_mdb
        lda     push+1
        sta     _a3d_mdb+1
        lda     dd
        ldx     dd+1
        jsr     _a3d_muldiv_
        sta     mx
        stx     mx+1
        lda     dz
        sta     _a3d_mda
        lda     dz+1
        sta     _a3d_mda+1
        lda     push
        sta     _a3d_mdb
        lda     push+1
        sta     _a3d_mdb+1
        lda     dd
        ldx     dd+1
        jsr     _a3d_muldiv_
        sta     mz
        stx     mz+1
        ; a -= (mx, mz); b += (mx, mz)
        lda     _apart_moved
        ora     bi
        ora     bj
        sta     _apart_moved
        ldy     #3
        sec
        lda     (ap),y
        sbc     mx
        sta     (ap),y
        iny
        lda     (ap),y
        sbc     mx+1
        sta     (ap),y
        iny
        sec
        lda     (ap),y
        sbc     mz
        sta     (ap),y
        iny
        lda     (ap),y
        sbc     mz+1
        sta     (ap),y
        ldy     #3
        clc
        lda     (bp),y
        adc     mx
        sta     (bp),y
        iny
        lda     (bp),y
        adc     mx+1
        sta     (bp),y
        iny
        clc
        lda     (bp),y
        adc     mz
        sta     (bp),y
        iny
        lda     (bp),y
        adc     mz+1
        sta     (bp),y
        rts

; void __fastcall__ minds_(uint8_t tick)
_minds_:
        sta     mtick
        ldx     #1
        lda     _G + NTANKS
        cmp     #5
        bcc     :+
        ldx     #3
:       stx     mmask
        lda     #1
        sta     mk
        lda     #<(_G + TK + TSIZE)
        sta     mp
        lda     #>(_G + TK + TSIZE)
        sta     mp+1
@k:     lda     mk
        cmp     _G + NTANKS
        bcc     :+
        rts
:       ldy     #1                      ; alive, not held
        lda     (mp),y
        beq     @next
        ldy     #15
        lda     (mp),y
        bne     @next
        lda     mk                      ; its tick to think?
        clc
        adc     mtick
        and     mmask
        beq     @think
        ldy     #16                     ; (want 0: it hasn't thought yet)
        lda     (mp),y
        iny
        ora     (mp),y
        beq     @think
        ldy     #11                     ; reload > 0: down one
        lda     (mp),y
        beq     :+
        bmi     :+
        sec
        sbc     #1
        sta     (mp),y
:       ldy     #16
        lda     (mp),y
        sta     _dr_want
        iny
        lda     (mp),y
        sta     _dr_want+1
        iny
        lda     (mp),y
        sta     _dr_speed
        iny
        lda     (mp),y
        sta     _dr_speed+1
        lda     mp
        ldx     mp+1
        jsr     _drive_
        jmp     @next
@think: lda     mk
        jsr     _think
@next:  inc     mk
        clc
        lda     mp
        adc     #TSIZE
        sta     mp
        bcc     @k
        inc     mp+1
        bne     @k
