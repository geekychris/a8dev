# a3d renderer: include this before sdk/project.mk in a game's Makefile:
#
#   NAME   := mygame
#   A8_SDK ?= ../../sdk
#   include ../a8_3d/a3d.mk
#   include $(A8_SDK)/project.mk
#
# It builds projects/a8_3d/lib/*.c *.s into build/a3d.lib (a library: the
# game links only the modules it uses) with the game's own flags.
A3D_DIR  := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))/lib
A3D_SRCS := $(wildcard $(A3D_DIR)/*.c) $(wildcard $(A3D_DIR)/*.s)
A3D_OBJS := $(patsubst $(A3D_DIR)/%,build/obj/a3d/%.o,$(A3D_SRCS))
A3D_LIB  := build/a3d.lib
EXTRA_OBJS    += $(A3D_LIB)
EXTRA_CFLAGS  += -I$(A3D_DIR)
EXTRA_ASFLAGS += -I$(A3D_DIR)

$(A3D_LIB): $(A3D_OBJS)
	rm -f $@
	ar65 a $@ $(A3D_OBJS)

build/obj/a3d/%.c.o: $(A3D_DIR)/%.c $(wildcard $(A3D_DIR)/*.h $(A3D_DIR)/*.inc)
	@mkdir -p $(dir $@)
	cl65 $(CFLAGS) -c -o $@ $<

build/obj/a3d/%.s.o: $(A3D_DIR)/%.s
	@mkdir -p $(dir $@)
	ca65 $(ASFLAGS) -o $@ $<
