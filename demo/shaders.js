// GENERATED from source/Shaders.cpp by demo/tools/check_shaders.py --write.
// Do not edit: tools/verify.sh fails if a character of this differs from the
// plugin's. The one escape allowed is \` for a backtick inside a comment.
export const VERSION = '#version 410 core\n';

// kQuadVertex, source/Shaders.cpp
export const QUAD_VERTEX = `
void main()
{
	vec2 p = vec2( float( ( gl_VertexID << 1 ) & 2 ), float( gl_VertexID & 2 ) );
	gl_Position = vec4( p * 2.0 - 1.0, 0.0, 1.0 );
}
`;

// kHistogramVertex, source/Shaders.cpp
export const HISTOGRAM_VERTEX = `
uniform sampler2D Raw;
uniform ivec2 Size;     // the readout, pixels
uniform ivec2 Grid;     // the sample grid
uniform float HistLow;  // degC at the bottom of bin 0
uniform float HistScale;// bins a kelvin
uniform int Bins;
void main()
{
	int i    = gl_VertexID % Grid.x;
	int j    = gl_VertexID / Grid.x;
	ivec2 at = ivec2( ( 2 * i + 1 ) * Size.x / ( 2 * Grid.x ), ( 2 * j + 1 ) * Size.y / ( 2 * Grid.y ) );
	float T  = texelFetch( Raw, at, 0 ).r;
	int k    = clamp( int( floor( ( T - HistLow ) * HistScale ) ), 0, Bins - 1 );
	gl_Position  = vec4( ( float( k ) + 0.5 ) / float( Bins ) * 2.0 - 1.0, 0.0, 0.0, 1.0 );
	gl_PointSize = 1.0;
}
`;

// kCommon, source/Shaders.cpp
export const COMMON = `
// The clip's temperature: Rec. 709 luma of the encoded values, times alpha
// (a transparent pixel is a cold one), mapped onto low + span.
float sceneTemp( vec4 src, float low, float span, int model )
{
	float y = dot( src.rgb, vec3( 0.2126, 0.7152, 0.0722 ) );
	return low + span * ( model == 1 ? y : y * src.a );
}
uint pcg( uint v )
{
	uint state = v * 747796405u + 2891336453u;
	uint word  = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}
// A uniform variate of zero mean and unit variance, on a 2^24 grid.
float unitVariate( uint h )
{
	return ( ( float( h >> 8u ) + 0.5 ) * ( 2.0 / 16777216.0 ) - 1.0 ) * 1.7320508;
}
`;

// kReadoutFragment, source/Shaders.cpp
export const READOUT_FRAGMENT = `
uniform sampler2D InputTexture;
uniform sampler2D State;
uniform vec3 Weights;     // the previous state, the clip, the shutter
uniform float ShutterTemp;
uniform float TempLow;
uniform float TempSpan;
uniform int LumaModel;    // 0 the plugin; 1 a negative control
uniform float DriftAmp;   // K RMS
uniform uint DriftSeed;
uniform int DriftModel;   // 0 the plugin; 1 a negative control
uniform float NarcDepth;  // K
uniform vec2 NarcCentre;  // pixels
uniform float NarcR;      // the spot's radius, pixels
uniform float NarcB;      // the defocus disc's radius, pixels
out vec4 FragColor;

// The area of the defocus disc (radius b, centred r from the spot's centre)
// inside the spot (radius R >= b), over the defocus disc's area: the spot
// convolved with a uniform disc.
float discOverlap( float r, float R, float b )
{
	if( r >= R + b )
		return 0.0;
	if( r <= R - b )
		return 1.0;
	float c1 = clamp( ( r * r + b * b - R * R ) / ( 2.0 * r * b ), -1.0, 1.0 );
	float c2 = clamp( ( r * r + R * R - b * b ) / ( 2.0 * r * R ), -1.0, 1.0 );
	float k  = sqrt( max( ( -r + b + R ) * ( r + b - R ) * ( r - b + R ) * ( r + b + R ), 0.0 ) );
	return ( b * b * acos( c1 ) + R * R * acos( c2 ) - 0.5 * k ) / ( 3.14159265 * b * b );
}

void main()
{
	ivec2 at   = ivec2( gl_FragCoord.xy );
	float S    = sceneTemp( texelFetch( InputTexture, at, 0 ), TempLow, TempSpan, LumaModel );
	float prev = texelFetch( State, at, 0 ).r;
	float T    = Weights.x * prev + Weights.y * S + Weights.z * ShutterTemp;
	uint col   = pcg( uint( at.x ) ^ pcg( DriftSeed * 2u ) );
	uint pix   = pcg( ( uint( at.y ) * 65536u + uint( at.x ) ) ^ pcg( DriftSeed * 2u + 1u ) );
	float fpn  = DriftModel == 1 ? unitVariate( pix ) : 0.70710678 * ( unitVariate( col ) + unitVariate( pix ) );
	float r    = length( vec2( at ) + 0.5 - NarcCentre );
	FragColor  = vec4( T + DriftAmp * fpn - NarcDepth * discOverlap( r, NarcR, NarcB ), 0.0, 0.0, 1.0 );
}
`;

