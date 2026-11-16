# Attributions

Bolometer is built on other people's work. This file lists what that work is, who did it, and
what it is doing here.

**A provisional hand copy (2026-11-16).** Bolometer is not yet registered in
`stoatworks-backend`, so this was written by hand in the generated file's shape. Once it is
registered, `scripts/sync-attributions.py` generates it from the master lists; edit it there
then, not here.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### The harness and the tooling — Stoatworks filings, fountain and ferrofluid before them

<https://github.com/stoatworks-labs/ferrofluid>  
Licence: MIT  
Copyright: Stoatworks Labs

The harness's rig (a headless CGL context, the software-renderer switch, the synthetic clock), PNG writer, cue sheets, --pipe/--film (SIGPIPE ignored, a closed stdout an exit of 1), --set by display name, --negative with its Perturb and --offline: tools/bmtest is filings' fgtest infrastructure, which is fountain's fttest, which is ferrofluid's fetest, under new checks. tools/verify.sh, tools/mutate.sh (with fountain's perl alarm), tools/glslc.sh and tools/sweep.py are filings' scripts with bolometer's steps, mutants and table. The clock's vote on seconds or milliseconds (ferrofluid's, by way of rosette, millpond and filings) with fountain's offline fallback is in source/Bolometer.cpp. All adapted. filings and fountain live at ~/dev and are not yet published.

### The pass buffers, the GL state and the log — Stoatworks millpond and radar

<https://github.com/stoatworks-labs/millpond>  
Licence: MIT  
Copyright: Stoatworks Labs

source/PassBuffer.{h,cpp} and source/Diag.{h,cpp} are millpond's, by way of filings. source/GLState.h is radar's, which is vectrix's, from resolume-scopes.

### The About block — Stoatworks zoetrope

<https://github.com/stoatworks-labs/zoetrope>  
Licence: MIT  
Copyright: Stoatworks Labs

source/StoatworksAbout*.h, a provisional hand copy of filings', itself fountain's, from zoetrope's first header (no user guide yet).

### The look card — Stoatworks schlieren

<https://github.com/stoatworks-labs/schlieren>  
Licence: MIT  
Copyright: Stoatworks Labs

The harness's test card (a sunset sky, a sun, a skyline, colour bars), the default clip in bmtest and the picture in docs/hero.png.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### LeptonModule — the Rainbow and Ironbow palettes

<https://github.com/groupgets/LeptonModule>  
Licence: BSD-2-Clause  
Copyright: © 2014 Pure Engineering LLC

source/Palettes.cpp carries `colormap_rainbow` (all 256 entries) and the iron half of `colormap_ironblack` (entries 127–254) unchanged from software/raspberrypi_video/Palettes.cpp, read at commit 77e03b7333, with the licence's notice in the file. Ironbow is those 128 entries interpolated to 256; entry 255 of the upstream table is dropped (its blue falls from 235 to 24). Whether these tables equal FLIR's own on-camera palettes is not established.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl, pinned at b1afaf9.

The plugin ABI itself. An FFGL effect or source is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>  
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)  
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Arrives inside the FFGL submodule at external/ffgl/deps/glew-2.1.0. Not fetched separately.

Resolves OpenGL entry points on Windows, where the system headers stop at OpenGL 1.1.

### libpng

<http://www.libpng.org/pub/png/libpng.html>  
Licence: PNG Reference Library License (libpng)  
Copyright: the PNG Reference Library authors

Arrives inside the FFGL submodule, under the SDK's CustomThumbnail sample.

Part of the upstream SDK tree rather than something this plugin calls directly — listed because it is present in the checkout.

## Work we checked ourselves against

No code was taken from these — but they were how we knew we had it right, and that is worth saying out loud. The spec (`~/Projects/resolume/specs/SPEC-bolometer.md`) quotes each one, and says what was tried and not reached.

### The thermal time constant, the FFC and drift — FLIR, Lepton Engineering Datasheet Rev 200

τ "on the order of 12 msec" (§5), the FFC's 23 frames at 26 Hz and its states, drift appearing as grain and columns drifting as a group, the 8.7 Hz exportable rate. Read in SparkFun's copy. source/Model.h carries τ and the FFC's length; bmtest --camera, --thermal and --shutter check the plugin against them.

### The freeze, the shutter behind the lens and the AGC — FLIR, Boson Thermal Imaging Core Product Datasheet, Doc. 102-2013-40 Rev 340

The frozen output "throughout the FFC in progress state", the shutter's order, the Plateau Value (factory default 7%) and the Damping Factor (85%), detail enhancement. Read in GroupGets' copy. bmtest --shutter and --agc check the plugin's freeze, plateau and damping.

### The export limit — 15 CFR Part 774 Supplement No. 1, Category 6 (eCFR, version 2025-10-01)

6A003.b.4.b Note 3: imaging cameras with a maximum frame rate at or below 9 Hz are not controlled. That is the default Frame Rate.

### Plateau equalisation — US 5,799,106 (Mooney, Silverman and DiSalvo; US Air Force)

The plateau as a clip on the histogram's bins, and its limits at one count (projection) and at every pixel (equalisation). The AGC's transfer in source/Shaders.cpp; bmtest --agc checks it against the card's own histogram.

### Halos from a linear base/detail split — F. Durand and J. Dorsey, SIGGRAPH 2002; F. Garcia, C. Schockaert and B. Mirbach, VISAPP 2015

Why detail enhancement on a linear low-pass rings hot edges dark. The unsharp mask in source/Shaders.cpp; bmtest --halo checks it against its kernel. (US 10,657,378 and US 10,726,532, Bovik and Goodall, were read and rejected as the mechanism: their halo is a chopper's.)

### Narcissus — S. H. Aslan, Analysis of narcissus effect in infrared optical systems with cooled detectors, PhD thesis, METU, 2022

The dark spot at the centre, and why a shutter calibration behind the lens cannot remove it. The spot's size here is chosen; the thesis's figures are for cooled detectors.

### Hash functions for GPU rendering — M. Jarzynski and M. Olano, Journal of Computer Graphics Techniques 9(3) (2020)

They recommend a PCG-style hash as the default one-input hash; the drift pattern's `pcg` (source/Shaders.cpp) is the fleet's copy of it. Its constants came from the fleet's code and were not checked against the paper's supplementary code.

## Standards and published specifications

What the implementation is measured against.

- **ITU-R BT.709** — the luma weights 0.2126, 0.7152, 0.0722 applied to the clip's encoded values, in source/Shaders.cpp (`sceneTemp`).

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.
