; os.s - Boing Twist runs with the OS ROM switched off (XL/XE PORTB bit 0),
; for the 14K of RAM under it: the game's state, the ball's texture and
; the rules' code (boing_twist.cfg). So this file stands in for the parts
; of the OS the game (and the a8dev SDK) rely on:
;
;   NMI   a DLI goes to (VDSLST); the vertical blank counts RTCLOK, copies
;         the shadows the SDK writes (SDMCTL, SDLSTL, CHBAS, GPRIOR, the
;         colours) to the chips, reads the joysticks into STICK0/STRIG0
;         and calls (VVBLKD) like the OS's deferred VBI
;   IRQ   a key press puts its code in CH ($2FC), as the OS does
;
; The ROM font is copied to $B800 first (text rows: CHBAS $B8). RESET on
; an XL/XE resets the MMU, so the OS comes back and boots as usual.

        .export _os_off, _os_vbi_exit
        .import __HICODE_LOAD__, __HICODE_RUN__, __HICODE_SIZE__
        .import __HIRODATA_SIZE__, __HIDATA_SIZE__
        .import __HIBSS_RUN__, __HIBSS_SIZE__
        .import _xe_bank_select
        .importzp ptr1, ptr2, ptr3

PORTB   = $D301
NMIEN   = $D40E
NMIST   = $D40F
NMIRES  = $D40F
IRQEN   = $D20E
IRQST   = $D20E
KBCODE  = $D209
SKCTL   = $D20F
PORTA   = $D300
TRIG0   = $D010
TRIG1   = $D011
DMACTL  = $D400
DLISTL  = $D402
DLISTH  = $D403
CHBASE  = $D409
CHACTL  = $D401
PRIOR   = $D01B
COLPM0  = $D012
GRACTL  = $D01D
RTCLOK  = $0012
VDSLST  = $0200
VVBLKD  = $0224
SDMCTL  = $022F
SDLSTL  = $0230
GPRIOR  = $026F
STICK0  = $0278
STICK1  = $0279
STRIG0  = $0284
STRIG1  = $0285
PCOLR0  = $02C0
CHACT   = $02F3
CHBAS   = $02F4
CH      = $02FC
POKMSK  = $0010
FONT    = $B800

.segment "LOWDATA"
irq_a:  .byte 0

.segment "LOWCODE"

nmi:    bit     NMIST
        bpl     @vbi
        jmp     (VDSLST)
@vbi:   sta     NMIRES
        pha
        txa
        pha
        tya
        pha
        inc     RTCLOK+2
        bne     @t
        inc     RTCLOK+1
        bne     @t
        inc     RTCLOK
@t:     lda     SDMCTL
        sta     DMACTL
        lda     SDLSTL
        sta     DLISTL
        lda     SDLSTL+1
        sta     DLISTH
        lda     CHBAS
        sta     CHBASE
        lda     CHACT
        sta     CHACTL
        lda     GPRIOR
        sta     PRIOR
        ldx     #8
@col:   lda     PCOLR0,x
        sta     COLPM0,x
        dex
        bpl     @col
        lda     PORTA
        tax
        and     #$0F
        sta     STICK0
        txa
        lsr
        lsr
        lsr
        lsr
        sta     STICK1
        lda     TRIG0
        sta     STRIG0
        lda     TRIG1
        sta     STRIG1
        jmp     (VVBLKD)
; the deferred VBI ends here (the OS's XITVBV)
_os_vbi_exit:
        pla
        tay
        pla
        tax
        pla
irq_rti:
        rti

irq:    sta     irq_a
        lda     IRQST
        and     #$40
        bne     @other
        lda     #$BF            ; acknowledge: the key bit off and on again
        sta     IRQEN
        lda     #$C0
        sta     IRQEN
        lda     KBCODE
        sta     CH
        lda     irq_a
        rti
@other: lda     #$C0            ; anything else: acknowledge everything
        sta     IRQEN
        lda     irq_a
        rti

; void os_off(void): copy the font, switch the OS ROM off, our vectors in,
; the rules' code up from bank 2, the state zeroed
_os_off:
        ; the font: $E000-$E3FF -> $B800
        ldx     #0
@f:     lda     $E000,x
        sta     FONT,x
        lda     $E100,x
        sta     FONT+$100,x
        lda     $E200,x
        sta     FONT+$200,x
        lda     $E300,x
        sta     FONT+$300,x
        inx
        bne     @f
        lda     #>FONT
        sta     CHBAS
        sei
        lda     #0
        sta     NMIEN
        lda     PORTB
        and     #$FE
        sta     PORTB
        ; the state (HIBSS) to zero
        lda     #<__HIBSS_RUN__
        sta     ptr1
        lda     #>__HIBSS_RUN__
        sta     ptr1+1
        ldx     #>(__HIBSS_SIZE__ + 255)
        lda     #0
        tay
@z:     sta     (ptr1),y
        iny
        bne     @z
        inc     ptr1+1
        dex
        bne     @z
        ; the rules: bank 2 at __HICODE_LOAD__ -> __HICODE_RUN__
        lda     #2
        jsr     _xe_bank_select
        lda     #<__HICODE_LOAD__
        sta     ptr1
        lda     #>__HICODE_LOAD__
        sta     ptr1+1
        lda     #<__HICODE_RUN__
        sta     ptr2
        lda     #>__HICODE_RUN__
        sta     ptr2+1
        ldx     #>(__HICODE_SIZE__ + __HIRODATA_SIZE__ + __HIDATA_SIZE__ + 255)
        ldy     #0
@c:     lda     (ptr1),y
        sta     (ptr2),y
        iny
        bne     @c
        inc     ptr1+1
        inc     ptr2+1
        dex
        bne     @c
        lda     #$FF
        jsr     _xe_bank_select
        lda     #<nmi
        sta     $FFFA
        lda     #>nmi
        sta     $FFFB
        lda     #<irq_rti       ; RESET resets the MMU first: the OS's vector is used
        sta     $FFFC
        lda     #>irq_rti
        sta     $FFFD
        lda     #<irq
        sta     $FFFE
        lda     #>irq
        sta     $FFFF
        lda     #<_os_vbi_exit
        sta     VVBLKD
        lda     #>_os_vbi_exit
        sta     VVBLKD+1
        lda     #$C0
        sta     POKMSK
        sta     IRQEN
        lda     #$FF
        sta     CH
        lda     #$40            ; VBI on (DLIs when the game starts them)
        sta     NMIEN
        cli
        rts

