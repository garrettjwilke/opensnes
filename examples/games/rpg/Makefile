OPENSNES := $(shell cd ../../.. && pwd)

TARGET   := rpg.sfc
ROM_NAME := RPG TEMPLATE
USE_LIB  := 1
LIB_MODULES := console dma background sprite text input collision

CSRC   := main.c
ASMSRC := data.asm

include $(OPENSNES)/make/common.mk

# AFTER the include (default-goal rule). The terrain tileset, the hero
# sheet and the dialog-box tiles are converted from the committed PNGs;
# the map binaries come from res/town.tmj via gen_assets.py.
res/tileset.pic res/tileset.pal: res/tileset.png
	@echo "[GFX] tileset..."
	@$(OPENSNES)/bin/gfx4snes -s 8 -o 16 -u 16 -p -m -i $<
# -s 16: the sheet holds 16x16 frames, so gfx4snes must cut 16x16
# blocks — see docs/tutorials/sprites.md. Converting it with -s 8
# happens to work only while the sheet is exactly 16 tiles wide.
res/hero.pic res/hero.pal: res/hero.png
	@echo "[GFX] hero..."
	@$(OPENSNES)/bin/gfx4snes -s 16 -p -i $<
res/uibox.pic res/uibox.pal: res/uibox.png
	@echo "[GFX] uibox..."
	@$(OPENSNES)/bin/gfx4snes -s 8 -o 16 -u 16 -p -m -i $<

clean: clean-gfx
.PHONY: clean-gfx
clean-gfx:
	@rm -f res/tileset.pic res/tileset.pal res/tileset.inc res/tileset_data.as res/tileset.map \
	       res/hero.pic res/hero.pal res/hero.inc res/hero_data.as \
	       res/uibox.pic res/uibox.pal res/uibox.inc res/uibox_data.as
