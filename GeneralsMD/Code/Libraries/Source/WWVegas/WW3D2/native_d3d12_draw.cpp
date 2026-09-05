#include "native_d3d12_renderer.h"
#include "native_d3d12_diagnostics.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstdio>

using Microsoft::WRL::ComPtr;
using NativeD3D12Internal::CpuTimer;
using NativeD3D12Internal::DiagnosticLog;

// Draw recording and immutable state snapshots. Submission/fences stay in renderer.cpp.
namespace {
	void DecodePackedColor(const unsigned char* source, float color[4])
	{
		const UINT packed = static_cast<UINT>(source[0]) |
			(static_cast<UINT>(source[1]) << 8) |
			(static_cast<UINT>(source[2]) << 16) |
			(static_cast<UINT>(source[3]) << 24);
		color[0] = static_cast<float>((packed >> 16) & 0xff) / 255.0f;
		color[1] = static_cast<float>((packed >> 8) & 0xff) / 255.0f;
		color[2] = static_cast<float>(packed & 0xff) / 255.0f;
		color[3] = static_cast<float>((packed >> 24) & 0xff) / 255.0f;
	}

	bool DrawVertexRange(const unsigned short* indices, UINT count, UINT vertexCount,
		UINT& first, UINT& last)
	{
		first = UINT_MAX; last = 0;
		for (UINT i=0;i<count;++i) {
			const UINT index = indices[i];
			if (index >= vertexCount) return false;
			first = (std::min)(first,index); last = (std::max)(last,index);
		}
		return true;
	}

	// Constants use HLSL register packing; vertex data stays in its source layout.
	struct NativeVertexConstants {
		std::array<float,16> worldViewProjection;
		std::array<UINT,4> vertexFlags = {};
		std::array<std::array<float,16>,4> textureMatrices = {};
		std::array<UINT,4> textureFlags = {};
		std::array<std::array<float,4>,11> treeSway = {};
		std::array<float,16> worldView = {};
		std::array<float,4> fogParameters = {}, fogColor = {};
		NativeLightingState lighting;
	};
	static_assert(sizeof(NativeVertexConstants) == 1264, "HLSL vertex constant packing");

}

NativeD3D12State NativeD3D12Renderer::CaptureState() const
{
	NativeD3D12State state;
	state.cullMode = m_cullMode;
	state.depthEnable = m_depthEnable;
	state.depthWrite = m_depthWrite;
	state.depthFunc = m_depthFunc;
	state.depthBias = m_depthBias;
	state.fillMode = m_fillMode;
	state.blendEnable = m_blendEnable;
	state.sourceBlend = m_sourceBlend;
	state.destinationBlend = m_destinationBlend;
	state.blendOp = m_blendOp;
	state.renderTargetWriteMask = m_renderTargetWriteMask;
	state.alphaTestEnable = m_alphaTestEnable;
	state.alphaTestFunc = m_alphaTestFunc;
	state.alphaTestRef = m_alphaTestRef;
	state.grayscale = m_grayscale;
	state.grayscaleTint = m_grayscaleTint;
	state.grayscaleAmount = m_grayscaleAmount;
	state.materialEnabled = m_materialEnabled;
	state.treeSway = m_treeSway;
	state.treeSwayOffset = m_treeSwayOffset;
	state.fogMode = m_fogMode;
	state.fogRange = m_fogRange;
	state.fogParameters = m_fogParameters;
	state.fogColor = m_fogColor;
	state.worldView = m_worldView;
	state.worldViewProjection = m_worldViewProjection;
	state.lighting = m_lighting;
	state.materialStages = m_materialStages;
	state.materialCoordinates = m_materialCoordinates;
	state.materialTextures = m_materialTextures;
	state.materialSamplers = m_materialSamplers;
	state.materialFactor = m_materialFactor;
	state.textureColorTexture = m_textureColorTexture;
	state.textureColorVertex = m_textureColorVertex;
	state.textureAlphaTexture = m_textureAlphaTexture;
	state.textureAlphaVertex = m_textureAlphaVertex;
	state.stencilEnable = m_stencilEnable;
	state.stencilFunc = m_stencilFunc;
	state.stencilRef = m_stencilRef;
	state.stencilReadMask = m_stencilReadMask;
	state.stencilWriteMask = m_stencilWriteMask;
	state.stencilFail = m_stencilFail;
	state.stencilDepthFail = m_stencilDepthFail;
	state.stencilPass = m_stencilPass;
	state.currentSamplerGpu = m_currentSamplerGpu;
	state.viewport = m_viewport;
	state.scissor = m_scissor;
	return state;
}

