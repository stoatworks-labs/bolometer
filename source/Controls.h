#pragma once

#include "StoatworksAboutLinks.h"

/**
    The host's parameters, and what they mean in physical units.

    Every ranged parameter the host sees is 0..1, because `SetParamInfo`
    clamps an `FF_TYPE_STANDARD` default into 0..1 before `SetParamRange`
    could widen it. The conversions live in Controls.cpp, one function per
    control, each with its inverse where a default or the harness needs it.
    Option parameters hold the element index (an option reads back 0..1
    whatever its count: map by index); Shutter is a press.

    Units: hertz for Frame Rate, seconds for Shutter Interval, kelvin per
    minute for Drift, kelvin for Narcissus (its NITD), Level and Width, a
    fraction of the histogram's samples for Plateau, the unsharp gain a for
    Halo.
*/
namespace bolometer
{

/// Every control, by what it is. The host sees them in this order, with the
/// About block last.
enum ParamId : unsigned int
{
	// -- Sensor ------------------------------------------------------------------
	PT_FRAME_RATE = 0,
	PT_SHUTTER_INTERVAL,
	PT_SHUTTER,
	PT_DRIFT,
	PT_NARCISSUS,

	// -- Span --------------------------------------------------------------------
	PT_SPAN,
	PT_PLATEAU,
	PT_LEVEL,
	PT_WIDTH,

	// -- Look --------------------------------------------------------------------
	PT_PALETTE,
	PT_HALO,
	PT_MIX,

	// -- The Stoatworks About block: a text line, then one button per link.
	PT_ABOUT_TEXT,
	PT_COUNT = PT_ABOUT_TEXT + 1 + stoatworks::about::kButtonCount
};

enum class Kind
{
	Standard,
	Option,
	Event
};

/// One control as the host is told about it.
struct ControlInfo
{
	unsigned int id;
	const char* name;///< at most 16 characters: FFGL's name field is not terminated
	const char* group;
	Kind kind;
	float defaultValue;///< the host's 0..1, or the element index
	const char* const* options;
	int optionCount;
};

/// The table, indexed by ParamId, for every id below PT_ABOUT_TEXT.
const ControlInfo& InfoOf( unsigned int id );

/// An option's index from the value the host stored.
int OptionIndex( float value, int count );

enum class FrameRate
{
	Export = 0,///< 9 Hz, the US export limit (EAR 6A003.b.4.b Note 3)
	Hz30,
	Hz60,
	Count
};
enum class Span
{
	Auto = 0,///< plateau equalisation
	Manual,  ///< Level and Width
	Count
};
enum class Palette
{
	WhiteHot = 0,
	BlackHot,
	Ironbow,
	Rainbow,
	Count
};

//---------------------------------------------------------------------------
// Conversions, 0..1 to physical and back.
//---------------------------------------------------------------------------
double FrameRateHz( int option );///< 9, 30 or 60
/// Seconds between FFCs, 5..300 geometric; the top of the slider (>= 0.999)
/// is Off, +infinity.
double ShutterIntervalFromParam( float v );
float ParamFromShutterInterval( double seconds );
double DriftFromParam( float v );///< K per minute, 0..1, linear
float ParamFromDrift( double kPerMinute );
double NarcissusFromParam( float v );///< K (NITD), 0..2, linear
float ParamFromNarcissus( double kelvin );
double PlateauFromParam( float v );///< a fraction of the samples, 1e-5..1, geometric
float ParamFromPlateau( double fraction );
double LevelFromParam( float v );///< degC, 15..40, linear
float ParamFromLevel( double celsius );
double WidthFromParam( float v );///< K, 0.5..30, geometric
float ParamFromWidth( double kelvin );
double HaloFromParam( float v );///< the unsharp gain a, 0..3, linear
float ParamFromHalo( double gain );

} // namespace bolometer
