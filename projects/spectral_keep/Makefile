NAME   := spectral_keep
A8_SDK ?= ../../sdk
CFG    := spectral_keep.cfg
OPT    := -Ors -g
include $(A8_SDK)/project.mk

# where each part runs (spectral_keep.cfg)
B0 := --code-name B0CODE --rodata-name B0RO --data-name B0DATA \
      -Dgame_init=b0_game_init -Dgame_step=b0_game_step -Droom_grounds=b0_room_grounds
HI := --code-name HICODE --rodata-name HICODE --data-name HICODE
FRONT := --code-name FRONT --rodata-name FRONTRO --data-name FRONTDATA --bss-name FRONTBSS
$(OBJDIR)/game.c.o: EXTRA_CFLAGS += $(B0)
$(OBJDIR)/room.c.o: EXTRA_CFLAGS += $(B0) --bss-name LOWRAM
$(OBJDIR)/world.c.o: EXTRA_CFLAGS += $(HI) --bss-name LOWRAM
$(OBJDIR)/levels.c.o: EXTRA_CFLAGS += --code-name HICODE --rodata-name LEVRO --data-name LEVRO
$(OBJDIR)/main.c.o $(OBJDIR)/probe.c.o $(OBJDIR)/view.c.o: EXTRA_CFLAGS += $(FRONT)

# the host sim (tools/sim.c: the bot, tools/bot.py)
HOSTSRC := tools/sim.c src/game.c src/room.c src/world.c src/levels.c src/probe.c src/isin.c src/rle.c tools/host_maths.c
build/host/sim: $(HOSTSRC) src/keep.h
	mkdir -p build/host
	cc -O2 -w -include tools/host.h -Isrc -Itools -o $@ $(HOSTSRC)
