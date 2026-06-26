/*
 * mon_video.c - VICE adapter for the revice video-recording core.
 *
 * Thin glue: it fills a video_host_t vtable from VICE (the native ZMBV
 * screenshot/movie recorder and the warp-mode controls) and forwards the
 * historical mon_video_binmon_record() API to the core in libs/video. All of
 * the logic (body parsing, the already-recording guard, the warp save/restore
 * policy) lives in the core and is unit-tested there; this file only wires it
 * to the emulator.
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

#include "machine-video.h"
#include "mon_video.h"
#include "screenshot.h"
#include "types.h"
#include "vsync.h"

#include "revice_video.h"

/* ---- host ops ---- */

static int h_start_recording(void *ctx, const char *path)
{
    (void)ctx;
    /* In-tree ZMBV gfxoutput driver: lossless ZMBV inside an AVI container;
       depends only on zlib, no external ffmpeg. */
    return screenshot_save("ZMBV", path, machine_video_canvas_get(0));
}

static void h_stop_recording(void *ctx)
{
    (void)ctx;
    screenshot_stop_recording();
}

static int h_is_recording(void *ctx)
{
    (void)ctx;
    return screenshot_is_recording();
}

static int h_get_warp(void *ctx)
{
    (void)ctx;
    return vsync_get_warp_mode();
}

static void h_set_warp(void *ctx, int value)
{
    (void)ctx;
    vsync_set_warp_mode(value);
}

static const video_host_t vid_host = {
    h_start_recording,
    h_stop_recording,
    h_is_recording,
    h_get_warp,
    h_set_warp
};

static video_core_t vid;
static int vid_inited = 0;

static void vid_ensure(void)
{
    if (!vid_inited) {
        video_core_init(&vid, &vid_host, NULL);
        vid_inited = 1;
    }
}

/* ---- public API (delegates to the core) ---- */

int mon_video_binmon_record(const unsigned char *body, int length)
{
    vid_ensure();
    if (length < 0) {
        return VIDEO_ERR_LENGTH;
    }
    return video_core_binmon_record(&vid, (const uint8_t *)body,
                                    (uint32_t)length);
}
