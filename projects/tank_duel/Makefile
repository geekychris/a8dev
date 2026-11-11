NAME   := tank_duel
A8_SDK ?= ../../sdk
CFG    := tank_duel.cfg
OPT    := -Or -g
# static locals (-Cl): the C stack only holds call arguments (tank_duel.cfg:
# 512 bytes); a3d's tables and work arrays in LOWRAM ($0C00-$1FFF)
EXTRA_CFLAGS  := -Cl -DA3D_LOWTAB -DA3D_FFILL
EXTRA_ASFLAGS := -DA3D_LOWTAB -DA3D_FFILL
ifeq ($(TT),1)
EXTRA_CFLAGS += -DTT
endif
include ../a8_3d/a3d.mk
include $(A8_SDK)/project.mk

# under the OS ROM (tank_duel.cfg, xload.s, os.s)
$(OBJDIR)/td_core.c.o: EXTRA_CFLAGS += --code-name HI1CODE --rodata-name HI1CODE
$(OBJDIR)/td_fix.c.o: EXTRA_CFLAGS += --code-name HI1CODE --rodata-name HI1CODE
$(OBJDIR)/td_ai.c.o: EXTRA_CFLAGS += --code-name HI2CODE --rodata-name HI2CODE
$(OBJDIR)/radar.c.o: EXTRA_CFLAGS += --code-name HI2CODE --rodata-name HI2CODE
$(OBJDIR)/view.c.o: EXTRA_CFLAGS += --bss-name LOWRAM
