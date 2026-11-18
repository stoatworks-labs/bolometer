"""The browser demo against the plugin: the same frames, two renderers, one picture.

    python3 demo/tools/crosscheck/crosscheck.py [--bmtest build-verify/bmtest] [--size 320x180] [--keep DIR]

Each case below is a run of frames -- a camera has a history, so one frame
proves little -- rendered twice:

  - by the PLUGIN: `bmtest --pipe` (the real BolometerPlugin class in a GL 4.1
    context, on its synthetic clock: frame n at n / fps seconds), fed the
    harness's own card, the hot ball crossing it, with the case's settings
    (`--set`) and cues (`--script`). Build bmtest first; tools/verify.sh
    leaves one in build-verify/.
  - by the PAGE: demo/ served from here and loaded in headless Chrome,
    through window.bolometerDemo.begin, which makes a fresh instance and runs
    the same frames at the same times with the same settings and cues,
    reading its float output back and rounding it as bmtest rounds its own.

The frames are the harness's, taken from bmtest itself rather than rebuilt
here: with Mix at 0 the plugin hands its clip through to the bit, so
`bmtest --film N --set Mix=0` IS the card, frame by frame. The page's own
card (plugin.js's GLSL port of buildCard, the page's default clip) is
compared with those frames too, and passes within --max levels: it is a
float port of a picture the harness draws in double.

The GPU half is the plugin's GLSL (check_shaders.py), so what this mostly
tests is the PORT: Camera.cpp's clock (which host frames read out which
sensor instant, the FFC's schedule and freeze, the lag's weights, the
drift's age and seed), Controls.cpp's conversions and Bolometer.cpp's pass
orchestration and every uniform. A difference is a port bug, or a
difference between GL 4.1 on this machine and WebGL2 through ANGLE on the
same GPU. Before any picture, the page's table of controls (names, kinds,
defaults) is compared with `bmtest --list`, and every option element the
page names is handed to bmtest by name, which refuses one it does not know.

`--input float` hands the page the clip as bmtest holds it (RGBA32F, the
codes / 255) instead of as every clip on the page is held (RGBA8), to tell a
difference of representation from one of arithmetic.

A case passes when no pixel of any frame is more than --max levels of 255
apart and --exact % of them are identical, and the page's camera read out on
the frames bmtest's did, as many times, with as many FFCs. The negative
controls -- the page a little wrong: one setting one slider step (0.001) off,
its clock 1 ms early, a frame dropped -- are each compared with the plugin's
run as it should be, and each must fail that test, so the comparison is shown
to see a slip of that size.

Not covered, because --pipe cannot do it: a change of raster under a running
instance (the resample pass). Its uniforms are reported as unset for that
reason alone.

Measured on an M4 Max (GL 4.1 against Chrome's ANGLE on Metal), 18 November
2026: see AGENTS.md, "Browser demo", for the figures.

Not run by tools/verify.sh: it needs a GPU and Chrome. Exit 1 if a table
differs, a uniform is missing or unset (outside the resample pass), a case
fails or a negative control passes.
"""
import argparse
import functools
import http.server
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
DEMO = os.path.abspath(os.path.join(HERE, "..", ".."))
REPO = os.path.abspath(os.path.join(DEMO, ".."))
sys.dont_write_bytecode = True

