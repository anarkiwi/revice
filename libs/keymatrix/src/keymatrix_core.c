/*
 * keymatrix_core.c - C64 keyboard-matrix injection core. See
 *                    revice_keymatrix.h.
 *
 * Logic-preserving port of the asid-vice fork's src/monitor/mon_keymatrix.c.
 * VICE-specific calls (keyboard_set_keyarr_any, alarms, maincpu_clk,
 * machine_get_cycles_per_frame, mon_out) became km_host_t callbacks; vice's
 * lib_strdup/strtok_r were replaced with in-place parsing on a stack buffer so
 * the core has no libc-extension or VICE dependencies.
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See LICENSE for copyright notice (GPL-2.0-or-later).
 */

#include "revice_keymatrix.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct kmx_keydef {
    const char *name;
    int row;
    int col;
    int kind;
};

/* C64 keyboard matrix. Names case-insensitive; aliases are separate entries
   pointing at the same (row,col). RESTORE/CAPS/4080 use negative row
   sentinels routed through host->set_keyarr_any(). */
static const struct kmx_keydef c64_keys[] = {
    /* Row 0: INST/DEL, RETURN, CRSR LR, F7, F1, F3, F5, CRSR UD */
    { "INSTDEL",   0, 0, 0 }, { "DEL",       0, 0, 0 },
    { "INST",      0, 0, 0 }, { "BACKSPACE", 0, 0, 0 }, { "BS", 0, 0, 0 },
    { "RETURN",    0, 1, 0 }, { "ENTER",     0, 1, 0 }, { "RTN", 0, 1, 0 },
    { "CRSRLR",    0, 2, 0 }, { "CR",        0, 2, 0 }, { "RIGHT", 0, 2, 0 },
    { "F7",        0, 3, 0 },
    { "F1",        0, 4, 0 },
    { "F3",        0, 5, 0 },
    { "F5",        0, 6, 0 },
    { "CRSRUD",    0, 7, 0 }, { "CD",        0, 7, 0 }, { "DOWN", 0, 7, 0 },

    /* Row 1: 3, W, A, 4, Z, S, E, LSHIFT */
    { "3",      1, 0, 0 }, { "W", 1, 1, 0 }, { "A", 1, 2, 0 }, { "4", 1, 3, 0 },
    { "Z",      1, 4, 0 }, { "S", 1, 5, 0 }, { "E", 1, 6, 0 },
    { "LSHIFT", 1, 7, 0 }, { "SHIFT",     1, 7, 0 }, { "LEFTSHIFT", 1, 7, 0 },

    /* Row 2: 5, R, D, 6, C, F, T, X */
    { "5", 2, 0, 0 }, { "R", 2, 1, 0 }, { "D", 2, 2, 0 }, { "6", 2, 3, 0 },
    { "C", 2, 4, 0 }, { "F", 2, 5, 0 }, { "T", 2, 6, 0 }, { "X", 2, 7, 0 },

    /* Row 3: 7, Y, G, 8, B, H, U, V */
    { "7", 3, 0, 0 }, { "Y", 3, 1, 0 }, { "G", 3, 2, 0 }, { "8", 3, 3, 0 },
    { "B", 3, 4, 0 }, { "H", 3, 5, 0 }, { "U", 3, 6, 0 }, { "V", 3, 7, 0 },

    /* Row 4: 9, I, J, 0, M, K, O, N */
    { "9", 4, 0, 0 }, { "I", 4, 1, 0 }, { "J", 4, 2, 0 }, { "0", 4, 3, 0 },
    { "M", 4, 4, 0 }, { "K", 4, 5, 0 }, { "O", 4, 6, 0 }, { "N", 4, 7, 0 },

    /* Row 5: +, P, L, -, ., :, @, , */
    { "PLUS",   5, 0, 0 },
    { "P",      5, 1, 0 },
    { "L",      5, 2, 0 },
    { "MINUS",  5, 3, 0 },
    { "PERIOD", 5, 4, 0 }, { "DOT", 5, 4, 0 },
    { "COLON",  5, 5, 0 },
    { "AT",     5, 6, 0 },
    { "COMMA",  5, 7, 0 },

    /* Row 6: pound, *, ;, CLR/HOME, RSHIFT, =, up-arrow, / */
    { "POUND",     6, 0, 0 }, { "STERLING",   6, 0, 0 },
    { "TIMES",     6, 1, 0 }, { "STAR",       6, 1, 0 }, { "ASTERISK", 6, 1, 0 },
    { "SEMICOLON", 6, 2, 0 }, { "SEMI",       6, 2, 0 },
    { "CLR",       6, 3, 0 }, { "HOME",       6, 3, 0 }, { "CLRHOME",  6, 3, 0 },
    { "RSHIFT",    6, 4, 0 }, { "RIGHTSHIFT", 6, 4, 0 },
    { "EQUALS",    6, 5, 0 }, { "EQ",         6, 5, 0 },
    { "UPARROW",   6, 6, 0 }, { "UP",         6, 6, 0 }, { "EXPONENT", 6, 6, 0 },
    { "SLASH",     6, 7, 0 },

    /* Row 7: 1, left-arrow, CTRL, 2, SPACE, CBM, Q, RUN/STOP */
    { "1",         7, 0, 0 },
    { "LEFTARROW", 7, 1, 0 }, { "LEFT",      7, 1, 0 },
    { "CTRL",      7, 2, 0 },
    { "2",         7, 3, 0 },
    { "SPACE",     7, 4, 0 }, { "SP",        7, 4, 0 },
    { "CBM",       7, 5, 0 }, { "COMMODORE", 7, 5, 0 },
    { "Q",         7, 6, 0 },
    { "RUNSTOP",   7, 7, 0 }, { "STOP",      7, 7, 0 }, { "RUN", 7, 7, 0 },

    /* Special keys not in keyarr; routed via set_keyarr_any. */
    { "RESTORE",  KM_ROW_RESTORE_1,  KM_COL_RESTORE_1,  1 },
    { "CAPSLOCK", KM_ROW_CAPSLOCK,   KM_COL_CAPSLOCK,   1 },
    { "CAPS",     KM_ROW_CAPSLOCK,   KM_COL_CAPSLOCK,   1 },
    { "4080",     KM_ROW_4080COLUMN, KM_COL_4080COLUMN, 1 },

    { NULL, 0, 0, 0 }
};

