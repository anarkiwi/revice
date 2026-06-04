/*
 * test_screen.c - unit tests for the screenscrape core (pack + render).
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See LICENSE for copyright notice (GPL-2.0-or-later).
 */

#include "revice_screen.h"
#include "revice_test.h"

#include <stdint.h>
#include <string.h>

/* Synthetic C64 memory: 64 KiB RAM, 1000-byte color RAM, 4 KiB chargen ROM. */
typedef struct {
    uint8_t ram[0x10000];
    uint8_t colorram[1000];
    uint8_t chargen[4096];
} mem_t;

static uint8_t m_ram(void *c, uint16_t a) { return ((mem_t *)c)->ram[a]; }
static uint8_t m_color(void *c, uint16_t a) { return ((mem_t *)c)->colorram[a - 0xd800]; }
static uint8_t m_chargen(void *c, uint16_t o) { return ((mem_t *)c)->chargen[o & 0x0fff]; }

/* A default-config snapshot: bank 0, screen $0400, charset $1000 (chargen
   ROM upper/graphics), standard control registers. */
static void default_inputs(screen_inputs_t *in, mem_t *m)
{
    memset(in, 0, sizeof(*in));
    in->d011 = 0x1b;
    in->d016 = 0x08;
    in->d018 = 0x14;            /* screen $0400, charset $1000 */
    in->d020 = 0x0e;            /* border light blue */
    in->d021 = 0x06;            /* bg blue */
    in->d022 = 0x01;
    in->d023 = 0x02;
    in->d024 = 0x03;
    in->vbank_phi1 = 0x0000;
    in->vbank_phi2 = 0x0000;
    in->chargen_mask_phi1  = 0x7000;
    in->chargen_value_phi1 = 0x1000;
    in->read_ram = m_ram;
    in->read_colorram = m_color;
    in->read_chargen = m_chargen;
    in->ctx = m;
}

static void test_pack_default_header(void)
{
    mem_t m;
    screen_inputs_t in;
    uint8_t buf[SCREEN_RESPONSE_SIZE];
    uint32_t len = sizeof(buf);
    memset(&m, 0, sizeof(m));
    default_inputs(&in, &m);

    CHECK_EQ_INT(screen_pack(&in, buf, sizeof(buf), &len), 0);
    CHECK_EQ_INT(len, SCREEN_RESPONSE_SIZE);
    CHECK_EQ_INT(buf[0], 0);                 /* vic_mode normal-text */
    CHECK_EQ_INT(buf[1], 25);
    CHECK_EQ_INT(buf[2], 40);
    CHECK_EQ_INT(buf[3], SCREEN_CHARSET_ROM_UPPER_GFX);
    CHECK_EQ_INT(buf[4], 0);                 /* vic_bank 0 */
    CHECK_EQ_INT(buf[5], 0x0e);              /* border */
    CHECK_EQ_INT(buf[6], 0x06);              /* bg0 */
    CHECK_EQ_INT(buf[10], 0x1b);             /* D011 raw */
    CHECK_EQ_INT(buf[11], 0x08);             /* D016 raw */
    CHECK_EQ_INT(buf[12], 0x14);             /* D018 raw */
    /* screen_addr $0400 */
    CHECK_EQ_INT((uint16_t)buf[14] | (buf[15] << 8), 0x0400);
    /* charset_addr $1000 */
    CHECK_EQ_INT((uint16_t)buf[16] | (buf[17] << 8), 0x1000);
    /* bitmap_addr 0 in text mode */
    CHECK_EQ_INT((uint16_t)buf[18] | (buf[19] << 8), 0x0000);
    /* payload_len 4048 */
    {
        uint32_t pl = (uint32_t)buf[20] | ((uint32_t)buf[21] << 8) |
                      ((uint32_t)buf[22] << 16) | ((uint32_t)buf[23] << 24);
        CHECK_EQ_INT(pl, 4048);
    }
}

static void test_pack_payload(void)
{
    mem_t m;
    screen_inputs_t in;
    uint8_t buf[SCREEN_RESPONSE_SIZE];
    uint32_t len = sizeof(buf);
    int i;
    memset(&m, 0, sizeof(m));
    /* "HELLO" at top-left: screencodes H=8 E=5 L=12 L=12 O=15. */
    m.ram[0x0400] = 8;  m.ram[0x0401] = 5;  m.ram[0x0402] = 12;
    m.ram[0x0403] = 12; m.ram[0x0404] = 15;
    for (i = 0; i < 1000; i++) {
        m.colorram[i] = (uint8_t)((i & 0x0f) | 0xa0);   /* high nibble is noise */
    }
    for (i = 0; i < 4096; i++) {
        m.chargen[i] = (uint8_t)(i & 0xff);
    }
    default_inputs(&in, &m);
    CHECK_EQ_INT(screen_pack(&in, buf, sizeof(buf), &len), 0);

    /* screen RAM at offset 24 */
    CHECK_EQ_INT(buf[24 + 0], 8);
    CHECK_EQ_INT(buf[24 + 4], 15);
    /* color RAM at offset 24+1000, noise masked off */
    CHECK_EQ_INT(buf[24 + 1000 + 0], 0);
    CHECK_EQ_INT(buf[24 + 1000 + 15], 15);
    /* charset at offset 24+2000, from chargen ROM base 0 */
    CHECK_EQ_INT(buf[24 + 2000 + 0], 0);
    CHECK_EQ_INT(buf[24 + 2000 + 7], 7);
    CHECK_EQ_INT(buf[24 + 2000 + 255], 255);
}