# name, --set settings (host values; options by element name), cues as
# (frame, parameter, value) -- options by index, every cue a step --, the
# host's frame rate, frames, and the raster (None: --size).
CASES = [
    ("defaults", {}, [], 60, 180, None),
    ("frame-rate-30", {"Frame Rate": "30 Hz"}, [], 60, 120, None),
    ("frame-rate-60", {"Frame Rate": "60 Hz"}, [], 60, 120, None),
    ("white-hot", {"Palette": "White Hot"}, [], 60, 60, None),
    ("black-hot", {"Palette": "Black Hot"}, [], 60, 60, None),
    ("rainbow", {"Palette": "Rainbow"}, [], 60, 60, None),
    ("manual", {"Span": "Manual", "Level": "0.3", "Width": "0.5"}, [], 60, 90, None),
    ("plateau-7", {"Plateau": "0.7690"}, [], 60, 90, None),
    ("plateau-100", {"Plateau": "1"}, [], 60, 90, None),
    ("halo-0", {"Halo": "0"}, [], 60, 90, None),
    ("halo-3", {"Halo": "1"}, [], 60, 90, None),
    ("narcissus-drift", {"Narcissus": "1", "Drift": "1"}, [], 60, 120, None),
    ("mix-half", {"Mix": "0.5"}, [], 60, 90, None),
    ("shutter-press", {}, [(40, "Shutter", 1), (41, "Shutter", 0)], 60, 150, None),
    ("interval-5s-drift", {"Shutter Interval": "0", "Drift": "1"}, [], 30, 200, None),
    ("rate-changes", {}, [(50, "Frame Rate", 2), (100, "Frame Rate", 1)], 60, 150, None),
    ("palette-span-cues", {}, [(30, "Palette", 3), (60, "Span", 1), (90, "Palette", 0)], 60, 120, None),
    ("host-24fps", {}, [], 24, 72, None),
    ("host-144fps", {}, [], 144, 216, None),
    ("defaults-640x360", {}, [], 60, 60, "640x360"),
    ("defaults-1280x720", {}, [], 60, 24, "1280x720"),
]
# The negative controls: the page a little wrong, compared with the plugin's
# run of the base case as it should be. (name, base, settings changed, what
# the page does differently, label)
NEGATIVES = [
    ("plateau-1-step", "defaults", {"Plateau": "0.6612"}, None, "Plateau one slider step (0.001) off"),
    ("halo-1-step", "defaults", {"Halo": "0.3343"}, None, "Halo one step off"),
    ("level-1-step", "manual", {"Level": "0.301"}, None, "Level (Manual) one step off"),
    ("clock-1ms-early", "defaults", {}, ("early", 0.001), "the page's clock 1 ms early"),
    ("frame-dropped", "defaults", {}, ("drop", 19), "one frame (19) dropped"),
]
# The page's own card against the harness's, at these frames of a 60 fps run.
CARD_FRAMES = [0, 45, 120, 179]


def parse_size(text):
    w, h = text.lower().split("x")
    return int(w), int(h)


def run_bmtest(bmtest, args, env, stdin=None):
    done = subprocess.run([bmtest, *args], input=stdin, capture_output=True, check=False, env=env)
    if done.returncode != 0:
        sys.exit(f"bmtest {' '.join(args)} failed: {done.stderr.decode(errors='replace').strip()}")
    return done.stdout, done.stderr.decode(errors="replace")


def value_at(track, frame, ramp):
    """main.cpp's valueAt, in float as there."""
    f32 = np.float32
    if frame <= track[0][0]:
        return f32(track[0][1])
    if frame >= track[-1][0]:
        return f32(track[-1][1])
    for (fa, va), (fb, vb) in zip(track, track[1:]):
        if fa <= frame < fb:
            if not ramp:
                return f32(va)
            t = f32(frame - fa) / f32(fb - fa)
            return f32(va) + (f32(vb) - f32(va)) * t
    return f32(track[-1][1])


def cue_changes(cues, frames, kinds):
    """What bmtest's --pipe sets before each frame: a track from its first key on,
    or from frame 0 if it ramps (a STANDARD control). Every cue here is a step."""
    tracks = {}
    for frame, name, value in cues:
        tracks.setdefault(name, []).append((frame, float(value)))
    out = []
    for n in range(frames):
        changes = {}
        for name, track in tracks.items():
            track.sort(key=lambda kv: kv[0])
            ramp = kinds[name] == "standard"
            if n >= track[0][0] or ramp:
                changes[name] = float(value_at(track, n, ramp))
        out.append(changes)
    return out


