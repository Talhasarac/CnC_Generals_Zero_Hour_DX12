/*
** Command & Conquer Generals Zero Hour(tm)
** Native Direct3D 12 renderer device layer.
*/

#include "native_d3d12_renderer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <d3dcompiler.h>
#include <d3d12sdklayers.h>
#include <chrono>
#include <cmath>
#include <string>

using Microsoft::WRL::ComPtr;

namespace
{
	NativeD3D12Renderer* activeRenderer = nullptr;
}

namespace
{
	void DiagnosticLog(const char* message)
	{
		if (!GetEnvironmentVariableA("GENERALS_D3D12_DIAGNOSTICS", nullptr, 0) &&
			!GetEnvironmentVariableA("GENERALS_D3D12_PROFILE", nullptr, 0)) return;
		FILE* file = nullptr;
		if (fopen_s(&file, "NativeD3D12.log", "a") == 0 && file) {
			std::fputs(message, file);
			std::fclose(file);
		}
	}
	struct CpuTimer {
		double* total;
		std::chrono::steady_clock::time_point start;
		explicit CpuTimer(double* value) : total(value) { if (total) start = std::chrono::steady_clock::now(); }
		~CpuTimer() { if (total) *total += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-start).count(); }
	};
	void LogD3D12Failure(const char* operation, HRESULT hr)
	{
		char buffer[256] = {};
		std::snprintf(buffer, sizeof(buffer), "NativeD3D12: %s failed (0x%08lX)\n",
			operation, static_cast<unsigned long>(hr));
		OutputDebugStringA(buffer);
		DiagnosticLog(buffer);
	}

	D3D12_BLEND AlphaBlendFactor(D3D12_BLEND blend)
	{
		switch (blend) {
		case D3D12_BLEND_SRC_COLOR: return D3D12_BLEND_SRC_ALPHA;
		case D3D12_BLEND_INV_SRC_COLOR: return D3D12_BLEND_INV_SRC_ALPHA;
		case D3D12_BLEND_DEST_COLOR: return D3D12_BLEND_DEST_ALPHA;
		case D3D12_BLEND_INV_DEST_COLOR: return D3D12_BLEND_INV_DEST_ALPHA;
		default: return blend;
		}
	}

	bool IsSoftwareAdapter(const DXGI_ADAPTER_DESC1& description)
	{
		return (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
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

	D3D12_FILTER SamplerFilter(UINT filterBits)
	{
		switch (filterBits & 7u)
		{
		case 0: return D3D12_FILTER_MIN_MAG_MIP_POINT;
		case 1: return D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR;
		case 2: return D3D12_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
		case 3: return D3D12_FILTER_MIN_POINT_MAG_MIP_LINEAR;
		case 4: return D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT;
		case 5: return D3D12_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
		case 6: return D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
		default: return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		}
	}
}

NativeD3D12Renderer::~NativeD3D12Renderer()
{
	Shutdown();
}

NativeD3D12Renderer* NativeD3D12Renderer::Active()
{
	return activeRenderer;
}

bool NativeD3D12Renderer::CheckHr(HRESULT hr, const char* operation) const
{
	if (SUCCEEDED(hr))
		return true;
	LogD3D12Failure(operation, hr);
	return false;
}

bool NativeD3D12Renderer::Initialize(HWND hwnd, UINT width, UINT height, bool windowed)
{
	Shutdown();
	if (hwnd == nullptr || width == 0 || height == 0)
		return false;

	m_hwnd = hwnd;
	m_width = width;
	m_height = height;
	m_windowed = windowed;
	m_diagnostics = GetEnvironmentVariableA("GENERALS_D3D12_DIAGNOSTICS", nullptr, 0) != 0;
	m_profiling = GetEnvironmentVariableA("GENERALS_D3D12_PROFILE", nullptr, 0) != 0;
	m_submittedFrames = 0;
	DiagnosticLog("Initialize native renderer\n");
	m_worldViewProjection = {1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f};

	UINT factoryFlags = 0;
#if !defined(_DEBUG)
	if (m_diagnostics)
#endif
	{
		ComPtr<ID3D12Debug> debug;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
		{
			debug->EnableDebugLayer();
			factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
	}

	if (!CheckHr(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&m_factory)),
		"CreateDXGIFactory2"))
		return false;
	if (!SelectAdapter() || !CreateDeviceAndQueue() || !CreateCommandObjects() ||
		!CreateDescriptorHeaps() || !CreateSwapChain() || !CreateFrameResources() ||
		!CreateDepthBuffer() || !CreateBasicPipeline(UINT_MAX) || (m_profiling && !CreateGpuProfiler()))
	{
		Shutdown();
		return false;
	}

	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
	activeRenderer = this;
	if (m_diagnostics || m_profiling) {
		char adapterName[256] = {};
		WideCharToMultiByte(CP_UTF8, 0, m_adapterDescription.Description, -1, adapterName, sizeof(adapterName), nullptr, nullptr);
		DiagnosticLog(adapterName); DiagnosticLog("\n");
	}
	return true;
}

bool NativeD3D12Renderer::SelectAdapter()
{
	// Prefer a hardware adapter.  IDXGIAdapter4 lets us skip software devices
	// without creating a temporary D3D12 device for every adapter.
	for (UINT index = 0;; ++index)
	{
		ComPtr<IDXGIAdapter1> candidate;
		const HRESULT enumResult = m_factory->EnumAdapterByGpuPreference(index,
			DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&candidate));
		if (enumResult == DXGI_ERROR_NOT_FOUND)
			break;
		if (FAILED(enumResult))
			return CheckHr(enumResult, "EnumAdapterByGpuPreference");

		DXGI_ADAPTER_DESC1 description = {};
		candidate->GetDesc1(&description);
		if (IsSoftwareAdapter(description))
			continue;

		ComPtr<ID3D12Device> probe;
		if (SUCCEEDED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&probe))))
		{
			m_adapter = candidate;
			m_adapterDescription = description;
			return true;
		}
	}

	// WARP is a native D3D12 adapter, not a graphics translation layer.  It
	// keeps the renderer testable on machines without a usable hardware driver.
	ComPtr<IDXGIAdapter> warp;
	if (!CheckHr(m_factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "EnumWarpAdapter"))
		return false;
	if (!CheckHr(warp.As(&m_adapter), "Query WARP adapter"))
		return false;
	return CheckHr(m_adapter->GetDesc1(&m_adapterDescription), "Get WARP description");
}

bool NativeD3D12Renderer::CreateDeviceAndQueue()
{
	if (!CheckHr(D3D12CreateDevice(m_adapter.Get(), D3D_FEATURE_LEVEL_11_0,
		IID_PPV_ARGS(&m_device)), "D3D12CreateDevice"))
		return false;

	D3D12_COMMAND_QUEUE_DESC queue = {};
	queue.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queue.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	return CheckHr(m_device->CreateCommandQueue(&queue, IID_PPV_ARGS(&m_commandQueue)),
		"CreateCommandQueue");
}

bool NativeD3D12Renderer::CreateCommandObjects()
{
	for (auto& allocator : m_commandAllocators)
	{
		if (!CheckHr(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(&allocator)), "CreateCommandAllocator"))
			return false;
	}
	if (!CheckHr(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
		m_commandAllocators[0].Get(), nullptr, IID_PPV_ARGS(&m_commandList)),
		"CreateCommandList"))
		return false;
	if (!CheckHr(m_commandList->Close(), "Close initial command list"))
		return false;
	if (!CheckHr(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)),
		"CreateFence"))
		return false;
	m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	return m_fenceEvent != nullptr;
}

bool NativeD3D12Renderer::CreateDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC rtv = {};
	rtv.NumDescriptors = FrameCount;
	rtv.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	if (!CheckHr(m_device->CreateDescriptorHeap(&rtv, IID_PPV_ARGS(&m_rtvHeap)),
		"Create RTV descriptor heap"))
		return false;

	D3D12_DESCRIPTOR_HEAP_DESC textureRtv = {};
	textureRtv.NumDescriptors = 2048;
	textureRtv.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	if (!CheckHr(m_device->CreateDescriptorHeap(&textureRtv, IID_PPV_ARGS(&m_textureRtvHeap)),
		"Create texture RTV descriptor heap"))
		return false;

	D3D12_DESCRIPTOR_HEAP_DESC dsv = {};
	dsv.NumDescriptors = 1;
	dsv.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	if (!CheckHr(m_device->CreateDescriptorHeap(&dsv, IID_PPV_ARGS(&m_dsvHeap)),
		"Create DSV descriptor heap"))
		return false;

	D3D12_DESCRIPTOR_HEAP_DESC srv = {};
	srv.NumDescriptors = 4096;
	srv.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srv.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	if (!CheckHr(m_device->CreateDescriptorHeap(&srv, IID_PPV_ARGS(&m_srvHeap)),
		"Create shader-resource descriptor heap"))
		return false;

	D3D12_DESCRIPTOR_HEAP_DESC sampler = {};
	sampler.NumDescriptors = 36;
	sampler.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
	sampler.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	if (!CheckHr(m_device->CreateDescriptorHeap(&sampler, IID_PPV_ARGS(&m_samplerHeap)),
		"Create sampler descriptor heap"))
		return false;

	m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	m_srvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_samplerDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
	m_rtvTextureDescriptorSize = m_rtvDescriptorSize;
	m_descriptorPool = std::make_shared<NativeD3D12DescriptorPool>();
	for (UINT filterBits = 0; filterBits <= 8; ++filterBits)
	{
		for (UINT addressBits = 0; addressBits < 4; ++addressBits)
		{
			const UINT descriptorIndex = filterBits * 4 + addressBits;
			D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_samplerHeap->GetCPUDescriptorHandleForHeapStart();
			cpu.ptr += static_cast<SIZE_T>(descriptorIndex) * m_samplerDescriptorSize;
			D3D12_SAMPLER_DESC description = {};
			description.Filter = filterBits == 8 ? D3D12_FILTER_ANISOTROPIC : SamplerFilter(filterBits);
			description.AddressU = (addressBits & 1) != 0 ?
				D3D12_TEXTURE_ADDRESS_MODE_CLAMP : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			description.AddressV = (addressBits & 2) != 0 ?
				D3D12_TEXTURE_ADDRESS_MODE_CLAMP : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			description.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			description.MipLODBias = 0.0f;
			description.MaxAnisotropy = 16;
			description.ComparisonFunc = D3D12_COMPARISON_FUNC_NONE;
			description.MinLOD = 0.0f;
			description.MaxLOD = D3D12_FLOAT32_MAX;
			m_device->CreateSampler(&description, cpu);
		}
	}
	D3D12_GPU_DESCRIPTOR_HANDLE samplerGpu = m_samplerHeap->GetGPUDescriptorHandleForHeapStart();
	m_currentSamplerGpu = samplerGpu;
	return true;
}

