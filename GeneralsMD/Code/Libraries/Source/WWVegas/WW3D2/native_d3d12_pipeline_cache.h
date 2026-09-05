#pragma once

#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <vector>
#include <memory>
#include <map>
#include <cstdint>
#include <cstddef>

#include <d3dcompiler.h>

// Only PSO-affecting values cross this boundary; no mutable engine draw state,
// texture ownership, command list, or frame fences are visible to the cache.
struct NativeD3D12PipelineSettings {
	D3D12_FILL_MODE fillMode = D3D12_FILL_MODE_SOLID;
	D3D12_CULL_MODE cullMode = D3D12_CULL_MODE_NONE;
	INT depthBias = 0;
	bool blendEnable = false;
	D3D12_BLEND sourceBlend = D3D12_BLEND_ONE;
	D3D12_BLEND destinationBlend = D3D12_BLEND_ZERO;
	D3D12_BLEND_OP blendOp = D3D12_BLEND_OP_ADD;
	UINT8 renderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	bool useDefaultDepth = true;
	bool depthEnable = true;
	bool depthWrite = true;
	D3D12_COMPARISON_FUNC depthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	bool stencilEnable = false;
	UINT8 stencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
	UINT8 stencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
	D3D12_COMPARISON_FUNC stencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	D3D12_STENCIL_OP stencilFail = D3D12_STENCIL_OP_KEEP;
	D3D12_STENCIL_OP stencilDepthFail = D3D12_STENCIL_OP_KEEP;
	D3D12_STENCIL_OP stencilPass = D3D12_STENCIL_OP_KEEP;
	DXGI_FORMAT targetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
};

class NativeD3D12PipelineCache final {
public:
	NativeD3D12PipelineCache() = default;
	NativeD3D12PipelineCache(const NativeD3D12PipelineCache&) = delete;
	NativeD3D12PipelineCache& operator=(const NativeD3D12PipelineCache&) = delete;
	bool CreateBasic(ID3D12Device* device, const NativeD3D12PipelineSettings& settings,
		UINT colorOffset, UINT normalOffset = UINT_MAX, UINT specularOffset = UINT_MAX);
	bool CreateTextured(ID3D12Device* device, const NativeD3D12PipelineSettings& settings,
		const std::array<UINT,4>& texcoordOffsets, UINT colorOffset,
		UINT normalOffset, UINT specularOffset, UINT materialVariant, UINT treeSwayOffset);
	ID3D12RootSignature* RootSignature() const { return m_rootSignature.Get(); }
	ID3D12PipelineState* Basic() const { return m_basicPipeline.Get(); }
	ID3D12PipelineState* Textured() const { return m_texturedPipeline.Get(); }
	size_t Size() const { return m_pipelineCache.size(); }
	// Caller must retire GPU work before discarding cached objects/device identity.
	void Reset();
private:
	Microsoft::WRL::ComPtr<ID3D12Device> m_device;
	using PipelineKey = std::array<UINT,30>;
	static PipelineKey GetPipelineKey(const NativeD3D12PipelineSettings& settings, bool textured);
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_basicPipeline, m_texturedPipeline;
	std::map<PipelineKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>> m_pipelineCache;
	std::array<Microsoft::WRL::ComPtr<ID3DBlob>,4> m_shaderCache;
	std::array<Microsoft::WRL::ComPtr<ID3DBlob>,6> m_materialPixelShaders;
};
