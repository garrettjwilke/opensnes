/**
 * @file apu.h
 * @brief Raw APU (SPC700) access — IPL upload and execution
 *
 * The S-SMP boots into a 64-byte IPL ROM speaking a handshake protocol
 * over the APU I/O ports ($2140-$2143). This module drives it from C:
 * wait for boot, push a wla-spc700-assembled binary into APU RAM, and
 * start it. It is the modern, ABI-verified alternative to the legacy
 * snesmod path for projects that want direct DSP control.
 *
 * Build side: list your APU program in the example Makefile's `SPCSRC`
 * (e.g. `SPCSRC := player.spc700.asm`); common.mk assembles it with
 * wla-spc700 against templates/memmap_spc700.inc into a flat
 * `player.spc700.bin` you `.incbin` and upload.
 *
 * @code
 * extern u8 spc_image[], spc_image_end[];
 *
 * apuWaitBoot();
 * apuUpload(spc_image, 0x0200, (u16)(spc_image_end - spc_image));
 * apuExecute(0x0200);
 * @endcode
 *
 * @warning These calls BLOCK on the APU handshake. Run them at init,
 *   before enabling per-frame work. Do not mix this module with the
 *   snesmod module in one ROM — both own the APU.
 *
 * @author OpenSNES Team
 * @copyright MIT License
 */

#ifndef OPENSNES_APU_H
#define OPENSNES_APU_H

#include <snes/types.h>

/**
 * @brief Block until the IPL ROM signals readiness ($AA/$BB)
 *
 * Call once after reset before any upload. consoleInit() does not touch
 * the APU.
 */
void apuWaitBoot(void);

/**
 * @brief Upload a memory block into APU RAM via the IPL protocol
 *
 * @param src     Source in ROM/WRAM (far pointer — the bank byte is
 *                honoured, SUPERFREE sections are fine)
 * @param spcAddr Destination address in APU RAM (e.g. 0x0200)
 * @param size    Byte count
 */
void apuUpload(const u8 *src, u16 spcAddr, u16 size);

/**
 * @brief End the upload session and start APU execution
 *
 * @param spcAddr Entry point in APU RAM (typically the upload base)
 */
void apuExecute(u16 spcAddr);

#endif /* OPENSNES_APU_H */
