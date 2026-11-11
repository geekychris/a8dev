NAME   := spectral_keep
A8_SDK ?= ../../sdk
OPT    := -Ors -g
CFG    := measure.cfg
include $(A8_SDK)/project.mk

ifeq ($(MATHCHECK),1)
EXTRA_CFLAGS += -DMATHCHECK
endif

ifeq ($(GATHERCHECK),1)
EXTRA_CFLAGS += -DGATHERCHECK
endif

ifeq ($(SCANCHECK),1)
EXTRA_CFLAGS += -DSCANCHECK
endif
