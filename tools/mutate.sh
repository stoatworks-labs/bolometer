#!/usr/bin/env bash
#
# Mutation testing: change ONE character of the shipped code and require that a
# check fails. A check that still passes against a mutant was not looking at
# the code it claims to cover -- and a harness that compiled its own copy of
# the shaders, or its own copy of the camera, would pass every mutant here.
#
# Each mutant is a fresh copy of the tree in a temporary directory (the FFGL
# SDK is symlinked, not copied), so no object from another mutant survives
# (make's mtime resolution is a second), built arm64-only, with only the named
# check run at 320x180 under a time limit (a mutant can hang: honeydew's
# lesson; a timeout counts as caught). (ferrofluid's script by way of fountain
# and filings; the mutants are bolometer's.)
#
#     tools/mutate.sh          every mutant
#     ONLY=3 tools/mutate.sh   the third alone
#     DRY=1 tools/mutate.sh    only check that every target is there once
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# file | the exact text | its one-character mutant | the check that must fail | what it is
# The fields are split on '|', so neither a target nor a mutant may contain one
# (conway's trap) -- which is also why no target here has a || in it.
MUTANTS=(
	"source/Shaders.cpp|	return low + span * ( model == 1 ? y : y * src.a );|	return low - span * ( model == 1 ? y : y * src.a );|--thermal|GLSL: the clip's temperature below the low end, + -> -"
	"source/Camera.cpp|	w.prev    = std::exp( -( x - lastT ) / tau );|	w.prev    = std::exp( +( x - lastT ) / tau );|--thermal|C++: the lag's weight on the last state growing, - -> +"
	"source/Shaders.cpp|	float T    = Weights.x * prev + Weights.y * S|	float T    = Weights.x * prev + Weights.x * S|--clock|GLSL: the readout weighs the clip by the last state's weight, y -> x"
	"source/Camera.cpp|std::floor( ( t - epoch ) * rate + kInstantSlack )|std::floor( ( t - epoch ) * rate - kInstantSlack )|--clock|C++: an instant on a host frame counted after it, + -> -"
	"source/Bolometer.cpp|clockScale  = hostDelta < 1.0 ? 1.0 : 0.001;|clockScale  = hostDelta > 1.0 ? 1.0 : 0.001;|--clock|C++: the clock-unit fallback upside down, < -> >"
	"source/Shaders.cpp|Weights.y * S + Weights.z * ShutterTemp, 0.0, 0.0, 1.0 );|Weights.y * S + Weights.y * ShutterTemp, 0.0, 0.0, 1.0 );|--shutter|GLSL: the bolometers weigh the shutter by the clip's weight, z -> y"
	"source/Camera.cpp|nextStart + D - model::kShutterMargin|nextStart + D + model::kShutterMargin|--shutter|C++: the shutter opens 3/26 s after the FFC ends, - -> +"
	"source/Camera.cpp|IndexAtOrAfter( lastEnd + interval )|IndexAtOrAfter( lastEnd - interval )|--shutter|C++: the next FFC scheduled before the last ended, + -> -"
	"source/Shaders.cpp|( 2 * i + 1 ) * Size.x / ( 2 * Grid.x )|( 2 * i + 2 ) * Size.x / ( 2 * Grid.x )|--agc|GLSL: the AGC grid reads the next pixel column, 1 -> 2"
	"source/Shaders.cpp|int( floor( ( T - HistLow ) * HistScale ) )|int( floor( ( T + HistLow ) * HistScale ) )|--agc|GLSL: the histogram's bins counted from -5 degC, - -> +"
	"source/Shaders.cpp|		if( j < k )|		if( j > k )|--agc|GLSL: the cumulative histogram counted from the top, < -> >"
	"source/Shaders.cpp|vec4( target + ( prev - target ) * Keep, 0.0, 1.0 )|vec4( target + ( prev + target ) * Keep, 0.0, 1.0 )|--agc|GLSL: the damping toward the sum of the transfers, - -> +"
	"source/Shaders.cpp|y       = lh.x + ( lh.y - lh.x ) * u;|y       = lh.x + ( lh.y + lh.x ) * u;|--agc|GLSL: the level inside a bin from the sum of its ends, - -> +"
	"source/Model.h|constexpr double kDamping = 0.85;|constexpr double kDamping = 0.95;|--agc|C++: the AGC's damping 0.95 a sensor frame, 8 -> 9"
	"source/Shaders.cpp|clamp( ( T - Level ) / Width + 0.5, 0.0, 1.0 )|clamp( ( T - Level ) * Width + 0.5, 0.0, 1.0 )|--manual|GLSL: Manual multiplies by Width, / -> *"
	"source/Shaders.cpp|clamp( v, 0.0, 1.0 ) * 255.0 + 0.5 )|clamp( v, 0.0, 1.0 ) * 255.0 + 0.6 )|--manual|GLSL: the 8-bit output rounded at 0.4, 5 -> 6"
	"source/Shaders.cpp|	float k     = -0.5 / ( Sigma * Sigma );|	float k     = -0.6 / ( Sigma * Sigma );|--halo|GLSL: the halo's horizontal Gaussian narrower, 5 -> 6"
	"source/Shaders.cpp|v = y + HaloGain * ( y - acc / total );|v = y + HaloGain * ( y + acc / total );|--halo|GLSL: the unsharp mask adds the blur, - -> +"
	"source/Shaders.cpp|+ R * R * acos( c2 ) - 0.5 * k )|+ R * R * acos( c2 ) - 0.6 * k )|--narcissus|GLSL: the lens of the two discs with the wrong triangle term, 5 -> 6"
	"source/Bolometer.cpp|const double cx = 0.5 * width|const double cx = 0.6 * width|--narcissus|C++: the narcissus centred at 0.6 of the width, 5 -> 6"
	"source/Shaders.cpp|: 0.70710678 * ( unitVariate( col )|: 0.80710678 * ( unitVariate( col )|--drift|GLSL: the drift's pattern 14% too strong, 7 -> 8"
	"source/Camera.cpp|: settings.driftRate * age;|: settings.driftRate / age;|--drift|C++: the drift falling with age, * -> /"
	"source/Palettes.cpp|static_cast< unsigned char >( 255 - i );|static_cast< unsigned char >( 254 - i );|--palette|C++: Black Hot one level off, 5 -> 4"
	"source/Palettes.cpp|+ kIron[ i1 ][ c ] * f + 0.5 ) );|+ kIron[ i1 ][ c ] * f + 0.6 ) );|--palette|C++: Ironbow's interpolation rounded at 0.4, 5 -> 6"
	"source/Shaders.cpp|float alpha = src.a + ( 1.0 - src.a ) * MixAmount;|float alpha = src.a - ( 1.0 - src.a ) * MixAmount;|--alpha|GLSL: the camera takes alpha away over a clear clip, + -> -"
	"source/Shaders.cpp|src.rgb * src.a * ( 1.0 - MixAmount )|src.rgb * src.a * ( 2.0 - MixAmount )|--alpha|GLSL: the clip's share of the mix doubled, 1 -> 2"
	"source/Shaders.cpp|vec2 p   = gl_FragCoord.xy * vec2( SrcSize )|vec2 p   = gl_FragCoord.yy * vec2( SrcSize )|--resize|GLSL: the resample reads x from y, x -> y"
)

