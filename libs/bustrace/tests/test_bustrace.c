/*
 * test_bustrace.c - golden-byte + property unit tests for the bus-trace core.
 *
 * The expected bytes are spelled out by hand from the documented format in
 * revice_bustrace.h so the serializer is provably wire-stable. Determinism is
 * asserted directly: the same access sequence must produce a byte-identical
 * stream across independent runs.
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See LICENSE for copyright notice (GPL-2.0-or-later).
 */

#include "revice_bustrace.h"
#include "revice_test.h"

#include <stdint.h>
#include <string.h>

#define CAP_MAX 4096

typedef struct {
    uint8_t buf[CAP_MAX];
    size_t  len;
} capture_t;

static void cap_emit(void *ctx, const uint8_t *buf, size_t len)
{
    capture_t *c = (capture_t *)ctx;
    if (c->len + len <= CAP_MAX) {
        memcpy(c->buf + c->len, buf, len);
    }
    c->len += len;
}

/* Header bytes are exactly as documented. */
static void test_header_layout(void)
{
    capture_t cap = {0};
    revice_bustrace_t bt;
    const uint8_t expect[REVICE_BUSTRACE_HEADER_SIZE] = {
        'R', 'B', 'T', '1',
        0x01, 0x00,                 /* version 1, le */
        0x0a,                       /* rec_size 10 */
        0x01,                       /* flags: HAS_PC */
        /* cycles_per_sec = 985248 = 0x000F08A0, le over 8 bytes */
        0xa0, 0x08, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    revice_bustrace_begin(&bt, 985248ull, cap_emit, &cap);
    CHECK_EQ_INT(cap.len, REVICE_BUSTRACE_HEADER_SIZE);
    CHECK_MEM(cap.buf, expect, sizeof(expect));
}

/* A single write record: delta from 0, addr, val, rw=write, pc. */
static void test_single_record_layout(void)
{
    capture_t cap = {0};
    revice_bustrace_t bt;
    /* header(16) + one record(10) + trailer(16) = 42 */
    const uint8_t rec[REVICE_BUSTRACE_REC_SIZE] = {
        0x64, 0x00, 0x00, 0x00,     /* cycle_delta 100 */
        0x00, 0xd4,                 /* addr 0xd400 le */
        0x21,                       /* val */
        0x01,                       /* rw: write */
        0x34, 0x12                  /* pc 0x1234 le */
    };
    revice_bustrace_begin(&bt, 0, cap_emit, &cap);
    revice_bustrace_access(&bt, 100, 0xd400, 0x21,
                           REVICE_BUSTRACE_RW_WRITE, 0x1234);
    CHECK_EQ_INT(cap.len, REVICE_BUSTRACE_HEADER_SIZE + REVICE_BUSTRACE_REC_SIZE);
    CHECK_MEM(cap.buf + REVICE_BUSTRACE_HEADER_SIZE, rec, sizeof(rec));
    revice_bustrace_end(&bt);
    CHECK_EQ_INT(cap.len, REVICE_BUSTRACE_HEADER_SIZE
                          + REVICE_BUSTRACE_REC_SIZE
                          + REVICE_BUSTRACE_TRAILER_SIZE);
}

/* Cycles are delta-encoded against the previous record's absolute cycle. */
static void test_cycle_deltas(void)
{
    capture_t cap = {0};
    revice_bustrace_t bt;
    const uint8_t *r0, *r1, *r2;
    revice_bustrace_begin(&bt, 0, cap_emit, &cap);
    revice_bustrace_access(&bt, 10, 0x1000, 0x00, 0, 0x0800);
    revice_bustrace_access(&bt, 25, 0x1001, 0x01, 0, 0x0801);
    revice_bustrace_access(&bt, 25, 0x1002, 0x02, 0, 0x0802); /* same cycle */
    r0 = cap.buf + REVICE_BUSTRACE_HEADER_SIZE;
    r1 = r0 + REVICE_BUSTRACE_REC_SIZE;
    r2 = r1 + REVICE_BUSTRACE_REC_SIZE;
    CHECK_EQ_INT(r0[0], 10);    /* 10 - 0  */
    CHECK_EQ_INT(r1[0], 15);    /* 25 - 10 */
    CHECK_EQ_INT(r2[0], 0);     /* 25 - 25 */
}

/* Trailer carries the record count and a CRC over only the record bytes; an
   independent CRC of the captured records must match. */
static void test_trailer_count_and_crc(void)
{
    capture_t cap = {0};
    revice_bustrace_t bt;
    const uint8_t *tr;
    uint64_t count;
    uint32_t crc_field, crc_calc;
    size_t recs_len;
    int i;

    revice_bustrace_begin(&bt, 985248ull, cap_emit, &cap);
    for (i = 0; i < 5; ++i) {
        revice_bustrace_access(&bt, (uint64_t)(i * 7), (uint16_t)(0xd400 + i),
                               (uint8_t)i, REVICE_BUSTRACE_RW_WRITE,
                               (uint16_t)(0x1000 + i));
    }
    revice_bustrace_end(&bt);

    tr = cap.buf + cap.len - REVICE_BUSTRACE_TRAILER_SIZE;
    CHECK_EQ_INT(tr[0], 'R');
    CHECK_EQ_INT(tr[1], 'B');
    CHECK_EQ_INT(tr[2], 'T');
    CHECK_EQ_INT(tr[3], 'e');

    count = 0;
    for (i = 0; i < 8; ++i) {
        count |= (uint64_t)tr[4 + i] << (8 * i);
    }
    CHECK_EQ_INT((long)count, 5);

    crc_field = (uint32_t)tr[12] | ((uint32_t)tr[13] << 8)
              | ((uint32_t)tr[14] << 16) | ((uint32_t)tr[15] << 24);
    recs_len = 5 * REVICE_BUSTRACE_REC_SIZE;
    crc_calc = revice_bustrace_crc32(0, cap.buf + REVICE_BUSTRACE_HEADER_SIZE,
                                     recs_len);
    CHECK_EQ_INT((long)crc_field, (long)crc_calc);
}

/* The whole point: identical access sequences -> byte-identical streams. */
static void test_determinism(void)
{
    capture_t a = {0};
    capture_t b = {0};
    revice_bustrace_t bt;
    int run, i;
    capture_t *caps[2];
    caps[0] = &a;
    caps[1] = &b;
    for (run = 0; run < 2; ++run) {
        revice_bustrace_begin(&bt, 1022727ull, cap_emit, caps[run]);
        for (i = 0; i < 256; ++i) {
            uint8_t rw = (i & 1) ? REVICE_BUSTRACE_RW_WRITE : 0;
            if ((i % 8) == 0) {
                rw |= REVICE_BUSTRACE_RW_OPCODE;
            }
            revice_bustrace_access(&bt, (uint64_t)(i * 3),
                                   (uint16_t)(0x0800 + (i * 5)),
                                   (uint8_t)(i * 13), rw,
                                   (uint16_t)(0x1000 + i));
        }
        revice_bustrace_end(&bt);
    }
    CHECK_EQ_INT(a.len, b.len);
    CHECK_MEM(a.buf, b.buf, a.len);
}

/* rw_flags carries write/opcode/dummy bits verbatim. */
static void test_rw_flag_bits(void)
{
    capture_t cap = {0};
    revice_bustrace_t bt;
    const uint8_t *r;
    revice_bustrace_begin(&bt, 0, cap_emit, &cap);
    revice_bustrace_access(&bt, 1, 0x1000, 0xaa,
                           REVICE_BUSTRACE_RW_WRITE | REVICE_BUSTRACE_RW_DUMMY,
                           0x2000);
    r = cap.buf + REVICE_BUSTRACE_HEADER_SIZE;
    CHECK_EQ_INT(r[6], 0xaa);
    CHECK_EQ_INT(r[7], REVICE_BUSTRACE_RW_WRITE | REVICE_BUSTRACE_RW_DUMMY);
}

/* Access before begin() (started=0) emits nothing and cannot crash. */
static void test_access_without_begin_is_noop(void)
{
    capture_t cap = {0};
    revice_bustrace_t bt;
    memset(&bt, 0, sizeof(bt));    /* started = 0 */
    revice_bustrace_access(&bt, 1, 0x1000, 0x00, 0, 0x0800);
    revice_bustrace_end(&bt);
    CHECK_EQ_INT(cap.len, 0);
}

/* A known CRC-32 vector ("123456789" -> 0xCBF43926) pins the polynomial. */
static void test_crc32_known_vector(void)
{
    const uint8_t v[] = "123456789";
    uint32_t crc = revice_bustrace_crc32(0, v, sizeof(v) - 1);
    CHECK_EQ_INT((long)crc, (long)0xCBF43926u);
}

int main(void)
{
    test_header_layout();
    test_single_record_layout();
    test_cycle_deltas();
    test_trailer_count_and_crc();
    test_determinism();
    test_rw_flag_bits();
    test_access_without_begin_is_noop();
    test_crc32_known_vector();
    REVICE_TEST_MAIN_END();
}
