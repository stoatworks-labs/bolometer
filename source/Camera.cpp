#include "Camera.h"

#include "Model.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace bolometer
{
namespace
{
/// A millionth of a sensor period: an instant this close to a host frame is
/// at or before it (the host's clock arrives through a subtraction in
/// double, never exactly).
constexpr double kInstantSlack = 1e-6;
constexpr double kInfinity     = std::numeric_limits< double >::infinity();
} // namespace

double Camera::Tau() const
{
	return hooks.tau > 0.0 ? hooks.tau : model::kTau;
}

double Camera::FfcDuration() const
{
	return hooks.ffcDuration > 0.0 ? hooks.ffcDuration : model::kFfcDuration;
}

long long Camera::IndexAtOrBefore( double t ) const
{
	return static_cast< long long >( std::floor( ( t - epoch ) * rate + kInstantSlack ) );
}

long long Camera::IndexAtOrAfter( double t ) const
{
	return static_cast< long long >( std::ceil( ( t - epoch ) * rate - kInstantSlack ) );
}

double Camera::InstantTime( long long k ) const
{
	return epoch + static_cast< double >( k ) / rate;
}

bool Camera::inProgress( double t ) const
{
	return std::isfinite( nextStart ) && t >= nextStart && t < nextStart + FfcDuration();
}

void Camera::reschedule()
{
	if( !std::isfinite( interval ) )
	{
		nextStart = kInfinity;
		return;
	}
	nextStart = InstantTime( std::max( IndexAtOrAfter( lastEnd + interval ), lastK + 1 ) );
}

CameraFrame Camera::prime( double t, const CameraSettings& settings )
{
	primed     = true;
	epoch      = t;
	rate       = settings.rate > 0.0 ? settings.rate : 9.0;
	interval   = settings.interval;
	lastT      = t;
	lastK      = 0;
	lastEnd    = t;
	startTime  = t;
	completed  = 0;
	freezeFrom = -1;
	reschedule();

	CameraFrame f;
	f.prime        = true;
	f.state        = { 0.0, 1.0, 0.0 };
	f.readout      = true;
	f.sample       = { 0.0, 1.0, 0.0 };
	f.fresh        = 0;
	f.driftSeed    = seed;
	f.readoutIndex = 0;
	f.readoutTime  = t;
	f.latestIndex  = 0;
	return f;
}

Weights Camera::weightsAt( double x, const std::vector< Window >& windows ) const
{
	const double tau = Tau();
	Weights w;
	if( hooks.perFrameAlpha )
	{
		//The wrong model: a fixed step per host frame, tuned for 60 fps.
		w.prev    = std::exp( -( 1.0 / 60.0 ) / tau );
		w.scene   = 1.0 - w.prev;
		w.shutter = 0.0;
		return w;
	}
	w.prev    = std::exp( -( x - lastT ) / tau );
	w.shutter = 0.0;
	for( const Window& win : windows )
	{
		const double a = std::max( win.start, lastT );
		const double b = std::min( win.open, x );
		if( b > a )
			w.shutter += std::exp( -( x - b ) / tau ) - std::exp( -( x - a ) / tau );
	}
	w.scene = std::max( 0.0, 1.0 - w.prev - w.shutter );
	return w;
}

bool Camera::frozenIndex( long long k, const std::vector< Window >& windows ) const
{
	for( const Window& win : windows )
		if( k >= IndexAtOrAfter( win.start ) && k <= IndexAtOrAfter( win.end ) - 1 )
			return true;
	return false;
}

CameraFrame Camera::Advance( double t, const CameraSettings& settings, bool press )
{
	++hostFrames;
	if( !primed || !( t >= lastT ) || !std::isfinite( t ) )
		return prime( t, settings );

	//A new rate starts a new grid of instants at the last host frame (whose
	//readout is already shown); an FFC in progress keeps its time window.
	if( settings.rate > 0.0 && settings.rate != rate )
	{
		const bool busy = inProgress( lastT );
		epoch           = lastT;
		rate            = settings.rate;
		lastK           = 0;
		if( !busy )
			reschedule();
	}
	if( settings.interval != interval )
	{
		interval = settings.interval;
		if( !inProgress( lastT ) )
			reschedule();
	}
	if( press && !inProgress( lastT ) )
		nextStart = std::min( nextStart, InstantTime( lastK + 1 ) );

	//The FFCs that touch ( lastT, t ]: one in progress, one starting, and --
	//across a long gap -- any that start and end inside it.
	const double D        = FfcDuration();
	const double endBefore = lastEnd;
	const uint32_t seedBefore = seed;
	std::vector< Window > windows;
	std::vector< Completion > completions;
	while( nextStart <= t )
	{
		const Window win { nextStart, nextStart + D - model::kShutterMargin, nextStart + D };
		windows.push_back( win );
		if( win.end > t )
			break;
		lastEnd = win.end;
		if( !hooks.driftKeepsPattern )
			++seed;
		++completed;
		completions.push_back( { win.end, seed } );
		reschedule();
	}

	CameraFrame f;
	f.state = weightsAt( t, windows );
	for( const Window& win : windows )
		f.shutterClosed = f.shutterClosed || ( t >= win.start && t < win.open );

	long long kNew  = 0;
	long long j     = -1;
	long long fresh = 0;
	double readAt   = t;
	if( hooks.hostFrameCounting )
	{
		//The wrong model: a sensor frame every round( 60 / f ) host frames.
		const long long every = std::max( 1LL, std::llround( 60.0 / rate ) );
		const bool fire       = hostFrames % every == 0;
		kNew                  = lastK + ( fire ? 1 : 0 );
		f.frozen              = !windows.empty() && t < windows.back().end && t >= windows.back().start;
		if( fire && !f.frozen )
		{
			j     = kNew;
			fresh = 1;
		}
	}
	else
	{
		kNew = IndexAtOrBefore( t );
		if( hooks.freezeHostFrames )
		{
			//The wrong model: the freeze held for a count of host frames.
			if( !windows.empty() && freezeFrom < 0 )
				freezeFrom = hostFrames;
			const long long hold = std::llround( 60.0 * D );
			f.frozen             = freezeFrom >= 0 && hostFrames - freezeFrom < hold;
			if( freezeFrom >= 0 && !f.frozen && windows.empty() )
				freezeFrom = -1;
			if( kNew > lastK && !f.frozen )
			{
				j     = kNew;
				fresh = kNew - lastK;
			}
		}
		else if( hooks.noFreeze )
		{
			if( kNew > lastK )
			{
				j     = kNew;
				fresh = kNew - lastK;
			}
		}
		else
		{
			//The latest instant no FFC has frozen, and how many were read out.
			f.frozen = frozenIndex( kNew, windows );
			j        = kNew;
			for( auto it = windows.rbegin(); it != windows.rend(); ++it )
			{
				const long long lo = IndexAtOrAfter( it->start ), hi = IndexAtOrAfter( it->end ) - 1;
				if( j >= lo && j <= hi )
					j = lo - 1;
			}
			fresh = kNew - lastK;
			for( const Window& win : windows )
			{
				const long long lo = std::max( IndexAtOrAfter( win.start ), lastK + 1 );
				const long long hi = std::min( IndexAtOrAfter( win.end ) - 1, kNew );
				if( hi >= lo )
					fresh -= hi - lo + 1;
			}
			if( j <= lastK )
				j = -1;
		}
		if( j >= 0 )
			readAt = hooks.sampleAtHostTime ? t : InstantTime( j );
	}

	if( j >= 0 )
	{
		f.readout      = true;
		f.sample       = hooks.sampleAtHostTime ? f.state : weightsAt( std::min( readAt, t ), windows );
		f.fresh        = static_cast< int >( std::max( 1LL, fresh ) );
		f.readoutIndex = j;
		f.readoutTime  = readAt;

		//The drift at the readout: its age since the last FFC that had ended
		//by then, and that FFC's pattern.
		double since       = endBefore;
		uint32_t pattern   = seedBefore;
		for( const Completion& c : completions )
			if( c.end <= readAt )
			{
				since   = c.end;
				pattern = c.seed;
			}
		if( hooks.driftNoReset )
			since = startTime;
		const double age = std::clamp( readAt - since, 0.0, hooks.driftNoCap ? kInfinity : model::kDriftAgeCap );
		f.driftAmplitude = hooks.driftSqrt ? settings.driftRate * std::sqrt( age * model::kDriftAgeCap ) : settings.driftRate * age;
		f.driftSeed      = pattern;
	}
	f.latestIndex   = kNew;
	f.ffcsCompleted = completed;

	lastT = t;
	lastK = kNew;
	return f;
}

} // namespace bolometer