bool NativeD3D12Renderer::CreateSwapChain()
{
	BOOL allowTearing = FALSE;
	m_swapChainFlags = SUCCEEDED(m_factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
		&allowTearing,sizeof(allowTearing))) && allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
	DiagnosticLog(m_swapChainFlags ? "Windowed immediate presentation supported\n" : "Windowed immediate presentation unavailable\n");
	DXGI_SWAP_CHAIN_DESC1 description = {};
	description.Flags = m_swapChainFlags;
	description.Width = m_width;
	description.Height = m_height;
	description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	description.BufferCount = FrameCount;
	description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	description.SampleDesc.Count = 1;
	description.Scaling = DXGI_SCALING_STRETCH;
	description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

	ComPtr<IDXGISwapChain1> swapChain;
	if (!CheckHr(m_factory->CreateSwapChainForHwnd(m_commandQueue.Get(), m_hwnd,
		&description, nullptr, nullptr, &swapChain), "CreateSwapChainForHwnd"))
		return false;
	if (!CheckHr(m_factory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER),
		"MakeWindowAssociation"))
		return false;
	return CheckHr(swapChain.As(&m_swapChain), "Query IDXGISwapChain3");
}

bool NativeD3D12Renderer::CreateGpuProfiler()
{
	D3D12_QUERY_HEAP_DESC queries = {};
	queries.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
	queries.Count = FrameCount*2;
	if (!CheckHr(m_device->CreateQueryHeap(&queries,IID_PPV_ARGS(&m_timestampHeap)),"Create GPU timer queries") ||
		!CheckHr(m_commandQueue->GetTimestampFrequency(&m_timestampFrequency),"Get GPU timer frequency")) return false;
	D3D12_HEAP_PROPERTIES heap = {};
	heap.Type = D3D12_HEAP_TYPE_READBACK;
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = FrameCount*2*sizeof(UINT64);
	desc.Height = desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	return CheckHr(m_device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
		D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&m_timestampReadback)),"Create GPU timer readback");
}

bool NativeD3D12Renderer::CreateFrameResources()
{
	D3D12_CPU_DESCRIPTOR_HANDLE handle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
	for (UINT index = 0; index < FrameCount; ++index)
	{
		if (!CheckHr(m_swapChain->GetBuffer(index, IID_PPV_ARGS(&m_backBuffers[index])),
			"Get swap-chain buffer"))
			return false;
		m_device->CreateRenderTargetView(m_backBuffers[index].Get(), nullptr, handle);
		handle.ptr += m_rtvDescriptorSize;
	}
	return true;
}

bool NativeD3D12Renderer::CreateDepthBuffer()
{
	D3D12_HEAP_PROPERTIES heap = {};
	heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC resource = {};
	resource.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resource.Width = m_width;
	resource.Height = m_height;
	resource.DepthOrArraySize = 1;
	resource.MipLevels = 1;
	resource.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	resource.SampleDesc.Count = 1;
	resource.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resource.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	D3D12_CLEAR_VALUE clear = {};
	clear.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	clear.DepthStencil.Depth = 1.0f;
	clear.DepthStencil.Stencil = 0;
	if (!CheckHr(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &resource,
		D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&m_depthBuffer)),
		"Create depth buffer"))
		return false;
	m_device->CreateDepthStencilView(m_depthBuffer.Get(), nullptr,
		m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
	return true;
}

bool NativeD3D12Renderer::CreateBasicPipeline(UINT colorOffset, UINT normalOffset, UINT specularOffset)
{
	CpuTimer timer(m_profiling ? &m_cpuMilliseconds[3] : nullptr);
	PipelineKey key = GetPipelineKey(false);
	key[20] = colorOffset;
	key[27] = normalOffset;
	key[28] = specularOffset;
	const auto cached = m_pipelineCache.find(key);
	if (cached != m_pipelineCache.end()) { m_basicPipeline = cached->second; return true; }
	static const std::string shaderSource = std::string(NativeLightingHlsl) + R"(
cbuffer Transform : register(b0) { row_major float4x4 worldViewProjection; uint4 vertexFlags; row_major float4x4 textureMatrices[4]; uint4 textureFlags; float4 treeSway[11]; row_major float4x4 worldView; float4 fogParameters; float4 fogColor; LightingState lighting; };
cbuffer AlphaTest : register(b1) { uint alphaTestEnable; uint alphaTestFunction; uint alphaTestReference; uint alphaTestPadding; uint grayscaleEnabled; uint textureColorTexture; uint textureColorVertex; uint textureAlphaTexture; uint textureAlphaVertex; };
struct VSInput { float3 position : POSITION; float4 color : COLOR0; float3 normal : NORMAL; float4 secondary : COLOR1; };
struct VSOutput { float4 position : SV_POSITION; float4 color : COLOR0; float4 fog : COLOR1; float4 specular : COLOR2; };
VSOutput mainVS(VSInput input)
{
    VSOutput output;
    output.position = mul(float4(input.position, 1.0f), worldViewProjection);
    output.color = vertexFlags.x != 0 ? input.color.bgra : float4(1,1,1,1);
    float3 eye = mul(float4(input.position,1),worldView).xyz;
    LightVertex(lighting,eye,input.normal,output.color,
        lighting.parameters.y != 0 ? input.secondary.bgra : float4(0,0,0,0),output.color,output.specular);
    float distance = vertexFlags.w != 0 ? length(eye) : abs(eye.z);
    float d = distance * fogParameters.z;
    float factor = vertexFlags.z == 1 ? exp(-d) : (vertexFlags.z == 2 ? exp(-d*d) :
        (vertexFlags.z == 3 ? (fogParameters.y-distance)/max(0.00001,fogParameters.y-fogParameters.x) : 1));
    output.fog = float4(fogColor.rgb,saturate(factor));
    return output;
}

float4 mainPS(VSOutput input) : SV_TARGET
{
    if (alphaTestEnable != 0)
    {
        float alpha = input.color.a;
        float reference = (float)alphaTestReference / 255.0f;
		bool alphaPass = false;
		if (alphaTestFunction == 1) alphaPass = false;
		else if (alphaTestFunction == 2) alphaPass = alpha < reference;
		else if (alphaTestFunction == 3) alphaPass = alpha == reference;
		else if (alphaTestFunction == 4) alphaPass = alpha <= reference;
		else if (alphaTestFunction == 5) alphaPass = alpha > reference;
		else if (alphaTestFunction == 6) alphaPass = alpha != reference;
		else if (alphaTestFunction == 7) alphaPass = alpha >= reference;
		else alphaPass = true;
		if (!alphaPass) discard;
    }
    float4 color = input.color;
    color.rgb = lerp(input.fog.rgb,saturate(color.rgb+input.specular.rgb),input.fog.a);
    if (grayscaleEnabled != 0)
    {
        color.rgb = dot(color.rgb, float3(0.299f, 0.587f, 0.114f)).xxx;
    }
    return color;
}
)";

	auto& vertexShader = m_shaderCache[0];
	auto& pixelShader = m_shaderCache[1];
	HRESULT hr = S_OK;
	if (!vertexShader || !pixelShader) {
	ComPtr<ID3DBlob> errors;
	UINT compileFlags = 0;
#if defined(_DEBUG)
	compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
	hr = D3DCompile(shaderSource.data(), shaderSource.size(), "native_d3d12_basic.hlsl",
		nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "mainVS", "vs_5_0", compileFlags, 0,
		&vertexShader, &errors);
	if (FAILED(hr))
	{
		if (errors != nullptr)
			OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
		return CheckHr(hr, "Compile native D3D12 vertex shader");
	}
	hr = D3DCompile(shaderSource.data(), shaderSource.size(), "native_d3d12_basic.hlsl",
		nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "mainPS", "ps_5_0", compileFlags, 0,
		&pixelShader, &errors);
	if (FAILED(hr))
	{
		if (errors != nullptr)
			OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
		return CheckHr(hr, "Compile native D3D12 pixel shader");
	}

	}

	if (m_rootSignature == nullptr)
	{
		D3D12_DESCRIPTOR_RANGE textureRange = {};
		textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		textureRange.NumDescriptors = 1;
		textureRange.BaseShaderRegister = 0;
		textureRange.OffsetInDescriptorsFromTableStart = 0;
		D3D12_DESCRIPTOR_RANGE samplerRange = {};
		samplerRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
		samplerRange.NumDescriptors = 1;
		samplerRange.BaseShaderRegister = 0;
		samplerRange.OffsetInDescriptorsFromTableStart = 0;
		D3D12_ROOT_PARAMETER rootParameters[11] = {};
		D3D12_DESCRIPTOR_RANGE extraRanges[6] = {};
		rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		rootParameters[0].Descriptor.ShaderRegister = 0;
		rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
		rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
		rootParameters[1].DescriptorTable.pDescriptorRanges = &textureRange;
		rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		rootParameters[2].Constants.ShaderRegister = 1;
		rootParameters[2].Constants.Num32BitValues = 9;
		rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[3].DescriptorTable.NumDescriptorRanges = 1;
		rootParameters[3].DescriptorTable.pDescriptorRanges = &samplerRange;
		rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		for (UINT stage=1; stage<4; ++stage) {
			for (UINT kind=0; kind<2; ++kind) {
				const UINT i=(stage-1)*2+kind, root=4+i;
				extraRanges[i] = kind ? samplerRange : textureRange;
				extraRanges[i].BaseShaderRegister = stage;
				rootParameters[root] = rootParameters[kind ? 3 : 1];
				rootParameters[root].DescriptorTable.pDescriptorRanges = &extraRanges[i];
			}
		}
		rootParameters[10].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		rootParameters[10].Descriptor.ShaderRegister = 2;
		rootParameters[10].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		D3D12_ROOT_SIGNATURE_DESC rootDescription = {};
		rootDescription.NumParameters = 11;
		rootDescription.pParameters = rootParameters;
		rootDescription.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
		ComPtr<ID3DBlob> serializedRoot;
		ComPtr<ID3DBlob> rootErrors;
		hr = D3D12SerializeRootSignature(&rootDescription, D3D_ROOT_SIGNATURE_VERSION_1,
			&serializedRoot, &rootErrors);
		if (FAILED(hr))
			return CheckHr(hr, "Serialize native D3D12 root signature");
		if (!CheckHr(m_device->CreateRootSignature(0, serializedRoot->GetBufferPointer(),
			serializedRoot->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)),
			"Create native D3D12 root signature"))
			return false;
	}

	D3D12_INPUT_ELEMENT_DESC inputs[4] = {};
	inputs[0].SemanticName = "POSITION";
	inputs[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputs[0].InputSlot = 0;
	inputs[0].AlignedByteOffset = 0;
	inputs[0].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
	inputs[1].SemanticName = "COLOR";
	inputs[1].Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	inputs[1].InputSlot = 0;
	inputs[1].AlignedByteOffset = colorOffset == UINT_MAX ? 0 : colorOffset;
	inputs[1].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
	inputs[2] = inputs[0];
	inputs[2].SemanticName = "NORMAL";
	inputs[2].AlignedByteOffset = normalOffset == UINT_MAX ? 0 : normalOffset;
	inputs[3] = inputs[1];
	inputs[3].SemanticIndex = 1;
	inputs[3].AlignedByteOffset = specularOffset == UINT_MAX ? 0 : specularOffset;

	D3D12_RASTERIZER_DESC rasterizer = {};
	rasterizer.FillMode = D3D12_FILL_MODE_SOLID;
	rasterizer.CullMode = m_cullMode;
	rasterizer.DepthClipEnable = TRUE;
	D3D12_BLEND_DESC blend = {};
	blend.RenderTarget[0].BlendEnable = m_blendEnable ? TRUE : FALSE;
	blend.RenderTarget[0].SrcBlend = m_sourceBlend;
	blend.RenderTarget[0].DestBlend = m_destinationBlend;
	blend.RenderTarget[0].BlendOp = m_blendOp;
	blend.RenderTarget[0].SrcBlendAlpha = AlphaBlendFactor(m_sourceBlend);
	blend.RenderTarget[0].DestBlendAlpha = AlphaBlendFactor(m_destinationBlend);
	blend.RenderTarget[0].BlendOpAlpha = m_blendOp;
	blend.RenderTarget[0].RenderTargetWriteMask = m_renderTargetWriteMask;
	D3D12_DEPTH_STENCIL_DESC depth = {};
	depth.DepthEnable = (m_useDefaultDepth && m_depthEnable) ? TRUE : FALSE;
	depth.DepthWriteMask = m_depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
	depth.DepthFunc = m_depthFunc;
	depth.StencilEnable = (m_useDefaultDepth && m_stencilEnable) ? TRUE : FALSE;
	depth.StencilReadMask = m_stencilReadMask;
	depth.StencilWriteMask = m_stencilWriteMask;
	depth.FrontFace.StencilFunc = m_stencilFunc;
	depth.FrontFace.StencilFailOp = m_stencilFail;
	depth.FrontFace.StencilDepthFailOp = m_stencilDepthFail;
	depth.FrontFace.StencilPassOp = m_stencilPass;
	depth.BackFace = depth.FrontFace;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC description = {};
	description.pRootSignature = m_rootSignature.Get();
	description.VS = {vertexShader->GetBufferPointer(), vertexShader->GetBufferSize()};
	description.PS = {pixelShader->GetBufferPointer(), pixelShader->GetBufferSize()};
	description.BlendState = blend;
	description.SampleMask = UINT_MAX;
	description.RasterizerState = rasterizer;
	description.DepthStencilState = depth;
	description.InputLayout = {inputs, 4};
	description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	description.NumRenderTargets = 1;
	description.RTVFormats[0] = m_targetFormat;
	description.DSVFormat = m_useDefaultDepth ? DXGI_FORMAT_D24_UNORM_S8_UINT : DXGI_FORMAT_UNKNOWN;
	description.SampleDesc.Count = 1;
	if (!CheckHr(m_device->CreateGraphicsPipelineState(&description,
		IID_PPV_ARGS(&m_basicPipeline)), "Create native D3D12 basic pipeline"))
		return false;
	m_basicColorOffset = colorOffset;
	m_pipelineCache.emplace(key, m_basicPipeline);
	return true;
}

