/*
 * soundbustrace.h - VICE adapter interface for the revice bus-trace feature.
 *
 * Declares the entry points the VICE tree wires up: the per-access hook called
 * from the maincpu bus observation point, the resource/cmdline registration,
 * and the shutdown finalizer. The trace logic itself is in the revice core
 * (revice_bustrace.h); this header carries only the VICE-facing glue.
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice (GPL-2.0-or-later).
 */

#ifndef VICE_SOUNDBUSTRACE_H
#define VICE_SOUNDBUSTRACE_H

#include "types.h"

#include "revice_bustrace.h"   /* REVICE_BUSTRACE_RW_* flag bits */

/* Feed one 6510 memory access into the trace. addr/val are the bus address and
   byte; rw_flags is a combination of REVICE_BUSTRACE_RW_WRITE / _OPCODE /
   _DUMMY. The accessing PC and the cycle are read from VICE globals inside the
   adapter. No-op (single pointer test) when no -bustrace file is set. */
extern void bustrace_observe_access(uint16_t addr, uint8_t val,
                                    uint8_t rw_flags);

/* True when a -bustrace file is configured. */
extern int bustrace_is_enabled(void);

/* Register the BusTraceFile resource and the -bustrace cmdline option. */
extern int bustrace_resources_init(void);
extern int bustrace_cmdline_options_init(void);

/* Finalize the trace (write trailer) and release resources. */
extern void bustrace_shutdown(void);

#endif /* VICE_SOUNDBUSTRACE_H */
