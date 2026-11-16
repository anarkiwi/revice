/*
 * test_video.c - unit tests for the video-recording core, driven by a mock
 *                host.
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See LICENSE for copyright notice (GPL-2.0-or-later).
 */

#include "revice_video.h"
#include "revice_test.h"

#include <stdint.h>
#include <string.h>

typedef struct {
    int  recording;          /* is_recording() state */
    int  warp;               /* current warp mode */
    int  start_calls;
    int  stop_calls;
    int  start_should_fail;  /* make start_recording() return < 0 */
    char last_path[256];
    int  rendered;           /* rendered recording open in the host */
    unsigned int height;     /* last start_rendered() height */
    int  rstart_calls;
    int  rstop_calls;
    int  frames;             /* encode_frame() calls */
    int  rstart_should_fail;
    int  encode_fail_at;     /* encode_frame() fails on this call (1-based) */
} mock_t;

static int h_start(void *c, const char *path)
{
    mock_t *m = (mock_t *)c;
    m->start_calls++;
    strncpy(m->last_path, path, sizeof(m->last_path) - 1);
    m->last_path[sizeof(m->last_path) - 1] = '\0';
    if (m->start_should_fail) {
        return -1;
    }
    m->recording = 1;
    return 0;
}

static void h_stop(void *c)
{
    mock_t *m = (mock_t *)c;
    m->stop_calls++;
    m->recording = 0;
}

static int h_is_recording(void *c) { return ((mock_t *)c)->recording; }
static int h_get_warp(void *c) { return ((mock_t *)c)->warp; }
static void h_set_warp(void *c, int v) { ((mock_t *)c)->warp = v; }

static int h_rstart(void *c, const char *path, unsigned int height)
{
    mock_t *m = (mock_t *)c;
    m->rstart_calls++;
    strncpy(m->last_path, path, sizeof(m->last_path) - 1);
    m->last_path[sizeof(m->last_path) - 1] = '\0';
    m->height = height;
    if (m->rstart_should_fail) {
        return -1;
    }
    m->rendered = 1;
    return 0;
}

static void h_rstop(void *c)
{
    mock_t *m = (mock_t *)c;
    m->rstop_calls++;
    m->rendered = 0;
}

static int h_encode(void *c)
{
    mock_t *m = (mock_t *)c;
    CHECK(m->rendered);
    return ++m->frames == m->encode_fail_at ? -1 : 0;
}

static const video_host_t host = {
    h_start, h_stop, h_is_recording, h_get_warp, h_set_warp,
    h_rstart, h_rstop, h_encode
};

static void setup(video_core_t *v, mock_t *m)
{
    memset(m, 0, sizeof(*m));
    m->warp = 1;                 /* harness boots warped */
    video_core_init(v, &host, m);
}

/* Build a start body: [1, path_len, path...]. */
static uint32_t make_start(uint8_t *buf, const char *path)
{
    size_t n = strlen(path);
    buf[0] = VIDEO_ACTION_START;
    buf[1] = (uint8_t)n;
    memcpy(&buf[2], path, n);
    return (uint32_t)(2 + n);
}

static void test_start_forces_warp_off(void)
{
    video_core_t v;
    mock_t m;
    uint8_t body[64];
    uint32_t len = make_start(body, "/renders/out.avi");
    setup(&v, &m);
    CHECK_EQ_INT(video_core_binmon_record(&v, body, len), VIDEO_OK);
    CHECK_EQ_INT(m.start_calls, 1);
    CHECK_EQ_INT(m.recording, 1);
    CHECK_EQ_INT(m.warp, 0);                       /* warp forced off */
    CHECK(strcmp(m.last_path, "/renders/out.avi") == 0);
}

static void test_stop_restores_warp(void)
{
    video_core_t v;
    mock_t m;
    uint8_t start[64];
    uint8_t stop[1] = { VIDEO_ACTION_STOP };
    uint32_t len = make_start(start, "/renders/out.avi");
    setup(&v, &m);
    CHECK_EQ_INT(video_core_binmon_record(&v, start, len), VIDEO_OK);
    CHECK_EQ_INT(m.warp, 0);
    CHECK_EQ_INT(video_core_binmon_record(&v, stop, sizeof(stop)), VIDEO_OK);
    CHECK_EQ_INT(m.stop_calls, 1);
    CHECK_EQ_INT(m.recording, 0);
    CHECK_EQ_INT(m.warp, 1);                        /* prior warp restored */
}

