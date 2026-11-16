/*
 * video_core.c - native video-recording core. See revice_video.h.
 *
 * The ZMBV path is a logic-preserving port of the asid-vice fork's
 * monitor_binary_process_video_record() (src/monitor/monitor_binary.c).
 * VICE-specific calls (screenshot_save / screenshot_stop_recording /
 * screenshot_is_recording, vsync_get_warp_mode / vsync_set_warp_mode, the
 * rendered-frame encoder) are video_host_t callbacks; the warp save/restore
 * flags and the rendered/paused state are fields of video_core_t so the core
 * has no VICE or global-state dependency.
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

/* Copy a [len, path...] field at body[off] into path; 0 if malformed. */
static int parse_path(const uint8_t *body, uint32_t length, uint32_t off,
                      char *path)
{
    uint8_t path_len;

    if (length <= off) {
        return 0;
    }
    path_len = body[off];
    if (path_len == 0 || off + 1 + path_len > length) {
        return 0;
    }
    memcpy(path, &body[off + 1], path_len);
    path[path_len] = '\0';
    return 1;
}

static int any_recording(video_core_t *v)
{
    return v->rendered || v->host->is_recording(v->ctx);
}

static void stop_rendered(video_core_t *v)
{
    v->host->stop_rendered(v->ctx);
    v->rendered = 0;
    v->paused = 0;
}

static int start_zmbv(video_core_t *v, const char *path)
{
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

int video_core_binmon_record(video_core_t *v, const uint8_t *body,
                             uint32_t length)
{
    char path[VIDEO_MAX_PATH];
    unsigned int height;

    if (length < 1) {
        return VIDEO_ERR_LENGTH;
    }

    switch (body[0]) {
        case VIDEO_ACTION_STOP:
            if (v->rendered) {
                stop_rendered(v);
                return VIDEO_OK;
            }
            v->host->stop_recording(v->ctx);
            if (v->warp_was_saved) {
                v->host->set_warp(v->ctx, v->saved_warp);
                v->warp_was_saved = 0;
            }
            return VIDEO_OK;

        case VIDEO_ACTION_START:
            if (!parse_path(body, length, 1, path)) {
                return VIDEO_ERR_LENGTH;
            }
            /* already recording; refuse rather than silently leak the file */
            return any_recording(v) ? VIDEO_ERR_FAILURE : start_zmbv(v, path);

        case VIDEO_ACTION_START_RENDERED:
            if (length < 3 || !parse_path(body, length, 3, path)) {
                return VIDEO_ERR_LENGTH;
            }
            height = (unsigned int)body[1] | ((unsigned int)body[2] << 8);
            if (height == 0) {
                return VIDEO_ERR_LENGTH;
            }
            if (any_recording(v)
                || v->host->start_rendered(v->ctx, path, height) < 0) {
                return VIDEO_ERR_FAILURE;
            }
            v->rendered = 1;
            v->paused = 0;
            return VIDEO_OK;

        case VIDEO_ACTION_PAUSE:
        case VIDEO_ACTION_RESUME:
            if (!v->rendered) {
                return VIDEO_ERR_FAILURE;
            }
            v->paused = body[0] == VIDEO_ACTION_PAUSE;
            return VIDEO_OK;

        default:
            return VIDEO_ERR_LENGTH;
    }
}

int video_core_frame(video_core_t *v)
{
    if (v->rendered && !v->paused && v->host->encode_frame(v->ctx) < 0) {
        stop_rendered(v);
    }
    return v->rendered;
}