/* ---- small dependency-free helpers (replace mon_out / lib / strtok_r) ---- */

#if defined(__GNUC__)
static void km_outf(km_core_t *km, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
#endif

static void km_outf(km_core_t *km, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    km->host->emit(km->ctx, buf);
}

static int km_strcasecmp(const char *a, const char *b)
{
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) {
            return ca - cb;
        }
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

/* strtok_r-equivalent over space/tab, no libc-extension dependency. */
static char *km_tok(char *str, char **saveptr)
{
    char *s = str ? str : *saveptr;
    char *tok;
    if (s == NULL) {
        return NULL;
    }
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    if (*s == '\0') {
        *saveptr = s;
        return NULL;
    }
    tok = s;
    while (*s && *s != ' ' && *s != '\t') {
        s++;
    }
    if (*s) {
        *s = '\0';
        s++;
    }
    *saveptr = s;
    return tok;
}

/* ---- parsing ---- */

static int find_key_by_name(const char *name, km_key_t *out)
{
    int i;
    for (i = 0; c64_keys[i].name != NULL; i++) {
        if (km_strcasecmp(name, c64_keys[i].name) == 0) {
            out->row  = c64_keys[i].row;
            out->col  = c64_keys[i].col;
            out->kind = c64_keys[i].kind;
            strncpy(out->name, c64_keys[i].name, sizeof(out->name) - 1);
            out->name[sizeof(out->name) - 1] = '\0';
            return 0;
        }
    }
    return -1;
}

static int parse_rc_pair(const char *tok, km_key_t *out)
{
    char *end;
    long r, c;
    r = strtol(tok, &end, 10);
    if (end == tok || *end != ',') {
        return -1;
    }
    c = strtol(end + 1, &end, 10);
    if (*end != '\0') {
        return -1;
    }
    if (r < -8 || r > 15 || c < 0 || c > 7) {
        return -1;
    }
    out->row  = (int)r;
    out->col  = (int)c;
    out->kind = (r < 0) ? 1 : 0;
    snprintf(out->name, sizeof(out->name), "%ld,%ld", r, c);
    return 0;
}

static int parse_key_token(const char *tok, km_key_t *out)
{
    if (strchr(tok, ',') != NULL) {
        return parse_rc_pair(tok, out);
    }
    return find_key_by_name(tok, out);
}

/* Parse a whitespace-tokenised arg string into a list of keys plus an optional
   "for <frames>" qualifier. Buffer is modified in place. */
static int parse_key_list(km_core_t *km, char *buf,
                          km_key_t *out_keys, int max_keys,
                          int *out_n_keys, int *out_for_frames)
{
    char *saveptr = NULL;
    char *tok;
    int n = 0;
    int for_frames = -1;

    for (tok = km_tok(buf, &saveptr);
         tok != NULL;
         tok = km_tok(NULL, &saveptr)) {
        if (km_strcasecmp(tok, "for") == 0) {
            char *frames_tok = km_tok(NULL, &saveptr);
            char *end;
            long v;
            if (frames_tok == NULL) {
                km_outf(km, "error: 'for' must be followed by a frame count\n");
                return -1;
            }
            v = strtol(frames_tok, &end, 0);
            if (end == frames_tok || *end != '\0' || v < 1 || v > 65535) {
                km_outf(km, "error: invalid frame count '%s'\n", frames_tok);
                return -1;
            }
            for_frames = (int)v;
            continue;
        }
        if (n >= max_keys) {
            km_outf(km, "error: too many keys (max %d)\n", max_keys);
            return -1;
        }
        if (parse_key_token(tok, &out_keys[n]) != 0) {
            km_outf(km, "error: unknown key '%s' (try 'keymatrix names')\n", tok);
            return -1;
        }
        n++;
    }
    *out_n_keys = n;
    *out_for_frames = for_frames;
    return 0;
}

/* Copy a const arg string into a writable buffer for in-place tokenising.
   Returns 0 on success, -1 if it would overflow. */
static int km_copy_args(const char *args, char *buf, size_t bufsize)
{
    size_t len = strlen(args);
    if (len >= bufsize) {
        return -1;
    }
    memcpy(buf, args, len + 1);
    return 0;
}

/* ---- state machine ---- */

static void km_apply_bits(km_core_t *km, km_key_t *keys, int n_keys, int value)
{
    int i;
    for (i = 0; i < n_keys; i++) {
        km->host->set_keyarr_any(km->ctx, keys[i].row, keys[i].col, value);
    }
}

static const char *km_reason_str(int r)
{
    switch (r) {
        case KM_RELEASE_OBSERVED: return "observed";
        case KM_RELEASE_TIMEOUT:  return "timeout";
        case KM_RELEASE_MANUAL:   return "manual";
        case KM_RELEASE_NONE:     return "none";
    }
    return "?";
}

static const char *km_mode_str(int m)
{
    switch (m) {
        case KM_MODE_TAP_OBSERVED: return "tap-observed";
        case KM_MODE_TAP_FIXED:    return "tap-fixed";
        case KM_MODE_PRESS_STICKY: return "press";
        case KM_MODE_IDLE:         return "idle";
    }
    return "?";
}

static void km_release_now(km_core_t *km, int reason)
{
    int i;
    long cyc_per_frame;
    uint64_t now;

    if (!km->active) {
        return;
    }

    km_apply_bits(km, km->keys, km->n_keys, 0);

    km->last.valid = 1;
    km->last.n_keys = km->n_keys;
    for (i = 0; i < km->n_keys; i++) {
        km->last.keys[i] = km->keys[i];
    }
    km->last.cia1_reads_total = km->cia1_reads_total;
    km->last.cia1_reads_sampling = km->cia1_reads_sampling;
    km->last.reason = (km->last_reason != KM_RELEASE_NONE)
                      ? km->last_reason : reason;
    km->last.mode = km->mode;

    cyc_per_frame = km->host->cycles_per_frame(km->ctx);
    now = km->host->now(km->ctx);
    if (cyc_per_frame > 0 && now >= km->press_clk) {
        uint64_t held = now - km->press_clk;
        uint64_t f = held / (uint64_t)cyc_per_frame;
        km->last.frames_held = (f > 65535) ? 65535 : (uint16_t)f;
    } else {
        km->last.frames_held = 0;
    }

    km->active = 0;
    km->n_keys = 0;
    km->mode = KM_MODE_IDLE;
    km->cia1_reads_total = 0;
    km->cia1_reads_sampling = 0;
    km->last_reason = KM_RELEASE_NONE;

    km->host->cancel_alarm(km->ctx);
}

void km_core_on_alarm(km_core_t *km)
{
    int r = (km->last_reason != KM_RELEASE_NONE)
            ? km->last_reason : KM_RELEASE_TIMEOUT;
    km_release_now(km, r);
}

static void km_supersede_active(km_core_t *km)
{
    if (km->active) {
        km_release_now(km, KM_RELEASE_MANUAL);
    }
}

static long km_cpf_or_pal(km_core_t *km)
{
    long cyc_per_frame = km->host->cycles_per_frame(km->ctx);
    if (cyc_per_frame <= 0) {
        cyc_per_frame = 19656;      /* PAL fallback */
    }
    return cyc_per_frame;
}

void km_core_tap(km_core_t *km, const char *args)
{
    char buf[256];
    km_key_t keys[KM_MAX_KEYS];
    int n_keys = 0;
    int for_frames = -1;
    long cyc_per_frame;
    int frames;
    int i;

    if (args == NULL || *args == '\0') {
        km_outf(km, "usage: keymatrix tap <key> [<key> ...] [for <frames>]\n");
        return;
    }
    if (km_copy_args(args, buf, sizeof(buf)) != 0) {
        km_outf(km, "error: argument too long\n");
        return;
    }
    if (parse_key_list(km, buf, keys, KM_MAX_KEYS, &n_keys, &for_frames) != 0) {
        return;
    }
    if (n_keys == 0) {
        km_outf(km, "error: no keys specified\n");
        return;
    }

    km_supersede_active(km);

    for (i = 0; i < n_keys; i++) {
        km->keys[i] = keys[i];
    }
    km->n_keys = n_keys;
    km_apply_bits(km, km->keys, km->n_keys, 1);

    if (for_frames > 0) {
        km->mode = KM_MODE_TAP_FIXED;
        frames = for_frames;
    } else {
        km->mode = KM_MODE_TAP_OBSERVED;
        frames = KM_DEFAULT_TIMEOUT_FRAMES;
    }
    km->cia1_reads_total = 0;
    km->cia1_reads_sampling = 0;
    km->last_reason = KM_RELEASE_NONE;
    km->active = 1;
    km->press_clk = km->host->now(km->ctx);

    cyc_per_frame = km_cpf_or_pal(km);
    km->timeout_clk = km->press_clk +
        (uint64_t)frames * (uint64_t)cyc_per_frame;
    km->host->set_alarm(km->ctx, km->timeout_clk);

    km_outf(km, "keymatrix: tap %d key%s, mode=%s, max %d frame%s\n",
            n_keys, n_keys == 1 ? "" : "s",
            km_mode_str(km->mode),
            frames, frames == 1 ? "" : "s");
}

void km_core_press(km_core_t *km, const char *args)
{
    char buf[256];
    km_key_t keys[KM_MAX_KEYS];
    int n_keys = 0;
    int for_frames = -1;
    int i;

    if (args == NULL || *args == '\0') {
        km_outf(km, "usage: keymatrix press <key> [<key> ...]\n");
        return;
    }
    if (km_copy_args(args, buf, sizeof(buf)) != 0) {
        km_outf(km, "error: argument too long\n");
        return;
    }
    if (parse_key_list(km, buf, keys, KM_MAX_KEYS, &n_keys, &for_frames) != 0) {
        return;
    }
    if (for_frames > 0) {
        km_outf(km, "note: 'for <frames>' has no effect on press; use 'tap' for that\n");
    }
    if (n_keys == 0) {
        km_outf(km, "error: no keys specified\n");
        return;
    }

    km_supersede_active(km);

    for (i = 0; i < n_keys; i++) {
        km->keys[i] = keys[i];
    }
    km->n_keys = n_keys;
    km_apply_bits(km, km->keys, km->n_keys, 1);

    km->mode = KM_MODE_PRESS_STICKY;
    km->cia1_reads_total = 0;
    km->cia1_reads_sampling = 0;
    km->last_reason = KM_RELEASE_NONE;
    km->active = 1;
    km->press_clk = km->host->now(km->ctx);
    /* No release alarm: clear via 'keymatrix release'. */

    km_outf(km, "keymatrix: pressed %d key%s (sticky; clear with 'keymatrix release')\n",
            n_keys, n_keys == 1 ? "" : "s");
}

void km_core_release(km_core_t *km, const char *args)
{
    char buf[256];
    km_key_t keys[KM_MAX_KEYS];
    int n_keys = 0;
    int for_frames = -1;
    int i;

    if (args == NULL || *args == '\0') {
        if (km->active) {
            km_release_now(km, KM_RELEASE_MANUAL);
        }
        km->host->clear_keymatrix(km->ctx);
        km_outf(km, "keymatrix: released all keys\n");
        return;
    }

    if (km_copy_args(args, buf, sizeof(buf)) != 0) {
        km_outf(km, "error: argument too long\n");
        return;
    }
    if (parse_key_list(km, buf, keys, KM_MAX_KEYS, &n_keys, &for_frames) != 0) {
        return;
    }
    if (n_keys == 0) {
        km_outf(km, "error: no keys specified\n");
        return;
    }

    for (i = 0; i < n_keys; i++) {
        km->host->set_keyarr_any(km->ctx, keys[i].row, keys[i].col, 0);
    }

    /* Drop matching keys from the active tracking set so observation counts
       stay correct. */
    if (km->active) {
        km_key_t surviving[KM_MAX_KEYS];
        int n_surviving = 0;
        int j, k;
        for (j = 0; j < km->n_keys; j++) {
            int dropped = 0;
            for (k = 0; k < n_keys; k++) {
                if (km->keys[j].row == keys[k].row &&
                    km->keys[j].col == keys[k].col) {
                    dropped = 1;
                    break;
                }
            }
            if (!dropped) {
                surviving[n_surviving++] = km->keys[j];
            }
        }
        km->n_keys = n_surviving;
        for (j = 0; j < n_surviving; j++) {
            km->keys[j] = surviving[j];
        }
        if (km->n_keys == 0) {
            km_release_now(km, KM_RELEASE_MANUAL);
        }
    }
    km_outf(km, "keymatrix: released %d key%s\n",
            n_keys, n_keys == 1 ? "" : "s");
}

void km_core_poke(km_core_t *km, const char *args)
{
    char buf[256];
    char *saveptr = NULL;
    char *tok;
    char *end;
    long row, col, value;

    if (args == NULL || *args == '\0') {
        km_outf(km, "usage: keymatrix poke <row> <col> <0|1>\n");
        return;
    }
    if (km_copy_args(args, buf, sizeof(buf)) != 0) {
        km_outf(km, "error: argument too long\n");
        return;
    }

    tok = km_tok(buf, &saveptr);
    if (tok == NULL) { goto bad; }
    row = strtol(tok, &end, 0);
    if (*end != '\0') { goto bad; }

    tok = km_tok(NULL, &saveptr);
    if (tok == NULL) { goto bad; }
    col = strtol(tok, &end, 0);
    if (*end != '\0') { goto bad; }

    tok = km_tok(NULL, &saveptr);
    if (tok == NULL) { goto bad; }
    value = strtol(tok, &end, 0);
    if (*end != '\0' || (value != 0 && value != 1)) { goto bad; }

    if (col < 0 || col > 7 || row < -8 || row > 15) {
        km_outf(km, "error: row must be -8..15, col must be 0..7\n");
        return;
    }

    km->host->set_keyarr_any(km->ctx, (int)row, (int)col, (int)value);
    km_outf(km, "keymatrix: poked (%ld,%ld) <- %ld\n", row, col, value);
    return;

bad:
    km_outf(km, "usage: keymatrix poke <row> <col> <0|1>\n");
}

void km_core_show(km_core_t *km)
{
    int r, c;

    km_outf(km, "keymatrix: live keyarr (rows down, cols right):\n");
    km_outf(km, "       ");
    for (c = 0; c < 8; c++) {
        km_outf(km, "%d ", c);
    }
    km_outf(km, "\n");
    for (r = 0; r < 8; r++) {
        int row_bits = km->host->get_keyarr(km->ctx, r);
        km_outf(km, "  r%d:  ", r);
        for (c = 0; c < 8; c++) {
            km_outf(km, "%c ", (row_bits & (1 << c)) ? '*' : '.');
        }
        km_outf(km, "\n");
    }
    km_outf(km, "  RESTORE: %s   CAPSLOCK: %s   4080: %s\n",
            km->host->custom_key_get(km->ctx, KM_CUSTOM_RESTORE1) ? "on" : "off",
            km->host->custom_key_get(km->ctx, KM_CUSTOM_CAPS) ? "on" : "off",
            km->host->custom_key_get(km->ctx, KM_CUSTOM_4080) ? "on" : "off");

    if (km->active) {
        km_outf(km, "\nactive injection: %s, %d key%s\n",
                km_mode_str(km->mode),
                km->n_keys, km->n_keys == 1 ? "" : "s");
        for (r = 0; r < km->n_keys; r++) {
            km_outf(km, "  %s (%d,%d)\n", km->keys[r].name,
                    km->keys[r].row, km->keys[r].col);
        }
        km_outf(km, "  cia1 reads: %u total, %u sampled injected bits\n",
                (unsigned)km->cia1_reads_total,
                (unsigned)km->cia1_reads_sampling);
    } else {
        km_outf(km, "\nno active injection\n");
    }

    if (km->last.valid) {
        km_outf(km, "\nlast tap: %d key%s (%s)\n",
                km->last.n_keys,
                km->last.n_keys == 1 ? "" : "s",
                km_mode_str(km->last.mode));
        for (r = 0; r < km->last.n_keys; r++) {
            km_outf(km, "  %s (%d,%d)\n", km->last.keys[r].name,
                    km->last.keys[r].row, km->last.keys[r].col);
        }
        km_outf(km, "  released after %u frame%s; reason: %s\n",
                (unsigned)km->last.frames_held,
                km->last.frames_held == 1 ? "" : "s",
                km_reason_str(km->last.reason));
        km_outf(km, "  cia1 reads: %u total, %u sampled injected bits\n",
                (unsigned)km->last.cia1_reads_total,
                (unsigned)km->last.cia1_reads_sampling);
    }
}

void km_core_names(km_core_t *km)
{
    int i;
    int col = 0;
    km_outf(km, "keymatrix: known C64 key names (case-insensitive):\n  ");
    for (i = 0; c64_keys[i].name != NULL; i++) {
        km_outf(km, "%-12s", c64_keys[i].name);
        col++;
        if (col == 5) {
            km_outf(km, "\n  ");
            col = 0;
        }
    }
    if (col != 0) {
        km_outf(km, "\n");
    }
    km_outf(km, "\n  Use 'keymatrix poke <row> <col> <0|1>' for raw matrix bits.\n");
    km_outf(km, "  Combine names for chords, e.g. 'keymatrix tap lshift a'.\n");
}

/* ---- CIA1 observation hooks ---- */

static void km_observe(km_core_t *km, int sampled)
{
    if (sampled) {
        km->cia1_reads_sampling++;
        if (km->mode == KM_MODE_TAP_OBSERVED &&
            km->last_reason == KM_RELEASE_NONE) {
            km->last_reason = KM_RELEASE_OBSERVED;
            km->host->set_alarm(km->ctx, km->host->now(km->ctx) + 1);
        }
    }
}

void km_core_cia1_pa_read(km_core_t *km, uint8_t row_scan_mask)
{
    int i;
    int sampled = 0;
    if (!km->active) {
        return;
    }
    km->cia1_reads_total++;
    for (i = 0; i < km->n_keys; i++) {
        if (km->keys[i].kind != 0) {
            continue;
        }
        if (km->keys[i].row < 0 || km->keys[i].row > 7) {
            continue;
        }
        if (!(row_scan_mask & (1 << km->keys[i].row))) {
            sampled = 1;
            break;
        }
    }
    km_observe(km, sampled);
}

void km_core_cia1_pb_read(km_core_t *km, uint8_t col_scan_mask)
{
    int i;
    int sampled = 0;
    if (!km->active) {
        return;
    }
    km->cia1_reads_total++;
    for (i = 0; i < km->n_keys; i++) {
        if (km->keys[i].kind != 0) {
            continue;
        }
        if (km->keys[i].col < 0 || km->keys[i].col > 7) {
            continue;
        }
        if (!(col_scan_mask & (1 << km->keys[i].col))) {
            sampled = 1;
            break;
        }
    }
    km_observe(km, sampled);
}

/* ---- binary monitor ---- */

static void put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

int km_core_binmon_set(km_core_t *km, const uint8_t *body, uint32_t length)
{
    uint8_t count;
    uint32_t needed;
    uint32_t i;
    if (length < 1) {
        return -1;
    }
    count = body[0];
    needed = 1U + (uint32_t)count * 3U;
    if (length < needed) {
        return -1;
    }
    for (i = 0; i < count; i++) {
        int8_t  row   = (int8_t)body[1 + i * 3 + 0];
        int8_t  col   = (int8_t)body[1 + i * 3 + 1];
        uint8_t value = body[1 + i * 3 + 2];
        km->host->set_keyarr_any(km->ctx, (int)row, (int)col, value ? 1 : 0);
    }
    return 0;
}

int km_core_binmon_tap(km_core_t *km, const uint8_t *body, uint32_t length)
{
    uint8_t mode;
    uint16_t frames;
    uint8_t count;
    uint32_t needed;
    uint32_t i;
    long cyc_per_frame;
    int frames_int;

    if (length < 4) {
        return -1;
    }
    mode = body[0];
    frames = (uint16_t)body[1] | ((uint16_t)body[2] << 8);
    count = body[3];
    needed = 4U + (uint32_t)count * 2U;
    if (length < needed) {
        return -1;
    }
    if (count == 0 || count > KM_MAX_KEYS) {
        return -1;
    }

    km_supersede_active(km);

    for (i = 0; i < count; i++) {
        int8_t row = (int8_t)body[4 + i * 2 + 0];
        int8_t col = (int8_t)body[4 + i * 2 + 1];
        km->keys[i].row  = (int)row;
        km->keys[i].col  = (int)col;
        km->keys[i].kind = (row < 0) ? 1 : 0;
        snprintf(km->keys[i].name, sizeof(km->keys[i].name),
                 "%d,%d", (int)row, (int)col);
    }
    km->n_keys = (int)count;
    km_apply_bits(km, km->keys, km->n_keys, 1);
    km->mode = (mode == 1) ? KM_MODE_TAP_FIXED : KM_MODE_TAP_OBSERVED;
    km->cia1_reads_total = 0;
    km->cia1_reads_sampling = 0;
    km->last_reason = KM_RELEASE_NONE;
    km->active = 1;
    km->press_clk = km->host->now(km->ctx);

    cyc_per_frame = km_cpf_or_pal(km);
    frames_int = (frames > 0) ? frames : KM_DEFAULT_TIMEOUT_FRAMES;
    km->timeout_clk = km->press_clk +
        (uint64_t)frames_int * (uint64_t)cyc_per_frame;
    km->host->set_alarm(km->ctx, km->timeout_clk);
    return 0;
}

int km_core_binmon_get(km_core_t *km, uint8_t *out, uint32_t *out_length)
{
    int i;
    uint8_t custom = 0;
    uint16_t frames_left = 0;

    if (out == NULL || out_length == NULL) {
        return -1;
    }
    if (*out_length < KM_BINMON_GET_RESPONSE_SIZE) {
        return -1;
    }

    for (i = 0; i < 8; i++) {
        out[i] = (uint8_t)(km->host->get_keyarr(km->ctx, i) & 0xff);
    }
    if (km->host->custom_key_get(km->ctx, KM_CUSTOM_RESTORE1)) custom |= 1U << 0;
    if (km->host->custom_key_get(km->ctx, KM_CUSTOM_RESTORE2)) custom |= 1U << 1;
    if (km->host->custom_key_get(km->ctx, KM_CUSTOM_CAPS))     custom |= 1U << 2;
    if (km->host->custom_key_get(km->ctx, KM_CUSTOM_4080))     custom |= 1U << 3;
    out[8]  = custom;
    out[9]  = 0;
    out[10] = 0;
    out[11] = 0;

    if (km->active) {
        put_le32(&out[12], km->cia1_reads_total);
        put_le32(&out[16], km->cia1_reads_sampling);
        out[20] = (uint8_t)km->last_reason;
        out[21] = (uint8_t)km->n_keys;
    } else if (km->last.valid) {
        put_le32(&out[12], km->last.cia1_reads_total);
        put_le32(&out[16], km->last.cia1_reads_sampling);
        out[20] = (uint8_t)km->last.reason;
        out[21] = (uint8_t)km->last.n_keys;
    } else {
        put_le32(&out[12], 0);
        put_le32(&out[16], 0);
        out[20] = 0;
        out[21] = 0;
    }

    if (km->active && km->timeout_clk > km->host->now(km->ctx)) {
        long cpf = km->host->cycles_per_frame(km->ctx);
        if (cpf > 0) {
            uint64_t left = km->timeout_clk - km->host->now(km->ctx);
            uint64_t f = left / (uint64_t)cpf;
            frames_left = (f > 65535) ? 65535 : (uint16_t)f;
        }
    }
    put_le16(&out[22], frames_left);

    *out_length = KM_BINMON_GET_RESPONSE_SIZE;
    return 0;
}

void km_core_init(km_core_t *km, const km_host_t *host, void *ctx)
{
    memset(km, 0, sizeof(*km));
    km->host = host;
    km->ctx = ctx;
    km->mode = KM_MODE_IDLE;
    km->last_reason = KM_RELEASE_NONE;
}