void NativeD3D12Renderer::RestoreState(const NativeD3D12State& state)
{
	m_cullMode = state.cullMode;
	m_depthEnable = state.depthEnable;
	m_depthWrite = state.depthWrite;
	m_depthFunc = state.depthFunc;
	m_depthBias = state.depthBias;
	m_fillMode = state.fillMode;
	m_blendEnable = state.blendEnable;
	m_sourceBlend = state.sourceBlend;
	m_destinationBlend = state.destinationBlend;
	m_blendOp = state.blendOp;
	m_renderTargetWriteMask = state.renderTargetWriteMask;
	m_alphaTestEnable = state.alphaTestEnable;
	m_alphaTestFunc = state.alphaTestFunc;
	m_alphaTestRef = state.alphaTestRef;
	m_grayscale = state.grayscale;
	m_grayscaleTint = state.grayscaleTint;
	m_grayscaleAmount = state.grayscaleAmount;
	m_materialEnabled = state.materialEnabled;
	m_treeSway = state.treeSway;
	m_treeSwayOffset = state.treeSwayOffset;
	m_fogMode = state.fogMode;
	m_fogRange = state.fogRange;
	m_fogParameters = state.fogParameters;
	m_fogColor = state.fogColor;
	m_worldView = state.worldView;
	m_worldViewProjection = state.worldViewProjection;
	m_lighting = state.lighting;
	m_materialStages = state.materialStages;
	m_materialCoordinates = state.materialCoordinates;
	m_materialTextures = state.materialTextures;
	m_materialSamplers = state.materialSamplers;
	m_materialFactor = state.materialFactor;
	m_textureColorTexture = state.textureColorTexture;
	m_textureColorVertex = state.textureColorVertex;
	m_textureAlphaTexture = state.textureAlphaTexture;
	m_textureAlphaVertex = state.textureAlphaVertex;
	m_stencilEnable = state.stencilEnable;
	m_stencilFunc = state.stencilFunc;
	m_stencilRef = state.stencilRef;
	m_stencilReadMask = state.stencilReadMask;
	m_stencilWriteMask = state.stencilWriteMask;
	m_stencilFail = state.stencilFail;
	m_stencilDepthFail = state.stencilDepthFail;
	m_stencilPass = state.stencilPass;
	m_currentSamplerGpu = state.currentSamplerGpu;
	m_viewport = state.viewport;
	m_scissor = state.scissor;
	// Pipeline/root bindings are selected by the next draw. Viewport/scissor
	// are dynamic command-list state and must be restored immediately.
	if (m_recording && m_commandList) {
		m_commandList->RSSetViewports(1, &m_viewport);
		m_commandList->RSSetScissorRects(1, &m_scissor);
	}
}


void NativeD3D12Renderer::SetFixedFunctionState(D3D12_CULL_MODE cullMode,
	bool depthEnable, bool depthWrite, D3D12_COMPARISON_FUNC depthFunc,
	bool blendEnable, D3D12_BLEND sourceBlend, D3D12_BLEND destinationBlend,
	D3D12_BLEND_OP blendOp, UINT8 renderTargetWriteMask)
{
	if (m_cullMode == cullMode && m_depthEnable == depthEnable &&
		m_depthWrite == depthWrite && m_depthFunc == depthFunc &&
		m_blendEnable == blendEnable && m_sourceBlend == sourceBlend &&
		m_destinationBlend == destinationBlend && m_blendOp == blendOp &&
		m_renderTargetWriteMask == renderTargetWriteMask)
		return;
	m_cullMode = cullMode;
	m_depthEnable = depthEnable;
	m_depthWrite = depthWrite;
	m_depthFunc = depthFunc;
	m_blendEnable = blendEnable;
	m_sourceBlend = sourceBlend;
	m_destinationBlend = destinationBlend;
	m_blendOp = blendOp;
	m_renderTargetWriteMask = renderTargetWriteMask;
}

void NativeD3D12Renderer::SetAlphaTestState(bool enable, D3D12_COMPARISON_FUNC function,
	UINT8 reference)
{
	m_alphaTestEnable = enable;
	m_alphaTestFunc = function;
	m_alphaTestRef = reference;
}

void NativeD3D12Renderer::SetGrayscale(bool enable, UINT32 tint, float amount)
{
	m_grayscale = enable;
	m_grayscaleTint = tint;
	m_grayscaleAmount = amount;
}

void NativeD3D12Renderer::SetTreeSway(const float (*offsets)[4], UINT count, UINT vertexOffset)
{
	m_treeSwayOffset = UINT_MAX;
	if (!offsets || count == 0 || count > m_treeSway.size()) return;
	m_treeSway = {};
	for (UINT i=0;i<count;++i) std::copy_n(offsets[i],4,m_treeSway[i].begin());
	m_treeSwayOffset = vertexOffset;
}

void NativeD3D12Renderer::SetVertexFog(UINT mode, float start, float end, float density,
	UINT32 color, bool range)
{
	m_fogMode = mode <= 3 ? mode : 0;
	m_fogRange = range;
	m_fogParameters = {start,end,(std::max)(0.0f,density),0};
	DecodePackedColor(reinterpret_cast<const unsigned char*>(&color),m_fogColor.data());
}

void NativeD3D12Renderer::SetWorldView(const float* matrix16)
{
	if (matrix16) std::copy_n(matrix16,16,m_worldView.begin());
	SetLighting(m_lighting); // Environment mapping also needs the current normal transform.
}

void NativeD3D12Renderer::SetLighting(const NativeLightingState& state)
{
	m_lighting = state;
	// Cofactors / determinant give the inverse transpose for row-vector normals.
	// Translation is excluded; singular transforms must not inject NaNs.
	const auto& a = m_worldView;
	auto& n = m_lighting.normalTransform;
	n = {};
	n[0]=a[5]*a[10]-a[6]*a[9]; n[1]=a[6]*a[8]-a[4]*a[10]; n[2]=a[4]*a[9]-a[5]*a[8];
	n[4]=a[2]*a[9]-a[1]*a[10]; n[5]=a[0]*a[10]-a[2]*a[8]; n[6]=a[1]*a[8]-a[0]*a[9];
	n[8]=a[1]*a[6]-a[2]*a[5]; n[9]=a[2]*a[4]-a[0]*a[6]; n[10]=a[0]*a[5]-a[1]*a[4];
	const float determinant = a[0]*n[0]+a[1]*n[1]+a[2]*n[2];
	if (std::abs(determinant) > 1e-20f) for (float& value : n) value /= determinant;
	else n = {};
}

void NativeD3D12Renderer::ReportMissingTexture(const char* filename, bool ddsAvailable)
{
	char message[1024];
	std::snprintf(message,sizeof(message),"NativeD3D12: missing texture '%s' (DDS available=%u; DDS/TGA load exhausted)\n",
		filename ? filename : "<null>",ddsAvailable ? 1u : 0u);
	DiagnosticLog(message);
}

