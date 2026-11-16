#include "Bolometer.h"

#include "Diag.h"
#include "GLState.h"
#include "Model.h"
#include "Palettes.h"
#include "Shaders.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>

using namespace ffglex;

namespace bolometer
{
namespace
{
/// Frames that must agree before the host's clock unit is settled.
constexpr int kClockVotes = 4;
/// Frames of advancing host time after which an unsettled vote is decided by
/// the size of the host's step alone (UpdateClock).
constexpr int kClockPatience = 30;

constexpr int kPrograms = static_cast< int >( shaders::Pass::Count );

double wallSeconds()
{
	using namespace std::chrono;
	static const steady_clock::time_point start = steady_clock::now();
	return duration_cast< duration< double > >( steady_clock::now() - start ).count();
}

std::string glStringOrUnknown( GLenum name )
{
	const GLubyte* value = glGetString( name );
	return value ? reinterpret_cast< const char* >( value ) : "unknown";
}
} // namespace

static_assert( PT_COUNT - PT_ABOUT_TEXT == stoatworks::about::kParamCount, "the About run no longer matches StoatworksAbout.h" );
static_assert( kPrograms == 9, "Bolometer.h sizes programs[] by hand" );

//---------------------------------------------------------------------------
BolometerPlugin::BolometerPlugin()
{
	SetMinInputs( 1 );
	SetMaxInputs( 1 );

	//The camera keeps time: the thermal lag, the sensor clock and the
	//shutter are all in seconds.
	SetTimeSupported( true );

	for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
		params[ id ] = InfoOf( id ).defaultValue;

	for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
	{
		const ControlInfo& info = InfoOf( id );
		switch( info.kind )
		{
		case Kind::Standard: SetParamInfo( id, info.name, FF_TYPE_STANDARD, params[ id ] ); break;
		case Kind::Option:
			SetOptionParamInfo( id, info.name, static_cast< unsigned int >( info.optionCount ), params[ id ] );
			for( int i = 0; i < info.optionCount; ++i )
				SetParamElementInfo( id, static_cast< unsigned int >( i ), info.options[ i ], static_cast< float >( i ) );
			break;
		case Kind::Event: SetParamInfo( id, info.name, FF_TYPE_EVENT, false ); break;
		}
		SetParamGroup( id, info.group );
	}

	// The About block, last. Inline rather than through a helper: SetParamInfo
	// is protected on CFFGLPlugin, so nothing outside the class can call it.
	SetParamInfo( PT_ABOUT_TEXT, "About", FF_TYPE_TEXT, stoatworks::about::defaultText() );
	{
		FFUInt32 aboutId = PT_ABOUT_TEXT + 1;
		for( const auto& b : stoatworks::about::buttons() )
			SetParamInfo( aboutId++, b.label, FF_TYPE_EVENT, false );
	}
	for( FFUInt32 i = PT_ABOUT_TEXT; i < PT_COUNT; ++i )
		SetParamGroup( i, "About" );
}

//---------------------------------------------------------------------------
GLint BolometerPlugin::where( const FFGLShader& shader, const char* name )
{
	//Keyed by the literal's address: every name here is a string literal.
	const auto key = std::make_pair( shader.GetGLID(), name );
	for( const auto& entry : uniformCache )
		if( entry.first == key )
			return entry.second;
	const GLint location = glGetUniformLocation( shader.GetGLID(), name );
	uniformCache.emplace_back( key, location );
	return location;
}

//---------------------------------------------------------------------------
FFResult BolometerPlugin::InitGL( const FFGLViewportStruct* vp )
{
	diag::init();
	diag::info( std::string( "GL vendor=" ) + glStringOrUnknown( GL_VENDOR ) + " renderer=" + glStringOrUnknown( GL_RENDERER )
	            + " version=" + glStringOrUnknown( GL_VERSION ) );

	const char* const names[ kPrograms ] = { "readout", "state", "histogram", "lut", "display", "blur", "shown", "composite", "resample" };
	for( int i = 0; i < kPrograms; ++i )
	{
		const shaders::Pass pass   = static_cast< shaders::Pass >( i );
		const std::string vertex   = shaders::AssembleVertex( pass );
		const std::string fragment = shaders::Assemble( pass );
		if( programs[ i ].Compile( vertex.c_str(), fragment.c_str() ) )
			continue;
		//Returning FF_FAIL is invisible to the operator: the plugin simply
		//does nothing in Resolume. These lines are the only record of which
		//pass it was.
		diag::error( std::string( "the " ) + names[ i ] + " shader failed to compile - the plugin will do nothing" );
		FFGLLog::LogToHost( "Bolometer: shader failed to compile" );
		DeInitGL();
		return FF_FAIL;
	}

	SavedGLState saved;
	saved.Capture();
	//A core profile draws nothing without a vertex array bound; every pass
	//makes its vertices from gl_VertexID.
	glGenVertexArrays( 1, &vao );

	//The palettes: one 256-entry row each, read by texelFetch.
	unsigned char table[ palettes::kPalettes ][ palettes::kEntries ][ 4 ];
	palettes::Build( table );
	glGenTextures( 1, &paletteTexture );
	glBindTexture( GL_TEXTURE_2D, paletteTexture );
	glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, palettes::kEntries, palettes::kPalettes, 0, GL_RGBA, GL_UNSIGNED_BYTE, table );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glBindTexture( GL_TEXTURE_2D, 0 );
	saved.Restore();