# ---------------------------------------------------------------- the server
class Handler(http.server.SimpleHTTPRequestHandler):
    """demo/ as it is, plus /xc/ for the job, the frames and the page's answers."""

    def __init__(self, *args, state, **kwargs):
        self.state = state
        super().__init__(*args, directory=DEMO, **kwargs)

    def log_message(self, *args):
        pass

    def do_GET(self):
        if self.path == "/xc/job.json":
            return self.reply(json.dumps(self.state["job"]).encode(), "application/json")
        if self.path.startswith("/xc/frames/"):
            return self.reply(self.state["frames"][self.path[len("/xc/frames/"):]], "application/octet-stream")
        return super().do_GET()

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        if self.path.startswith("/xc/out/"):
            self.state["page"][self.path[len("/xc/out/"):]] = body
        elif self.path.startswith("/xc/log/"):
            self.state["log"][self.path[len("/xc/log/"):]] = json.loads(body)
        elif self.path == "/xc/table":
            self.state["table"] = json.loads(body)
        elif self.path == "/xc/missing":
            self.state["missing"] = json.loads(body)
        elif self.path == "/xc/error":
            self.state["error"] = body.decode(errors="replace")
            self.state["finished"].set()
        elif self.path == "/xc/done":
            self.state["finished"].set()
        self.reply(b"ok", "text/plain")

    def reply(self, body, kind):
        self.send_response(200)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


# ---------------------------------------------------------------- the judging
def compare(a, b, width, height):
    """Every frame of two runs: the share identical, within 1 and 4, the worst
    difference and the frame it is on, and the mean over colour."""
    x = np.frombuffer(a, np.uint8).reshape(-1, height, width, 4).astype(np.int16)
    y = np.frombuffer(b, np.uint8).reshape(-1, height, width, 4).astype(np.int16)
    worst = np.abs(x - y).max(axis=3)
    per_frame = worst.reshape(worst.shape[0], -1).max(axis=1)
    return {
        "exact": float((worst == 0).mean() * 100.0),
        "within1": float((worst <= 1).mean() * 100.0),
        "within4": float((worst <= 4).mean() * 100.0),
        "max": int(worst.max()),
        "worst_frame": int(per_frame.argmax()),
        "frames_off": int((per_frame > 0).sum()),
        "pixels_off": int((worst > 0).sum()),
        "mean": float(np.abs(x - y)[..., :3].mean()),
    }


def passes(s, args):
    return s["max"] <= args.max and s["exact"] >= args.exact


LIST_ROW = re.compile(r"^(\d+)\s+(.+?)\s{2,}(standard|option|event|text|boolean)\s+(\S+)\s*$")


def compare_table(bmtest, page_table, env):
    """`bmtest --list` against the page's table: name, kind, default (4 dp), in
    order, up to the About block; then every option element the page names,
    handed to bmtest by name, which refuses one it does not know."""
    rows = []
    for line in run_bmtest(bmtest, ["--list"], env)[0].decode().splitlines()[1:]:
        m = LIST_ROW.match(line.rstrip())
        if m is None:
            continue
        index, name, kind, value = m.groups()
        if name == "About":
            break
        rows.append((int(index), name, kind, float(value)))
    problems = []
    if len(rows) != len(page_table):
        problems.append(f"bmtest lists {len(rows)} controls before the About block, the page {len(page_table)}")
    for (index, name, kind, value), mine in zip(rows, page_table):
        if (name, kind) != (mine["name"], mine["kind"]) or abs(value - mine["value"]) > 0.00005:
            problems.append(f"#{index}: bmtest says {name} {kind} {value:.4f}, the page "
                            f"{mine['name']} {mine['kind']} {mine['value']:.4f}")
    elements = 0
    for mine in page_table:
        for e in mine["elements"]:
            done = subprocess.run([bmtest, "--film", "1", "--size", "16x9", "--set", f"{mine['name']}={e}"],
                                  capture_output=True, check=False, env=env)
            elements += 1
            if done.returncode != 0:
                problems.append(f"bmtest refuses {mine['name']}={e}: {done.stderr.decode(errors='replace').strip()}")
    return len(rows), elements, problems


