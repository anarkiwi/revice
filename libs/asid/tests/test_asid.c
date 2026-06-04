/*
 * test_asid.c - golden-byte unit tests for the ASID protocol codec.
 *
 * The expected message bytes here were derived by hand from the asid-vice
 * fork's soundasid.c so the extraction is provably wire-compatible.
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See LICENSE for copyright notice (GPL-2.0-or-later).
 */

#include "revice_asid.h"
#include "revice_test.h"

#include <stdint.h>

typedef struct {
    uint8_t  chip;
    uint8_t  bytes[ASID_BUFFER_SIZE];
    size_t   len;
    uint64_t nsec;
} emitted_t;

typedef struct {
    emitted_t msg[64];
    int       count;
} capture_t;

static void cap_emit(void *ctx, uint8_t chip, const uint8_t *msg, size_t len,
                     uint64_t nsec)
{
    capture_t *c = (capture_t *)ctx;
    emitted_t *e = &c->msg[c->count++];
    e->chip = chip;
    e->len = len;
    e->nsec = nsec;
    memcpy(e->bytes, msg, len);
}

/* Standard "update" form for a single register write, no MSB. */
static void test_standard_update_single_reg(void)
{
    asid_core_t core;
    capture_t cap = {0};
    /* f0 2d 4e | mask[4] | msb[4] | payload | f7 ; reg 0 = regmap index 0. */
    const uint8_t expect[] = {
        0xf0, 0x2d, 0x4e,
        0x01, 0x00, 0x00, 0x00,   /* mask: bit 0 set */
        0x00, 0x00, 0x00, 0x00,   /* msb: none */
        0x42,                     /* payload: value */
        0xf7
    };
    asid_core_init(&core, 0);
    asid_core_set_reg(&core, 0, 0, 0x42, cap_emit, &cap);
    CHECK_EQ_INT(cap.count, 0);            /* set_reg alone does not emit */
    asid_core_flush(&core, 0, 0, cap_emit, &cap);
    CHECK_EQ_INT(cap.count, 1);
    CHECK_EQ_INT(cap.msg[0].chip, 0);
    CHECK_EQ_INT(cap.msg[0].len, sizeof(expect));
    CHECK_MEM(cap.msg[0].bytes, expect, sizeof(expect));
    /* Nothing dirty now: a second flush is a no-op. */
    asid_core_flush(&core, 0, 0, cap_emit, &cap);
    CHECK_EQ_INT(cap.count, 1);
}

/* A value > 0x7f sets the corresponding MSB bit and the payload carries the
   low 7 bits. */
static void test_standard_update_msb(void)
{
    asid_core_t core;
    capture_t cap = {0};
    const uint8_t expect[] = {
        0xf0, 0x2d, 0x4e,
        0x01, 0x00, 0x00, 0x00,   /* mask bit 0 */
        0x01, 0x00, 0x00, 0x00,   /* msb bit 0  */
        0x00,                     /* 0x80 & 0x7f */
        0xf7
    };
    asid_core_init(&core, 0);
    asid_core_set_reg(&core, 0, 0, 0x80, cap_emit, &cap);
    asid_core_flush(&core, 0, 0, cap_emit, &cap);
    CHECK_EQ_INT(cap.count, 1);
    CHECK_EQ_INT(cap.msg[0].len, sizeof(expect));
    CHECK_MEM(cap.msg[0].bytes, expect, sizeof(expect));
}

/* Per-register short form, and the byte-saving accounting. */
static void test_update_reg_short_form(void)
{
    asid_core_t core;
    capture_t cap = {0};
    /* f0 2d 6c | reg val | f7 */
    const uint8_t expect_short[] = { 0xf0, 0x2d, 0x6c, 0x00, 0x42, 0xf7 };
    asid_core_init(&core, 1);              /* use_update_reg = true */
    asid_core_set_reg(&core, 0, 0, 0x42, cap_emit, &cap);
    asid_core_flush(&core, 0, 0, cap_emit, &cap);
    CHECK_EQ_INT(cap.count, 1);
    CHECK_EQ_INT(cap.msg[0].len, sizeof(expect_short));
    CHECK_MEM(cap.msg[0].bytes, expect_short, sizeof(expect_short));
    /* Standard form would have been 13 bytes; short form is 6 -> 7 saved. */
    CHECK_EQ_INT(core.bytes_saved, 7);
}

