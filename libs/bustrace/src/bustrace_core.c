/*
 * bustrace_core.c - CPU bus-trace serializer core. See revice_bustrace.h.
 *
 * Pure C, no allocation, no time/RNG, no floating point, no VICE headers.
 * Every multi-byte field is written little-endian by hand so the byte stream
 * is identical on every host - this is the determinism the dropped sidtrace
 * tool lacked.
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See LICENSE for copyright notice (GPL-2.0-or-later).
 */

#include "revice_bustrace.h"

#include <string.h>

/* Little-endian byte writers. */
static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
    p[2] = (uint8_t)((v >> 16) & 0xff);
    p[3] = (uint8_t)((v >> 24) & 0xff);
}

static void put_u64(uint8_t *p, uint64_t v)
{
    int i;
    for (i = 0; i < 8; ++i) {
        p[i] = (uint8_t)((v >> (8 * i)) & 0xff);
    }
}

/* CRC-32 (IEEE 802.3, reflected) computed bytewise so there is no table to get
   out of sync; deterministic and host-independent. */
uint32_t revice_bustrace_crc32(uint32_t seed, const uint8_t *buf, size_t len)
{
    uint32_t crc = ~seed;
    size_t i;
    int b;
    for (i = 0; i < len; ++i) {
        crc ^= buf[i];
        for (b = 0; b < 8; ++b) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (0xedb88320u & mask);
        }
    }
    return ~crc;
}

void revice_bustrace_begin(revice_bustrace_t *bt, uint64_t cycles_per_sec,
                           revice_bustrace_emit_fn emit, void *ctx)
{
    uint8_t hdr[REVICE_BUSTRACE_HEADER_SIZE];

    memset(bt, 0, sizeof(*bt));
    bt->emit = emit;
    bt->ctx = ctx;
    bt->last_cycle = 0;
    bt->rec_count = 0;
    bt->crc = 0;
    bt->started = 1;

    hdr[0] = REVICE_BUSTRACE_MAGIC0;
    hdr[1] = REVICE_BUSTRACE_MAGIC1;
    hdr[2] = REVICE_BUSTRACE_MAGIC2;
    hdr[3] = REVICE_BUSTRACE_MAGIC3;
    put_u16(&hdr[4], (uint16_t)REVICE_BUSTRACE_VERSION);
    hdr[6] = (uint8_t)REVICE_BUSTRACE_REC_SIZE;
    hdr[7] = (uint8_t)REVICE_BUSTRACE_FLAG_HAS_PC;
    put_u64(&hdr[8], cycles_per_sec);

    if (emit) {
        emit(ctx, hdr, sizeof(hdr));
    }
}

void revice_bustrace_access(revice_bustrace_t *bt, uint64_t cycle,
                            uint16_t addr, uint8_t val, uint8_t rw_flags,
                            uint16_t pc)
{
    uint8_t rec[REVICE_BUSTRACE_REC_SIZE];
    uint64_t delta;

    if (!bt->started) {
        return;
    }

    /* Monotonic, delta-encoded. A non-decreasing clock keeps the delta >= 0;
       guard against a stray out-of-order cycle so the stream never blows up. */
    delta = (cycle >= bt->last_cycle) ? (cycle - bt->last_cycle) : 0u;
    bt->last_cycle = cycle;
    if (delta > 0xffffffffull) {
        delta = 0xffffffffull;   /* saturate; absolute cycle is informational */
    }

    put_u32(&rec[0], (uint32_t)delta);
    put_u16(&rec[4], addr);
    rec[6] = val;
    rec[7] = rw_flags;
    put_u16(&rec[8], pc);

    bt->crc = revice_bustrace_crc32(bt->crc, rec, sizeof(rec));
    bt->rec_count++;

    if (bt->emit) {
        bt->emit(bt->ctx, rec, sizeof(rec));
    }
}

void revice_bustrace_end(revice_bustrace_t *bt)
{
    uint8_t tr[REVICE_BUSTRACE_TRAILER_SIZE];

    if (!bt->started) {
        return;
    }
    bt->started = 0;

    tr[0] = REVICE_BUSTRACE_MAGIC0;     /* 'R' */
    tr[1] = REVICE_BUSTRACE_MAGIC1;     /* 'B' */
    tr[2] = REVICE_BUSTRACE_MAGIC2;     /* 'T' */
    tr[3] = 0x65;                       /* 'e' end marker */
    put_u64(&tr[4], bt->rec_count);
    put_u32(&tr[12], bt->crc);

    if (bt->emit) {
        bt->emit(bt->ctx, tr, sizeof(tr));
    }
}