void NativeD3D12Renderer::SetTextureCombine(bool textureColor, bool vertexColor,
	bool textureAlpha, bool vertexAlpha)
{
	m_textureColorTexture = textureColor;
	m_textureColorVertex = vertexColor;
	m_textureAlphaTexture = textureAlpha;
	m_textureAlphaVertex = vertexAlpha;
}

void NativeD3D12Renderer::SetStencilState(bool enable, D3D12_COMPARISON_FUNC function,
	UINT8 reference, UINT8 readMask, UINT8 writeMask, D3D12_STENCIL_OP failOperation,
	D3D12_STENCIL_OP depthFailOperation, D3D12_STENCIL_OP passOperation)
{
	if (m_stencilEnable == enable && m_stencilFunc == function && m_stencilRef == reference &&
		m_stencilReadMask == readMask && m_stencilWriteMask == writeMask &&
		m_stencilFail == failOperation && m_stencilDepthFail == depthFailOperation &&
		m_stencilPass == passOperation)
		return;
	m_stencilEnable = enable;
	m_stencilFunc = function;
	m_stencilRef = reference;
	m_stencilReadMask = readMask;
	m_stencilWriteMask = writeMask;
	m_stencilFail = failOperation;
	m_stencilDepthFail = depthFailOperation;
	m_stencilPass = passOperation;
}

void NativeD3D12Renderer::BindTexture(UINT rootParameter,
	const NativeD3D12Texture* texture)
{
	if (!IsInitialized() || !m_recording || !RetainTexture(texture))
		return;
	ID3D12DescriptorHeap* heaps[] = {m_srvHeap.Get(), m_samplerHeap.Get()};
	m_commandList->SetDescriptorHeaps(2, heaps);
	m_commandList->SetGraphicsRootDescriptorTable(rootParameter, texture->SrvGpuHandle());
}

void NativeD3D12Renderer::SetSamplerState(NativeD3D12FilterMode minFilter,
	NativeD3D12FilterMode magFilter, NativeD3D12FilterMode mipFilter, bool clampU,
	bool clampV, UINT maxAnisotropy)
{
	if (!IsInitialized() || m_samplerHeap == nullptr)
		return;
	UINT filterBits = 0;
	if (minFilter == NativeD3D12FilterMode::Anisotropic ||
		magFilter == NativeD3D12FilterMode::Anisotropic ||
		mipFilter == NativeD3D12FilterMode::Anisotropic)
	{
		filterBits = 8;
	}
	else
	{
		// SamplerFilter encodes the D3D12 filter by the components that are
		// linear: bit 2 = minification, bit 1 = magnification, bit 0 = mip.
		if (minFilter != NativeD3D12FilterMode::Point) filterBits |= 4;
		if (magFilter != NativeD3D12FilterMode::Point) filterBits |= 2;
		if (mipFilter != NativeD3D12FilterMode::Point) filterBits |= 1;
	}
	const UINT descriptorIndex = filterBits * 4 + (clampU ? 1u : 0u) + (clampV ? 2u : 0u);
	m_currentSamplerGpu = m_samplerHeap->GetGPUDescriptorHandleForHeapStart();
	m_currentSamplerGpu.ptr += static_cast<UINT64>(descriptorIndex) * m_samplerDescriptorSize;
	(void)maxAnisotropy;
}

D3D12_RESOURCE_BARRIER NativeD3D12Renderer::Transition(ID3D12Resource* resource,
	D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = resource;
	barrier.Transition.StateBefore = before;
	barrier.Transition.StateAfter = after;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	return barrier;
}

D3D12_CPU_DESCRIPTOR_HANDLE NativeD3D12Renderer::CurrentRenderTarget() const
{
	D3D12_CPU_DESCRIPTOR_HANDLE handle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
	handle.ptr += static_cast<SIZE_T>(m_frameIndex) * m_rtvDescriptorSize;
	return handle;
}

