/* a8h headless emulator: Atari800 built as libatari800 (no SDL) plus the
   a8h main loop (see a8h.c). */
#include "config.h"
#include "a8h.h"

int main(int argc, char *argv[])
{
	return A8H_HeadlessMain(argc, argv);
}
