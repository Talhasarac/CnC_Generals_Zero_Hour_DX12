#pragma once

// Store a packed little-endian pixel without assuming alignment or writing into
// the following pixel. In particular, BGR24 must write exactly three bytes.
inline void WritePackedSurfacePixel(unsigned char* destination, unsigned int color,
	unsigned int bytesPerPixel)
{
	if (bytesPerPixel < 1 || bytesPerPixel > 4) return;
	for (unsigned int byte = 0; byte < bytesPerPixel; ++byte)
		destination[byte] = static_cast<unsigned char>(color >> (byte * 8));
}