	camera.Reprime();
	bufferWidth = bufferHeight = 0;
	diag::info( "initialised" );
	return CFFGLPlugin::InitGL( vp );
}

//---------------------------------------------------------------------------
CameraSettings BolometerPlugin::Settings() const
{
	CameraSettings s;
	s.rate      = FrameRateHz( OptionIndex( params[ PT_FRAME_RATE ], static_cast< int >( FrameRate::Count ) ) );
	s.interval  = ShutterIntervalFromParam( params[ PT_SHUTTER_INTERVAL ] );
	s.driftRate = DriftFromParam( params[ PT_DRIFT ] ) / 60.0;
	return s;
}

//---------------------------------------------------------------------------
void BolometerPlugin::draw( PassBuffer& target )
{
	glBindFramebuffer( GL_FRAMEBUFFER, target.GetGLID() );
	glViewport( 0, 0, static_cast< GLsizei >( target.GetWidth() ), static_cast< GLsizei >( target.GetHeight() ) );
	glDrawArrays( GL_TRIANGLES, 0, 3 );
}

void BolometerPlugin::resample( PassBuffer& from, PassBuffer& to )
{
	const FFGLShader& program = programs[ static_cast< int >( shaders::Pass::Resample ) ];
	glUseProgram( program.GetGLID() );
	bindUnit( 0, from.TextureID() );
	glUniform1i( where( program, "Src" ), 0 );
	glUniform2i( where( program, "SrcSize" ), static_cast< GLint >( from.GetWidth() ), static_cast< GLint >( from.GetHeight() ) );
	glUniform2i( where( program, "DstSize" ), static_cast< GLint >( to.GetWidth() ), static_cast< GLint >( to.GetHeight() ) );
	draw( to );
}

//---------------------------------------------------------------------------
bool BolometerPlugin::ensureBuffers( int width, int height )
{
	const auto Nearest = PassBuffer::Sampling::Nearest;
	if( !histogram.Ensure( model::kBins, 1, GL_R32F, Nearest ) || !lut[ 0 ].Ensure( model::kBins, 1, GL_RGBA32F, Nearest )
	    || !lut[ 1 ].Ensure( model::kBins, 1, GL_RGBA32F, Nearest ) )
		return false;

	const bool resized = bufferWidth > 0 && ( width != bufferWidth || height != bufferHeight );
	if( resized && !hooks.resizeClearsState && state[ current ].IsValid() && shown.IsValid() )
	{
		//The thermal state and the held frame survive a raster change: each is
		//resampled into a buffer of the new size, which then replaces it.
		PassBuffer nextState, nextShown;
		if( !nextState.Ensure( width, height, GL_R32F, Nearest ) || !nextShown.Ensure( width, height, GL_RGBA8, Nearest ) )
			return false;
		glBindVertexArray( vao );
		resample( state[ current ], nextState );
		resample( shown, nextShown );
		state[ current ].TakeFrom( nextState );
		shown.TakeFrom( nextShown );
		state[ 1 - current ].Destroy();
	}
	if( resized && hooks.resizeResetsLut )
		primeLut = true;

	const bool ok = state[ 0 ].Ensure( width, height, GL_R32F, Nearest ) && state[ 1 ].Ensure( width, height, GL_R32F, Nearest )
	                && shown.Ensure( width, height, GL_RGBA8, Nearest ) && raw.Ensure( width, height, GL_R32F, Nearest )
	                && display.Ensure( width, height, GL_R32F, Nearest ) && blurred.Ensure( width, height, GL_R32F, Nearest );
	if( ok )
	{
		bufferWidth  = width;
		bufferHeight = height;
	}
	return ok;
}

