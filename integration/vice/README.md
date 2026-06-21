# Wiring revice into a VICE tree

This directory describes how a VICE source tree (e.g. the `asid-vice` fork)
consumes the revice libraries, and provides a script to do the mechanical
parts. The goal is that as upstream VICE advances, re-wiring the custom
features means re-applying a *small, well-understood* set of edits while all the
feature logic stays in revice (and stays tested).

## Model

revice is added to the VICE tree as a **git submodule at `src/revice`**. Its
pure cores and the thin VICE adapters are compiled directly from the submodule
by the existing per-directory automake libraries — no source copies. Because
VICE uses recursive automake without `subdir-objects`, a source listed as
`$(top_srcdir)/src/revice/.../foo.c` compiles to `foo.o` in the consuming
directory; `AM_INIT_AUTOMAKE([-Wno-portability ...])` (already set in VICE)
permits the out-of-directory path.

Each core is compiled into the **same** static library as the adapter that
uses it, so static-link order is never an issue. `screen_core.c` is the one
core used from two libraries (the machine-agnostic `mon_screen.c` in
`libmonitor` and the C64 `c64screen.c` in `libc64sc`); it is compiled into
`libmonitor`, and the VICE link line lists `c64sc_lib` *before* `monitor_lib`,
so `c64screen.o`'s reference to `screen_pack` resolves against the later
`libmonitor.a`.

| revice file | compiled into VICE lib | dir |
|---|---|---|
| `libs/keymatrix/src/keymatrix_core.c` + `vice/mon_keymatrix.c` | `libmonitor` | `src/monitor` |
| `libs/screen/src/screen_core.c` + `vice/mon_screen.c` | `libmonitor` | `src/monitor` |
| `libs/driveattach/src/driveattach_core.c` | `libmonitor` | `src/monitor` |
| `libs/checkpoint/src/checkpoint_core.c` | `libmonitor` | `src/monitor` |
| `libs/screen/vice/c64screen.c` | `libc64sc` | `src/c64` |
| `libs/asid/src/asid_core.c` + `vice/soundasid.c` | sounddrv lib | `src/arch/shared/sounddrv` |
| `libs/bustrace/src/bustrace_core.c` + `vice/soundbustrace.c` | `libvsid` | `src/c64` |

## Bus-trace feature

The bus-trace (`-bustrace <file>`) records the full 6510 bus during VSID
playback — every memory access `{cycle, addr, val, rw, pc}` — into a
deterministic binary trace, the provenance substrate for generic BACC recovery.
It is **additive and non-perturbing**: with no `-bustrace` file the per-access
hook is a single null-pointer test and the SID-register dump is byte-identical
to a build without the feature.

- Core (`bustrace_core.c`) + adapter (`soundbustrace.c`) compile into
  `libvsid.a` (Makefile.am wiring above).
- The per-access hook is placed in `src/c64/vsidcpu.c`'s `FEATURE_CPUMEMHISTORY`
  memmap functions — the one point where VICE already observes every CPU read
  and write (patch `07-bustrace-vsidcpu-hook.patch`). The trace therefore needs
  the tree configured with `--enable-cpuhistory`.
- Resource (`BusTraceFile`), cmdline (`-bustrace`) and shutdown finalize are
  wired in `src/c64/vsid.c` (patch `08-bustrace-vsid-wiring.patch`).

Usage:

```
vsid --enable-cpuhistory ... -bustrace tune.bus.bin tune.sid    # trace + (any) dump
```

Run twice on the same tune and the `.bus.bin` is byte-identical (the trailer
carries a record count + CRC-32 a reader can verify). See
`libs/bustrace/include/revice_bustrace.h` for the record format.

## What changes in the VICE tree

### A. Build wiring (mechanical — handled by `apply-wiring.sh`)

`src/monitor/Makefile.am`
- add to `AM_CPPFLAGS`:
  `-I$(top_srcdir)/src/revice/libs/keymatrix/include -I$(top_srcdir)/src/revice/libs/keymatrix/vice`
  `-I$(top_srcdir)/src/revice/libs/screen/include    -I$(top_srcdir)/src/revice/libs/screen/vice`
  `-I$(top_srcdir)/src/revice/libs/driveattach/include`
  `-I$(top_srcdir)/src/revice/libs/checkpoint/include`