// kStateFragment, source/Shaders.cpp
export const STATE_FRAGMENT = `
uniform sampler2D InputTexture;
uniform sampler2D State;
uniform vec3 Weights;
uniform float ShutterTemp;
uniform float TempLow;
uniform float TempSpan;
uniform int LumaModel;
out vec4 FragColor;
void main()
{
	ivec2 at   = ivec2( gl_FragCoord.xy );
	float S    = sceneTemp( texelFetch( InputTexture, at, 0 ), TempLow, TempSpan, LumaModel );
	float prev = texelFetch( State, at, 0 ).r;
	FragColor  = vec4( Weights.x * prev + Weights.y * S + Weights.z * ShutterTemp, 0.0, 0.0, 1.0 );
}
`;

// kHistogramFragment, source/Shaders.cpp
export const HISTOGRAM_FRAGMENT = `
out vec4 FragColor;
void main()
{
	FragColor = vec4( 1.0, 0.0, 0.0, 1.0 );
}
`;

// kLutFragment, source/Shaders.cpp
export const LUT_FRAGMENT = `
uniform sampler2D Histogram;
uniform sampler2D PrevLut;
uniform float Plateau;  // the clip, in counts
uniform float Keep;     // d^m: how much of the last transfer stays
uniform int Bins;
uniform int PlateauModel;// 0 the plugin; 1 and 2 negative controls
out vec4 FragColor;
void main()
{
	int k       = int( gl_FragCoord.x );
	float below = 0.0, own = 0.0, total = 0.0;
	for( int j = 0; j < Bins; ++j )
	{
		float h = texelFetch( Histogram, ivec2( j, 0 ), 0 ).r;
		if( PlateauModel == 0 )
			h = min( h, Plateau );
		total += h;
		if( j < k )
			below += h;
		if( j == k )
			own = h;
	}
	float top = below + own;
	if( PlateauModel == 2 )
	{
		below = min( below, Plateau );
		top   = min( top, Plateau );
		total = min( total, Plateau );
	}
	vec2 target = vec2( below, top ) / max( total, 1e-30 );
	vec2 prev   = texelFetch( PrevLut, ivec2( k, 0 ), 0 ).rg;
	FragColor   = vec4( target + ( prev - target ) * Keep, 0.0, 1.0 );
}
`;

// kDisplayFragment, source/Shaders.cpp
export const DISPLAY_FRAGMENT = `
uniform sampler2D Raw;
uniform sampler2D Lut;
uniform int Auto;
uniform float Level;
uniform float Width;
uniform float HistLow;
uniform float HistScale;
uniform int Bins;
out vec4 FragColor;
void main()
{
	ivec2 at = ivec2( gl_FragCoord.xy );
	float T  = texelFetch( Raw, at, 0 ).r;
	float y;
	if( Auto == 1 )
	{
		float pos = ( T - HistLow ) * HistScale;
		if( pos <= 0.0 )
			y = 0.0;
		else if( pos >= float( Bins ) )
			y = 1.0;
		else
		{
			int k   = int( floor( pos ) );
			float u = pos - float( k );
			vec2 lh = texelFetch( Lut, ivec2( k, 0 ), 0 ).rg;
			y       = lh.x + ( lh.y - lh.x ) * u;
		}
	}
	else
		y = clamp( ( T - Level ) / Width + 0.5, 0.0, 1.0 );
	FragColor = vec4( y, 0.0, 0.0, 1.0 );
}
`;

