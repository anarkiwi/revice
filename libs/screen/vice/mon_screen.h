/*
 * mon_screen.h - Monitor screen-scrape command (VICE-facing API).
 *
 * Stable public contract for the in-tree wiring (src/c64/c64.c registers the
 * provider, src/monitor/monitor_binary.c serves the SCREEN_GET opcode). The
 * machine-agnostic shell in mon_screen.c forwards rendering to the revice
 * screen core (libs/screen); the C64 provider in c64screen.c forwards packing
 * to the same core. Symbol names + the wire size are unchanged from the
 * original asid-vice fork.
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice.
 */

#ifndef VICE_MON_SCREEN_H
#define VICE_MON_SCREEN_H

#include "types.h"

#define MON_SCREEN_HEADER_BYTES   24
#define MON_SCREEN_TEXT_COLS      40
#define MON_SCREEN_TEXT_ROWS      25
#define MON_SCREEN_TEXT_CELLS     (MON_SCREEN_TEXT_COLS * MON_SCREEN_TEXT_ROWS)
#define MON_SCREEN_CHARSET_BYTES  2048
#define MON_SCREEN_BINMON_GET_RESPONSE_SIZE \
    (MON_SCREEN_HEADER_BYTES + MON_SCREEN_TEXT_CELLS * 2 + MON_SCREEN_CHARSET_BYTES)

#define MON_SCREEN_CHARSET_ROM_UPPER_GFX    0
#define MON_SCREEN_CHARSET_ROM_UPPER_LOWER  1
#define MON_SCREEN_CHARSET_RAM              2

/* Per-machine provider callback (filled with the wire-format response). */
typedef int (*mon_screen_provider_t)(uint8_t *buf, uint32_t buf_len,
                                     uint32_t *out_len);

void mon_screen_register_provider(mon_screen_provider_t provider);
void mon_screen_show(const char *args);
int mon_screen_binmon_get(uint8_t *out, uint32_t *out_length);

#endif
