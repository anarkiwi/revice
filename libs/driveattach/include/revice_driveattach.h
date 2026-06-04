/*
 * revice_driveattach.h - decoder for the binmon DRIVE_ATTACH (0x78) command
 *                        body, extracted from the asid-vice fork's
 *                        src/monitor/monitor_binary.c.
 *
 * DRIVE_ATTACH is a thin runtime attach/detach primitive that the standard
 * RESOURCE_SET opcode cannot express (it refuses zero-length values and the
 * disk-image slot is not bound to a resource at all). The core only validates
 * and decodes the request body; the adapter calls VICE's
 * file_system_attach_disk() / file_system_detach_disk() with the result.
 *
 * Wire body: unit:u8 (8..11), drive:u8 (0..1), path_len:u8, path[path_len].
 * path_len == 0 means detach; otherwise attach the (NUL-terminated) path.
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See LICENSE for copyright notice (GPL-2.0-or-later).
 */

#ifndef REVICE_DRIVEATTACH_H
#define REVICE_DRIVEATTACH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Decode status. The adapter maps these onto binmon error codes:
   DA_ERR_LENGTH -> e_MON_ERR_CMD_INVALID_LENGTH,
   DA_ERR_PARAM  -> e_MON_ERR_INVALID_PARAMETER. */
typedef enum {
    DA_OK         = 0,
    DA_ERR_LENGTH = -1,
    DA_ERR_PARAM  = -2
} da_status_t;

#define DA_PATH_MAX 256   /* path_len is u8, +1 for the NUL terminator */

typedef struct {
    int     detach;            /* 1 = detach the slot, 0 = attach `path` */
    uint8_t unit;              /* 8..11 */
    uint8_t drive;             /* 0..1  */
    char    path[DA_PATH_MAX]; /* NUL-terminated; empty when detach */
} da_request_t;

/* Validate + decode a DRIVE_ATTACH body. Returns DA_OK and fills *out on
   success. */
da_status_t da_decode(const uint8_t *body, uint32_t length, da_request_t *out);

#ifdef __cplusplus
}
#endif

#endif /* REVICE_DRIVEATTACH_H */
