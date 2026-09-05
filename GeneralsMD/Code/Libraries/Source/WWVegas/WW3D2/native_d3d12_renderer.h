/*
** Command & Conquer Generals Zero Hour(tm)
** Native Direct3D 12 renderer device layer.
**
** This is deliberately a small, explicit D3D12 layer.  It does not expose
** Direct3D 8/9 interfaces and it does not load or proxy a graphics DLL.  The
** higher renderer owns draw-state recording; this class only owns the
** native D3D12 objects needed to submit a frame safely.
*/

#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <windows.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <memory>
#include <map>
#include "native_d3d12_lighting.h"

struct NativeD3D12DescriptorPool
{
	std::vector<UINT> freeSrv, freeRtv;
	UINT nextSrv = 0, nextRtv = 0;
};

struct NativeD3D12BufferVersion {
	Microsoft::WRL::ComPtr<ID3D12Resource> resource;
	std::weak_ptr<NativeD3D12DescriptorPool> deviceIdentity;
	UINT64 revision = 0;
	D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
};

// CPU authoring storage with revisioned, GPU-resident snapshots. Submitted
// frames retain snapshots until their fence retires; writes cannot overwrite them.
class NativeD3D12UploadBuffer final
{
public:
	NativeD3D12UploadBuffer() = default;
	explicit NativeD3D12UploadBuffer(std::size_t size, bool streaming = false) : m_bytes(size), m_streaming(streaming) {}

	HRESULT Lock(std::size_t offset, std::size_t size, void** data)
	{
		if (data == nullptr || offset > m_bytes.size() || size > m_bytes.size() - offset)
			return E_INVALIDARG;
		++m_revision;
		++m_locks;
		*data = m_bytes.data() + offset;
		return S_OK;
	}
	HRESULT Lock(std::size_t offset, std::size_t size, unsigned char** data, DWORD)
	{
		return Lock(offset, size, reinterpret_cast<void**>(data));
	}
	HRESULT Unlock() { if (m_locks) --m_locks; ++m_revision; return S_OK; }
	const void* Data() const { return m_bytes.data(); }
	void* MutableData() { ++m_revision; return m_bytes.data(); }
	std::size_t Size() const { return m_bytes.size(); }

private:
	friend class NativeD3D12Renderer;
	std::vector<unsigned char> m_bytes;
	UINT64 m_revision = 1;
	UINT m_locks = 0;
	bool m_streaming = false;
	mutable std::vector<std::shared_ptr<NativeD3D12BufferVersion>> m_gpuVersions;
	mutable UINT64 m_indexRangesRevision = 0;
	mutable std::map<std::pair<size_t,UINT>,std::pair<UINT,UINT>> m_indexRanges;
};

// A renderer-owned sampled texture. This is an engine resource, not a
// compatibility object: it exposes only native D3D12 resources and handles.
struct NativeD3D12TextureLevel
{
	const void* data = nullptr;
	UINT rowPitch = 0;
	UINT slicePitch = 0;
};

enum class NativeD3D12FilterMode : UINT8
{
	Point,
	Linear,
	Anisotropic
};

struct NativeD3D12TextureStorage
{
	Microsoft::WRL::ComPtr<ID3D12Resource> m_resource;
	D3D12_CPU_DESCRIPTOR_HANDLE m_srvCpu = {};
	D3D12_GPU_DESCRIPTOR_HANDLE m_srvGpu = {};
	D3D12_CPU_DESCRIPTOR_HANDLE m_rtvCpu = {};
	bool m_hasRtv = false;
	UINT m_width = 0;
	UINT m_height = 0;
	UINT m_mipLevels = 0;
	DXGI_FORMAT m_format = DXGI_FORMAT_UNKNOWN;
	D3D12_RESOURCE_STATES m_state = D3D12_RESOURCE_STATE_COMMON;
	std::shared_ptr<NativeD3D12DescriptorPool> pool;
	UINT srvIndex = UINT_MAX, rtvIndex = UINT_MAX;
	~NativeD3D12TextureStorage()
	{
		if (pool && srvIndex != UINT_MAX) pool->freeSrv.push_back(srvIndex);
		if (pool && rtvIndex != UINT_MAX) pool->freeRtv.push_back(rtvIndex);
	}
};

