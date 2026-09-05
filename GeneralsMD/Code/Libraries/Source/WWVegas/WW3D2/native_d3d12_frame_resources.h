#pragma once

#include "native_d3d12_resources.h"

// Fence-indexed ownership, not a submission queue. The renderer alone signals
// and waits fences, then explicitly retires the corresponding slot.
class NativeD3D12FrameResources final {
public:
	static constexpr UINT FrameCount = 2;
	NativeD3D12FrameResources() = default;
	NativeD3D12FrameResources(const NativeD3D12FrameResources&) = delete;
	NativeD3D12FrameResources& operator=(const NativeD3D12FrameResources&) = delete;
	bool Allocate(ID3D12Device* device, UINT frame, UINT size, UINT alignment,
		ID3D12Resource*& resource, UINT64& offset, unsigned char*& mapped);
	void RetainUpload(UINT frame, const Microsoft::WRL::ComPtr<ID3D12Resource>& upload) {
		m_frames[frame].uploads.push_back(upload);
	}
	void RetainTexture(UINT frame, const std::shared_ptr<NativeD3D12TextureStorage>& texture) {
		m_frames[frame].textures.push_back(texture);
	}
	void RetainBuffer(UINT frame, const std::shared_ptr<NativeD3D12BufferVersion>& buffer) {
		m_frames[frame].buffers.push_back(buffer);
	}
	// Precondition: this slot's submitted fence has completed. Keep upload-page
	// allocations warm; only release retained draw resources and reset offsets.
	void Retire(UINT frame);
	// Precondition: all submitted work has completed (shutdown/device reset).
	void Release();
private:
	struct UploadPage {
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		unsigned char* mapped = nullptr;
		UINT size = 0, used = 0;
	};
	struct Frame {
		std::vector<UploadPage> pages;
		std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> uploads;
		std::vector<std::shared_ptr<NativeD3D12TextureStorage>> textures;
		std::vector<std::shared_ptr<NativeD3D12BufferVersion>> buffers;
	};
	std::array<Frame, FrameCount> m_frames;
};
