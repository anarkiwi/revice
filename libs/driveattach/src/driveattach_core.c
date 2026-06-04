/*
 * driveattach_core.c - DRIVE_ATTACH body decoder. See revice_driveattach.h.
 *
 * Logic-preserving port of the validation in the asid-vice fork's
 * monitor_binary_process_drive_attach().
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See LICENSE for copyright notice (GPL-2.0-or-later).
 */

#include "revice_driveattach.h"

#include <string.h>

da_status_t da_decode(const uint8_t *body, uint32_t length, da_request_t *out)
{
    uint8_t unit;
    uint8_t drive;
    uint8_t path_len;

    if (body == NULL || out == NULL) {
        return DA_ERR_PARAM;
    }
    if (length < 3) {
        return DA_ERR_LENGTH;
    }
    unit = body[0];
    drive = body[1];
    path_len = body[2];

    if (length < (uint32_t)(3 + path_len)) {
        return DA_ERR_LENGTH;
    }
    if (unit < 8 || unit > 11 || drive > 1) {
        return DA_ERR_PARAM;
    }

    memset(out, 0, sizeof(*out));
    out->unit = unit;
    out->drive = drive;

    if (path_len == 0) {
        out->detach = 1;
    } else {
        out->detach = 0;
        memcpy(out->path, &body[3], path_len);
        out->path[path_len] = '\0';
    }
    return DA_OK;
}
