# a8dev SDK

The library every project links (reference: [docs/sdk.md](../docs/sdk.md)).

## Versioning

`A8_SDK_VERSION` (sdk/include/a8hw.h, included by every SDK header) is
`major * 10000 + minor * 100 + patch`.

- **patch**: fixes, faster code, docs. No API change.
- **minor**: additions only (new functions, headers, macros). Every existing
  program builds and behaves the same.
- **major**: a change that can break a program (renamed or removed function,
  changed meaning, memory map change). Only with the platform owner's
  agreement and every project in the repo updated in the same PR.

A project that needs something newer can say so:

```c
#if A8_SDK_VERSION < 10100
#error needs a8dev SDK 1.1
#endif
```

Changes to `sdk/` go through a PR that keeps `./a8 selftest` green.

## Changelog

- **1.2.1**: a8_log (printf) split from a8_log_s/a8_log_u/a8_done/a8_fail: using only
  those no longer links vsnprintf (~1.3K).
- **1.2.0**: `sound_tick` in asm (21 cycles idle, ~200 busy; was ~1,900 / 2,800 in C),
  same API and output (projects/soundtest); optional `LOWRAM` segment $0C00-$1FFF.
- **1.1.0**: project.mk `EXTRA_SRC_DIRS` (compile shared source directories).
- **1.0.0**: first release: a8.cfg memory map + crt init, log
  ($D1F0/$D1F1, compiled out with RELEASE=1), frame timer, input, POKEY
  sound/sfx/music, display lists (ANTIC 2/4/D/E/F + text rows), drawing and
  text, PMG, 130XE banks, fix8, fix16/vec3, project.mk, template, boot loader.
