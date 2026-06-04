/*
 * mon_screen.c - VICE adapter: machine-agnostic shell for the monitor's
 *                `screenscrape` / SCREEN_GET commands.
 *
 * Keeps the provider-registration plumbing (a C64 build registers its packer
 * via mon_screen_register_provider) and forwards text rendering to the revice
 * screen core (screen_render). The packing logic lives in c64screen.c +
 * libs/screen.
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice.
 */

#include "vice.h"

#include <stddef.h>

#include "mon_screen.h"
#include "monitor.h"
#include "montypes.h"
#include "types.h"

#include "revice_screen.h"

/* The VICE-facing response size must match the core's wire size. */
static void mon_screen_static_checks(void)
{
    (void)sizeof(char[(MON_SCREEN_BINMON_GET_RESPONSE_SIZE ==
                       SCREEN_RESPONSE_SIZE) ? 1 : -1]);
    (void)mon_screen_static_checks;
}

static mon_screen_provider_t s_provider = NULL;

void mon_screen_register_provider(mon_screen_provider_t provider)
{
    s_provider = provider;
}

int mon_screen_binmon_get(uint8_t *out, uint32_t *out_length)
{
    if (out == NULL || out_length == NULL) {
        return -1;
    }
    if (*out_length < MON_SCREEN_BINMON_GET_RESPONSE_SIZE) {
        return -1;
    }
    if (s_provider == NULL) {
        return -1;                    /* not supported on this machine */
    }
    return s_provider(out, *out_length, out_length);
}

static void mon_screen_emit(void *ctx, const char *str)
{
    (void)ctx;
    mon_out("%s", str);
}

void mon_screen_show(const char *args)
{
    uint8_t buf[MON_SCREEN_BINMON_GET_RESPONSE_SIZE];
    uint32_t got = sizeof(buf);
    int rc;
    int raw = (args != NULL && *args != '\0');

    if (s_provider == NULL) {
        mon_out("screen: not supported on this machine "
                "(provider registered only by C64 builds)\n");
        return;
    }

    rc = s_provider(buf, sizeof(buf), &got);
    if (rc != 0 || got < MON_SCREEN_HEADER_BYTES) {
        mon_out("screen: provider returned error %d (got %u bytes)\n",
                rc, (unsigned)got);
        return;
    }

    screen_render(buf, got, raw, mon_screen_emit, NULL);
}
