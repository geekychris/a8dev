# Shared build rules for Atari 8-bit projects in this repo (cc65).
#
# A project Makefile is just:
#
#   NAME := mygame
#   include ../../sdk/project.mk
#
# Layout:
#   src/*.c src/*.s   -> compiled (cc65 -O, target atari) and linked
#   sdk/src/*.c *.s   -> build/a8sdk.lib (a program links only what it uses)
#
# Outputs:  build/<NAME>.xex   Atari executable (load with the emulator / DOS)
#           build/<NAME>.map   ld65 map (segments, symbols)
#           build/<NAME>.lbl   VICE-style labels (./a8 symbols, emu_break("func"))
#           build/<NAME>.dbg   ld65 debug info (source lines)
#
# Memory map: sdk/cfg/a8.cfg (code $2000-$8FFF, stack $9000, PMG $9800,
# screen $A000-$BFFF). The link fails when MAIN overflows; we also check that
# LOWBSS/LOWCODE/LOWDATA (the 130XE bank helpers and buffers) end below the
# $4000 bank window.
#
# Overridable: OPT (default -Oirs), CFG (linker config), EXTRA_CFLAGS,
#   EXTRA_ASFLAGS, EXTRA_LDFLAGS, EXTRA_OBJS, DEBUG=1 (-O0; -g is always on:
#   it only adds symbols to the .lbl/.dbg), A8_NO_LOG=1
#   (compile a8_log calls out), START (load address, default $2000)
#
# Run via bin/a8-make (or ./a8 build), which supplies the toolchain.

ifndef NAME
$(error set NAME before including project.mk)
endif

SDK_DIR := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))

CC65_TARGET ?= atari
CFG     ?= $(SDK_DIR)/cfg/a8.cfg
SRC_DIR ?= src
BUILD   ?= build
OBJDIR  := $(BUILD)/obj

ifeq ($(DEBUG),1)
OPT ?= -O0 -g
else
OPT ?= -Oirs -g
endif

DEFS    := $(if $(filter 1,$(A8_NO_LOG)),-DA8_NO_LOG)
CFLAGS   = -t $(CC65_TARGET) $(OPT) -I$(SRC_DIR) -I$(SDK_DIR)/include $(DEFS) $(EXTRA_CFLAGS)
ASFLAGS  = -t $(CC65_TARGET) -g -I$(SRC_DIR) -I$(SDK_DIR)/include $(EXTRA_ASFLAGS)
LDFLAGS  = -t $(CC65_TARGET) -C $(CFG) $(if $(START),--start-addr $(START)) \
           -m $(BUILD)/$(NAME).map -Ln $(BUILD)/$(NAME).lbl -Wl --dbgfile,$(BUILD)/$(NAME).dbg $(EXTRA_LDFLAGS)

SRCS     := $(wildcard $(SRC_DIR)/*.c) $(wildcard $(SRC_DIR)/*.s)
SDK_SRCS := $(wildcard $(SDK_DIR)/src/*.c) $(wildcard $(SDK_DIR)/src/*.s)
OBJS     := $(patsubst $(SRC_DIR)/%,$(OBJDIR)/%.o,$(SRCS)) $(EXTRA_OBJS)
SDK_OBJS := $(patsubst $(SDK_DIR)/src/%,$(OBJDIR)/sdk/%.o,$(SDK_SRCS))
SDK_LIB  := $(BUILD)/a8sdk.lib
OUT      := $(BUILD)/$(NAME).xex

.DEFAULT_GOAL := all
all: $(OUT)
	@echo "built $(OUT) ($$(wc -c < $(OUT) | tr -d ' ') bytes)"

$(OBJDIR) $(OBJDIR)/sdk:
	mkdir -p $@

$(SDK_LIB): $(SDK_OBJS)
	rm -f $@
	ar65 a $@ $(SDK_OBJS)

# cc65 2.18 (e.g. a distro/Homebrew one with A8_NATIVE=1) wants the INIT
# segment as rw data; 2.19+ as bss. Link with a copy fixed for the version.
CC65_OLD := $(shell cc65 --version 2>&1 | grep -c 'V2\.1[0-8]')
LINK_CFG := $(if $(filter 1,$(CC65_OLD)),$(BUILD)/link.cfg,$(CFG))
$(BUILD)/link.cfg: $(CFG) | $(OBJDIR)
	sed -e 's/^\( *INIT: .*\)type = bss, define = yes,/\1type = rw,/' $< > $@

$(OUT): $(OBJS) $(SDK_LIB) $(LINK_CFG)
	cl65 $(subst -C $(CFG),-C $(LINK_CFG),$(LDFLAGS)) -o $@ $(OBJS) $(SDK_LIB)
	@# LOWBSS/LOWCODE/LOWDATA must end below the 130XE bank window ($4000).
	@m=$(BUILD)/$(NAME).map; \
	 for s in LOWBSS LOWCODE LOWDATA; do \
	   e=$$(awk -v s=$$s '$$1 == s && $$3 ~ /^[0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F][0-9A-F]$$/ {print $$3; exit}' $$m); \
	   if [ -n "$$e" ] && [ $$((0x$$e)) -ge $$((0x4000)) ]; then \
	     echo "error: $(NAME): $$s ends at \$$$$e, inside the 130XE bank window (\$$4000-\$$7FFF); make the program's start smaller"; \
	     rm -f $@; exit 1; fi; done

# every object depends on all headers: simple and safe for small projects
HDRS := $(wildcard $(SRC_DIR)/*.h) $(wildcard $(SDK_DIR)/include/*.h) $(wildcard $(SDK_DIR)/src/*.h)

$(OBJDIR)/%.c.o: $(SRC_DIR)/%.c $(HDRS) | $(OBJDIR)
	cl65 $(CFLAGS) -c --listing $(OBJDIR)/$*.lst -o $@ $<

$(OBJDIR)/%.s.o: $(SRC_DIR)/%.s | $(OBJDIR)
	ca65 $(ASFLAGS) -o $@ $<

$(OBJDIR)/sdk/%.c.o: $(SDK_DIR)/src/%.c $(HDRS) | $(OBJDIR)/sdk
	cl65 $(CFLAGS) -c -o $@ $<

$(OBJDIR)/sdk/%.s.o: $(SDK_DIR)/src/%.s | $(OBJDIR)/sdk
	ca65 $(ASFLAGS) -o $@ $<

clean:
	rm -rf $(BUILD)

.PHONY: all clean
