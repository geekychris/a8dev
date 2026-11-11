NAME   := boing_twist
A8_SDK ?= ../../sdk
CFG    := boing_twist.cfg
include $(A8_SDK)/project.mk

# the banks' data is made by tools/gen_data.py (checked in)
$(OBJDIR)/xbanks.s.o: gfx/bank0.bin gfx/bank1.bin gfx/bank2.bin gfx/bank3.bin
# the rules run from the RAM under the OS ROM (boing_twist.cfg)
$(OBJDIR)/bt_core.c.o: EXTRA_CFLAGS += --code-name HICODE --rodata-name HIRODATA --data-name HIDATA
