/*
** Command & Conquer Generals Zero Hour(tm)
** Native Direct3D 12 renderer device layer.
**
** Native D3D12 coordinator: device/presentation, frame submission and draw state.
** Pipeline/shader caches and fence-indexed resource lifetimes are composed
** modules. No Direct3D 8/9 interfaces, proxy DLLs or translated runtimes.
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
#include "native_d3d12_state.h"
#include "native_d3d12_pipeline_cache.h"
#include "native_d3d12_frame_resources.h"

class NativeD3D12Renderer final
{
public:
	static constexpr UINT FrameCount = NativeD3D12FrameResources::FrameCount;

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
	NativeD3D12PipelineSettings PipelineSettings() const;
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
	NativeD3D12PipelineCache m_pipelines;
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
	DXGI_FORMAT m_targetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	bool m_useDefaultDepth = true;
	UINT64 m_nextFenceValue = 0;
	std::array<UINT64, FrameCount> m_fenceValues = {};
	HANDLE m_fenceEvent = nullptr;
	std::array<float, 16> m_worldViewProjection = {};
	std::shared_ptr<NativeD3D12Texture> m_currentRenderTarget;
	NativeD3D12FrameResources m_frameResources;
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
