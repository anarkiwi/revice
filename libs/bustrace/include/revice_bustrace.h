/*
 * revice_bustrace.h - CPU bus-trace / observation-graph core.
 *
 * The pure, OS- and VICE-independent half of the asid-vice bus-trace feature.
 * It ingests a stream of 6510 memory accesses - {cycle, addr, val, rw, pc} -
 * during SID playback and serializes them into a compact, deterministic binary
 * record stream. It knows nothing about VICE's CPU, the emulator clock, or the
 * host filesystem: the caller feeds accesses in and the core hands fully-formed
 * byte records to an emit callback, which the adapter writes to a file
 * alongside the existing SID-register dump.
 *
 * This is the provenance substrate for generic BACC recovery (accumulators ->
 * rate/dwell, table-walks -> orderlists, indexed-read -> freq-write -> the A440
 * note map, per-voice generators). It REPLACES the dropped libsidplayfp
 * `sidtrace` `.sidwr.bin`/`.bus.bin`, which was non-deterministic (its output
 * varied run-to-run on the same tune) and broke byte-exact gates. VICE is the
 * trusted, deterministic, byte-exact emulator, so the observation lives here.
 *
 * DETERMINISM GUARANTEE
 * ---------------------
 * The core performs no allocation, no time/RNG/environment reads, and no
 * floating point. Given an identical access sequence it emits a byte-identical
 * stream. All multi-byte fields are little-endian and explicitly sized, so the
 * output is independent of host endianness/word size. The only state is a
 * monotonic record counter (for a self-describing trailer) and the running
 * cycle baseline used for delta encoding.
 *
 * BINARY FORMAT (version 1)
 * -------------------------
 * A trace file is:   header  record*  trailer
 *
 * Header (16 bytes, written once at open):
 *   off  size  field
 *   0    4     magic       "RBT1"  (0x52 0x42 0x54 0x31)
 *   4    2     version     uint16le = REVICE_BUSTRACE_VERSION (1)
 *   6    1     rec_size    uint8    = REVICE_BUSTRACE_REC_SIZE (10)
 *   7    1     flags       uint8    bit0: PC field present (always 1 here)
 *   8    8     cycles_per_sec uint64le  CPU clock (PAL/NTSC/Drean), 0 if unknown
 *
 * Record (10 bytes, one per memory access, in emit order = cycle order):
 *   off  size  field
 *   0    4     cycle_delta uint32le  cycles since the previous record's cycle
 *                                    (first record's delta is from 0). A delta
 *                                    that would overflow 32 bits is impossible
 *                                    in a single playback window; the adapter
 *                                    feeds the absolute clock and the core
 *                                    deltas it, so the stream stays compact and
 *                                    the absolute cycle is reconstructable by a
 *                                    running sum.
 *   4    2     addr        uint16le  full 16-bit bus address
 *   6    1     val         uint8     byte on the bus (read result or stored)
 *   7    1     rw_flags    uint8     bit0: 1=write, 0=read
 *                                    bit1: 1=this access is an opcode fetch
 *                                    bit2: 1=dummy/internal access (no real bus
 *                                          effect; kept for fidelity, filterable)
 *   8    2     pc          uint16le  program counter of the accessing
 *                                    instruction (write-provenance / call graph)
 *
 * Trailer (16 bytes, written once at close):
 *   0    4     magic       "RBTe"  (end marker)
 *   4    8     rec_count   uint64le  number of records emitted
 *   12   4     crc32       uint32le  CRC-32 (IEEE 802.3, reflected) over every
 *                                    record byte (not header/trailer). A reader
 *                                    can verify completeness; two runs of the
 *                                    same tune must produce the same crc32.
 *
 * The format is fixed-width and append-only: a reader strides records by
 * rec_size, reconstructs absolute cycles by summing cycle_delta, and validates
 * with rec_count + crc32. Nothing here depends on wall-clock time or host
 * layout, which is exactly the property sidtrace lacked.
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See LICENSE for copyright notice (GPL-2.0-or-later).
 */

#ifndef REVICE_BUSTRACE_H
#define REVICE_BUSTRACE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define REVICE_BUSTRACE_MAGIC0      0x52  /* 'R' */
#define REVICE_BUSTRACE_MAGIC1      0x42  /* 'B' */
#define REVICE_BUSTRACE_MAGIC2      0x54  /* 'T' */
#define REVICE_BUSTRACE_MAGIC3      0x31  /* '1' */

#define REVICE_BUSTRACE_VERSION     1u
#define REVICE_BUSTRACE_HEADER_SIZE  16u
#define REVICE_BUSTRACE_REC_SIZE     10u
#define REVICE_BUSTRACE_TRAILER_SIZE 16u

/* header flags */
#define REVICE_BUSTRACE_FLAG_HAS_PC  0x01u

/* rw_flags bits in each record */
#define REVICE_BUSTRACE_RW_WRITE     0x01u   /* set: write, clear: read */
#define REVICE_BUSTRACE_RW_OPCODE    0x02u   /* opcode fetch */
#define REVICE_BUSTRACE_RW_DUMMY     0x04u   /* dummy / internal cycle access */

/* Emit callback: a span of finished trace bytes ready for the file. The core
   only ever asks the host to append; it never seeks or reads back. */
typedef void (*revice_bustrace_emit_fn)(void *ctx, const uint8_t *buf,
                                        size_t len);

typedef struct {
    revice_bustrace_emit_fn emit;
    void       *ctx;
    uint64_t    last_cycle;     /* absolute cycle of the previous record */
    uint64_t    rec_count;      /* records emitted so far                */
    uint32_t    crc;            /* running CRC-32 over record bytes       */
    int         started;        /* header written?                        */
} revice_bustrace_t;

/* Reset state and write the 16-byte header via emit. Must be called once
   before any access. cycles_per_sec is recorded verbatim in the header (pass
   machine_get_cycles_per_second(); 0 is allowed). */
void revice_bustrace_begin(revice_bustrace_t *bt, uint64_t cycles_per_sec,
                           revice_bustrace_emit_fn emit, void *ctx);

/* Record one bus access. cycle is the absolute emulator clock at the access;
   it must be monotonically non-decreasing across calls (the core delta-encodes
   it). addr is the 16-bit bus address, val the byte transferred, rw_flags the
   REVICE_BUSTRACE_RW_* bits, pc the accessing instruction's program counter.
   Emits exactly one fixed-width record. */
void revice_bustrace_access(revice_bustrace_t *bt, uint64_t cycle,
                            uint16_t addr, uint8_t val, uint8_t rw_flags,
                            uint16_t pc);

/* Write the 16-byte trailer (end magic + record count + CRC-32). After this
   the trace file is complete and self-validating. Safe to call once. */
void revice_bustrace_end(revice_bustrace_t *bt);

/* Standalone CRC-32 (IEEE, reflected) over a buffer, exposed so readers/tests
   can independently verify the trailer crc. Pass 0 as the initial seed. */
uint32_t revice_bustrace_crc32(uint32_t seed, const uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* REVICE_BUSTRACE_H */
