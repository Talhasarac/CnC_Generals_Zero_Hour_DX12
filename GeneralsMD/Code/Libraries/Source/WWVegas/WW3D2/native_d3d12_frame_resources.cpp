#include "native_d3d12_frame_resources.h"
#include "native_d3d12_diagnostics.h"
#include <algorithm>
#include <cassert>

using NativeD3D12Internal::CheckHr;

bool NativeD3D12FrameResources::Allocate(ID3D12Device* device, UINT frame, UINT size, UINT alignment,
	ID3D12Resource*& resource, UINT64& offset, unsigned char*& mapped)
{
	if (!device || frame >= FrameCount || !size || !alignment || (alignment & (alignment-1)) ||
		size > UINT_MAX-(alignment-1)) return false;
	auto& pages = m_frames[frame].pages;
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
		if (!CheckHr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
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


void NativeD3D12FrameResources::Retire(UINT frame)
{
	assert(frame < FrameCount);
	auto& slot = m_frames[frame];
	slot.uploads.clear();
	slot.buffers.clear();
	slot.textures.clear();
	for (auto& page : slot.pages) page.used = 0;
}

void NativeD3D12FrameResources::Release()
{
	for (UINT frame = 0; frame < FrameCount; ++frame) {
		Retire(frame);
		m_frames[frame].pages.clear();
	}
}
