/*
 * c64screen.c - VICE adapter: C64 screen-state provider for the monitor's
 *               `screenscrape` / SCREEN_GET commands.
 *
 * Gathers the live VIC-II registers + address-decode state and the relevant
 * memory (main RAM, color RAM, chargen ROM) into a screen_inputs_t and hands
 * it to the revice screen core (screen_pack), which produces the wire-format
 * response. Registered with the monitor at machine init via
 * mon_screen_register_provider().
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice.
 */

#include "vice.h"

#include <stddef.h>

#include "c64mem.h"
#include "mem.h"
#include "mon_screen.h"
#include "types.h"
#include "viciitypes.h"

#include "revice_screen.h"

static uint8_t cb_read_ram(void *ctx, uint16_t addr)
{
    (void)ctx;
    return mem_ram[addr & 0xffff];
}

static uint8_t cb_read_colorram(void *ctx, uint16_t addr)
{
    (void)ctx;
    return (uint8_t)colorram_read(addr);
}

static uint8_t cb_read_chargen(void *ctx, uint16_t offset)
{
    (void)ctx;
    return mem_chargen_rom[offset & 0x0fff];
}

static int c64screen_get_state(uint8_t *buf, uint32_t buf_len, uint32_t *out_len)
{
    screen_inputs_t in;

    in.d011 = vicii.regs[0x11];
    in.d016 = vicii.regs[0x16];
    in.d018 = vicii.regs[0x18];
    in.d020 = vicii.regs[0x20];
    in.d021 = vicii.regs[0x21];
    in.d022 = vicii.regs[0x22];
    in.d023 = vicii.regs[0x23];
    in.d024 = vicii.regs[0x24];
    in.vbank_phi1 = (uint16_t)vicii.vbank_phi1;
    in.vbank_phi2 = (uint16_t)vicii.vbank_phi2;
    in.chargen_mask_phi1  = (uint16_t)vicii.vaddr_chargen_mask_phi1;
    in.chargen_value_phi1 = (uint16_t)vicii.vaddr_chargen_value_phi1;
    in.read_ram = cb_read_ram;
    in.read_colorram = cb_read_colorram;
    in.read_chargen = cb_read_chargen;
    in.ctx = NULL;

    return screen_pack(&in, buf, buf_len, out_len);
}

void c64screen_init(void)
{
    mon_screen_register_provider(c64screen_get_state);
}