class NativeD3D12Texture final
{
public:
	NativeD3D12Texture() = default;
	NativeD3D12Texture(const NativeD3D12Texture&) = delete;
	NativeD3D12Texture& operator=(const NativeD3D12Texture&) = delete;

	ID3D12Resource* Resource() const { return m_storage->m_resource.Get(); }
	D3D12_CPU_DESCRIPTOR_HANDLE SrvCpuHandle() const { return m_storage->m_srvCpu; }
	D3D12_GPU_DESCRIPTOR_HANDLE SrvGpuHandle() const { return m_storage->m_srvGpu; }
	D3D12_CPU_DESCRIPTOR_HANDLE RtvCpuHandle() const { return m_storage->m_rtvCpu; }
	bool HasRenderTargetView() const { return m_storage->m_hasRtv; }
	UINT Width() const { return m_storage->m_width; }
	UINT Height() const { return m_storage->m_height; }
	UINT MipLevels() const { return m_storage->m_mipLevels; }
	DXGI_FORMAT Format() const { return m_storage->m_format; }
	bool IsValid() const { return m_storage->m_resource != nullptr; }
	// Distinct engine texture settings can reference the same GPU allocation.
	// Each reference participates in the existing fence-retired storage lifetime.
	NativeD3D12Texture* ShareResource() const {
		auto* reference = new NativeD3D12Texture;
		reference->m_storage = m_storage;
		return reference;
	}

private:
	friend class NativeD3D12Renderer;
	std::shared_ptr<NativeD3D12TextureStorage> m_storage = std::make_shared<NativeD3D12TextureStorage>();
};

// Engine material data compiled into native HLSL. No legacy API objects or bytecode.
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

class NativeD3D12Renderer final
{
public:
	static constexpr UINT FrameCount = 2;

	NativeD3D12Renderer() = default;
	NativeD3D12Renderer(const NativeD3D12Renderer&) = delete;
	NativeD3D12Renderer& operator=(const NativeD3D12Renderer&) = delete;
	~NativeD3D12Renderer();

	// Creates a hardware D3D12 device and a flip-model swap chain for hwnd.
	// A WARP device is used only when no hardware adapter can create D3D12.
	bool Initialize(HWND hwnd, UINT width, UINT height, bool windowed);
	void Shutdown();
	bool Resize(UINT width, UINT height);

	// Records the invariant part of a frame transition.  Game render passes
	// append their commands between BeginFrame and EndFrame.
	bool BeginFrame(const FLOAT clearColor[4], FLOAT clearDepth = 1.0f);
	void Clear(const FLOAT color[4], FLOAT depth = 1.0f, UINT8 stencil = 0,
		bool clearColor = true, bool clearDepthStencil = true);
	bool EndFrame(UINT syncInterval = 0, bool present = true);
	bool ReadbackFrame(std::vector<unsigned char>& rgba);
	bool IsRecording() const { return m_recording; }
	void SetViewport(FLOAT x, FLOAT y, FLOAT width, FLOAT height,
		FLOAT minDepth = 0.0f, FLOAT maxDepth = 1.0f);

	bool IsInitialized() const { return m_device != nullptr && m_swapChain != nullptr; }
	static NativeD3D12Renderer* Active();
	NativeD3D12State CaptureState() const;
	// Signed D24 depth units; negative values pull coplanar decals toward the camera.
	void SetDepthBias(INT bias) { m_depthBias = bias; }
	bool SetRasterizerFill(D3D12_FILL_MODE mode) {
		if (mode!=D3D12_FILL_MODE_SOLID && mode!=D3D12_FILL_MODE_WIREFRAME) return false;
		m_fillMode=mode;
		return true;
	}
	void RestoreState(const NativeD3D12State& state);
	void SetWorldViewProjection(const float* matrix16);
	UINT Width() const { return m_width; }
	UINT Height() const { return m_height; }