//---------------------------------------------------------------------------
void BolometerPlugin::render( const FFGLTextureStruct* input, const GLint* hostViewport, GLuint hostFBO, int width, int height )
{
	const CameraFrame& f = lastFrame;
	glBindVertexArray( vao );

	const int lumaModel = hooks.lumaIgnoresAlpha ? 1 : 0;
	auto setScene       = [ & ]( const FFGLShader& p, const Weights& w ) {
		glUniform1i( where( p, "InputTexture" ), 0 );
		glUniform1i( where( p, "State" ), 1 );
		glUniform3f( where( p, "Weights" ), static_cast< float >( w.prev ), static_cast< float >( w.scene ), static_cast< float >( w.shutter ) );
		glUniform1f( where( p, "ShutterTemp" ), static_cast< float >( model::kShutterTemp ) );
		glUniform1f( where( p, "TempLow" ), static_cast< float >( model::kTempLow ) );
		glUniform1f( where( p, "TempSpan" ), static_cast< float >( model::kTempSpan ) );
		glUniform1i( where( p, "LumaModel" ), lumaModel );
	};

	//The readout, from the state at the last host frame (before it moves).
	if( f.readout )
	{
		const FFGLShader& p = programs[ static_cast< int >( shaders::Pass::Readout ) ];
		glUseProgram( p.GetGLID() );
		bindUnit( 0, input->Handle );
		bindUnit( 1, state[ current ].TextureID() );
		setScene( p, f.sample );
		double depth = NarcissusFromParam( params[ PT_NARCISSUS ] );
		if( hooks.narcissusModel == 2 && f.ffcsCompleted > 0 )
			depth = 0.0;
		const double radius = model::kNarcissusR * ( hooks.narcissusModel == 1 ? width : height );
		const double cx = 0.5 * width + ( hooks.narcissusModel == 3 ? 0.05 * height : 0.0 ), cy = 0.5 * height;
		glUniform1f( where( p, "DriftAmp" ), static_cast< float >( f.driftAmplitude ) );
		glUniform1ui( where( p, "DriftSeed" ), f.driftSeed );
		glUniform1f( where( p, "NarcDepth" ), static_cast< float >( depth ) );
		glUniform2f( where( p, "NarcCentre" ), static_cast< float >( cx ), static_cast< float >( cy ) );
		glUniform1f( where( p, "NarcR" ), static_cast< float >( radius ) );
		glUniform1f( where( p, "NarcB" ), static_cast< float >( model::kNarcissusB * height ) );
		draw( raw );
	}

	//The bolometers at the host frame's time, into the other half.
	{
		const FFGLShader& p = programs[ static_cast< int >( shaders::Pass::State ) ];
		glUseProgram( p.GetGLID() );
		bindUnit( 0, input->Handle );
		bindUnit( 1, state[ current ].TextureID() );
		setScene( p, f.state );
		draw( state[ 1 - current ] );
		current = 1 - current;
	}

	if( f.readout )
	{
		const double histScale = model::kBins / ( model::kHistHigh - model::kHistLow );

		//The histogram of the readout on the fixed grid: exact counts.
		{
			const FFGLShader& p = programs[ static_cast< int >( shaders::Pass::Histogram ) ];
			glBindFramebuffer( GL_FRAMEBUFFER, histogram.GetGLID() );
			glViewport( 0, 0, model::kBins, 1 );
			glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
			glClear( GL_COLOR_BUFFER_BIT );
			glUseProgram( p.GetGLID() );
			bindUnit( 0, raw.TextureID() );
			bindUnit( 1, 0 );
			glUniform1i( where( p, "Raw" ), 0 );
			glUniform2i( where( p, "Size" ), width, height );
			glUniform2i( where( p, "Grid" ), model::kGridW, model::kGridH );
			glUniform1f( where( p, "HistLow" ), static_cast< float >( model::kHistLow ) );
			glUniform1f( where( p, "HistScale" ), static_cast< float >( histScale ) );
			glUniform1i( where( p, "Bins" ), model::kBins );
			glEnable( GL_BLEND );
			glBlendFunc( GL_ONE, GL_ONE );
			glEnable( GL_PROGRAM_POINT_SIZE );
			glDrawArrays( GL_POINTS, 0, model::kGridCount );
			glDisable( GL_PROGRAM_POINT_SIZE );
			glDisable( GL_BLEND );
		}

		//The transfer, damped against the last one by d^m.
		{
			const FFGLShader& p = programs[ static_cast< int >( shaders::Pass::Lut ) ];
			glUseProgram( p.GetGLID() );
			bindUnit( 0, histogram.TextureID() );
			bindUnit( 1, lut[ currentLut ].TextureID() );
			double keep = std::pow( model::kDamping, f.fresh );
			if( hooks.dampingModel == 1 )
				keep = model::kDamping;
			if( hooks.dampingModel == 2 || f.prime || primeLut )
				keep = 0.0;
			const double plateau = PlateauFromParam( params[ PT_PLATEAU ] ) * model::kGridCount;
			glUniform1i( where( p, "Histogram" ), 0 );
			glUniform1i( where( p, "PrevLut" ), 1 );
			glUniform1f( where( p, "Plateau" ), static_cast< float >( plateau ) );
			glUniform1f( where( p, "Keep" ), static_cast< float >( keep ) );
			glUniform1i( where( p, "Bins" ), model::kBins );
			glUniform1i( where( p, "PlateauModel" ), hooks.plateauModel );
			draw( lut[ 1 - currentLut ] );
			currentLut = 1 - currentLut;
			primeLut   = false;
		}

		//The display level.
		{
			const FFGLShader& p = programs[ static_cast< int >( shaders::Pass::Display ) ];
			glUseProgram( p.GetGLID() );
			bindUnit( 0, raw.TextureID() );
			bindUnit( 1, lut[ currentLut ].TextureID() );
			const bool autoSpan = OptionIndex( params[ PT_SPAN ], static_cast< int >( Span::Count ) ) == static_cast< int >( Span::Auto );
			const double widthK = WidthFromParam( params[ PT_WIDTH ] ) * ( hooks.widthHalf ? 2.0 : 1.0 );
			glUniform1i( where( p, "Raw" ), 0 );
			glUniform1i( where( p, "Lut" ), 1 );
			glUniform1i( where( p, "Auto" ), autoSpan ? 1 : 0 );
			glUniform1f( where( p, "Level" ), static_cast< float >( LevelFromParam( params[ PT_LEVEL ] ) ) );
			glUniform1f( where( p, "Width" ), static_cast< float >( widthK ) );
			glUniform1f( where( p, "HistLow" ), static_cast< float >( model::kHistLow ) );
			glUniform1f( where( p, "HistScale" ), static_cast< float >( histScale ) );
			glUniform1i( where( p, "Bins" ), model::kBins );
			draw( display );
		}

		//The halo (the detail enhancement's unsharp mask), the core's 8-bit
		//output and the palette, into the held frame.
		const double gain   = HaloFromParam( params[ PT_HALO ] ) * hooks.haloSign;
		const double sigma  = model::kHaloSigma * height * hooks.haloSigmaScale;
		const int radius    = static_cast< int >( std::ceil( model::kHaloCut * sigma ) );
		if( gain != 0.0 )
		{
			const FFGLShader& p = programs[ static_cast< int >( shaders::Pass::Blur ) ];
			glUseProgram( p.GetGLID() );
			bindUnit( 0, display.TextureID() );
			bindUnit( 1, 0 );
			glUniform1i( where( p, "Src" ), 0 );
			glUniform2i( where( p, "Dir" ), 1, 0 );
			glUniform1i( where( p, "Radius" ), radius );
			glUniform1f( where( p, "Sigma" ), static_cast< float >( sigma ) );
			glUniform2i( where( p, "Size" ), width, height );
			draw( blurred );
		}
		{
			const FFGLShader& p = programs[ static_cast< int >( shaders::Pass::Shown ) ];
			glUseProgram( p.GetGLID() );
			bindUnit( 0, display.TextureID() );
			bindUnit( 1, blurred.TextureID() );
			bindUnit( 2, paletteTexture );
			glUniform1i( where( p, "Y" ), 0 );
			glUniform1i( where( p, "BlurH" ), 1 );
			glUniform1i( where( p, "PaletteTable" ), 2 );
			glUniform1i( where( p, "Radius" ), radius );
			glUniform1f( where( p, "Sigma" ), static_cast< float >( sigma ) );
			glUniform2i( where( p, "Size" ), width, height );
			glUniform1f( where( p, "HaloGain" ), static_cast< float >( gain ) );
			glUniform1i( where( p, "PaletteRow" ), OptionIndex( params[ PT_PALETTE ], static_cast< int >( Palette::Count ) ) );
			glUniform1i( where( p, "PaletteModel" ), hooks.paletteModel );
			draw( shown );
		}
	}

	//The picture, into the host's framebuffer and viewport.
	{
		const FFGLShader& p = programs[ static_cast< int >( shaders::Pass::Composite ) ];
		glBindFramebuffer( GL_FRAMEBUFFER, hostFBO );
		glViewport( hostViewport[ 0 ], hostViewport[ 1 ], hostViewport[ 2 ], hostViewport[ 3 ] );
		glUseProgram( p.GetGLID() );
		bindUnit( 0, input->Handle );
		bindUnit( 1, shown.TextureID() );
		bindUnit( 2, 0 );
		glUniform1i( where( p, "InputTexture" ), 0 );
		glUniform1i( where( p, "Shown" ), 1 );
		glUniform2i( where( p, "ViewOrigin" ), hostViewport[ 0 ], hostViewport[ 1 ] );
		glUniform1f( where( p, "MixAmount" ), std::clamp( params[ PT_MIX ], 0.0f, 1.0f ) );
		glUniform1i( where( p, "AlphaModel" ), hooks.alphaPassThrough ? 1 : 0 );
		glDrawArrays( GL_TRIANGLES, 0, 3 );
	}
	unbindTextureUnits( 3 );
	glActiveTexture( GL_TEXTURE0 );
	glUseProgram( 0 );
}

