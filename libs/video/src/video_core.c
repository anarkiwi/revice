/*
 * video_core.c - native video-recording core. See revice_video.h.
 *
 * Logic-preserving port of the asid-vice fork's
 * monitor_binary_process_video_record() (src/monitor/monitor_binary.c).
 * VICE-specific calls (screenshot_save / screenshot_stop_recording /
 * screenshot_is_recording, vsync_get_warp_mode / vsync_set_warp_mode) became
 * video_host_t callbacks; the static warp save/restore flags became fields of
 * video_core_t so the core has no VICE or global-state dependency.
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See LICENSE for copyright notice (GPL-2.0-or-later).
 */

#include "revice_video.h"

#include <string.h>

void video_core_init(video_core_t *v, const video_host_t *host, void *ctx)
{
    memset(v, 0, sizeof(*v));
    v->host = host;
    v->ctx = ctx;
}

int video_core_binmon_record(video_core_t *v, const uint8_t *body,
                             uint32_t length)
{
    uint8_t action;
    uint8_t path_len;
    char path[VIDEO_MAX_PATH];

    if (length < 1) {
        return VIDEO_ERR_LENGTH;
    }

    action = body[0];

    if (action == VIDEO_ACTION_STOP) {
        /* stop */
        v->host->stop_recording(v->ctx);
        if (v->warp_was_saved) {
            v->host->set_warp(v->ctx, v->saved_warp);
            v->warp_was_saved = 0;
        }
        return VIDEO_OK;
    }

    /* start: body is action(1) + path_len(1) + path */
    if (length < 2) {
        return VIDEO_ERR_LENGTH;
    }

    path_len = body[1];
    if (path_len == 0 || (uint32_t)(2 + path_len) > length) {
        return VIDEO_ERR_LENGTH;
    }

    memcpy(path, &body[2], path_len);
    path[path_len] = '\0';

    if (v->host->is_recording(v->ctx)) {
        /* already recording; refuse rather than silently leak the file */
        return VIDEO_ERR_FAILURE;
    }

    /* Force warp off so screenshot_save_core() actually encodes frames. */
    v->saved_warp = v->host->get_warp(v->ctx);
    v->warp_was_saved = 1;
    v->host->set_warp(v->ctx, 0);

    if (v->host->start_recording(v->ctx, path) < 0) {
        /* restore warp on failure so we don't leave the machine crawling */
        v->host->set_warp(v->ctx, v->saved_warp);
        v->warp_was_saved = 0;
        return VIDEO_ERR_FAILURE;
    }

    return VIDEO_OK;
}
