#pragma once
#include "native_d3d12_renderer.h"

// A draw's authored pipeline, independent of the legacy mutable state cache.
struct NativePipelineDescription
{
	D3D12_CULL_MODE cull = D3D12_CULL_MODE_NONE;
	bool depthTest = true;
	bool depthWrite = true;
	D3D12_COMPARISON_FUNC depthCompare = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	bool blend = false;
	D3D12_BLEND source = D3D12_BLEND_ONE;
	D3D12_BLEND destination = D3D12_BLEND_ZERO;
	D3D12_BLEND_OP operation = D3D12_BLEND_OP_ADD;
	UINT8 colorMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	bool alphaTest = false;
	D3D12_COMPARISON_FUNC alphaCompare = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
	UINT8 alphaReference = 0x60;

	void Apply(NativeD3D12Renderer& renderer) const
	{
		renderer.SetFixedFunctionState(cull, depthTest, depthWrite, depthCompare,
			blend, source, destination, operation, colorMask);
		renderer.SetAlphaTestState(alphaTest, alphaCompare, alphaReference);
	}
};
