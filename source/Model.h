#pragma once

/**
    The model's constants, each with where it comes from (specs/SPEC-bolometer.md
    has the quotations). "Chosen" means no source gives it.

    The harness keeps its own copies, typed from the spec, so that a constant
    changed here is a constant the checks see change.
*/
namespace bolometer::model
{

constexpr double kPi = 3.14159265358979323846;

/// The bolometer's thermal time constant, s: Lepton Engineering Datasheet Rev
/// 200 section 5, "on the order of 12 msec" (Boson: nominally 8 ms; FLIR: 7-12).
constexpr double kTau = 0.012;

/// The mapping from the clip, chosen: T = kTempLow + kTempSpan x luma x alpha.
constexpr double kTempLow  = 15.0;///< degC, a transparent or black pixel
constexpr double kTempSpan = 25.0;///< K, so white reads 40 degC

/// Lepton's default FFC: "nominally 23 frames at 26Hz", s.
constexpr double kFfcDuration = 23.0 / 26.0;
/// The shutter opens this long before the FFC ends, so the bolometers relax
/// back (chosen out of Lepton's 15 overhead frames: 3 of them, 9.6 tau).
constexpr double kShutterMargin = 3.0 / 26.0;
/// The shutter's temperature, degC (chosen: mid-range; it never shows).
constexpr double kShutterTemp = 27.5;
/// Drift stops growing after this long without an FFC, s (chosen, so that
/// Shutter Off does not run away).
constexpr double kDriftAgeCap = 600.0;

/// The AGC's histogram: 512 bins over 5..50 degC, from a fixed 256 x 144
/// grid of readout samples (chosen; about a real core's pixel count).
constexpr int kBins        = 512;
constexpr double kHistLow  = 5.0;
constexpr double kHistHigh = 50.0;
constexpr int kGridW       = 256;
constexpr int kGridH       = 144;
constexpr int kGridCount   = kGridW * kGridH;
/// Boson's AGC Damping Factor, factory default 85%, applied per sensor frame
/// (the filter's form is assumed).
constexpr double kDamping = 0.85;

/// The narcissus spot: a disc of radius kNarcissusR frame heights blurred by
/// a defocus disc of radius kNarcissusB (both chosen).
constexpr double kNarcissusR = 0.15;
constexpr double kNarcissusB = 0.10;

/// The detail enhancement's Gaussian, sigma in frame heights (chosen), cut at
/// ceil( kHaloCut x sigma ) pixels and renormalised.
constexpr double kHaloSigma = 0.01;
constexpr double kHaloCut   = 4.0;

} // namespace bolometer::model
