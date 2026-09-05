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

// Shared CPU authoring data and GPU allocation identity. No renderer/device singleton dependency.
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
