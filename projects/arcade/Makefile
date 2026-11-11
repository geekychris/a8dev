# The arcade menu: built by ./a8 arcade (a8dev/arcade.py writes build/gen/games.h
# from every project's arcade.json, then builds this, then the ATR disk).
NAME   := menu
A8_SDK ?= ../../sdk
EXTRA_CFLAGS := -Ibuild/gen
include $(A8_SDK)/project.mk

ifeq ($(wildcard build/gen/games.h),)
$(shell mkdir -p build/gen && printf '%s\n' '#define GAME_COUNT 0' 'static const char *const game_title[1] = { "" };' 'static const char *const game_desc[1] = { "" };' 'static const char *const game_controls[1] = { "" };' > build/gen/games.h)
endif
