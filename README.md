# bolometer

> **AI-assisted project.** This codebase was created with [Claude](https://claude.com/claude-code)
> (Anthropic), directed and reviewed by a human author. In **Resolume Arena 7.29.0 on a Mac**
> it loads with every control as declared and, over a still test card, drifts on Arena's clock,
> takes a Shutter press and changes the picture with every control a still can show. On
> **Windows** its release build loads, registers and renders in Arena 7.27.1 on software
> rendering, all 10 valued controls live. Everything else is established by an offline
> harness, `bmtest`, that drives
> the real plugin class through the same `ProcessOpenGL` a host calls, in a headless GL
> context on a synthetic clock. It runs on this Mac's GPU at two rasters, and again on Apple's
> software renderer. The camera's bolometers follow an RK4 integration of their own lag to
> **1.5e-12 K**, and τ read back out of the picture's state is **12.00000 ms** at 24, 30, 60 and
> 144 fps. Over 10 minutes at nine host rates and three sensor rates, **1,954,476** host frames
> read out exactly the sensor instants integer arithmetic gives, with **0** disagreeing. A host
> clock at 4.99e8 ms changes the picture on the same frames as one from 0, to **0** codes on the
> GPU. **37** deliberately wrong models and **27** one-character mutants of the shipped code are
> all caught. How luma becomes temperature, the drift's growth and the narcissus spot's size are
> **chosen**, not measured: see [Status](#status).

A thermal camera on an uncooled microbolometer core, looking at your clip, for
[Resolume](https://resolume.com) Arena and Avenue: an FFGL effect, **SW Bolometer**.

![A night skyline under a sky shading from violet at the top to orange at the horizon, in the
orange-yellow-white of an ironbow thermal palette. A large yellow sun sits behind the
buildings and a white ball crosses in front of them. A white sign hangs in the sky with a dark
halo around it, the buildings' edges glow, and fine vertical stripes run through the whole
picture](docs/hero.png)

<sub>One frame rendered by `bmtest`, not captured from Resolume: the defaults over the
harness's card, 29 s in (`--frames 1740`), a second before the first shutter. The stripes are
29 s of drift; the next FFC wipes them.</sub>

Every pixel is a tiny thermal mass that warms and cools towards the scene with a 12 ms time
constant. **It is read out at the export-limited 9 Hz, corrected by a calibration that a
shutter refreshes every so often, and a histogram decides which colour each temperature
gets.** Nothing is drawn on top. What you see follows from those four things:

- **the stutter**: the picture changes only at sensor instants, exactly 1/9 s apart in host
  time (at 60 fps, a 6-7-7 rhythm of host frames);
- **the freeze**: every Shutter Interval the flat-field correction holds the last frame for
  23/26 s, then the picture comes back with its drift wiped;
- **the contrast that jumps** when something hot enters, as plateau equalisation hands it its
  share of the scale and the background slides darker, damped over a few sensor frames;
- **the halos**: hot things ringed dark and cold things ringed light, the detail enhancement's
  unsharp mask;
- **grain and column stripes** that grow between shutters and vanish at each one;
- **a faint dark spot dead centre**, narcissus: the detector seeing itself in the lens, which
  the shutter cannot remove because it sits behind the lens.

Temperature comes from the clip's encoded luma and alpha, T = 15 °C + 25 K × luma × alpha.
**Luma is not temperature**: that mapping is a choice, and a transparent pixel reads as cold
as black.

> **Photosensitivity note.** The automatic gain re-ranges the palette whenever the clip's
> brightness changes, so on a dark clip with small flickering lights it can turn that flicker
> into flashing over a large part of the picture. Over eight of Resolume's demo clips the
> defaults stayed at or under three flashes a second, the line broadcast and web guidance
> draws; at Frame Rate 60 Hz, or in Rainbow, two dark flickering clips went over it (4.5 a
> second in a third of the picture). The shutter is a freeze, not a flash. The
> [user guide](https://stoatworks-labs.com/software/bolometer/guide/#flashing-and-keeping-it-safe)
> has the table and how to keep a show within the guidance.

<!-- downloads:start -->

## Download

**[v0.1.0](https://github.com/stoatworks-labs/bolometer/releases/tag/v0.1.0)** — prebuilt for macOS and Windows. Pick your platform:

<details>
<summary><b>macOS</b> — Universal (Apple Silicon + Intel)</summary>

| Build | Download | Size |
| --- | --- | --- |
| Universal (Apple Silicon + Intel) · .dmg disk image | [`bolometer-0.1.0-macos-universal.dmg`](https://github.com/stoatworks-labs/bolometer/releases/download/v0.1.0/bolometer-0.1.0-macos-universal.dmg) | 228 KB |
| Universal (Apple Silicon + Intel) · .zip archive | [`bolometer-macos-universal.zip`](https://github.com/stoatworks-labs/bolometer/releases/latest/download/bolometer-macos-universal.zip) | 188 KB |

</details>

<details>
<summary><b>Windows</b> — x64</summary>

| Build | Download | Size |
| --- | --- | --- |
| x64 · .exe installer | [`bolometer-0.1.0-windows-x86_64-setup.exe`](https://github.com/stoatworks-labs/bolometer/releases/download/v0.1.0/bolometer-0.1.0-windows-x86_64-setup.exe) | 249 KB |
| x64 · .zip archive | [`bolometer-windows-x86_64.zip`](https://github.com/stoatworks-labs/bolometer/releases/latest/download/bolometer-windows-x86_64.zip) | 119 KB |

</details>

All builds, checksums and release notes: [github.com/stoatworks-labs/bolometer/releases](https://github.com/stoatworks-labs/bolometer/releases).

macOS builds are signed and notarised and open normally. The Windows builds are unsigned, so SmartScreen warns once.

<!-- downloads:end -->

## Controls

| Group | |
| --- | --- |
| **Sensor** | Frame Rate (9, 30 or 60 Hz), Shutter Interval (5–300 s, Off at the top), Shutter (an FFC now), Drift (0–1 K a minute), Narcissus (0–2 K). |
| **Span** | Span (Auto: plateau equalisation; Manual), Plateau (10⁻⁵–1 of the samples), Level (15–40 °C) and Width (0.5–30 K), the last two in Manual only. |
| **Look** | Palette (White Hot, Black Hot, Ironbow, Rainbow), Halo (0–3), Mix. |

The defaults are 9 Hz, a shutter every 30 s, Drift 0.1 K a minute, Narcissus 0.1 K, Auto
with a 2% plateau, Ironbow, Halo 1 and Mix 1. Level 27.5 °C and Width 25 K span the whole
mapped range in Manual. At Mix 1 the camera's picture is opaque, as a camera's is; Mix 0
hands the clip through untouched.

## Status

**v0.1.0, released 18 November 2026, and early days.** It has been in Resolume Arena on
both platforms, over a still test card, and nowhere else: no show, no real footage, no real
GPU on Windows, nothing timed inside Resolume, and never beside a real thermal camera.

### In Resolume

- **Windows, Arena 7.27.1** (win-lab, software rendering: Mesa llvmpipe, no GPU, no sound
  device), the fleet's Arena gate on the DLL `release.yml` built from the registered tree:
  it loads and registers (`SW Bolometer` / `BO01` / effect), all 18 host controls (the 17
  declared plus Arena's Opacity) match their names, order, types, ranges and defaults, it
  renders (45 levels from its bare input at the defaults), and Arena's log stays clean: 8
  passed, 0 failed, 2 skipped. All 10 valued controls changed Arena's picture: Drift with
  Shutter Interval Off, Level and Width in Span Manual. Frame Rate, Shutter Interval and
  Shutter are recorded inert: on a still carrier they change only when the picture is
  refreshed, not what a grab shows. Software rendering says nothing about a GPU or speed.
- **macOS, Arena 7.29.0** on Apple Silicon (macOS 26), the same run's universal bundle, over
  REST with one layer added on top of an open show and a still of the harness's card on it:
  `SW Bolometer` is in the effect list with all 17 controls, their defaults and the option
  names as declared, and the About line reads `Bolometer v0.1.0`. From Arena's composition
  snapshot (mean difference in 8-bit levels over the middle three-fifths): the effect at its
  defaults 20.5 from the bare clip; with Drift 0 and the shutter Off, 0.06 over 3 s (still);
  Drift at 1 K a minute 12.7 after 6 s and 5.7 after 6 more, so the drift grows on Arena's
  clock; a Shutter pressed over REST (true, then false) 16.6, and after it the picture is the
  drift-free still's to 0.10, so the FFC wiped the drift; Narcissus 6.4, Halo 9.0, Plateau
  27.0, White Hot to Ironbow 10.5, Black Hot to Rainbow 162.7, Span 26.9, Level 91.0, Width
  10.2, Mix 21.9; Frame Rate 0.0, as a still should give; Mix 0 against the bare clip 0.0.
  The layer was removed, Arena quit without saving and the show file untouched.

### Measured offline, on macOS

`tools/verify.sh` passes on this machine (Apple Silicon, macOS 26) against a fresh universal
Release build (16 November 2026). The picture checks run at **two rasters**, 1280×720 and
320×180, and every one again on **Apple's software renderer** at 320×180. Two checks need no
GL at all. The first figure is this GPU at 1280×720; the software renderer's figure is given
where it differs.

| check | result |
| --- | --- |
| `--camera` | 6.5 s of a scene stepping 16 K every 5 frames, through one FFC, at 24, 60 and 144 fps: the camera's bolometer within **1.5e-12 K** of an RK4 integration of dT/dt = (target − T)/τ; **50** readouts, the exact count, within **3.9e-13 K** of RK4 at their own instants |
| `--instants` | 10 minutes at 9, 30 and 60 Hz under 24, 25, 30, 50, 60, 120, 144, 29.97 and 59.94 fps, from 0 s and from 4.99e8 ms: **1,954,476** host frames and **753,390** readouts, **0** disagreeing with integer arithmetic for the instants and the FFC schedule |
| `--thermal` | grey 0.8, red, grey 0.8 at alpha 0.5 and a colour at alpha 0 settle at **35.00000, 20.31500, 25.00000, 15.00000 °C**, worst pixel 5.3e-7 K off (1.4e-6 K software); τ read out of the state after a 15 K step **11.99998–12.00000 ms** at 24, 30, 60 and 144 fps, worst 5.7e-5 of τ |
| `--clock` | 9, 30 and 60 Hz under 24, 30, 60 and 144 fps: **0** host frames whose change disagrees with ⌊n f / fps⌋, from 0 and from 4.99e8 ms; each readout's instant recovered from the lag to **3.2e-8 s** (a host frame is up to 0.042 s away); the two clocks' pictures **0** codes apart (up to 1 on the software renderer, 2 of 12 cases); the clock's unit settles by frame 60 rendered faster than real time |
| `--shutter` | an FFC freezes instants **45–52** (8) at 9 Hz, **150–176** (27) at 30 Hz and **300–353** (54) at 60 Hz, under 24, 60 and 144 fps, and a press at frame 40 freezes 6–13: **0** frames whose change disagrees; the bolometers within **3.1e-6 K** of the shutter's window (4.1e-6 K software) |
| `--agc` | the GPU's histogram equals the card's on the 256 × 144 grid, **0** of 36,864 counts off; the levels within **0.383** codes of plateau equalisation at 7% and **0.500** at 100%; the transfer closes on a new scene by **0.85^m** over m sensor frames at 24 and 144 fps, worst 3.9e-5 off |
| `--manual` | a ramp from 16.1 to 38.9 °C under three Level/Width pairs: every level within **0.499** codes |
| `--halo` | an edge at Halo 2, both axes: net overshoot **0.0000**; one-sided sums 1.7216 against **1.7316**, first moment 15.42 against **15.64**, so σ reads 7.145 px for 7.197 (at 320×180, 1.802 px for 1.800) |
| `--narcissus` | 2 K deep: **0** K off inside and outside, the profile within **1.9e-6 K** of a chord-by-chord sum of the two discs' overlap (7.2e-5 K software), centred to **0.0000 px**, the same after an FFC |
| `--drift` | at 1 K a minute the RMS grows linearly with age (1, 2, 4 and 4.9 s agree to **5.4e-8**), **0.18%** from the rate (0.81% at 320×180); the column means hold **0.502** of the variance (want 0.501); an FFC starts a new pattern (correlation −0.005 with the old) and 700 s without one reads as 600 s |
| `--palette` | 26 levels at anchors typed from LeptonModule's tables, across the four palettes: **0** wrong |
| `--alpha` | alpha = a + (1 − a) Mix to **4.5e-8**, colour to **1.2e-7**; a transparent pixel reads **15 °C** exactly; Mix 0 the clip **bit for bit**. Through `--pipe` on two of Resolume's clips with alpha (BattleWeapon_Tank_09, 60% transparent; Ethnik2, 50%): alpha **255** everywhere at Mix 1 and within **0.5** codes of a + (1 − a)/2 at Mix 0.5 |
| `--resize` | 1280×720 → 320×180 → 1920×1080 mid-run: the bolometers run on from where they were to **1.7e-6 K**, the held frame is the frame before (**0** values differ), and the AGC's transfer is a never-resized run's to **0** while still moving |
| `--glstate` | the host's GL state comes back, **0** things changed |
| `--pipe` | 2.5 frames in, exactly 2 out; bad cues and settings (`Palette=Nope`, `Drift=abc`, `Drift=nan`) refused; a closed stdout (`\| head -c 1`) exits 1, not 141 |
| `--negative` | **37** deliberately wrong models, **37** caught |
| mutation | **27** one-character mutants of the shipped GLSL and C++, **27** caught |
| `tools/sweep.py` | all **12** controls measurably change the picture |
| shaders | all 11 compile through `glslc`, not merely through Apple's driver |
| the bundle | universal (`x86_64 arm64`), exports `plugMain`, ad-hoc signs; `oxbow` reads `SW Bolometer` / `BO01` / `effect` and renders through it |

Render cost, from verify.sh's final run on 16 November 2026 with other builds sharing the
machine (240 timed frames at 60 fps). At the defaults a whole frame, from the plugin's call to
`glFinish`, takes **0.27 ms** median at 720p and **0.48 ms** at 1080p (worst 2.1 and 2.5 ms).
Most frames only hold the last picture (GPU **0.08** and **0.23 ms**); the one in six or seven
that reads out a sensor instant runs every pass (GPU **1.09** and **1.91 ms**). With every pass
on every frame (a 60 Hz sensor) and Halo 3: a whole frame **1.30 ms** at 720p and **2.18 ms** at
1080p median (worst 1.7 and 2.6 ms). macOS only; 4K was not measured.

### Not established

- **Resolume only over a still test card**, on two machines: not Avenue, not a show, not real
  footage, not a GPU on Windows. The 9 Hz stutter and the shutter's freeze have never been
  watched inside Resolume; the offline harness checks both against the host's clock.
- **Windows** is built by CI (`ci.yml`, MSVC with no warnings), and `tools/wincheck.ps1`
  loads the DLL as a host does and reads its name, id, type and parameters.
- **No thermal camera was compared.** Every figure is checked against the model as written in
  the spec, and the model against the datasheets' words, not against footage from a Lepton or
  a Boson.
- **Chosen, not measured:** the luma-to-temperature mapping and its 15–40 °C range, the drift's
  linear growth and its rate, the narcissus spot's size, the halo's σ, the damping's form, how
  the FFC's overhead splits around the shutter, and the shutter's 27.5 °C.
- **Left out:** temporal noise (NETD), the radiance nonlinearity, the optics' blur, the
  sensor's own low resolution (the picture keeps the host's raster), dead pixels, scene-based
  NUC and the rest of a real AGC.
- The software renderer was checked at 320×180 only, and `--resize` at its own three rasters.
- The defaults were chosen over seven of Resolume's bundled clips through `--pipe`, looked at
  as stills, not in a show.
- No presets and no OpenFX port. The user guide is
  [docs/USER-GUIDE.md](docs/USER-GUIDE.md), on the site at
  [stoatworks-labs.com/software/bolometer/guide](https://stoatworks-labs.com/software/bolometer/guide/).

## Browser demo

[bolometer-demo.stoatworks-labs.com](https://bolometer-demo.stoatworks-labs.com/) runs the
plugin's own GLSL in WebGL2, unedited, fed by a JavaScript port of its CPU half (the camera's
clock, the shutter's schedule, the lag's weights, the drift, the AGC's damping and the pass
orchestration) that only a reader checks, over the harness's card, a generated clip or one you
pick. Run frame by frame against `bmtest` from the same frames, 18 of 21 cases came out
identical to the bit; in the other three a few values rounded to the neighbouring 8-bit code
(`demo/tools/crosscheck/`; the figures are in AGENTS.md). It opens on the plugin's defaults, with a photosensitivity note at
the top. It is a demo, not the plugin, and the page lists what it leaves out.

## Build

macOS (universal: Apple Silicon and Intel):

```bash
git clone --recursive <this repo>
```

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build
```

```bash
cmake --install build
```

The last one copies `Bolometer.bundle` into `~/Documents/Resolume Arena/Extra Effects`.
Restart Arena and it's under Effects as **SW Bolometer**.

## Building and testing

```bash
OXBOW=/path/to/oxbow tools/verify.sh
```

That's the whole lot: shader hygiene and `glslc`, a fresh universal build with no warnings,
the bundle's architecture, plist and ad-hoc signature, a host's view of it through `oxbow`
(required), every check at two rasters and on the software renderer, the pipe, the output
alpha on Resolume's clips, the negative controls, the mutants, the sweep for dead controls and
the render cost. `CLAUDE.md` lists the checks one by one; `./build/bmtest --all` runs them all.

Render a frame of your own:

```bash
./build/bmtest --out /tmp/bolometer.png --frames 600 --set "Palette=White Hot" --set "Halo=0.5"
```

Run a clip through it:

```bash
ffmpeg -nostdin -i in.mov -vf scale=1280:720 -f rawvideo -pix_fmt rgba - | ./build/bmtest --pipe --fps 30 --size 1280x720 | ffmpeg -f rawvideo -pix_fmt rgba -s 1280x720 -r 30 -i - out.mov
```

## License

MIT. See [LICENSE](LICENSE) and [ATTRIBUTIONS.md](ATTRIBUTIONS.md).

<!-- attributions:start -->
This project is built on other people's work — see [ATTRIBUTIONS.md](ATTRIBUTIONS.md).
<!-- attributions:end -->
