/*
 * mon_video.c - VICE adapter for the revice video-recording core.
 *
 * Fills a video_host_t vtable from VICE and forwards the historical
 * mon_video_binmon_record() API to the core in libs/video. All of the logic
 * (body parsing, the already-recording guard, the pause state, the warp
 * save/restore policy) lives in the core and is unit-tested there.
 *
 * ZMBV mode drives VICE's native screenshot/movie recorder.
 *
 * Rendered mode renders each emulated frame exactly as the GTK3 window does
 * (video_canvas_refresh_all() region, scaled by videoconfig->scalex/scaley,
 * through video_canvas_render() into a 32-bit canvas_physical_width x
 * canvas_physical_height buffer with the renderer's colour tables, R,G,B,pad
 * bytes on little-endian hosts) and pipes it to an `ffmpeg` child, which
 * scales it with lanczos to the requested height at the display aspect and
 * encodes H.264. The frame hook runs on the emulation thread: a one-shot
 * vsync_on_vsync_do() callback renders the frame and re-arms a maincpu alarm
 * due immediately, whose handler queues the callback for the next vsync.
 * (Queueing directly from the callback would run it again in the same vsync,
 * since execute_vsync_callbacks() drains until the queue is empty.)
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

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "alarm.h"
#include "archdep.h"
#include "lib.h"
#include "log.h"
#include "machine-video.h"
#include "machine.h"
#include "maincpu.h"
#include "mon_video.h"
#include "screenshot.h"
#include "types.h"
#include "video.h"
#include "videoarch.h"
#include "viewport.h"
#include "vsync.h"

#include "revice_video.h"

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

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

/* ---- rendered recording ---- */

extern char **environ;

static struct {
    video_canvas_t *canvas;
    uint8_t *buf;            /* w x h x 4 rendered frame */
    unsigned int w, h;
    int fd;                  /* ffmpeg stdin */
    pid_t pid;
    unsigned long frames;
    alarm_t *alarm;
    int armed;               /* alarm set or vsync callback queued */
} rec = { NULL, NULL, 0, 0, -1, 0, 0, NULL, 0 };

static video_core_t vid;
static int vid_inited = 0;

static void rec_arm(void);

static void rec_vsync(void *param)
{
    (void)param;
    rec.armed = 0;
    if (video_core_frame(&vid)) {
        rec_arm();
    }
}

static void rec_alarm(CLOCK offset, void *data)
{
    (void)offset;
    (void)data;
    alarm_unset(rec.alarm);
    vsync_on_vsync_do(rec_vsync, NULL);
}

static void rec_arm(void)
{
    if (rec.armed) {
        return;
    }
    if (rec.alarm == NULL) {
        rec.alarm = alarm_new(maincpu_alarm_context, "MonVideoRecord",
                              rec_alarm, NULL);
    }
    rec.armed = 1;
    alarm_set(rec.alarm, maincpu_clk);
}

static double display_pixel_aspect(video_canvas_t *c)
{
    switch (c->videoconfig->aspect_mode) {
        case VIDEO_ASPECT_MODE_TRUE:
            return c->geometry->pixel_aspect_ratio;
        case VIDEO_ASPECT_MODE_CUSTOM:
            return c->videoconfig->aspect_ratio;
        default:
            return 1.0;
    }
}

