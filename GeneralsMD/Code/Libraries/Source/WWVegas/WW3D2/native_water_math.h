#pragma once
#include "native_draw_state.h"
#include <vector>

inline bool Build_Native_Sea_Gradients(const std::vector<unsigned char>& bgra,
	UINT width, UINT height, std::vector<unsigned char>& result)
{
	if (!width || !height || size_t(width)>SIZE_MAX/height/4 || bgra.size()!=size_t(width)*height*4) return false;
	std::vector<unsigned char> gradients(bgra.size());
	const auto value=[&](UINT x,UINT y) { return int(bgra[(size_t(y)*width+x)*4]); };
	for (UINT y=0;y<height;++y) for (UINT x=0;x<width;++x) {
		const int du=value((x+width-1)%width,y)-value((x+1)%width,y);
		const int dv=value(x,(y+height-1)%height)-value(x,(y+1)%height);
		auto* out=gradients.data()+(size_t(y)*width+x)*4;
		out[0]=static_cast<unsigned char>(128+du/2);
		out[1]=static_cast<unsigned char>(128+dv/2);
		out[2]=128; out[3]=255;
	}
	result.swap(gradients);
	return true;
}

inline NativeMaterialCoordinates Describe_Native_Sea_Projection(const std::array<float,16>& worldViewProjection)
{
	NativeMaterialCoordinates uv;
	uv.position=uv.transform=uv.projected=true; uv.offset=UINT_MAX;
	// Row-vector clip space to texture space. Projected material coordinates
	// divide by z, so carry clip-w there (not clip-z).
	for (UINT row=0;row<4;++row) {
		uv.matrix[row*4]=0.5f*(worldViewProjection[row*4]+worldViewProjection[row*4+3]);
		uv.matrix[row*4+1]=0.5f*(worldViewProjection[row*4+3]-worldViewProjection[row*4+1]);
		uv.matrix[row*4+2]=worldViewProjection[row*4+3];
		uv.matrix[row*4+3]=0;
	}
	return uv;
}