bool NativeD3D12Renderer::DrawIndexed(const void* vertices, UINT vertexBytes, UINT vertexStride,
	UINT vertexCount, const unsigned short* indices, UINT indexCount, UINT startIndex,
	UINT baseVertex, D3D12_PRIMITIVE_TOPOLOGY topology, UINT colorOffset,
	const NativeD3D12UploadBuffer* vertexOwner, const NativeD3D12UploadBuffer* indexOwner,
	UINT normalOffset, UINT specularOffset)
{
	CpuTimer timer(m_profiling ? &m_cpuMilliseconds[0] : nullptr);
	if (!IsInitialized() || !m_recording || vertices == nullptr || indices == nullptr || vertexBytes == 0 ||
		vertexStride < sizeof(float) * 3 || vertexCount == 0 || indexCount == 0)
		return false;
	if (colorOffset != UINT_MAX && (colorOffset > vertexStride || sizeof(DWORD) > vertexStride - colorOffset))
		colorOffset = UINT_MAX;
	if (normalOffset != UINT_MAX && (normalOffset > vertexStride || 12 > vertexStride-normalOffset)) return false;
	if (specularOffset != UINT_MAX && (specularOffset > vertexStride || 4 > vertexStride-specularOffset)) return false;
	if (startIndex != 0 || baseVertex >= vertexCount ||
		indexCount > UINT_MAX / sizeof(unsigned short)) return false;
	if (vertexCount > vertexBytes / vertexStride)
		return false;
	UINT firstVertex, lastVertex;
	if (!ResolveIndexRange(indices,indexCount,vertexCount-baseVertex,firstVertex,lastVertex,indexOwner)) return false;
	const UINT usedVertices = lastVertex-firstVertex+1;
	m_profileVertices += usedVertices;
	if (!CreateBasicPipeline(colorOffset, normalOffset, specularOffset))
		return false;

	NativeVertexConstants constants = {};
	constants.worldViewProjection = m_worldViewProjection;
	constants.vertexFlags[0] = colorOffset != UINT_MAX;
	const auto* source = static_cast<const unsigned char*>(vertices) +
		static_cast<size_t>(firstVertex+baseVertex)*vertexStride;
	constants.vertexFlags[2] = m_fogMode;
	constants.vertexFlags[3] = m_fogRange;
	constants.worldView = m_worldView;
	constants.fogParameters = m_fogParameters;
	constants.fogColor = m_fogColor;
	constants.lighting = m_lighting;
	constants.lighting.flags[0] &= normalOffset != UINT_MAX;
	constants.lighting.flags[1] &= normalOffset != UINT_MAX || specularOffset != UINT_MAX;
	constants.lighting.parameters[1] = specularOffset != UINT_MAX;
	for (UINT& source : constants.lighting.sources)
		if ((source == 1 && colorOffset == UINT_MAX) || (source == 2 && specularOffset == UINT_MAX)) source = 0;
	const UINT usedBytes = usedVertices*vertexStride;
	D3D12_GPU_VIRTUAL_ADDRESS vertexAddress, indexAddress, transformAddress;
	if (!ResolveGeometry(source,usedBytes,vertexAddress,vertexOwner) ||
		!ResolveGeometry(indices,indexCount*sizeof(unsigned short),indexAddress,indexOwner) ||
		!UploadGeometry(&constants,sizeof(constants),transformAddress)) return false;

	D3D12_VERTEX_BUFFER_VIEW vertexView = {};
	vertexView.BufferLocation = vertexAddress;
	vertexView.SizeInBytes = usedBytes;
	vertexView.StrideInBytes = vertexStride;
	D3D12_INDEX_BUFFER_VIEW indexView = {};
	indexView.BufferLocation = indexAddress;
	indexView.SizeInBytes = indexCount * sizeof(unsigned short);
	indexView.Format = DXGI_FORMAT_R16_UINT;

	m_commandList->SetGraphicsRootSignature(m_pipelines.RootSignature());
	m_commandList->SetGraphicsRootConstantBufferView(0, transformAddress);
	UINT alphaTestConstants[11] = {
		m_alphaTestEnable ? 1u : 0u, static_cast<UINT>(m_alphaTestFunc), m_alphaTestRef, 0u,
		m_grayscale ? 1u : 0u, m_textureColorTexture ? 1u : 0u,
		m_textureColorVertex ? 1u : 0u, m_textureAlphaTexture ? 1u : 0u,
		m_textureAlphaVertex ? 1u : 0u, m_grayscaleTint, 0};
	std::memcpy(&alphaTestConstants[10], &m_grayscaleAmount, sizeof(float));
	m_commandList->SetGraphicsRoot32BitConstants(2, 11, alphaTestConstants, 0);
	m_commandList->SetPipelineState(m_pipelines.Basic());
	m_commandList->IASetPrimitiveTopology(topology);
	m_commandList->IASetVertexBuffers(0, 1, &vertexView);
	m_commandList->IASetIndexBuffer(&indexView);
	m_commandList->OMSetStencilRef(m_stencilRef);
	m_commandList->DrawIndexedInstanced(indexCount, 1, 0, -static_cast<INT>(firstVertex), 0);
	++m_recordedDraws;
	return true;
}