static int h_start_rendered(void *ctx, const char *path, unsigned int height)
{
    video_canvas_t *c = machine_video_canvas_get(0);
    posix_spawn_file_actions_t fa;
    char size[32], rate[48], scale[64];
    char *argv[] = {
        "ffmpeg", "-nostdin", "-loglevel", "error", "-y",
        "-f", "rawvideo", "-pixel_format", "rgb0",
        "-video_size", size, "-framerate", rate, "-i", "pipe:0",
        "-vf", scale, "-c:v", "libx264", "-crf", "18",
        "-pix_fmt", "yuv420p", (char *)path, NULL
    };
    unsigned int width;
    int fds[2];
    int rc;

    (void)ctx;
    if (c == NULL || c->draw_buffer->canvas_physical_width == 0
        || c->draw_buffer->canvas_physical_height == 0) {
        return -1;
    }
    rec.canvas = c;
    rec.w = c->draw_buffer->canvas_physical_width;
    rec.h = c->draw_buffer->canvas_physical_height;
    width = 2 * (unsigned int)lround(rec.w * display_pixel_aspect(c) * height
                                     / rec.h / 2.0);
    if (width == 0) {
        width = 2;
    }
    snprintf(size, sizeof(size), "%ux%u", rec.w, rec.h);
    snprintf(rate, sizeof(rate), "%ld/%ld", machine_get_cycles_per_second(),
             machine_get_cycles_per_frame());
    snprintf(scale, sizeof(scale), "scale=%u:%u:flags=lanczos", width, height);

    if (pipe(fds) < 0) {
        return -1;
    }
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, fds[0], STDIN_FILENO);
    rc = posix_spawnp(&rec.pid, argv[0], &fa, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    close(fds[0]);
    if (rc != 0) {
        log_error(LOG_DEFAULT, "VIDEO_RECORD: cannot run ffmpeg: %s",
                  strerror(rc));
        close(fds[1]);
        return -1;
    }
    rec.fd = fds[1];
    rec.buf = lib_calloc((size_t)rec.w * rec.h, 4);
    rec.frames = 0;
    log_message(LOG_DEFAULT, "VIDEO_RECORD: rendered %s %s fps -> %ux%u %s",
                size, rate, width, height, path);
    rec_arm();
    return 0;
}

static void h_stop_rendered(void *ctx)
{
    int status;

    (void)ctx;
    if (rec.fd >= 0) {
        close(rec.fd);
        rec.fd = -1;
        while (waitpid(rec.pid, &status, 0) < 0 && errno == EINTR) {
        }
        log_message(LOG_DEFAULT, "VIDEO_RECORD: rendered %lu frames",
                    rec.frames);
    }
    lib_free(rec.buf);
    rec.buf = NULL;
}

static int h_encode_frame(void *ctx)
{
    video_canvas_t *c = rec.canvas;
    viewport_t *vp = c->viewport;
    geometry_t *g = c->geometry;
    unsigned int sx = c->videoconfig->scalex;
    unsigned int sy = c->videoconfig->scaley;
    unsigned int xi = vp->x_offset * sx;
    unsigned int yi = vp->y_offset * sy;
    unsigned int w = MIN(c->draw_buffer->canvas_width,
                         g->screen_size.width - vp->first_x) * sx;
    unsigned int h = MIN(c->draw_buffer->canvas_height,
                         vp->last_line - vp->first_line + 1) * sy;
    const uint8_t *p = rec.buf;
    size_t left = (size_t)rec.w * rec.h * 4;
    ssize_t n = 0;

    (void)ctx;
    if (xi < rec.w && yi < rec.h) {
        video_canvas_render(c, rec.buf, (int)MIN(w, rec.w - xi),
                            (int)MIN(h, rec.h - yi),
                            (int)(vp->first_x + g->extra_offscreen_border_left),
                            (int)vp->first_line, (int)xi, (int)yi,
                            (int)rec.w * 4);
    }
    archdep_signals_pipe_set();
    while (left > 0 && ((n = write(rec.fd, p, left)) > 0
                        || (n < 0 && errno == EINTR))) {
        if (n > 0) {
            p += n;
            left -= (size_t)n;
        }
    }
    archdep_signals_pipe_unset();
    if (left > 0) {
        log_error(LOG_DEFAULT, "VIDEO_RECORD: ffmpeg pipe closed, stopping");
        return -1;
    }
    rec.frames++;
    return 0;
}

static const video_host_t vid_host = {
    h_start_recording,
    h_stop_recording,
    h_is_recording,
    h_get_warp,
    h_set_warp,
    h_start_rendered,
    h_stop_rendered,
    h_encode_frame
};

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