static void test_empty_body_rejected(void)
{
    video_core_t v;
    mock_t m;
    setup(&v, &m);
    CHECK_EQ_INT(video_core_binmon_record(&v, NULL, 0), VIDEO_ERR_LENGTH);
}

static void test_start_missing_pathlen_rejected(void)
{
    video_core_t v;
    mock_t m;
    uint8_t body[1] = { VIDEO_ACTION_START };
    setup(&v, &m);
    CHECK_EQ_INT(video_core_binmon_record(&v, body, sizeof(body)),
                 VIDEO_ERR_LENGTH);
}

static void test_start_zero_pathlen_rejected(void)
{
    video_core_t v;
    mock_t m;
    uint8_t body[2] = { VIDEO_ACTION_START, 0 };
    setup(&v, &m);
    CHECK_EQ_INT(video_core_binmon_record(&v, body, sizeof(body)),
                 VIDEO_ERR_LENGTH);
}

static void test_start_truncated_path_rejected(void)
{
    video_core_t v;
    mock_t m;
    uint8_t body[4] = { VIDEO_ACTION_START, 10, 'a', 'b' }; /* claims 10, has 2 */
    setup(&v, &m);
    CHECK_EQ_INT(video_core_binmon_record(&v, body, sizeof(body)),
                 VIDEO_ERR_LENGTH);
}

static void test_double_start_refused(void)
{
    video_core_t v;
    mock_t m;
    uint8_t body[64];
    uint32_t len = make_start(body, "/renders/out.avi");
    setup(&v, &m);
    CHECK_EQ_INT(video_core_binmon_record(&v, body, len), VIDEO_OK);
    /* second start while recording -> failure, warp untouched */
    CHECK_EQ_INT(video_core_binmon_record(&v, body, len), VIDEO_ERR_FAILURE);
    CHECK_EQ_INT(m.start_calls, 1);
    CHECK_EQ_INT(m.warp, 0);
}

static void test_start_failure_restores_warp(void)
{
    video_core_t v;
    mock_t m;
    uint8_t body[64];
    uint32_t len = make_start(body, "/renders/out.avi");
    setup(&v, &m);
    m.start_should_fail = 1;
    CHECK_EQ_INT(video_core_binmon_record(&v, body, len), VIDEO_ERR_FAILURE);
    CHECK_EQ_INT(m.recording, 0);
    CHECK_EQ_INT(m.warp, 1);                        /* warp restored on failure */
}

/* Build a rendered start body: [2, h_lo, h_hi, path_len, path...]. */
static uint32_t make_rendered(uint8_t *buf, unsigned int height,
                              const char *path)
{
    size_t n = strlen(path);
    buf[0] = VIDEO_ACTION_START_RENDERED;
    buf[1] = (uint8_t)(height & 0xff);
    buf[2] = (uint8_t)(height >> 8);
    buf[3] = (uint8_t)n;
    memcpy(&buf[4], path, n);
    return (uint32_t)(4 + n);
}

static int act(video_core_t *v, uint8_t action)
{
    return video_core_binmon_record(v, &action, 1);
}

static void run_frames(video_core_t *v, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        video_core_frame(v);
    }
}

static void test_rendered_start_keeps_warp(void)
{
    video_core_t v;
    mock_t m;
    uint8_t body[300];
    uint32_t len = make_rendered(body, 720, "/renders/out.mp4");
    setup(&v, &m);
    CHECK_EQ_INT(video_core_binmon_record(&v, body, len), VIDEO_OK);
    CHECK_EQ_INT(m.rstart_calls, 1);
    CHECK_EQ_INT(m.start_calls, 0);
    CHECK_EQ_INT(m.height, 720);
    CHECK(strcmp(m.last_path, "/renders/out.mp4") == 0);
    CHECK_EQ_INT(m.warp, 1);
    CHECK_EQ_INT(act(&v, VIDEO_ACTION_STOP), VIDEO_OK);
    CHECK_EQ_INT(m.rstop_calls, 1);
    CHECK_EQ_INT(m.stop_calls, 0);
    CHECK_EQ_INT(m.warp, 1);
}

