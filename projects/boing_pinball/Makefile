NAME   := boing_pinball
A8_SDK ?= ../../sdk
CFG    := boing_pinball.cfg
OPT    := -Ors -g
ifeq ($(SEGCHECK),1)
EXTRA_CFLAGS += -DSEGCHECK
EXTRA_ASFLAGS += -DSEGCHECK
endif
include $(A8_SDK)/project.mk

# where each part runs (boing_pinball.cfg)
B0 := --code-name B0CODE --rodata-name B0RO --data-name B0DATA --bss-name B0BSS -Cl \
      -Dpb_world_step=b0_world_step -Dpb_flipper_tip=b0_flipper_tip
B2 := --code-name B2CODE --rodata-name B2RO --data-name B2DATA --bss-name B2BSS \
      -Dpb_world_init=b2_world_init -Dpb_add_ball=b2_add_ball -Dpb_plunge=b2_plunge \
      -Dpb_kick_saucer=b2_kick_saucer -Dpb_nudge=b2_nudge -Dpb_remove_drained=b2_remove_drained \
      -Dpb_ramp_step=b2_ramp_step -Dpb_mb_prepare=b2_mb_prepare -Dpb_hit_balls=b2_hit_balls
B1 := --code-name B1CODE --rodata-name B1RO --data-name B1DATA --bss-name B1BSS \
      -Dpb_game_init=b1_game_init -Dpb_game_start=b1_game_start -Dpb_game_tick=b1_game_tick \
      -Dpb_live_balls=b1_live_balls -Dpb_say=b1_say
FRONT := --code-name FRONT --rodata-name FRONTRO --data-name FRONTDATA --bss-name FRONTBSS
$(OBJDIR)/pb_phys.c.o $(OBJDIR)/zmath.c.o: EXTRA_CFLAGS += $(B0)
$(OBJDIR)/physasm.s.o: EXTRA_ASFLAGS +=
$(OBJDIR)/pb_phys2.c.o: EXTRA_CFLAGS += $(B2)
$(OBJDIR)/pb_rules.c.o: EXTRA_CFLAGS += $(B1) -Cl
$(OBJDIR)/table_0.c.o $(OBJDIR)/sin_tab.c.o: EXTRA_CFLAGS += --rodata-name TABRO --data-name TABDATA --bss-name TABBSS
$(OBJDIR)/front.c.o $(OBJDIR)/pb_rules2.c.o $(OBJDIR)/segcheck.c.o $(OBJDIR)/gfx_data.c.o: EXTRA_CFLAGS += $(FRONT)

# the table's graphics are made by tools/gen_gfx.py (checked in)
$(OBJDIR)/gfx.s.o: gfx/fonts.bin gfx/map.bin gfx/flips.bin
# what both the rules (bank 1) and the front end call: under the ROM
$(OBJDIR)/pb_common.c.o: EXTRA_CFLAGS += --code-name HICODE --rodata-name TABRO --data-name TABDATA
