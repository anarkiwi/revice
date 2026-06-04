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

## Feature documentation

The user-facing behavior of each feature (binmon opcodes, response layouts,
monitor commands) is documented in the asid-vice fork's `README.md`. The wire
formats there are the contracts the cores here implement and test.

## License

GPL-2.0-or-later, matching VICE. See `LICENSE`.