	ID3D12Device* Device() const { return m_device.Get(); }
	ID3D12GraphicsCommandList* CommandList() const { return m_commandList.Get(); }
	ID3D12Resource* CurrentBackBuffer() const { return m_backBuffers[m_frameIndex].Get(); }
	D3D12_CPU_DESCRIPTOR_HANDLE CurrentRenderTarget() const;
	D3D12_CPU_DESCRIPTOR_HANDLE DepthStencil() const { return m_dsvHeap->GetCPUDescriptorHandleForHeapStart(); }

	// Creates a native sampled texture. The resource starts in COPY_DEST;
	// UploadTexture2D records the subresource copies into the open command list.
	NativeD3D12Texture* CreateTexture2D(UINT width, UINT height, UINT mipLevels,
		DXGI_FORMAT format, bool renderTarget = false);
	bool UploadTexture2D(NativeD3D12Texture& texture,
		const NativeD3D12TextureLevel* levels, UINT levelCount);
	static UINT TextureMipCount(UINT width, UINT height, UINT requested = 0);
	bool UploadBgraTexture(NativeD3D12Texture& texture,
		const NativeD3D12TextureLevel& baseLevel, bool opaque = false);
	// Explicit, infrequent CPU access for recoloring and surface copies. Flushes
	// queued work but preserves the current frame, target, viewport and uploads.
	bool ReadbackTexture(const NativeD3D12Texture& texture, UINT mip,
		std::vector<unsigned char>& bgra);
	static bool DecodeTextureBgra(DXGI_FORMAT format, UINT width, UINT height,
		const NativeD3D12TextureLevel& source, std::vector<unsigned char>& bgra);
	void BindTexture(UINT rootParameter, const NativeD3D12Texture* texture);
	void SetSamplerState(NativeD3D12FilterMode minFilter, NativeD3D12FilterMode magFilter,
		NativeD3D12FilterMode mipFilter, bool clampU, bool clampV, UINT maxAnisotropy = 1);
	bool SetRenderTarget(const NativeD3D12Texture* texture, bool useDefaultDepth = true);
	// GPU-only snapshot for refraction/distortion. Keeps the current target,
	// viewport and frame open; destination becomes shader-readable.
	bool CopyCurrentRenderTarget(NativeD3D12Texture& destination);
	DXGI_FORMAT RenderTargetFormat() const { return m_targetFormat; }
	UINT RenderTargetWidth() const { return m_currentRenderTarget ? m_currentRenderTarget->Width() : m_width; }
	UINT RenderTargetHeight() const { return m_currentRenderTarget ? m_currentRenderTarget->Height() : m_height; }
	void SetFixedFunctionState(D3D12_CULL_MODE cullMode, bool depthEnable,
		bool depthWrite, D3D12_COMPARISON_FUNC depthFunc, bool blendEnable,
		D3D12_BLEND sourceBlend, D3D12_BLEND destinationBlend,
		D3D12_BLEND_OP blendOp, UINT8 renderTargetWriteMask);
	void SetAlphaTestState(bool enable, D3D12_COMPARISON_FUNC function, UINT8 reference);
	void SetGrayscale(bool enable, UINT32 tint = 0xffffffff, float amount = 1.0f);
	// Tree vertices store sway index, push-aside darkening and base height in
	// their former normal field. Null data disables this scoped vertex effect.
	void SetTreeSway(const float (*offsets)[4], UINT count, UINT vertexOffset = 12);
	void SetVertexFog(UINT mode, float start, float end, float density, UINT32 color, bool range);
	void SetWorldView(const float* matrix16);
	void SetLighting(const NativeLightingState& state);
	static void ReportMissingTexture(const char* filename, bool ddsAvailable);
	void SetMaterialStage(UINT stage, const NativeMaterialStage& operation,
		const NativeMaterialCoordinates& coordinates, const NativeD3D12Texture* texture);
	void SetMaterialEnabled(bool enabled) { m_materialEnabled = enabled; }
	bool MaterialEnabled() const { return m_materialEnabled; }
	void SetMaterialFactor(UINT32 color);
	void SetMaterialSampler(UINT stage);
	void SetTextureCombine(bool textureColor, bool vertexColor,
		bool textureAlpha, bool vertexAlpha);
	void SetStencilState(bool enable, D3D12_COMPARISON_FUNC function, UINT8 reference,
		UINT8 readMask, UINT8 writeMask, D3D12_STENCIL_OP failOperation,
		D3D12_STENCIL_OP depthFailOperation, D3D12_STENCIL_OP passOperation);
	bool DrawScreenQuad(FLOAT x, FLOAT y, FLOAT width, FLOAT height, UINT32 color);
	bool DrawTexturedScreenQuad(FLOAT x, FLOAT y, FLOAT width, FLOAT height,
		FLOAT u0, FLOAT v0, FLOAT u1, FLOAT v1, UINT32 color,
		const NativeD3D12Texture* texture, bool useMaterial = false);
	bool DrawMaskedScreenQuad(FLOAT x, FLOAT y, FLOAT width, FLOAT height,
		FLOAT u0, FLOAT v0, FLOAT u1, FLOAT v1,
		const NativeD3D12Texture* texture, const NativeD3D12Texture* mask, FLOAT radius);
	bool DrawIndexedTextured(const void* vertices, UINT vertexBytes, UINT vertexStride,
		UINT vertexCount, UINT texcoordOffset, const unsigned short* indices,
		UINT indexCount, D3D12_PRIMITIVE_TOPOLOGY topology,
		const NativeD3D12Texture* texture, UINT colorOffset = UINT_MAX,
		const NativeD3D12UploadBuffer* vertexOwner = nullptr,
		const NativeD3D12UploadBuffer* indexOwner = nullptr,
		UINT normalOffset = UINT_MAX, UINT specularOffset = UINT_MAX);