UINT NativeD3D12Renderer::TextureMipCount(UINT width, UINT height, UINT requested)
{
	if (!width || !height) return 0;
	UINT count = 1;
	while (width > 1 || height > 1) {
		width = (std::max)(1u, width / 2);
		height = (std::max)(1u, height / 2);
		++count;
	}
	return requested ? (std::min)(count, requested) : count;
}

bool NativeD3D12Renderer::UploadBgraTexture(NativeD3D12Texture& texture,
	const NativeD3D12TextureLevel& base, bool opaque)
{
	if (!texture.IsValid() || texture.Format() != DXGI_FORMAT_B8G8R8A8_UNORM ||
		!base.data || texture.Width() > UINT_MAX / 4) return false;
	UINT width = texture.Width(), height = texture.Height();
	if (base.rowPitch < width * 4 ||
		UINT64(base.rowPitch) * (height - 1) + width * 4 > base.slicePitch) return false;
	const UINT count = texture.MipLevels();
	if (!count || count > TextureMipCount(width,height)) return false;
	if (count == 1 && !opaque) return UploadTexture2D(texture,&base,1);
	std::vector<std::vector<unsigned char>> pixels(count);
	std::vector<NativeD3D12TextureLevel> levels(count);
	pixels[0].resize(size_t(width) * height * 4);
	for (UINT y = 0; y < height; ++y)
		std::memcpy(pixels[0].data() + size_t(y)*width*4,
			static_cast<const unsigned char*>(base.data) + size_t(y)*base.rowPitch, width*4);
	if (opaque)
		for (size_t i = 3; i < pixels[0].size(); i += 4) pixels[0][i] = 255;
	levels[0] = {pixels[0].data(),width*4,width*height*4};
	for (UINT level = 1; level < count; ++level) {
		const UINT nextWidth = (std::max)(1u,width/2), nextHeight = (std::max)(1u,height/2);
		pixels[level].resize(size_t(nextWidth)*nextHeight*4);
		// Proportional boxes include the final row/column of odd-sized images.
		// Generate only on a texture revision, never as per-frame sampling work.
		for (UINT y = 0; y < nextHeight; ++y) for (UINT x = 0; x < nextWidth; ++x) {
			const UINT x0 = x*width/nextWidth, x1 = (x+1)*width/nextWidth;
			const UINT y0 = y*height/nextHeight, y1 = (y+1)*height/nextHeight;
			const UINT samples = (x1-x0)*(y1-y0);
			UINT sum[4] = {};
			for (UINT sy = y0; sy < y1; ++sy) for (UINT sx = x0; sx < x1; ++sx)
				for (UINT c = 0; c < 4; ++c)
					sum[c] += pixels[level-1][(size_t(sy)*width+sx)*4+c];
			for (UINT c = 0; c < 4; ++c)
				pixels[level][(size_t(y)*nextWidth+x)*4+c] = static_cast<unsigned char>((sum[c]+samples/2)/samples);
		}
		width = nextWidth; height = nextHeight;
		levels[level] = {pixels[level].data(),width*4,width*height*4};
	}
	return UploadTexture2D(texture,levels.data(),count);
}

