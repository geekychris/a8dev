; a8dev start-up (a constructor, runs before main): point the C stack at the
; fixed $9000-$97FF area of sdk/cfg/a8.cfg (cc65's crt0 starts it at MEMTOP,
; which is inside our screen area), lower MEMTOP/APPMHI to match, and switch
; BASIC off on XL/XE so $A000-$BFFF is RAM. Nothing is on the C stack yet when
; constructors run, so moving it is safe.

        .export         __A8_INIT__ : absolute = 1
        .constructor    a8_crt_init, 30
; the C stack pointer is c_sp in the pinned cc65 (2.19 git) and sp in 2.18
.if .version < $0300            ; 2.18 says $0320 (old encoding), 2.19 git $0213
        .importzp       c_sp
.else
        .importzp       sp
c_sp    = sp
.endif
        .import         __STACK_START__, __STACK_SIZE__

MEMTOP  = $02E5
APPMHI  = $000E
RAMTOP  = $006A
PORTB   = $D301

.segment "ONCE"

a8_crt_init:
        lda     #<(__STACK_START__ + __STACK_SIZE__)
        sta     c_sp
        lda     #>(__STACK_START__ + __STACK_SIZE__)
        sta     c_sp+1
        lda     #<(__STACK_START__ - 1)
        sta     MEMTOP
        sta     APPMHI
        lda     #>(__STACK_START__ - 1)
        sta     MEMTOP+1
        sta     APPMHI+1
        ; XL/XE: PORTB bit 1 = 1 is BASIC off. On a 400/800 PORTB is the
        ; joystick 3/4 port set to input, so this write changes nothing.
        lda     PORTB
        ora     #$02
        sta     PORTB
        rts
