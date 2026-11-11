NAME   := tank_duel
A8_SDK ?= ../../sdk
CFG    := tank_duel.cfg
OPT    := -Ors -g
# the front end (game.c, view.c, a3d's and the SDK's C) runs in main memory
# $4000-$8FFF; the rules (td_core.c) in the 130XE's bank 0 (tank_duel.cfg)
FRONT := --code-name FRONT --rodata-name FRONTRO --data-name FRONTDATA
B0    := --code-name B0CODE --rodata-name B0RO --data-name B0DATA \
         -Dtd_new_game=b0_td_new_game -Dtd_tick=b0_td_tick -Dtd_bot=b0_td_bot \
         -Dtd_pop_event=b0_td_pop_event -Dtd_enemies_left=b0_td_enemies_left
EXTRA_CFLAGS  := -Cl -DA3D_LOWTAB -DA3D_MAXV=32 $(FRONT)
EXTRA_ASFLAGS := -DA3D_LOWTAB -DA3D_LOWXF -DA3D_MAXV=32
include ../a8_3d/a3d.mk
include $(A8_SDK)/project.mk
$(OBJDIR)/td_core.c.o: EXTRA_CFLAGS += $(B0)