bool NativeD3D12Renderer::DrawIndexedTextured(const void* vertices, UINT vertexBytes,
	UINT vertexStride, UINT vertexCount, UINT texcoordOffset, const unsigned short* indices,
	UINT indexCount, D3D12_PRIMITIVE_TOPOLOGY topology, const NativeD3D12Texture* texture,
	UINT colorOffset, const NativeD3D12UploadBuffer* vertexOwner,
	const NativeD3D12UploadBuffer* indexOwner, UINT normalOffset, UINT specularOffset)
{
	CpuTimer timer(m_profiling ? &m_cpuMilliseconds[0] : nullptr);
	if (!IsInitialized() || !m_recording || vertices == nullptr || indices == nullptr ||
		(texture ? !texture->IsValid() : !m_materialEnabled) || vertexBytes == 0 || vertexStride < sizeof(float) * 3 || vertexCount == 0 ||
		indexCount == 0)
		return false;
	if (!m_materialEnabled && (texcoordOffset > vertexStride || sizeof(float) * 2 > vertexStride - texcoordOffset))
		return false;
	if (colorOffset != UINT_MAX && (colorOffset > vertexStride || sizeof(DWORD) > vertexStride - colorOffset))
		colorOffset = UINT_MAX;
	if (normalOffset != UINT_MAX && (normalOffset > vertexStride || 12 > vertexStride-normalOffset)) return false;
	if (specularOffset != UINT_MAX && (specularOffset > vertexStride || 4 > vertexStride-specularOffset)) return false;
	if (indexCount > UINT_MAX / sizeof(unsigned short)) return false;
	if (vertexCount > vertexBytes / vertexStride)
		return false;
	UINT firstVertex, lastVertex;
	if (!ResolveIndexRange(indices,indexCount,vertexCount,firstVertex,lastVertex,indexOwner)) return false;
	const UINT usedVertices = lastVertex-firstVertex+1;
	m_profileVertices += usedVertices;
	NativeVertexConstants constants = {};
	constants.worldViewProjection = m_worldViewProjection;
	constants.vertexFlags[0] = colorOffset != UINT_MAX;
	std::array<UINT,4> offsets = {};
	constants.vertexFlags[2] = m_fogMode;
	constants.vertexFlags[3] = m_fogRange;
	constants.worldView = m_worldView;
	constants.fogParameters = m_fogParameters;
	constants.fogColor = m_fogColor;
	constants.lighting = m_lighting;
	constants.lighting.flags[0] &= normalOffset != UINT_MAX;
	constants.lighting.flags[1] &= normalOffset != UINT_MAX || specularOffset != UINT_MAX;
	constants.lighting.parameters[1] = specularOffset != UINT_MAX;
	for (UINT& source : constants.lighting.sources)
		if ((source == 1 && colorOffset == UINT_MAX) || (source == 2 && specularOffset == UINT_MAX)) source = 0;
	if (m_treeSwayOffset != UINT_MAX) {
		if (m_treeSwayOffset > vertexStride || sizeof(float)*3 > vertexStride-m_treeSwayOffset) return false;
		constants.vertexFlags[1] = 1;
		constants.treeSway = m_treeSway;
	}
	for (UINT stage=0;stage<4;++stage) {
		const auto& uv = m_materialCoordinates[stage];
		const UINT offset = m_materialEnabled ? uv.offset : texcoordOffset;
		const bool validUV = offset <= vertexStride && sizeof(float)*2 <= vertexStride-offset;
		offsets[stage] = validUV ? offset : 0;
		constants.textureMatrices[stage] = uv.matrix;
		constants.textureFlags[stage] = (m_materialEnabled && uv.position ? 1u : 0u) |
			(m_materialEnabled && uv.transform ? 2u : 0u) |
			(m_materialEnabled && uv.projected ? 4u : 0u) | (validUV ? 8u : 0u) |
			(m_materialEnabled && uv.environment == NativeEnvironmentCoordinates::CameraNormal ? 16u : 0u) |
			(m_materialEnabled && uv.environment == NativeEnvironmentCoordinates::CameraReflection ? 32u : 0u) |
			(normalOffset != UINT_MAX ? 64u : 0u);
	}
	// A material can contain only diffuse/factor operations. Supply a stable
	// neutral SRV so it still runs through the material shader, without a CPU
	// recoloring pass or a texture allocation/upload on every draw.
	if (!texture) {
		if (!m_neutralMaterialTexture) {
			std::unique_ptr<NativeD3D12Texture> neutral(CreateTexture2D(1,1,1,DXGI_FORMAT_R8G8B8A8_UNORM));
			const UINT32 white=0xffffffff;
			const NativeD3D12TextureLevel level={&white,4,4};
			if (!neutral || !UploadTexture2D(*neutral,&level,1)) return false;
			m_neutralMaterialTexture=std::move(neutral);
		}
		texture=m_neutralMaterialTexture.get();
	}
	if (!RetainTexture(texture) || !CreateTexturedPipeline(offsets, colorOffset, normalOffset, specularOffset))
		return false;

	if (m_currentRenderTarget && m_currentRenderTarget->m_storage == texture->m_storage) return false;
	if (texture->m_storage->m_state != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
		const auto barrier = Transition(texture->Resource(), texture->m_storage->m_state,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		m_commandList->ResourceBarrier(1, &barrier);
		texture->m_storage->m_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	}
	const auto* source = static_cast<const unsigned char*>(vertices) +
		static_cast<size_t>(firstVertex)*vertexStride;
	const UINT usedBytes = usedVertices*vertexStride;
	D3D12_GPU_VIRTUAL_ADDRESS vertexAddress, indexAddress, transformAddress;
	if (!ResolveGeometry(source,usedBytes,vertexAddress,vertexOwner) ||
		!ResolveGeometry(indices,indexCount*sizeof(unsigned short),indexAddress,indexOwner) ||
		!UploadGeometry(&constants,sizeof(constants),transformAddress)) return false;

	D3D12_VERTEX_BUFFER_VIEW vertexView = {};
	vertexView.BufferLocation = vertexAddress;
	vertexView.SizeInBytes = usedBytes;
	vertexView.StrideInBytes = vertexStride;
	D3D12_INDEX_BUFFER_VIEW indexView = {};
	indexView.BufferLocation = indexAddress;
	indexView.SizeInBytes = indexCount * sizeof(unsigned short);
	indexView.Format = DXGI_FORMAT_R16_UINT;
	ID3D12DescriptorHeap* heaps[] = {m_srvHeap.Get(), m_samplerHeap.Get()};
	m_commandList->SetDescriptorHeaps(2, heaps);
	m_commandList->SetGraphicsRootSignature(m_pipelines.RootSignature());
	m_commandList->SetGraphicsRootConstantBufferView(0, transformAddress);
	UINT alphaTestConstants[11] = {
		m_alphaTestEnable ? 1u : 0u, static_cast<UINT>(m_alphaTestFunc), m_alphaTestRef, m_materialEnabled ? 1u : 0u,
		m_grayscale ? 1u : 0u, m_textureColorTexture ? 1u : 0u,
		m_textureColorVertex ? 1u : 0u, m_textureAlphaTexture ? 1u : 0u,
		m_textureAlphaVertex ? 1u : 0u, m_grayscaleTint, 0};
	std::memcpy(&alphaTestConstants[10], &m_grayscaleAmount, sizeof(float));
	m_commandList->SetGraphicsRoot32BitConstants(2, 11, alphaTestConstants, 0);
	struct MaterialConstants {
		std::array<NativeMaterialStage, 4> stages;
		std::array<float,4> factor;
	} material = {m_materialStages, m_materialFactor};
	static_assert(sizeof(NativeMaterialStage) == 80, "HLSL material packing");
	D3D12_GPU_VIRTUAL_ADDRESS materialAddress;
	if (!UploadGeometry(&material, sizeof(material), materialAddress)) return false;
	m_commandList->SetGraphicsRootConstantBufferView(10, materialAddress);
	for (UINT stage=0; stage<4; ++stage) {
		const NativeD3D12Texture* binding = m_materialEnabled && m_materialTextures[stage] ?
			m_materialTextures[stage].get() : texture;
		if (!RetainTexture(binding)) return false;
		if (m_currentRenderTarget && m_currentRenderTarget->m_storage == binding->m_storage) return false;
		if (binding->m_storage->m_state != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
			const auto barrier = Transition(binding->Resource(), binding->m_storage->m_state,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			m_commandList->ResourceBarrier(1, &barrier);
			binding->m_storage->m_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		}
		const UINT textureRoot = stage ? 4+(stage-1)*2 : 1;
		const UINT samplerRoot = stage ? textureRoot+1 : 3;
		m_commandList->SetGraphicsRootDescriptorTable(textureRoot, binding->SrvGpuHandle());
		m_commandList->SetGraphicsRootDescriptorTable(samplerRoot,
			m_materialEnabled && m_materialSamplers[stage].ptr ? m_materialSamplers[stage] : m_currentSamplerGpu);
	}
	m_commandList->SetPipelineState(m_pipelines.Textured());
	m_commandList->IASetPrimitiveTopology(topology);
	m_commandList->IASetVertexBuffers(0, 1, &vertexView);
	m_commandList->IASetIndexBuffer(&indexView);
	m_commandList->OMSetStencilRef(m_stencilRef);
	m_commandList->DrawIndexedInstanced(indexCount, 1, 0, -static_cast<INT>(firstVertex), 0);
	++m_recordedDraws;
	return true;
}

bool NativeD3D12Renderer::DrawScreenQuad(FLOAT x, FLOAT y, FLOAT width, FLOAT height,
	UINT32 color)
{
	if (!m_recording || width <= 0.0f || height <= 0.0f || m_viewport.Width <= 0.0f ||
		m_viewport.Height <= 0.0f)
		return false;
	const FLOAT left = ((x - m_viewport.TopLeftX) / m_viewport.Width) * 2.0f - 1.0f;
	const FLOAT right = ((x + width - m_viewport.TopLeftX) / m_viewport.Width) * 2.0f - 1.0f;
	const FLOAT top = 1.0f - ((y - m_viewport.TopLeftY) / m_viewport.Height) * 2.0f;
	const FLOAT bottom = 1.0f - ((y + height - m_viewport.TopLeftY) / m_viewport.Height) * 2.0f;
	struct ScreenVertex { float position[3]; DWORD color; };
	ScreenVertex vertices[4] = {};
	vertices[0].position[0] = right; vertices[0].position[1] = bottom;
	vertices[1].position[0] = right; vertices[1].position[1] = top;
	vertices[2].position[0] = left; vertices[2].position[1] = bottom;
	vertices[3].position[0] = left; vertices[3].position[1] = top;
	for (auto& vertex : vertices) vertex.color = color;
	const unsigned short indices[6] = {0, 1, 2, 2, 1, 3};
	const std::array<float, 16> savedTransform = m_worldViewProjection;
	m_worldViewProjection = {1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f};
	const UINT savedFog = m_fogMode;
	const auto savedFill = m_fillMode;
	m_fillMode = D3D12_FILL_MODE_SOLID;
	m_fogMode = 0;
	const bool result = DrawIndexed(vertices, sizeof(vertices), sizeof(ScreenVertex), 4,
		indices, 6, 0, 0, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, sizeof(float) * 3);
	m_fogMode = savedFog;
	m_fillMode = savedFill;
	m_worldViewProjection = savedTransform;
	return result;
}

bool NativeD3D12Renderer::DrawTexturedScreenQuad(FLOAT x, FLOAT y, FLOAT width,
	FLOAT height, FLOAT u0, FLOAT v0, FLOAT u1, FLOAT v1, UINT32 color,
	const NativeD3D12Texture* texture, bool useMaterial)
{
	if (!m_recording || texture == nullptr || !texture->IsValid() || width <= 0.0f ||
		height <= 0.0f || m_viewport.Width <= 0.0f || m_viewport.Height <= 0.0f)
		return false;
	const FLOAT left = ((x - m_viewport.TopLeftX) / m_viewport.Width) * 2.0f - 1.0f;
	const FLOAT right = ((x + width - m_viewport.TopLeftX) / m_viewport.Width) * 2.0f - 1.0f;
	const FLOAT top = 1.0f - ((y - m_viewport.TopLeftY) / m_viewport.Height) * 2.0f;
	const FLOAT bottom = 1.0f - ((y + height - m_viewport.TopLeftY) / m_viewport.Height) * 2.0f;
	struct ScreenVertex
	{
		float position[3];
		DWORD color;
		float texcoord[2];
	};
	ScreenVertex vertices[4] = {};
	vertices[0] = {{right, bottom, 0.0f}, color, {u1, v1}};
	vertices[1] = {{right, top, 0.0f}, color, {u1, v0}};
	vertices[2] = {{left, bottom, 0.0f}, color, {u0, v1}};
	vertices[3] = {{left, top, 0.0f}, color, {u0, v0}};
	const unsigned short indices[6] = {0, 1, 2, 2, 1, 3};
	const std::array<float, 16> savedTransform = m_worldViewProjection;
	m_worldViewProjection = {1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f};
	const bool savedMaterial = m_materialEnabled;
	const UINT savedFog = m_fogMode, savedSway = m_treeSwayOffset;
	const auto savedFill = m_fillMode;
	m_fillMode = D3D12_FILL_MODE_SOLID;
	m_fogMode = 0;
	m_treeSwayOffset = UINT_MAX;
	m_materialEnabled = useMaterial;
	const bool result = DrawIndexedTextured(vertices, sizeof(vertices), sizeof(ScreenVertex),
		4, sizeof(float) * 3 + sizeof(DWORD), indices, 6,
		D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, texture, sizeof(float) * 3);
	m_fogMode = savedFog;
	m_treeSwayOffset = savedSway;
	m_fillMode = savedFill;
	m_worldViewProjection = savedTransform;
	m_materialEnabled = savedMaterial;
	return result;
}

bool NativeD3D12Renderer::DrawMaskedScreenQuad(FLOAT x, FLOAT y, FLOAT width,
	FLOAT height, FLOAT u0, FLOAT v0, FLOAT u1, FLOAT v1,
	const NativeD3D12Texture* texture, const NativeD3D12Texture* mask, FLOAT radius)
{
	if (!texture || !mask || u1 == u0 || v1 == v0 || radius <= 0.0f) return false;
	const auto stages = m_materialStages;
	const auto coordinates = m_materialCoordinates;
	const auto textures = m_materialTextures;
	const auto samplers = m_materialSamplers;
	const bool materialEnabled = m_materialEnabled;
	NativeMaterialStage scene;
	scene.colorOp = NativeMaterialOp::Select1;
	scene.alphaOp = NativeMaterialOp::Select1;
	scene.alphaArg1 = UINT(NativeMaterialSource::Texture);
	NativeMaterialCoordinates uv;
	uv.offset = sizeof(float) * 3 + sizeof(DWORD);
	SetMaterialStage(0, scene, uv, texture);
	SetMaterialSampler(0);
	NativeMaterialStage masked;
	masked.colorOp = masked.alphaOp = NativeMaterialOp::Modulate;
	masked.alphaArg1 = UINT(NativeMaterialSource::Texture);
	masked.alphaArg2 = UINT(NativeMaterialSource::Current);
	uv.transform = true;
	uv.matrix[0] = 2.0f * radius / (u1 - u0);
	uv.matrix[5] = 2.0f * radius / (v1 - v0);
	// Authored two-component UV transforms multiply (u,v,1,0), unlike
	// position-generated transforms which multiply (x,y,z,1).
	uv.matrix[8] = 0.5f - radius - u0 * uv.matrix[0];
	uv.matrix[9] = 0.5f - radius - v0 * uv.matrix[5];
	SetMaterialStage(1, masked, uv, mask);
	SetMaterialSampler(1);
	SetMaterialStage(2, NativeMaterialStage(), NativeMaterialCoordinates(), nullptr);
	SetMaterialStage(3, NativeMaterialStage(), NativeMaterialCoordinates(), nullptr);
	const bool result = DrawTexturedScreenQuad(x, y, width, height, u0, v0, u1, v1,
		0xffffffff, texture, true);
	m_materialStages = stages;
	m_materialCoordinates = coordinates;
	m_materialTextures = textures;
	m_materialSamplers = samplers;
	m_materialEnabled = materialEnabled;
	return result;
}

void NativeD3D12Renderer::SetWorldViewProjection(const float* matrix16)
{
	if (matrix16 != nullptr)
		std::copy(matrix16, matrix16 + m_worldViewProjection.size(), m_worldViewProjection.begin());
}

bool NativeD3D12Renderer::UploadGeometry(const void* source, UINT size,
	D3D12_GPU_VIRTUAL_ADDRESS& address, ID3D12Resource** uploadResource, UINT64* uploadOffset)
{
	CpuTimer timer(m_profiling ? &m_cpuMilliseconds[1] : nullptr);
	if (!source) return false;
	ID3D12Resource* resource = nullptr;
	UINT64 offset = 0;
	unsigned char* mapped = nullptr;
	if (!AllocateFrameUpload(size,D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT,resource,offset,mapped))
		return false;
	std::memcpy(mapped,source,size);
	m_profileUploadBytes += size;
	address = resource->GetGPUVirtualAddress()+offset;
	if (uploadResource) *uploadResource = resource;
	if (uploadOffset) *uploadOffset = offset;
	return true;
}

bool NativeD3D12Renderer::ResolveIndexRange(const unsigned short* indices, UINT count,
	UINT vertexCount, UINT& first, UINT& last, const NativeD3D12UploadBuffer* owner)
{
	if (!owner || owner->m_locks || owner->m_streaming)
		return DrawVertexRange(indices,count,vertexCount,first,last);
	const uintptr_t begin = reinterpret_cast<uintptr_t>(owner->Data());
	const uintptr_t position = reinterpret_cast<uintptr_t>(indices);
	if (position < begin || position-begin > owner->Size() ||
		count > (owner->Size()-(position-begin))/sizeof(unsigned short)) return false;
	if (owner->m_indexRangesRevision != owner->m_revision) {
		owner->m_indexRanges.clear();
		owner->m_indexRangesRevision = owner->m_revision;
	}
	const auto key = std::make_pair(static_cast<size_t>(position-begin),count);
	auto found = owner->m_indexRanges.find(key);
	if (found != owner->m_indexRanges.end()) {
		first = found->second.first; last = found->second.second;
		return last < vertexCount;
	}
	if (!DrawVertexRange(indices,count,vertexCount,first,last)) return false;
	owner->m_indexRanges.emplace(key,std::make_pair(first,last));
	return true;
}

bool NativeD3D12Renderer::ResolveGeometry(const void* source, UINT size,
	D3D12_GPU_VIRTUAL_ADDRESS& address, const NativeD3D12UploadBuffer* owner)
{
	if (!owner) return UploadGeometry(source,size,address);
	const uintptr_t begin = reinterpret_cast<uintptr_t>(owner->Data());
	const uintptr_t position = reinterpret_cast<uintptr_t>(source);
	if (position < begin || position-begin > owner->Size() ||
		size > owner->Size()-(position-begin) || owner->Size() > UINT_MAX) return false;
	// Some immediate-mode draws intentionally occur inside a write lock. Their
	// pointers may be edited again without a new revision: take a transient copy.
	// Dynamic UI/sorting arenas append tiny ranges repeatedly. Copying their
	// entire capacity for each revision would cost more than streaming the draw.
	if (owner->m_locks || owner->m_streaming) return UploadGeometry(source,size,address);
	auto& versions = owner->m_gpuVersions;
	versions.erase(std::remove_if(versions.begin(),versions.end(),[this](const auto& entry) {
		return entry->deviceIdentity.lock() != m_descriptorPool;
	}),versions.end());
	std::shared_ptr<NativeD3D12BufferVersion> version;
	for (const auto& entry : versions) {
		if (entry->revision == owner->m_revision) { version = entry; ++m_profileBufferHits; break; }
	}
	if (!version) {
		for (const auto& entry : versions) {
			if (entry.use_count() == 1) { version = entry; break; }
		}
		if (!version) {
			version = std::make_shared<NativeD3D12BufferVersion>();
			D3D12_HEAP_PROPERTIES heap = {};
			heap.Type = D3D12_HEAP_TYPE_DEFAULT;
			D3D12_RESOURCE_DESC desc = {};
			desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			desc.Width = owner->Size();
			desc.Height = desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
			desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			if (!CheckHr(m_device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
				D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&version->resource)),
				"Create persistent geometry buffer")) return false;
			version->deviceIdentity = m_descriptorPool;
			versions.push_back(version);
		}
		ID3D12Resource* upload = nullptr;
		UINT64 offset = 0;
		D3D12_GPU_VIRTUAL_ADDRESS unused;
		if (!UploadGeometry(owner->Data(),static_cast<UINT>(owner->Size()),unused,&upload,&offset)) return false;
		if (version->state != D3D12_RESOURCE_STATE_COPY_DEST) {
			const auto barrier = Transition(version->resource.Get(),version->state,D3D12_RESOURCE_STATE_COPY_DEST);
			m_commandList->ResourceBarrier(1,&barrier);
		}
		m_commandList->CopyBufferRegion(version->resource.Get(),0,upload,offset,owner->Size());
		const auto barrier = Transition(version->resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_GENERIC_READ);
		m_commandList->ResourceBarrier(1,&barrier);
		version->state = D3D12_RESOURCE_STATE_GENERIC_READ;
		version->revision = owner->m_revision;
		++m_profileBufferCopies;
	}
	address = version->resource->GetGPUVirtualAddress()+(position-begin);
	m_frameResources.RetainBuffer(m_frameIndex, version);
	return true;
}

