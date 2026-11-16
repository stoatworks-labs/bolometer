#pragma once

#include <string>

/**
    The passes. The CPU has worked out every weight (Camera); the GPU applies
    them per pixel. No time reaches a shader.

    Every host frame:

    - **state** -- each bolometer's temperature at the host frame:
      prev x T + scene x S + shutter x T_shutter, into the other half of an
      R32F ping-pong. S = 15 degC + 25 K x luma x alpha (Rec. 709 luma of the
      clip's encoded values).
    - **composite** -- to the host: the held camera frame mixed over the clip,
      premultiplied; alpha = mix( a, 1, Mix ); Mix 0 hands the clip through.

    On a host frame that reads out a sensor frame, before the state:

    - **readout** -- the bolometers at the sensor instant, plus the drift
      (column and pixel fixed pattern, unit variance, times the amplitude),
      minus the narcissus spot (a disc blurred by a defocus disc), into R32F.

    and after it:

    - **histogram** -- one point per sample of a fixed 256 x 144 grid of the
      readout, into a 512 x 1 R32F, blended additively: exact counts.
    - **lut** -- per bin, the clipped cumulative histogram's ends over its
      total: plateau equalisation's transfer, damped against the last one by
      d^m (m sensor frames read out since).
    - **display** -- the readout through the transfer (Span Auto, linear
      within a bin) or Level and Width (Manual): the display level, R32F.
    - **blur** -- the detail enhancement's Gaussian, horizontally (only when
      Halo is on).
    - **shown** -- the Gaussian vertically, the unsharp mask y + a ( y - G y ),
      8-bit quantisation and the palette, into the held RGBA8 frame.

    And on a raster change, **resample**: the thermal state and the held frame
    are carried to the new raster bilinearly, never cleared.

    tools/glslc.sh's ASSEMBLED table mirrors Assemble().
*/
namespace bolometer::shaders
{

extern const char* const kVersion;
extern const char* const kQuadVertex;
extern const char* const kHistogramVertex;
extern const char* const kCommon;
extern const char* const kReadoutFragment;
extern const char* const kStateFragment;
extern const char* const kHistogramFragment;
extern const char* const kLutFragment;
extern const char* const kDisplayFragment;
extern const char* const kBlurFragment;
extern const char* const kShownFragment;
extern const char* const kCompositeFragment;
extern const char* const kResampleFragment;

enum class Pass
{
	Readout,
	State,
	Histogram,
	Lut,
	Display,
	Blur,
	Shown,
	Composite,
	Resample,
	Count
};
std::string AssembleVertex( Pass pass );
std::string Assemble( Pass pass );

} // namespace bolometer::shaders
