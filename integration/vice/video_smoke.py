#!/usr/bin/env python3
"""End-to-end check of the rendered VIDEO_RECORD mode against a running x64sc.

Counts frames with a stop checkpoint at $C00B, run once per frame by a raster loop
(line 256); records A frames, pauses P, resumes B, stops, then ffprobes the file
(via --exec when ffprobe is in the emulator's container) for A + B frames.
"""

import argparse
import json
import shlex
import subprocess
import sys

import numpy as np
from tqdm import tqdm
from vice_driver.binmon import BinMon, OPCODE

FRAME_PC = 0xC00B
IDLE_PC = 0xE5CD
REG_PC = 3
# SEI; wait $D011 bit 7 clear; wait it set; NOP (once per frame); JMP $C001
PROG = bytes.fromhex("78ad11d030fbad11d010fbea4c01c0")


def record(bm, body, ok=True):
    """Send VIDEO_RECORD and return the binmon error byte."""
    return bm.call(OPCODE.VIDEO_RECORD, bytes(body), require_ok=ok, timeout=30).err


def run_frames(bm, checknum, n, label):
    """Resume the CPU for exactly n frames."""
    for _ in tqdm(range(n), desc=label, unit="frame"):
        bm.wait_for_checkpoint(checknum, timeout=10)


def probe(prefix, path):
    """Return ffprobe's description of the first video stream, frames counted."""
    out = subprocess.run(
        prefix
        + [
            "ffprobe",
            "-v",
            "error",
            "-count_frames",
            "-select_streams",
            "v:0",
            "-show_entries",
            "stream=width,height,nb_read_frames,r_frame_rate,codec_name,pix_fmt",
            "-of",
            "json",
            path,
        ],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    return json.loads(out)["streams"][0]


def scanline_period(prefix, path, width, height, frame):
    """Dominant row period of the top border in one frame and its power share.

    CRT scanlines give a period of height / emulated_lines output rows.
    """
    raw = subprocess.run(
        prefix
        + [
            "ffmpeg",
            "-v",
            "error",
            "-i",
            path,
            "-vf",
            f"select=eq(n\\,{frame})",
            "-frames:v",
            "1",
            "-f",
            "rawvideo",
            "-pix_fmt",
            "gray",
            "-",
        ],
        check=True,
        capture_output=True,
    ).stdout
    img = np.frombuffer(raw, np.uint8).reshape(height, width).astype(np.float64)
    rows = img[: height // 10, width // 4 : 3 * width // 4].mean(axis=1)
    spec = np.abs(np.fft.rfft(rows - rows.mean())) ** 2
    k = int(np.argmax(spec[1:])) + 1
    return len(rows) / k, float(spec[k] / max(spec[1:].sum(), 1e-12))


def scenario(args):
    """Drive the recording through start, pause, resume and stop; return checks."""
    run_a, paused, run_b = args.frames
    path_b = args.path.encode()
    start = [2, args.height & 0xFF, args.height >> 8, len(path_b), *path_b]
    checks = {}
    bm = BinMon(args.host, args.port, auto_resume=False)
    bm.connect()
    bm.run_until_pc(IDLE_PC, timeout=60)
    bm.mem_set(0xC000, PROG)
    bm.registers_set({REG_PC: 0xC000})
    checknum = bm.checkpoint_set(FRAME_PC, stop_when_hit=True).checknum
    bm.wait_for_checkpoint(checknum, timeout=10)
    checks["pause_idle_refused"] = record(bm, [3], ok=False) != 0
    checks["start"] = record(bm, start) == 0
    checks["double_start_refused"] = record(bm, start, ok=False) != 0
    run_frames(bm, checknum, run_a, "record")
    checks["pause"] = record(bm, [3]) == 0
    run_frames(bm, checknum, paused, "paused")
    checks["resume"] = record(bm, [4]) == 0
    run_frames(bm, checknum, run_b, "record")
    checks["stop"] = record(bm, [0]) == 0
    checks["short_body_refused"] = record(bm, [2, 0xD0], ok=False) != 0
    bm.exit()
    bm.close()
    return checks


def main():
    """Run the scenario and print a JSON report; non-zero exit on mismatch."""
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=6502)
    ap.add_argument("--path", required=True, help="output path as VICE sees it")
    ap.add_argument("--height", type=int, default=720)
    ap.add_argument("--frames", type=int, nargs=3, default=[150, 40, 100])
    ap.add_argument("--exec", default="", help="command prefix for ffprobe/ffmpeg")
    args = ap.parse_args()
    prefix = shlex.split(args.exec)
    checks = scenario(args)
    st = probe(prefix, args.path)
    width, height = int(st["width"]), int(st["height"])
    period, share = scanline_period(
        prefix, args.path, width, height, args.frames[0] // 2
    )
    report = {
        "stream": st,
        "frames_run": sum(args.frames),
        "frames_paused": args.frames[1],
        "frames_expected": args.frames[0] + args.frames[2],
        "frames_encoded": int(st["nb_read_frames"]),
        "border_row_period": round(period, 3),
        "border_row_period_power_share": round(share, 3),
        "checks": checks,
    }
    checks["frame_count"] = report["frames_encoded"] == report["frames_expected"]
    checks["height"] = height == args.height
    print(json.dumps(report, indent=2))
    return 0 if all(checks.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
