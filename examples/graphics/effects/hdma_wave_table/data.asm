;----------------------------------------------------------------------
; HDMA Wave Table — Mode 3 background data (256 colors, 8bpp)
;
; Original procedural water-caustics art (res/water.bmp, generated —
; no krom assets). Tileset >32KB: split across two SUPERFREE sections
; (LoROM bank limit); post-A6 C pointers carry the bank byte and
; dmaCopyVram reads it directly.
;----------------------------------------------------------------------

.section ".rodata1" superfree

tiles:      .incbin "res/water.pic" skip 0 read 32768
tiles_end:

.ends

.section ".rodata2" superfree

tiles2:     .incbin "res/water.pic" skip 32768
tiles2_end:

tilemap:    .incbin "res/water.map"
tilemap_end:

palette:    .incbin "res/water.pal"
palette_end:

.ends
