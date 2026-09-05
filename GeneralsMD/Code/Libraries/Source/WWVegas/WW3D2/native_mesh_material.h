#pragma once
#include "shader.h"
#include "native_pipeline_description.h"
#include <algorithm>

inline NativePipelineDescription Describe_Native_Mesh_Pipeline(const ShaderClass& shader,
	float alpha, bool additive, bool forceMultiply, bool mirrored)
{
	ShaderClass effective = shader;
	const bool multiply = forceMultiply && shader.Get_Dst_Blend_Func()==ShaderClass::DSTBLEND_ZERO;
	if (multiply) {
		effective.Set_Src_Blend_Func(ShaderClass::SRCBLEND_ZERO);
		effective.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_SRC_COLOR);
	}
	if (alpha!=1.0f && !additive) {
		effective.Set_Src_Blend_Func(ShaderClass::SRCBLEND_SRC_ALPHA);
		effective.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA);
	}
	auto pipeline = effective.Get_Native_Pipeline(mirrored);
	// Preserve the engine's two-term forced multiply, rather than silently
	// substituting a single destination-color multiplication.
	if (multiply && alpha==1.0f) pipeline.source = D3D12_BLEND_DEST_COLOR;
	pipeline.alphaReference = static_cast<UINT8>((std::max)(0.0f,(std::min)(255.0f,96.0f*alpha)));
	return pipeline;
}

inline void Describe_Native_Mesh_Opacity(NativeLightingState& lighting, float alpha, bool additive)
{
	if (alpha==1.0f) return;
	if (additive) lighting.diffuse[0]=lighting.diffuse[1]=lighting.diffuse[2]=alpha;
	lighting.diffuse[3]=alpha;
}