limit() {
	# macOS has no timeout(1): perl's alarm is enough.
	perl -e 'alarm shift; exec @ARGV' "$@"
}

caught=0
ran=0
index=0
for entry in "${MUTANTS[@]}"; do
	index=$(( index + 1 ))
	[[ -n "${ONLY:-}" && "$ONLY" != "$index" ]] && continue
	ran=$(( ran + 1 ))
	IFS='|' read -r file original mutant check what <<<"$entry"
	tree="$WORK/tree"
	rm -rf "$tree"
	mkdir -p "$tree/external"
	cp -R "$REPO/source" "$REPO/tools" "$REPO/cmake" "$REPO/CMakeLists.txt" "$tree/"
	ln -s "$REPO/external/ffgl" "$tree/external/ffgl"

	python3 - "$tree/$file" "$original" "$mutant" <<'PY'
import sys, pathlib
path, original, mutant = pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[3]
text = path.read_text()
if text.count(original) != 1:
    sys.exit(f"mutation target found {text.count(original)} times in {path}: '{original}'")
if sum(a != b for a, b in zip(original, mutant)) != 1 or len(original) != len(mutant):
    sys.exit("a mutant must differ by exactly one character")
path.write_text(text.replace(original, mutant))
PY

	if [[ "${DRY:-0}" == 1 ]]; then
		printf '   ok    %2d %s\n' "$index" "$what"
		caught=$(( caught + 1 ))
		continue
	fi
	printf '\n== mutant %d: %s\n' "$index" "$what"
	cmake -S "$tree" -B "$tree/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DBOLOMETER_BUILD_TOOLS=ON >/dev/null
	cmake --build "$tree/build" --target bmtest -j8 >/dev/null 2>&1
	status=0
	limit 300 "$tree/build/bmtest" "$check" --size 320x180 >"$WORK/log" 2>&1 || status=$?
	if [[ "$status" -eq 0 ]]; then
		printf '   FAIL  %s still PASSES -- the check does not cover this code\n' "$check"
	else
		printf '   ok    %s fails against the mutant (exit %d):\n' "$check" "$status"
		# A mutant that timed out printed no FAIL: grep finding none must not end the run.
		{ grep -E '^  FAIL' "$WORK/log" || true; } | head -2 | cut -c1-200 | sed 's/^/        /'
		caught=$(( caught + 1 ))
	fi
done

printf '\nmutants: %d, caught: %d\n' "$ran" "$caught"
[[ "$ran" -gt 0 && "$caught" -eq "$ran" ]]