void NativeD3D12Renderer::SetMaterialStage(UINT stage, const NativeMaterialStage& operation,
	const NativeMaterialCoordinates& coordinates, const NativeD3D12Texture* texture)
{
	if (stage>=4) return;
	m_materialStages[stage] = operation;
	m_materialCoordinates[stage] = coordinates;
	if (texture && m_materialTextures[stage] && m_materialTextures[stage]->m_storage == texture->m_storage) return;
	if (texture && texture->m_storage->pool == m_descriptorPool) {
		m_materialTextures[stage] = std::make_shared<NativeD3D12Texture>();
		m_materialTextures[stage]->m_storage = texture->m_storage;
	} else m_materialTextures[stage].reset();
}
void NativeD3D12Renderer::SetMaterialFactor(UINT32 color)
{
	DecodePackedColor(reinterpret_cast<const unsigned char*>(&color), m_materialFactor.data());
}
void NativeD3D12Renderer::SetMaterialSampler(UINT stage)
{
	if (stage<4) m_materialSamplers[stage] = m_currentSamplerGpu;
}


NativeD3D12PipelineSettings NativeD3D12Renderer::PipelineSettings() const
{
	NativeD3D12PipelineSettings settings;
	settings.fillMode = m_fillMode;
	settings.cullMode = m_cullMode;
	settings.depthBias = m_depthBias;
	settings.blendEnable = m_blendEnable;
	settings.sourceBlend = m_sourceBlend;
	settings.destinationBlend = m_destinationBlend;
	settings.blendOp = m_blendOp;
	settings.renderTargetWriteMask = m_renderTargetWriteMask;
	settings.useDefaultDepth = m_useDefaultDepth;
	settings.depthEnable = m_depthEnable;
	settings.depthWrite = m_depthWrite;
	settings.depthFunc = m_depthFunc;
	settings.stencilEnable = m_stencilEnable;
	settings.stencilReadMask = m_stencilReadMask;
	settings.stencilWriteMask = m_stencilWriteMask;
	settings.stencilFunc = m_stencilFunc;
	settings.stencilFail = m_stencilFail;
	settings.stencilDepthFail = m_stencilDepthFail;
	settings.stencilPass = m_stencilPass;
	settings.targetFormat = m_targetFormat;
	return settings;
}

