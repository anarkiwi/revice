/*
 * screen_core.c - C64 screenscrape core. See revice_screen.h.
 *
 * Logic-preserving port of the asid-vice fork's c64screen_get_state()
 * (src/c64/c64screen.c) and mon_screen_show() (src/monitor/mon_screen.c).
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See LICENSE for copyright notice (GPL-2.0-or-later).
 */

#include "revice_screen.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* Video mode from D011 bits 5-6 + D016 bit 4 (same formula as
   vicii_update_video_mode()). */
static uint8_t derive_vic_mode(const screen_inputs_t *in)
{
    return (uint8_t)(((in->d011 & 0x60) | (in->d016 & 0x10)) >> 4);
}

/* CIA2 PA bits 0-1 select the VIC bank, kept in vbank_phi2. */
static uint8_t derive_vic_bank(const screen_inputs_t *in)
{
    return (uint8_t)((in->vbank_phi2 >> 14) & 0x03);
}

int screen_pack(const screen_inputs_t *in, uint8_t *buf, uint32_t buflen,
                uint32_t *out_len)
{
    uint8_t  vic_mode;
    uint8_t  vic_bank;
    uint16_t screen_addr;
    uint16_t charset_addr;
    uint16_t bitmap_addr;
    uint16_t charset_vic_addr;
    int      charset_in_chargen_window;
    uint8_t  charset_kind;
    uint8_t *screen_dst;
    uint8_t *color_dst;
    uint8_t *charset_dst;
    int      i;

    if (in == NULL || buf == NULL || out_len == NULL) {
        return -1;
    }
    if (buflen < SCREEN_RESPONSE_SIZE) {
        return -1;
    }

    vic_mode = derive_vic_mode(in);
    vic_bank = derive_vic_bank(in);

    /* Screen RAM base: D018 bits 4-7 give a 1 KiB offset within the VIC bank. */
    screen_addr = (uint16_t)(in->vbank_phi2 + ((in->d018 & 0xf0) << 6));

    /* Charset/bitmap base: D018 bits 1-3 give a 2 KiB offset within the bank. */
    charset_vic_addr = (uint16_t)(((in->d018 & 0x0e) << 10) + in->vbank_phi1);
    charset_addr = charset_vic_addr;

    /* Bitmap base: D018 bit 3 selects lower/upper 8 KiB of the VIC bank. */
    bitmap_addr = (uint16_t)(in->vbank_phi1 + ((in->d018 & 0x08) << 10));

    /* Charset address inside the chargen-ROM window -> VIC reads ROM. */
    charset_in_chargen_window =
        ((charset_vic_addr & in->chargen_mask_phi1) == in->chargen_value_phi1);

    if (charset_in_chargen_window) {
        if ((charset_vic_addr & 0x0800) == 0) {
            charset_kind = SCREEN_CHARSET_ROM_UPPER_GFX;
        } else {
            charset_kind = SCREEN_CHARSET_ROM_UPPER_LOWER;
        }
    } else {
        charset_kind = SCREEN_CHARSET_RAM;
    }

    /* ---- Header (24 bytes) ---- */
    memset(buf, 0, SCREEN_HEADER_BYTES);
    buf[0]  = vic_mode;
    buf[1]  = SCREEN_TEXT_ROWS;
    buf[2]  = SCREEN_TEXT_COLS;
    buf[3]  = charset_kind;
    buf[4]  = vic_bank;
    buf[5]  = (uint8_t)(in->d020 & 0x0f);  /* border colour */
    buf[6]  = (uint8_t)(in->d021 & 0x0f);  /* bg #0 */
    buf[7]  = (uint8_t)(in->d022 & 0x0f);  /* bg #1 */
    buf[8]  = (uint8_t)(in->d023 & 0x0f);  /* bg #2 */
    buf[9]  = (uint8_t)(in->d024 & 0x0f);  /* bg #3 */
    buf[10] = in->d011;
    buf[11] = in->d016;
    buf[12] = in->d018;
    /* buf[13] reserved */
    put_le16(&buf[14], screen_addr);
    put_le16(&buf[16], charset_addr);
    /* VICII_IS_BITMAP_MODE(x) == (x & 0x02) */
    put_le16(&buf[18], (vic_mode & 0x02) ? bitmap_addr : (uint16_t)0);
    put_le32(&buf[20],
             SCREEN_TEXT_CELLS + SCREEN_TEXT_CELLS + SCREEN_CHARSET_BYTES);

    screen_dst  = buf + SCREEN_HEADER_BYTES;
    color_dst   = screen_dst + SCREEN_TEXT_CELLS;
    charset_dst = color_dst  + SCREEN_TEXT_CELLS;

    /* ---- Screen RAM (1000 bytes) ---- */
    for (i = 0; i < SCREEN_TEXT_CELLS; i++) {
        screen_dst[i] = in->read_ram(in->ctx, (uint16_t)(screen_addr + i));
    }

    /* ---- Color RAM (1000 bytes, low nibble = fg colour) ---- */
    for (i = 0; i < SCREEN_TEXT_CELLS; i++) {
        color_dst[i] = (uint8_t)(in->read_colorram(in->ctx,
                                 (uint16_t)(0xd800 + i)) & 0x0f);
    }

    /* ---- Character set (2048 bytes) ---- */
    if (charset_in_chargen_window) {
        uint16_t rom_base = (uint16_t)(charset_vic_addr & 0x0800);
        for (i = 0; i < SCREEN_CHARSET_BYTES; i++) {
            charset_dst[i] = in->read_chargen(in->ctx,
                                              (uint16_t)(rom_base + i));
        }
    } else {
        for (i = 0; i < SCREEN_CHARSET_BYTES; i++) {
            charset_dst[i] = in->read_ram(in->ctx,
                                          (uint16_t)(charset_addr + i));
        }
    }

    *out_len = SCREEN_RESPONSE_SIZE;
    return 0;
}

