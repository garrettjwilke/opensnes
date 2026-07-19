;----------------------------------------------------------------------
; benchrom — real map data for the map.asm measurement points (same
; blobs and bank-2 pinning as devtools/libtests, shared with the
; examples/maps/mapscroll assets).
;----------------------------------------------------------------------

.section ".rodata2" semifree bank 2

mapdata:
.incbin "../../examples/maps/mapscroll/res/BG1.m16"
mapdata_end:

tilesetdef:
.incbin "../../examples/maps/mapscroll/res/tiledMario.t16"
tilesetdef_end:

tilesetatt:
.incbin "../../examples/maps/mapscroll/res/tiledMario.b16"
tilesetatt_end:

.ends
