/*
 * revice_video.h - native video-recording core (pure), extracted from the
 *                  asid-vice fork's src/monitor/monitor_binary.c
 *                  VIDEO_RECORD (0x79) opcode.
 *
 * video_core_binmon_record() parses the VIDEO_RECORD request body
 * ([action, path_len, path...]) and drives VICE's native screenshot/movie
 * recorder to start or stop a recording. The core owns the body validation,
 * the "already recording" guard, and the warp-mode save/restore policy
 * (recording forces warp OFF so frames are actually encoded, and stopping
 * restores the prior warp state). Everything that touches the emulator -
 * starting/stopping the ZMBV recorder, reading/setting warp mode - is injected
 * through the video_host_t vtable, so the logic builds and unit-tests with no
 * VICE present.
 *
 * The thin VICE adapter (vice/mon_video.c) fills video_host_t from VICE and
 * re-exports mon_video_binmon_record(), so the in-tree wiring
 * (monitor_binary.c opcode dispatch) is a single call.
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See LICENSE for copyright notice (GPL-2.0-or-later).
 */

#ifndef REVICE_VIDEO_H
#define REVICE_VIDEO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* VIDEO_RECORD request body actions (wire byte 0). */
#define VIDEO_ACTION_STOP   0
#define VIDEO_ACTION_START  1

/* Maximum path the core will copy out of the body (NUL-terminated). The wire
   path_len is a u8 so 255 is the largest a request can carry. */
#define VIDEO_MAX_PATH      256

/* Return codes from video_core_binmon_record(). 0 == success; the negatives
   map to the binmon protocol errors the adapter reports (see vice/mon_video.c).
   These mirror the original inline opcode's three failure modes exactly. */
enum video_result {
    VIDEO_OK              =  0,
    VIDEO_ERR_LENGTH      = -1,   /* malformed/short body -> CMD_INVALID_LENGTH */
    VIDEO_ERR_FAILURE     = -2    /* already recording or recorder refused start
                                     -> CMD_FAILURE */
};

/* Host operations the core needs from the emulator. */
typedef struct {
    /* screenshot_save("ZMBV", path, canvas): begin recording to `path`.
       Returns 0 on success, negative on failure (mirrors VICE's API). */
    int  (*start_recording)(void *ctx, const char *path);
    /* screenshot_stop_recording(): finalize/close the current recording. */
    void (*stop_recording)(void *ctx);
    /* screenshot_is_recording(): non-zero if a recording is in progress. */
    int  (*is_recording)(void *ctx);
    /* vsync_get_warp_mode(). */
    int  (*get_warp)(void *ctx);
    /* vsync_set_warp_mode(value). */
    void (*set_warp)(void *ctx, int value);
} video_host_t;

typedef struct {
    const video_host_t *host;
    void               *ctx;
    int                 saved_warp;       /* warp mode captured at start */
    int                 warp_was_saved;   /* 1 while a forced-warp-off is owed */
} video_core_t;

void video_core_init(video_core_t *v, const video_host_t *host, void *ctx);

/* Binary-monitor entry point. Parses the VIDEO_RECORD body and starts/stops
   recording. Returns a video_result code; the adapter maps it to the binmon
   response/error. */
int video_core_binmon_record(video_core_t *v, const uint8_t *body,
                             uint32_t length);

#ifdef __cplusplus
}
#endif

#endif /* REVICE_VIDEO_H */