static void test_rendered_height_little_endian(void)
{
    video_core_t v;
    mock_t m;
    uint8_t body[300];
    uint32_t len = make_rendered(body, 0x1234, "a");
    setup(&v, &m);
    CHECK_EQ_INT(body[1], 0x34);
    CHECK_EQ_INT(video_core_binmon_record(&v, body, len), VIDEO_OK);
    CHECK_EQ_INT(m.height, 0x1234);
}

static void test_rendered_max_path(void)
{
    video_core_t v;
    mock_t m;
    uint8_t body[300];
    char path[256];
    uint32_t len;
    memset(path, 'p', 255);
    path[255] = '\0';
    len = make_rendered(body, 720, path);
    setup(&v, &m);
    CHECK_EQ_INT(video_core_binmon_record(&v, body, len), VIDEO_OK);
    CHECK_EQ_INT(strlen(m.last_path), 255);
}

static void test_rendered_frames_and_pause(void)
{
    video_core_t v;
    mock_t m;
    uint8_t body[300];
    uint32_t len = make_rendered(body, 720, "/r.mp4");
    setup(&v, &m);
    CHECK_EQ_INT(video_core_frame(&v), 0);          /* idle: nothing encoded */
    CHECK_EQ_INT(m.frames, 0);
    CHECK_EQ_INT(video_core_binmon_record(&v, body, len), VIDEO_OK);
    run_frames(&v, 10);
    CHECK_EQ_INT(act(&v, VIDEO_ACTION_PAUSE), VIDEO_OK);
    CHECK_EQ_INT(act(&v, VIDEO_ACTION_PAUSE), VIDEO_OK);   /* idempotent */
    run_frames(&v, 7);
    CHECK_EQ_INT(video_core_frame(&v), 1);          /* still active while paused */
    CHECK_EQ_INT(act(&v, VIDEO_ACTION_RESUME), VIDEO_OK);
    run_frames(&v, 5);
    CHECK_EQ_INT(m.frames, 15);
    CHECK_EQ_INT(act(&v, VIDEO_ACTION_STOP), VIDEO_OK);
    CHECK_EQ_INT(video_core_frame(&v), 0);
    CHECK_EQ_INT(m.frames, 15);
}

static void test_restart_clears_pause(void)
{
    video_core_t v;
    mock_t m;
    uint8_t body[300];
    uint32_t len = make_rendered(body, 720, "/r.mp4");
    setup(&v, &m);
    CHECK_EQ_INT(video_core_binmon_record(&v, body, len), VIDEO_OK);
    CHECK_EQ_INT(act(&v, VIDEO_ACTION_PAUSE), VIDEO_OK);
    CHECK_EQ_INT(act(&v, VIDEO_ACTION_STOP), VIDEO_OK);
    CHECK_EQ_INT(video_core_binmon_record(&v, body, len), VIDEO_OK);
    run_frames(&v, 3);
    CHECK_EQ_INT(m.frames, 3);
}

static void test_pause_resume_need_rendered(void)
{
    video_core_t v;
    mock_t m;
    uint8_t zmbv[64];
    uint32_t len = make_start(zmbv, "/renders/out.avi");
    setup(&v, &m);
    CHECK_EQ_INT(act(&v, VIDEO_ACTION_PAUSE), VIDEO_ERR_FAILURE);
    CHECK_EQ_INT(act(&v, VIDEO_ACTION_RESUME), VIDEO_ERR_FAILURE);
    CHECK_EQ_INT(video_core_binmon_record(&v, zmbv, len), VIDEO_OK);
    CHECK_EQ_INT(act(&v, VIDEO_ACTION_PAUSE), VIDEO_ERR_FAILURE);
    CHECK_EQ_INT(act(&v, VIDEO_ACTION_RESUME), VIDEO_ERR_FAILURE);
}

