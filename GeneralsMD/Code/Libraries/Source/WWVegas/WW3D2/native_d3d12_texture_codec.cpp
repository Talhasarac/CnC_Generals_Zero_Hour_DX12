#include "native_d3d12_texture_codec.h"
#include <algorithm>
#include <cstring>

bool NativeD3D12TextureCodec::DecodeBgra(DXGI_FORMAT format, UINT width, UINT height,
	const NativeD3D12TextureLevel& source, std::vector<unsigned char>& bgra)
{
	const bool bc1 = format == DXGI_FORMAT_BC1_UNORM;
	const bool bc2 = format == DXGI_FORMAT_BC2_UNORM;
	const bool bc3 = format == DXGI_FORMAT_BC3_UNORM;
	const bool compressed = bc1 || bc2 || bc3;
	UINT bytes = 0;
	switch (format) {
	case DXGI_FORMAT_BC1_UNORM: bytes = 8; break;
	case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC3_UNORM: bytes = 16; break;
	case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8X8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM: bytes = 4; break;
	case DXGI_FORMAT_B5G6R5_UNORM: case DXGI_FORMAT_B5G5R5A1_UNORM:
	case DXGI_FORMAT_B4G4R4A4_UNORM: bytes = 2; break;
	case DXGI_FORMAT_A8_UNORM: case DXGI_FORMAT_R8_UNORM: bytes = 1; break;
	default: return false;
	}
	if (!width || !height || width > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
		height > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION || !source.data) return false;
	const UINT rows = compressed ? (height+3)/4 : height;
	const UINT rowBytes = (compressed ? (width+3)/4 : width)*bytes;
	if (source.rowPitch < rowBytes || (source.slicePitch &&
		UINT64(rows-1)*source.rowPitch+rowBytes > source.slicePitch)) return false;
	bgra.resize(size_t(width)*height*4);
	const auto* data = static_cast<const unsigned char*>(source.data);
	auto word = [](const unsigned char* p) { return UINT(p[0]) | UINT(p[1])<<8; };
	auto rgb565 = [](UINT value, unsigned char* color) {
		color[0] = static_cast<unsigned char>(((value&31)<<3) | ((value&31)>>2));
		color[1] = static_cast<unsigned char>((((value>>5)&63)<<2) | (((value>>5)&63)>>4));
		color[2] = static_cast<unsigned char>((((value>>11)&31)<<3) | (((value>>11)&31)>>2));
		color[3] = 255;
	};
	if (!compressed) {
		for (UINT y=0;y<height;++y) for (UINT x=0;x<width;++x) {
			const auto* p = data+size_t(y)*source.rowPitch+x*bytes;
			auto* out = bgra.data()+(size_t(y)*width+x)*4;
			if (bytes == 4) {
				std::memcpy(out,p,4);
				if (format == DXGI_FORMAT_R8G8B8A8_UNORM) std::swap(out[0],out[2]);
				if (format == DXGI_FORMAT_B8G8R8X8_UNORM) out[3] = 255;
			} else if (bytes == 1) {
				out[0] = out[1] = out[2] = format == DXGI_FORMAT_A8_UNORM ? 255 : *p;
				out[3] = format == DXGI_FORMAT_A8_UNORM ? *p : 255;
			} else {
				const UINT value = word(p);
				if (format == DXGI_FORMAT_B5G6R5_UNORM) rgb565(value,out);
				else if (format == DXGI_FORMAT_B4G4R4A4_UNORM)
					for (UINT c=0;c<4;++c) out[c] = static_cast<unsigned char>(((value>>(c*4))&15)*17);
				else {
					for (UINT c=0;c<3;++c) {
						const UINT v = (value>>(c*5))&31;
						out[c] = static_cast<unsigned char>((v<<3)|(v>>2));
					}
					out[3] = value&0x8000 ? 255 : 0;
				}
			}
		}
		return true;
	}
	// Decode complete blocks into clipped output. Small/odd mip dimensions must
	// never write the unused pixels of the final 4x4 block into adjacent rows.
	for (UINT by=0;by<height;by+=4) for (UINT bx=0;bx<width;bx+=4) {
		const auto* block = data+size_t(by/4)*source.rowPitch+(bx/4)*bytes;
		const auto* colorBlock = block+(bc1 ? 0 : 8);
		const UINT c0 = word(colorBlock), c1 = word(colorBlock+2);
		unsigned char colors[4][4] = {};
		rgb565(c0,colors[0]); rgb565(c1,colors[1]);
		for (UINT c=0;c<3;++c) {
			colors[2][c] = static_cast<unsigned char>((bc1 && c0<=c1) ?
				(UINT(colors[0][c])+colors[1][c])/2 : (2*UINT(colors[0][c])+colors[1][c])/3);
			colors[3][c] = bc1 && c0<=c1 ? 0 :
				static_cast<unsigned char>((UINT(colors[0][c])+2*UINT(colors[1][c]))/3);
		}
		colors[2][3] = 255; colors[3][3] = bc1 && c0<=c1 ? 0 : 255;
		UINT alphas[8] = {block[0],block[1]};
		UINT64 alphaBits = 0;
		if (bc3) {
			for (UINT i=0;i<6;++i) alphaBits |= UINT64(block[2+i])<<(i*8);
			if (alphas[0]>alphas[1])
				for (UINT i=2;i<8;++i) alphas[i] = ((8-i)*alphas[0]+(i-1)*alphas[1])/7;
			else {
				for (UINT i=2;i<6;++i) alphas[i] = ((6-i)*alphas[0]+(i-1)*alphas[1])/5;
				alphas[6] = 0; alphas[7] = 255;
			}
		}
		for (UINT y=0;y<4 && by+y<height;++y) for (UINT x=0;x<4 && bx+x<width;++x) {
			const UINT pixel = y*4+x, selector = (colorBlock[4+y]>>(x*2))&3;
			auto* out = bgra.data()+(size_t(by+y)*width+bx+x)*4;
			std::memcpy(out,colors[selector],4);
			if (bc2) out[3] = static_cast<unsigned char>(((block[pixel/2]>>((pixel%2)*4))&15)*17);
			if (bc3) out[3] = static_cast<unsigned char>(alphas[(alphaBits>>(pixel*3))&7]);
		}
	}
	return true;
}