bool NativeD3D12Renderer::CreateTexturedPipeline(const std::array<UINT,4>& texcoordOffsets, UINT colorOffset,
	UINT normalOffset, UINT specularOffset)
{
	CpuTimer timer(m_profiling ? &m_cpuMilliseconds[3] : nullptr);
	PipelineKey key = GetPipelineKey(true);
	UINT stageCount = 0;
	while (stageCount < 4 && m_materialStages[stageCount].colorOp != NativeMaterialOp::Disable) ++stageCount;
	const UINT materialVariant = m_materialEnabled ? stageCount+1 : 0;
	key[25] = materialVariant;
	key[20] = colorOffset;
	for (UINT stage=0;stage<4;++stage) key[21+stage] = texcoordOffsets[stage];
	key[26] = m_treeSwayOffset;
	key[27] = normalOffset;
	key[28] = specularOffset;
	const auto cached = m_pipelineCache.find(key);
	if (cached != m_pipelineCache.end()) { m_texturedPipeline = cached->second; return true; }
	static const std::string shaderSource = std::string(NativeLightingHlsl) + R"(
cbuffer Transform : register(b0) { row_major float4x4 worldViewProjection; uint4 vertexFlags; row_major float4x4 textureMatrices[4]; uint4 textureFlags; float4 treeSway[11]; row_major float4x4 worldView; float4 fogParameters; float4 fogColor; LightingState lighting; };
cbuffer AlphaTest : register(b1) { uint alphaTestEnable; uint alphaTestFunction; uint alphaTestReference; uint alphaTestPadding; uint grayscaleEnabled; uint textureColorTexture; uint textureColorVertex; uint textureAlphaTexture; uint textureAlphaVertex; };
struct MaterialStage { uint colorOp; uint colorArg1; uint colorArg2; uint colorArg0; uint alphaOp; uint alphaArg1; uint alphaArg2; uint alphaArg0; uint4 resultFlags; };
cbuffer Material : register(b2) { MaterialStage stages[4]; float4 materialFactor; };
Texture2D texture0 : register(t0);
Texture2D texture1 : register(t1);
Texture2D texture2 : register(t2);
Texture2D texture3 : register(t3);
SamplerState sampler1 : register(s1);
SamplerState sampler2 : register(s2);
SamplerState sampler3 : register(s3);
SamplerState sampler0 : register(s0);
struct VSInput { float3 position : POSITION; float4 color : COLOR0; float2 texcoord : TEXCOORD0; float2 uv1 : TEXCOORD1; float2 uv2 : TEXCOORD2; float2 uv3 : TEXCOORD3; float3 tree : TEXCOORD4; float3 normal : NORMAL; float4 secondary : COLOR1; };
struct VSOutput { float4 position : SV_POSITION; float4 color : COLOR0; float2 texcoord : TEXCOORD0; float2 uv1 : TEXCOORD1; float2 uv2 : TEXCOORD2; float2 uv3 : TEXCOORD3; float4 fog : COLOR1; float4 specular : COLOR2; };
float2 VertexUV(uint stage, float2 uv, float3 position) {
    uint flags = textureFlags[stage];
    float4 coordinate = (flags & 1) != 0 ? float4(position,1) : float4((flags & 8) != 0 ? uv : float2(0,0),1,0);
    if ((flags & 2) != 0) {
        coordinate = mul(coordinate,textureMatrices[stage]);
        float divisor = (flags & 4) != 0 && coordinate.z != 0 ? coordinate.z : 1;
        return coordinate.xy/divisor;
    }
    return coordinate.xy;
}
VSOutput mainVS(VSInput input)
{
    VSOutput output;
    output.position = mul(float4(input.position, 1.0f), worldViewProjection);
    output.color = vertexFlags.x != 0 ? input.color.bgra : float4(1,1,1,1);
    if (vertexFlags.y != 0) {
        uint swayIndex = input.tree.x >= 0 && input.tree.x < 11 ? (uint)input.tree.x : 0;
        input.position += treeSway[swayIndex].xyz * max(0,input.position.z-input.tree.z);
        output.position = mul(float4(input.position,1),worldViewProjection);
        output.color.rgb *= saturate(input.tree.y);
    }
    output.texcoord = VertexUV(0,input.texcoord,input.position);
    output.uv1 = VertexUV(1,input.uv1,input.position);
    output.uv2 = VertexUV(2,input.uv2,input.position);
    output.uv3 = VertexUV(3,input.uv3,input.position);
    float3 eye = mul(float4(input.position,1),worldView).xyz;
    LightVertex(lighting,eye,input.normal,output.color,
        lighting.parameters.y != 0 ? input.secondary.bgra : float4(0,0,0,0),output.color,output.specular);
    float distance = vertexFlags.w != 0 ? length(eye) : abs(eye.z);
    float d = distance * fogParameters.z;
    float factor = vertexFlags.z == 1 ? exp(-d) : (vertexFlags.z == 2 ? exp(-d*d) :
        (vertexFlags.z == 3 ? (fogParameters.y-distance)/max(0.00001,fogParameters.y-fogParameters.x) : 1));
    output.fog = float4(fogColor.rgb,saturate(factor));
    return output;
}

float4 Argument(uint selection, float4 diffuse, float4 current, float4 sampled, float4 temporary, float4 specular) {
    uint source = selection & 15;
    float4 value = source == 0 ? diffuse : (source == 1 ? current : (source == 2 ? sampled : (source == 3 ? materialFactor : (source == 5 ? temporary : (source == 4 ? specular : float4(0,0,0,0))))));
    if ((selection & 32) != 0) value = value.aaaa;
    if ((selection & 16) != 0) value = 1-value;
    return value;
}
float4 Combine(uint op, float4 a, float4 b, float4 c, float4 diffuse, float4 current, float4 sampled) {

    if (op == 1) return saturate(a);
    if (op == 2) return saturate(b);
    if (op == 3) return saturate(a*b);
    if (op == 4) return saturate(2*a*b);
    if (op == 5) return saturate(4*a*b);
    if (op == 6) return saturate(a+b);
    if (op == 7) return saturate(a+b-0.5);
    if (op == 8) return saturate(2*(a+b-0.5));
    if (op == 9) return saturate(a-b);
    if (op == 10) return saturate(a+b*(1-a));
    if (op == 11) return saturate(lerp(b,a,diffuse.a));
    if (op == 12) return saturate(lerp(b,a,sampled.a));
    if (op == 13) return saturate(lerp(b,a,materialFactor.a));
    if (op == 14) return saturate(lerp(b,a,current.a));
    if (op == 15) return saturate(a+b*(1-sampled.a));
    if (op == 16) return saturate(a+b*a.a);
    if (op == 17) return saturate(a*b+a.a);
    if (op == 18) return saturate(a+b*(1-a.a));
    if (op == 19) return saturate((1-a)*b+a.a);
    if (op == 20) return saturate(dot(a.rgb*2-1,b.rgb*2-1).xxxx);
    if (op == 21) return saturate(a*b+c);
    if (op == 22) return saturate(lerp(b,a,c));
    return current;
}
float4 mainPS(VSOutput input) : SV_TARGET
{
	// Variant zero is a direct sprite. Material variants contain only the
	// active texture stages, so a one-texture UI draw never samples four maps.
#if MATERIAL_VARIANT == 0
	float4 sampled = texture0.Sample(sampler0, input.texcoord);
	float4 color = float4(1.0f, 1.0f, 1.0f, 1.0f);
	if (textureColorTexture != 0) color.rgb *= sampled.rgb;
	if (textureColorVertex != 0) color.rgb *= input.color.rgb;
	if (textureAlphaTexture != 0) color.a *= sampled.a;
	if (textureAlphaVertex != 0) color.a *= input.color.a;
#else
    float4 color = input.color;
#if MATERIAL_VARIANT > 1
        float4 samples[MATERIAL_VARIANT-1];
        samples[0] = texture0.Sample(sampler0,input.texcoord);
#if MATERIAL_VARIANT > 2
        samples[1] = texture1.Sample(sampler1,input.uv1);
#endif
#if MATERIAL_VARIANT > 3
        samples[2] = texture2.Sample(sampler2,input.uv2);
#endif
#if MATERIAL_VARIANT > 4
        samples[3] = texture3.Sample(sampler3,input.uv3);
#endif
        float4 temporary = 0;
        [unroll] for (uint stage = 0; stage < MATERIAL_VARIANT-1; ++stage) {
            MaterialStage settings = stages[stage];
            float4 a = Argument(settings.colorArg1,input.color,color,samples[stage],temporary,input.specular);
            float4 b = Argument(settings.colorArg2,input.color,color,samples[stage],temporary,input.specular);
            float4 c = Argument(settings.colorArg0,input.color,color,samples[stage],temporary,input.specular);
            float3 rgb = Combine(settings.colorOp,a,b,c,input.color,color,samples[stage]).rgb;
            a = Argument(settings.alphaArg1,input.color,color,samples[stage],temporary,input.specular);
            b = Argument(settings.alphaArg2,input.color,color,samples[stage],temporary,input.specular);
            c = Argument(settings.alphaArg0,input.color,color,samples[stage],temporary,input.specular);
            float alpha = Combine(settings.alphaOp,a,b,c,input.color,color,samples[stage]).a;
            if (settings.resultFlags.x != 0) temporary = float4(rgb,alpha);
            else color = float4(rgb,alpha);
        }
#endif
#endif
    color.rgb = lerp(input.fog.rgb,saturate(color.rgb+input.specular.rgb),input.fog.a);
    if (grayscaleEnabled != 0)
    {
        color.rgb = dot(color.rgb, float3(0.299f, 0.587f, 0.114f)).xxx;
    }
    if (alphaTestEnable != 0)
    {
        float reference = (float)alphaTestReference / 255.0f;
		bool alphaPass = false;
		if (alphaTestFunction == 1) alphaPass = false;
		else if (alphaTestFunction == 2) alphaPass = color.a < reference;
		else if (alphaTestFunction == 3) alphaPass = color.a == reference;
		else if (alphaTestFunction == 4) alphaPass = color.a <= reference;
		else if (alphaTestFunction == 5) alphaPass = color.a > reference;
		else if (alphaTestFunction == 6) alphaPass = color.a != reference;
		else if (alphaTestFunction == 7) alphaPass = color.a >= reference;
		else alphaPass = true;
		if (!alphaPass) discard;
    }
    return color;
}
)";

	auto& vertexShader = m_shaderCache[2];
	auto& pixelShader = m_materialPixelShaders[materialVariant];
	HRESULT hr = S_OK;
	if (!vertexShader || !pixelShader) {
	ComPtr<ID3DBlob> errors;
	UINT compileFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#if defined(_DEBUG)
	compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
	char variantValue[2] = {static_cast<char>('0'+materialVariant),0};
	const D3D_SHADER_MACRO defines[] = {{"MATERIAL_VARIANT",variantValue},{nullptr,nullptr}};
	if (!vertexShader) {
	hr = D3DCompile(shaderSource.data(), shaderSource.size(),
		"native_d3d12_textured.hlsl", defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
		"mainVS", "vs_5_0", compileFlags, 0, &vertexShader, &errors);
	if (FAILED(hr))
		return CheckHr(hr, "Compile native D3D12 textured vertex shader");
	}
	if (!pixelShader) {
	hr = D3DCompile(shaderSource.data(), shaderSource.size(),
		"native_d3d12_textured.hlsl", defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
		"mainPS", "ps_5_0", compileFlags, 0, &pixelShader, &errors);
	if (FAILED(hr))
		return CheckHr(hr, "Compile native D3D12 textured pixel shader");
	}

	}

	D3D12_INPUT_ELEMENT_DESC inputs[9] = {};
	inputs[0].SemanticName = "POSITION";
	inputs[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputs[0].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
	inputs[1].SemanticName = "COLOR";
	inputs[1].Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	inputs[1].AlignedByteOffset = colorOffset == UINT_MAX ? 0 : colorOffset;
	inputs[1].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
	inputs[2].SemanticName = "TEXCOORD";
	inputs[2].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputs[2].AlignedByteOffset = texcoordOffsets[0];
	inputs[2].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
	for (UINT stage = 1; stage < 4; ++stage) {
		inputs[2+stage] = inputs[2];
		inputs[2+stage].SemanticIndex = stage;
		inputs[2+stage].AlignedByteOffset = texcoordOffsets[stage];
	}
	inputs[6] = inputs[0];
	inputs[6].SemanticName = "TEXCOORD";
	inputs[6].SemanticIndex = 4;
	inputs[6].AlignedByteOffset = m_treeSwayOffset == UINT_MAX ? 0 : m_treeSwayOffset;
	inputs[7] = inputs[0];
	inputs[7].SemanticName = "NORMAL";
	inputs[7].AlignedByteOffset = normalOffset == UINT_MAX ? 0 : normalOffset;
	inputs[8] = inputs[1];
	inputs[8].SemanticIndex = 1;
	inputs[8].AlignedByteOffset = specularOffset == UINT_MAX ? 0 : specularOffset;
	D3D12_RASTERIZER_DESC rasterizer = {};
	rasterizer.FillMode = D3D12_FILL_MODE_SOLID;
	rasterizer.CullMode = m_cullMode;
	rasterizer.DepthClipEnable = TRUE;
	D3D12_BLEND_DESC blend = {};
	blend.RenderTarget[0].BlendEnable = m_blendEnable ? TRUE : FALSE;
	blend.RenderTarget[0].SrcBlend = m_sourceBlend;
	blend.RenderTarget[0].DestBlend = m_destinationBlend;
	blend.RenderTarget[0].BlendOp = m_blendOp;
	blend.RenderTarget[0].SrcBlendAlpha = AlphaBlendFactor(m_sourceBlend);
	blend.RenderTarget[0].DestBlendAlpha = AlphaBlendFactor(m_destinationBlend);
	blend.RenderTarget[0].BlendOpAlpha = m_blendOp;
	blend.RenderTarget[0].RenderTargetWriteMask = m_renderTargetWriteMask;
	D3D12_DEPTH_STENCIL_DESC depth = {};
	depth.DepthEnable = (m_useDefaultDepth && m_depthEnable) ? TRUE : FALSE;
	depth.DepthWriteMask = m_depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
	depth.DepthFunc = m_depthFunc;
	depth.StencilEnable = (m_useDefaultDepth && m_stencilEnable) ? TRUE : FALSE;
	depth.StencilReadMask = m_stencilReadMask;
	depth.StencilWriteMask = m_stencilWriteMask;
	depth.FrontFace.StencilFunc = m_stencilFunc;
	depth.FrontFace.StencilFailOp = m_stencilFail;
	depth.FrontFace.StencilDepthFailOp = m_stencilDepthFail;
	depth.FrontFace.StencilPassOp = m_stencilPass;
	depth.BackFace = depth.FrontFace;
	D3D12_GRAPHICS_PIPELINE_STATE_DESC description = {};
	description.pRootSignature = m_rootSignature.Get();
	description.VS = {vertexShader->GetBufferPointer(), vertexShader->GetBufferSize()};
	description.PS = {pixelShader->GetBufferPointer(), pixelShader->GetBufferSize()};
	description.BlendState = blend;
	description.SampleMask = UINT_MAX;
	description.RasterizerState = rasterizer;
	description.DepthStencilState = depth;
	description.InputLayout = {inputs, 9};
	description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	description.NumRenderTargets = 1;
	description.RTVFormats[0] = m_targetFormat;
	description.DSVFormat = m_useDefaultDepth ? DXGI_FORMAT_D24_UNORM_S8_UINT : DXGI_FORMAT_UNKNOWN;
	description.SampleDesc.Count = 1;
	if (!CheckHr(m_device->CreateGraphicsPipelineState(&description,
		IID_PPV_ARGS(&m_texturedPipeline)), "Create native D3D12 textured pipeline"))
		return false;
	m_texturedTexcoordOffset = texcoordOffsets[0];
	m_texturedColorOffset = colorOffset;
	m_pipelineCache.emplace(key, m_texturedPipeline);
	return true;
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

void NativeD3D12Renderer::SetGrayscale(bool enable)
{
	m_grayscale = enable;
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
}

void NativeD3D12Renderer::SetLighting(const NativeLightingState& state)
{
	m_lighting = state;
	if (!state.flags[0]) return;
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

NativeD3D12Texture* NativeD3D12Renderer::CreateTexture2D(UINT width, UINT height,
	UINT mipLevels, DXGI_FORMAT format, bool renderTarget)
{
	if (!IsInitialized() || width == 0 || height == 0 || mipLevels == 0 ||
		format == DXGI_FORMAT_UNKNOWN)
		return nullptr;

	D3D12_RESOURCE_DESC description = {};
	description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	description.Width = width;
	description.Height = height;
	description.DepthOrArraySize = 1;
	description.MipLevels = static_cast<UINT16>(mipLevels);
	description.Format = format;
	description.SampleDesc.Count = 1;
	description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	description.Flags = renderTarget ? D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET : D3D12_RESOURCE_FLAG_NONE;

	D3D12_HEAP_PROPERTIES heap = {};
	heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_CLEAR_VALUE clear = {};
	clear.Format = format;
	clear.Color[3] = 1.0f;
	Microsoft::WRL::ComPtr<ID3D12Resource> resource;
	const D3D12_RESOURCE_STATES initialState = renderTarget ?
		D3D12_RESOURCE_STATE_RENDER_TARGET : D3D12_RESOURCE_STATE_COPY_DEST;
	if (!CheckHr(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
		&description, initialState, renderTarget ? &clear : nullptr,
		IID_PPV_ARGS(&resource)), "Create native D3D12 texture"))
		return nullptr;

	if ((m_descriptorPool->freeSrv.empty() && m_descriptorPool->nextSrv >= 4096) ||
		(renderTarget && m_descriptorPool->freeRtv.empty() && m_descriptorPool->nextRtv >= 2048))
		return nullptr;
	const UINT descriptorIndex = m_descriptorPool->freeSrv.empty() ?
		m_descriptorPool->nextSrv++ : m_descriptorPool->freeSrv.back();
	if (!m_descriptorPool->freeSrv.empty()) m_descriptorPool->freeSrv.pop_back();
	D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
	cpu.ptr += static_cast<SIZE_T>(descriptorIndex) * m_srvDescriptorSize;
	D3D12_GPU_DESCRIPTOR_HANDLE gpu = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
	gpu.ptr += static_cast<UINT64>(descriptorIndex) * m_srvDescriptorSize;
	D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
	srv.Format = format;
	srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.Texture2D.MipLevels = mipLevels;
	m_device->CreateShaderResourceView(resource.Get(), &srv, cpu);

	NativeD3D12Texture* texture = new NativeD3D12Texture();
	texture->m_storage->pool = m_descriptorPool;
	texture->m_storage->srvIndex = descriptorIndex;
	texture->m_storage->m_resource = resource;
	texture->m_storage->m_srvCpu = cpu;
	texture->m_storage->m_srvGpu = gpu;
	if (renderTarget)
	{
		const UINT rtvIndex = m_descriptorPool->freeRtv.empty() ?
			m_descriptorPool->nextRtv++ : m_descriptorPool->freeRtv.back();
		if (!m_descriptorPool->freeRtv.empty()) m_descriptorPool->freeRtv.pop_back();
		texture->m_storage->rtvIndex = rtvIndex;
		D3D12_CPU_DESCRIPTOR_HANDLE rtvCpu = m_textureRtvHeap->GetCPUDescriptorHandleForHeapStart();
		rtvCpu.ptr += static_cast<SIZE_T>(rtvIndex) * m_rtvTextureDescriptorSize;
		m_device->CreateRenderTargetView(resource.Get(), nullptr, rtvCpu);
		texture->m_storage->m_rtvCpu = rtvCpu;
		texture->m_storage->m_hasRtv = true;
	}
	texture->m_storage->m_width = width;
	texture->m_storage->m_height = height;
	texture->m_storage->m_mipLevels = mipLevels;
	texture->m_storage->m_format = format;
	texture->m_storage->m_state = initialState;
	return texture;
}

bool NativeD3D12Renderer::SetRenderTarget(const NativeD3D12Texture* texture,
	bool useDefaultDepth)
{
	if (!IsInitialized() || m_commandList == nullptr || !m_recording)
		return false;
	if (texture != nullptr && (!texture->HasRenderTargetView() || !RetainTexture(texture)))
		return false;

	if (m_currentRenderTarget != nullptr && (texture == nullptr || m_currentRenderTarget->m_storage != texture->m_storage))
	{
		const D3D12_RESOURCE_BARRIER barrier = Transition(
			m_currentRenderTarget->Resource(), D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		m_commandList->ResourceBarrier(1, &barrier);
		m_currentRenderTarget->m_storage->m_state =
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	}
	if (texture != nullptr && (m_currentRenderTarget == nullptr || m_currentRenderTarget->m_storage != texture->m_storage))
	{
		if (texture->m_storage->m_state != D3D12_RESOURCE_STATE_RENDER_TARGET)
		{
			const D3D12_RESOURCE_BARRIER barrier = Transition(
				texture->Resource(), texture->m_storage->m_state, D3D12_RESOURCE_STATE_RENDER_TARGET);
			m_commandList->ResourceBarrier(1, &barrier);
		}
		texture->m_storage->m_state = D3D12_RESOURCE_STATE_RENDER_TARGET;
	}

	const D3D12_CPU_DESCRIPTOR_HANDLE rtv = texture != nullptr ?
		texture->RtvCpuHandle() : CurrentRenderTarget();
	const D3D12_CPU_DESCRIPTOR_HANDLE dsv = DepthStencil();
	m_commandList->OMSetRenderTargets(1, &rtv, FALSE, useDefaultDepth ? &dsv : nullptr);
	m_currentRenderTarget.reset();
	if (texture) {
		m_currentRenderTarget = std::make_shared<NativeD3D12Texture>();
		m_currentRenderTarget->m_storage = texture->m_storage;
	}
	m_targetFormat = texture ? texture->Format() : DXGI_FORMAT_R8G8B8A8_UNORM;
	m_useDefaultDepth = useDefaultDepth;
	return true;
}

bool NativeD3D12Renderer::UploadTexture2D(NativeD3D12Texture& texture,
	const NativeD3D12TextureLevel* levels, UINT levelCount)
{
	CpuTimer timer(m_profiling ? &m_cpuMilliseconds[2] : nullptr);
	if (!IsInitialized() || !texture.IsValid() || texture.m_storage->pool != m_descriptorPool || levels == nullptr ||
		levelCount == 0 || levelCount > texture.MipLevels())
		return false;

	const D3D12_RESOURCE_DESC description = texture.Resource()->GetDesc();
	std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(levelCount);
	std::vector<UINT> rows(levelCount);
	std::vector<UINT64> rowSizes(levelCount);
	UINT64 uploadBytes = 0;
	m_device->GetCopyableFootprints(&description, 0, levelCount, 0,
		footprints.data(), rows.data(), rowSizes.data(), &uploadBytes);
	if (uploadBytes == 0)
		return false;
	for (UINT level = 0; level < levelCount; ++level)
	{
		if (rows[level] == 0 || levels[level].data == nullptr ||
			levels[level].rowPitch < rowSizes[level] ||
			(levels[level].slicePitch != 0 && static_cast<UINT64>(rows[level] - 1) *
			levels[level].rowPitch + rowSizes[level] > levels[level].slicePitch))
			return false;
		const UINT64 footprintEnd = footprints[level].Offset +
			static_cast<UINT64>(footprints[level].Footprint.RowPitch) *
			(rows[level] - 1) + rowSizes[level];
		if (footprints[level].Offset >= uploadBytes || footprintEnd > uploadBytes)
			return false;
	}

	D3D12_HEAP_PROPERTIES heap = {};
	heap.Type = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC uploadDescription = {};
	uploadDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	uploadDescription.Width = uploadBytes;
	uploadDescription.Height = 1;
	uploadDescription.DepthOrArraySize = 1;
	uploadDescription.MipLevels = 1;
	uploadDescription.SampleDesc.Count = 1;
	uploadDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	Microsoft::WRL::ComPtr<ID3D12Resource> upload;
	unsigned char* mapped = nullptr;
	UINT64 frameOffset = 0;
	if (m_recording) {
		ID3D12Resource* page = nullptr;
		if (uploadBytes > UINT_MAX || !AllocateFrameUpload(static_cast<UINT>(uploadBytes),
			D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT,page,frameOffset,mapped)) return false;
		upload = page;
	} else {
		if (!CheckHr(m_device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,
			&uploadDescription,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,
			IID_PPV_ARGS(&upload)),"Create texture upload buffer")) return false;
		D3D12_RANGE readRange = {0,0};
		if (FAILED(upload->Map(0,&readRange,reinterpret_cast<void**>(&mapped)))) return false;
	}
	for (UINT level = 0; level < levelCount; ++level)
	{
		if (levels[level].data == nullptr || levels[level].rowPitch == 0)
		{
			if (!m_recording) upload->Unmap(0, nullptr);
			return false;
		}
		const unsigned char* source = static_cast<const unsigned char*>(levels[level].data);
		unsigned char* destination = mapped + footprints[level].Offset;
		const UINT sourcePitch = levels[level].rowPitch;
		const UINT destinationPitch = footprints[level].Footprint.RowPitch;
		const UINT copyPitch = static_cast<UINT>(rowSizes[level]);
		if (levels[level].slicePitch != 0 &&
			(static_cast<UINT64>(rows[level] - 1) * sourcePitch + copyPitch >
				levels[level].slicePitch))
		{
			if (!m_recording) upload->Unmap(0, nullptr);
			return false;
		}
		for (UINT row = 0; row < rows[level]; ++row)
			std::memcpy(destination + static_cast<size_t>(row) * destinationPitch,
				source + static_cast<size_t>(row) * sourcePitch, copyPitch);
	}
	if (!m_recording) upload->Unmap(0, nullptr);
	ComPtr<ID3D12CommandAllocator> uploadAllocator;
	ComPtr<ID3D12GraphicsCommandList> uploadList;
	if (!m_recording && (!CheckHr(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(&uploadAllocator)), "Create texture upload allocator") ||
		!CheckHr(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
		uploadAllocator.Get(), nullptr, IID_PPV_ARGS(&uploadList)),
		"Create texture upload command list")))
		return false;
	ID3D12GraphicsCommandList* copyList = m_recording ? m_commandList.Get() : uploadList.Get();
	if (texture.m_storage->m_state != D3D12_RESOURCE_STATE_COPY_DEST)
	{
		const D3D12_RESOURCE_BARRIER toCopy = Transition(texture.Resource(),
			texture.m_storage->m_state, D3D12_RESOURCE_STATE_COPY_DEST);
		copyList->ResourceBarrier(1, &toCopy);
	}
	for (UINT level = 0; level < levelCount; ++level)
	{
		D3D12_TEXTURE_COPY_LOCATION destination = {};
		destination.pResource = texture.Resource();
		destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		destination.SubresourceIndex = level;
		D3D12_TEXTURE_COPY_LOCATION source = {};
		source.pResource = upload.Get();
		source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		source.PlacedFootprint = footprints[level];
		source.PlacedFootprint.Offset += frameOffset;
		copyList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
	}
	const D3D12_RESOURCE_BARRIER barrier = Transition(texture.Resource(),
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	copyList->ResourceBarrier(1, &barrier);
	texture.m_storage->m_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	if (m_recording) {
		m_uploadResources[m_frameIndex].push_back(upload);
		return RetainTexture(&texture);
	}
	if (!CheckHr(uploadList->Close(), "Close texture upload command list"))
		return false;
	ID3D12CommandList* lists[] = {uploadList.Get()};
	m_commandQueue->ExecuteCommandLists(1, lists);
	WaitForGpu();
	texture.m_storage->m_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	return true;
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

bool NativeD3D12Renderer::BeginFrame(const FLOAT clearColor[4], FLOAT clearDepth)
{
	if (!IsInitialized() || m_recording || clearColor == nullptr)
		return false;
	m_profileFenceMs = 0;
	{
		CpuTimer timer(m_profiling ? &m_profileFenceMs : nullptr);
		if (!WaitForFence(m_fenceValues[m_frameIndex])) return false;
	}
	if (m_timestampHeap && m_timestampReady[m_frameIndex]) {
		const SIZE_T index = m_frameIndex*2;
		D3D12_RANGE range = {index*sizeof(UINT64),(index+2)*sizeof(UINT64)};
		UINT64* values = nullptr;
		if (SUCCEEDED(m_timestampReadback->Map(0,&range,reinterpret_cast<void**>(&values)))) {
			if (m_timestampFrequency && values[index+1] >= values[index])
				m_profileGpuMs = (values[index+1]-values[index])*1000.0/m_timestampFrequency;
			D3D12_RANGE noWrite = {0,0};
			m_timestampReadback->Unmap(0,&noWrite);
		}
	}
	if (!CheckHr(m_commandAllocators[m_frameIndex]->Reset(), "Reset command allocator") ||
		!CheckHr(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), nullptr),
			"Reset command list"))
		return false;
	m_uploadResources[m_frameIndex].clear();
	m_recordedDraws = 0;
	m_cpuMilliseconds = {};
	m_engineCpuMs = {};
	m_profileVertices = m_profileUploadBytes = 0;
	m_profileBufferHits = m_profileBufferCopies = 0;
	m_bufferReferences[m_frameIndex].clear();
	m_textureReferences[m_frameIndex].clear();
	for (auto& page : m_uploadPages[m_frameIndex]) page.used = 0;
	m_targetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	m_useDefaultDepth = true;
	if (m_timestampHeap) m_commandList->EndQuery(m_timestampHeap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,m_frameIndex*2);

	const D3D12_RESOURCE_BARRIER barrier = Transition(m_backBuffers[m_frameIndex].Get(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	m_commandList->ResourceBarrier(1, &barrier);
	m_currentRenderTarget = nullptr;
	const D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_currentRenderTarget != nullptr ?
		m_currentRenderTarget->RtvCpuHandle() : CurrentRenderTarget();
	const D3D12_CPU_DESCRIPTOR_HANDLE dsv = DepthStencil();
	m_commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
	m_commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
	m_commandList->ClearDepthStencilView(dsv,
		D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, clearDepth, 0, 0, nullptr);

	D3D12_VIEWPORT viewport = {0.0f, 0.0f, static_cast<FLOAT>(m_width),
		static_cast<FLOAT>(m_height), 0.0f, 1.0f};
	D3D12_RECT scissor = {0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height)};
	m_commandList->RSSetViewports(1, &viewport);
	m_commandList->RSSetScissorRects(1, &scissor);
	m_viewport = viewport;
	m_scissor = scissor;
	m_recording = true;
	return true;
}

