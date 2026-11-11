; gfx.s - the table's graphics (tools/gen_gfx.py makes gfx/), loaded into
; bank 3: the fonts and the map are copied out at start-up (front.c), the
; flippers' frames are read from here as the flippers move.
.segment "GFX"
        .export _g_fonts, _g_map, _g_flips
_g_fonts:
        .incbin "gfx/fonts.bin"
_g_map:
        .incbin "gfx/map.bin"
_g_flips:
        .incbin "gfx/flips.bin"
