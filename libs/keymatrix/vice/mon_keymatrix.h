/*
 * mon_keymatrix.h - Monitor keyboard-matrix injection (VICE-facing API).
 *
 * This is the stable public contract the in-tree wiring depends on
 * (src/c64/c64cia1.c read hooks, src/monitor/monitor_binary.c opcodes,
 * src/monitor/mon_parse.y rules). The implementation in mon_keymatrix.c is a
 * thin adapter over the revice keymatrix core (libs/keymatrix); the symbol
 * names and the binmon GET response size below are unchanged from the original
 * asid-vice fork so the wiring does not need to change.
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice.
 */

#ifndef VICE_MON_KEYMATRIX_H
#define VICE_MON_KEYMATRIX_H

#include "types.h"

/* Parser-action entry points (rest-of-line args; NULL/empty == no args). */
void mon_keymatrix_tap(const char *args);
void mon_keymatrix_press(const char *args);
void mon_keymatrix_release(const char *args);
void mon_keymatrix_poke(const char *args);
void mon_keymatrix_show(void);
void mon_keymatrix_names(void);

/* CIA1 observation hooks. Called from src/c64/c64cia1.c read_ciapa /
   read_ciapb; both short-circuit when no injection is active. */
void mon_keymatrix_cia1_pa_read(uint8_t row_scan_mask);
void mon_keymatrix_cia1_pb_read(uint8_t col_scan_mask);

/* Binary-monitor entry points (see the fork README / revice_keymatrix.h for
   the body and 24-byte GET response layouts). Return 0 on success, negative on
   a validation failure. */
int mon_keymatrix_binmon_set(const uint8_t *body, uint32_t length);
int mon_keymatrix_binmon_tap(const uint8_t *body, uint32_t length);
int mon_keymatrix_binmon_get(uint8_t *out, uint32_t *out_length);

#define MON_KEYMATRIX_BINMON_GET_RESPONSE_SIZE 24

#endif