void NativeD3D12Renderer::SetViewport(FLOAT x, FLOAT y, FLOAT width, FLOAT height,
	FLOAT minDepth, FLOAT maxDepth)
{
	if (!IsInitialized() || !m_recording || width <= 0.0f || height <= 0.0f)
		return;
	m_viewport = {x, y, width, height, minDepth, maxDepth};
	m_scissor = {static_cast<LONG>(x), static_cast<LONG>(y),
		static_cast<LONG>(x + width), static_cast<LONG>(y + height)};
	m_commandList->RSSetViewports(1, &m_viewport);
	m_commandList->RSSetScissorRects(1, &m_scissor);
}

void NativeD3D12Renderer::Clear(const FLOAT color[4], FLOAT depth, UINT8 stencil,
	bool clearColor, bool clearDepthStencil)
{
	if (!IsInitialized() || !m_recording || color == nullptr || (!clearColor && !clearDepthStencil))
		return;
	const D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_currentRenderTarget != nullptr ?
		m_currentRenderTarget->RtvCpuHandle() : CurrentRenderTarget();
	const D3D12_CPU_DESCRIPTOR_HANDLE dsv = DepthStencil();
	if (clearColor)
		m_commandList->ClearRenderTargetView(rtv, color, 0, nullptr);
	if (clearDepthStencil && m_useDefaultDepth)
		m_commandList->ClearDepthStencilView(dsv,
			D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, depth, stencil, 0, nullptr);
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

	m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
	m_commandList->SetGraphicsRootConstantBufferView(0, transformAddress);
	const UINT alphaTestConstants[9] = {
		m_alphaTestEnable ? 1u : 0u, static_cast<UINT>(m_alphaTestFunc), m_alphaTestRef, 0u,
		m_grayscale ? 1u : 0u, m_textureColorTexture ? 1u : 0u,
		m_textureColorVertex ? 1u : 0u, m_textureAlphaTexture ? 1u : 0u,
		m_textureAlphaVertex ? 1u : 0u};
	m_commandList->SetGraphicsRoot32BitConstants(2, 9, alphaTestConstants, 0);
	m_commandList->SetPipelineState(m_basicPipeline.Get());
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
	if (!IsInitialized() || !m_recording || vertices == nullptr || indices == nullptr || texture == nullptr ||
		!texture->IsValid() || vertexBytes == 0 || vertexStride < sizeof(float) * 3 || vertexCount == 0 ||
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
			(m_materialEnabled && uv.projected ? 4u : 0u) | (validUV ? 8u : 0u);
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
	m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
	m_commandList->SetGraphicsRootConstantBufferView(0, transformAddress);
	const UINT alphaTestConstants[9] = {
		m_alphaTestEnable ? 1u : 0u, static_cast<UINT>(m_alphaTestFunc), m_alphaTestRef, m_materialEnabled ? 1u : 0u,
		m_grayscale ? 1u : 0u, m_textureColorTexture ? 1u : 0u,
		m_textureColorVertex ? 1u : 0u, m_textureAlphaTexture ? 1u : 0u,
		m_textureAlphaVertex ? 1u : 0u};
	m_commandList->SetGraphicsRoot32BitConstants(2, 9, alphaTestConstants, 0);
	struct MaterialConstants {
		std::array<NativeMaterialStage, 4> stages;
		std::array<float,4> factor;
	} material = {m_materialStages, m_materialFactor};
	static_assert(sizeof(NativeMaterialStage) == 48, "HLSL material packing");
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
	m_commandList->SetPipelineState(m_texturedPipeline.Get());
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
	m_fogMode = 0;
	const bool result = DrawIndexed(vertices, sizeof(vertices), sizeof(ScreenVertex), 4,
		indices, 6, 0, 0, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, sizeof(float) * 3);
	m_fogMode = savedFog;
	m_worldViewProjection = savedTransform;
	return result;
}

bool NativeD3D12Renderer::DrawTexturedScreenQuad(FLOAT x, FLOAT y, FLOAT width,
	FLOAT height, FLOAT u0, FLOAT v0, FLOAT u1, FLOAT v1, UINT32 color,
	const NativeD3D12Texture* texture)
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
	m_fogMode = 0;
	m_treeSwayOffset = UINT_MAX;
	m_materialEnabled = false;
	const bool result = DrawIndexedTextured(vertices, sizeof(vertices), sizeof(ScreenVertex),
		4, sizeof(float) * 3 + sizeof(DWORD), indices, 6,
		D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, texture, sizeof(float) * 3);
	m_fogMode = savedFog;
	m_treeSwayOffset = savedSway;
	m_worldViewProjection = savedTransform;
	m_materialEnabled = savedMaterial;
	return result;
}

