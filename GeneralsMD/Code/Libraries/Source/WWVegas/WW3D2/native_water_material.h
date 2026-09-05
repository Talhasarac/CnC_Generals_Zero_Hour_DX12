#pragma once
#include "native_draw_state.h"

// Layer a restrained reflection over the authored river material, retaining
// its texture-alpha coverage and shoreline feathering instead of flooding banks.
inline NativeMaterialDescription Describe_Native_Water_Reflection(
	const NativeD3D12Texture* base, const NativeD3D12Texture* edge,
	const NativeD3D12Texture* reflection, UINT baseUV, UINT edgeUV,
	const NativeMaterialCoordinates& projectedUV)
{
	NativeMaterialDescription material;
	material.enabled=true;
	material.factor=0x2effffff; // 46/255 maximum reflection opacity (~18%).
	material.textures[0]=base;
	material.coordinates[0].offset=baseUV;
	material.samplers[0]={NativeD3D12FilterMode::Linear,NativeD3D12FilterMode::Linear,NativeD3D12FilterMode::Point,false,false,1};
	material.stages[0].colorOp=NativeMaterialOp::Select1;
	material.stages[0].colorArg1=UINT(NativeMaterialSource::Diffuse);
	material.stages[0].alphaOp=NativeMaterialOp::Modulate;
	material.stages[0].alphaArg1=UINT(NativeMaterialSource::Texture);
	material.stages[0].alphaArg2=UINT(NativeMaterialSource::Diffuse);
	UINT next=1;
	if (edge) {
		material.textures[next]=edge;
		material.coordinates[next].offset=edgeUV;
		material.samplers[next]=material.samplers[0];
		material.stages[next].colorOp=NativeMaterialOp::Select1;
		material.stages[next].colorArg1=UINT(NativeMaterialSource::Current);
		material.stages[next].alphaOp=NativeMaterialOp::Modulate;
		material.stages[next].alphaArg1=UINT(NativeMaterialSource::Texture);
		material.stages[next++].alphaArg2=UINT(NativeMaterialSource::Current);
	}
	material.textures[next]=reflection;
	material.coordinates[next]=projectedUV;
	material.samplers[next]={NativeD3D12FilterMode::Linear,NativeD3D12FilterMode::Linear,NativeD3D12FilterMode::Point,true,true,1};
	material.stages[next].colorOp=NativeMaterialOp::Select1;
	material.stages[next].colorArg1=UINT(NativeMaterialSource::Texture);
	material.stages[next].alphaOp=NativeMaterialOp::Modulate;
	material.stages[next].alphaArg1=UINT(NativeMaterialSource::Current);
	material.stages[next].alphaArg2=UINT(NativeMaterialSource::Factor);
	return material;
}

inline NativeMaterialDescription Describe_Native_Water_Material(
	const NativeD3D12Texture* base, const NativeD3D12Texture* edge,
	const NativeD3D12Texture* sparkle, const NativeD3D12Texture* noise,
	UINT baseUV, UINT edgeUV, const NativeMaterialCoordinates& noiseUV)
{
	NativeMaterialDescription material;
	material.enabled=true;
	for (UINT i=0;i<4;++i) {
		material.samplers[i]={NativeD3D12FilterMode::Linear,NativeD3D12FilterMode::Linear,
			NativeD3D12FilterMode::Point,false,false,1};
		material.coordinates[i].offset=baseUV;
		material.stages[i].alphaOp=NativeMaterialOp::Select1;
		material.stages[i].alphaArg1=UINT(NativeMaterialSource::Current);
	}
	material.textures[0]=base;
	auto& water=material.stages[0];
	water.colorOp=water.alphaOp=NativeMaterialOp::Modulate;
	water.colorArg1=water.alphaArg1=UINT(NativeMaterialSource::Texture);
	water.colorArg2=water.alphaArg2=UINT(NativeMaterialSource::Diffuse);
	UINT next=1;
	if (edge) {
		material.textures[next]=edge;
		material.coordinates[next].offset=edgeUV;
		auto& stage=material.stages[next++];
		stage.colorOp=NativeMaterialOp::Add;
		stage.alphaOp=NativeMaterialOp::Modulate;
		stage.colorArg1=stage.alphaArg1=UINT(NativeMaterialSource::Texture);
		stage.colorArg2=stage.alphaArg2=UINT(NativeMaterialSource::Current);
	}
	if (sparkle && noise) {
		material.textures[next]=sparkle;
		auto& highlight=material.stages[next++];
		highlight.colorOp=NativeMaterialOp::Select1;
		highlight.colorArg1=UINT(NativeMaterialSource::Texture);
		highlight.resultFlags[0]=1;
		material.textures[next]=noise;
		material.coordinates[next]=noiseUV;
		auto& modulate=material.stages[next];
		modulate.colorOp=NativeMaterialOp::MultiplyAdd;
		modulate.colorArg0=UINT(NativeMaterialSource::Current);
		modulate.colorArg1=UINT(NativeMaterialSource::Temporary);
		modulate.colorArg2=UINT(NativeMaterialSource::Texture);
	}
	return material;
}
