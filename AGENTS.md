# AGENTS.md: Bolometer

Read this before changing the camera (`Camera::Advance`, `weightsAt`, the FFC schedule), the
clock, the AGC (the histogram, the LUT and its damping), the halo, the narcissus, the drift,
the composite or the resize. `CLAUDE.md` has the commands; the spec is
`~/Projects/resolume/specs/SPEC-bolometer.md`, and it is kept in step with this repo (no
As-built section: where the build departed from the first draft, the spec was changed, and
[Where the build departed from the spec](#where-the-build-departed-from-the-spec) says how).

## The idea

**Every pixel is a tiny thermal mass with a 12 ms time constant. It is read out at the
export-limited 9 Hz, corrected by a calibration a shutter refreshes, and plateau equalisation
decides which colour each temperature gets.** Nothing is drawn: the stutter, the freeze, the
contrast that jumps, the halos, the stripes and the dark centre all follow from the camera's
mechanism, each from a datasheet's words (the spec quotes them).

## Decisions taken without asking (the brief says decide and write it down)

- **Identity.** `SW Bolometer`, `BO01` (checked free across `~/Projects` and `~/dev`), an
  effect, `com.stoatworks.ffgl.bolometer`, harness `bmtest`. One class, `BolometerPlugin`;
  host indices are the ParamIds, the About block last.
- **All time is on the CPU in double.** `Camera` holds the epoch, the sensor instants, the FFC
  schedule and the drift's age, and turns them into weights for the frame: the old state's,
  the clip's and the shutter's, each the exact integral of a first-order lag over a piecewise
  constant target. No time reaches a shader; the shaders see weights in [0, 1], d^m, the
  drift's amplitude and seed, and flags.
- **Two thermal evaluations a frame.** The readout is the bolometers at the latest sensor
  instant t_k (from the state at the last host frame, before it moves); the state is the
  bolometers at the host frame t_n. A frame with no new instant runs only the state pass and
  the composite.
- **The instants are k/f from an epoch**, k = ⌊(t − e) f + 10⁻⁶⌋: an instant that coincides
  with a host frame counts as at or before it. A Frame Rate change starts a new grid at the
  last host frame (whose readout is already shown); an FFC in progress keeps its time window.
- **The FFC's instants come from integers too.** The first starts at the first instant at
  least Shutter Interval after the epoch, each next one at the first instant at least Shutter
  Interval after the last one ended; a press starts one at the next instant. The freeze is the
  instants in [c, c + 23/26 s): ⌈23 f/26⌉ of them, 8, 27 and 54 at 9, 30 and 60 Hz.
- **The AGC's histogram is GL_POINTS into a 512 × 1 R32F buffer with additive blending**, one
  point per sample of a fixed 256 × 144 grid, so it counts the same 36,864 samples at every
  raster. R32F additive blending was the risk; it is exact on this GPU and on Apple's software
  renderer (`--agc` compares every bin with the card's own count). The histogram and LUT run on
  every readout frame in Manual too, so switching to Auto is warm.
- **The LUT holds each bin's ends** (C_{k−1}/N_T, C_k/N_T) in an RGBA32F row, damped as a
  whole by d^m toward the new target, m the sensor frames read out since the last update. A
  temperature at fraction u of bin k shows at the ends' mix.
- **The halo is an unsharp mask on the span's output** (Auto or Manual), before the 8-bit
  quantisation: two separable passes of explicit Gaussian weights, σ = 0.01 H, cut at ⌈4σ⌉ px
  and renormalised, the edge clamped (past the edge is more of the edge).
- **The narcissus and the drift are added to the readout**, in host pixels: the spot is centred
  on the frame, R = 0.15 H and b = 0.1 H; the drift's pattern is √½ (column variate + pixel
  variate), PCG-hashed with a seed that changes at each FFC's end.
- **The composite assumes straight alpha**: alpha = a + (1 − a) Mix, colour =
  (clip × a × (1 − Mix) + camera × Mix) / alpha. At Mix 1 the camera's picture is opaque, as a
  camera's is; a transparent pixel of the clip is a cold one (15 °C). Mix 0 is a branch that
  hands the clip through. Looked at on two of Resolume's clips with real alpha
  (BattleWeapon_Tank_09 and Ethnik2), checked in verify.sh.
- **Resize** resamples the thermal state and the shown frame bilinearly by hand (an explicit
  shader, not the driver's filtering) and never clears them. The histogram grid and the LUT
  do not depend on the raster and carry over untouched, as do the clock, the FFC schedule and
  the drift's age.
- **The clock** is ferrofluid's vote on seconds or milliseconds with fountain's fallback (after
  30 frames without a verdict, the step's size decides), by way of filings.
- **Shutter Interval's default is 30 s**, so a clip shows a shutter; the cores' own defaults are
  minutes.
- **No OpenFX port, no browser demo** (not required this round).

## How the defaults were chosen

Frame Rate 9 Hz is the idea; Shutter Interval 30 s, Span Auto, Level 27.5 °C, Width 25 K and
Mix 1 were set from the model. Drift, Narcissus, Plateau, Palette and Halo were chosen over
Resolume's bundled clips through `bmtest --pipe` at 480×270 and 30 fps, tiled into contact
sheets and looked at as stills (the script is not in the repo):

- **The palettes** over Ethnik2, BattleWeapon_Tank_09, NeonRoom2_32 and Metalive 01 at 2.5 s:
  the clip, White Hot, Ironbow and Rainbow. Ironbow read as a thermal camera at a glance;
  White Hot, the core's own 8-bit grey with no palette, read as a grey video effect on these
  clips. **Ironbow**, an aesthetic call (an open question below).
- **Plateau, Narcissus and Drift** over BattleWeapon_Tank_09 and Metalive 01 at 2.5 s and 28 s
  (just before the first FFC). The first draft (Plateau 7%, Boson's default; Narcissus 0.3 K;
  Drift 0.3 K a minute) was swamped: these clips' backgrounds are perfectly flat, black or
  transparent, one temperature, and plateau equalisation at 7% hands that one bin enough of
  the scale that the narcissus spot and half a minute of drift fill the frame and the subject
  goes pale. Narcissus 0.1 K and Drift 0.1 K a minute at 7% still lifted the background to
  stripes by 28 s; at **2%** the background goes dark, the stripes build visibly toward the
  FFC and the subject holds. 0.05 K, 0.05 K a minute and a 0.3% plateau hid the stripes
  almost entirely. Chosen: **Plateau 2%, Narcissus 0.1 K, Drift 0.1 K a minute**.
- **Plateau 7% against 2% and Halo 1 against 0.5** over FogAndDust_3, Galactucity_21 and
  IntoTheGlow_02 at 28 s (textured clips, with Narcissus 0.2 K and Drift 0.1 K a minute): 2% did
  not flatten them, and Halo 0.5 lost the ringing that makes the look. **Halo 1** stays.

These are looks, not measurements.

## The traps actually hit

- **The camera check's readout count was off by one**: it asked for more than 50 readouts in
  6.5 s at 9 Hz through one FFC, and there are exactly 50. It now computes the exact count
  from integers (the instants up to the last frame, less the ⌈23 f/26⌉ frozen) and requires
  equality.
- **A halo card's grey that is not a code.** g0 = 0.35 quantises to 89/255, biasing every pixel
  of the plateau by 0.00098 and the one-sided sums with it. The card's two levels are now
  exact codes (89/255 and 166/255).
- **The provisional defaults swamped the demo clips** (above): a default must survive the
  bundled clips, the fleet's rule, and the flat backgrounds of Resolume's Shop74 clips are the
  worst case for histogram equalisation.
- **Diag.h said six shaders**, filings' count; the plugin compiles nine programs.
- **The shutter check printed "frames" for sensor instants** in its output; it says instants
  now. A small thing, but the claim is about instants, not host frames.
- **zsh has no PIPESTATUS** and **macOS has no timeout**: the pipe checks run under bash, the
  mutants under perl's alarm, as in filings.
- **The FFGL SDK warns** (FFGLUtilities.cpp, an implicit int → float); verify.sh ignores
  warnings under external/ffgl only.

## Verified, and assumed

Verified, by `OXBOW=... tools/verify.sh` on 16 November 2026 (numbers in README's Status):
every claim in the table there, the picture checks at 1280×720 and 320×180 on this Mac's GPU
and every one again on Apple's software renderer at 320×180; the output alpha on two of
Resolume's clips; the negative controls and mutants; the sweep; the bundle universal, signed
ad hoc, read and rendered by oxbow; verify.sh failing (exit 1) with no oxbow.

Assumed: T = 15 °C + 25 K × luma × alpha; a signal linear in T; τ = 12 ms (Lepton); D = 23/26 s
and the shutter opening 3/26 s before its end; the shutter at 27.5 °C; drift linear in age at
Drift's rate, half in the columns, capped at 600 s; the narcissus spot's R and b; σ = 0.01 H;
the damping's first-order form per sensor frame; LeptonModule's palettes standing in for FLIR's.

Not done: loading it into Resolume on any platform; a Windows build; comparing with a real
thermal camera; a user guide, presets, an OpenFX port, a browser demo.

## Would this hold on another rasteriser, at another raster?

One line per check.

- `--camera`: no GL; double arithmetic against RK4 at steps of at most 20 µs.
- `--instants`: no GL; integers against the camera's double.
- `--thermal`: yes; per-pixel float32 arithmetic, and τ is read from the state, not the
  picture. Passes on the software renderer.
- `--clock`: the change sets are state and hold anywhere; the pictures at 4.99e8 ms are
  bit-identical on this GPU, and 1 code apart in 2 of 12 cases on the software renderer, hence
  the tolerance of 1 code.
- `--shutter`: yes; change sets from the picture, the bolometers from the state.
- `--agc`: yes on any driver whose additive R32F blending is exact for integer counts below
  2²⁴ (both this GPU and the software renderer are); the stripe card's populations are exact on
  the fixed grid at any raster.
- `--manual`: yes; the tolerance is the 8-bit rounding.
- `--halo`: yes; the expected sums are computed from the kernel at the raster checked (σ 7.2 px
  at 720p, 1.8 px at 180p) and the tolerance is half a code a pixel, summed.
- `--narcissus`: yes; the chord sum is evaluated at the pixel centres of the raster checked.
- `--drift`: yes, statistically; the tolerances are 4 standard errors from the uniform
  variates' fourth moment at the raster checked (wider at 320×180).
- `--palette`: yes; exact 8-bit lookups.
- `--alpha`: yes to float32; Mix 0 is a texelFetch and a copy.
- `--resize`: yes at its three rasters; the held frame is compared bit for bit, which a
  bilinear resample of an RGBA8 buffer at another driver might not keep.
- `--glstate`: state the plugin puts back, on any driver that reports it.

## Tolerances and where they come from

| check | tolerance | source |
| --- | --- | --- |
| camera | 1e-6 K, readouts exact in number | double against an RK4 whose own error is far below; measured 1.5e-12 K |
| instants | 0 disagreements | integers |
| thermal | 1e-4 K; τ to 1e-3 of itself | float32 resolves 3.8e-6 K at 35 °C; 10× float32's bound on τ at a 0.01 K residual |
| clock | 0 frames; instants to 1e-5 s; pictures 1 code | integers; a host frame is 7–42 ms away, so 1e-5 s separates instant from frame; the software renderer's measured 1 code |
| shutter | 0 frames; 1e-4 K | integers; float32 at the shutter's 27.5 °C |
| agc | 0 counts; 0.5 code; 1e-3 on the damping ratio | exact counts; the 8-bit output's rounding; 8× float32's bound at a 1e-3 distance |
| manual | 0.5 code | the 8-bit output's rounding |
| halo | half a code a pixel, summed over the sum's pixels | the 8-bit rounding of the picture the sums are read from |
| narcissus | 1e-4 K inside, 1e-5 K outside, 1e-3 N on the profile, 0.01 px centroid | float32; the chord sum's own error (3.8e-7 K) is far below |
| drift | 1e-4 on linearity; 4 standard errors on the rate, the column share, the correlation and the cap | the uniform variates' fourth moment (9/5) over the raster's columns and pixels |
| palette | exact | 8-bit tables |
| alpha | 1e-5; Mix 0 bit-exact; on Resolume's clips 1 code | float32; a copy; 127.5 + a/2 is a half code |
| resize | 1e-4 K; 0 values; 1e-6 on the transfer | float32; a copy; float32 |

## The negative controls and mutants

`bmtest --negative`, each wrong model required to fail (checks failed of those run):

| check | wrong model | failed |
| --- | --- | --- |
| camera | τ 15 ms | 3 |
| camera | the lag stepped by a fixed alpha a host frame, tuned for 60 fps | 3 |
| camera | an FFC of 1 s | 3 |
| camera | the readout taken at the host frame, not the sensor instant | 2 |
| instants | a sensor frame every round(60/f) host frames | 1 |
| instants | the freeze held for round(60 D) host frames | 1 |
| instants | no freeze: sensor frames read out through the FFC | 1 |
| thermal | τ 15 ms | 8 |
| thermal | the lag stepped by a fixed alpha a host frame, tuned for 60 fps | 6 |
| thermal | a transparent pixel keeps its colour's temperature | 2 |
| clock | the host's clock read in single precision | 26 |
| clock | a sensor frame every round(60/f) host frames | 20 |
| clock | the readout taken at the host frame, not the sensor instant | 14 |
| shutter | no freeze | 14 |
| shutter | the freeze held for round(60 D) host frames | 8 |
| shutter | an FFC of 1 s | 14 |
| agc | no plateau: plain histogram equalisation | 2 |
| agc | the plateau applied to the cumulative histogram | 2 |
| agc | the transfer damped by 0.85 a host frame | 2 |
| agc | no damping | 4 |
| manual | Width read as a half-width | 2 |
| halo | the Gaussian 1.5× wider | 4 |
| halo | the unsharp mask's sign flipped | 4 |
| narcissus | its radius in frame widths | 2 |
| narcissus | removed by an FFC | 2 |
| narcissus | off centre by 0.05 H | 2 |
| drift | growing as √age | 2 |
| drift | the age never reset | 2 |
| drift | the pattern all per pixel, no column part | 2 |
| drift | an FFC keeps the pattern | 2 |
| drift | the age never capped | 2 |
| palette | an index off by one | 2 |
| palette | the table reversed | 2 |
| alpha | the clip's alpha handed through | 2 |
| alpha | a transparent pixel keeps its colour's temperature | 2 |
| resize | a new raster starts from cleared buffers | 1 |
| resize | a new raster re-primes the AGC | 1 |

`--glstate` has no negative control.

`tools/mutate.sh`, 27 one-character mutants, each built arm64 and run at 320×180 under a 300 s
alarm, all caught, by the check named:

| # | mutant | caught by |
| --- | --- | --- |
| 1 | GLSL: the clip's temperature below the low end, + → − | `--thermal` |
| 2 | C++: the lag's weight on the last state growing, − → + | `--thermal` |
| 3 | GLSL: the readout weighs the clip by the last state's weight, y → x | `--clock` |
| 4 | C++: an instant on a host frame counted after it, + → − | `--clock` |
| 5 | C++: the clock-unit fallback upside down, < → > | `--clock` |
| 6 | GLSL: the bolometers weigh the shutter by the clip's weight, z → y | `--shutter` |
| 7 | C++: the shutter opens 3/26 s after the FFC ends, − → + | `--shutter` |
| 8 | C++: the next FFC scheduled before the last ended, + → − | `--shutter` |
| 9 | GLSL: the AGC grid reads the next pixel column, 1 → 2 | `--agc` |
| 10 | GLSL: the histogram's bins counted from −5 °C, − → + | `--agc` |
| 11 | GLSL: the cumulative histogram counted from the top, < → > | `--agc` |
| 12 | GLSL: the damping toward the sum of the transfers, − → + | `--agc` |
| 13 | GLSL: the level inside a bin from the sum of its ends, − → + | `--agc` |
| 14 | C++: the AGC's damping 0.95 a sensor frame | `--agc` |
| 15 | GLSL: Manual multiplies by Width, / → * | `--manual` |
| 16 | GLSL: the 8-bit output rounded at 0.4 | `--manual` |
| 17 | GLSL: the halo's Gaussian narrower (−0.6/σ²) | `--halo` |
| 18 | GLSL: the unsharp mask adds the blur, − → + | `--halo` |
| 19 | GLSL: the discs' overlap with the wrong triangle term | `--narcissus` |
| 20 | C++: the narcissus centred at 0.6 of the width | `--narcissus` |
| 21 | GLSL: the drift's pattern 14% too strong | `--drift` |
| 22 | C++: the drift falling with age, * → / | `--drift` |
| 23 | C++: Black Hot one level off | `--palette` |
| 24 | C++: Ironbow's interpolation rounded at 0.4 | `--palette` |
| 25 | GLSL: the camera takes alpha away over a clear clip, + → − | `--alpha` |
| 26 | GLSL: the clip's share of the mix doubled, 1 → 2 | `--alpha` |
| 27 | GLSL: the resample reads x from y | `--resize` |

## The sweep's honest list

`python3 tools/sweep.py --bare` (no per-control context): **3 of 12 controls are dead**, and
that is the design. Shutter Interval shows only once an FFC has started, and a bare render is
3 s, shorter than the shortest interval (5 s); Level and Width act only in Span Manual. With
the sweep's normal contexts (320 frames for Shutter Interval, Manual for Level and Width, 90 s
at 2 fps with the FFC off for Drift) all 12 change the picture. Drift is alive even bare (3 s
of 1 K a minute shows at a 2% plateau).

## Where the build departed from the spec

The spec was written first and has been changed to match each of these:

- **Defaults:** Plateau 2%, not Boson's 7%; Palette Ironbow; Drift 0.1 K a minute; Narcissus
  0.1 K; Halo 1 (the spec left the last four to the demo clips).
- **The composite's colour** is (clip × a × (1 − Mix) + camera × Mix) / alpha, the straight-alpha
  form of the same mix, not mix(clip, camera, Mix): over a transparent pixel the clip's colour
  is not there to mix.
- **A Frame Rate change** starts its grid at the last host frame, not the frame that sees the
  change.
- **The checks as built:** `--camera` (RK4) and `--instants` (integers) are the offline checks;
  `--thermal` reads τ from the state at four host rates and the settled temperatures (the
  readout's instant is `--clock`'s); `--agc` measures 7% and 100% on a stripe card and the
  damping, not projection at 10⁻⁵ or the contrast jump; `--halo` reads an edge's net overshoot,
  one-sided sum and first moment, not a disc's second moment; `--narcissus` compares the whole
  profile with a chord-by-chord sum; `--drift` adds the cap and a new pattern after an FFC;
  the negative controls are the 37 above (the FFC's wrong length is 1 s, the halo's wrong width
  1.5×).
- **The pictures at 4.99e8 ms** are bit-identical on this GPU (measured); the check allows a code
  for the software renderer, where 2 of 12 cases differ by one.

## Open questions

- **The default palette.** Ironbow reads as "thermal camera" at a glance; White Hot is the core's
  own 8-bit grey, with no palette applied. Which should a clip open in?
- **Plateau 2% against Boson's 7%.** 2% was chosen because Resolume's clips have perfectly flat
  backgrounds; on a busy clip 7% may look more like the real core. Keep the departure?
- **The output alpha.** At Mix 1 the camera's picture is opaque, as a camera's is, so a clip with
  alpha becomes a rectangle with the transparent parts cold. The alternative is to keep the
  clip's alpha (a thermal look on the layer's shape only). Which does a VJ want?
- **No temporal noise (NETD).** The picture is still between readouts and noiseless within one;
  a real core's frame-to-frame noise (its NETD) would show in Manual at a narrow Width. Add it?
- **Narcissus and drift on synthetic, perfectly flat backgrounds.** Equalisation makes a flat
  background show every millikelvin, so on Resolume's clips the drift's stripes and the spot
  appear where no real scene would be that uniform. Is that the look, or should flat regions
  be held back (a real core's AGC has tail rejection and a max gain, both left out)?
- **Luma of the encoded values** is the temperature (equal steps of apparent brightness are
  equal steps of temperature). Linear luminance would put more of the scale into the
  highlights. Keep encoded?
- **The drift and the narcissus are in host pixels**, so the stripes are finer at 4K than at
  720p; a real core's pixels are a 160 × 120 or 320 × 256 grid. Should the drift be on a sensor
  grid instead?