	const DXGI_ADAPTER_DESC1& AdapterDescription() const { return m_adapterDescription; }
	UINT ResidentBufferCopiesThisFrame() const { return m_profileBufferCopies; }
	bool ProfilingEnabled() const { return m_profiling; }
	void SetEngineCpuTiming(double scene, double ui, double preparation, double overlays, double windows) {
		m_engineCpuMs[0] = scene; m_engineCpuMs[1] = ui; m_engineCpuMs[2] = preparation;
		m_engineCpuMs[4] = overlays; m_engineCpuMs[5] = windows;
	}
	void AddTextureConversionMs(double elapsed) { m_engineCpuMs[3] += elapsed; }

	// Used by device-loss and process shutdown paths.  All submitted work must
	// be complete before command allocators, descriptors, or resources go away.
	void WaitForGpu();

	// Uploads a transient geometry range and records it in the current command
	// list.  The returned views remain valid until the submitted frame retires.
	bool DrawIndexed(const void* vertices, UINT vertexBytes, UINT vertexStride,
		UINT vertexCount, const unsigned short* indices, UINT indexCount,
		UINT startIndex, UINT baseVertex, D3D12_PRIMITIVE_TOPOLOGY topology,
		UINT colorOffset = UINT_MAX, const NativeD3D12UploadBuffer* vertexOwner = nullptr,
		const NativeD3D12UploadBuffer* indexOwner = nullptr,
		UINT normalOffset = UINT_MAX, UINT specularOffset = UINT_MAX);

private:
	bool CreateDeviceAndQueue();
	bool CreateSwapChain();
	bool CreateGpuProfiler();
	bool CreateFrameResources();
	bool CreateDepthBuffer();
	bool CreateDescriptorHeaps();
	bool CreateCommandObjects();
	bool CreateBasicPipeline(UINT colorOffset, UINT normalOffset = UINT_MAX, UINT specularOffset = UINT_MAX);
	bool CreateTexturedPipeline(const std::array<UINT,4>& texcoordOffsets, UINT colorOffset,
		UINT normalOffset, UINT specularOffset);
	using PipelineKey = std::array<UINT, 30>;
	PipelineKey GetPipelineKey(bool textured) const;
	bool UploadGeometry(const void* source, UINT size, D3D12_GPU_VIRTUAL_ADDRESS& address,
		ID3D12Resource** uploadResource = nullptr, UINT64* uploadOffset = nullptr);
	bool AllocateFrameUpload(UINT size, UINT alignment, ID3D12Resource*& resource,
		UINT64& offset, unsigned char*& mapped);
	bool ResolveGeometry(const void* source, UINT size, D3D12_GPU_VIRTUAL_ADDRESS& address,
		const NativeD3D12UploadBuffer* owner);
	bool ResolveIndexRange(const unsigned short* indices, UINT count, UINT vertexCount,
		UINT& first, UINT& last, const NativeD3D12UploadBuffer* owner);
	bool WaitForFence(UINT64 value);
	bool RetainTexture(const NativeD3D12Texture* texture);
	bool SelectAdapter();
	bool CheckHr(HRESULT hr, const char* operation) const;