//---------------------------------------------------------------------------
void BolometerPlugin::UpdateClock()
{
	const double wallNow = wallSeconds();
	if( wallStart < 0.0 )
		wallStart = wallNow;
	const double raw_ = hostTime;

	//Resolume has been seen sending both seconds and milliseconds through
	//SetTime, and nothing in the call says which. Vote on it against the wall
	//clock for a few frames, then stop asking (ferrofluid's, by way of
	//rosette, millpond and filings).
	if( clockScale == 0.0 && raw_ >= 0.0 && lastRawTime >= 0.0 && lastWallTime >= 0.0 )
	{
		const double hostDelta = raw_ - lastRawTime;
		const double wallDelta = wallNow - lastWallTime;
		if( hostDelta > 0.0 && wallDelta >= 0.0005 )
		{
			const double ratio = hostDelta / wallDelta;
			if( ratio > 0.1 && ratio < 10.0 )
				++secondsVotes;
			else if( ratio > 100.0 && ratio < 10000.0 )
				++millisVotes;
			if( secondsVotes >= kClockVotes || millisVotes >= kClockVotes )
			{
				clockScale  = millisVotes > secondsVotes ? 0.001 : 1.0;
				settledJump = true;//wall time to the host's own: start the camera again
			}
		}
		//A host that renders much faster than real time -- an offline render,
		//oxbow's selftest, bmtest -- never wins the vote. After kClockPatience
		//frames the step's size decides: a frame is well under a second and
		//well over a millisecond (fountain's).
		if( hostDelta > 0.0 && clockScale == 0.0 && ++clockSamples >= kClockPatience )
		{
			clockScale  = hostDelta < 1.0 ? 1.0 : 0.001;
			settledJump = true;
		}
	}
	if( raw_ >= 0.0 )
		lastRawTime = raw_;
	lastWallTime = wallNow;

	//Double throughout: Resolume's clock has been measured at ~499 million ms,
	//where a float resolves 0.03 s. The camera only ever subtracts.
	now = ( raw_ >= 0.0 && clockScale != 0.0 ) ? raw_ * clockScale : wallNow - wallStart;
	if( hooks.floatClock )
		now = static_cast< double >( static_cast< float >( now ) );
	if( ++clockFrames == 60 )
		diag::info( "host clock at frame 60: raw=" + std::to_string( raw_ ) + " scale=" + std::to_string( clockScale )
		            + " seconds=" + std::to_string( now ) );
}

