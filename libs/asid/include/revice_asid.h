/*
 * revice_asid.h - ASID-over-MIDI protocol codec, extracted from the asid-vice
 *                 fork's src/arch/shared/sounddrv/soundasid.c.
 *
 * This is the pure, OS- and VICE-independent half of the ASID sound device:
 * it turns a stream of SID register writes into the SysEx "update" messages
 * defined by the ASID protocol (http://paulus.kapsi.fi/asid_protocol.txt). It
 * knows nothing about ALSA, MIDI ports, or the emulator clock - the caller
 * feeds register writes in and gets fully-formed SysEx messages out via an
 * emit callback, then drives the bytes onto whatever MIDI transport it has.
 *
 * Two message encodings are produced and the cheaper one is chosen per flush:
 *   - the standard ASID "update" (mask/MSB header + packed payload), and
 *   - the shorter per-register "update reg" form (enabled by use_update_reg),
 *     used by clients on a Vessel/VAP receiver to cut MIDI bandwidth.
 *
 * Originally written by aTc <aTc@k-n-p.org>, updated by josh@vandervecken.com.
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See LICENSE for copyright notice (GPL-2.0-or-later).
 */

#ifndef REVICE_ASID_H
#define REVICE_ASID_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SysEx framing. */
#define ASID_SYSEX_START   0xf0
#define ASID_SYSEX_MAN_ID  0x2d
#define ASID_SYSEX_STOP    0xf7

/* ASID command bytes. */
#define ASID_CMD_START        0x4c
#define ASID_CMD_STOP         0x4d
#define ASID_CMD_UPDATE       0x4e   /* chip 0 standard update   */
#define ASID_CMD_UPDATE2      0x50   /* chip 1 standard update   */
#define ASID_CMD_UPDATE_REG   0x6c   /* chip 0 per-register form */
#define ASID_CMD_UPDATE2_REG  0x6d   /* chip 1 per-register form */

#define ASID_CHIPS           2       /* dual-SID (2SID) support  */
#define ASID_NUM_REGS        25      /* SID registers carried in a frame */
#define ASID_MAX_SID_REG     24      /* highest addressable SID register */
#define ASID_BUFFER_SIZE     256     /* per-chip working buffer size */

/* Timing helpers (used by the realtime flush scheduler in the adapter). */
#define ASID_CYCLE_PAD            20000u
#define ASID_IRQ_FLUSH_THRESHOLD  256u
/* PAL C64 CPU clock (17.734475 MHz / 18), used as a fallback only. */
#define ASID_PAL_CYCLES_PER_SEC   985248u

/* Pre-built fixed SysEx messages (start/stop the ASID session). */
extern const uint8_t asid_core_start_msg[4];
extern const uint8_t asid_core_stop_msg[4];

/* Per-chip codec state. The two working buffers are seeded once in
   asid_core_init() with the SysEx prefix + the chip's update opcode and are
   only ever rewritten from byte 3 onward. */
typedef struct {
    uint8_t update_buffer[ASID_BUFFER_SIZE];     /* standard update form */
    uint8_t update_reg_buffer[ASID_BUFFER_SIZE]; /* per-register form    */
    uint8_t sid_register[ASID_NUM_REGS];
    uint8_t sid_modified[ASID_NUM_REGS];         /* 0/1 dirty flags      */
    uint8_t sid_modified_flag;                   /* any reg dirty?       */
} asid_chip_t;

typedef struct {
    asid_chip_t chip[ASID_CHIPS];
    int         use_update_reg;   /* prefer the shorter per-register form */
    uint32_t    bytes_saved;      /* cumulative bytes saved by short form */
} asid_core_t;

/* Emit callback: a complete SysEx message ready for the MIDI transport.
   `chip` identifies the SID, `nsec` is the relative schedule time the caller
   passed to the flush (0 for the synchronous control-register flush). */
typedef void (*asid_emit_fn)(void *ctx, uint8_t chip,
                             const uint8_t *msg, size_t len, uint64_t nsec);

/* Reset codec state and seed both working buffers for every chip. */
void asid_core_init(asid_core_t *core, int use_update_reg);

/* Record a SID register write. `byte` is masked to the register's writable
   bits. A no-op write (value unchanged) is ignored. Writing one of the
   control registers (4 / 11 / 18) while that register is already dirty forces
   a synchronous flush of the chip first (nsec 0) so the previous gate edge is
   not lost - this mirrors the fork's _set_reg(). Registers above
   ASID_MAX_SID_REG are ignored. */
void asid_core_set_reg(asid_core_t *core, uint8_t chip, uint8_t reg,
                       uint8_t byte, asid_emit_fn emit, void *ctx);

/* Build and emit an update message for `chip` carrying every register dirtied
   since the last flush, then clear the dirty set. Chooses the per-register
   form when use_update_reg is set and it is actually shorter, crediting the
   saving to core->bytes_saved. Does nothing (no emit) if nothing is dirty. */
void asid_core_flush(asid_core_t *core, uint8_t chip, uint64_t nsec,
                     asid_emit_fn emit, void *ctx);

/* Convert an emulator cycle count to nanoseconds for the given CPU clock rate
   (cycles per second). Pass machine_get_cycles_per_second() so the MIDI flush
   scheduling is correct on PAL, NTSC and Drean alike; a cycles_per_sec of 0
   falls back to PAL (ASID_PAL_CYCLES_PER_SEC). */
uint64_t asid_clock_to_nanos(uint64_t clock, uint64_t cycles_per_sec);

/* True when the IRQ delta since the last flush is large enough that the
   previous IRQ's register writes should be flushed (irq_diff > threshold). */
int asid_should_flush_on_irq(uint64_t irq_clk, uint64_t last_irq);

#ifdef __cplusplus
}
#endif

#endif /* REVICE_ASID_H */