- add to `libmonitor_a_SOURCES` (and *remove* any in-tree `mon_keymatrix.c/.h`,
  `mon_screen.c/.h`):
  `$(top_srcdir)/src/revice/libs/keymatrix/src/keymatrix_core.c`
  `$(top_srcdir)/src/revice/libs/keymatrix/vice/mon_keymatrix.c`
  `$(top_srcdir)/src/revice/libs/screen/src/screen_core.c`
  `$(top_srcdir)/src/revice/libs/screen/vice/mon_screen.c`
  `$(top_srcdir)/src/revice/libs/driveattach/src/driveattach_core.c`
  `$(top_srcdir)/src/revice/libs/checkpoint/src/checkpoint_core.c`

`src/c64/Makefile.am`
- add to `AM_CPPFLAGS`:
  `-I$(top_srcdir)/src/revice/libs/screen/include -I$(top_srcdir)/src/revice/libs/screen/vice`
- add to `libc64sc_a_SOURCES` (and *remove* any in-tree `c64screen.c`):
  `$(top_srcdir)/src/revice/libs/screen/vice/c64screen.c`

`src/arch/shared/sounddrv/Makefile.am` — the ASID driver is built
*conditionally* (it links ALSA) through configure's `@SOUND_DRIVERS@` +
`EXTRA_libsounddrv_a_SOURCES`, so it must **not** be added to a plain
`_a_SOURCES` (that compiles it unconditionally and breaks non-ALSA builds):
- add to `AM_CPPFLAGS`: `-I$(top_srcdir)/src/revice/libs/asid/include`
- in `EXTRA_libsounddrv_a_SOURCES`, replace the in-tree `soundasid.c` entry with:
  `$(top_srcdir)/src/revice/libs/asid/vice/soundasid.c`
  `$(top_srcdir)/src/revice/libs/asid/src/asid_core.c`

`configure.ac` — emit `asid_core.o` next to `soundasid.o` so it is linked
whenever the ASID driver is (under `--with-alsa`):
- `SOUND_DRIVERS="$SOUND_DRIVERS soundalsa.o soundasid.o asid_core.o"`
  (changing `configure.ac` requires re-running `./autogen.sh`).

On a pristine *upstream* VICE tree (no ASID at all) this is a full new sound
device, not a hook: also add the `--with-alsa` `SOUND_DRIVERS` line, the
`EXTRA_libsounddrv_a_SOURCES` entries, the `"asid"` row in `sound.c`'s device
table, and the `dump2` field + `sound_init_asid_device` prototype in `sound.h`
(see section B). `apply-wiring.sh` repoints/extends these when the ASID driver
is already present (as in the asid-vice fork).

### B. Code wiring (the genuine VICE edits — see `patches/`)

