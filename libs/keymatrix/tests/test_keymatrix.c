/*
 * test_keymatrix.c - unit tests for the keymatrix core, driven by a mock host.
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See LICENSE for copyright notice (GPL-2.0-or-later).
 */

#include "revice_keymatrix.h"
#include "revice_test.h"

#include <stdint.h>
#include <string.h>

typedef struct {
    int      keyarr[8];      /* matrix bits per row */
    int      custom[5];      /* custom-key state by KM_CUSTOM_* id */
    uint64_t clock;
    long     cpf;
    uint64_t alarm_clk;
    int      alarm_set;
    int      clear_count;
    int      last_set_row, last_set_col, last_set_val;
    char     out[32768];
} mock_t;

static void h_set(void *c, int row, int col, int val)
{
    mock_t *m = (mock_t *)c;
    m->last_set_row = row;
    m->last_set_col = col;
    m->last_set_val = val;
    if (row >= 0 && row < 8 && col >= 0 && col < 8) {
        if (val) {
            m->keyarr[row] |= (1 << col);
        } else {
            m->keyarr[row] &= ~(1 << col);
        }
    } else {
        int id = 0;
        if (row == KM_ROW_RESTORE_1 && col == KM_COL_RESTORE_1) id = KM_CUSTOM_RESTORE1;
        else if (row == KM_ROW_RESTORE_2 && col == KM_COL_RESTORE_2) id = KM_CUSTOM_RESTORE2;
        else if (row == KM_ROW_CAPSLOCK && col == KM_COL_CAPSLOCK) id = KM_CUSTOM_CAPS;
        else if (row == KM_ROW_4080COLUMN && col == KM_COL_4080COLUMN) id = KM_CUSTOM_4080;
        if (id) m->custom[id] = val ? 1 : 0;
    }
}

static int h_get(void *c, int row) { return ((mock_t *)c)->keyarr[row]; }
static int h_custom(void *c, int id) { return ((mock_t *)c)->custom[id]; }
static void h_clear(void *c)
{
    mock_t *m = (mock_t *)c;
    memset(m->keyarr, 0, sizeof(m->keyarr));
    m->clear_count++;
}
static uint64_t h_now(void *c) { return ((mock_t *)c)->clock; }
static long h_cpf(void *c) { return ((mock_t *)c)->cpf; }
static void h_setalarm(void *c, uint64_t clk)
{
    mock_t *m = (mock_t *)c;
    m->alarm_clk = clk;
    m->alarm_set = 1;
}
static void h_cancelalarm(void *c) { ((mock_t *)c)->alarm_set = 0; }
static void h_emit(void *c, const char *s)
{
    mock_t *m = (mock_t *)c;
    size_t cur = strlen(m->out);
    size_t add = strlen(s);
    if (cur + add < sizeof(m->out)) {
        memcpy(m->out + cur, s, add + 1);
    }
}

static const km_host_t host = {
    h_set, h_get, h_custom, h_clear, h_now, h_cpf,
    h_setalarm, h_cancelalarm, h_emit
};

static void setup(km_core_t *km, mock_t *m)
{
    memset(m, 0, sizeof(*m));
    m->cpf = 19656;
    m->clock = 1000;
    km_core_init(km, &host, m);
}

static void test_names(void)
{
    km_core_t km;
    mock_t m;
    setup(&km, &m);
    km_core_names(&km);
    CHECK(strstr(m.out, "LSHIFT") != NULL);
    CHECK(strstr(m.out, "RUNSTOP") != NULL);
    CHECK(strstr(m.out, "RESTORE") != NULL);
}

static void test_tap_single(void)
{
    km_core_t km;
    mock_t m;
    setup(&km, &m);
    km_core_tap(&km, "a");                 /* A = row 1, col 2 */
    CHECK(m.keyarr[1] & (1 << 2));
    CHECK_EQ_INT(km.active, 1);
    CHECK_EQ_INT(km.mode, KM_MODE_TAP_OBSERVED);
    CHECK_EQ_INT(m.alarm_set, 1);
    CHECK(strstr(m.out, "tap 1 key") != NULL);
}

static void test_tap_chord(void)
{
    km_core_t km;
    mock_t m;
    setup(&km, &m);
    km_core_tap(&km, "lshift a");          /* LSHIFT = 1,7 ; A = 1,2 */
    CHECK(m.keyarr[1] & (1 << 7));
    CHECK(m.keyarr[1] & (1 << 2));
    CHECK_EQ_INT(km.n_keys, 2);
}

