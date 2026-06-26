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

static const video_host_t host = {
    h_start, h_stop, h_is_recording, h_get_warp, h_set_warp
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
    REVICE_TEST_MAIN_END();
}