def main(argv):
    parser = argparse.ArgumentParser()
    default_bmtest = os.path.join(REPO, "build-verify", "bmtest")
    if not os.access(default_bmtest, os.X_OK):
        default_bmtest = os.path.join(REPO, "build", "bmtest")
    parser.add_argument("--bmtest", default=default_bmtest)
    parser.add_argument("--size", default="320x180", help="the raster of every case that does not name one")
    parser.add_argument("--keep", help="write each case's worst frame, both pictures, here as PNGs")
    parser.add_argument("--max", type=int, default=1,
                        help="the largest difference, in levels of 255, allowed anywhere (default 1)")
    parser.add_argument("--exact", type=float, default=99.0,
                        help="share of pixels, %%, that must be identical (default 99)")
    parser.add_argument("--only", help="run only the cases whose names contain this")
    parser.add_argument("--input", choices=("rgba8", "float"), default="rgba8",
                        help="how the page holds the clip: rgba8, as every clip on the page is (default), or "
                             "float, RGBA32F holding the same codes / 255, as bmtest --pipe holds it")
    parser.add_argument("--chrome", default=os.environ.get(
        "CHROME", "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"))
    parser.add_argument("--port", type=int, default=0, help="the local server's port (default: any free one)")
    args = parser.parse_args(argv)
    if not os.access(args.bmtest, os.X_OK):
        sys.exit(f"no bmtest at {args.bmtest}: build it (tools/verify.sh, or cmake --build build --target bmtest)")

    work = tempfile.mkdtemp(prefix="bolometer-xc-")
    # The harness writes the plugin's log; keep it out of ~/Library/Logs.
    env = {**os.environ, "BOLOMETER_LOG_DIR": os.path.join(work, "log")}
    kinds = {"Frame Rate": "option", "Shutter": "event", "Palette": "option", "Span": "option"}

    cases = [c for c in CASES if not args.only or args.only in c[0]]
    negatives = [n for n in NEGATIVES if any(c[0] == n[1] for c in cases)]

    # The harness's card, frame by frame at each host rate and raster: Mix 0
    # hands the clip through to the bit.
    need = {}
    for name, _, _, fps, frames, size in cases:
        key = (size or args.size, fps)
        need[key] = max(need.get(key, 0), frames)
    if any(c[0] == "defaults" for c in cases):
        need[(args.size, 60)] = max(need.get((args.size, 60), 0), max(CARD_FRAMES) + 1)
    frames_by_key = {}
    for (size, fps), count in need.items():
        out, _ = run_bmtest(args.bmtest, ["--film", str(count), "--fps", str(fps), "--size", size, "--set", "Mix=0"], env)
        frames_by_key[f"{size}@{fps}"] = out

    plugin, plugin_log, job_cases = {}, {}, []
    for name, settings, cues, fps, frames, size in cases:
        size = size or args.size
        width, height = parse_size(size)
        key = f"{size}@{fps}"
        cards = frames_by_key[key][: width * height * 4 * frames]
        sets = [x for k, v in settings.items() for x in ("--set", f"{k}={v}")]
        script = []
        if cues:
            path = os.path.join(work, f"{name}.cues")
            with open(path, "w") as handle:
                handle.write("".join(f"{f} {n} {v}\n" for f, n, v in cues))
            script = ["--script", path]
        out, err = run_bmtest(args.bmtest, ["--pipe", "--fps", str(fps), "--size", size, *sets, *script], env, stdin=cards)
        if len(out) != len(cards):
            sys.exit(f"bmtest --pipe for {name} wrote {len(out)} bytes for {len(cards)} in")
        plugin[name] = out
        m = re.search(r"(\d+) sensor frames read out, (\d+) FFCs", err)
        plugin_log[name] = (int(m.group(1)), int(m.group(2))) if m else None
        job_cases.append({"name": name, "settings": settings, "width": width, "height": height, "cards": key,
                          "indices": list(range(frames)), "times": [n / fps for n in range(frames)],
                          "changes": cue_changes(cues, frames, kinds), "fps": fps})
    by_name = {c["name"]: c for c in job_cases}
    for name, base, change, how, _ in negatives:
        c = {**by_name[base], "name": name, "settings": {**by_name[base]["settings"], **change}}
        if how and how[0] == "early":
            c["times"] = [t - how[1] if n else t for n, t in enumerate(c["times"])]
        if how and how[0] == "drop":
            keep = [n for n in c["indices"] if n != how[1]]
            c["indices"] = keep
            c["times"] = [by_name[base]["times"][n] for n in keep]
            c["changes"] = [by_name[base]["changes"][n] for n in keep]
        job_cases.append(c)

    cw, ch = parse_size(args.size)
    card_jobs = [{"name": f"{n}", "width": cw, "height": ch, "time": n / 60} for n in CARD_FRAMES] \
        if any(c[0] == "defaults" for c in cases) else []
    job = {"cases": job_cases, "cards": card_jobs, "input": args.input}

    state = {"job": job, "frames": frames_by_key, "page": {}, "log": {}, "table": None, "missing": None,
             "error": None, "finished": threading.Event()}
    server = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), functools.partial(Handler, state=state))
    port = server.server_address[1]
    threading.Thread(target=server.serve_forever, daemon=True).start()
    profile = tempfile.mkdtemp(prefix="bolometer-chrome-")
    chrome = subprocess.Popen([
        args.chrome, "--headless=new", "--no-first-run", "--no-default-browser-check",
        f"--user-data-dir={profile}", "--use-angle=metal", "--enable-gpu", "--ignore-gpu-blocklist",
        "--window-size=1600,1200", f"http://127.0.0.1:{port}/tools/crosscheck/",
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        if not state["finished"].wait(1200):
            sys.exit("the page never finished (1200 s)")
    finally:
        chrome.terminate()
        try:
            chrome.wait(10)
        except subprocess.TimeoutExpired:
            chrome.kill()
        server.shutdown()
        shutil.rmtree(profile, ignore_errors=True)
    if state["error"]:
        shutil.rmtree(work, ignore_errors=True)
        sys.exit(f"the page failed:\n{state['error']}")

    failures = 0
    count, elements, table_problems = compare_table(args.bmtest, state["table"] or [], env)
    if table_problems:
        failures += len(table_problems)
        print("the page's controls differ from bmtest's:")
        for line in table_problems:
            print(f"  {line}")
    else:
        print(f"controls: all {count} of bmtest --list's before the About block, same names, kinds and defaults, "
              f"in order; all {elements} option elements accepted by bmtest by name")
    gaps = state["missing"] or {}
    unset = [u for u in gaps.get("unset", []) if not u.startswith("resample: ")]
    resample_unset = [u for u in gaps.get("unset", []) if u.startswith("resample: ")]
    for label, items in (("uniforms the page set that a program lacks", gaps.get("missing")),
                         ("uniforms a program has that the page never set", unset),
                         ("GL errors", gaps.get("errors")), ("frames the instance refused", gaps.get("refused"))):
        if items:
            failures += 1
            print(f"{label}: {', '.join(sorted(set(items))[:20])}")
    if not (gaps.get("missing") or unset or gaps.get("errors") or gaps.get("refused")):
        print("every uniform set, none missing, no GL error, no frame refused"
              + (f" (the resample pass, which --pipe cannot reach, never ran: {', '.join(resample_unset)})"
                 if resample_unset else ""))

    if args.keep:
        from PIL import Image
        os.makedirs(args.keep, exist_ok=True)

    def camera_agrees(name, base):
        log = state["log"].get(name) or []
        reads = sum(r[0] for r in log)
        ffcs = log[-1][2] if log else -1
        return (reads, ffcs) == plugin_log[base], reads, ffcs

    print(f"\nevery frame of each run; == identical, <=1 within one level of 255, max the worst anywhere\n")
    print(f"{'case':<22} {'raster':>9} {'fps':>4} {'frames':>6} {'==':>9} {'<=1':>9} {'max':>4} {'mean':>7} "
          f"{'readouts':>9} {'FFCs':>5}")
    for name, _, _, fps, frames, size in cases:
        size = size or args.size
        width, height = parse_size(size)
        got = state["page"].get(name)
        if got is None or len(got) != len(plugin[name]):
            print(f"{name:<22} no picture from the page")
            failures += 1
            continue
        s = compare(plugin[name], got, width, height)
        same, reads, ffcs = camera_agrees(name, name)
        ok = passes(s, args) and same
        failures += 0 if ok else 1
        expected = plugin_log[name]
        print(f"{name:<22} {size:>9} {fps:>4} {frames:>6} {s['exact']:8.4f}% {s['within1']:8.4f}% {s['max']:>4} "
              f"{s['mean']:7.4f} {reads:>4}/{expected[0]:<4} {ffcs:>2}/{expected[1]:<2} {'ok' if ok else 'FAIL'}"
              + (f"  ({s['pixels_off']} pixels on {s['frames_off']} frames not identical, the worst on frame "
                 f"{s['worst_frame']})" if s["max"] else ""))
        if args.keep and s["max"]:
            n = s["worst_frame"]
            one = width * height * 4
            for label, data in (("plugin", plugin[name]), ("page", got)):
                Image.frombytes("RGBA", (width, height), data[n * one:(n + 1) * one]).save(
                    os.path.join(args.keep, f"{name}-{n}-{label}.png"))

    if state["job"]["cards"]:
        print(f"\nthe page's own card (plugin.js, float) against the harness's (double), within {args.max} level(s)")
        one = cw * ch * 4
        for k in state["job"]["cards"]:
            n = int(k["name"])
            mine = state["page"].get(f"card-{k['name']}")
            theirs = frames_by_key[f"{args.size}@60"][n * one:(n + 1) * one]
            s = compare(theirs, mine, cw, ch) if mine else None
            # A GLSL port in float of a picture the harness makes in double and
            # rounds with lround: within --max levels is the test, not identity.
            ok = s is not None and s["max"] <= args.max
            failures += 0 if ok else 1
            print(f"  t = {k['time']:6.3f} s  " + (f"{s['exact']:8.4f}% {s['within1']:8.4f}% {s['max']:>4} "
                                                     f"{s['mean']:7.4f}  {'ok' if ok else 'FAIL'}" if s else "no picture"))

    print("\nnegative controls: the page a little wrong, against the plugin as it should be")
    for name, base, _, how, label in negatives:
        got = state["page"].get(name)
        size = next((c[5] for c in cases if c[0] == base), None) or args.size
        width, height = parse_size(size)
        want = plugin[base]
        if how and how[0] == "drop":
            one = width * height * 4
            want = b"".join(want[n * one:(n + 1) * one] for n in range(len(want) // one) if n != how[1])
        s = compare(want, got, width, height) if got else None
        same = camera_agrees(name, base)[0] if not (how and how[0] == "drop") else True
        caught = s is not None and not (passes(s, args) and same)
        failures += 0 if caught else 1
        print(f"  {label:<36} " + (f"{s['exact']:8.4f}% {s['within1']:8.4f}% {s['max']:>4} {s['mean']:7.4f}  "
                                     f"{'caught' if caught else 'NOT CAUGHT'}" if s else "no picture"))
    print(f"\n{'every case passes, every negative control is caught' if not failures else f'{failures} problem(s)'}"
          f" (pass: no pixel more than {args.max} level(s) apart, {args.exact}% identical, the same readouts and FFCs)")
    shutil.rmtree(work, ignore_errors=True)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
