#pragma once

#include "native_d3d12_renderer.h"

// Shadow counts occupy the bits not reserved for player-color occlusion.
// Compositing must neither increment those counts nor write scene depth.
inline void SetNativeShadowCompositeState(NativeD3D12Renderer& renderer, UINT8 occlusionMask)
{
	renderer.SetFixedFunctionState(D3D12_CULL_MODE_NONE, false, false,
		D3D12_COMPARISON_FUNC_ALWAYS, true, D3D12_BLEND_DEST_COLOR,
		D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
		D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN | D3D12_COLOR_WRITE_ENABLE_BLUE);
	renderer.SetStencilState(true, D3D12_COMPARISON_FUNC_NOT_EQUAL, 0,
		static_cast<UINT8>(~occlusionMask & 0x7f), 0,
		D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP);
	renderer.SetAlphaTestState(false, D3D12_COMPARISON_FUNC_ALWAYS, 0);
	renderer.SetLighting(NativeLightingState());
	renderer.SetMaterialEnabled(false);
	renderer.SetGrayscale(false);
	renderer.SetTreeSway(nullptr, 0);
	renderer.SetVertexFog(0, 0, 1, 1, 0, false);
}
