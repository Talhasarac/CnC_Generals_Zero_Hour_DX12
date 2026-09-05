#pragma once
#include "ww3dformat.h"

// Bias signed gradients before UNORM filtering/mip generation. Decoding in HLSL
// is linear, so interpolation across negative/positive gradients remains valid.
// BGRA stores U in red, V in green, and luminance in blue; alpha is not a gradient.
inline bool EncodeNativeBumpPixel(unsigned char* bgra, const unsigned char* source, WW3DFormat format)
{
	int u, v;
	unsigned luminance = 255;
	if (format == WW3D_FORMAT_U8V8 || format == WW3D_FORMAT_X8L8V8U8) {
		u = source[0] < 128 ? source[0] : int(source[0])-256;
		v = source[1] < 128 ? source[1] : int(source[1])-256;
		if (format == WW3D_FORMAT_X8L8V8U8) luminance = source[2];
	} else if (format == WW3D_FORMAT_L6V5U5) {
		const unsigned packed = unsigned(source[0]) | (unsigned(source[1]) << 8);
		u = int(packed & 31); if (u >= 16) u -= 32;
		v = int((packed >> 5) & 31); if (v >= 16) v -= 32;
		u = u == -16 ? -128 : u*127/15;
		v = v == -16 ? -128 : v*127/15;
		luminance = ((packed >> 10)*255+31)/63;
	} else return false;
	bgra[0] = static_cast<unsigned char>(luminance);
	bgra[1] = static_cast<unsigned char>(v+128);
	bgra[2] = static_cast<unsigned char>(u+128);
	bgra[3] = 255;
	return true;
}
