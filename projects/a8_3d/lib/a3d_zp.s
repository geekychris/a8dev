; a3d: the renderer's zero page (cc65 allocates it in ZEROPAGE, $82-$FF).
        .exportzp a3d_bsel, a3d_p, a3d_q, a3d_t, a3d_m
        .exportzp _a3d_bsel

.segment "ZEROPAGE"
a3d_bsel: .res 1        ; $00 = drawing buffer 0, $10 = buffer 1 (ORA into line hi)
a3d_p:    .res 2        ; pointer into the back buffer
a3d_q:    .res 2        ; second pointer
a3d_m:    .res 8        ; multiply: pointers to sq1lo+a, sq2lo+~a, sq1hi+a, sq2hi+~a
a3d_t:    .res 16       ; scratch for the routines (not kept across calls)
_a3d_bsel = a3d_bsel    ; C: extern uint8_t a3d_bsel; #pragma zpsym ("a3d_bsel")

; ---------------------------------------------------------------- HUD DLI
; on the last 3D line: the text rows' colours (COLPF1 luminance, COLPF2
; background); the OS VBI puts the shadows back for the next frame
        .export   _a3d_hud_dli, _a3d_hud_c1, _a3d_hud_c2
.segment "DATA"
_a3d_hud_c1:    .byte $0E
_a3d_hud_c2:    .byte $92
.segment "CODE"
_a3d_hud_dli:
        pha
        txa
        pha
        lda     _a3d_hud_c2
        ldx     _a3d_hud_c1
        sta     $D40A           ; WSYNC
        sta     $D018           ; COLPF2
        stx     $D017           ; COLPF1
        pla
        tax
        pla
        rti