/* MSB folds into bit 6 of the register byte in the short form. */
static void test_update_reg_short_form_msb(void)
{
    asid_core_t core;
    capture_t cap = {0};
    const uint8_t expect_short[] = { 0xf0, 0x2d, 0x6c, 0x40, 0x00, 0xf7 };
    asid_core_init(&core, 1);
    asid_core_set_reg(&core, 0, 0, 0x80, cap_emit, &cap);
    asid_core_flush(&core, 0, 0, cap_emit, &cap);
    CHECK_EQ_INT(cap.count, 1);
    CHECK_MEM(cap.msg[0].bytes, expect_short, sizeof(expect_short));
}

/* Short form emits in regmap order: control registers (4/11/18) come last so
   a voice's freq/PW/ADSR are applied before its gate edge, matching the
   standard update form. */
static void test_update_reg_control_last(void)
{
    asid_core_t core;
    capture_t cap = {0};
    asid_core_init(&core, 1);
    /* Write the control register first, then an envelope register; the short
       form must still place the control register after the envelope. */
    asid_core_set_reg(&core, 0, 4, 0x21, cap_emit, &cap);  /* v1 control/gate */
    asid_core_set_reg(&core, 0, 5, 0x11, cap_emit, &cap);  /* v1 attack/decay */
    asid_core_flush(&core, 0, 0, cap_emit, &cap);
    CHECK_EQ_INT(cap.count, 1);
    /* f0 2d 6c | (5,0x11) (4,0x21) | f7 */
    CHECK_EQ_INT(cap.msg[0].len, 8);
    CHECK_EQ_INT(cap.msg[0].bytes[2], 0x6c);
    CHECK_EQ_INT(cap.msg[0].bytes[3], 5);      /* envelope reg first */
    CHECK_EQ_INT(cap.msg[0].bytes[4], 0x11);
    CHECK_EQ_INT(cap.msg[0].bytes[5], 4);      /* control reg last */
    CHECK_EQ_INT(cap.msg[0].bytes[6], 0x21);
    CHECK_EQ_INT(cap.msg[0].bytes[7], 0xf7);
}

/* regmask clamps each register to its writable bits before anything else. */
static void test_regmask_clamps(void)
{
    asid_core_t core;
    capture_t cap = {0};
    asid_core_init(&core, 1);
    /* reg 3 mask 0x0f, reg 21 mask 0x07. */
    asid_core_set_reg(&core, 0, 3, 0xff, cap_emit, &cap);
    asid_core_set_reg(&core, 0, 21, 0xff, cap_emit, &cap);
    asid_core_flush(&core, 0, 0, cap_emit, &cap);
    CHECK_EQ_INT(cap.count, 1);
    /* Short form pairs are (reg, value) in ascending register order. */
    /* reg 3 -> 0x0f, reg 21 -> 0x07 */
    CHECK_EQ_INT(cap.msg[0].bytes[0], 0xf0);
    CHECK_EQ_INT(cap.msg[0].bytes[1], 0x2d);
    CHECK_EQ_INT(cap.msg[0].bytes[2], 0x6c);
    CHECK_EQ_INT(cap.msg[0].bytes[3], 3);
    CHECK_EQ_INT(cap.msg[0].bytes[4], 0x0f);
    CHECK_EQ_INT(cap.msg[0].bytes[5], 21);
    CHECK_EQ_INT(cap.msg[0].bytes[6], 0x07);
    CHECK_EQ_INT(cap.msg[0].bytes[7], 0xf7);
    CHECK_EQ_INT(cap.msg[0].len, 8);
}

/* No-op writes (value unchanged) never dirty the register. */
static void test_noop_write_ignored(void)
{
    asid_core_t core;
    capture_t cap = {0};
    asid_core_init(&core, 0);
    asid_core_set_reg(&core, 0, 0, 0x00, cap_emit, &cap);  /* already 0 */
    asid_core_flush(&core, 0, 0, cap_emit, &cap);
    CHECK_EQ_INT(cap.count, 0);                            /* nothing dirty */
}

/* Re-writing a control register (4/11/18) while it is still dirty forces a
   synchronous flush of the prior value (nsec 0) before storing the new one. */
