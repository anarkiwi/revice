/*
 * checkpoint_core.c - silent-checkpoint body decode. See revice_checkpoint.h.
 *
 * Mirrors the asid-vice fork's monitor_binary_process_checkpoint_set():
 *     if (command->length >= 10) silent = (bool)body[9];
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See LICENSE for copyright notice (GPL-2.0-or-later).
 */

#include "revice_checkpoint.h"

#include <stddef.h>

int checkpoint_decode_silent(const uint8_t *body, uint32_t length)
{
    if (body == NULL) {
        return 0;
    }
    if (length >= (CHECKPOINT_SILENT_BODY_OFFSET + 1)) {
        return body[CHECKPOINT_SILENT_BODY_OFFSET] ? 1 : 0;
    }
    return 0;
}
