/*
 * revice_checkpoint.h - decoder for the asid-vice fork's silent-checkpoint
 *                       extension to the binmon CHECKPOINT_SET (0x12) body.
 *
 * silent-checkpoint is fundamentally a behavioral patch to VICE's breakpoint
 * handler (mon_breakpoint_check_checkpoint skips the per-hit event/trace/
 * disassembly when cp->silent is set) plus a `silent` field on the checkpoint
 * struct - see integration/vice/patches/. The only piece with a stable,
 * testable wire contract is the backward-compatible decode of the optional
 * `silent` byte appended at body offset 9, which is what this core provides.
 *
 * Standard CHECKPOINT_SET bodies are 8 bytes (+1 optional memspace byte at
 * offset 8). asid-vice appends `silent` at offset 9: a length-10+ body carries
 * it, anything shorter leaves silent = false.
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See LICENSE for copyright notice (GPL-2.0-or-later).
 */

#ifndef REVICE_CHECKPOINT_H
#define REVICE_CHECKPOINT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CHECKPOINT_SILENT_BODY_OFFSET 9

/* Returns 1 if the CHECKPOINT_SET body requests a silent checkpoint, else 0.
   Backward-compatible: bodies shorter than 10 bytes always return 0. */
int checkpoint_decode_silent(const uint8_t *body, uint32_t length);

#ifdef __cplusplus
}
#endif

#endif /* REVICE_CHECKPOINT_H */