/* ---- renderer ---- */

char screen_screencode_to_ascii(uint8_t sc)
{
    sc &= 0x7f;                       /* strip reverse-video */
    if (sc == 0x00) {
        return '@';
    }
    if (sc <= 0x1a) {
        return (char)('A' + sc - 1);
    }
    if (sc == 0x1b) { return '['; }
    if (sc == 0x1c) { return '\\'; }
    if (sc == 0x1d) { return ']'; }
    if (sc == 0x1e) { return '^'; }
    if (sc == 0x1f) { return '_'; }
    if (sc >= 0x20 && sc <= 0x3f) {
        return (char)sc;
    }
    return '.';
}

static const char *vic_mode_name(uint8_t m)
{
    switch (m) {
        case 0: return "normal-text";
        case 1: return "multicolor-text";
        case 2: return "hires-bitmap";
        case 3: return "multicolor-bitmap";
        case 4: return "extended-text";
        case 5: return "illegal-text";
        case 6: return "illegal-bitmap-1";
        case 7: return "illegal-bitmap-2";
        default: return "?";
    }
}

static const char *charset_kind_name(uint8_t k)
{
    switch (k) {
        case SCREEN_CHARSET_ROM_UPPER_GFX:   return "ROM upper/graphics";
        case SCREEN_CHARSET_ROM_UPPER_LOWER: return "ROM upper/lowercase";
        case SCREEN_CHARSET_RAM:             return "custom (RAM)";
        default: return "?";
    }
}

struct emit_ctx {
    void (*emit)(void *ctx, const char *str);
    void *ctx;
};

#if defined(__GNUC__)
static void e_outf(struct emit_ctx *e, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
#endif

static void e_outf(struct emit_ctx *e, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    e->emit(e->ctx, buf);
}

static void show_state_header(struct emit_ctx *e, const uint8_t *hdr)
{
    uint16_t screen_addr  = (uint16_t)hdr[14] | ((uint16_t)hdr[15] << 8);
    uint16_t charset_addr = (uint16_t)hdr[16] | ((uint16_t)hdr[17] << 8);
    uint16_t bitmap_addr  = (uint16_t)hdr[18] | ((uint16_t)hdr[19] << 8);
    uint32_t payload_len  = (uint32_t)hdr[20] | ((uint32_t)hdr[21] << 8) |
                            ((uint32_t)hdr[22] << 16) | ((uint32_t)hdr[23] << 24);
    e_outf(e, "screen: vic_mode=%s rows=%u cols=%u vic_bank=%u\n",
           vic_mode_name(hdr[0]), hdr[1], hdr[2], hdr[4]);
    e_outf(e, "        screen=$%04x charset=$%04x bitmap=$%04x\n",
           screen_addr, charset_addr, bitmap_addr);
    e_outf(e, "        charset_kind=%s   payload_bytes=%u\n",
           charset_kind_name(hdr[3]), (unsigned)payload_len);
    e_outf(e, "        D011=$%02x D016=$%02x D018=$%02x\n",
           hdr[10], hdr[11], hdr[12]);
    e_outf(e, "        border=%u bg0=%u bg1=%u bg2=%u bg3=%u\n",
           hdr[5], hdr[6], hdr[7], hdr[8], hdr[9]);
}

void screen_render(const uint8_t *buf, uint32_t len, int raw,
                   void (*emit)(void *ctx, const char *str), void *ctx)
{
    struct emit_ctx e;
    int rows, cols, r, c;
    const uint8_t *screen;

    e.emit = emit;
    e.ctx = ctx;

    if (buf == NULL || len < SCREEN_HEADER_BYTES) {
        e_outf(&e, "screen: response too short (%u bytes)\n", (unsigned)len);
        return;
    }

    show_state_header(&e, buf);

    rows = buf[1];
    cols = buf[2];
    if (rows <= 0 || cols <= 0 ||
        (uint32_t)(SCREEN_HEADER_BYTES + rows * cols) > len) {
        e_outf(&e, "screen: header reports rows=%d cols=%d which doesn't fit "
               "in %u-byte response\n", rows, cols, (unsigned)len);
        return;
    }

    screen = buf + SCREEN_HEADER_BYTES;

    if (raw) {
        e_outf(&e, "\nscreen RAM hex (first %d bytes):\n", rows * cols);
        for (r = 0; r < rows * cols; r++) {
            if ((r % 16) == 0) {
                e_outf(&e, "\n  +%03x:", r);
            }
            e_outf(&e, " %02x", screen[r]);
        }
        e_outf(&e, "\n");
    } else {
        e_outf(&e, "\n        +");
        for (c = 0; c < cols; c++) {
            e_outf(&e, "-");
        }
        e_outf(&e, "+\n");
        for (r = 0; r < rows; r++) {
            e_outf(&e, "  r%02d:  |", r);
            for (c = 0; c < cols; c++) {
                e_outf(&e, "%c", screen_screencode_to_ascii(screen[r * cols + c]));
            }
            e_outf(&e, "|\n");
        }
        e_outf(&e, "        +");
        for (c = 0; c < cols; c++) {
            e_outf(&e, "-");
        }
        e_outf(&e, "+\n");
        e_outf(&e, "(approximate ASCII; '.' = unmappable. Use 'screen raw' for hex.)\n");
    }
}
