#pragma once

#include "Camera.h"
#include "Controls.h"
#include "PassBuffer.h"

#include <FFGLSDK.h>
// After FFGLSDK.h, which is where FFUInt32 comes from.
#include "StoatworksAboutParams.h"

#include <utility>
#include <vector>

namespace bolometer
{

/**
    The plugin, `SW Bolometer`: a thermal camera with an uncooled
    microbolometer core, looking at the clip.

    Each frame: the clock, then the camera's clock (Camera: the sensor
    instants, the FFCs, the exact weights of the lag), then the passes
    (Shaders.h). The picture only changes when a sensor frame is read out;
    between readouts the held frame is composited over the clip.

    Host indices are the ParamIds, About last.
*/
class BolometerPlugin : public CFFGLPlugin
{
public:
	/// The negative controls the plugin itself carries (the camera's are in
	/// Camera::Hooks). Each is a wrong model a check must catch.
	struct TestHooks
	{
		bool floatClock        = false;///< the host's clock read in single precision
		int plateauModel       = 0;    ///< 1 no plateau; 2 the plateau applied to the cumulative
		int dampingModel       = 0;    ///< 1 per host frame; 2 none
		bool widthHalf         = false;///< Manual's Width read as a half-width
		double haloSigmaScale  = 1.0;  ///< the halo's Gaussian this much wider
		double haloSign        = 1.0;  ///< -1: the unsharp mask's sign flipped
		int narcissusModel     = 0;    ///< 1 its radius in frame widths; 2 removed by an FFC; 3 off centre
		int driftModel         = 0;    ///< 1 the pattern all per-pixel, no column part
		int paletteModel       = 0;    ///< 1 an index off by one; 2 the table reversed
		bool alphaPassThrough  = false;///< the clip's alpha handed through
		bool lumaIgnoresAlpha  = false;///< a transparent pixel keeps its colour's temperature
		bool resizeClearsState = false;///< a new raster starts from cleared buffers
		bool resizeResetsLut   = false;///< a new raster re-primes the AGC
	};

	BolometerPlugin();

	FFResult InitGL( const FFGLViewportStruct* viewport ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* input ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	char* GetTextParameter( unsigned int index ) override;
	/// The base class's stub fails, and a failed default deletes the instance
	/// -- so without this no real host can load the plugin.
	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	FFResult SetTime( double time ) override;

	/// Set a control by what it is.
	void SetById( unsigned int id, float value )
	{
		SetFloatParameter( id, value );
	}
	float GetById( unsigned int id ) const
	{
		return id < PT_COUNT ? params[ id ] : 0.0f;
	}

	/// What the camera is set to, from the controls.
	CameraSettings Settings() const;

	//-------------------------------------------------------------------
	// For the harness. Nothing in the plugin's own operation calls these.
	//-------------------------------------------------------------------
	TestHooks& Hooks()
	{
		return hooks;
	}
	Camera& CameraForTest()
	{
		return camera;
	}
	const CameraFrame& LastFrame() const
	{
		return lastFrame;
	}
	/// The bolometers' temperatures at the last host frame, R32F.
	GLuint StateTextureID() const
	{
		return state[ current ].TextureID();
	}
	/// The last readout (temperature + drift - narcissus), R32F.
	GLuint RawTextureID() const
	{
		return raw.TextureID();
	}
	/// The last display levels (before the halo), R32F.
	GLuint DisplayTextureID() const
	{
		return display.TextureID();
	}
	/// The held camera frame, RGBA8.
	GLuint ShownTextureID() const
	{
		return shown.TextureID();
	}
	/// The AGC's transfer, 512 x 1 RGBA32F: ( lo, hi ) per bin.
	GLuint LutTextureID() const
	{
		return lut[ currentLut ].TextureID();
	}
	/// The last histogram, 512 x 1 R32F, counts.
	GLuint HistogramTextureID() const
	{
		return histogram.TextureID();
	}
	double HostNow() const
	{
		return now;
	}
	/// The host clock's unit as settled: 1 seconds, 0.001 milliseconds, 0 not yet.
	double ClockScale() const
	{
		return clockScale;
	}
	void SetClockScaleForTest( double scale )
	{
		clockScale = scale;
	}

private:
	void UpdateClock();
	bool ensureBuffers( int width, int height );
	void resample( PassBuffer& from, PassBuffer& to );
	void render( const FFGLTextureStruct* input, const GLint* hostViewport, GLuint hostFBO, int width, int height );
	void draw( PassBuffer& target );

	GLint where( const ffglex::FFGLShader& shader, const char* name );

	float params[ PT_COUNT ] = {};

	Camera camera;
	CameraFrame lastFrame;
	TestHooks hooks;

	ffglex::FFGLShader programs[ static_cast< int >( 9 ) ];
	GLuint vao            = 0;
	GLuint paletteTexture = 0;
	PassBuffer state[ 2 ];
	int current = 0;
	PassBuffer raw, display, blurred, shown, histogram;
	PassBuffer lut[ 2 ];
	int currentLut  = 0;
	bool primeLut   = false;
	int bufferWidth = 0, bufferHeight = 0;
	std::vector< std::pair< std::pair< GLuint, const char* >, GLint > > uniformCache;

	// The clock (ferrofluid's vote on seconds or milliseconds, fountain's
	// offline fallback).
	double hostTime     = -1.0;
	double clockScale   = 0.0;
	double lastRawTime  = -1.0;
	double lastWallTime = -1.0;
	double wallStart    = -1.0;
	int secondsVotes    = 0;
	int millisVotes     = 0;
	int clockSamples    = 0;
	int clockFrames     = 0;
	bool settledJump    = false;
	double now          = 0.0;

	int shutterPresses = 0;
	bool shutterHeld   = false;
};

} // namespace bolometer