void NativeD3D12Renderer::SetWorldViewProjection(const float* matrix16)
{
	if (matrix16 != nullptr)
		std::copy(matrix16, matrix16 + m_worldViewProjection.size(), m_worldViewProjection.begin());
}

bool NativeD3D12Renderer::EndFrame(UINT syncInterval, bool present)
{
	if (!IsInitialized() || !m_recording)
		return false;
	if (m_currentRenderTarget != nullptr)
	{
		const D3D12_RESOURCE_BARRIER offscreenBarrier = Transition(
			m_currentRenderTarget->Resource(), D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		m_commandList->ResourceBarrier(1, &offscreenBarrier);
		m_currentRenderTarget->m_storage->m_state =
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		const D3D12_CPU_DESCRIPTOR_HANDLE rtv = CurrentRenderTarget();
		const D3D12_CPU_DESCRIPTOR_HANDLE dsv = DepthStencil();
		m_commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
		m_currentRenderTarget = nullptr;
	}
	const D3D12_RESOURCE_BARRIER barrier = Transition(m_backBuffers[m_frameIndex].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	m_commandList->ResourceBarrier(1, &barrier);
	if (m_timestampHeap) {
		m_commandList->EndQuery(m_timestampHeap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,m_frameIndex*2+1);
		m_commandList->ResolveQueryData(m_timestampHeap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,m_frameIndex*2,2,
			m_timestampReadback.Get(),m_frameIndex*2*sizeof(UINT64));
		m_timestampReady[m_frameIndex] = true;
	}
	if (!CheckHr(m_commandList->Close(), "Close command list"))
		return false;
	m_recording = false;
	ID3D12CommandList* lists[] = {m_commandList.Get()};
	m_commandQueue->ExecuteCommandLists(1, lists);
	++m_submittedFrames;
	if (m_profiling) {
		const ULONGLONG now = GetTickCount64();
		if (!m_profileWindowStart) m_profileWindowStart = now;
		++m_profileWindowFrames;
		if (now-m_profileWindowStart >= 1000) {
			char message[192];
			std::snprintf(message,sizeof(message),"Presentation rate: %.1f FPS (%u frames / %llu ms), draws=%u\n",
				m_profileWindowFrames*1000.0/(now-m_profileWindowStart),m_profileWindowFrames,
				now-m_profileWindowStart,m_recordedDraws);
			DiagnosticLog(message);
			m_profileWindowStart = now;
			m_profileWindowFrames = 0;
		}
	}
	if ((m_diagnostics || m_profiling) && (m_submittedFrames <= 5 || m_submittedFrames % 60 == 0)) {
		char message[256];
		std::snprintf(message, sizeof(message), "Frame %llu: draws=%u textures=%u pipelines=%zu viewport=%.0fx%.0f\n",
			m_submittedFrames, m_recordedDraws, m_descriptorPool->nextSrv, m_pipelineCache.size(), m_viewport.Width, m_viewport.Height);
		DiagnosticLog(message);
		std::snprintf(message, sizeof(message), "Native CPU ms: draw=%.2f geometryUpload=%.2f textureUpload=%.2f pipeline=%.2f vertices=%llu uploadKB=%llu\n",
			m_cpuMilliseconds[0],m_cpuMilliseconds[1],m_cpuMilliseconds[2],m_cpuMilliseconds[3],m_profileVertices,m_profileUploadBytes/1024);
		DiagnosticLog(message);
		std::snprintf(message, sizeof(message), "Resident buffers: hits=%u copies=%u\n",
			m_profileBufferHits,m_profileBufferCopies);
		DiagnosticLog(message);
		std::snprintf(message,sizeof(message),"GPU/queue ms: retiredFrame=%.2f fenceWait=%.2f previousPresent=%.2f\n",
			m_profileGpuMs,m_profileFenceMs,m_profilePresentMs);
		DiagnosticLog(message);
		std::snprintf(message,sizeof(message),"Engine graphics CPU ms: scene=%.2f ui=%.2f preparation=%.2f textureConversion=%.2f\n",
			m_engineCpuMs[0],m_engineCpuMs[1],m_engineCpuMs[2],m_engineCpuMs[3]);
		DiagnosticLog(message);
		std::snprintf(message,sizeof(message),"UI CPU ms: overlays=%.2f windows=%.2f remainder=%.2f\n",
			m_engineCpuMs[4],m_engineCpuMs[5],m_engineCpuMs[1]-m_engineCpuMs[4]-m_engineCpuMs[5]);
		DiagnosticLog(message);
		ComPtr<ID3D12InfoQueue> info;
		if (SUCCEEDED(m_device.As(&info))) {
			for (UINT64 i=0;i<info->GetNumStoredMessages();++i) {
				SIZE_T size=0;
				info->GetMessage(i,nullptr,&size);
				std::vector<unsigned char> bytes(size);
				auto* entry=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
				if (SUCCEEDED(info->GetMessage(i,entry,&size))) {
					DiagnosticLog(entry->pDescription); DiagnosticLog("\n");
				}
			}
			info->ClearStoredMessages();
		}
	}
	m_profilePresentMs = 0;
	if (present) {
		CpuTimer timer(m_profiling ? &m_profilePresentMs : nullptr);
		// Match immediate presentation without implicit desktop-compositor pacing.
		// The feature and windowed/sync-interval checks are required by DXGI.
		const UINT flags = syncInterval == 0 && m_windowed &&
			(m_swapChainFlags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) ? DXGI_PRESENT_ALLOW_TEARING : 0;
		if (!CheckHr(m_swapChain->Present(syncInterval,flags),"Present")) return false;
	}

	const UINT64 signalValue = ++m_nextFenceValue;
	m_fenceValues[m_frameIndex] = signalValue;
	if (!CheckHr(m_commandQueue->Signal(m_fence.Get(), signalValue), "Signal frame fence"))
		return false;
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
	return true;
}

void NativeD3D12Renderer::WaitForGpu()
{
	if (m_commandQueue == nullptr || m_fence == nullptr || m_fenceEvent == nullptr)
		return;
	const UINT64 value = ++m_nextFenceValue;
	if (SUCCEEDED(m_commandQueue->Signal(m_fence.Get(), value)) &&
		m_fence->GetCompletedValue() < value)
	{
		if (SUCCEEDED(m_fence->SetEventOnCompletion(value, m_fenceEvent)))
			WaitForSingleObject(m_fenceEvent, INFINITE);
	}
}

bool NativeD3D12Renderer::Resize(UINT width, UINT height)
{
	if (!IsInitialized() || width == 0 || height == 0)
		return false;
	if (m_recording && !EndFrame(0, false)) return false;
	WaitForGpu();
	for (auto& buffer : m_backBuffers)
		buffer.Reset();
	m_depthBuffer.Reset();
	if (!CheckHr(m_swapChain->ResizeBuffers(FrameCount, width, height,
		DXGI_FORMAT_R8G8B8A8_UNORM, m_swapChainFlags), "ResizeBuffers"))
		return false;
	m_width = width;
	m_height = height;
	if (!CreateFrameResources() || !CreateDepthBuffer())
		return false;
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
	return true;
}

void NativeD3D12Renderer::Shutdown()
{
	if (m_recording) EndFrame(0, false);
	m_recording = false;
	m_currentRenderTarget.reset();
	if (activeRenderer == this)
		activeRenderer = nullptr;
	WaitForGpu();
	if (m_fenceEvent != nullptr)
	{
		CloseHandle(m_fenceEvent);
		m_fenceEvent = nullptr;
	}
	m_commandList.Reset();
	m_timestampHeap.Reset();
	m_timestampReadback.Reset();
	m_timestampReady = {};
	m_timestampFrequency = 0;
	m_profileGpuMs = m_profilePresentMs = m_profileFenceMs = 0;
	for (auto& allocator : m_commandAllocators)
		allocator.Reset();
	m_depthBuffer.Reset();
	for (auto& buffer : m_backBuffers)
		buffer.Reset();
	m_dsvHeap.Reset();
	m_samplerHeap.Reset();
	m_rtvHeap.Reset();
	m_swapChain.Reset();
	m_commandQueue.Reset();
	m_device.Reset();
	m_adapter.Reset();
	m_factory.Reset();
	m_adapterDescription = {};
	m_hwnd = nullptr;
	m_width = 0;
	m_height = 0;
	m_frameIndex = 0;
	m_samplerDescriptorSize = 0;
	m_currentSamplerGpu = {};
	m_fenceValues = {};
	m_nextFenceValue = 0;
	m_worldViewProjection = {1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f};
	for (auto& resources : m_uploadResources)
		resources.clear();
	for (auto& pages : m_uploadPages) pages.clear();
	for (auto& references : m_bufferReferences) references.clear();
	for (auto& references : m_textureReferences) references.clear();
	m_pipelineCache.clear();
	for (auto& shader : m_shaderCache) shader.Reset();
	for (auto& shader : m_materialPixelShaders) shader.Reset();
	m_profileWindowStart = 0;
	m_profileWindowFrames = 0;
	for (auto& texture : m_materialTextures) texture.reset();
	m_materialEnabled = false;
	m_materialSamplers = {};
	m_descriptorPool.reset();
	m_texturedPipeline.Reset();
	m_textureRtvHeap.Reset();
	m_srvHeap.Reset();
	m_fence.Reset();
	m_basicPipeline.Reset();
	m_rootSignature.Reset();
}


NativeD3D12Renderer::PipelineKey NativeD3D12Renderer::GetPipelineKey(bool textured) const
{
	return {UINT(textured), UINT(m_cullMode), UINT(m_depthEnable), UINT(m_depthWrite),
		UINT(m_depthFunc), UINT(m_blendEnable), UINT(m_sourceBlend), UINT(m_destinationBlend),
		UINT(m_blendOp), m_renderTargetWriteMask, UINT(m_stencilEnable), UINT(m_stencilFunc),
		m_stencilReadMask, m_stencilWriteMask, UINT(m_stencilFail), UINT(m_stencilDepthFail),
		UINT(m_stencilPass), UINT(m_targetFormat), UINT(m_useDefaultDepth), 0};
}

bool NativeD3D12Renderer::WaitForFence(UINT64 value)
{
	if (m_fence->GetCompletedValue() == UINT64_MAX) return false;
	if (m_fence->GetCompletedValue() >= value) return true;
	if (!CheckHr(m_fence->SetEventOnCompletion(value, m_fenceEvent), "Set frame fence event"))
		return false;
	return WaitForSingleObject(m_fenceEvent, 30000) == WAIT_OBJECT_0 &&
		m_fence->GetCompletedValue() != UINT64_MAX;
}

bool NativeD3D12Renderer::RetainTexture(const NativeD3D12Texture* texture)
{
	if (!texture || !texture->IsValid() || texture->m_storage->pool != m_descriptorPool) return false;
	if (m_recording) m_textureReferences[m_frameIndex].push_back(texture->m_storage);
	return true;
}

bool NativeD3D12Renderer::AllocateFrameUpload(UINT size, UINT alignment,
	ID3D12Resource*& resource, UINT64& offset, unsigned char*& mapped)
{
	if (!m_recording || !size || !alignment || (alignment & (alignment-1)) ||
		size > UINT_MAX-(alignment-1)) return false;
	auto& pages = m_uploadPages[m_frameIndex];
	UploadPage* page = nullptr;
	for (auto& candidate : pages) {
		const UINT64 begin = (static_cast<UINT64>(candidate.used)+alignment-1) & ~static_cast<UINT64>(alignment-1);
		if (begin+size <= candidate.size) { page = &candidate; offset = begin; break; }
	}
	if (!page) {
		UploadPage next;
		next.size = (std::max)((size+alignment-1) & ~(alignment-1),1024u*1024u);
		D3D12_HEAP_PROPERTIES heap = {};
		heap.Type = D3D12_HEAP_TYPE_UPLOAD;
		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Width = next.size;
		desc.Height = desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		if (!CheckHr(m_device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
			D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&next.resource)),
			"Create frame upload page")) return false;
		D3D12_RANGE noRead = {0,0};
		if (!CheckHr(next.resource->Map(0,&noRead,reinterpret_cast<void**>(&next.mapped)),
			"Map frame upload page")) return false;
		pages.push_back(std::move(next));
		page = &pages.back();
		offset = 0;
	}
	resource = page->resource.Get();
	mapped = page->mapped+offset;
	page->used = static_cast<UINT>(offset+size);
	return true;
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
				D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&version->resource)),
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
	m_bufferReferences[m_frameIndex].push_back(version);
	return true;
}

