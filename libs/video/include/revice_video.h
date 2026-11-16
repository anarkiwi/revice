/*
 * revice_video.h - native video-recording core (pure), extracted from the
 *                  asid-vice fork's src/monitor/monitor_binary.c
 *                  VIDEO_RECORD (0x79) opcode.
 *
 * video_core_binmon_record() parses the VIDEO_RECORD request body and
 * starts, stops, pauses or resumes one of two recording modes:
 *
 *  - ZMBV: VICE's native screenshot/movie recorder. Recording forces warp OFF
 *    (the recorder skips frames in warp) and stopping restores the prior warp
 *    state.
 *  - rendered: every emulated frame, after the video chip's render filter
 *    (CRT emulation), scaled to a requested height and encoded by the host.
 *    Warp is left alone; frames are encoded in warp too. The host calls
 *    video_core_frame() once per emulated frame and the core decides whether
 *    that frame is encoded (rendered recording active and not paused).
 *
 * The core owns body validation, the "already recording" guard, the pause
 * state and the warp save/restore policy. Everything that touches the
 * emulator is injected through the video_host_t vtable, so the logic builds
 * and unit-tests with no VICE present.
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
#define VIDEO_ACTION_STOP           0  /* [0] */
#define VIDEO_ACTION_START          1  /* [1, len, path...] ZMBV */
#define VIDEO_ACTION_START_RENDERED 2  /* [2, h_lo, h_hi, len, path...] */
#define VIDEO_ACTION_PAUSE          3  /* [3] */
#define VIDEO_ACTION_RESUME         4  /* [4] */

/* Maximum path the core will copy out of the body (NUL-terminated). The wire
   path_len is a u8 so 255 is the largest a request can carry. */
#define VIDEO_MAX_PATH      256

/* Return codes from video_core_binmon_record(). 0 == success; the negatives
   map to the binmon protocol errors the adapter reports (see vice/mon_video.c).
   These mirror the original inline opcode's three failure modes exactly. */
enum video_result {
    VIDEO_OK              =  0,
    VIDEO_ERR_LENGTH      = -1,   /* malformed/short body -> CMD_INVALID_LENGTH */
    VIDEO_ERR_FAILURE     = -2    /* already recording, recorder refused start,
                                     or pause/resume with no rendered
                                     recording -> CMD_FAILURE */
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
    /* Begin a rendered recording to `path`, scaled to `height` pixels high,
       and start calling video_core_frame() every emulated frame. Returns 0
       on success, negative on failure. */
    int  (*start_rendered)(void *ctx, const char *path, unsigned int height);
    /* Finalize the rendered recording (flush and wait for the encoder). */
    void (*stop_rendered)(void *ctx);
    /* Render and encode the current frame. Returns 0 on success, negative
       on failure (the core then stops the recording). */
    int  (*encode_frame)(void *ctx);
} video_host_t;

typedef struct {
    const video_host_t *host;
    void               *ctx;
    int                 saved_warp;       /* warp mode captured at start */
    int                 warp_was_saved;   /* 1 while a forced-warp-off is owed */
    int                 rendered;         /* rendered recording active */
    int                 paused;           /* rendered frames not encoded */
} video_core_t;

void video_core_init(video_core_t *v, const video_host_t *host, void *ctx);

/* Binary-monitor entry point. Parses the VIDEO_RECORD body and starts/stops
   recording. Returns a video_result code; the adapter maps it to the binmon
   response/error. */
int video_core_binmon_record(video_core_t *v, const uint8_t *body,
                             uint32_t length);

/* Per-frame entry point for a rendered recording. Encodes the frame unless
   paused; returns non-zero while the rendered recording remains active. */
int video_core_frame(video_core_t *v);

#ifdef __cplusplus
}
#endif

#endif /* REVICE_VIDEO_H */
