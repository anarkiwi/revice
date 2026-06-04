/*
 * mon_keymatrix.c - VICE adapter for the revice keymatrix core.
 *
 * Thin glue: it fills a km_host_t vtable from VICE (keyboard matrix, clock,
 * release alarm, mon_out) and forwards the historical mon_keymatrix_*() API to
 * the core in libs/keymatrix. All of the logic (key tables, parsing, the
 * observation/release state machine, the binmon codecs) lives in the core and
 * is unit-tested there; this file only wires it to the emulator.
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "vice.h"

#include "alarm.h"
#include "keyboard.h"
#include "machine.h"
#include "maincpu.h"
#include "mon_keymatrix.h"
#include "monitor.h"
#include "montypes.h"
#include "types.h"

#include "revice_keymatrix.h"

/* Compile-time guard: the core mirrors VICE's keyboard sentinels; keep them
   in lock-step. */
static void km_static_checks(void)
{
    (void)sizeof(char[(KM_ROW_RESTORE_1   == KBD_ROW_RESTORE_1)   ? 1 : -1]);
    (void)sizeof(char[(KM_COL_RESTORE_1   == KBD_COL_RESTORE_1)   ? 1 : -1]);
    (void)sizeof(char[(KM_ROW_RESTORE_2   == KBD_ROW_RESTORE_2)   ? 1 : -1]);
    (void)sizeof(char[(KM_COL_RESTORE_2   == KBD_COL_RESTORE_2)   ? 1 : -1]);
    (void)sizeof(char[(KM_ROW_4080COLUMN  == KBD_ROW_4080COLUMN)  ? 1 : -1]);
    (void)sizeof(char[(KM_COL_4080COLUMN  == KBD_COL_4080COLUMN)  ? 1 : -1]);
    (void)sizeof(char[(KM_ROW_CAPSLOCK    == KBD_ROW_CAPSLOCK)    ? 1 : -1]);
    (void)sizeof(char[(KM_COL_CAPSLOCK    == KBD_COL_CAPSLOCK)    ? 1 : -1]);
    (void)sizeof(char[(KM_CUSTOM_RESTORE1 == KBD_CUSTOM_RESTORE1) ? 1 : -1]);
    (void)sizeof(char[(KM_CUSTOM_RESTORE2 == KBD_CUSTOM_RESTORE2) ? 1 : -1]);
    (void)sizeof(char[(KM_CUSTOM_CAPS     == KBD_CUSTOM_CAPS)     ? 1 : -1]);
    (void)sizeof(char[(KM_CUSTOM_4080     == KBD_CUSTOM_4080)     ? 1 : -1]);
    (void)sizeof(char[(MON_KEYMATRIX_BINMON_GET_RESPONSE_SIZE ==
                       KM_BINMON_GET_RESPONSE_SIZE) ? 1 : -1]);
    (void)km_static_checks;
}

static km_core_t kmx;
static int kmx_inited = 0;
static alarm_t *kmx_alarm = NULL;

static void kmx_alarm_callback(CLOCK offset, void *data)
{
    (void)offset;
    (void)data;
    km_core_on_alarm(&kmx);
}

/* ---- host ops ---- */

static void h_set_keyarr_any(void *ctx, int row, int col, int value)
{
    (void)ctx;
    keyboard_set_keyarr_any(row, col, value);
}

static int h_get_keyarr(void *ctx, int row)
{
    (void)ctx;
    return keyarr[row] & 0xff;
}

static int h_custom_key_get(void *ctx, int id)
{
    (void)ctx;
    return keyboard_custom_key_get(id);
}

static void h_clear_keymatrix(void *ctx)
{
    (void)ctx;
    keyboard_clear_keymatrix();
}

static uint64_t h_now(void *ctx)
{
    (void)ctx;
    return (uint64_t)maincpu_clk;
}

static long h_cycles_per_frame(void *ctx)
{
    (void)ctx;
    return machine_get_cycles_per_frame();
}

static void h_set_alarm(void *ctx, uint64_t clk)
{
    (void)ctx;
    if (kmx_alarm == NULL) {
        kmx_alarm = alarm_new(maincpu_alarm_context, "MonitorKeymatrix",
                              kmx_alarm_callback, NULL);
    }
    alarm_set(kmx_alarm, (CLOCK)clk);
}

static void h_cancel_alarm(void *ctx)
{
    (void)ctx;
    if (kmx_alarm != NULL) {
        alarm_unset(kmx_alarm);
    }
}

static void h_emit(void *ctx, const char *str)
{
    (void)ctx;
    mon_out("%s", str);
}

static const km_host_t kmx_host = {
    h_set_keyarr_any,
    h_get_keyarr,
    h_custom_key_get,
    h_clear_keymatrix,
    h_now,
    h_cycles_per_frame,
    h_set_alarm,
    h_cancel_alarm,
    h_emit
};

static void kmx_ensure(void)
{
    if (!kmx_inited) {
        km_core_init(&kmx, &kmx_host, NULL);
        kmx_inited = 1;
    }
}

/* ---- public API (delegates to the core) ---- */

void mon_keymatrix_tap(const char *args)     { kmx_ensure(); km_core_tap(&kmx, args); }
void mon_keymatrix_press(const char *args)   { kmx_ensure(); km_core_press(&kmx, args); }
void mon_keymatrix_release(const char *args) { kmx_ensure(); km_core_release(&kmx, args); }
void mon_keymatrix_poke(const char *args)    { kmx_ensure(); km_core_poke(&kmx, args); }
void mon_keymatrix_show(void)                { kmx_ensure(); km_core_show(&kmx); }
void mon_keymatrix_names(void)               { kmx_ensure(); km_core_names(&kmx); }

void mon_keymatrix_cia1_pa_read(uint8_t row_scan_mask)
{
    if (!kmx_inited) {
        return;                       /* feature unused: cheapest path */
    }
    km_core_cia1_pa_read(&kmx, row_scan_mask);
}

void mon_keymatrix_cia1_pb_read(uint8_t col_scan_mask)
{
    if (!kmx_inited) {
        return;
    }
    km_core_cia1_pb_read(&kmx, col_scan_mask);
}

int mon_keymatrix_binmon_set(const uint8_t *body, uint32_t length)
{
    kmx_ensure();
    return km_core_binmon_set(&kmx, body, length);
}

int mon_keymatrix_binmon_tap(const uint8_t *body, uint32_t length)
{
    kmx_ensure();
    return km_core_binmon_tap(&kmx, body, length);
}

int mon_keymatrix_binmon_get(uint8_t *out, uint32_t *out_length)
{
    kmx_ensure();
    return km_core_binmon_get(&kmx, out, out_length);
}
