/*
** Command & Conquer Generals Zero Hour(tm)
** Native Direct3D 12 renderer device layer.
*/

#include "native_d3d12_renderer.h"
#include "native_d3d12_diagnostics.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <d3d12sdklayers.h>
#include <chrono>
#include <cmath>

using Microsoft::WRL::ComPtr;
using NativeD3D12Internal::CpuTimer;
using NativeD3D12Internal::DiagnosticLog;

namespace
{
	NativeD3D12Renderer* activeRenderer = nullptr;
}

namespace {
	bool IsSoftwareAdapter(const DXGI_ADAPTER_DESC1& description)
	{
		return (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
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
	return NativeD3D12Internal::CheckHr(hr, operation);
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
	m_frameResources.Retire(m_frameIndex);
	m_recordedDraws = 0;
	m_cpuMilliseconds = {};
	m_engineCpuMs = {};
	m_profileVertices = m_profileUploadBytes = 0;
	m_profileBufferHits = m_profileBufferCopies = 0;
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
			m_submittedFrames, m_recordedDraws, m_descriptorPool->nextSrv, m_pipelines.Size(), m_viewport.Width, m_viewport.Height);
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
	m_frameResources.Release();
	m_pipelines.Reset();
	m_profileWindowStart = 0;
	m_profileWindowFrames = 0;
	for (auto& texture : m_materialTextures) texture.reset();
	m_neutralMaterialTexture.reset();
	m_materialEnabled = false;
	m_materialSamplers = {};
	m_descriptorPool.reset();
	m_textureRtvHeap.Reset();
	m_srvHeap.Reset();
	m_fence.Reset();
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