	static D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* resource,
		D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);

	HWND m_hwnd = nullptr;
	UINT m_width = 0;
	UINT m_height = 0;
	bool m_windowed = true;
	UINT m_swapChainFlags = 0;
	UINT m_frameIndex = 0;
	UINT m_rtvDescriptorSize = 0;
	UINT m_srvDescriptorSize = 0;
	std::shared_ptr<NativeD3D12DescriptorPool> m_descriptorPool;
	UINT m_rtvTextureDescriptorSize = 0;


	Microsoft::WRL::ComPtr<IDXGIFactory6> m_factory;
	Microsoft::WRL::ComPtr<IDXGIAdapter1> m_adapter;
	DXGI_ADAPTER_DESC1 m_adapterDescription = {};
	Microsoft::WRL::ComPtr<ID3D12Device> m_device;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue;
	Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swapChain;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_textureRtvHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_samplerHeap;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_depthBuffer;
	std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, FrameCount> m_backBuffers;

	std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, FrameCount> m_commandAllocators;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;
	Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_basicPipeline;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_texturedPipeline;
	UINT m_basicColorOffset = UINT_MAX;
	UINT m_texturedTexcoordOffset = UINT_MAX;
	UINT m_texturedColorOffset = UINT_MAX;
	D3D12_CULL_MODE m_cullMode = D3D12_CULL_MODE_NONE;
	bool m_depthEnable = true;
	bool m_depthWrite = true;
	D3D12_COMPARISON_FUNC m_depthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	INT m_depthBias = 0;
	D3D12_FILL_MODE m_fillMode = D3D12_FILL_MODE_SOLID;
	bool m_blendEnable = false;
	D3D12_BLEND m_sourceBlend = D3D12_BLEND_ONE;
	D3D12_BLEND m_destinationBlend = D3D12_BLEND_ZERO;
	D3D12_BLEND_OP m_blendOp = D3D12_BLEND_OP_ADD;
	UINT8 m_renderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	bool m_alphaTestEnable = false;
	D3D12_COMPARISON_FUNC m_alphaTestFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	UINT8 m_alphaTestRef = 0;
	bool m_grayscale = false;
	UINT32 m_grayscaleTint = 0xffffffff;
	float m_grayscaleAmount = 1.0f;
	bool m_materialEnabled = false;
	std::array<std::array<float,4>,11> m_treeSway = {};
	UINT m_treeSwayOffset = UINT_MAX;
	UINT m_fogMode = 0;
	bool m_fogRange = false;
	std::array<float,4> m_fogParameters = {}, m_fogColor = {};
	std::array<float,16> m_worldView = {};
	NativeLightingState m_lighting;
	std::array<NativeMaterialStage, 4> m_materialStages;
	std::array<NativeMaterialCoordinates, 4> m_materialCoordinates;
	std::array<std::shared_ptr<NativeD3D12Texture>, 4> m_materialTextures;
	std::unique_ptr<NativeD3D12Texture> m_neutralMaterialTexture;
	std::array<D3D12_GPU_DESCRIPTOR_HANDLE, 4> m_materialSamplers = {};
	std::array<float, 4> m_materialFactor = {1,1,1,1};
	bool m_textureColorTexture = true;
	bool m_textureColorVertex = true;
	bool m_textureAlphaTexture = true;
	bool m_textureAlphaVertex = true;
	bool m_stencilEnable = false;
	D3D12_COMPARISON_FUNC m_stencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	UINT8 m_stencilRef = 0;
	UINT8 m_stencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
	UINT8 m_stencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
	D3D12_STENCIL_OP m_stencilFail = D3D12_STENCIL_OP_KEEP;
	D3D12_STENCIL_OP m_stencilDepthFail = D3D12_STENCIL_OP_KEEP;
	D3D12_STENCIL_OP m_stencilPass = D3D12_STENCIL_OP_KEEP;
	std::map<PipelineKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>> m_pipelineCache;
	std::array<Microsoft::WRL::ComPtr<ID3DBlob>, 4> m_shaderCache;
	std::array<Microsoft::WRL::ComPtr<ID3DBlob>, 6> m_materialPixelShaders;
	DXGI_FORMAT m_targetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	bool m_useDefaultDepth = true;
	UINT64 m_nextFenceValue = 0;
	std::array<UINT64, FrameCount> m_fenceValues = {};
	HANDLE m_fenceEvent = nullptr;
	std::array<std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>>, FrameCount> m_uploadResources;
	std::array<float, 16> m_worldViewProjection = {};
	std::shared_ptr<NativeD3D12Texture> m_currentRenderTarget;
	std::array<std::vector<std::shared_ptr<NativeD3D12TextureStorage>>, FrameCount> m_textureReferences;
	struct UploadPage {
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		unsigned char* mapped = nullptr;
		UINT size = 0, used = 0;
	};
	std::array<std::vector<UploadPage>, FrameCount> m_uploadPages;
	std::array<std::vector<std::shared_ptr<NativeD3D12BufferVersion>>, FrameCount> m_bufferReferences;
	D3D12_VIEWPORT m_viewport = {};
	D3D12_RECT m_scissor = {};
	UINT m_samplerDescriptorSize = 0;
	D3D12_GPU_DESCRIPTOR_HANDLE m_currentSamplerGpu = {};
	bool m_recording = false;
	bool m_diagnostics = false;
	bool m_profiling = false;
	std::array<double, 4> m_cpuMilliseconds = {};
	UINT64 m_profileVertices = 0, m_profileUploadBytes = 0;
	UINT m_profileBufferHits = 0, m_profileBufferCopies = 0;
	ULONGLONG m_profileWindowStart = 0;
	UINT m_profileWindowFrames = 0;
	Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_timestampHeap;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_timestampReadback;
	std::array<bool,FrameCount> m_timestampReady = {};
	UINT64 m_timestampFrequency = 0;
	double m_profileGpuMs = 0, m_profilePresentMs = 0, m_profileFenceMs = 0;
	std::array<double,6> m_engineCpuMs = {};
	UINT64 m_submittedFrames = 0;
	UINT m_recordedDraws = 0;
};

// Use around an effect/UI pass so it cannot leak native draw settings into
// the following world pass, including exits caused by missing resources.
class NativeD3D12ScopedState final
{
public:
	explicit NativeD3D12ScopedState(NativeD3D12Renderer& renderer)
		: m_renderer(renderer), m_state(renderer.CaptureState()) {}
	~NativeD3D12ScopedState() { m_renderer.RestoreState(m_state); }
	NativeD3D12ScopedState(const NativeD3D12ScopedState&) = delete;
	NativeD3D12ScopedState& operator=(const NativeD3D12ScopedState&) = delete;
private:
	NativeD3D12Renderer& m_renderer;
	NativeD3D12State m_state;
};
