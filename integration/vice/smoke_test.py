#!/usr/bin/env python3
"""smoke_test.py - launch a headless x64sc with the binary monitor and exercise
the revice-backed opcodes end to end.

Usage: smoke_test.py /path/to/x64sc

Starts x64sc with -binarymonitor on a local port, resumes the CPU, then issues
SCREEN_GET (0x77) and KEYMATRIX_GET (0x76) and checks the response framing and
fixed sizes against the documented layouts. Exits non-zero on any mismatch.

This file is part of VICE, the Versatile Commodore Emulator.
See LICENSE for copyright notice (GPL-2.0-or-later).
"""

import os
import socket
import struct
import subprocess
import sys
import tempfile
import time

PORT = 6502
SCREEN_GET = 0x77
KEYMATRIX_GET = 0x76
KEYMATRIX_TAP = 0x75
EXIT = 0xaa

SCREEN_RESPONSE_SIZE = 4072
KEYMATRIX_GET_RESPONSE_SIZE = 24


def send(sock, op, body=b"", req=1):
    sock.send(bytes([0x02, 0x02]) + struct.pack("<II", len(body), req)
              + bytes([op]) + body)


def read_one(sock):
    hdr = b""
    while len(hdr) < 12:
        chunk = sock.recv(12 - len(hdr))
        if not chunk:
            raise EOFError("monitor closed the connection")
        hdr += chunk
    _stx, _ver, blen, op, err, rid = struct.unpack("<BBIBBI", hdr)
    body = b""
    while len(body) < blen:
        body += sock.recv(blen - len(body))
    return op, err, rid, body


def call(sock, op, body=b"", req=1):
    send(sock, op, body, req)
    while True:
        r = read_one(sock)
        if r[2] == req:
            return r


def main():
    if len(sys.argv) < 2:
        print("usage: smoke_test.py /path/to/x64sc", file=sys.stderr)
        return 2
    x64sc = sys.argv[1]

    workdir = tempfile.mkdtemp(prefix="revice-smoke-")
    proc = subprocess.Popen(
        [x64sc, "-default", "-warp", "-silent",
         "-binarymonitor",
         "-binarymonitoraddress", f"ip4://127.0.0.1:{PORT}"],
        cwd=workdir,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        env={**os.environ, "SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy"},
    )
    try:
        sock = None
        for _ in range(100):
            if proc.poll() is not None:
                print("x64sc exited early", file=sys.stderr)
                return 1
            try:
                sock = socket.create_connection(("127.0.0.1", PORT), timeout=1)
                break
            except OSError:
                time.sleep(0.2)
        if sock is None:
            print("could not connect to binmon", file=sys.stderr)
            return 1

        # Drain the initial STOPPED event, then resume the CPU.
        sock.settimeout(0.5)
        try:
            while True:
                read_one(sock)
        except (socket.timeout, EOFError):
            pass
        sock.settimeout(5)
        call(sock, EXIT)

        # SCREEN_GET: C64 build -> fixed 4072-byte response.
        op, err, _rid, body = call(sock, SCREEN_GET, req=2)
        assert op == SCREEN_GET, f"bad screen op {op:#x}"
        assert err == 0, f"SCREEN_GET error {err:#x}"
        assert len(body) == SCREEN_RESPONSE_SIZE, \
            f"SCREEN_GET len {len(body)} != {SCREEN_RESPONSE_SIZE}"
        assert body[1] == 25 and body[2] == 40, "screen dims not 25x40"
        print(f"SCREEN_GET ok: {len(body)} bytes, mode={body[0]}")

        # KEYMATRIX_TAP "A" (row 1 col 2), observed mode, then KEYMATRIX_GET.
        call(sock, KEYMATRIX_TAP,
             struct.pack("<BHB", 0, 60, 1) + bytes([1, 2]), req=3)
        op, err, _rid, body = call(sock, KEYMATRIX_GET, req=4)
        assert op == KEYMATRIX_GET, f"bad keymatrix op {op:#x}"
        assert err == 0, f"KEYMATRIX_GET error {err:#x}"
        assert len(body) == KEYMATRIX_GET_RESPONSE_SIZE, \
            f"KEYMATRIX_GET len {len(body)} != {KEYMATRIX_GET_RESPONSE_SIZE}"
        assert body[21] == 1, f"expected 1 active key, got {body[21]}"
        print(f"KEYMATRIX_GET ok: {len(body)} bytes, n_keys={body[21]}")

        print("smoke test PASSED")
        return 0
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()


if __name__ == "__main__":
    sys.exit(main())
