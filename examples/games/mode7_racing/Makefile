OPENSNES := $(shell cd ../../.. && pwd)

TARGET   := mode7_racing.sfc
ROM_NAME := MODE7 RACING
USE_LIB  := 1
LIB_MODULES := console dma background sprite math mode7 input

CSRC   := main.c
ASMSRC := data.asm

include $(OPENSNES)/make/common.mk

# AFTER the include on purpose: the first rule in a Makefile is the
# default goal (common.mk provides `all`). Regenerates the Mode 7
# conversion when the committed source PNG changes.
res/track.pc7: res/track.png
	@echo "[GFX] Converting Mode 7 track..."
	@$(OPENSNES)/bin/gfx4snes -p -m -M 7 -i $<