bool NativeD3D12Renderer::DecodeTextureBgra(DXGI_FORMAT format, UINT width, UINT height,
	const NativeD3D12TextureLevel& source, std::vector<unsigned char>& bgra)
{
	const bool bc1 = format == DXGI_FORMAT_BC1_UNORM;
	const bool bc2 = format == DXGI_FORMAT_BC2_UNORM;
	const bool bc3 = format == DXGI_FORMAT_BC3_UNORM;
	const bool compressed = bc1 || bc2 || bc3;
	UINT bytes = 0;
	switch (format) {
	case DXGI_FORMAT_BC1_UNORM: bytes = 8; break;
	case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC3_UNORM: bytes = 16; break;
	case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8X8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM: bytes = 4; break;
	case DXGI_FORMAT_B5G6R5_UNORM: case DXGI_FORMAT_B5G5R5A1_UNORM:
	case DXGI_FORMAT_B4G4R4A4_UNORM: bytes = 2; break;
	case DXGI_FORMAT_A8_UNORM: case DXGI_FORMAT_R8_UNORM: bytes = 1; break;
	default: return false;
	}
	if (!width || !height || width > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
		height > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION || !source.data) return false;
	const UINT rows = compressed ? (height+3)/4 : height;
	const UINT rowBytes = (compressed ? (width+3)/4 : width)*bytes;
	if (source.rowPitch < rowBytes || (source.slicePitch &&
		UINT64(rows-1)*source.rowPitch+rowBytes > source.slicePitch)) return false;
	bgra.resize(size_t(width)*height*4);
	const auto* data = static_cast<const unsigned char*>(source.data);
	auto word = [](const unsigned char* p) { return UINT(p[0]) | UINT(p[1])<<8; };
	auto rgb565 = [](UINT value, unsigned char* color) {
		color[0] = static_cast<unsigned char>(((value&31)<<3) | ((value&31)>>2));
		color[1] = static_cast<unsigned char>((((value>>5)&63)<<2) | (((value>>5)&63)>>4));
		color[2] = static_cast<unsigned char>((((value>>11)&31)<<3) | (((value>>11)&31)>>2));
		color[3] = 255;
	};
	if (!compressed) {
		for (UINT y=0;y<height;++y) for (UINT x=0;x<width;++x) {
			const auto* p = data+size_t(y)*source.rowPitch+x*bytes;
			auto* out = bgra.data()+(size_t(y)*width+x)*4;
			if (bytes == 4) {
				std::memcpy(out,p,4);
				if (format == DXGI_FORMAT_R8G8B8A8_UNORM) std::swap(out[0],out[2]);
				if (format == DXGI_FORMAT_B8G8R8X8_UNORM) out[3] = 255;
			} else if (bytes == 1) {
				out[0] = out[1] = out[2] = format == DXGI_FORMAT_A8_UNORM ? 255 : *p;
				out[3] = format == DXGI_FORMAT_A8_UNORM ? *p : 255;
			} else {
				const UINT value = word(p);
				if (format == DXGI_FORMAT_B5G6R5_UNORM) rgb565(value,out);
				else if (format == DXGI_FORMAT_B4G4R4A4_UNORM)
					for (UINT c=0;c<4;++c) out[c] = static_cast<unsigned char>(((value>>(c*4))&15)*17);
				else {
					for (UINT c=0;c<3;++c) {
						const UINT v = (value>>(c*5))&31;
						out[c] = static_cast<unsigned char>((v<<3)|(v>>2));
					}
					out[3] = value&0x8000 ? 255 : 0;
				}
			}
		}
		return true;
	}
	// Decode complete blocks into clipped output. Small/odd mip dimensions must
	// never write the unused pixels of the final 4x4 block into adjacent rows.
	for (UINT by=0;by<height;by+=4) for (UINT bx=0;bx<width;bx+=4) {
		const auto* block = data+size_t(by/4)*source.rowPitch+(bx/4)*bytes;
		const auto* colorBlock = block+(bc1 ? 0 : 8);
		const UINT c0 = word(colorBlock), c1 = word(colorBlock+2);
		unsigned char colors[4][4] = {};
		rgb565(c0,colors[0]); rgb565(c1,colors[1]);
		for (UINT c=0;c<3;++c) {
			colors[2][c] = static_cast<unsigned char>((bc1 && c0<=c1) ?
				(UINT(colors[0][c])+colors[1][c])/2 : (2*UINT(colors[0][c])+colors[1][c])/3);
			colors[3][c] = bc1 && c0<=c1 ? 0 :
				static_cast<unsigned char>((UINT(colors[0][c])+2*UINT(colors[1][c]))/3);
		}
		colors[2][3] = 255; colors[3][3] = bc1 && c0<=c1 ? 0 : 255;
		UINT alphas[8] = {block[0],block[1]};
		UINT64 alphaBits = 0;
		if (bc3) {
			for (UINT i=0;i<6;++i) alphaBits |= UINT64(block[2+i])<<(i*8);
			if (alphas[0]>alphas[1])
				for (UINT i=2;i<8;++i) alphas[i] = ((8-i)*alphas[0]+(i-1)*alphas[1])/7;
			else {
				for (UINT i=2;i<6;++i) alphas[i] = ((6-i)*alphas[0]+(i-1)*alphas[1])/5;
				alphas[6] = 0; alphas[7] = 255;
			}
		}
		for (UINT y=0;y<4 && by+y<height;++y) for (UINT x=0;x<4 && bx+x<width;++x) {
			const UINT pixel = y*4+x, selector = (colorBlock[4+y]>>(x*2))&3;
			auto* out = bgra.data()+(size_t(by+y)*width+bx+x)*4;
			std::memcpy(out,colors[selector],4);
			if (bc2) out[3] = static_cast<unsigned char>(((block[pixel/2]>>((pixel%2)*4))&15)*17);
			if (bc3) out[3] = static_cast<unsigned char>(alphas[(alphaBits>>(pixel*3))&7]);
		}
	}
	return true;
}

