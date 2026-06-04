# revice

Reusable, **independently tested** cores for the custom features carried by the
[asid-vice](https://github.com/anarkiwi/asid-vice) fork of the
[VICE](https://vice-emu.sourceforge.io/) Commodore emulator.

The fork adds five features directly inside VICE's source tree. That makes them
expensive to maintain: every time upstream VICE advances they have to be
hand-reconciled against a moving codebase, and the feature *logic* has no tests
of its own — it can only be exercised by building the whole emulator.

revice moves the logic out into small, pure C libraries with no dependency on
VICE. Everything that touches the emulator is injected through a host-ops
struct, so the fiddly, version-stable parts — the ASID MIDI wire format, the
`SCREEN_GET` byte layout, the keymatrix tables / parsing / observation state
machine, the binmon request decoders — are unit-tested here in CI with **no
VICE build required**. VICE then consumes revice as a git submodule and each
feature shrinks to a thin adapter plus a few unchanged wiring lines.

## The libraries

| lib | what it is | VICE adapter(s) |
|---|---|---|
| `libs/asid` | ASID-over-MIDI protocol codec (SID-register tracking → SysEx update messages, long + short forms) | `vice/soundasid.c` (ALSA transport + `sound_device_t`) |
| `libs/keymatrix` | C64 keyboard-matrix injection: key tables, arg parsing, the press/tap/observe/release state machine, binmon codecs | `vice/mon_keymatrix.c` |
| `libs/screen` | `screenscrape`: pack VIC-II state + RAM into the 4072-byte `SCREEN_GET` response; render the 40×25 grid | `vice/mon_screen.c`, `vice/c64screen.c` |
| `libs/driveattach` | binmon `DRIVE_ATTACH` (0x78) body decoder | (inline in `monitor_binary.c`) |
| `libs/checkpoint` | silent-checkpoint `CHECKPOINT_SET` body decode (the behavioral change ships as a patch) | (inline in `monitor_binary.c`) |

Each `libs/<name>` has:
- `include/revice_<name>.h` — the core API and host-ops vtable,
- `src/<name>_core.c` — pure C, no VICE headers (only `<stdint.h>` etc.),
- `vice/…` — the thin VICE adapter(s), compiled only inside a VICE tree,
- `tests/test_<name>.c` — assert-based unit tests run by CTest.

## Build & test

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

No dependencies beyond a C99 compiler and CMake ≥ 3.13. The `vice/` adapters are
**not** built here (they need VICE headers); they are compiled inside a VICE
tree — see `integration/vice/`.

## Using it from VICE

revice is added to a VICE tree as a submodule at `src/revice`; the cores +
adapters are compiled directly from the submodule by the existing per-directory
automake libraries, and the genuine VICE edits are a small set of patches.
`integration/vice/README.md` has the full model, the wiring patches
(`integration/vice/patches/`), an `apply-wiring.sh` helper, and a binmon
`smoke_test.py`.

```sh
# inside a VICE tree:
git submodule add https://github.com/anarkiwi/revice src/revice
src/revice/integration/vice/apply-wiring.sh
./autogen.sh && ./configure --with-alsa && make -j
```

## CI

`.github/workflows/ci.yml` runs two jobs:
- **unit** — builds and runs the core unit tests (fast, no VICE).
- **integration-build** — checks out the asid-vice fork, wires revice in, and
  builds the affected VICE libraries (`libmonitor`, `libc64sc`, `libsounddrv`),
  asserting the revice objects archived in; then best-effort builds headless
  `x64sc` and runs the binmon smoke test.

## Feature reference

This is the canonical reference for the wire formats and commands the cores
here implement and test. For how to *build/run* the emulator with these
features, see the asid-vice fork's `README.md`.

### ASID sound device

Enable with `-sounddev asid`; pick the MIDI port with `-soundarg`. The list of
ports is printed to `vice.log` when the driver starts. The argument is a
bitfield:

| `-soundarg` value | meaning |
|---|---|
| `0`–`1023` | MIDI output port number |
| `+1024` | use the shorter per-register ("update reg") SysEx form (Vessel/VAP) |

```
vsid -sounddev asid -soundarg 1 tune.sid          # port 1, standard form
vsid -sounddev asid -soundarg 1025 tune.sid       # port 1, short register form
vsid -sound -soundoutput 2 -sidextra 1 -sounddev asid -soundarg 1 2sid.sid   # 2SID
```

The codec tracks each SID's registers and, per IRQ, emits only the changed
ones; control registers (4/11/18) are transmitted last so a voice's
freq/PW/ADSR are applied before its gate edge (both the standard and short
forms). Timing is scheduled against the machine's actual CPU clock (PAL / NTSC
/ Drean). C64 (and 2SID via Vessel/VAP) only.

### Monitor commands

Both commands work in the interactive text monitor (debugging) and as
binary-monitor opcodes (automation). C64 only.

#### `keymatrix` — keyboard-matrix injection

Injects key state into the CIA1 keyboard matrix, so programs that scan the
matrix directly (games, demos, loaders) see it. `tap` releases on the first
observed CIA1 read of an injected bit, or a frame timeout.

```
keymatrix tap     <key> [<key> ...] [for <frames>]   # release on observation / timeout
keymatrix press   <key> [<key> ...]                  # sticky chord
keymatrix release [<key> [<key> ...]]                # clear listed keys, or all
keymatrix poke    <row> <col> <0|1>                  # raw matrix bit
keymatrix show                                       # live matrix + last tap report
keymatrix names                                      # list recognised key names
```

Keys are symbolic (`A`, `F1`, `LSHIFT`, `RUNSTOP`, `RESTORE`, `CBM`, …;
case-insensitive) or a raw `<row>,<col>` pair like `7,7`.

| Opcode | Name | Body | Response |
|---|---|---|---|
| `0x74` | KEYMATRIX_SET | `count:u8`, then `count × {row:i8, col:i8, value:u8}` | empty |
| `0x75` | KEYMATRIX_TAP | `mode:u8` (0=observed, 1=fixed-frames), `frames:u16`, `count:u8`, then `count × {row:i8, col:i8}` | empty |
| `0x76` | KEYMATRIX_GET | empty | 24 bytes (below) |

KEYMATRIX_GET response (24 bytes, little-endian):

| Offset | Type | Field |
|---|---|---|
| 0–7 | u8×8 | `keyarr[0..7]` — live matrix rows |
| 8 | u8 | custom-key bitmap: bit0=RESTORE1, bit1=RESTORE2, bit2=CAPS, bit3=4080 |
| 9–11 | — | padding (zero) |
| 12–15 | u32 | `cia1_reads_total` |
| 16–19 | u32 | `cia1_reads_sampling` |
| 20 | u8 | `release_reason` (0=none, 1=observed, 2=timeout, 3=manual) |
| 21 | u8 | `n_keys` |
| 22–23 | u16 | `frames_until_timeout` (active tap only, else 0) |

#### `screenscrape` — read the screen + active charset

`screenscrape` (text; add `raw` for hex) renders the 40×25 screen; the binmon
`SCREEN_GET` opcode returns the same state as one fixed blob.

| Opcode | Name | Body | Response |
|---|---|---|---|
| `0x77` | SCREEN_GET | empty | 4072 bytes (below) |

SCREEN_GET response — header (24 bytes):

| Offset | Type | Field |
|---|---|---|
| 0 | u8 | `vic_mode`: 0=normal-text, 1=mc-text, 2=hires-bitmap, 3=mc-bitmap, 4=ext-text, 5–7=illegal |
| 1 | u8 | `rows` (25) |
| 2 | u8 | `cols` (40) |
| 3 | u8 | `charset_kind`: 0=ROM upper/graphics, 1=ROM upper/lowercase, 2=custom RAM |
| 4 | u8 | `vic_bank` (0..3) |
| 5 | u8 | `border_color` |
| 6–9 | u8×4 | `bg_color[0..3]` |
| 10 | u8 | raw `$D011` |
| 11 | u8 | raw `$D016` |
| 12 | u8 | raw `$D018` |
| 13 | u8 | reserved (zero) |
| 14–15 | u16 | `screen_addr` |
| 16–17 | u16 | `charset_addr` |
| 18–19 | u16 | `bitmap_addr` (0 in text modes) |
| 20–23 | u32 | `payload_len` (always 4048) |

SCREEN_GET response — payload (4048 bytes, from body offset 24):

| Offset | Bytes | Field |
|---|---|---|
| 24 | 1000 | screen RAM (screen codes in text modes) |
| 1024 | 1000 | color RAM (low nibble = foreground colour) |
| 2024 | 2048 | character set (256 chars × 8 rows); from chargen ROM if `charset_kind` is 0/1, else RAM. In bitmap mode this is the lower 2 KiB of bitmap memory — consult `vic_mode`/`bitmap_addr`. |

### `DRIVE_ATTACH` — runtime image attach / detach (binmon only)

Attach/detach a disk image at runtime — a primitive `RESOURCE_SET` cannot
express (it rejects zero-length values and the disk slot is not a resource). A
same-path re-attach also forces a flush of pending writes.

| Opcode | Name | Body | Response |
|---|---|---|---|
| `0x78` | DRIVE_ATTACH | `unit:u8` (8..11), `drive:u8` (0/1), `path_len:u8`, `path:u8 × path_len` | empty |

`path_len == 0` ⇒ detach; otherwise attach the (not NUL-terminated) path.

### silent-checkpoint — byte-granular polled coverage

A per-checkpoint `silent` flag set via an optional byte appended to the binmon
`CHECKPOINT_SET` (`0x12`) body:

| Body offset | Type | Field |
|---|---|---|
| 0–7 (8) / +1 memspace (9) | — | standard CHECKPOINT_SET body |
| 9 | u8 | `silent` (optional; bodies shorter than 10 bytes leave it false) |

When set, a hit increments `hit_count` but skips the per-hit CHECKPOINT_INFO
event, trace print, disassembly, and command — so ~45K stop-when-hit=false
watchpoints under warp playback don't drown the binmon pipeline in events.

### Binary-monitor framing

Request:

| Bytes | Field |
|---|---|
| 0 | `0x02` (STX) |
| 1 | `0x02` (API version) |
| 2–5 | body length (u32 LE) |
| 6–9 | request id (u32 LE; echoed back) |
| 10 | command opcode |
| 11… | body |

Response (solicited or unsolicited event):

| Bytes | Field |
|---|---|
| 0 | `0x02` (STX) |
| 1 | `0x02` (API version) |
| 2–5 | body length (u32 LE) |
| 6 | response opcode |
| 7 | error code |
| 8–11 | echoed request id (0 for unsolicited events) |
| 12… | body |

Error codes: `0x00` OK, `0x80` invalid length, `0x81` invalid parameter, `0x82`
invalid API version, `0x8f` command failure (e.g. `SCREEN_GET` on a non-C64
build). Unsolicited events: `0x61` JAM, `0x62` STOPPED, `0x63` RESUMED. Send
`EXIT` (`0xaa`) once after connecting to resume the CPU (binmon halts it on
connect).

## License

GPL-2.0-or-later, matching VICE. See `LICENSE`.
