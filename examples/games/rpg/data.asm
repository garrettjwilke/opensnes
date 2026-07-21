;----------------------------------------------------------------------
; rpg — assets generated from the Tiled map (res/town.tmj) by
; gen_assets.py. The map is the single source of truth: terrain,
; per-tile collision and entity positions all come from it.
;
; town_collision is read from C by plain array indexing: post-#121
; const-qualified reads use far addressing, so the 4 KB map lives
; outside bank $00 with no ASM accessor — the SDK's dogfood of #121.
;----------------------------------------------------------------------

.section ".rodata_tiles" superfree
town_tiles:  .incbin "res/tileset.pic"
town_tiles_end:
.ends

.section ".rodata_townmap" superfree
town_map:    .incbin "res/town_map.bin"
town_map_end:
.ends

.section ".rodata_coll" superfree
town_collision: .incbin "res/town_collision.bin"
town_collision_end:
.ends

.section ".rodata_hero" superfree
hero_tiles:  .incbin "res/hero.pic"
hero_tiles_end:
.ends

.section ".rodata_ui" superfree
ui_tiles:    .incbin "res/uibox.pic"
ui_tiles_end:
.ends

; Palettes forced to bank $00 (dmaCopyCGram reads bank $00 only).
.section ".rpgpal" semifree bank 0
town_pal:  .incbin "res/tileset.pal"
town_pal_end:
hero_pal:  .incbin "res/hero.pal"
hero_pal_end:
npc_pal:   .incbin "res/npc.pal"
npc_pal_end:
ui_pal:    .incbin "res/uibox.pal"
ui_pal_end:
.ends
