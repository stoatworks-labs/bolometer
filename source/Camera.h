#pragma once

#include <cstdint>
#include <vector>

/**
    The camera's clock, on the CPU, in double: the sensor instants, the
    flat-field corrections (FFC) and the exact weights of the thermal lag.

    Nothing time-like reaches a shader. Each host frame the plugin calls
    Advance( t ) with the host's time in seconds, and gets back what the GPU
    needs for that frame: the weights that take every bolometer from its
    state at the last host frame to its state now, and, when a sensor frame
    is read out, the weights at that sensor instant.

    **The lag.** Each bolometer follows dT/dt = ( target - T ) / tau. The
    target is piecewise constant: the clip's temperature S, held over the
    host interval ( t_prev, t ], or the shutter's temperature while the
    shutter is closed. A constant target integrates exactly, so for any
    instant X in the interval

        T( X ) = prev x T( t_prev ) + scene x S + shutter x T_shutter,
        prev    = exp( -( X - t_prev ) / tau ),
        shutter = sum over the closed segments [ a, b ) of
                  exp( -( X - min( b, X ) ) / tau ) - exp( -( X - a ) / tau ),
        scene   = 1 - prev - shutter,

    whatever the host's frame rate.

    **The sensor clock.** Instants t_k = epoch + k / f. The instants at or
    before t are k <= floor( ( t - epoch ) f + 1e-6 ): an instant that
    coincides with a host frame (to a millionth of a period) is at or before
    it. The picture is the readout at the latest instant at or before t that
    an FFC has not frozen (sample and hold).

    **The FFC.** It starts at a sensor instant c and lasts D = 23/26 s
    (Lepton's default). The shutter is closed over [ c, c + D - 3/26 ); the
    sensor frames with t_k in [ c, c + D ) are not read out, so the last valid
    frame is held (Boson's freeze). When it ends the drift restarts from zero
    with a new pattern. The next starts at the first instant at least the
    interval after the end of the last; a press starts one at the next
    instant.
*/
namespace bolometer
{

struct CameraSettings
{
	double rate      = 9.0;///< sensor frames a second
	double interval  = 30.0;///< s between FFCs (end to start); infinity: never
	double driftRate = 0.0;///< K per second
};

/// The weights of the previous state, the clip and the shutter at an instant.
struct Weights
{
	double prev = 1.0, scene = 0.0, shutter = 0.0;
};

/// What one host frame needs.
struct CameraFrame
{
	bool prime = false;  ///< the first frame (or the clock went backwards): the state starts settled
	Weights state;       ///< the bolometers at the host frame's time
	bool readout = false;///< a sensor frame is read out this host frame
	Weights sample;      ///< the bolometers at the readout's instant
	int fresh = 0;       ///< sensor frames read out (not frozen) since the last host frame
	double driftAmplitude = 0.0;///< K RMS, at the readout's instant
	uint32_t driftSeed    = 0;

	// For the harness and the log.
	long long readoutIndex = -1;///< k of the readout, -1 for none
	double readoutTime     = 0.0;
	long long latestIndex  = 0;///< k of the latest instant at or before t
	bool frozen            = false;///< the latest instant is inside an FFC
	bool shutterClosed     = false;///< at the host frame's time
	int ffcsCompleted      = 0;
};

class Camera
{
public:
	/// The negative controls: each is a wrong model a check must catch.
	struct Hooks
	{
		double tau             = 0.0;  ///< 0: the model's
		double ffcDuration     = 0.0;  ///< 0: the model's
		bool perFrameAlpha     = false;///< the lag stepped by a fixed alpha a host frame (one tuned for 60 fps)
		bool sampleAtHostTime  = false;///< the readout taken at t, not at the sensor instant
		bool hostFrameCounting = false;///< a sensor frame every round( 60 / f ) host frames
		bool freezeHostFrames  = false;///< the freeze counted as round( 60 D ) host frames
		bool noFreeze          = false;///< sensor frames read out through the FFC
		bool driftNoReset      = false;///< the drift's age counted from the start, never reset
		bool driftSqrt         = false;///< the drift growing as sqrt( age )
	};

	/// The next host frame, at `t` seconds. `press` starts an FFC at the next
	/// sensor instant (ignored while one is in progress).
	CameraFrame Advance( double t, const CameraSettings& settings, bool press );

	/// Start again from the next frame (the host's clock changed its unit).
	void Reprime()
	{
		primed = false;
	}

	Hooks hooks;

	// Read-only, for the harness.
	double Epoch() const
	{
		return epoch;
	}
	double Rate() const
	{
		return rate;
	}
	double NextFfcStart() const
	{
		return nextStart;
	}
	double LastFfcEnd() const
	{
		return lastEnd;
	}
	double Tau() const;
	double FfcDuration() const;

	/// floor( ( t - epoch ) f + 1e-6 ) and ceil( ( t - epoch ) f - 1e-6 ).
	long long IndexAtOrBefore( double t ) const;
	long long IndexAtOrAfter( double t ) const;
	double InstantTime( long long k ) const;

private:
	struct Window
	{
		double start, open, end;///< FFC start, shutter open, FFC end
	};
	struct Completion
	{
		double end;
		uint32_t seed;
	};

	CameraFrame prime( double t, const CameraSettings& settings );
	void reschedule();
	bool inProgress( double t ) const;
	Weights weightsAt( double x, const std::vector< Window >& windows ) const;
	bool frozenIndex( long long k, const std::vector< Window >& windows ) const;

	bool primed     = false;
	double epoch    = 0.0;
	double rate     = 9.0;
	double interval = 30.0;
	double lastT    = 0.0;
	long long lastK = 0;
	double nextStart = 0.0;///< infinity: none scheduled
	double lastEnd   = 0.0;///< the end of the last FFC, or the epoch
	double startTime = 0.0;///< the prime's time (driftNoReset)
	uint32_t seed    = 1;
	int completed    = 0;
	long long hostFrames = 0;
	long long freezeFrom = -1;///< freezeHostFrames: the host frame its freeze began
};

} // namespace bolometer
