NAME   := scairy_fun
A8_SDK ?= ../../sdk
CFG    := scairy_fun.cfg
include $(A8_SDK)/project.mk

# the renderer runs from the RAM under the OS ROM (scairy_fun.cfg), its
# state in the screen area's spare half; the routes' code under the ROM,
# their segment arrays in LOWRAM
$(OBJDIR)/render.c.o: EXTRA_CFLAGS += --code-name HIA --rodata-name HIA --data-name HIA --bss-name SCRBSS
$(OBJDIR)/track.c.o: EXTRA_CFLAGS += --code-name HIB --rodata-name HIB --data-name HIB --bss-name LOWRAM
$(OBJDIR)/sprites.c.o: EXTRA_CFLAGS += --code-name HIA2 --rodata-name HIA2 --data-name HIA2 --bss-name SCRBSS
# the simulation and the front end: optimised for size (-Ors: ~10% smaller)
$(OBJDIR)/world.c.o $(OBJDIR)/main.c.o: OPT = -Ors -g
