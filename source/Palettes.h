#pragma once

/**
    The four palettes, as 256-entry tables an 8-bit display level indexes.

    White Hot and Black Hot are grey ramps. Rainbow is LeptonModule's
    `colormap_rainbow` unchanged. Ironbow is the iron half of LeptonModule's
    `colormap_ironblack` (entries 127-254, black to (255, 253, 235)), spread
    over 256 entries by linear interpolation: entries 0-126 of that table are
    a white-to-black grey ramp, and entry 255, (255, 255, 24), is dropped (its
    blue falls from 235 to 24, which reads as a typo).

    Whether these are FLIR's own on-camera palettes is not established; they
    are LeptonModule's (BSD-2-Clause, Palettes.cpp carries the notice).
*/
namespace bolometer::palettes
{

constexpr int kEntries     = 256;
constexpr int kIronEntries = 128;
constexpr int kPalettes    = 4;///< in the order of the Palette option

extern const unsigned char kRainbow[ kEntries ][ 3 ];
extern const unsigned char kIron[ kIronEntries ][ 3 ];

/// The four palettes as RGBA8 rows, alpha 255.
void Build( unsigned char out[ kPalettes ][ kEntries ][ 4 ] );

} // namespace bolometer::palettes