// kBlurFragment, source/Shaders.cpp
export const BLUR_FRAGMENT = `
uniform sampler2D Src;
uniform ivec2 Dir;
uniform int Radius;
uniform float Sigma;
uniform ivec2 Size;
out vec4 FragColor;
void main()
{
	ivec2 at    = ivec2( gl_FragCoord.xy );
	float k     = -0.5 / ( Sigma * Sigma );
	float acc   = 0.0, total = 0.0;
	for( int i = -Radius; i <= Radius; ++i )
	{
		float w = exp( k * float( i * i ) );
		acc += w * texelFetch( Src, clamp( at + Dir * i, ivec2( 0 ), Size - 1 ), 0 ).r;
		total += w;
	}
	FragColor = vec4( acc / total, 0.0, 0.0, 1.0 );
}
`;

// kShownFragment, source/Shaders.cpp
export const SHOWN_FRAGMENT = `
uniform sampler2D Y;
uniform sampler2D BlurH;
uniform sampler2D PaletteTable;
uniform int Radius;
uniform float Sigma;
uniform ivec2 Size;
uniform float HaloGain;  // a
uniform int PaletteRow;
uniform int PaletteModel;// 0 the plugin; 1 and 2 negative controls
out vec4 FragColor;
void main()
{
	ivec2 at = ivec2( gl_FragCoord.xy );
	float y  = texelFetch( Y, at, 0 ).r;
	float v  = y;
	if( HaloGain != 0.0 )
	{
		float k   = -0.5 / ( Sigma * Sigma );
		float acc = 0.0, total = 0.0;
		for( int i = -Radius; i <= Radius; ++i )
		{
			float w = exp( k * float( i * i ) );
			acc += w * texelFetch( BlurH, clamp( at + ivec2( 0, i ), ivec2( 0 ), Size - 1 ), 0 ).r;
			total += w;
		}
		v = y + HaloGain * ( y - acc / total );
	}
	int level = int( floor( clamp( v, 0.0, 1.0 ) * 255.0 + 0.5 ) );
	if( PaletteModel == 1 )
		level = min( level + 1, 255 );
	else if( PaletteModel == 2 )
		level = 255 - level;
	FragColor = vec4( texelFetch( PaletteTable, ivec2( level, PaletteRow ), 0 ).rgb, 1.0 );
}
`;

// kCompositeFragment, source/Shaders.cpp
export const COMPOSITE_FRAGMENT = `
uniform sampler2D InputTexture;
uniform sampler2D Shown;
uniform ivec2 ViewOrigin;// the host viewport's corner
uniform float MixAmount;
uniform int AlphaModel;  // 0 the plugin; 1 a negative control
out vec4 FragColor;
void main()
{
	ivec2 at = ivec2( gl_FragCoord.xy ) - ViewOrigin;
	vec4 src = texelFetch( InputTexture, at, 0 );
	if( MixAmount <= 0.0 )
	{
		FragColor = src;
		return;
	}
	vec3 cam    = texelFetch( Shown, at, 0 ).rgb;
	float alpha = src.a + ( 1.0 - src.a ) * MixAmount;
	vec3 premul = src.rgb * src.a * ( 1.0 - MixAmount ) + cam * MixAmount;
	vec3 rgb    = alpha > 0.0 ? premul / alpha : src.rgb;
	FragColor   = vec4( rgb, AlphaModel == 1 ? src.a : alpha );
}
`;

// kResampleFragment, source/Shaders.cpp
export const RESAMPLE_FRAGMENT = `
uniform sampler2D Src;
uniform ivec2 SrcSize;
uniform ivec2 DstSize;
out vec4 FragColor;
void main()
{
	vec2 p   = gl_FragCoord.xy * vec2( SrcSize ) / vec2( DstSize ) - 0.5;
	vec2 b   = floor( p );
	vec2 f   = p - b;
	ivec2 i0 = ivec2( b );
	ivec2 hi = SrcSize - 1;
	vec4 a   = texelFetch( Src, clamp( i0, ivec2( 0 ), hi ), 0 );
	vec4 c   = texelFetch( Src, clamp( i0 + ivec2( 1, 0 ), ivec2( 0 ), hi ), 0 );
	vec4 d   = texelFetch( Src, clamp( i0 + ivec2( 0, 1 ), ivec2( 0 ), hi ), 0 );
	vec4 e   = texelFetch( Src, clamp( i0 + ivec2( 1, 1 ), ivec2( 0 ), hi ), 0 );
	FragColor = mix( mix( a, c, f.x ), mix( d, e, f.x ), f.y );
}
`;
