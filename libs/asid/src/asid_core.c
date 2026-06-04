/*
 * asid_core.c - ASID-over-MIDI protocol codec. See revice_asid.h.
 *
 * Ported verbatim (logic-preserving) from the asid-vice fork's
 * src/arch/shared/sounddrv/soundasid.c so the wire format is provably
 * unchanged; only the ALSA transport, the emulator-clock plumbing, and the
 * VICE sound_device_t registration were left behind in the adapter.
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See LICENSE for copyright notice (GPL-2.0-or-later).
 */

#include "revice_asid.h"

#include <string.h>

const uint8_t asid_core_start_msg[4] = {
    ASID_SYSEX_START, ASID_SYSEX_MAN_ID, ASID_CMD_START, ASID_SYSEX_STOP
};
const uint8_t asid_core_stop_msg[4] = {
    ASID_SYSEX_START, ASID_SYSEX_MAN_ID, ASID_CMD_STOP, ASID_SYSEX_STOP
};

static const uint8_t asid_prefix[2] = { ASID_SYSEX_START, ASID_SYSEX_MAN_ID };
static const uint8_t asid_update[ASID_CHIPS] = { ASID_CMD_UPDATE, ASID_CMD_UPDATE2 };
static const uint8_t asid_update_reg[ASID_CHIPS] = { ASID_CMD_UPDATE_REG, ASID_CMD_UPDATE2_REG };

/* Order in which registers are packed into the standard update payload.
   IDs 25-27 are not implemented (rumoured re-writes of 4/11/18). */
static const uint8_t regmap[ASID_NUM_REGS] = {
    0,  1,  2,  3,  5,  6,  7,  8,  9,  10, 12, 13, 14,
    15, 16, 17, 19, 20, 21, 22, 23, 24, 4,  11, 18
};

/* Writable-bit mask per SID register. */
static const uint8_t regmask[ASID_NUM_REGS] = {
    /* 0    1    2    3    4    5    6    7  */
    255, 255, 255, 15, 255, 255, 255, 255,
    /* 8    9   10   11   12   13   14   15  */
    255, 255, 15, 255, 255, 255, 255, 255,
    /* 16  17   18   19   20   21   22   23  24 */
    255, 15, 255, 255, 255, 7, 255, 255, 255
};

void asid_core_init(asid_core_t *core, int use_update_reg)
{
    int chip;

    memset(core, 0, sizeof(*core));
    core->use_update_reg = use_update_reg ? 1 : 0;

    for (chip = 0; chip < ASID_CHIPS; ++chip) {
        asid_chip_t *state = &core->chip[chip];
        memcpy(state->update_buffer, asid_prefix, sizeof(asid_prefix));
        memcpy(state->update_reg_buffer, asid_prefix, sizeof(asid_prefix));
        state->update_buffer[sizeof(asid_prefix)] = asid_update[chip];
        state->update_reg_buffer[sizeof(asid_prefix)] = asid_update_reg[chip];
    }
}

void asid_core_flush(asid_core_t *core, uint8_t chip, uint64_t nsec,
                     asid_emit_fn emit, void *ctx)
{
    asid_chip_t *state;
    uint8_t i;
    uint8_t t;
    uint8_t mapped_reg;
    uint8_t m;
    uint8_t p;
    uint32_t mask = 0;
    uint32_t msb = 0;

    if (chip >= ASID_CHIPS) {
        return;
    }
    state = &core->chip[chip];

    if (!state->sid_modified_flag) {
        return;
    }

    /* Per-register ("update reg") form: pairs of (reg, value), MSB folded
       into bit 6 of the register byte. Emitted in regmap order so the control
       registers (4/11/18) come last, exactly like the standard update form —
       the receiver applies each pair in order, so a voice's freq/PW/ADSR are
       set before its gate edge. */
    t = sizeof(asid_prefix) + 1;
    for (i = 0; i < ASID_NUM_REGS; ++i) {
        uint8_t reg = regmap[i];
        uint8_t enc;
        uint8_t val;
        if (!state->sid_modified[reg]) {
            continue;
        }
        val = state->sid_register[reg];
        enc = reg;
        if (val > 0x7f) {
            enc |= (1 << 6);
        }
        state->update_reg_buffer[t++] = enc;
        state->update_reg_buffer[t++] = val & 0x7f;
    }
    state->update_reg_buffer[t++] = ASID_SYSEX_STOP;

    /* Standard ASID "update" form: a 4-byte mask + 4-byte MSB header (7 bits
       per byte) followed by the 7-bit payload, in regmap order. */
    m = sizeof(asid_prefix) + 1;
    p = m + 8;
    for (i = 0; i < ASID_NUM_REGS; ++i) {
        uint8_t val;
        mapped_reg = regmap[i];
        if (!state->sid_modified[mapped_reg]) {
            continue;
        }
        val = state->sid_register[mapped_reg];
        mask |= (1u << i);
        if (val > 0x7f) {
            msb |= (1u << i);
        }
        state->update_buffer[p++] = val & 0x7f;
    }
    for (i = 0; i < sizeof(mask); ++i, mask >>= 7) {
        state->update_buffer[m++] = mask & 0x7f;
    }
    for (i = 0; i < sizeof(msb); ++i, msb >>= 7) {
        state->update_buffer[m++] = msb & 0x7f;
    }
    state->update_buffer[p++] = ASID_SYSEX_STOP;

    state->sid_modified_flag = 0;
    memset(state->sid_modified, 0, sizeof(state->sid_modified));

    if (core->use_update_reg && (t < p)) {
        emit(ctx, chip, state->update_reg_buffer, t, nsec);
        core->bytes_saved += (uint32_t)(p - t);
    } else {
        emit(ctx, chip, state->update_buffer, p, nsec);
    }
}

void asid_core_set_reg(asid_core_t *core, uint8_t chip, uint8_t reg,
                       uint8_t byte, asid_emit_fn emit, void *ctx)
{
    asid_chip_t *state;

    if (chip >= ASID_CHIPS || reg >= ASID_NUM_REGS) {
        return;
    }
    state = &core->chip[chip];

    byte = regmask[reg] & byte;
    if (state->sid_register[reg] == byte) {
        return;
    }
    /* Flush on change to a control register so its previous edge is sent. */
    if ((reg == 4 || reg == 11 || reg == 18) && state->sid_modified[reg]) {
        asid_core_flush(core, chip, 0, emit, ctx);
    }
    state->sid_register[reg] = byte;
    state->sid_modified[reg] = 1;
    state->sid_modified_flag = 1;
}

uint64_t asid_clock_to_nanos(uint64_t clock, uint64_t cycles_per_sec)
{
    if (cycles_per_sec == 0) {
        cycles_per_sec = ASID_PAL_CYCLES_PER_SEC;
    }
    return (uint64_t)((double)clock / (double)cycles_per_sec * 1e9);
}

int asid_should_flush_on_irq(uint64_t irq_clk, uint64_t last_irq)
{
    return (irq_clk - last_irq) > ASID_IRQ_FLUSH_THRESHOLD;
}