//---------------------------------------------------------------------------
FFResult BolometerPlugin::ProcessOpenGL( ProcessOpenGLStruct* pgl )
{
	if( pgl == nullptr || pgl->numInputTextures < 1 || pgl->inputTextures[ 0 ] == nullptr )
		return FF_FAIL;
	const FFGLTextureStruct* input = pgl->inputTextures[ 0 ];

	//The host's state, put back however this returns.
	ScopedGLState restore;
	const GLint* hostViewport = restore.saved.viewport;
	glDisable( GL_BLEND );

	const int width  = static_cast< int >( input->Width );
	const int height = static_cast< int >( input->Height );
	if( width <= 0 || height <= 0 )
		return FF_FAIL;

	UpdateClock();
	if( settledJump )
	{
		camera.Reprime();
		settledJump = false;
	}
	const bool press = shutterPresses > 0;
	shutterPresses   = 0;

	if( !ensureBuffers( width, height ) )
	{
		diag::error( "could not allocate the camera's buffers" );
		return FF_FAIL;
	}
	lastFrame = camera.Advance( now, Settings(), press );
	render( input, hostViewport, pgl->HostFBO, width, height );
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult BolometerPlugin::DeInitGL()
{
	uniformCache.clear();
	for( FFGLShader& p : programs )
		p.FreeGLResources();
	if( vao != 0 )
		glDeleteVertexArrays( 1, &vao );
	if( paletteTexture != 0 )
		glDeleteTextures( 1, &paletteTexture );
	vao = paletteTexture = 0;
	for( PassBuffer* b : { &state[ 0 ], &state[ 1 ], &raw, &display, &blurred, &shown, &histogram, &lut[ 0 ], &lut[ 1 ] } )
		b->Destroy();
	bufferWidth = bufferHeight = 0;
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult BolometerPlugin::SetTime( double time )
{
	hostTime = time;
	return FF_SUCCESS;
}

char* BolometerPlugin::GetTextParameter( unsigned int index )
{
	if( index == PT_ABOUT_TEXT )
	{
		static const std::string text = stoatworks::about::textParam( 0 );
		return const_cast< char* >( text.c_str() );
	}
	return CFFGLPlugin::GetTextParameter( index );
}

FFResult BolometerPlugin::SetTextParameter( unsigned int index, const char* value )
{
	if( index == PT_ABOUT_TEXT )
		return FF_SUCCESS;
	return CFFGLPlugin::SetTextParameter( index, value );
}

FFResult BolometerPlugin::SetFloatParameter( unsigned int index, float value )
{
	if( index >= PT_COUNT )
		return FF_FAIL;

	// The About buttons open a browser and store nothing.
	if( index >= PT_ABOUT_TEXT )
		return stoatworks::about::handleParam( index - PT_ABOUT_TEXT, value ) ? FF_SUCCESS : FF_FAIL;

	//Shutter acts on the press, once: an event arrives as 1 on press and 0 on
	//release, and a host is free to restate either.
	if( index == PT_SHUTTER )
	{
		const bool down = value >= 0.5f;
		if( down && !shutterHeld )
			++shutterPresses;
		shutterHeld = down;
	}
	params[ index ] = value;
	return FF_SUCCESS;
}

float BolometerPlugin::GetFloatParameter( unsigned int index )
{
	return index < PT_COUNT ? params[ index ] : 0.0f;
}

} // namespace bolometer