static void test_unknown_key(void)
{
    km_core_t km;
    mock_t m;
    setup(&km, &m);
    km_core_tap(&km, "notakey");
    CHECK_EQ_INT(km.active, 0);
    CHECK(strstr(m.out, "unknown key") != NULL);
}

static void test_rc_pair(void)
{
    km_core_t km;
    mock_t m;
    setup(&km, &m);
    km_core_tap(&km, "7,7");               /* RUN/STOP */
    CHECK(m.keyarr[7] & (1 << 7));
}

static void test_observed_release(void)
{
    km_core_t km;
    mock_t m;
    setup(&km, &m);
    km_core_tap(&km, "a");                 /* row 1 */
    /* Program drives all rows low and reads PA: row 1 bit clear -> sampled. */
    km_core_cia1_pa_read(&km, 0x00);
    CHECK_EQ_INT(km.cia1_reads_total, 1);
    CHECK_EQ_INT(km.cia1_reads_sampling, 1);
    CHECK_EQ_INT(km.last_reason, KM_RELEASE_OBSERVED);
    CHECK_EQ_INT(m.alarm_clk, m.clock + 1); /* re-armed for next cycle */
    /* Alarm fires -> release. */
    km_core_on_alarm(&km);
    CHECK_EQ_INT(km.active, 0);
    CHECK_EQ_INT((m.keyarr[1] & (1 << 2)) != 0, 0);
    CHECK_EQ_INT(km.last.reason, KM_RELEASE_OBSERVED);
    CHECK_EQ_INT(km.last.valid, 1);
}

static void test_no_sample_when_row_driven_high(void)
{
    km_core_t km;
    mock_t m;
    setup(&km, &m);
    km_core_tap(&km, "a");                 /* row 1 */
    km_core_cia1_pa_read(&km, 0xff);       /* every row driven high -> not sampled */
    CHECK_EQ_INT(km.cia1_reads_total, 1);
    CHECK_EQ_INT(km.cia1_reads_sampling, 0);
    CHECK_EQ_INT(km.last_reason, KM_RELEASE_NONE);
}

static void test_timeout_release(void)
{
    km_core_t km;
    mock_t m;
    setup(&km, &m);
    km_core_tap(&km, "a");
    m.clock += 60 * m.cpf;                 /* advance past the timeout */
    km_core_on_alarm(&km);                 /* never observed */
    CHECK_EQ_INT(km.active, 0);
    CHECK_EQ_INT(km.last.reason, KM_RELEASE_TIMEOUT);
}

static void test_fixed_mode_ignores_observation(void)
{
    km_core_t km;
    mock_t m;
    setup(&km, &m);
    km_core_tap(&km, "a for 5");
    CHECK_EQ_INT(km.mode, KM_MODE_TAP_FIXED);
    km_core_cia1_pa_read(&km, 0x00);       /* sampled, but fixed mode ignores */
    CHECK_EQ_INT(km.cia1_reads_sampling, 1);
    CHECK_EQ_INT(km.last_reason, KM_RELEASE_NONE);
    CHECK_EQ_INT(km.active, 1);
}

static void test_press_sticky(void)
{
    km_core_t km;
    mock_t m;
    setup(&km, &m);
    km_core_press(&km, "a");
    CHECK_EQ_INT(km.mode, KM_MODE_PRESS_STICKY);
    CHECK_EQ_INT(m.alarm_set, 0);          /* no release alarm armed */
    km_core_cia1_pa_read(&km, 0x00);
    CHECK_EQ_INT(km.active, 1);            /* sticky: not released on observe */
}

static void test_release_all(void)
{
    km_core_t km;
    mock_t m;
    setup(&km, &m);
    km_core_press(&km, "a");
    km_core_release(&km, "");
    CHECK_EQ_INT(km.active, 0);
    CHECK_EQ_INT(m.clear_count, 1);
}

static void test_release_subset(void)
{
    km_core_t km;
    mock_t m;
    setup(&km, &m);
    km_core_press(&km, "a z");              /* a=1,2 ; z=1,4 */
    km_core_release(&km, "a");
    CHECK_EQ_INT((m.keyarr[1] & (1 << 2)) != 0, 0);  /* a cleared */
    CHECK(m.keyarr[1] & (1 << 4));                    /* z survives */
    CHECK_EQ_INT(km.n_keys, 1);
    km_core_release(&km, "z");
    CHECK_EQ_INT(km.active, 0);                       /* last key -> released */
}

