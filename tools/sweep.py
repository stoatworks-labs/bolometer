"""Render every parameter at both ends of its range and fail if any made no
difference. (ferrofluid's script by way of fountain and filings; the table is
bolometer's.)

**This is the only thing in the repo that catches a dead control.** A GLSL
uniform whose name does not match the C++ is silently ignored --
`glGetUniformLocation` returns -1 and `glUniform` on -1 is a documented no-op
-- so a slider can be stone dead while everything compiles, links, loads and
renders.

Each control is swept where it can act. A render is 180 frames (3 s at 60 fps)
at 320x180 of the harness's card, a hot ball crossing a sunset skyline. Three
controls need a context. Shutter Interval only shows once an FFC has started:
its renders run 320 frames, so at 5 s the low end is frozen mid-FFC when the
high end (Off) is live. Drift grows at most a kelvin a minute: its renders run
at 2 fps (90 s) with the FFC Off. Level and Width only act in Span Manual.
Shutter is a press (`--at`, half a second before the frame read). An option
parameter reads back 0..1 whatever its count (the fleet's trap), so options
are set here by name. A control whose ends differ by less than `--floor` (mean
8-bit difference per channel) is barely alive: vectrix's lesson.

Usage::

    tools/sweep.py [--build BUILD_DIR] [--verbose] [--jobs N] [--bare]

`--bare` drops every per-control context, the addendum's honesty check: what
goes dead there is the list of controls that only act with another one set,
which AGENTS.md records.
"""

import argparse
import concurrent.futures
import pathlib
import re
import subprocess
import sys
import tempfile
import zlib

REPO = pathlib.Path(__file__).resolve().parent.parent

MANUAL = ["Span=Manual"]
NO_FFC = ["Shutter Interval=1"]
SLOW = [["--fps", "2"]]

# name -> (low settings, high settings, context settings, frames)
SWEEP = {
    "Frame Rate": (["Frame Rate=9 Hz"], ["Frame Rate=60 Hz"], [], 180),
    "Shutter Interval": (["Shutter Interval=0"], ["Shutter Interval=1"], [], 320),
    "Shutter": ([], [["--at", "150", "Shutter=1"]], [], 180),
    "Drift": (["Drift=0"], ["Drift=1"], NO_FFC + SLOW, 180),
    "Narcissus": (["Narcissus=0"], ["Narcissus=1"], [], 180),
    "Span": (["Span=Auto"], ["Span=Manual"], [], 180),
    "Plateau": (["Plateau=0"], ["Plateau=1"], [], 180),
    "Level": (["Level=0.2"], ["Level=0.8"], MANUAL, 180),
    "Width": (["Width=0.2"], ["Width=0.8"], MANUAL, 180),
    "Palette": (["Palette=White Hot"], ["Palette=Ironbow"], [], 180),
    "Halo": (["Halo=0"], ["Halo=1"], [], 180),
    "Mix": (["Mix=0"], ["Mix=1"], [], 180),
}

# The About block: a text line and browser buttons, with no pixel to sweep.
SKIP = {"About", "Project page", "Source on GitHub", "Support the work", "User guide"}


def read_png(path):
    """Enough of PNG for bmtest's own writer: 8-bit RGBA, filter 0 rows."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, width, height, idat = 8, 0, 0, b""
    while pos < len(data):
        length = int.from_bytes(data[pos:pos + 4], "big")
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width = int.from_bytes(body[0:4], "big")
            height = int.from_bytes(body[4:8], "big")
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = width * 4
    out = bytearray()
    for y in range(height):
        start = y * (stride + 1)
        out += raw[start + 1:start + 1 + stride]
    return bytes(out)


def difference(a, b):
    if len(a) != len(b):
        return 255.0
    return sum(abs(x - y) for x, y in zip(a, b)) / len(a)


def render(bmtest, out, settings, frames):
    args = [str(bmtest), "--out", str(out), "--size", "320x180", "--frames", str(frames)]
    for setting in settings:
        args += list(setting) if isinstance(setting, list) else ["--set", setting]
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"bmtest failed: {' '.join(args)}\n{result.stderr.strip()}")
    return read_png(out)


def parameters(bmtest):
    result = subprocess.run([str(bmtest), "--list"], capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"bmtest --list failed: {result.stderr.strip()}")
    names = []
    for line in result.stdout.splitlines()[1:]:
        parts = re.split(r"\s{2,}", line.strip())
        if len(parts) >= 3 and parts[0].isdigit():
            names.append(parts[1].strip())
    return names


def sweep_one(bmtest, scratch, name, bare):
    low, high, context, frames = SWEEP[name]
    context = [] if bare else context
    frames = 180 if bare else frames
    tag = f"p{abs(hash(name))}"
    before = render(bmtest, scratch / f"{tag}-a.png", context + low, frames)
    after = render(bmtest, scratch / f"{tag}-b.png", context + high, frames)
    return name, difference(before, after)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=pathlib.Path, default=REPO / "build")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--bare", action="store_true")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--floor", type=float, default=0.05,
                        help="mean 8-bit difference below which a control is 'barely alive'")
    args = parser.parse_args()

    build = args.build if args.build.is_absolute() else REPO / args.build
    bmtest = build / "bmtest"
    if not bmtest.exists():
        print(f"{bmtest} not found", file=sys.stderr)
        return 1

    plugin = "SW Bolometer"
    declared = parameters(bmtest)
    unknown = [n for n in declared if n not in SWEEP and n not in SKIP]
    if unknown:
        # A new parameter with no sweep is a hole, not a pass.
        print(f"{plugin}: no sweep defined for: {', '.join(unknown)}", file=sys.stderr)
        return 1
    dead, weak = [], []
    with tempfile.TemporaryDirectory() as scratch:
        scratch = pathlib.Path(scratch)
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            names = [n for n in declared if n in SWEEP]
            results = list(pool.map(lambda n: sweep_one(bmtest, scratch, n, args.bare), names))
    for name, delta in results:
        if delta == 0.0:
            dead.append(name)
            print(f"  DEAD {plugin}: {name:16s} both ends identical")
        elif delta < args.floor:
            weak.append(name)
            print(f"  WEAK {plugin}: {name:16s} mean delta {delta:.4f}")
        elif args.verbose:
            print(f"  ok   {plugin}: {name:16s} mean delta {delta:.3f}")
    print(f"{plugin}: {len(results)} parameters swept{' (bare: no context)' if args.bare else ''}, "
          f"{len(dead)} dead, {len(weak)} barely alive")
    if dead or weak:
        if not args.bare:
            print("\nA parameter that changes nothing is usually a uniform name that does not match\n"
                  "the C++, or a setting nothing reads. Both are silent everywhere else.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
