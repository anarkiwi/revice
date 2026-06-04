/*
 * revice_keymatrix.h - C64 keyboard-matrix injection with observation-based
 *                      verification (pure core extracted from the asid-vice
 *                      fork's src/monitor/mon_keymatrix.c).
 *
 * The core owns the key-name table, argument parsing, the binary-monitor wire
 * codecs, and the press/tap/observe/release state machine. Everything that
 * touches the emulator - writing the keyboard matrix, reading the clock,
 * arming the release alarm, printing to the monitor - is injected through the
 * km_host_t vtable, so the logic builds and unit-tests with no VICE present.
 *
 * The thin VICE adapter (vice/mon_keymatrix.c) fills km_host_t from VICE and
 * re-exports the historical mon_keymatrix_*() symbols, so the in-tree wiring
 * (c64cia1.c read hooks, monitor_binary.c opcodes, mon_parse.y rules) does not
 * change.
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See LICENSE for copyright notice (GPL-2.0-or-later).
 */

#ifndef REVICE_KEYMATRIX_H
#define REVICE_KEYMATRIX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KM_MAX_KEYS                16
#define KM_DEFAULT_TIMEOUT_FRAMES  60
#define KM_BINMON_GET_RESPONSE_SIZE 24

/* Mirror of the VICE keyboard.h sentinels for the special non-matrix keys.
   The adapter compiles a _Static_assert that these equal the KBD_* values so
   the two never drift. */
#define KM_ROW_RESTORE_1   (-3)
#define KM_COL_RESTORE_1   0
#define KM_ROW_RESTORE_2   (-3)
#define KM_COL_RESTORE_2   1
#define KM_ROW_4080COLUMN  (-4)
#define KM_COL_4080COLUMN  0
#define KM_ROW_CAPSLOCK    (-4)
#define KM_COL_CAPSLOCK    1

/* Mirror of KBD_CUSTOM_* ids passed to host->custom_key_get(). */
#define KM_CUSTOM_RESTORE1 1
#define KM_CUSTOM_RESTORE2 2
#define KM_CUSTOM_CAPS     3
#define KM_CUSTOM_4080     4

/* Release-reason codes (also the wire values in the binmon GET response). */
enum km_release_reason {
    KM_RELEASE_NONE     = 0,
    KM_RELEASE_OBSERVED = 1,
    KM_RELEASE_TIMEOUT  = 2,
    KM_RELEASE_MANUAL   = 3
};

enum km_mode {
    KM_MODE_IDLE = 0,
    KM_MODE_TAP_OBSERVED,
    KM_MODE_TAP_FIXED,
    KM_MODE_PRESS_STICKY
};

/* Host operations the core needs from the emulator. */
typedef struct {
    /* Set keyboard matrix bit (row<0 routes to a custom key in the adapter). */
    void     (*set_keyarr_any)(void *ctx, int row, int col, int value);
    /* Read keyarr[row] (row 0..7). Used by `show` and the binmon GET. */
    int      (*get_keyarr)(void *ctx, int row);
    /* keyboard_custom_key_get(id) for the KM_CUSTOM_* ids. */
    int      (*custom_key_get)(void *ctx, int id);
    /* keyboard_clear_keymatrix(): drop every matrix bit. */
    void     (*clear_keymatrix)(void *ctx);
    /* maincpu_clk. */
    uint64_t (*now)(void *ctx);
    /* machine_get_cycles_per_frame(); <=0 if unknown. */
    long     (*cycles_per_frame)(void *ctx);
    /* Arm / cancel the single release alarm at an absolute clock. */
    void     (*set_alarm)(void *ctx, uint64_t clk);
    void     (*cancel_alarm)(void *ctx);
    /* Emit one chunk of monitor text (adapter does mon_out("%s", s)). */
    void     (*emit)(void *ctx, const char *str);
} km_host_t;

/* Internal key descriptor (exposed so callers can stack-allocate km_core_t). */
typedef struct {
    int  row;
    int  col;
    int  kind;        /* 0 = matrix bit, 1 = custom key */
    char name[16];
} km_key_t;

typedef struct {
    const km_host_t *host;
    void            *ctx;

    int      active;                 /* master gate read by the CIA1 hooks */
    int      mode;                   /* enum km_mode */
    km_key_t keys[KM_MAX_KEYS];
    int      n_keys;
    uint32_t cia1_reads_total;
    uint32_t cia1_reads_sampling;
    uint64_t timeout_clk;
    int      last_reason;            /* enum km_release_reason */
    uint64_t press_clk;

    struct {
        int      valid;
        km_key_t keys[KM_MAX_KEYS];
        int      n_keys;
        uint32_t cia1_reads_total;
        uint32_t cia1_reads_sampling;
        int      reason;
        int      mode;
        uint16_t frames_held;
    } last;
} km_core_t;

void km_core_init(km_core_t *km, const km_host_t *host, void *ctx);

/* Text-monitor verbs. `args` is the rest-of-line tail (may be NULL/empty). */
void km_core_tap(km_core_t *km, const char *args);
void km_core_press(km_core_t *km, const char *args);
void km_core_release(km_core_t *km, const char *args);
void km_core_poke(km_core_t *km, const char *args);
void km_core_show(km_core_t *km);
void km_core_names(km_core_t *km);

/* CIA1 read observation hooks. No-ops when no injection is active. */
void km_core_cia1_pa_read(km_core_t *km, uint8_t row_scan_mask);
void km_core_cia1_pb_read(km_core_t *km, uint8_t col_scan_mask);

/* Called by the adapter's alarm callback when the release alarm fires. */
void km_core_on_alarm(km_core_t *km);

/* Binary-monitor entry points. Return 0 on success, negative on a malformed
   body (caller maps to a protocol error). See revice_keymatrix.c / the fork
   README for the exact body + response layouts. */
int km_core_binmon_set(km_core_t *km, const uint8_t *body, uint32_t length);
int km_core_binmon_tap(km_core_t *km, const uint8_t *body, uint32_t length);
int km_core_binmon_get(km_core_t *km, uint8_t *out, uint32_t *out_length);

#ifdef __cplusplus
}
#endif

#endif /* REVICE_KEYMATRIX_H */
