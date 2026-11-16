#include "Bolometer.h"

/**
    The registration. `bolometer_core` is an OBJECT library so this file-scope
    constructor, which nothing references by name, is linked into the bundle
    (an archive would let the linker drop it, and the bundle would load and
    report no plugins).
*/
static CFFGLPluginInfo PluginInfo(
	PluginFactory< bolometer::BolometerPlugin >,// Create method
	"BO01",                                     // Plugin unique ID of maximum length 4
	"SW Bolometer",                             // Plugin name
	2,                                          // API major version number
	1,                                          // API minor version number
	0,                                          // Plugin major version number
	1,                                          // Plugin minor version number
	FF_EFFECT,                                  // Plugin type
	"A thermal camera with an uncooled microbolometer core, looking at the clip. Brightness is read as temperature; "
	"every pixel warms and cools with a 12 ms time constant; the picture is read out at the export-limited 9 Hz and held; "
	"plateau equalisation shares out the greys, so the picture jumps in contrast when something hot comes in; every so "
	"often the shutter closes and the frame freezes; detail enhancement rings hot things with dark halos, and the "
	"narcissus spot sits dead centre.",
	"Bolometer FFGL effect"// About
);

extern "C" const char* BolometerBuildStamp()
{
	return "bolometer " BOLOMETER_VERSION ", built " __DATE__ " " __TIME__;
}
