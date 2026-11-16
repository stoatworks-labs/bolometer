// The checks: included by main.cpp inside its anonymous namespace.
//
// Every claim is measured out of the picture (the host's output) or the
// plugin's own buffers (the bolometers, the readout, the AGC's histogram and
// transfer, the held frame), and set against the harness's own derivation:
// integer arithmetic for the sensor clock and the FFC, the lag's solution or
// RK4 for the bolometers, the card's geometry for the histogram, moments for
// the halo, chord-by-chord geometry for the narcissus, the uniform
// distribution's moments for the drift, and anchors typed from upstream's
// tables for the palettes. None of them calls the plugin's own arithmetic.

constexpr double kTau        = 0.012;       // s: the spec's lag
constexpr double kShutterC   = 27.5;        // degC: the spec's shutter
constexpr double kFfcSeconds = 23.0 / 26.0; // the spec's FFC
constexpr double kOpenAfter  = 20.0 / 26.0; // the shutter opens 3/26 s before the FFC ends

/// The spec's temperature: 15 + 25 x Rec. 709 luma x alpha.
double tempOf( double r, double g, double b, double a = 1.0 )
{
	return 15.0 + 25.0 * ( 0.2126 * r + 0.7152 * g + 0.0722 * b ) * a;
}
double greyFor( double T )
{
	return ( T - 15.0 ) / 25.0;
}

/// One texel of a plugin buffer, by a framebuffer of the harness's own.
GLuint g_readFBO = 0;
double readTexel( GLuint texture, int x, int y )
{
	if( g_readFBO == 0 )
		glGenFramebuffers( 1, &g_readFBO );
	GLint previous = 0;
	glGetIntegerv( GL_READ_FRAMEBUFFER_BINDING, &previous );
	glBindFramebuffer( GL_READ_FRAMEBUFFER, g_readFBO );
	glFramebufferTexture2D( GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0 );
	float v[ 4 ] = {};
	glPixelStorei( GL_PACK_ALIGNMENT, 1 );
	glReadPixels( x, y, 1, 1, GL_RGBA, GL_FLOAT, v );
	glFramebufferTexture2D( GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0 );
	glBindFramebuffer( GL_READ_FRAMEBUFFER, static_cast< GLuint >( previous ) );
	return v[ 0 ];
}

size_t differing( const Floats& a, const Floats& b )
{
	size_t n = 0;
	for( size_t i = 0; i < a.size() && i < b.size(); ++i )
		n += a[ i ] != b[ i ];
	return n + ( a.size() > b.size() ? a.size() - b.size() : b.size() - a.size() );
}

int code( float v )
{
	return static_cast< int >( std::lround( std::clamp( v, 0.0f, 1.0f ) * 255.0f ) );
}

//===========================================================================
// Rigs.
//===========================================================================

/// Every control back at its shipped default.
void defaults( Rig& rig )
{
	for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
	{
		const ControlInfo& info = InfoOf( id );
		if( info.kind == Kind::Standard || info.kind == Kind::Option )
			rig.Set( id, info.defaultValue );
	}
}

/// The quiet camera: the defaults, then no drift, no narcissus, no halo, no
/// FFC, Mix 1, White Hot, and Span Manual with Level 27.5 and Width 25 -- so
/// a pixel's level is the clip's grey (T = 15 + 25 g, level = ( T - 27.5 ) /
/// 25 + 0.5 = g). Every check then sets what it measures (honeydew's lesson).
void quiet( Rig& rig )
{
	defaults( rig );
	rig.Set( PT_DRIFT, 0.0f );
	rig.Set( PT_NARCISSUS, 0.0f );
	rig.Set( PT_HALO, 0.0f );
	rig.Set( PT_SHUTTER_INTERVAL, 1.0f );
	rig.Set( PT_MIX, 1.0f );
	rig.Set( PT_PALETTE, static_cast< int >( Palette::WhiteHot ) );
	rig.Set( PT_SPAN, static_cast< int >( Span::Manual ) );
	rig.Set( PT_LEVEL, ParamFromLevel( 27.5 ) );
	rig.Set( PT_WIDTH, ParamFromWidth( 25.0 ) );
	rig.Set( PT_FRAME_RATE, static_cast< int >( FrameRate::Export ) );
}

bool startRig( Rig& rig, int w, int h, const Perturb& perturb, const Floats* picture = nullptr, double fps = 60.0 )
{
	rig.fps = fps;
	if( !rig.Init( w, h, picture ) )
		return false;
	rig.plugin.Hooks()                = perturb.plugin;
	rig.plugin.CameraForTest().hooks = perturb.camera;
	quiet( rig );
	return true;
}

/// Bars that move every host frame on the left half, a flat grey on the
/// right: the picture changes on every readout and holds between them, and
/// the right half's bolometers can be read against the lag.
void clockCard( Floats& card, int w, int h, int n, double flat )
{
	std::vector< float > column( static_cast< size_t >( w ) );
	for( int x = 0; x < w; ++x )
		column[ static_cast< size_t >( x ) ] =
			x < w / 2 ? static_cast< float >( 0.5 + 0.35 * std::sin( 2.0 * model::kPi * ( 3.0 * x / w + 0.37 * n ) ) ) : static_cast< float >( flat );
	card.resize( static_cast< size_t >( w ) * h * 4 );
	for( int x = 0; x < w; ++x )
	{
		float* o = &card[ static_cast< size_t >( x ) * 4 ];
		o[ 0 ] = o[ 1 ] = o[ 2 ] = column[ static_cast< size_t >( x ) ];
		o[ 3 ]                   = 1.0f;
	}
	for( int y = 1; y < h; ++y )
		std::memcpy( &card[ static_cast< size_t >( y ) * w * 4 ], card.data(), static_cast< size_t >( w ) * 4 * sizeof( float ) );
}

/// 32 vertical stripes, a temperature each. Each stripe covers 8 of the
/// AGC grid's 256 columns at any width divisible by 32.
Floats stripeCard( int w, int h, const std::vector< double >& temps )
{
	return greyCard( w, h, [ & ]( int x, int ) { return greyFor( temps[ static_cast< size_t >( x * 32 / w ) ] ); } );
}

/// A temperature in the middle of an AGC bin, at fraction u of it.
double binTemp( int k, double u )
{
	return model::kHistLow + ( k + u ) * ( model::kHistHigh - model::kHistLow ) / model::kBins;
}

std::vector< double > stripesA()
{
	const double bg = binTemp( 230, 0.5 ), hot = binTemp( 370, 0.8 ), cold = binTemp( 150, 0.3 ), warm = binTemp( 245, 0.7 ), body = binTemp( 290, 0.25 );
	std::vector< double > t( 32, bg );
	for( int s : { 2, 3, 4 } )
		t[ s ] = hot;
	t[ 10 ] = cold;
	t[ 20 ] = t[ 21 ] = warm;
	for( int s = 25; s <= 30; ++s )
		t[ s ] = body;
	return t;
}
std::vector< double > stripesB()
{
	const double bg = binTemp( 230, 0.5 ), hot = binTemp( 370, 0.8 ), cold = binTemp( 150, 0.3 );
	std::vector< double > t( 32, bg );
	for( int s = 4; s < 12; ++s )
		t[ s ] = hot;
	for( int s = 20; s < 24; ++s )
		t[ s ] = cold;
	return t;
}

/// The harness's histogram of a stripe card on the spec's grid: sample
/// ( i, j ) reads pixel ( ( 2i + 1 ) W / 512, ... ), in integers.
std::vector< double > stripeHistogram( int w, const std::vector< double >& temps )
{
	std::vector< double > h( model::kBins, 0.0 );
	for( int i = 0; i < model::kGridW; ++i )
	{
		const int px  = ( 2 * i + 1 ) * w / ( 2 * model::kGridW );
		const double T = temps[ static_cast< size_t >( px * 32 / w ) ];
		const int k   = std::clamp( static_cast< int >( std::floor( ( T - model::kHistLow ) * model::kBins / ( model::kHistHigh - model::kHistLow ) ) ), 0,
		                            model::kBins - 1 );
		h[ static_cast< size_t >( k ) ] += model::kGridH;
	}
	return h;
}

/// Plateau equalisation, from the spec: each bin clipped at P, the level of
/// T the clipped count below its bin plus its share of its own, over the
/// clipped total.
double equalised( const std::vector< double >& h, double plateau, double T )
{
	double total = 0.0;
	for( double c : h )
		total += std::min( c, plateau );
	const double pos = ( T - model::kHistLow ) * model::kBins / ( model::kHistHigh - model::kHistLow );
	const int k      = static_cast< int >( std::floor( pos ) );
	double below     = 0.0;
	for( int j = 0; j < k; ++j )
		below += std::min( h[ static_cast< size_t >( j ) ], plateau );
	return ( below + ( pos - k ) * std::min( h[ static_cast< size_t >( k ) ], plateau ) ) / total;
}

/// The readout index at host frame n of a clock at fps = num / den: the
/// latest instant at or before it, k = floor( n f den / num ), unless an FFC
/// froze it -- then the last before that FFC. Integers throughout.
long long expectedReadout( long long n, long long num, long long den, long long f, const std::vector< std::pair< long long, long long > >& frozen )
{
	long long k = n * f * den / num;
	for( auto it = frozen.rbegin(); it != frozen.rend(); ++it )
		if( k >= it->first && k < it->first + it->second )
			k = it->first - 1;
	return k;
}

/// The sensor frames an FFC freezes: those with t_k in [ c, c + 23/26 ),
/// ceil( 23 f / 26 ) of them (23 f / 26 is never a whole number here).
long long frozenCount( long long f )
{
	return ( 23 * f + 25 ) / 26;
}