static void test_cross_mode_double_start_refused(void)
{
    video_core_t v;
    mock_t m;
    uint8_t zmbv[64];
    uint8_t rend[300];
    uint32_t zlen = make_start(zmbv, "/renders/out.avi");
    uint32_t rlen = make_rendered(rend, 720, "/r.mp4");
    setup(&v, &m);
    CHECK_EQ_INT(video_core_binmon_record(&v, zmbv, zlen), VIDEO_OK);
    CHECK_EQ_INT(video_core_binmon_record(&v, rend, rlen), VIDEO_ERR_FAILURE);
    CHECK_EQ_INT(m.rstart_calls, 0);
    CHECK_EQ_INT(act(&v, VIDEO_ACTION_STOP), VIDEO_OK);
    CHECK_EQ_INT(m.warp, 1);
    CHECK_EQ_INT(video_core_binmon_record(&v, rend, rlen), VIDEO_OK);
    CHECK_EQ_INT(video_core_binmon_record(&v, rend, rlen), VIDEO_ERR_FAILURE);
    CHECK_EQ_INT(video_core_binmon_record(&v, zmbv, zlen), VIDEO_ERR_FAILURE);
    CHECK_EQ_INT(m.rstart_calls, 1);
    CHECK_EQ_INT(m.start_calls, 1);
    CHECK_EQ_INT(m.warp, 1);
}

static void test_rendered_start_failure(void)
{
    video_core_t v;
    mock_t m;
    uint8_t body[300];
    uint32_t len = make_rendered(body, 720, "/r.mp4");
    setup(&v, &m);
    m.rstart_should_fail = 1;
    CHECK_EQ_INT(video_core_binmon_record(&v, body, len), VIDEO_ERR_FAILURE);
    CHECK_EQ_INT(video_core_frame(&v), 0);
    CHECK_EQ_INT(act(&v, VIDEO_ACTION_PAUSE), VIDEO_ERR_FAILURE);
    CHECK_EQ_INT(m.frames, 0);
    CHECK_EQ_INT(m.warp, 1);
}

static void test_encode_failure_stops(void)
{
    video_core_t v;
    mock_t m;
    uint8_t body[300];
    uint32_t len = make_rendered(body, 720, "/r.mp4");
    setup(&v, &m);
    m.encode_fail_at = 3;
    CHECK_EQ_INT(video_core_binmon_record(&v, body, len), VIDEO_OK);
    CHECK_EQ_INT(video_core_frame(&v), 1);
    CHECK_EQ_INT(video_core_frame(&v), 1);
    CHECK_EQ_INT(video_core_frame(&v), 0);
    CHECK_EQ_INT(m.rstop_calls, 1);
    run_frames(&v, 4);
    CHECK_EQ_INT(m.frames, 3);
    CHECK_EQ_INT(video_core_binmon_record(&v, body, len), VIDEO_OK);
}

static void test_rendered_malformed_rejected(void)
{
    static const struct {
        uint8_t body[8];
        uint32_t len;
    } bad[] = {
        { { VIDEO_ACTION_START_RENDERED }, 1 },
        { { VIDEO_ACTION_START_RENDERED, 0xd0 }, 2 },
        { { VIDEO_ACTION_START_RENDERED, 0xd0, 0x02 }, 3 },
        { { VIDEO_ACTION_START_RENDERED, 0xd0, 0x02, 0 }, 4 },
        { { VIDEO_ACTION_START_RENDERED, 0xd0, 0x02, 3, 'a', 'b' }, 6 },
        { { VIDEO_ACTION_START_RENDERED, 0x00, 0x00, 1, 'a' }, 5 },
        { { 5 }, 1 },
        { { 0xff, 1, 'a' }, 3 },
    };
    video_core_t v;
    mock_t m;
    size_t i;
    setup(&v, &m);
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        CHECK_EQ_INT(video_core_binmon_record(&v, bad[i].body, bad[i].len),
                     VIDEO_ERR_LENGTH);
    }
    CHECK_EQ_INT(m.rstart_calls + m.start_calls, 0);
    CHECK_EQ_INT(m.warp, 1);
}

int main(void)
{
    test_start_forces_warp_off();
    test_stop_restores_warp();
    test_empty_body_rejected();
    test_start_missing_pathlen_rejected();
    test_start_zero_pathlen_rejected();
    test_start_truncated_path_rejected();
    test_double_start_refused();
    test_start_failure_restores_warp();
    test_rendered_start_keeps_warp();
    test_rendered_height_little_endian();
    test_rendered_max_path();
    test_rendered_frames_and_pause();
    test_restart_clears_pause();
    test_pause_resume_need_rendered();
    test_cross_mode_double_start_refused();
    test_rendered_start_failure();
    test_encode_failure_stops();
    test_rendered_malformed_rejected();
    REVICE_TEST_MAIN_END();
}
