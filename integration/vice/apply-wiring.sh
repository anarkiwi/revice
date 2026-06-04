#!/usr/bin/env bash
#
# apply-wiring.sh - wire the revice submodule into a VICE source tree.
#
# Run from the root of a VICE tree that has revice as a submodule at
# src/revice. Performs the mechanical build wiring (automake source + include
# additions) idempotently, then applies the code-wiring patches in patches/.
# See README.md for the full model.
#
# This file is part of VICE, the Versatile Commodore Emulator.
# See LICENSE for copyright notice (GPL-2.0-or-later).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VICE_ROOT="$(pwd)"
MARK_BEGIN="# >>> revice wiring (managed by apply-wiring.sh) >>>"
MARK_END="# <<< revice wiring <<<"

die() { echo "apply-wiring: error: $*" >&2; exit 1; }

[ -f "$VICE_ROOT/src/monitor/Makefile.am" ] || \
    die "run this from the root of a VICE source tree (src/monitor/Makefile.am not found)"
[ -d "$VICE_ROOT/src/revice/libs" ] || \
    die "revice submodule not found at src/revice — run: git submodule add <url> src/revice"

# Append a guarded automake fragment to a Makefile.am if not already present.
# $1 = path to Makefile.am, $2 = fragment body.
wire_makefile() {
    local mk="$1" body="$2"
    if grep -qF "$MARK_BEGIN" "$mk"; then
        echo "  already wired: $mk (skipping)"
        return
    fi
    {
        printf '\n%s\n' "$MARK_BEGIN"
        printf '%s\n' "$body"
        printf '%s\n' "$MARK_END"
    } >> "$mk"
    echo "  wired: $mk"
}

R='$(top_srcdir)/src/revice/libs'

echo "Wiring build (Makefile.am)..."

wire_makefile "$VICE_ROOT/src/monitor/Makefile.am" "\
AM_CPPFLAGS += \\
	-I$R/keymatrix/include   -I$R/keymatrix/vice \\
	-I$R/screen/include      -I$R/screen/vice \\
	-I$R/driveattach/include -I$R/checkpoint/include
libmonitor_a_SOURCES += \\
	$R/keymatrix/src/keymatrix_core.c \\
	$R/keymatrix/vice/mon_keymatrix.c \\
	$R/screen/src/screen_core.c \\
	$R/screen/vice/mon_screen.c \\
	$R/driveattach/src/driveattach_core.c \\
	$R/checkpoint/src/checkpoint_core.c"

# c64cia1.c (in libc64sc) includes the keymatrix header, so libc64sc needs the
# keymatrix adapter include dir too — with the header in the submodule it must
# be reached as "mon_keymatrix.h" (see the c64cia1.c note in README section B).
wire_makefile "$VICE_ROOT/src/c64/Makefile.am" "\
AM_CPPFLAGS += -I$R/screen/include -I$R/screen/vice -I$R/keymatrix/vice
libc64sc_a_SOURCES += $R/screen/vice/c64screen.c"

# ASID sound device. Unlike the monitor features, the ASID driver is built
# *conditionally* (it pulls in ALSA) via configure's @SOUND_DRIVERS@ + the
# EXTRA_libsounddrv_a_SOURCES list, so it must NOT be appended to _a_SOURCES
# (that would compile it unconditionally and break non-ALSA builds). Instead:
#   - repoint the EXTRA list's soundasid.c at the revice adapter,
#   - add asid_core.c (no ALSA dependency) so automake knows how to build it,
#   - make configure emit asid_core.o next to soundasid.o in SOUND_DRIVERS.
SNDMK="$VICE_ROOT/src/arch/shared/sounddrv/Makefile.am"
if [ -f "$SNDMK" ] && grep -qE '^\s*soundasid\.c' "$SNDMK"; then
    if grep -qF "$MARK_BEGIN" "$SNDMK"; then
        echo "  already wired: $SNDMK (skipping)"
    else
        # Repoint the existing EXTRA_libsounddrv_a_SOURCES soundasid.c entry and
        # add asid_core.c right after it.
        sed -i -E "s#^(\s*)soundasid\.c( *\\\\?)#\1$R/asid/vice/soundasid.c \\\\\n\1$R/asid/src/asid_core.c\2#" "$SNDMK"
        wire_makefile "$SNDMK" "AM_CPPFLAGS += -I$R/asid/include"
        echo "  wired: $SNDMK (EXTRA list repointed + asid_core.c added)"
    fi

    # configure.ac: ensure asid_core.o ships wherever soundasid.o does.
    CONF="$VICE_ROOT/configure.ac"
    if [ -f "$CONF" ]; then
        if grep -q 'soundasid\.o asid_core\.o' "$CONF"; then
            echo "  already wired: $CONF (SOUND_DRIVERS)"
        elif grep -q 'soundasid\.o' "$CONF"; then
            sed -i 's/soundasid\.o/soundasid.o asid_core.o/g' "$CONF"
            echo "  wired: $CONF (added asid_core.o to SOUND_DRIVERS) — re-run ./autogen.sh"
        else
            echo "  NOTE: $CONF has no soundasid.o in SOUND_DRIVERS; add the ASID driver"
            echo "        (and asid_core.o) per README section A before building"
        fi
    fi
else
    echo "  skip: ASID not present in $SNDMK (upstream tree) — see README section A"
fi

echo "Applying code-wiring patches..."
shopt -s nullglob
patches=("$SCRIPT_DIR"/patches/*.patch)
if [ ${#patches[@]} -eq 0 ]; then
    echo "  no patches/*.patch present — apply the code edits from README.md section B by hand"
else
    for p in "${patches[@]}"; do
        if git -C "$VICE_ROOT" apply --reverse --check "$p" >/dev/null 2>&1; then
            echo "  already applied: $(basename "$p")"
        elif git -C "$VICE_ROOT" apply --check "$p" >/dev/null 2>&1; then
            git -C "$VICE_ROOT" apply "$p"
            echo "  applied: $(basename "$p")"
        else
            echo "  NEEDS HAND-PORT: $(basename "$p") does not apply cleanly to this tree" >&2
        fi
    done
fi

cat <<'EOF'

Done. Next:
  ./autogen.sh && ./configure --with-alsa && make -j
For a headless build:
  ./autogen.sh && ./configure --with-alsa --enable-headlessui && make -j x64sc vsid
EOF
