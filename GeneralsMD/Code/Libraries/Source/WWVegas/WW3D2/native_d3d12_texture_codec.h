#pragma once

#include "native_d3d12_resources.h"

namespace NativeD3D12TextureCodec {
// CPU format conversion only; no device, descriptors, frame, or renderer state.
bool DecodeBgra(DXGI_FORMAT format, UINT width, UINT height,
	const NativeD3D12TextureLevel& source, std::vector<unsigned char>& bgra);
}
