#pragma once

#include "native_d3d12_resources.h"
#include "native_d3d12_lighting.h"

enum class NativeMaterialOp : UINT { Disable, Select1, Select2, Modulate, Modulate2X, Modulate4X, Add, AddSigned, AddSigned2X, Subtract, AddSmooth, BlendDiffuseAlpha, BlendTextureAlpha, BlendFactorAlpha, BlendCurrentAlpha, BlendTextureAlphaPremultiplied, ModulateAlphaAddColor, ModulateColorAddAlpha, ModulateInvAlphaAddColor, ModulateInvColorAddAlpha, Dot3, MultiplyAdd, Lerp, BumpEnvironment, BumpEnvironmentLuminance };
enum class NativeMaterialSource : UINT { Diffuse, Current, Texture, Factor, Specular, Temporary };
struct NativeMaterialStage {
	NativeMaterialOp colorOp = NativeMaterialOp::Disable;
	UINT colorArg1 = UINT(NativeMaterialSource::Texture), colorArg2 = UINT(NativeMaterialSource::Current);
	UINT colorArg0 = UINT(NativeMaterialSource::Current);
	NativeMaterialOp alphaOp = NativeMaterialOp::Select1;
	UINT alphaArg1 = UINT(NativeMaterialSource::Current), alphaArg2 = UINT(NativeMaterialSource::Texture);
	UINT alphaArg0 = UINT(NativeMaterialSource::Current);
	std::array<UINT,4> resultFlags = {}; // x: write the temporary register instead of current.
	std::array<float,4> bumpMatrix = {1,0,0,1}; // m00, m01, m10, m11.
	// Luminance scale/offset; signed UV sample decode scale/offset.
	std::array<float,4> bumpParameters = {1,0,1,0};
};
enum class NativeEnvironmentCoordinates : UINT { None, CameraNormal, CameraReflection };
struct NativeMaterialCoordinates {
	UINT offset = UINT_MAX;
	bool position = false, transform = false, projected = false;
	NativeEnvironmentCoordinates environment = NativeEnvironmentCoordinates::None;
	std::array<float, 16> matrix = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
};
// Complete native draw settings for a scoped pass. Texture references stay alive
// while captured. Targets, command-list lifetime and resource transitions remain
// explicit; a snapshot must be restored within the same renderer/device lifetime.
struct NativeD3D12State
{
	D3D12_CULL_MODE cullMode = D3D12_CULL_MODE_NONE;
	bool depthEnable = true, depthWrite = true;
	D3D12_COMPARISON_FUNC depthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	INT depthBias = 0;
	D3D12_FILL_MODE fillMode = D3D12_FILL_MODE_SOLID;
	bool blendEnable = false;
	D3D12_BLEND sourceBlend = D3D12_BLEND_ONE, destinationBlend = D3D12_BLEND_ZERO;
	D3D12_BLEND_OP blendOp = D3D12_BLEND_OP_ADD;
	UINT8 renderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	bool alphaTestEnable = false;
	D3D12_COMPARISON_FUNC alphaTestFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	UINT8 alphaTestRef = 0;
	bool grayscale = false;
	UINT32 grayscaleTint = 0xffffffff;
	float grayscaleAmount = 1.0f;
	bool materialEnabled = false;
	std::array<std::array<float,4>,11> treeSway = {};
	UINT treeSwayOffset = UINT_MAX;
	UINT fogMode = 0;
	bool fogRange = false;
	std::array<float,4> fogParameters = {}, fogColor = {};
	std::array<float,16> worldView = {}, worldViewProjection = {};
	NativeLightingState lighting;
	std::array<NativeMaterialStage,4> materialStages;
	std::array<NativeMaterialCoordinates,4> materialCoordinates;
	std::array<std::shared_ptr<NativeD3D12Texture>,4> materialTextures;
	std::array<D3D12_GPU_DESCRIPTOR_HANDLE,4> materialSamplers = {};
	std::array<float,4> materialFactor = {1,1,1,1};
	bool textureColorTexture = true, textureColorVertex = true;
	bool textureAlphaTexture = true, textureAlphaVertex = true;
	bool stencilEnable = false;
	D3D12_COMPARISON_FUNC stencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	UINT8 stencilRef = 0, stencilReadMask = 0xff, stencilWriteMask = 0xff;
	D3D12_STENCIL_OP stencilFail = D3D12_STENCIL_OP_KEEP;
	D3D12_STENCIL_OP stencilDepthFail = D3D12_STENCIL_OP_KEEP;
	D3D12_STENCIL_OP stencilPass = D3D12_STENCIL_OP_KEEP;
	D3D12_GPU_DESCRIPTOR_HANDLE currentSamplerGpu = {};
	D3D12_VIEWPORT viewport = {};
	D3D12_RECT scissor = {};
};