bool NativeD3D12Renderer::ReadbackTexture(const NativeD3D12Texture& texture, UINT mip,
	std::vector<unsigned char>& bgra)
{
	if (!IsInitialized() || !texture.IsValid() || texture.m_storage->pool != m_descriptorPool ||
		mip >= texture.MipLevels()) return false;
	const auto desc = texture.Resource()->GetDesc();
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
	UINT rows = 0;
	UINT64 rowBytes = 0, total = 0;
	m_device->GetCopyableFootprints(&desc,mip,1,0,&footprint,&rows,&rowBytes,&total);
	if (!total || total > UINT_MAX) return false;
	D3D12_HEAP_PROPERTIES heap = {};
	heap.Type = D3D12_HEAP_TYPE_READBACK;
	D3D12_RESOURCE_DESC buffer = {};
	buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	buffer.Width = total;
	buffer.Height = buffer.DepthOrArraySize = buffer.MipLevels = buffer.SampleDesc.Count = 1;
	buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	ComPtr<ID3D12Resource> readback;
	ComPtr<ID3D12CommandAllocator> allocator;
	ComPtr<ID3D12GraphicsCommandList> list;
	if (!CheckHr(m_device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,
		D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)),"Create texture readback") ||
		!CheckHr(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(&allocator)),"Create readback allocator") ||
		!CheckHr(m_device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),
			nullptr,IID_PPV_ARGS(&list)),"Create readback list")) return false;
	const auto previousState = texture.m_storage->m_state;
	if (previousState != D3D12_RESOURCE_STATE_COPY_SOURCE) {
		const auto barrier = Transition(texture.Resource(),previousState,D3D12_RESOURCE_STATE_COPY_SOURCE);
		list->ResourceBarrier(1,&barrier);
	}
	D3D12_TEXTURE_COPY_LOCATION source = {}, target = {};
	source.pResource = texture.Resource(); source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	source.SubresourceIndex = mip;
	target.pResource = readback.Get(); target.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	target.PlacedFootprint = footprint;
	list->CopyTextureRegion(&target,0,0,0,&source,nullptr);
	if (previousState != D3D12_RESOURCE_STATE_COPY_SOURCE) {
		const auto barrier = Transition(texture.Resource(),D3D12_RESOURCE_STATE_COPY_SOURCE,previousState);
		list->ResourceBarrier(1,&barrier);
	}
	if (!CheckHr(list->Close(),"Close readback list")) return false;
	if (m_recording) {
		if (!CheckHr(m_commandList->Close(),"Suspend frame for readback")) return false;
		ID3D12CommandList* pending[] = {m_commandList.Get()};
		m_commandQueue->ExecuteCommandLists(1,pending);
	}
	ID3D12CommandList* copies[] = {list.Get()};
	m_commandQueue->ExecuteCommandLists(1,copies);
	const UINT64 fence = ++m_nextFenceValue;
	if (!CheckHr(m_commandQueue->Signal(m_fence.Get(),fence),"Signal readback fence") ||
		!WaitForFence(fence)) { m_recording = false; return false; }
	if (m_recording) {
		// Keep frame uploads and ownership snapshots alive, and never clear the
		// target or advance the swap chain during a synchronous surface access.
		m_fenceValues[m_frameIndex] = fence;
		if (!CheckHr(m_commandAllocators[m_frameIndex]->Reset(),"Reset readback frame allocator") ||
			!CheckHr(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(),nullptr),
				"Resume frame after readback")) { m_recording = false; return false; }
		const auto rtv = m_currentRenderTarget ? m_currentRenderTarget->RtvCpuHandle() : CurrentRenderTarget();
		const auto dsv = DepthStencil();
		m_commandList->OMSetRenderTargets(1,&rtv,FALSE,m_useDefaultDepth ? &dsv : nullptr);
		m_commandList->RSSetViewports(1,&m_viewport);
		m_commandList->RSSetScissorRects(1,&m_scissor);
	}
	unsigned char* data = nullptr;
	D3D12_RANGE range = {0,static_cast<SIZE_T>(total)};
	if (!CheckHr(readback->Map(0,&range,reinterpret_cast<void**>(&data)),"Map texture readback")) return false;
	NativeD3D12TextureLevel level = {data+footprint.Offset,footprint.Footprint.RowPitch,
		static_cast<UINT>(total-footprint.Offset)};
	const bool result = DecodeTextureBgra(texture.Format(),(std::max)(1u,texture.Width()>>mip),
		(std::max)(1u,texture.Height()>>mip),level,bgra);
	D3D12_RANGE noWrite = {0,0};
	readback->Unmap(0,&noWrite);
	return result;
}

bool NativeD3D12Renderer::ReadbackFrame(std::vector<unsigned char>& rgba)
{
	if (!m_recording || !SetRenderTarget(nullptr)) return false;
	const auto desc = CurrentBackBuffer()->GetDesc();
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
	UINT rows = 0;
	UINT64 rowBytes = 0, total = 0;
	m_device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows, &rowBytes, &total);
	D3D12_HEAP_PROPERTIES heap = {};
	heap.Type = D3D12_HEAP_TYPE_READBACK;
	D3D12_RESOURCE_DESC buffer = {};
	buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	buffer.Width = total;
	buffer.Height = buffer.DepthOrArraySize = buffer.MipLevels = buffer.SampleDesc.Count = 1;
	buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	ComPtr<ID3D12Resource> readback;
	if (!CheckHr(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)), "Create pixel readback")) return false;
	auto barrier = Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
	m_commandList->ResourceBarrier(1, &barrier);
	D3D12_TEXTURE_COPY_LOCATION source = {}, target = {};
	source.pResource = CurrentBackBuffer();
	source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	target.pResource = readback.Get();
	target.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	target.PlacedFootprint = footprint;
	m_commandList->CopyTextureRegion(&target, 0, 0, 0, &source, nullptr);
	std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
	m_commandList->ResourceBarrier(1, &barrier);
	const UINT frame = m_frameIndex;
	if (!EndFrame(0, false) || !WaitForFence(m_fenceValues[frame])) return false;
	unsigned char* data = nullptr;
	D3D12_RANGE range = {0, static_cast<SIZE_T>(total)};
	if (!CheckHr(readback->Map(0, &range, reinterpret_cast<void**>(&data)), "Map pixel readback")) return false;
	rgba.resize(static_cast<size_t>(m_width) * m_height * 4);
	for (UINT y = 0; y < m_height; ++y)
		std::memcpy(rgba.data() + static_cast<size_t>(y) * m_width * 4,
			data + footprint.Offset + static_cast<size_t>(y) * footprint.Footprint.RowPitch, m_width * 4);
	D3D12_RANGE noWrite = {0, 0};
	readback->Unmap(0, &noWrite);
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
