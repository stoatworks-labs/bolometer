#include "Controls.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace bolometer
{
namespace
{
double clamp01( float v )
{
	return std::clamp( static_cast< double >( v ), 0.0, 1.0 );
}
double geometric( float v, double low, double high )
{
	return low * std::pow( high / low, clamp01( v ) );
}
float inverseGeometric( double value, double low, double high )
{
	const double lo = std::min( low, high ), hi = std::max( low, high );
	return static_cast< float >( std::log( std::clamp( value, lo, hi ) / low ) / std::log( high / low ) );
}
double linear( float v, double low, double high )
{
	return low + ( high - low ) * clamp01( v );
}
float inverseLinear( double value, double low, double high )
{
	return static_cast< float >( std::clamp( ( value - low ) / ( high - low ), 0.0, 1.0 ) );
}

constexpr double kIntervalLow = 5.0, kIntervalHigh = 300.0;
constexpr float kIntervalOff  = 0.999f;
constexpr double kDriftHigh   = 1.0;
constexpr double kNarcHigh    = 2.0;
constexpr double kPlateauLow = 1e-5, kPlateauHigh = 1.0;
constexpr double kLevelLow = 15.0, kLevelHigh = 40.0;
constexpr double kWidthLow = 0.5, kWidthHigh = 30.0;
constexpr double kHaloHigh = 3.0;

const char* const kRateNames[]    = { "9 Hz", "30 Hz", "60 Hz" };
const char* const kSpanNames[]    = { "Auto", "Manual" };
const char* const kPaletteNames[] = { "White Hot", "Black Hot", "Ironbow", "Rainbow" };

ControlInfo standard( unsigned int id, const char* name, const char* group, float value )
{
	return { id, name, group, Kind::Standard, value, nullptr, 0 };
}
ControlInfo option( unsigned int id, const char* name, const char* group, float value, const char* const* names, int count )
{
	return { id, name, group, Kind::Option, value, names, count };
}
ControlInfo event( unsigned int id, const char* name, const char* group )
{
	return { id, name, group, Kind::Event, 0.0f, nullptr, 0 };
}

//---------------------------------------------------------------------------
// The defaults. Frame Rate 9 Hz is the idea; a shutter every 30 s so a clip
// shows one (the cores' own defaults are minutes); Plateau 7% is Boson's
// factory value; Manual's window spans the whole mapped range. Drift,
// Narcissus, Palette and Halo were chosen over Resolume's demo clips through
// --pipe (AGENTS.md says how).
//---------------------------------------------------------------------------
const ControlInfo kTable[] = {
	option( PT_FRAME_RATE, "Frame Rate", "Sensor", 0.0f, kRateNames, static_cast< int >( FrameRate::Count ) ),
	standard( PT_SHUTTER_INTERVAL, "Shutter Interval", "Sensor", ParamFromShutterInterval( 30.0 ) ),
	event( PT_SHUTTER, "Shutter", "Sensor" ),
	standard( PT_DRIFT, "Drift", "Sensor", ParamFromDrift( 0.3 ) ),
	standard( PT_NARCISSUS, "Narcissus", "Sensor", ParamFromNarcissus( 0.3 ) ),

	option( PT_SPAN, "Span", "Span", 0.0f, kSpanNames, static_cast< int >( Span::Count ) ),
	standard( PT_PLATEAU, "Plateau", "Span", ParamFromPlateau( 0.07 ) ),
	standard( PT_LEVEL, "Level", "Span", ParamFromLevel( 27.5 ) ),
	standard( PT_WIDTH, "Width", "Span", ParamFromWidth( 25.0 ) ),

	option( PT_PALETTE, "Palette", "Look", static_cast< float >( Palette::WhiteHot ), kPaletteNames, static_cast< int >( Palette::Count ) ),
	standard( PT_HALO, "Halo", "Look", ParamFromHalo( 1.0 ) ),
	standard( PT_MIX, "Mix", "Look", 1.0f ),
};
static_assert( sizeof( kTable ) / sizeof( kTable[ 0 ] ) == PT_ABOUT_TEXT, "every control needs a row in the table" );
} // namespace

const ControlInfo& InfoOf( unsigned int id )
{
	return kTable[ std::min< unsigned int >( id, PT_ABOUT_TEXT - 1 ) ];
}

int OptionIndex( float value, int count )
{
	const long i = std::lround( static_cast< double >( value ) );
	return static_cast< int >( std::clamp< long >( i, 0, count - 1 ) );
}

double FrameRateHz( int option )
{
	switch( static_cast< FrameRate >( std::clamp( option, 0, static_cast< int >( FrameRate::Count ) - 1 ) ) )
	{
	case FrameRate::Export: return 9.0;
	case FrameRate::Hz30: return 30.0;
	case FrameRate::Hz60: return 60.0;
	case FrameRate::Count: break;
	}
	return 9.0;
}

double ShutterIntervalFromParam( float v )
{
	if( v >= kIntervalOff )
		return std::numeric_limits< double >::infinity();
	return geometric( v, kIntervalLow, kIntervalHigh );
}
float ParamFromShutterInterval( double seconds )
{
	if( !std::isfinite( seconds ) )
		return 1.0f;
	return std::min( inverseGeometric( seconds, kIntervalLow, kIntervalHigh ), 0.998f );
}
double DriftFromParam( float v )
{
	return linear( v, 0.0, kDriftHigh );
}
float ParamFromDrift( double k )
{
	return inverseLinear( k, 0.0, kDriftHigh );
}
double NarcissusFromParam( float v )
{
	return linear( v, 0.0, kNarcHigh );
}
float ParamFromNarcissus( double k )
{
	return inverseLinear( k, 0.0, kNarcHigh );
}
double PlateauFromParam( float v )
{
	return geometric( v, kPlateauLow, kPlateauHigh );
}
float ParamFromPlateau( double f )
{
	return inverseGeometric( f, kPlateauLow, kPlateauHigh );
}
double LevelFromParam( float v )
{
	return linear( v, kLevelLow, kLevelHigh );
}
float ParamFromLevel( double c )
{
	return inverseLinear( c, kLevelLow, kLevelHigh );
}
double WidthFromParam( float v )
{
	return geometric( v, kWidthLow, kWidthHigh );
}
float ParamFromWidth( double k )
{
	return inverseGeometric( k, kWidthLow, kWidthHigh );
}
double HaloFromParam( float v )
{
	return linear( v, 0.0, kHaloHigh );
}
float ParamFromHalo( double a )
{
	return inverseLinear( a, 0.0, kHaloHigh );
}

} // namespace bolometer