static void test_pack_custom_ram_charset(void)
{
    mem_t m;
    screen_inputs_t in;
    uint8_t buf[SCREEN_RESPONSE_SIZE];
    uint32_t len = sizeof(buf);
    int i;
    memset(&m, 0, sizeof(m));
    for (i = 0; i < 2048; i++) {
        m.ram[0x3000 + i] = (uint8_t)(i & 0x7f);
    }
    default_inputs(&in, &m);
    in.d018 = 0x1c;            /* charset offset $3000 (outside chargen window) */
    CHECK_EQ_INT(screen_pack(&in, buf, sizeof(buf), &len), 0);
    CHECK_EQ_INT(buf[3], SCREEN_CHARSET_RAM);
    CHECK_EQ_INT((uint16_t)buf[16] | (buf[17] << 8), 0x3000);
    /* charset from RAM at $3000 */
    CHECK_EQ_INT(buf[24 + 2000 + 0], 0);
    CHECK_EQ_INT(buf[24 + 2000 + 9], 9);
}

static void test_pack_bitmap_mode(void)
{
    mem_t m;
    screen_inputs_t in;
    uint8_t buf[SCREEN_RESPONSE_SIZE];
    uint32_t len = sizeof(buf);
    memset(&m, 0, sizeof(m));
    default_inputs(&in, &m);
    in.d011 = 0x3b;            /* BMM set -> hires bitmap */
    in.d018 = 0x18;            /* bitmap base $2000 */
    CHECK_EQ_INT(screen_pack(&in, buf, sizeof(buf), &len), 0);
    CHECK_EQ_INT(buf[0], 2);   /* hires-bitmap */
    /* bitmap_addr now emitted (non-zero) */
    CHECK(((uint16_t)buf[18] | (buf[19] << 8)) != 0);
}

static void test_pack_buffer_too_small(void)
{
    mem_t m;
    screen_inputs_t in;
    uint8_t buf[10];
    uint32_t len = sizeof(buf);
    memset(&m, 0, sizeof(m));
    default_inputs(&in, &m);
    CHECK(screen_pack(&in, buf, sizeof(buf), &len) < 0);
}

static void test_screencode_to_ascii(void)
{
    CHECK_EQ_INT(screen_screencode_to_ascii(0x00), '@');
    CHECK_EQ_INT(screen_screencode_to_ascii(0x01), 'A');
    CHECK_EQ_INT(screen_screencode_to_ascii(0x1a), 'Z');
    CHECK_EQ_INT(screen_screencode_to_ascii(0x20), ' ');
    CHECK_EQ_INT(screen_screencode_to_ascii(0x80), '@');   /* reverse bit stripped */
    CHECK_EQ_INT(screen_screencode_to_ascii(0x40), '.');   /* unmappable */
}

/* render capture */
static char g_out[65536];
static void r_emit(void *ctx, const char *s)
{
    (void)ctx;
    size_t cur = strlen(g_out);
    size_t add = strlen(s);
    if (cur + add < sizeof(g_out)) {
        memcpy(g_out + cur, s, add + 1);
    }
}

static void test_render_grid(void)
{
    mem_t m;
    screen_inputs_t in;
    uint8_t buf[SCREEN_RESPONSE_SIZE];
    uint32_t len = sizeof(buf);
    memset(&m, 0, sizeof(m));
    m.ram[0x0400] = 8;  m.ram[0x0401] = 5;  m.ram[0x0402] = 12;
    m.ram[0x0403] = 12; m.ram[0x0404] = 15;
    default_inputs(&in, &m);
    screen_pack(&in, buf, sizeof(buf), &len);

    g_out[0] = '\0';
    screen_render(buf, len, 0, r_emit, NULL);
    CHECK(strstr(g_out, "HELLO") != NULL);
    CHECK(strstr(g_out, "vic_mode=normal-text") != NULL);
    CHECK(strstr(g_out, "charset_kind=ROM upper/graphics") != NULL);

    g_out[0] = '\0';
    screen_render(buf, len, 1, r_emit, NULL);
    CHECK(strstr(g_out, "screen RAM hex") != NULL);
}

int main(void)
{
    test_pack_default_header();
    test_pack_payload();
    test_pack_custom_ram_charset();
    test_pack_bitmap_mode();
    test_pack_buffer_too_small();
    test_screencode_to_ascii();
    test_render_grid();
    REVICE_TEST_MAIN_END();
}
