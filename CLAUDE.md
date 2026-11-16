# bolometer

A thermal camera on an uncooled microbolometer core, looking at the clip, for Resolume
Arena/Avenue as one FFGL effect. Each pixel is a thermal mass with a 12 ms time constant,
read out at the export-limited 9 Hz (or 30/60 Hz), frozen for 23/26 s by each flat-field
correction, drifting between shutters, with a narcissus spot at the centre; plateau
equalisation and an unsharp mask make the picture, through one of four palettes. Temperature
is the clip's encoded luma × alpha, 15–40 °C. C++/GLSL, CMake MODULE → universal `.bundle`
(macOS). MIT.

| id | display name | bundle | type |
| --- | --- | --- | --- |
| `BO01` | `SW Bolometer` | `Bolometer.bundle`, `com.stoatworks.ffgl.bolometer` | effect |

Read `AGENTS.md` before touching the camera (`Camera::Advance`, `weightsAt`, the FFC
schedule), the clock, the AGC (histogram, LUT, damping), the halo, the narcissus, the drift,
the composite or the resize.

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release` (the harness is on by default:
  `-DBOLOMETER_BUILD_TOOLS=ON`)
- Fast dev build: add `-DCMAKE_OSX_ARCHITECTURES=arm64`
- Build: `cmake --build build -j8`
- Install to Resolume: `cmake --install build` (into Arena's Extra Effects).
  Never from a shared session on this Mac: nothing here has been loaded into Arena.
- Render offline: `./build/bmtest --out /tmp/b.png` (the defaults over the harness's card,
  360 frames at 60 fps); the summary line gives the sensor frames read out and the FFCs
- A clip of your own: `--clip clip.rgba` (raw RGBA at `--size`, top row first)
- List parameters: `./build/bmtest --list`
- Set anything by name: `./build/bmtest --set "Frame Rate=60 Hz" --set "Drift=0.5"`
  (a standard control takes the host's 0..1; an option takes its name)
- Press something later: `./build/bmtest --out /tmp/b.png --at 100 "Shutter=1"`
- Film: `./build/bmtest --film 600 --size 1280x720 --script cues | ffmpeg -f rawvideo -pix_fmt rgba -s 1280x720 -r 60 -i - out.mp4`
- A clip through it: `ffmpeg -nostdin -i in.mov -vf scale=W:H -f rawvideo -pix_fmt rgba - | ./build/bmtest --pipe --fps 30 --size WxH | ffmpeg -f rawvideo -pix_fmt rgba -s WxH -r 30 -i - out.mp4`

## Verify
- Everything: `OXBOW=/path/to/oxbow tools/verify.sh` (GLSL reserved words, `glslc`, the
  submodule pin, a fresh universal build with no warnings, lipo, plist, ad-hoc signature,
  `oxbow probe` and `selftest` (required: no oxbow is a failure, exit 1); every check at
  1280x720 and 320x180; every check again on Apple's software renderer at 320x180;
  `--offline`; `--pipe`; the output alpha on Resolume's two clips with alpha (required;
  `ALPHA_CLIPS=folder` overrides `/Applications/Resolume Arena/media/Shop74`); the negative
  controls; 27 mutants; the sweep; the bench). `KEEP_BUILD=1` reuses `build-verify/`.
- Every check at once: `./build/bmtest --all`. One at a time (`--size WxH`: that raster alone):
  - **No GL**: `--camera` (the camera's weights against RK4 of the lag, through an FFC, at
    24/60/144 fps; the readouts at their instants), `--instants` (10 minutes at nine host rates
    and three sensor rates, from 0 and from 4.99e8 ms: every readout and FFC against integer
    arithmetic).
  - **The bolometers and the clock**: `--thermal` (the settled temperature from luma × alpha;
    τ = 12 ms from the state at 24/30/60/144 fps), `--clock` (the frames the picture changes on,
    the readout's instant, a clock at 4.99e8 ms, the unit vote offline), `--shutter` (the
    frozen instants, a press, the bolometers under the shutter).
  - **The picture**: `--agc` (the GPU's histogram against the card's count, plateau
    equalisation's levels, the damping per sensor frame), `--manual`, `--halo` (an edge's
    overshoot sums against the kernel's), `--narcissus` (the profile against a chord sum),
    `--drift` (linear in age, half in the columns, reset with a new pattern, capped),
    `--palette`, `--alpha`.
  - **The rest**: `--resize` (mid-run: the state, the held frame and the AGC carry over),
    `--glstate`.
- The checks can fail: `./build/bmtest --negative` (37 wrong models), `tools/mutate.sh`
  (27 mutants, arm64, 320x180, perl alarm at 300 s; `ONLY=n` runs one, `DRY=1` only checks
  every target is there once).
- No dead controls: `python3 tools/sweep.py` (`--bare` drops the per-control contexts: 3
  dead by design, AGENTS.md says which).
- Render cost: `./build/bmtest --bench` (720p and 1080p; GPU time and the whole frame).
- `BMTEST_RENDERER=software` runs any of it on Apple's software renderer.
- Shared Mac: arm64 builds while iterating, no 4K loops, never kill a process you didn't start.

## Notes
- **All time is on the CPU in double** (`Camera`): the sensor instants k/f from an epoch, the
  FFC schedule, the drift's age. The shaders see only weights, d^m, the drift's amplitude and
  seed, and flags. Nothing counts host frames.
- A frame with a new sensor instant runs readout → state → histogram → LUT → display → blur ×2
  → shown → composite; a frame without one runs state → composite and shows the held frame.
- The histogram is GL_POINTS from a fixed 256 × 144 grid, additive into 512 × 1 R32F: the same
  36,864 samples at every raster.
- All host parameters are 0..1 and mapped in `Controls.cpp`; options hold the element index;
  Shutter acts on the rising edge.
- GLSL reserved words (`patch sample input output filter common active half layout flat
  packed`, `noise1`–`noise4`) must not be identifiers; `verify.sh` greps for them.
- Override `SetTextParameter` to return `FF_SUCCESS` for the About block, or no host can
  instantiate the plugin.
- `bolometer_core` is an OBJECT library: the plugin registers itself from a file-scope
  constructor nothing references.
- The macOS build must be universal. Check with `lipo`, never the build log.
- **Provisional hand copies:** `source/StoatworksAbout*.h` (guide = "", no user guide) and
  `ATTRIBUTIONS.md`. Once the project is registered in stoatworks-backend, `sync-about.py`
  and `sync-attributions.py` generate them; edit them there then.
- No `.github/workflows/`, on purpose. GitHub Actions only on a public repo.
- Windows has never been built; the CMake has the branches from filings, untested.
- Local only: `~/dev/bolometer`, no remote, nothing released, not on the website, never
  loaded into Resolume. The spec is `~/Projects/resolume/specs/SPEC-bolometer.md`.

## Diagnostics

`source/Diag.{h,cpp}`: log file only, no crash handler (this runs inside Resolume).

    ~/Library/Logs/bolometer/bolometer.YYYY-MM-DD.log
    %LOCALAPPDATA%\bolometer\logs\bolometer.YYYY-MM-DD.log

`BOLOMETER_LOG_DIR` overrides the folder.