static void test_poke(void)
{
    km_core_t km;
    mock_t m;
    setup(&km, &m);
    km_core_poke(&km, "7 7 1");
    CHECK(m.keyarr[7] & (1 << 7));
    km_core_poke(&km, "7 7 0");
    CHECK_EQ_INT((m.keyarr[7] & (1 << 7)) != 0, 0);
    km_core_poke(&km, "9 9 1");             /* out of range */
    CHECK(strstr(m.out, "row must be") != NULL);
}

static void test_custom_key(void)
{
    km_core_t km;
    mock_t m;
    setup(&km, &m);
    km_core_tap(&km, "restore");
    CHECK_EQ_INT(m.custom[KM_CUSTOM_RESTORE1], 1);
}

static void test_binmon_set(void)
{
    km_core_t km;
    mock_t m;
    uint8_t body[] = { 1, 1, 2, 1 };        /* count=1, row=1, col=2, val=1 */
    setup(&km, &m);
    CHECK_EQ_INT(km_core_binmon_set(&km, body, sizeof(body)), 0);
    CHECK(m.keyarr[1] & (1 << 2));
    /* short body rejected */
    CHECK(km_core_binmon_set(&km, body, 2) < 0);
}

static void test_binmon_tap(void)
{
    km_core_t km;
    mock_t m;
    uint8_t body[] = { 0, 60, 0, 1, 1, 2 }; /* mode=observed, 60 frames, 1 key */
    setup(&km, &m);
    CHECK_EQ_INT(km_core_binmon_tap(&km, body, sizeof(body)), 0);
    CHECK_EQ_INT(km.active, 1);
    CHECK(m.keyarr[1] & (1 << 2));
    CHECK_EQ_INT(km.mode, KM_MODE_TAP_OBSERVED);
    /* count==0 rejected */
    {
        uint8_t bad[] = { 0, 60, 0, 0 };
        CHECK(km_core_binmon_tap(&km, bad, sizeof(bad)) < 0);
    }
}

static void test_binmon_get_layout(void)
{
    km_core_t km;
    mock_t m;
    uint8_t out[KM_BINMON_GET_RESPONSE_SIZE];
    uint32_t len = sizeof(out);
    setup(&km, &m);
    km_core_tap(&km, "a");                  /* row 1, 1 key, observed */
    CHECK_EQ_INT(km_core_binmon_get(&km, out, &len), 0);
    CHECK_EQ_INT(len, KM_BINMON_GET_RESPONSE_SIZE);
    CHECK_EQ_INT(out[1], 1 << 2);           /* keyarr row 1 has A's bit */
    CHECK_EQ_INT(out[21], 1);               /* n_keys */
    /* frames_until_timeout (u16 LE) should be near 60 just after the tap. */
    {
        uint16_t frames_left = (uint16_t)out[22] | ((uint16_t)out[23] << 8);
        CHECK(frames_left <= 60 && frames_left >= 58);
    }
    /* too-small buffer rejected */
    len = 10;
    CHECK(km_core_binmon_get(&km, out, &len) < 0);
}

static void test_supersede(void)
{
    km_core_t km;
    mock_t m;
    setup(&km, &m);
    km_core_tap(&km, "a");
    km_core_tap(&km, "b");                  /* supersedes: a released, b held */
    CHECK_EQ_INT((m.keyarr[1] & (1 << 2)) != 0, 0); /* a (1,2) released */
    CHECK(m.keyarr[3] & (1 << 4));                   /* b = 3,4 held */
    CHECK_EQ_INT(km.n_keys, 1);
    CHECK_EQ_INT(km.last.reason, KM_RELEASE_MANUAL);
}

int main(void)
{
    test_names();
    test_tap_single();
    test_tap_chord();
    test_unknown_key();
    test_rc_pair();
    test_observed_release();
    test_no_sample_when_row_driven_high();
    test_timeout_release();
    test_fixed_mode_ignores_observation();
    test_press_sticky();
    test_release_all();
    test_release_subset();
    test_poke();
    test_custom_key();
    test_binmon_set();
    test_binmon_tap();
    test_binmon_get_layout();
    test_supersede();
    REVICE_TEST_MAIN_END();
}