//===========================================================================
// --thermal
//===========================================================================
int runThermal( const Perturb& perturb )
{
	std::printf( "\n=== thermal: the bolometers settle at T = 15 + 25 x luma x alpha, and follow a step with tau = 12 ms whatever the host's frame rate (24, 30, 60, "
	             "144 fps)\n" );
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		//The mapping: four patches, measured in the settled state.
		{
			Floats card( static_cast< size_t >( w ) * h * 4 );
			const float patch[ 4 ][ 4 ] = { { 0.8f, 0.8f, 0.8f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.8f, 0.8f, 0.8f, 0.5f }, { 0.3f, 0.9f, 0.6f, 0.0f } };
			for( int y = 0; y < h; ++y )
				for( int x = 0; x < w; ++x )
					std::memcpy( &card[ ( static_cast< size_t >( y ) * w + x ) * 4 ], patch[ x * 4 / w ], 4 * sizeof( float ) );
			Rig rig;
			if( !startRig( rig, w, h, perturb, &card ) || !rig.Render( 30 ) )
				return 1;
			const std::vector< double > T = rig.State();
			double worst                  = 0.0;
			double seen[ 4 ]              = {};
			for( int y = 0; y < h; ++y )
				for( int x = 0; x < w; ++x )
				{
					const float* p    = patch[ x * 4 / w ];
					const double want = tempOf( p[ 0 ], p[ 1 ], p[ 2 ], p[ 3 ] );
					const double got  = T[ static_cast< size_t >( y ) * w + x ];
					worst             = std::max( worst, std::fabs( got - want ) );
					seen[ x * 4 / w ] = got;
				}
			Check( worst <= 1e-4, fmt( "%4dx%-4d grey 0.8, red, grey 0.8 at alpha 0.5, a colour at alpha 0: %.5f %.5f %.5f %.5f degC (want 35, 20.315, 25, 15); "
			                           "worst pixel %.2e K off (tolerance 1e-4 K: float32 at 35 degC resolves 3.8e-6 K)",
			                           w, h, seen[ 0 ], seen[ 1 ], seen[ 2 ], seen[ 3 ], worst ) );
		}
		//The lag: a step from grey 0.2 to 0.8 half a second in, at four host rates.
		for( const int fps : { 24, 30, 60, 144 } )
		{
			const Floats low = flatCard( w, h, 0.2f, 0.2f, 0.2f ), high = flatCard( w, h, 0.8f, 0.8f, 0.8f );
			Rig rig;
			if( !startRig( rig, w, h, perturb, &low, fps ) )
				return 1;
			const int n0 = fps / 2;
			if( !rig.Render( n0 ) )
				return 1;
			const int cx = w / 2, cy = h / 2;
			const double S0 = readTexel( rig.plugin.StateTextureID(), cx, cy );
			rig.Upload( high );
			std::vector< double > seen;
			for( int m = 0; m < fps; ++m )
			{
				if( !rig.Render( 1 ) )
					return 1;
				seen.push_back( readTexel( rig.plugin.StateTextureID(), cx, cy ) );
			}
			const double S1 = seen.back();
			double worst = 0.0, first = 0.0;
			int used = 0;
			for( size_t m = 0; m < seen.size(); ++m )
			{
				if( std::fabs( S1 - seen[ m ] ) <= 0.01 )
					break;
				//Frame n0 + m is at ( n0 + m ) / fps; the step began after n0 - 1.
				const double elapsed = static_cast< double >( m + 1 ) / fps;
				const double tau     = -elapsed / std::log( ( S1 - seen[ m ] ) / ( S1 - S0 ) );
				if( m == 0 )
					first = tau;
				worst = std::max( worst, std::fabs( tau / kTau - 1.0 ) );
				++used;
			}
			Check( used >= 1 && worst <= 1e-3,
			       fmt( "%4dx%-4d %3d fps: a step of %.3f K; tau from the state %.5f ms at the first frame after it, %d frames used (residual > 0.01 K), worst "
			            "%.2e of 12 ms off (tolerance 1e-3: 10x float32's bound at a 0.01 K residual)",
			            w, h, fps, S1 - S0, first * 1e3, used, worst ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --clock
//===========================================================================
int runClock( const Perturb& perturb )
{
	std::printf( "\n=== clock: the picture changes on exactly the host frames after which a sensor instant k / f has passed (floor( n f / fps ) in integers), at 9, "
	             "30 and 60 Hz under 24, 30, 60 and 144 fps;\n    each readout is the bolometers at its instant, not at the host frame (from the readout "
	             "buffer); a host clock at 4.99e8 ms does the same; the unit settles offline\n" );
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		const int qx = 3 * w / 4, qy = h / 2;
		for( const int f : { 9, 30, 60 } )
			for( const int fps : { 24, 30, 60, 144 } )
			{
				Rig zero, late;
				if( !startRig( zero, w, h, perturb, nullptr, fps ) || !startRig( late, w, h, perturb, nullptr, fps ) )
					return 1;
				late.plugin.SetClockScaleForTest( 0.001 );
				late.timeOffset = 4.99e8;
				late.timeScale  = 1000.0;
				const int option = f == 9 ? 0 : ( f == 30 ? 1 : 2 );
				zero.Set( PT_FRAME_RATE, option );
				late.Set( PT_FRAME_RATE, option );

				const int frames = 2 * fps;
				Floats card;
				Bytes before[ 2 ];
				double stateBefore[ 2 ] = {};
				int setMismatch[ 2 ] = {}, pictureWorst = 0, readouts = 0;
				double instantWorst[ 2 ] = {};
				for( int n = 0; n < frames; ++n )
				{
					const double g = n % 2 ? 0.8 : 0.2;
					clockCard( card, w, h, n, g );
					Rig* rigs[ 2 ] = { &zero, &late };
					Bytes now[ 2 ];
					for( int r = 0; r < 2; ++r )
					{
						rigs[ r ]->Upload( card );
						if( !rigs[ r ]->Render( 1 ) )
							return 1;
						now[ r ] = rigs[ r ]->OutputBytes();
						const double raw   = readTexel( rigs[ r ]->plugin.RawTextureID(), qx, qy );
						const double state = readTexel( rigs[ r ]->plugin.StateTextureID(), qx, qy );
						if( n >= 1 )
						{
							const bool expected = expectedReadout( n, fps, 1, f, {} ) > expectedReadout( n - 1, fps, 1, f, {} );
							const bool changed  = now[ r ] != before[ r ];
							setMismatch[ r ] += expected != changed;
							readouts += r == 0 && expected;
							if( expected )
							{
								//The instant the readout was taken, solved from the lag:
								//raw = S + ( T_prev - S ) exp( -( t - t_prev ) / tau ).
								const double S     = tempOf( g, g, g );
								const double tPrev = static_cast< double >( n - 1 ) / fps;
								const double t     = tPrev - kTau * std::log( ( raw - S ) / ( stateBefore[ r ] - S ) );
								const double want  = static_cast< double >( expectedReadout( n, fps, 1, f, {} ) ) / f;
								instantWorst[ r ]  = std::max( instantWorst[ r ], std::fabs( t - want ) );
							}
						}
						before[ r ]      = now[ r ];
						stateBefore[ r ] = state;
					}
					for( size_t i = 0; i < now[ 0 ].size(); ++i )
						pictureWorst = std::max( pictureWorst, std::abs( static_cast< int >( now[ 0 ][ i ] ) - static_cast< int >( now[ 1 ][ i ] ) ) );
				}
				Check( setMismatch[ 0 ] == 0 && setMismatch[ 1 ] == 0 && instantWorst[ 0 ] <= 1e-5 && instantWorst[ 1 ] <= 1e-5 && pictureWorst <= 1,
				       fmt( "%4dx%-4d %2d Hz at %3d fps, %d frames: %d readouts; frames whose change disagrees with floor( n f / fps ): %d, at 4.99e8 ms %d; each "
				            "readout's instant from the lag within %.2e s, at 4.99e8 ms %.2e s (tolerance 1e-5 s; the host frame is %.4f s away at worst); the two "
				            "clocks' pictures within %d codes (tolerance 1)",
				            w, h, f, fps, frames, readouts, setMismatch[ 0 ], setMismatch[ 1 ], instantWorst[ 0 ], instantWorst[ 1 ], 1.0 / fps, pictureWorst ) );
			}

		//The unit, unforced: this harness renders far faster than real time,
		//as an offline host or oxbow's selftest does, so the wall-clock vote
		//cannot settle it and the step's size has to.
		for( const double unit : { 1.0, 1000.0 } )
		{
			Rig r;
			if( !startRig( r, w, h, perturb ) )
				return 1;
			r.plugin.SetClockScaleForTest( 0.0 );
			r.timeOffset = unit == 1.0 ? 12.5 : 4.99e8;
			r.timeScale  = unit;
			if( !r.Render( 60 ) )
				return 1;
			const double want = 1.0 / unit;
			const double got  = r.plugin.ClockScale();
			const double host = ( r.timeOffset + r.timeScale * r.Seconds( r.frame - 1 ) ) * want;
			Check( got == want && std::fabs( r.plugin.HostNow() - host ) < 1e-6,
			       fmt( "%4dx%-4d a host clock in %s at 60 fps, rendered faster than real time: settled on %s by frame 60, and reads %.6f s (host %.6f s)", w, h,
			            unit == 1.0 ? "seconds" : "milliseconds", got == 1.0 ? "seconds" : ( got == 0.001 ? "milliseconds" : "nothing" ), r.plugin.HostNow(),
			            host ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --shutter
//===========================================================================
/// One FFC scenario: interval 5 s (the first FFC at instant 5f), or a press
/// at host frame `pressAt` with the interval Off.
bool shutterScenario( const Perturb& perturb, int w, int h, int f, int fps, int pressAt, double seconds, int& setMismatch, double& stateWorst,
                      long long& firstFrozen, long long& count, int& readouts )
{
	Rig rig;
	if( !startRig( rig, w, h, perturb, nullptr, fps ) )
		return false;
	rig.Set( PT_FRAME_RATE, f == 9 ? 0 : ( f == 30 ? 1 : 2 ) );
	rig.Set( PT_SHUTTER_INTERVAL, pressAt < 0 ? 0.0f : 1.0f );
	//The FFC, from the spec in integers: the first instant at least 5 s after
	//the start, or the instant after the last one before the press.
	firstFrozen = pressAt < 0 ? 5LL * f : static_cast< long long >( pressAt - 1 ) * f / fps + 1;
	count       = frozenCount( f );
	const std::vector< std::pair< long long, long long > > frozen = { { firstFrozen, count } };
	const double c = static_cast< double >( firstFrozen ) / f, o = c + kOpenAfter;
	const double S = 20.0, Tc = kShutterC + ( S - kShutterC ) * std::exp( -( o - c ) / kTau );
	const int qx = 3 * w / 4, qy = h / 2;
	const int frames = static_cast< int >( seconds * fps );
	Floats card;
	Bytes before;
	setMismatch = 0;
	stateWorst  = 0.0;
	readouts    = 0;
	for( int n = 0; n < frames; ++n )
	{
		clockCard( card, w, h, n, greyFor( S ) );
		rig.Upload( card );
		const bool ok = n == pressAt ? rig.Press( PT_SHUTTER ) : rig.Render( 1 );
		if( !ok )
			return false;
		const Bytes now = rig.OutputBytes();
		if( n >= 1 )
		{
			const bool expected = expectedReadout( n, fps, 1, f, frozen ) > expectedReadout( n - 1, fps, 1, f, frozen );
			setMismatch += expected != ( now != before );
			readouts += expected;
		}
		before = now;
		//The bolometers on the right: the scene, then the shutter's 27.5 degC
		//from c, then the scene again from c + 20/26 s.
		const double t = static_cast< double >( n ) / fps;
		double want    = S;
		if( t >= c && t < o )
			want = kShutterC + ( S - kShutterC ) * std::exp( -( t - c ) / kTau );
		else if( t >= o )
			want = S + ( Tc - S ) * std::exp( -( t - o ) / kTau );
		stateWorst = std::max( stateWorst, std::fabs( readTexel( rig.plugin.StateTextureID(), qx, qy ) - want ) );
	}
	return true;
}

int runShutter( const Perturb& perturb )
{
	std::printf( "\n=== shutter: an FFC starts at the first instant 5 s after the start (Shutter Interval 5 s), or at the next instant after a press; it holds the "
	             "picture over the ceil( 23 f / 26 ) sensor frames in [ c, c + 23/26 s ) -- host time, not host frames --\n    and the bolometers see the "
	             "shutter's 27.5 degC over [ c, c + 20/26 s )\n" );
	struct Case
	{
		int f, fps, pressAt;
		double seconds;
	};
	const Case cases[] = { { 9, 24, -1, 6.5 }, { 9, 60, -1, 6.5 }, { 9, 144, -1, 6.5 }, { 30, 60, -1, 6.5 }, { 60, 24, -1, 6.5 }, { 60, 144, -1, 6.5 }, { 9, 60, 40, 2.0 } };
	for( const Raster& raster : kRasters )
		for( const Case& c : cases )
		{
			int mismatch = 0, readouts = 0;
			double worst = 0.0;
			long long first = 0, count = 0;
			if( !shutterScenario( perturb, raster.w, raster.h, c.f, c.fps, c.pressAt, c.seconds, mismatch, worst, first, count, readouts ) )
				return 1;
			Check( mismatch == 0 && worst <= 1e-4,
			       fmt( "%4dx%-4d %2d Hz at %3d fps, %s: instants %lld..%lld frozen (%lld), %d readouts; frames whose change disagrees: %d; the bolometers %.2e K "
			            "from the shutter's window (tolerance 1e-4 K)",
			            raster.w, raster.h, c.f, c.fps, c.pressAt < 0 ? "interval 5 s" : "a press at frame 40", first, first + count - 1, count, readouts, mismatch,
			            worst ) );
		}
	return Verdict();
}

//===========================================================================
// --agc
//===========================================================================
int runAgc( const Perturb& perturb )
{
	std::printf( "\n=== agc: Auto counts the readout on the 256 x 144 grid exactly, and its levels are plateau equalisation of that histogram (Plateau 7%% and "
	             "100%%);\n    the transfer moves toward a new scene by 0.85 a sensor frame read out, at 24 and at 144 host fps\n" );
	const std::vector< double > tempsA = stripesA(), tempsB = stripesB();
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		const Floats card                 = stripeCard( w, h, tempsA );
		const std::vector< double > histA = stripeHistogram( w, tempsA );
		for( const double plateau : { 0.07, 1.0 } )
		{
			Rig rig;
			if( !startRig( rig, w, h, perturb, &card ) )
				return 1;
			rig.Set( PT_SPAN, static_cast< int >( Span::Auto ) );
			rig.Set( PT_PLATEAU, ParamFromPlateau( plateau ) );
			if( !rig.Render( 20 ) )
				return 1;
			//The histogram against the readout itself, binned by the harness.
			const std::vector< double > raw  = rig.Raw();
			const std::vector< double > hist = rig.Histogram();
			std::vector< double > mine( model::kBins, 0.0 );
			for( int j = 0; j < model::kGridH; ++j )
				for( int i = 0; i < model::kGridW; ++i )
				{
					const int px = ( 2 * i + 1 ) * w / ( 2 * model::kGridW ), py = ( 2 * j + 1 ) * h / ( 2 * model::kGridH );
					const double T = raw[ static_cast< size_t >( py ) * w + px ];
					const int k    = std::clamp( static_cast< int >( std::floor( ( T - model::kHistLow ) * model::kBins / ( model::kHistHigh - model::kHistLow ) ) ),
					                             0, model::kBins - 1 );
					mine[ static_cast< size_t >( k ) ] += 1.0;
				}
			double histWorst = 0.0, sum = 0.0;
			for( int k = 0; k < model::kBins; ++k )
			{
				histWorst = std::max( histWorst, std::fabs( hist[ static_cast< size_t >( k ) ] - mine[ static_cast< size_t >( k ) ] ) );
				histWorst = std::max( histWorst, std::fabs( hist[ static_cast< size_t >( k ) ] - histA[ static_cast< size_t >( k ) ] ) );
				sum += hist[ static_cast< size_t >( k ) ];
			}
			//The levels, out of the picture, against equalisation of the card's own histogram.
			const Floats out = rig.Output();
			double worst     = 0.0;
			for( int x = 0; x < w; ++x )
			{
				const double want = equalised( histA, plateau * model::kGridCount, tempsA[ static_cast< size_t >( x * 32 / w ) ] );
				const double got  = out[ ( static_cast< size_t >( h / 2 ) * w + x ) * 4 ];
				worst             = std::max( worst, std::fabs( got - want ) );
			}
			const double hotWant = equalised( histA, plateau * model::kGridCount, tempsA[ 3 ] );
			Check( histWorst == 0.0 && sum == model::kGridCount && worst <= 0.5 / 255.0 + 1e-5,
			       fmt( "%4dx%-4d Plateau %3.0f%%: the histogram %s the readout's on the grid and the card's (worst bin %.0f off, %.0f counts); levels within %.3f "
			            "codes of equalisation (the hot stripes at %.4f; tolerance 0.5 code, the 8-bit output)",
			            w, h, plateau * 100.0, histWorst == 0.0 ? "equals" : "DIFFERS FROM", histWorst, sum, worst * 255.0, hotWant ) );
		}

		//The damping: a 60 Hz sensor, a new scene 1 s in, at two host rates.
		for( const int fps : { 24, 144 } )
		{
			const Floats cardB = stripeCard( w, h, tempsB );
			Rig rig;
			if( !startRig( rig, w, h, perturb, &card, fps ) )
				return 1;
			rig.Set( PT_SPAN, static_cast< int >( Span::Auto ) );
			rig.Set( PT_PLATEAU, ParamFromPlateau( 0.07 ) );
			rig.Set( PT_FRAME_RATE, static_cast< int >( FrameRate::Hz60 ) );
			if( !rig.Render( fps ) )
				return 1;
			rig.Upload( cardB );
			std::vector< std::vector< double > > luts, hists;
			const int start = fps, frames = 2 * fps;
			for( int n = start; n < start + frames; ++n )
			{
				if( !rig.Render( 1 ) )
					return 1;
				luts.push_back( rig.Lut() );
				hists.push_back( rig.Histogram() );
			}
			const std::vector< double >& last = luts.back();
			const std::vector< double >& done = hists.back();
			size_t bin = 0;
			for( size_t i = 0; i < last.size(); ++i )
				if( std::fabs( luts[ 0 ][ i ] - last[ i ] ) > std::fabs( luts[ 0 ][ bin ] - last[ bin ] ) )
					bin = i;
			int pairs = 0;
			double worst = 0.0;
			int prev = -1;
			for( int m = 0; m < frames; ++m )
			{
				const int n = start + m;
				if( expectedReadout( n, fps, 1, 60, {} ) == expectedReadout( n - 1, fps, 1, 60, {} ) )
					continue;
				if( prev >= 0 && hists[ static_cast< size_t >( m ) ] == done )
				{
					const double e1 = luts[ static_cast< size_t >( prev ) ][ bin ] - last[ bin ];
					const double e2 = luts[ static_cast< size_t >( m ) ][ bin ] - last[ bin ];
					if( std::fabs( e1 ) > 1e-3 )
					{
						const long long fresh = expectedReadout( n, fps, 1, 60, {} ) - expectedReadout( start + prev, fps, 1, 60, {} );
						worst                 = std::max( worst, std::fabs( e2 / e1 - std::pow( 0.85, static_cast< double >( fresh ) ) ) );
						++pairs;
					}
				}
				prev = m;
			}
			Check( pairs >= 3 && worst <= 1e-3,
			       fmt( "%4dx%-4d 60 Hz sensor at %3d fps: %d successive readouts of the transfer (bin %zu, %s) shrink its distance to the new scene's by 0.85^m, "
			            "m the sensor frames between them, worst %.2e off (tolerance 1e-3: 8x float32's bound at a 1e-3 distance)",
			            w, h, fps, pairs, bin / 2, bin % 2 ? "top" : "bottom", worst ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --manual
//===========================================================================
int runManual( const Perturb& perturb )
{
	std::printf( "\n=== manual: Span Manual maps T to ( T - Level ) / Width + 0.5, clipped: Level the middle of the scale, Width all of it\n" );
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		auto grey         = [ w ]( int x ) { return 0.05 + 0.9 * ( x + 0.5 ) / w; };
		const Floats card = greyCard( w, h, [ & ]( int x, int ) { return grey( x ); } );
		Rig rig;
		if( !startRig( rig, w, h, perturb, &card ) )
			return 1;
		const double settings[ 3 ][ 2 ] = { { 27.5, 10.0 }, { 22.0, 4.0 }, { 33.0, 25.0 } };
		double worst                    = 0.0;
		int clipped                     = 0;
		for( const auto& s : settings )
		{
			rig.Set( PT_LEVEL, ParamFromLevel( s[ 0 ] ) );
			rig.Set( PT_WIDTH, ParamFromWidth( s[ 1 ] ) );
			if( !rig.Render( 10 ) )
				return 1;
			const Floats out = rig.Output();
			for( int x = 0; x < w; ++x )
			{
				const double T    = tempOf( grey( x ), grey( x ), grey( x ) );
				const double y    = ( T - s[ 0 ] ) / s[ 1 ] + 0.5;
				const double want = std::clamp( y, 0.0, 1.0 );
				clipped += y < 0.0 || y > 1.0;
				worst = std::max( worst, std::fabs( out[ ( static_cast< size_t >( h / 2 ) * w + x ) * 4 ] - want ) );
			}
		}
		Check( worst <= 0.5 / 255.0 + 1e-5 && clipped > 0,
		       fmt( "%4dx%-4d a ramp from 16.1 to 38.9 degC under Level/Width 27.5/10, 22/4, 33/25 K: every level within %.3f codes (tolerance 0.5 code, the 8-bit "
		            "output); %d pixels clipped at an end",
		            w, h, worst * 255.0, clipped ) );
	}
	return Verdict();
}

//===========================================================================
// --halo
//===========================================================================
int runHalo( const Perturb& perturb )
{
	std::printf( "\n=== halo: the detail enhancement is an unsharp mask y + a ( y - G y ), G a Gaussian of sigma 0.01 H: across an edge of Delta its overshoot "
	             "has no net level,\n    a one-sided sum a Delta Sum_i>0 i g_i and a first moment a Delta Sum_i>0 i^2 g_i (g the kernel), along both axes\n" );
	const double a = 2.0, g0 = 89.0 / 255.0, g1 = 166.0 / 255.0, delta = g1 - g0;// levels whose plateaus are exact codes
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		//The kernel the spec describes: a Gaussian of sigma 0.01 H, cut at 4 sigma, normalised.
		const double sigma = 0.01 * h;
		const int R        = static_cast< int >( std::ceil( 4.0 * sigma ) );
		std::vector< double > g( static_cast< size_t >( R ) + 1 );
		double total = 0.0;
		for( int i = -R; i <= R; ++i )
			total += std::exp( -0.5 * i * i / ( sigma * sigma ) );
		double sum1 = 0.0, sum2 = 0.0;
		for( int i = 1; i <= R; ++i )
		{
			const double gi = std::exp( -0.5 * i * i / ( sigma * sigma ) ) / total;
			sum1 += i * gi;
			sum2 += static_cast< double >( i ) * i * gi;
		}
		const double wantA = a * delta * sum1, wantB = a * delta * sum2;
		const int reach    = R + 2;
		double tolA = 0.0, tolB = 0.0;
		for( int m = -reach; m < reach; ++m )
		{
			tolA += m >= 0 ? 0.5 / 255.0 : 0.0;
			tolB += 0.5 / 255.0 * std::fabs( m + 0.5 );
		}
		for( const bool vertical : { true, false } )
		{
			const Floats card = greyCard( w, h, [ & ]( int x, int y ) { return ( vertical ? x < w / 2 : y < h / 2 ) ? g0 : g1; } );
			Rig rig;
			if( !startRig( rig, w, h, perturb, &card ) )
				return 1;
			rig.Set( PT_HALO, ParamFromHalo( a ) );
			if( !rig.Render( 10 ) )
				return 1;
			const Floats out = rig.Output();
			double right = 0.0, left = 0.0, moment = 0.0;
			for( int m = -reach; m < reach; ++m )
			{
				const int x       = vertical ? w / 2 + m : w / 2;
				const int y       = vertical ? h / 2 : h / 2 + m;
				const double d    = out[ ( static_cast< size_t >( y ) * w + x ) * 4 ] - ( m < 0 ? g0 : g1 );
				( m >= 0 ? right : left ) += d;
				moment += ( m + 0.5 ) * d;
			}
			const double sigmaD = std::sqrt( 2.0 * moment / ( a * delta ) ), sigmaK = std::sqrt( 2.0 * sum2 );
			Check( std::fabs( right + left ) <= 2.0 * tolA && std::fabs( right - wantA ) <= tolA && std::fabs( -left - wantA ) <= tolA
			           && std::fabs( moment - wantB ) <= tolB,
			       fmt( "%4dx%-4d a %s edge of %.2f, Halo %.0f: net overshoot %+.4f (tolerance %.4f); one-sided sums %+.4f and %+.4f, want %.4f (tolerance %.4f); "
			            "first moment %.3f, want %.3f (tolerance %.3f) -- the kernel's sigma %.3f px, want %.3f (tolerances: half a code a pixel, summed)",
			            w, h, vertical ? "vertical" : "horizontal", delta, a, right + left, 2.0 * tolA, right, -left, wantA, tolA, moment, wantB, tolB, sigmaD, sigmaK ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --narcissus
//===========================================================================
/// The fraction of a disc of radius b at ( px, py ) inside a disc of radius R
/// at ( cx, cy ), by chords: slice the small disc across x and intersect each
/// slice's span in y with the spot's. Midpoint rule, `slices` slices.
double discInsideSpot( double px, double py, double b, double cx, double cy, double R, int slices )
{
	double area = 0.0;
	const double dx = 2.0 * b / slices;
	for( int i = 0; i < slices; ++i )
	{
		const double x  = px - b + ( i + 0.5 ) * dx;
		const double hb = std::sqrt( std::max( 0.0, b * b - ( x - px ) * ( x - px ) ) );
		const double rr = R * R - ( x - cx ) * ( x - cx );
		if( rr <= 0.0 )
			continue;
		const double hs = std::sqrt( rr );
		const double lo = std::max( py - hb, cy - hs ), hi = std::min( py + hb, cy + hs );
		if( hi > lo )
			area += ( hi - lo ) * dx;
	}
	return area / ( model::kPi * b * b );
}

int runNarcissus( const Perturb& perturb )
{
	std::printf( "\n=== narcissus: the lens's cold reflection of the sensor, depth N, centred: N inside r < R - b, nothing past R + b, and between them the "
	             "share of a defocus disc of radius b = 0.1 H inside the spot of radius R = 0.15 H\n    (by chords); the readout's own; an FFC leaves it\n" );
	const double N = 2.0;
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		const Floats card = flatCard( w, h, 0.5f, 0.5f, 0.5f );
		Rig rig;
		if( !startRig( rig, w, h, perturb, &card ) )
			return 1;
		rig.Set( PT_NARCISSUS, ParamFromNarcissus( N ) );
		rig.Set( PT_SHUTTER_INTERVAL, 0.0f );
		if( !rig.Render( 60 ) )
			return 1;
		const double R = 0.15 * h, b = 0.1 * h, cx = 0.5 * w, cy = 0.5 * h;
		auto measure = [ & ]( double& inner, double& outer, double& profile, double& quad, double& ox, double& oy ) {
			const std::vector< double > raw = rig.Raw();
			const double S                  = raw[ 0 ];
			inner = outer = profile = quad = 0.0;
			double sum = 0.0, mx = 0.0, my = 0.0;
			for( int y = 0; y < h; ++y )
				for( int x = 0; x < w; ++x )
				{
					const double d = S - raw[ static_cast< size_t >( y ) * w + x ];
					const double r = std::hypot( x + 0.5 - cx, y + 0.5 - cy );
					if( r < R - b - 1.0 )
						inner = std::max( inner, std::fabs( d - N ) );
					if( r > R + b + 1.0 )
						outer = std::max( outer, std::fabs( d ) );
					sum += d;
					mx += d * ( x + 0.5 );
					my += d * ( y + 0.5 );
					const bool onLine = y == h / 2 || x == w / 2;
					if( onLine && r >= R - b - 1.0 && r <= R + b + 1.0 )
					{
						const double frac  = discInsideSpot( x + 0.5, y + 0.5, b, cx, cy, R, 20000 );
						const double frac2 = discInsideSpot( x + 0.5, y + 0.5, b, cx, cy, R, 40000 );
						profile            = std::max( profile, std::fabs( d - N * frac ) );
						quad               = std::max( quad, N * std::fabs( frac - frac2 ) );
					}
				}
			ox = mx / sum - cx;
			oy = my / sum - cy;
		};
		double inner, outer, profile, quad, ox, oy;
		measure( inner, outer, profile, quad, ox, oy );
		if( !rig.Render( 330 ) )
			return 1;
		const int ffcs = rig.plugin.LastFrame().ffcsCompleted;
		double inner2, outer2, profile2, quad2, ox2, oy2;
		measure( inner2, outer2, profile2, quad2, ox2, oy2 );
		Check( inner <= 1e-4 && outer <= 1e-5 && profile <= 1e-3 * N && quad <= 1e-4 && std::fabs( ox ) <= 0.01 && std::fabs( oy ) <= 0.01 && ffcs >= 1
		           && inner2 <= 1e-4 && profile2 <= 1e-3 * N,
		       fmt( "%4dx%-4d N = %.1f K: inside %.2e K from N, outside %.2e K from nothing (tolerances 1e-4, 1e-5 K); the profile %.2e K from the chord sum "
		            "(tolerance 1e-3 N; the sum's own error %.1e K); centroid off by (%.4f, %.4f) px (tolerance 0.01); after %d FFC: inside %.2e K, profile %.2e K",
		            w, h, N, inner, outer, profile, quad, ox, oy, ffcs, inner2, profile2 ) );
	}
	return Verdict();
}

//===========================================================================
// --drift
//===========================================================================
struct DriftReading
{
	double rms = 0.0, columnShare = 0.0;
	std::vector< double > x;
};

DriftReading readDrift( Rig& rig )
{
	const std::vector< double > raw = rig.Raw();
	const int w = rig.width, h = rig.height;
	DriftReading out;
	out.x.resize( raw.size() );
	double sq = 0.0;
	std::vector< double > column( static_cast< size_t >( w ), 0.0 );
	for( int y = 0; y < h; ++y )
		for( int x = 0; x < w; ++x )
		{
			const double v                      = raw[ static_cast< size_t >( y ) * w + x ] - 27.5;
			out.x[ static_cast< size_t >( y ) * w + x ] = v;
			sq += v * v;
			column[ static_cast< size_t >( x ) ] += v / h;
		}
	double colSq = 0.0;
	for( double c : column )
		colSq += c * c;
	out.rms         = std::sqrt( sq / raw.size() );
	out.columnShare = ( colSq / w ) / ( sq / raw.size() );
	return out;
}

int runDrift( const Perturb& perturb )
{
	std::printf( "\n=== drift: the fixed pattern since the last FFC grows as Drift x age -- its RMS -- with half its variance in the column means; an FFC starts "
	             "it again with a new pattern; its age stops at 600 s\n" );
	const double rate = 1.0 / 60.0;//K a second: Drift at 1 K/min
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		const Floats card = flatCard( w, h, 0.5f, 0.5f, 0.5f );
		//The standard error of the RMS and of the column share, from the
		//uniform variates' fourth moment (9/5): columns dominate.
		const double seRms   = 0.25 * std::sqrt( 0.8 / w + 4.8 / ( static_cast< double >( w ) * h ) );
		const double seShare = 0.25 * std::sqrt( 0.8 / w );
		const double seRho   = 0.5 / std::sqrt( static_cast< double >( w ) );
		Rig rig;
		if( !startRig( rig, w, h, perturb, &card ) )
			return 1;
		rig.Set( PT_DRIFT, ParamFromDrift( 1.0 ) );
		rig.Set( PT_SHUTTER_INTERVAL, 0.0f );
		//Readout frames at 60 fps and 9 Hz: frame 60 (k = 9), 120, 240, 300, and
		//after the FFC at 5 s (it ends at 5 + 23/26 s) frame 414 (k = 62).
		const int at[]          = { 60, 120, 240, 294, 414 };
		std::vector< DriftReading > seen;
		for( const int n : at )
		{
			if( !rig.Render( n + 1 - rig.frame ) )
				return 1;
			seen.push_back( readDrift( rig ) );
		}
		double linear = 0.0, level = 0.0, share = 0.0;
		for( int i = 0; i < 4; ++i )
		{
			const double age = static_cast< double >( expectedReadout( at[ i ], 60, 1, 9, {} ) ) / 9.0;
			linear           = std::max( linear, std::fabs( seen[ static_cast< size_t >( i ) ].rms / ( rate * age ) - seen[ 0 ].rms / ( rate * 1.0 ) ) );
			level            = std::max( level, std::fabs( seen[ static_cast< size_t >( i ) ].rms / ( rate * age ) - 1.0 ) );
			share            = std::max( share, std::fabs( seen[ static_cast< size_t >( i ) ].columnShare - ( 0.5 + 0.5 / h ) ) );
		}
		const long long kAfter = expectedReadout( 414, 60, 1, 9, { { 45, frozenCount( 9 ) } } );
		const double ageAfter  = static_cast< double >( kAfter ) / 9.0 - ( 5.0 + kFfcSeconds );
		const double after     = seen[ 4 ].rms / ( rate * ageAfter ) - 1.0;
		double dot = 0.0, n1 = 0.0, n2 = 0.0;
		for( size_t i = 0; i < seen[ 3 ].x.size(); ++i )
		{
			dot += seen[ 3 ].x[ i ] * seen[ 4 ].x[ i ];
			n1 += seen[ 3 ].x[ i ] * seen[ 3 ].x[ i ];
			n2 += seen[ 4 ].x[ i ] * seen[ 4 ].x[ i ];
		}
		const double rho = dot / std::sqrt( n1 * n2 );

		//The cap: no FFC, and the next frame 700 s later.
		Rig late;
		if( !startRig( late, w, h, perturb, &card, 1.0 / 700.0 ) )
			return 1;
		late.Set( PT_DRIFT, ParamFromDrift( 1.0 ) );
		if( !late.Render( 2 ) )
			return 1;
		const double capped = readDrift( late ).rms / ( rate * 600.0 ) - 1.0;

		Check( linear <= 1e-4 && level <= 4.0 * seRms && share <= 4.0 * seShare && std::fabs( after ) <= 4.0 * seRms && std::fabs( rho ) <= 4.0 * seRho
		           && std::fabs( capped ) <= 4.0 * seRms,
		       fmt( "%4dx%-4d Drift 1 K/min: RMS / age at 1, 2, 4, 4.9 s agree within %.1e (tolerance 1e-4: linear) and are %.2f%% from the rate (tolerance %.2f%%, "
		            "4 standard errors); column means hold %.4f of the variance, want %.4f (tolerance %.4f); %.2f s after the FFC %+.2f%%, correlation with the "
		            "pattern before %+.4f (tolerance %.4f); 700 s without one: %+.2f%% from 600 s",
		            w, h, linear, level * 100.0, 400.0 * seRms, seen[ 0 ].columnShare, 0.5 + 0.5 / h, 4.0 * seShare, ageAfter, after * 100.0, rho, 4.0 * seRho,
		            capped * 100.0 ) );
	}
	return Verdict();
}

//===========================================================================
// --palette
//===========================================================================
int runPalette( const Perturb& perturb )
{
	std::printf( "\n=== palette: the core's 8-bit level through each palette -- White Hot grey, Black Hot inverted, Ironbow and Rainbow at anchors typed from "
	             "upstream's tables\n" );
	const int levels[ 8 ] = { 0, 1, 64, 127, 128, 192, 254, 255 };
	struct Anchor
	{
		int palette, level, rgb[ 3 ];
	};
	//From LeptonModule's Palettes.cpp, read 2026-11-16: colormap_rainbow
	//entries 0, 1, 64, 128, 192, 254, 255; colormap_ironblack 127 and 254
	//(Ironbow's ends) and 190/191 (level 128: 221 + 2 x 0.749, 52 + 3 x 0.749,
	//64 - 5 x 0.749, rounded half up).
	const Anchor anchors[] = { { 3, 0, { 1, 3, 74 } },       { 3, 1, { 0, 3, 74 } },     { 3, 64, { 0, 113, 223 } },     { 3, 128, { 212, 215, 3 } },
		                       { 3, 192, { 251, 33, 59 } },  { 3, 254, { 255, 230, 206 } }, { 3, 255, { 255, 233, 208 } }, { 2, 0, { 0, 0, 0 } },
		                       { 2, 128, { 222, 54, 60 } }, { 2, 255, { 255, 253, 235 } } };
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		const Floats card = greyCard( w, h, [ & ]( int x, int ) { return levels[ x * 8 / w ] / 255.0; } );
		Rig rig;
		if( !startRig( rig, w, h, perturb, &card ) )
			return 1;
		int wrong = 0, compared = 0;
		std::string first;
		for( int p = 0; p < 4; ++p )
		{
			rig.Set( PT_PALETTE, p );
			if( !rig.Render( 10 ) )
				return 1;
			const Floats out = rig.Output();
			for( int s = 0; s < 8; ++s )
			{
				const int x = ( 2 * s + 1 ) * w / 16;
				int got[ 3 ];
				for( int c = 0; c < 3; ++c )
					got[ c ] = code( out[ ( static_cast< size_t >( h / 2 ) * w + x ) * 4 + c ] );
				int want[ 3 ] = { -1, -1, -1 };
				if( p == 0 )
					want[ 0 ] = want[ 1 ] = want[ 2 ] = levels[ s ];
				else if( p == 1 )
					want[ 0 ] = want[ 1 ] = want[ 2 ] = 255 - levels[ s ];
				else
					for( const Anchor& a : anchors )
						if( a.palette == p && a.level == levels[ s ] )
							std::memcpy( want, a.rgb, sizeof( want ) );
				if( want[ 0 ] < 0 )
					continue;
				++compared;
				if( got[ 0 ] != want[ 0 ] || got[ 1 ] != want[ 1 ] || got[ 2 ] != want[ 2 ] )
				{
					if( wrong++ == 0 )
						first = fmt( "; first: palette %d level %d is (%d,%d,%d), want (%d,%d,%d)", p, levels[ s ], got[ 0 ], got[ 1 ], got[ 2 ], want[ 0 ], want[ 1 ],
						             want[ 2 ] );
				}
			}
		}
		Check( wrong == 0 && compared == 26, fmt( "%4dx%-4d %d levels compared across the four palettes, %d wrong (exact: 8-bit)%s", w, h, compared, wrong, first.c_str() ) );
	}
	return Verdict();
}

//===========================================================================
// --alpha
//===========================================================================
int runAlpha( const Perturb& perturb )
{
	std::printf( "\n=== alpha: the camera's picture is opaque -- alpha = a + ( 1 - a ) Mix, colour ( clip x a ( 1 - Mix ) + camera x Mix ) / alpha; a transparent "
	             "pixel is cold (15 degC) to the camera; Mix 0 hands the clip through\n" );
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		Floats card = buildCard( w, h );
		for( int y = 0; y < h; ++y )
			for( int x = 0; x < w; ++x )
				card[ ( static_cast< size_t >( y ) * w + x ) * 4 + 3 ] = x < w / 2 ? 0.0f : std::round( 255.0f * ( 2.0f * x / w - 1.0f ) ) / 255.0f;
		Rig rig;
		if( !startRig( rig, w, h, perturb, &card ) )
			return 1;
		double alphaWorst[ 2 ] = {}, colourWorst[ 2 ] = {};
		const float mixes[ 2 ] = { 1.0f, 0.5f };
		for( int k = 0; k < 2; ++k )
		{
			rig.Set( PT_MIX, mixes[ k ] );
			if( !rig.Render( 10 ) )
				return 1;
			const Floats out = rig.Output(), cam = rig.Shown();
			for( size_t p = 0; p < out.size() / 4; ++p )
			{
				const double a     = card[ 4 * p + 3 ];
				const double alpha = a + ( 1.0 - a ) * mixes[ k ];
				alphaWorst[ k ]    = std::max( alphaWorst[ k ], std::fabs( out[ 4 * p + 3 ] - alpha ) );
				for( int c = 0; c < 3; ++c )
				{
					const double want = ( card[ 4 * p + c ] * a * ( 1.0 - mixes[ k ] ) + cam[ 4 * p + c ] * mixes[ k ] ) / alpha;
					colourWorst[ k ]  = std::max( colourWorst[ k ], std::fabs( out[ 4 * p + c ] - want ) );
				}
			}
		}
		const std::vector< double > raw = rig.Raw();
		double cold                     = 0.0;
		for( int y = 0; y < h; ++y )
			for( int x = 0; x < w / 2; ++x )
				cold = std::max( cold, std::fabs( raw[ static_cast< size_t >( y ) * w + x ] - 15.0 ) );
		rig.Set( PT_MIX, 0.0f );
		if( !rig.Render( 1 ) )
			return 1;
		const size_t through = differing( rig.Output(), card );
		Check( alphaWorst[ 0 ] <= 1e-5 && alphaWorst[ 1 ] <= 1e-5 && colourWorst[ 0 ] <= 1e-5 && colourWorst[ 1 ] <= 1e-5 && cold <= 1e-5 && through == 0,
		       fmt( "%4dx%-4d a clip transparent on the left and ramped on the right: alpha within %.1e at Mix 1, %.1e at Mix 0.5; colour within %.1e, %.1e "
		            "(tolerance 1e-5: float32); the transparent half reads %.1e K from 15 degC (tolerance 1e-5); Mix 0: %zu values differ from the clip (bit-exact)",
		            w, h, alphaWorst[ 0 ], alphaWorst[ 1 ], colourWorst[ 0 ], colourWorst[ 1 ], cold, through ) );
	}
	return Verdict();
}

//===========================================================================
// --resize
//===========================================================================
int runResize( const Perturb& perturb )
{
	std::printf( "\n=== resize: the bolometers, the held frame and the AGC carry across a raster change mid-run (1280x720 -> 320x180 -> 1920x1080): inside each "
	             "stripe the state runs on from where it was,\n    the frame held between readouts is the one before, and the transfer is a never-resized "
	             "run's\n" );
	const std::vector< double > tempsA = stripesA(), tempsB = stripesB();
	Rig moved, always;
	const Floats a1280 = stripeCard( 1280, 720, tempsA ), b1280 = stripeCard( 1280, 720, tempsB );
	if( !startRig( moved, 1280, 720, perturb, &a1280 ) || !startRig( always, 1280, 720, perturb, &a1280 ) )
		return 1;
	for( Rig* r : { &moved, &always } )
	{
		r->Set( PT_SPAN, static_cast< int >( Span::Auto ) );
		r->Set( PT_PLATEAU, ParamFromPlateau( 0.07 ) );
	}
	if( !moved.Render( 60 ) || !always.Render( 60 ) )
		return 1;
	moved.Upload( b1280 );
	always.Upload( b1280 );
	//Frames 60 (a readout) and 61 (not) on the new scene; 62 and 123 are not
	//readouts at 9 Hz under 60 fps.
	if( !moved.Render( 2 ) || !always.Render( 2 ) )
		return 1;
	const double e = std::exp( -( 1.0 / 60.0 ) / kTau );
	double stateWorst = 0.0;
	int heldWrong = 0, inside = 0;
	auto across = [ & ]( int w, int h ) -> bool {
		//Each stripe's state and code at its centre, before.
		const std::vector< double > before = moved.State();
		const Bytes held                   = moved.OutputBytes();
		const int ow = moved.width, oh = moved.height;
		const Floats card = stripeCard( w, h, tempsB );
		if( !moved.Resize( w, h, &card ) || !moved.Render( 1 ) || !always.Render( 1 ) )
			return false;
		const std::vector< double > after = moved.State();
		const Bytes shown                 = moved.OutputBytes();
		for( int s = 0; s < 32; ++s )
		{
			//The middle half of the stripe, away from its edges at either raster.
			for( int x = s * w / 32 + w / 128; x < ( s + 1 ) * w / 32 - w / 128; ++x )
			{
				const int ox = ( 2 * s + 1 ) * ow / 64;
				const double T0 = before[ static_cast< size_t >( oh / 2 ) * ow + ox ];
				const double want = e * T0 + ( 1.0 - e ) * tempsB[ static_cast< size_t >( s ) ];
				stateWorst        = std::max( stateWorst, std::fabs( after[ static_cast< size_t >( h / 2 ) * w + x ] - want ) );
				for( int c = 0; c < 4; ++c )
					heldWrong += shown[ ( static_cast< size_t >( h / 2 ) * w + x ) * 4 + c ] != held[ ( static_cast< size_t >( oh / 2 ) * ow + ox ) * 4 + c ];
				++inside;
			}
		}
		return true;
	};
	if( !across( 320, 180 ) )
		return 1;
	const int toMiddle = 122 - moved.frame;
	if( !moved.Render( toMiddle ) || !always.Render( toMiddle ) )
		return 1;
	if( !across( 1920, 1080 ) )
		return 1;
	const std::vector< double > lutMoved = moved.Lut(), lutAlways = always.Lut();
	double lutWorst = 0.0, lutMoving = 0.0;
	for( size_t i = 0; i < lutMoved.size(); ++i )
		lutWorst = std::max( lutWorst, std::fabs( lutMoved[ i ] - lutAlways[ i ] ) );
	if( !moved.Render( 27 ) || !always.Render( 27 ) )
		return 1;
	const std::vector< double > lutMoved2 = moved.Lut(), lutAlways2 = always.Lut();
	for( size_t i = 0; i < lutMoved2.size(); ++i )
	{
		lutWorst  = std::max( lutWorst, std::fabs( lutMoved2[ i ] - lutAlways2[ i ] ) );
		lutMoving = std::max( lutMoving, std::fabs( lutMoved2[ i ] - lutMoved[ i ] ) );
	}
	Check( stateWorst <= 1e-4 && heldWrong == 0 && lutWorst <= 1e-6 && lutMoving > 1e-4,
	       fmt( "across both resizes, %d pixels inside the stripes: the state %.2e K from its own last value run on one host frame (tolerance 1e-4 K); %d "
	            "values of the held frame differ from the frame before; the transfer %.2e from a never-resized run's (tolerance 1e-6) while it still moves by "
	            "%.2e",
	            inside, stateWorst, heldWrong, lutWorst, lutMoving ) );
	return Verdict();
}

//===========================================================================
// --glstate
//===========================================================================
int runGLState( const Perturb& perturb )
{
	std::printf( "\n=== glstate: the host's GL state comes back as it was handed over\n" );
	for( const Raster& raster : kRasters )
	{
		Rig rig;
		if( !startRig( rig, raster.w, raster.h, perturb ) )
			return 1;
		rig.Set( PT_FRAME_RATE, static_cast< int >( FrameRate::Hz60 ) );
		rig.Set( PT_SPAN, static_cast< int >( Span::Auto ) );
		rig.Set( PT_HALO, ParamFromHalo( 1.0 ) );
		rig.Set( PT_DRIFT, ParamFromDrift( 0.3 ) );
		rig.Set( PT_NARCISSUS, ParamFromNarcissus( 0.3 ) );
		rig.Set( PT_MIX, 0.6f );
		GLuint vao = 0, buffer = 0;
		glGenVertexArrays( 1, &vao );
		glGenBuffers( 1, &buffer );
		int wrong = 0;
		for( int frame = 0; frame < 3; ++frame )
		{
			glBindFramebuffer( GL_FRAMEBUFFER, rig.outputFBO );
			glViewport( 3, 5, raster.w - 7, raster.h - 9 );
			glEnable( GL_BLEND );
			glBlendFuncSeparate( GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ZERO );
			glClearColor( 0.1f, 0.2f, 0.3f, 0.4f );
			glBindVertexArray( vao );
			glBindBuffer( GL_ARRAY_BUFFER, buffer );
			glActiveTexture( GL_TEXTURE3 );
			rig.plugin.SetTime( rig.Seconds( frame ) );
			rig.Set( PT_SHUTTER, frame == 1 ? 1.0f : 0.0f );
			if( rig.plugin.ProcessOpenGL( &rig.process ) != FF_SUCCESS )
				return 1;
			GLint vp[ 4 ], src = 0, dst = 0, srcA = 0, dstA = 0, va = 0, ab = 0, at = 0;
			GLfloat cc[ 4 ];
			glGetIntegerv( GL_VIEWPORT, vp );
			glGetIntegerv( GL_BLEND_SRC_RGB, &src );
			glGetIntegerv( GL_BLEND_DST_RGB, &dst );
			glGetIntegerv( GL_BLEND_SRC_ALPHA, &srcA );
			glGetIntegerv( GL_BLEND_DST_ALPHA, &dstA );
			glGetFloatv( GL_COLOR_CLEAR_VALUE, cc );
			glGetIntegerv( GL_VERTEX_ARRAY_BINDING, &va );
			glGetIntegerv( GL_ARRAY_BUFFER_BINDING, &ab );
			glGetIntegerv( GL_ACTIVE_TEXTURE, &at );
			wrong += vp[ 0 ] != 3 || vp[ 1 ] != 5 || vp[ 2 ] != raster.w - 7 || vp[ 3 ] != raster.h - 9;
			wrong += !glIsEnabled( GL_BLEND ) || src != GL_SRC_ALPHA || dst != GL_ONE || srcA != GL_ONE || dstA != GL_ZERO;
			wrong += cc[ 0 ] != 0.1f || cc[ 1 ] != 0.2f || cc[ 2 ] != 0.3f || cc[ 3 ] != 0.4f;
			wrong += va != static_cast< GLint >( vao ) || ab != static_cast< GLint >( buffer ) || at != GL_TEXTURE3;
			wrong += glIsEnabled( GL_PROGRAM_POINT_SIZE ) ? 1 : 0;
			wrong += glGetError() != GL_NO_ERROR;
		}
		glBindVertexArray( 0 );
		glDisable( GL_BLEND );
		glActiveTexture( GL_TEXTURE0 );
		glDeleteVertexArrays( 1, &vao );
		glDeleteBuffers( 1, &buffer );
		Check( wrong == 0, fmt( "%4dx%-4d three frames (one a Shutter press, every pass running) with a host viewport, blend, clear colour, vertex array, buffer and "
		                        "texture unit of its own: %d things not put back, GL errors included",
		                        raster.w, raster.h, wrong ) );
	}
	return Verdict();
}

//===========================================================================
// --camera (no GL): the camera's weights against RK4 of the lag.
//===========================================================================
/// RK4 of dT/dt = ( target - T ) / tau from a to b with the target held,
/// `steps` steps.
double rk4( double T, double target, double tau, double span, int steps )
{
	const double hstep = span / steps;
	auto d             = [ & ]( double v ) { return ( target - v ) / tau; };
	for( int i = 0; i < steps; ++i )
	{
		const double k1 = d( T ), k2 = d( T + 0.5 * hstep * k1 ), k3 = d( T + 0.5 * hstep * k2 ), k4 = d( T + hstep * k3 );
		T += hstep / 6.0 * ( k1 + 2.0 * k2 + 2.0 * k3 + k4 );
	}
	return T;
}

/// The harness's bolometer: RK4 from t0 to t1 with the scene at S and the
/// shutter's 27.5 over [ c, o ), split at c and o.
double integrate( double T, double t0, double t1, double S, double c, double o, double tau )
{
	double cuts[ 4 ] = { t0, std::clamp( c, t0, t1 ), std::clamp( o, t0, t1 ), t1 };
	for( int i = 0; i < 3; ++i )
	{
		const double span = cuts[ i + 1 ] - cuts[ i ];
		if( span <= 0.0 )
			continue;
		const double mid    = 0.5 * ( cuts[ i ] + cuts[ i + 1 ] );
		const double target = mid >= c && mid < o ? kShutterC : S;
		T                   = rk4( T, target, tau, span, std::max( 4, static_cast< int >( std::ceil( span / 2e-5 ) ) ) );
	}
	return T;
}

int runCamera( const Perturb& perturb )
{
	std::printf( "\n=== camera (no GL): the camera's weights carry a bolometer exactly as RK4 of dT/dt = ( target - T ) / tau does, across the shutter's window, at "
	             "24, 60 and 144 host fps; each readout is RK4's bolometer at its instant\n" );
	for( const int fps : { 24, 60, 144 } )
	{
		Camera camera;
		camera.hooks = perturb.camera;
		CameraSettings settings;
		settings.rate     = 9.0;
		settings.interval = 5.0;
		const double c = 5.0, o = 5.0 + kOpenAfter;
		double T = 0.0, Tref = 0.0, worst = 0.0, sampleWorst = 0.0;
		int samples = 0;
		const int frames = static_cast< int >( 6.5 * fps );
		for( int n = 0; n < frames; ++n )
		{
			const double t = static_cast< double >( n ) / fps;
			//The scene steps between 18 and 34 degC every fifth host frame.
			const double S        = ( n / 5 ) % 2 ? 34.0 : 18.0;
			const CameraFrame cf  = camera.Advance( t, settings, false );
			const double Tbefore  = T;
			if( cf.prime )
			{
				T = Tref = S;
				continue;
			}
			const double tPrev   = static_cast< double >( n - 1 ) / fps;
			const double refPrev = Tref;
			T                    = cf.state.prev * Tbefore + cf.state.scene * S + cf.state.shutter * kShutterC;
			Tref                 = integrate( Tref, tPrev, t, S, c, o, kTau );
			worst                = std::max( worst, std::fabs( T - Tref ) );
			if( cf.readout )
			{
				const long long j = expectedReadout( n, fps, 1, 9, { { 45, frozenCount( 9 ) } } );
				const double tj   = static_cast< double >( j ) / 9.0;
				const double got  = cf.sample.prev * Tbefore + cf.sample.scene * S + cf.sample.shutter * kShutterC;
				const double want = integrate( refPrev, tPrev, std::min( tj, t ), S, c, o, kTau );
				sampleWorst       = std::max( sampleWorst, std::fabs( got - want ) );
				++samples;
			}
		}
		//Instants 1 .. floor( 6.5 x 9 ) - 1 that the FFC did not freeze.
		const long long wantSamples = expectedReadout( frames - 1, fps, 1, 9, {} ) - frozenCount( 9 );
		Check( worst <= 1e-6 && sampleWorst <= 1e-6 && samples == wantSamples,
		       fmt( "%3d fps, 6.5 s, a scene stepping 16 K every 5 frames and one FFC: the camera's bolometer %.2e K from RK4's (tolerance 1e-6 K); %d readouts "
		            "(want %lld) %.2e K from RK4 at their instants",
		            fps, worst, samples, wantSamples, sampleWorst ) );
	}
	return Verdict();
}

//===========================================================================
// --instants (no GL): the camera's clock against integers.
//===========================================================================
int runInstants( const Perturb& perturb )
{
	std::printf( "\n=== instants (no GL): over 10 minutes the camera reads out exactly the instants integer arithmetic gives -- floor( n f / fps ), the FFCs at "
	             "s_1 = 5 f and s_{i+1} = s_i + 5 f + ceil( 23 f / 26 ), each freezing ceil( 23 f / 26 ) --\n    at 9, 30, 60 Hz under 24, 25, 30, 50, 60, "
	             "120, 144, 29.97 and 59.94 fps, from 0 s and from 4.99e8 ms\n" );
	struct Rate
	{
		long long num, den;
	};
	const Rate rates[] = { { 24, 1 }, { 25, 1 }, { 30, 1 }, { 50, 1 }, { 60, 1 }, { 120, 1 }, { 144, 1 }, { 30000, 1001 }, { 60000, 1001 } };
	long long totalFrames = 0, totalReadouts = 0;
	int mismatches = 0;
	std::string first;
	for( const long long f : { 9LL, 30LL, 60LL } )
		for( const Rate& rate : rates )
			for( const bool late : { false, true } )
			{
				Camera camera;
				camera.hooks = perturb.camera;
				CameraSettings settings;
				settings.rate     = static_cast< double >( f );
				settings.interval = 5.0;
				const long long frames = 600 * rate.num / rate.den;
				std::vector< std::pair< long long, long long > > frozen;
				const long long M = frozenCount( f );
				for( long long s = 5 * f; s < 700 * f; s += 5 * f + M )
					frozen.emplace_back( s, M );
				long long lastRead = 0;
				for( long long n = 0; n < frames; ++n )
				{
					const double seconds = static_cast< double >( n ) * static_cast< double >( rate.den ) / static_cast< double >( rate.num );
					const double t       = late ? ( 4.99e8 + 1000.0 * seconds ) * 0.001 : seconds;
					const CameraFrame cf = camera.Advance( t, settings, false );
					if( n == 0 )
						continue;
					const long long want = expectedReadout( n, rate.num, rate.den, f, frozen );
					const bool expected  = want > lastRead;
					const bool bad       = cf.readout != expected || ( expected && cf.readoutIndex != want );
					if( bad && mismatches++ == 0 )
						first = fmt( "; first: %lld Hz at %lld/%lld fps%s, frame %lld: read %s %lld, want %s %lld", f, rate.num, rate.den, late ? " late" : "", n,
						             cf.readout ? "out" : "nothing", cf.readoutIndex, expected ? "out" : "nothing", want );
					if( expected )
					{
						lastRead = want;
						++totalReadouts;
					}
				}
				totalFrames += frames;
			}
	Check( mismatches == 0, fmt( "%lld host frames, %lld readouts: %d disagree%s", totalFrames, totalReadouts, mismatches, first.c_str() ) );
	return Verdict();
}

//===========================================================================
// The registry.
//===========================================================================
struct CheckEntry
{
	const char* flag;
	CheckFn run;
	bool offline;///< needs no GL context
};

const std::vector< CheckEntry >& checks()
{
	static const std::vector< CheckEntry > list = {
		{ "camera", runCamera, true },
		{ "instants", runInstants, true },
		{ "thermal", runThermal, false },
		{ "clock", runClock, false },
		{ "shutter", runShutter, false },
		{ "agc", runAgc, false },
		{ "manual", runManual, false },
		{ "halo", runHalo, false },
		{ "narcissus", runNarcissus, false },
		{ "drift", runDrift, false },
		{ "palette", runPalette, false },
		{ "alpha", runAlpha, false },
		{ "resize", runResize, false },
		{ "glstate", runGLState, false },
	};
	return list;
}

bool isOffline( const std::string& flag )
{
	for( const CheckEntry& c : checks() )
		if( flag == c.flag )
			return c.offline;
	return false;
}

//===========================================================================
// --negative
//===========================================================================
int runNegative( bool offlineOnly = false )
{
	struct Case
	{
		const char* name;
		CheckFn check;
		Perturb perturb;
		const char* what;
	};
	std::vector< Case > cases;
	auto add = [ & ]( const char* name, CheckFn fn, const char* what, std::function< void( Perturb& ) > set ) {
		Perturb p;
		set( p );
		cases.push_back( { name, fn, p, what } );
	};
	add( "camera", runCamera, "tau 15 ms", []( Perturb& p ) { p.camera.tau = 0.015; } );
	add( "camera", runCamera, "the lag stepped by a fixed alpha a host frame, tuned for 60 fps", []( Perturb& p ) { p.camera.perFrameAlpha = true; } );
	add( "camera", runCamera, "an FFC of 1 s", []( Perturb& p ) { p.camera.ffcDuration = 1.0; } );
	add( "camera", runCamera, "the readout taken at the host frame, not the sensor instant", []( Perturb& p ) { p.camera.sampleAtHostTime = true; } );
	add( "instants", runInstants, "a sensor frame every round( 60 / f ) host frames", []( Perturb& p ) { p.camera.hostFrameCounting = true; } );
	add( "instants", runInstants, "the freeze held for round( 60 D ) host frames", []( Perturb& p ) { p.camera.freezeHostFrames = true; } );
	add( "instants", runInstants, "no freeze: sensor frames read out through the FFC", []( Perturb& p ) { p.camera.noFreeze = true; } );
	add( "thermal", runThermal, "tau 15 ms", []( Perturb& p ) { p.camera.tau = 0.015; } );
	add( "thermal", runThermal, "the lag stepped by a fixed alpha a host frame, tuned for 60 fps", []( Perturb& p ) { p.camera.perFrameAlpha = true; } );
	add( "thermal", runThermal, "a transparent pixel keeps its colour's temperature", []( Perturb& p ) { p.plugin.lumaIgnoresAlpha = true; } );
	add( "clock", runClock, "the host's clock read in single precision", []( Perturb& p ) { p.plugin.floatClock = true; } );
	add( "clock", runClock, "a sensor frame every round( 60 / f ) host frames", []( Perturb& p ) { p.camera.hostFrameCounting = true; } );
	add( "clock", runClock, "the readout taken at the host frame, not the sensor instant", []( Perturb& p ) { p.camera.sampleAtHostTime = true; } );
	add( "shutter", runShutter, "no freeze: sensor frames read out through the FFC", []( Perturb& p ) { p.camera.noFreeze = true; } );
	add( "shutter", runShutter, "the freeze held for round( 60 D ) host frames", []( Perturb& p ) { p.camera.freezeHostFrames = true; } );
	add( "shutter", runShutter, "an FFC of 1 s", []( Perturb& p ) { p.camera.ffcDuration = 1.0; } );
	add( "agc", runAgc, "no plateau: plain histogram equalisation", []( Perturb& p ) { p.plugin.plateauModel = 1; } );
	add( "agc", runAgc, "the plateau applied to the cumulative histogram", []( Perturb& p ) { p.plugin.plateauModel = 2; } );
	add( "agc", runAgc, "the transfer damped by 0.85 a host frame", []( Perturb& p ) { p.plugin.dampingModel = 1; } );
	add( "agc", runAgc, "no damping", []( Perturb& p ) { p.plugin.dampingModel = 2; } );
	add( "manual", runManual, "Width read as a half-width", []( Perturb& p ) { p.plugin.widthHalf = true; } );
	add( "halo", runHalo, "the halo's Gaussian 1.5 x wider", []( Perturb& p ) { p.plugin.haloSigmaScale = 1.5; } );
	add( "halo", runHalo, "the unsharp mask's sign flipped", []( Perturb& p ) { p.plugin.haloSign = -1.0; } );
	add( "narcissus", runNarcissus, "its radius in frame widths", []( Perturb& p ) { p.plugin.narcissusModel = 1; } );
	add( "narcissus", runNarcissus, "removed by an FFC", []( Perturb& p ) { p.plugin.narcissusModel = 2; } );
	add( "narcissus", runNarcissus, "off centre by 0.05 H", []( Perturb& p ) { p.plugin.narcissusModel = 3; } );
	add( "drift", runDrift, "the drift growing as sqrt( age )", []( Perturb& p ) { p.camera.driftSqrt = true; } );
	add( "drift", runDrift, "the drift's age never reset", []( Perturb& p ) { p.camera.driftNoReset = true; } );
	add( "drift", runDrift, "the pattern all per pixel, no column part", []( Perturb& p ) { p.plugin.driftModel = 1; } );
	add( "drift", runDrift, "an FFC keeps the pattern", []( Perturb& p ) { p.camera.driftKeepsPattern = true; } );
	add( "drift", runDrift, "the drift's age never capped", []( Perturb& p ) { p.camera.driftNoCap = true; } );
	add( "palette", runPalette, "an index off by one", []( Perturb& p ) { p.plugin.paletteModel = 1; } );
	add( "palette", runPalette, "the table reversed", []( Perturb& p ) { p.plugin.paletteModel = 2; } );
	add( "alpha", runAlpha, "the clip's alpha handed through", []( Perturb& p ) { p.plugin.alphaPassThrough = true; } );
	add( "alpha", runAlpha, "a transparent pixel keeps its colour's temperature", []( Perturb& p ) { p.plugin.lumaIgnoresAlpha = true; } );
	add( "resize", runResize, "a new raster starts from cleared buffers", []( Perturb& p ) { p.plugin.resizeClearsState = true; } );
	add( "resize", runResize, "a new raster re-primes the AGC", []( Perturb& p ) { p.plugin.resizeResetsLut = true; } );

	if( offlineOnly )
		cases.erase( std::remove_if( cases.begin(), cases.end(), []( const Case& c ) { return !isOffline( c.name ); } ), cases.end() );

	int unfalsifiable = 0;
	for( const Case& c : cases )
	{
		std::printf( "\n=== negative control: %s -- %s\n", c.name, c.what );
		const int before = g_failures;
		g_failures       = 0;
		c.check( c.perturb );
		const int observed = g_failures;
		g_failures         = before;
		if( observed > 0 )
			std::printf( "  ok    %s failed %d check%s, as it must\n", c.name, observed, observed == 1 ? "" : "s" );
		else
		{
			std::printf( "  FAIL  %s PASSED against a wrong model -- it cannot fail, so it is not a check\n", c.name );
			++unfalsifiable;
		}
	}
	std::printf( "\nnegative controls: %zu wrong models, %d of them undetected\n", cases.size(), unfalsifiable );
	std::printf( "\n  %s\n", unfalsifiable == 0 ? "PASS" : "FAIL" );
	return unfalsifiable == 0 ? 0 : 1;
}

int runOffline()
{
	int failed = 0;
	for( const CheckEntry& check : checks() )
		if( check.offline )
			failed |= check.run( Perturb {} );
	failed |= runNegative( true );
	std::printf( "\n  offline: the checks that need no GL context. The picture checks were NOT run --\n"
	             "  tools/verify.sh runs them against a real driver, at 1280x720 and 320x180, and again on the software renderer.\n"
	             "\n  %s\n",
	             failed == 0 ? "PASS" : "FAIL" );
	return failed == 0 ? 0 : 1;
}

//===========================================================================
// --bench: GPU time per frame from GL_TIME_ELAPSED.
//===========================================================================
int runBench()
{
	struct Size
	{
		int w, h;
		const char* name;
	};
	const Size sizes[] = { { 1280, 720, "720p" }, { 1920, 1080, "1080p" } };
	struct Case
	{
		const char* what;
		std::function< void( Rig& ) > set;
	};
	const Case cases[] = {
		{ "defaults (9 Hz, Auto, Halo 1)", []( Rig& ) {} },
		{ "60 Hz: every pass every frame, Halo 3", []( Rig& r ) {
			 r.Set( PT_FRAME_RATE, static_cast< int >( FrameRate::Hz60 ) );
			 r.Set( PT_HALO, 1.0f );
		 } },
	};
	std::printf( "\n=== bench: GPU time per frame (GL_TIME_ELAPSED), the harness's card moving under it at 60 fps; 120 frames of warm-up, 240 timed\n" );
	GLuint query = 0;
	glGenQueries( 1, &query );
	for( const Case& c : cases )
		for( const Size& size : sizes )
		{
			Rig rig;
			if( !rig.Init( size.w, size.h ) )
				return 1;
			defaults( rig );
			c.set( rig );
			std::vector< Floats > cards;
			for( int i = 0; i < 8; ++i )
				cards.push_back( buildCard( size.w, size.h, i * 0.5 ) );
			for( int f = 0; f < 120; ++f )
			{
				rig.Upload( cards[ static_cast< size_t >( f % 8 ) ] );
				if( !rig.Render( 1 ) )
					return 1;
			}
			std::vector< double > gpu, readout, held;
			for( int f = 0; f < 240; ++f )
			{
				rig.Upload( cards[ static_cast< size_t >( f % 8 ) ] );
				glFinish();
				glBeginQuery( GL_TIME_ELAPSED, query );
				if( !rig.Render( 1 ) )
					return 1;
				glEndQuery( GL_TIME_ELAPSED );
				GLuint64 ns = 0;
				glGetQueryObjectui64v( query, GL_QUERY_RESULT, &ns );
				const double ms = static_cast< double >( ns ) * 1e-6;
				gpu.push_back( ms );
				( rig.plugin.LastFrame().readout ? readout : held ).push_back( ms );
			}
			std::sort( gpu.begin(), gpu.end() );
			std::sort( readout.begin(), readout.end() );
			std::sort( held.begin(), held.end() );
			std::printf( "  %-40s %-6s GPU median %6.3f ms, worst %6.3f (%4.1f%% of a 60 fps frame); readout frames %6.3f ms median (%zu), held frames %6.3f ms (%zu)\n",
			             c.what, size.name, gpu[ gpu.size() / 2 ], gpu.back(), 100.0 * gpu[ gpu.size() / 2 ] / ( 1000.0 / 60.0 ),
			             readout.empty() ? 0.0 : readout[ readout.size() / 2 ], readout.size(), held.empty() ? 0.0 : held[ held.size() / 2 ], held.size() );
		}
	glDeleteQueries( 1, &query );
	return 0;
}
