NAME   := rolling_steel
A8_SDK ?= ../../sdk
CFG    := rolling_steel.cfg
OPT    := -Ors -g
include $(A8_SDK)/project.mk
EXTRA_ASFLAGS += --bin-include-dir .

# the banks' contents (tools/gen_course.py) are .incbin'd by xbanks.s
build/obj/xbanks.s.o: $(wildcard gfx/bank*.bin)