These are the lines that must live in VICE itself. They are intentionally tiny
and are the only thing to reconcile when VICE moves. The `patches/` directory
holds them as diffs against VICE 3.10 (the asid-vice fork's base):

| patch | file(s) | what |
|---|---|---|
| `01-monitor-lexer-parser.patch` | `mon_lex.l`, `mon_parse.y`, `mon_command.c` | `keymatrix`/`screenscrape` tokens, grammar, help |
| `02-c64-hooks.patch` | `c64.c`, `c64cia1.c` | `c64screen_init()` + CIA1 observation hooks |
| `03-binmon-opcodes.patch` | `monitor_binary.c` | binmon `0x74`–`0x78` opcodes + the `silent` flag |
| `04-sound-asid-device.patch` | `sound.h`, `sound.c` | `dump2` field, prototype, device-table row, call-site |
| `05-silent-checkpoint.patch` | `mon_breakpoint.c/.h` | `silent` field + the on-hit guard |
| `06-binmon-route-through-revice-cores.patch` | `monitor_binary.c` | route DRIVE_ATTACH/checkpoint through the revice cores (apply after `03`) |

Patches `01`–`05` are exactly the fork's feature additions, so they apply to a
pristine VICE 3.10 tree; `06` applies on top of `03`. When VICE moves, these are
the diffs to reconcile by hand. Per-file summary:

- **`src/monitor/mon_lex.l`** — a `KMX_VERB` start condition + the `keymatrix`
  and `screenscrape` command tokens.
- **`src/monitor/mon_parse.y`** — `#include "mon_keymatrix.h"` / `"mon_screen.h"`,
  the new `%token`s, and the `monitor_misc_rules` for the keymatrix sub-verbs
  and `screenscrape`.
- **`src/monitor/mon_command.c`** — the `screenscrape` and `keymatrix` help-text
  entries.
- **`src/monitor/monitor_binary.c`** — `#include "mon_keymatrix.h"`,
  `"mon_screen.h"`, `"attach.h"`; the `0x74`–`0x78` command/response enums; the
  handler functions (KEYMATRIX_SET/TAP/GET, SCREEN_GET, DRIVE_ATTACH); the
  dispatch `else if` arms; and the `silent` flag in
  `monitor_binary_process_checkpoint_set()` (patch `03`). Patch `06` then routes
  DRIVE_ATTACH through `da_decode()` and the checkpoint `silent` decode through
  `checkpoint_decode_silent()` (adding the `revice_driveattach.h` /
  `revice_checkpoint.h` includes), so those two cores are the single source of
  truth rather than inline copies.
- **`src/monitor/mon_breakpoint.c` / `.h`** — the `bool silent` field on
  `mon_checkpoint_s` and the `if (!cp->silent) { ... }` guard around the per-hit
  event/trace/disassembly block in `mon_breakpoint_check_checkpoint()`.
- **`src/c64/c64.c`** — call `c64screen_init()` from `machine_specific_init()`.
- **`src/c64/c64cia1.c`** — `#include "mon_keymatrix.h"` (plain, **not**
  `monitor/mon_keymatrix.h`: the header lives in the submodule and is reached via
  the `keymatrix/vice` include `apply-wiring.sh` adds to `libc64sc`) + the
  `mon_keymatrix_cia1_pa_read(...)` / `_pb_read(...)` calls in `read_ciapa` /
  `read_ciapb`.
- **`src/sound.h`** — the custom `dump2` function-pointer field on
  `sound_device_t` and the `int sound_init_asid_device(void);` prototype.
- **`src/sound.c`** — the `"asid"` entry in the device table and the `dump2`
  call-site in `sound_store()`.

## Using it

```sh
# one-time, inside the VICE tree:
git submodule add <revice-url> src/revice
integration/vice/apply-wiring.sh            # run from the VICE tree root
./autogen.sh && ./configure --with-alsa && make -j

# when upstream VICE advances:
git submodule update --remote src/revice    # bump revice
#  re-apply / reconcile the small code edits in patches/ against the new tree
./autogen.sh && make -j
```

`apply-wiring.sh` performs the build wiring (section A) idempotently and applies
the code-wiring patches in `patches/` if present, reporting anything that needs
a hand-port.

### Converting an existing fork that already carries the features in-tree

The asid-vice fork already has the feature bodies + code wiring in its own tree.
To switch it to source them from the submodule instead (so revice is the single
source of truth):

1. `git submodule add <revice-url> src/revice`
2. Delete the in-tree feature bodies (now sourced from the submodule):
   `src/monitor/mon_keymatrix.{c,h}`, `src/monitor/mon_screen.{c,h}`,
   `src/c64/c64screen.c`, `src/arch/shared/sounddrv/soundasid.c`, and remove
   their `_SOURCES` lines.
3. Run `apply-wiring.sh` (adds the submodule sources/includes + `configure.ac`
   `SOUND_DRIVERS` + patch `06`; patches `01`–`05` report "already applied").
4. Because `src/monitor/mon_keymatrix.h` is gone, change `c64cia1.c`'s include
   from `"monitor/mon_keymatrix.h"` to `"mon_keymatrix.h"` (resolved via the
   `keymatrix/vice` include added to `libc64sc`).
5. `./autogen.sh && ./configure --with-alsa && make -j`.
