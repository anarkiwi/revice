/*
 * revice_screen.h - C64 screenscrape core (pure), extracted from the asid-vice
 *                   fork's src/c64/c64screen.c + src/monitor/mon_screen.c.
 *
 * screen_pack() turns a snapshot of VIC-II registers + memory into the fixed
 * 4072-byte SCREEN_GET wire response (header + screen RAM + color RAM + the
 * active 2 KiB character set). screen_render() turns that buffer back into the
 * text-monitor's 40x25 ASCII grid (or a hex dump). Neither touches VICE: the
 * register values arrive in a struct and memory is read through callbacks, so
 * the byte layout is unit-tested with synthetic inputs.
 *
 * The VICE adapters gather the VIC-II regs, main RAM, color RAM and chargen
 * ROM into the inputs struct (vice/c64screen.c) and keep the provider-callback
 * plumbing (vice/mon_screen.c), so the in-tree wiring (c64.c registration,
 * monitor_binary.c SCREEN_GET opcode) does not change.
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See LICENSE for copyright notice (GPL-2.0-or-later).
 */

#ifndef REVICE_SCREEN_H
#define REVICE_SCREEN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCREEN_HEADER_BYTES   24
#define SCREEN_TEXT_COLS      40
#define SCREEN_TEXT_ROWS      25
#define SCREEN_TEXT_CELLS     (SCREEN_TEXT_COLS * SCREEN_TEXT_ROWS)  /* 1000 */
#define SCREEN_CHARSET_BYTES  2048
#define SCREEN_RESPONSE_SIZE \
    (SCREEN_HEADER_BYTES + SCREEN_TEXT_CELLS * 2 + SCREEN_CHARSET_BYTES) /* 4072 */

/* charset_kind values (header byte 3). */
#define SCREEN_CHARSET_ROM_UPPER_GFX    0
#define SCREEN_CHARSET_ROM_UPPER_LOWER  1
#define SCREEN_CHARSET_RAM              2

/* Snapshot of everything screen_pack() needs. Register fields are the raw
   VIC-II register bytes; vbank_* and chargen_* are the VIC-II address-decode
   helpers VICE keeps in sync with CIA2 / the chargen ROM window. */
typedef struct {
    uint8_t  d011, d016, d018;     /* control / memory pointers       */
    uint8_t  d020, d021, d022, d023, d024;  /* border + bg colours    */
    uint16_t vbank_phi1;           /* VIC fetch bank base (phi1)      */
    uint16_t vbank_phi2;           /* VIC fetch bank base (phi2)      */
    uint16_t chargen_mask_phi1;    /* vicii.vaddr_chargen_mask_phi1   */
    uint16_t chargen_value_phi1;   /* vicii.vaddr_chargen_value_phi1  */

    /* Memory readers. read_ram: CPU-equivalent 16-bit address into main RAM.
       read_colorram: called with 0xd800+i. read_chargen: 0..4095 offset into
       the 4 KiB chargen ROM. */
    uint8_t (*read_ram)(void *ctx, uint16_t addr);
    uint8_t (*read_colorram)(void *ctx, uint16_t addr);
    uint8_t (*read_chargen)(void *ctx, uint16_t offset);
    void    *ctx;
} screen_inputs_t;

/* Build the fixed-layout SCREEN_GET response into buf (>= SCREEN_RESPONSE_SIZE).
   On success returns 0 and sets *out_len to SCREEN_RESPONSE_SIZE. Returns
   negative on a null/too-small buffer. */
int screen_pack(const screen_inputs_t *in, uint8_t *buf, uint32_t buflen,
                uint32_t *out_len);

/* Render a packed response for the text monitor via the emit callback. When
   raw is non-zero, dumps screen RAM as hex; otherwise draws the 40x25 ASCII
   grid. A one-line state header is always emitted first. */
void screen_render(const uint8_t *buf, uint32_t len, int raw,
                   void (*emit)(void *ctx, const char *str), void *ctx);

/* Map a C64 screencode to the closest printable ASCII (or '.'), reverse-video
   bit ignored. Exposed for the renderer and tests. */
char screen_screencode_to_ascii(uint8_t sc);

#ifdef __cplusplus
}
#endif

#endif /* REVICE_SCREEN_H */
