/*
 * soundbustrace.c - VICE adapter: CPU bus-trace / observation-graph capture.
 *
 * The trace serializer (record layout, delta/CRC, determinism) lives in the
 * revice bustrace core (libs/bustrace) and is unit-tested there. This file is
 * the VICE half: it registers the `-bustrace <file>` resource/cmdline option,
 * opens the trace file, feeds every 6510 memory access from the maincpu bus
 * hook into the core, and finalizes the file at shutdown.
 *
 * It is deliberately ADDITIVE and non-perturbing: when no trace file is set
 * (the default) bustrace_observe_access() is a single null-pointer test and
 * returns immediately, so the existing SID-register dump is byte-identical
 * with the feature off vs on. The trace captures the full bus - every read and
 * write, with cycle, address, value, rw, and the accessing PC - which is the
 * provenance substrate for generic BACC recovery (it replaces the dropped
 * libsidplayfp `sidtrace` `.bus.bin`, whose output was non-deterministic).
 *
 * Wiring: the per-access call is placed in src/c64/vsidcpu.c's memmap hooks
 * (the FEATURE_CPUMEMHISTORY observation point - the one place VICE already
 * sees every CPU access), and the open/close are driven from VICE resource
 * init/shutdown. See integration/vice/README.md.
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice (GPL-2.0-or-later).
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "vice.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cmdline.h"
#include "lib.h"
#include "log.h"
#include "machine.h"
#include "maincpu.h"
#include "resources.h"
#include "types.h"
#include "util.h"

#include "revice_bustrace.h"
#include "soundbustrace.h"

/* Set by the -bustrace resource; NULL/empty means the feature is off. */
static char *bustrace_filename = NULL;

static FILE *bustrace_fd = NULL;
static revice_bustrace_t bustrace_core;
static int bustrace_open = 0;

/* Core emit callback: append finished trace bytes to the file. The core only
   ever appends, so a plain buffered fwrite is sufficient and deterministic. */
static void bustrace_file_emit(void *ctx, const uint8_t *buf, size_t len)
{
    FILE *f = (FILE *)ctx;
    if (f != NULL && len > 0) {
        fwrite(buf, 1, len, f);
    }
}

/* Open the trace file and write the header. Called lazily on the first
   observed access so the file exists only when something was actually traced
   and the CPU clock is live. Idempotent. */
static void bustrace_ensure_open(void)
{
    if (bustrace_open) {
        return;
    }
    if (bustrace_filename == NULL || bustrace_filename[0] == '\0') {
        return;
    }
    bustrace_fd = fopen(bustrace_filename, "wb");
    if (bustrace_fd == NULL) {
        log_warning(LOG_DEFAULT, "bustrace: cannot open '%s'",
                    bustrace_filename);
        /* Disable so we do not retry every access. */
        bustrace_open = -1;
        return;
    }
    revice_bustrace_begin(&bustrace_core,
                          (uint64_t)machine_get_cycles_per_second(),
                          bustrace_file_emit, bustrace_fd);
    bustrace_open = 1;
    log_message(LOG_DEFAULT, "bustrace: writing CPU bus trace to '%s'",
                bustrace_filename);
}

/* Called from the maincpu bus hook for every memory access. Hot path: when the
   feature is off this is one pointer test. */
void bustrace_observe_access(uint16_t addr, uint8_t val, uint8_t rw_flags)
{
    if (bustrace_filename == NULL || bustrace_filename[0] == '\0') {
        return;
    }
    if (bustrace_open == 0) {
        bustrace_ensure_open();
    }
    if (bustrace_open != 1) {
        return;
    }
    revice_bustrace_access(&bustrace_core, (uint64_t)maincpu_clk,
                           addr, val, rw_flags, (uint16_t)reg_pc);
}

int bustrace_is_enabled(void)
{
    return (bustrace_filename != NULL && bustrace_filename[0] != '\0');
}

void bustrace_shutdown(void)
{
    if (bustrace_open == 1) {
        revice_bustrace_end(&bustrace_core);
        log_message(LOG_DEFAULT, "bustrace: %llu records written",
                    (unsigned long long)bustrace_core.rec_count);
    }
    if (bustrace_fd != NULL) {
        fclose(bustrace_fd);
        bustrace_fd = NULL;
    }
    bustrace_open = 0;
    if (bustrace_filename != NULL) {
        lib_free(bustrace_filename);
        bustrace_filename = NULL;
    }
}

/* ---- VICE resource + cmdline registration --------------------------------- */

static int set_bustrace_filename(const char *val, void *param)
{
    (void)param;
    /* Changing the trace target mid-run closes any open file first. */
    if (bustrace_open == 1) {
        revice_bustrace_end(&bustrace_core);
    }
    if (bustrace_fd != NULL) {
        fclose(bustrace_fd);
        bustrace_fd = NULL;
    }
    bustrace_open = 0;
    return util_string_set(&bustrace_filename, val);
}

static const resource_string_t resources_string[] = {
    { "BusTraceFile", "", RES_EVENT_NO, NULL,
      &bustrace_filename, set_bustrace_filename, NULL },
    RESOURCE_STRING_LIST_END
};

int bustrace_resources_init(void)
{
    return resources_register_string(resources_string);
}

static const cmdline_option_t cmdline_options[] = {
    { "-bustrace", SET_RESOURCE, CMDLINE_ATTRIB_NEED_ARGS,
      NULL, NULL, "BusTraceFile", NULL,
      "<Name>", "Write a deterministic CPU bus trace to <Name> (additive; "
                "does not affect the SID register dump)" },
    CMDLINE_LIST_END
};

int bustrace_cmdline_options_init(void)
{
    return cmdline_register_options(cmdline_options);
}