static void test_control_register_forces_flush(void)
{
    asid_core_t core;
    capture_t cap = {0};
    const uint8_t expect_first[] = {
        0xf0, 0x2d, 0x4e,
        0x00, 0x00, 0x00, 0x02,   /* reg 4 is regmap index 22 -> bit 22 */
        0x00, 0x00, 0x00, 0x00,
        0x10,
        0xf7
    };
    asid_core_init(&core, 0);
    asid_core_set_reg(&core, 0, 4, 0x10, cap_emit, &cap);  /* first: no flush */
    CHECK_EQ_INT(cap.count, 0);
    asid_core_set_reg(&core, 0, 4, 0x11, cap_emit, &cap);  /* forces flush */
    CHECK_EQ_INT(cap.count, 1);
    CHECK_EQ_INT(cap.msg[0].nsec, 0);
    CHECK_EQ_INT(cap.msg[0].len, sizeof(expect_first));
    CHECK_MEM(cap.msg[0].bytes, expect_first, sizeof(expect_first));
    /* The new value is now pending; an explicit flush emits it. */
    asid_core_flush(&core, 0, 123, cap_emit, &cap);
    CHECK_EQ_INT(cap.count, 2);
    CHECK_EQ_INT(cap.msg[1].nsec, 123);
    CHECK_EQ_INT(cap.msg[1].bytes[11], 0x11);
}

/* The two chips have independent state and distinct update opcodes. */
static void test_two_chips_independent(void)
{
    asid_core_t core;
    capture_t cap = {0};
    asid_core_init(&core, 0);
    asid_core_set_reg(&core, 0, 0, 0x11, cap_emit, &cap);
    asid_core_set_reg(&core, 1, 0, 0x22, cap_emit, &cap);
    asid_core_flush(&core, 0, 0, cap_emit, &cap);
    asid_core_flush(&core, 1, 0, cap_emit, &cap);
    CHECK_EQ_INT(cap.count, 2);
    CHECK_EQ_INT(cap.msg[0].bytes[2], 0x4e);   /* chip 0 update opcode */
    CHECK_EQ_INT(cap.msg[0].bytes[11], 0x11);
    CHECK_EQ_INT(cap.msg[1].bytes[2], 0x50);   /* chip 1 update opcode */
    CHECK_EQ_INT(cap.msg[1].bytes[11], 0x22);
}

/* Out-of-range chip/reg writes are ignored, not crashes or overruns. */
static void test_bounds(void)
{
    asid_core_t core;
    capture_t cap = {0};
    asid_core_init(&core, 0);
    asid_core_set_reg(&core, ASID_CHIPS, 0, 0x42, cap_emit, &cap);
    asid_core_set_reg(&core, 0, ASID_NUM_REGS, 0x42, cap_emit, &cap);
    asid_core_flush(&core, 0, 0, cap_emit, &cap);
    CHECK_EQ_INT(cap.count, 0);
}

static void test_fixed_messages(void)
{
    const uint8_t start[] = { 0xf0, 0x2d, 0x4c, 0xf7 };
    const uint8_t stop[]  = { 0xf0, 0x2d, 0x4d, 0xf7 };
    CHECK_MEM(asid_core_start_msg, start, sizeof(start));
    CHECK_MEM(asid_core_stop_msg, stop, sizeof(stop));
}

static void test_timing_helpers(void)
{
    CHECK(asid_should_flush_on_irq(1000, 0));         /* diff 1000 > 256 */
    CHECK(!asid_should_flush_on_irq(100, 0));         /* diff 100  <= 256 */
    CHECK(!asid_should_flush_on_irq(256, 0));         /* boundary: not > */

    /* clock_to_nanos uses the machine clock: cycles_per_sec cycles == 1s. */
    CHECK_EQ_INT(asid_clock_to_nanos(0, 985248), 0);
    CHECK_EQ_INT(asid_clock_to_nanos(985248, 985248), 1000000000ULL);   /* PAL  */
    CHECK_EQ_INT(asid_clock_to_nanos(1022727, 1022727), 1000000000ULL); /* NTSC */
    /* NTSC cycles are shorter, so a fixed cycle count is fewer ns on NTSC. */
    CHECK(asid_clock_to_nanos(100000, 1022727) < asid_clock_to_nanos(100000, 985248));
    /* cycles_per_sec == 0 falls back to PAL. */
    CHECK_EQ_INT(asid_clock_to_nanos(985248, 0), asid_clock_to_nanos(985248, 985248));
}

int main(void)
{
    test_standard_update_single_reg();
    test_standard_update_msb();
    test_update_reg_short_form();
    test_update_reg_short_form_msb();
    test_update_reg_control_last();
    test_regmask_clamps();
    test_noop_write_ignored();
    test_control_register_forces_flush();
    test_two_chips_independent();
    test_bounds();
    test_fixed_messages();
    test_timing_helpers();
    REVICE_TEST_MAIN_END();
}
