#pragma once
#include "WW3D2/native_draw_state.h"
#include "WW3D2/texture.h"
#include "W3DDevice/GameClient/W3DShroud.h"

// World-space effects author bindings and projection without a state replay.
inline NativeMaterialDescription NativeWorldModulatedMaterial(TextureClass* texture,
	UINT uvOffset, W3DShroud* shroud, bool modulateShroudAlpha = false)
{
	NativeMaterialDescription material;
	material.enabled = true;
	auto& base = material.stages[0];
	base.colorOp = base.alphaOp = NativeMaterialOp::Modulate;
	base.colorArg1 = base.alphaArg1 = UINT(NativeMaterialSource::Texture);
	base.colorArg2 = base.alphaArg2 = UINT(NativeMaterialSource::Diffuse);
	material.coordinates[0].offset = uvOffset;
	if (texture) {
		material.textures[0] = texture->Prepare_Native_Texture();
		material.samplers[0] = texture->Get_Filter().Get_Native_Description();
	}
	TextureClass* mask = shroud ? shroud->getShroudTexture() : NULL;
	if (mask && shroud->getCellWidth() > 0 && shroud->getCellHeight() > 0 &&
		shroud->getTextureWidth() > 0 && shroud->getTextureHeight() > 0) {
		material.textures[1] = mask->Prepare_Native_Texture();
		if (material.textures[1]) {
			auto& stage = material.stages[1];
			stage.colorOp = NativeMaterialOp::Modulate;
			stage.alphaOp = modulateShroudAlpha ? NativeMaterialOp::Modulate : NativeMaterialOp::Select2;
			stage.colorArg1 = stage.alphaArg1 = UINT(NativeMaterialSource::Texture);
			stage.colorArg2 = stage.alphaArg2 = UINT(NativeMaterialSource::Current);
			auto& uv = material.coordinates[1];
			uv.position = uv.transform = true;
			uv.offset = UINT_MAX;
			const float sx = 1.0f / (shroud->getCellWidth() * shroud->getTextureWidth());
			const float sy = 1.0f / (shroud->getCellHeight() * shroud->getTextureHeight());
			uv.matrix = {sx,0,0,0, 0,sy,0,0, 0,0,1,0,
				(-float(shroud->getDrawOriginX()) + shroud->getCellWidth()) * sx,
				(-float(shroud->getDrawOriginY()) + shroud->getCellHeight()) * sy,0,1};
			material.samplers[1] = {NativeD3D12FilterMode::Linear,NativeD3D12FilterMode::Linear,
				NativeD3D12FilterMode::Point,true,true,1};
		}
	}
	return material;
}