bool NativeD3D12Renderer::CreateBasicPipeline(UINT colorOffset, UINT normalOffset, UINT specularOffset)
{
	CpuTimer timer(m_profiling ? &m_cpuMilliseconds[3] : nullptr);
	return m_pipelines.CreateBasic(m_device.Get(), PipelineSettings(), colorOffset, normalOffset, specularOffset);
}

bool NativeD3D12Renderer::CreateTexturedPipeline(const std::array<UINT,4>& texcoordOffsets,
	UINT colorOffset, UINT normalOffset, UINT specularOffset)
{
	CpuTimer timer(m_profiling ? &m_cpuMilliseconds[3] : nullptr);
	UINT stageCount = 0;
	while (stageCount < 4 && m_materialStages[stageCount].colorOp != NativeMaterialOp::Disable) ++stageCount;
	const UINT variant = m_materialEnabled ? stageCount + 1 : 0;
	return m_pipelines.CreateTextured(m_device.Get(), PipelineSettings(), texcoordOffsets,
		colorOffset, normalOffset, specularOffset, variant, m_treeSwayOffset);
}

bool NativeD3D12Renderer::AllocateFrameUpload(UINT size, UINT alignment,
	ID3D12Resource*& resource, UINT64& offset, unsigned char*& mapped)
{
	return m_recording && m_frameResources.Allocate(m_device.Get(), m_frameIndex,
		size, alignment, resource, offset, mapped);
}
