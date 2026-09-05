#include "native_d3d12_renderer.h"
#include "native_d3d12_diagnostics.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstdio>

using Microsoft::WRL::ComPtr;
using NativeD3D12Internal::CpuTimer;
using NativeD3D12Internal::DiagnosticLog;

#include "native_d3d12_texture_codec.h"

// Texture allocation, copies, target binding and synchronized surface access.
// These operations coordinate with the renderer-owned queue and frame state.
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

bool NativeD3D12Renderer::CopyCurrentRenderTarget(NativeD3D12Texture& destination)
{
	if (!m_recording || !destination.IsValid() ||
		destination.Width() != RenderTargetWidth() || destination.Height() != RenderTargetHeight() ||
		destination.Format() != m_targetFormat || destination.MipLevels() != 1)
		return false;
	ID3D12Resource* source = m_currentRenderTarget ? m_currentRenderTarget->Resource() : CurrentBackBuffer();
	if (source == destination.Resource() || !RetainTexture(&destination)) return false;
	D3D12_RESOURCE_BARRIER barriers[2];
	UINT count = 0;
	barriers[count++] = Transition(source,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
	if (destination.m_storage->m_state != D3D12_RESOURCE_STATE_COPY_DEST)
		barriers[count++] = Transition(destination.Resource(),destination.m_storage->m_state,D3D12_RESOURCE_STATE_COPY_DEST);
	m_commandList->ResourceBarrier(count,barriers);
	D3D12_TEXTURE_COPY_LOCATION src = {}, dst = {};
	src.pResource = source;
	src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dst.pResource = destination.Resource();
	dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	m_commandList->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
	barriers[0] = Transition(source,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
	barriers[1] = Transition(destination.Resource(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	m_commandList->ResourceBarrier(2,barriers);
	destination.m_storage->m_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	return true;
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
		m_frameResources.RetainUpload(m_frameIndex, upload);
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

bool NativeD3D12Renderer::RetainTexture(const NativeD3D12Texture* texture)
{
	if (!texture || !texture->IsValid() || texture->m_storage->pool != m_descriptorPool) return false;
	if (m_recording) m_frameResources.RetainTexture(m_frameIndex, texture->m_storage);
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


bool NativeD3D12Renderer::DecodeTextureBgra(DXGI_FORMAT format, UINT width, UINT height,
	const NativeD3D12TextureLevel& source, std::vector<unsigned char>& bgra)
{
	return NativeD3D12TextureCodec::DecodeBgra(format, width, height, source, bgra);
}
