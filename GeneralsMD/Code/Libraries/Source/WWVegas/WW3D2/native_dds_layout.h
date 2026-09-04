#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <algorithm>

// Disk layout only: native uploads retain BC blocks without a graphics API
// conversion. Compute each mip from its dimensions, never by dividing the
// preceding byte count by four (incorrect for rectangular and sub-block mips).
struct NativeDDSLayout {
	struct Level { unsigned width, height, offset, rowPitch, size; };
	std::array<Level,15> levels = {};
	unsigned count = 0, dxt = 0, dataBytes = 0;
	bool Parse(const unsigned char* header, size_t headerBytes, size_t fileBytes) {
		*this = {};
		if (!header || headerBytes < 128 || fileBytes < 128 || std::memcmp(header,"DDS ",4)) return false;
		const auto u32 = [header](unsigned offset) { uint32_t value; std::memcpy(&value,header+offset,4); return value; };
		if (u32(4)!=124 || u32(76)!=32 || !(u32(80)&4) || (u32(112)&0x20fe00) || u32(24)>1) return false;
		unsigned width=u32(16), height=u32(12), mipCount=u32(28);
		if (!width || !height || width>16384 || height>16384) return false;
		unsigned maxMips=1;
		for (unsigned edge=(std::max)(width,height);edge>1;edge>>=1) ++maxMips;
		if (!mipCount) mipCount=1;
		if (mipCount>maxMips) return false;
		const unsigned fourcc=u32(84);
		if ((fourcc&0x00ffffff)!=0x00545844 || (fourcc>>24)<'1' || (fourcc>>24)>'5') return false;
		dxt=(fourcc>>24)-'0';
		const unsigned blockBytes=dxt==1 ? 8 : 16;
		unsigned offset=0;
		for (unsigned mip=0;mip<mipCount;++mip) {
			const unsigned rowPitch=((width+3)/4)*blockBytes;
			const unsigned size=rowPitch*((height+3)/4);
			if (offset>fileBytes-128 || size>fileBytes-128-offset) { *this={}; return false; }
			levels[mip]={width,height,offset,rowPitch,size};
			offset+=size;
			width=(std::max)(1u,width/2); height=(std::max)(1u,height/2);
		}
		count=mipCount; dataBytes=offset;
		return true;
	}
};
