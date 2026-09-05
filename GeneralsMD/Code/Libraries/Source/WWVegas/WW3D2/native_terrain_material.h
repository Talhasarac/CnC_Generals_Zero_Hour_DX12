#pragma once
#include "native_draw_state.h"

// Main heightmap keeps its two atlas passes: UV0 establishes the surface,
// UV1 blends using texture alpha times vertex alpha. Do not let a previous
// material choose the alpha operands or coordinate set implicitly.
inline NativeMaterialDescription Describe_Native_Terrain_Layer(
	const NativeD3D12Texture* atlas, UINT uvOffset, bool blend,
	const NativeSamplerDesc& sampler)
{
	NativeMaterialDescription material;
	material.enabled=true;
	material.textures[0]=atlas;
	material.coordinates[0].offset=uvOffset;
	material.samplers[0]=sampler;
	material.samplers[0].clampU=material.samplers[0].clampV=true;
	auto& stage=material.stages[0];
	stage.colorOp=atlas ? NativeMaterialOp::Modulate : NativeMaterialOp::Select2;
	stage.colorArg1=UINT(NativeMaterialSource::Texture);
	stage.colorArg2=UINT(NativeMaterialSource::Diffuse);
	stage.alphaOp=blend ? NativeMaterialOp::Modulate : NativeMaterialOp::Select2;
	stage.alphaArg1=UINT(NativeMaterialSource::Texture);
	stage.alphaArg2=UINT(NativeMaterialSource::Diffuse);
	return material;
}

// Extra terrain blend layers use the road-style alpha compositing equations.
// The second pass retains the original texture-alpha masked lightmap multiply.
inline NativeMaterialDescription Describe_Native_Terrain_Overlay(
	const NativeD3D12Texture* atlas, UINT uvOffset, const NativeSamplerDesc& sampler,
	const NativeD3D12Texture* modulation, const NativeMaterialCoordinates& projection,
	const NativeSamplerDesc& modulationSampler, bool secondPass)
{
	auto material=Describe_Native_Terrain_Layer(atlas,uvOffset,true,sampler);
	if (modulation) {
		material.textures[1]=modulation;
		material.coordinates[1]=projection;
		material.samplers[1]=modulationSampler;
		auto& stage=material.stages[1];
		stage.colorOp=stage.alphaOp=NativeMaterialOp::Modulate;
		stage.colorArg1=stage.alphaArg1=UINT(NativeMaterialSource::Texture);
		stage.colorArg2=stage.alphaArg2=UINT(NativeMaterialSource::Current);
	}
	if (secondPass) {
		material.stages[0].colorOp=NativeMaterialOp::Select2;
		material.stages[0].colorArg2=UINT(NativeMaterialSource::Diffuse)|32u; // alpha replicate
		material.stages[0].alphaOp=NativeMaterialOp::Select1;
		material.stages[1].colorOp=NativeMaterialOp::BlendCurrentAlpha;
		material.stages[1].alphaOp=NativeMaterialOp::Disable;
	}
	return material;
}

// Flat terrain's baked tile texture is supplied by the tile at stage 1.
// Shroud modulates prelit diffuse before that tile, without touching alpha.
inline NativeMaterialDescription Describe_Native_Flat_Base(bool textured,
	const NativeD3D12Texture* shroud, const NativeMaterialCoordinates& projection,
	const NativeSamplerDesc& sampler)
{
	NativeMaterialDescription material;
	material.enabled=true;
	auto& base=material.stages[0];
	base.colorOp=textured && shroud ? NativeMaterialOp::Modulate : NativeMaterialOp::Select2;
	base.colorArg1=UINT(NativeMaterialSource::Texture);
	base.colorArg2=UINT(NativeMaterialSource::Diffuse);
	material.samplers[0]=material.samplers[1]=sampler;
	material.samplers[0].clampU=material.samplers[0].clampV=true;
	material.samplers[1].clampU=material.samplers[1].clampV=true;
	if (textured) {
		material.textures[0]=shroud;
		material.coordinates[0]=projection;
		material.stages[1].colorOp=NativeMaterialOp::Modulate;
	}
	return material;
}

// Cloud and light-map textures multiply the framebuffer in a second pass.
// Compact absent layers, so noise-only does not depend on a previous cloud pass.
inline NativeMaterialDescription Describe_Native_Terrain_Modulation(
	const NativeD3D12Texture* cloud, const NativeMaterialCoordinates& cloudUV,
	const NativeD3D12Texture* noise, const NativeMaterialCoordinates& noiseUV,
	NativeD3D12FilterMode mip)
{
	NativeMaterialDescription material;
	material.enabled=true;
	UINT stage=0;
	for (UINT layer=0;layer<2;++layer) {
		const auto* texture=layer ? noise : cloud;
		if (!texture) continue;
		material.textures[stage]=texture;
		material.coordinates[stage]=layer ? noiseUV : cloudUV;
		material.stages[stage].colorOp=stage ? NativeMaterialOp::Modulate : NativeMaterialOp::Select1;
		material.samplers[stage]={layer ? NativeD3D12FilterMode::Point : NativeD3D12FilterMode::Linear,
			NativeD3D12FilterMode::Linear,mip,false,false,1};
		++stage;
	}
	return material;
}
