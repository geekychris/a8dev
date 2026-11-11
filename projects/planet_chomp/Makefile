NAME   := planet_chomp
A8_SDK ?= ../../sdk
CFG    := planet_chomp.cfg
OPT    := -Ors -g
include $(A8_SDK)/project.mk

# the rules: static locals (cc65's stack frames are slow)
$(OBJDIR)/game.c.o $(OBJDIR)/view.c.o: EXTRA_CFLAGS += -Cl
# the maze's data runs under the OS ROM (planet_chomp.cfg)
$(OBJDIR)/maze_data.c.o: EXTRA_CFLAGS += --rodata-name TOPO
$(OBJDIR)/maze_levels.c.o: EXTRA_CFLAGS += --rodata-name LEVELS

ifeq ($(MATHCHECK),1)
EXTRA_CFLAGS += -DMATHCHECK
endif
