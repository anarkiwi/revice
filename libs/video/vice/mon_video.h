/*
 * mon_video.h - Monitor native video-recording (VICE-facing API).
 *
 * This is the stable public contract the in-tree wiring depends on
 * (src/monitor/monitor_binary.c VIDEO_RECORD opcode 0x79). The implementation
 * in mon_video.c is a thin adapter over the revice video core (libs/video);
 * the symbol name below is unchanged so the wiring is a single call.
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice.
 */

#ifndef VICE_MON_VIDEO_H
#define VICE_MON_VIDEO_H

#include "types.h"

/* Pulls in the VIDEO_OK / VIDEO_ERR_LENGTH / VIDEO_ERR_FAILURE result codes the
   opcode dispatch maps to binmon responses. */
#include "revice_video.h"

/* Binary-monitor entry point for VIDEO_RECORD (0x79). Parses the request body
   ([action:u8, path_len:u8, path:u8 × path_len]; action 1 = start, 0 = stop)
   and drives VICE's native ZMBV recorder. Returns 0 on success, or a negative
   revice video_result code: VIDEO_ERR_LENGTH (-1) for a malformed/short body,
   VIDEO_ERR_FAILURE (-2) when already recording or the recorder refused start.
   See revice_video.h / the fork README for the exact body layout. */
int mon_video_binmon_record(const unsigned char *body, int length);

#endif
