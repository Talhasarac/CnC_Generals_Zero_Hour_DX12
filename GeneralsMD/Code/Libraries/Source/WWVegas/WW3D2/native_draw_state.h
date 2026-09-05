/*
** Native D3D12 draw descriptions shared by WW3D2 producers.
**
** These are value descriptions for one draw.  They are deliberately separate
** from DX8Wrapper's deferred state cache: producers can describe the native
** layout and material they need, then apply/submit that description through
** NativeD3D12Renderer's existing setters.  Resource ownership remains with
** the engine and renderer; a submission only keeps non-owning views until the
** call returns.
*/

#pragma once

#include "native_d3d12_renderer.h"

#include <array>
#include <cstdint>
#include <limits>

enum class NativeVertexSemantic : std::uint8_t
{
	Position,
	BlendWeight,
	BlendIndex,
	Normal,
	PointSize,
	Color,
	TexCoord
};

struct NativeVertexElementDesc
{
	NativeVertexSemantic semantic = NativeVertexSemantic::Position;
	UINT semanticIndex = 0;
	DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
	UINT offset = 0;
	UINT inputSlot = 0;
	D3D12_INPUT_CLASSIFICATION inputClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
	UINT instanceStepRate = 0;
};

struct NativeVertexLayoutDesc
{
	static constexpr UINT MaxElements = 16;

	std::array<NativeVertexElementDesc, MaxElements> elements = {};
	UINT elementCount = 0;
	UINT stride = 0;
	bool valid = true;

	bool Add(NativeVertexSemantic semantic, UINT semanticIndex, DXGI_FORMAT format,
		UINT offset, UINT inputSlot = 0,
		D3D12_INPUT_CLASSIFICATION inputClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
		UINT instanceStepRate = 0)
	{
		if (elementCount >= MaxElements || format == DXGI_FORMAT_UNKNOWN)
		{
			valid = false;
			return false;
		}
		elements[elementCount++] = {semantic, semanticIndex, format, offset, inputSlot,
			inputClass, instanceStepRate};
		return true;
	}

	UINT Find_Offset(NativeVertexSemantic semantic, UINT semanticIndex = 0) const
	{
		for (UINT i = 0; i < elementCount; ++i)
			if (elements[i].semantic == semantic && elements[i].semanticIndex == semanticIndex)
				return elements[i].offset;
		return UINT_MAX;
	}

	bool Supports_Native_Draw(UINT vertexStride) const
	{
		if (!valid || elementCount > MaxElements || vertexStride < 12 ||
			(stride != 0 && stride != vertexStride) ||
			Find_Offset(NativeVertexSemantic::Position) != 0) return false;
		for (UINT i = 0; i < elementCount; ++i)
		{
			const auto& e = elements[i];
			if (e.inputSlot != 0 || e.inputClass != D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA ||
				e.instanceStepRate != 0) return false;
			UINT bytes = 0;
			switch (e.format) {
			case DXGI_FORMAT_R32_FLOAT: case DXGI_FORMAT_R8G8B8A8_UNORM: bytes = 4; break;
			case DXGI_FORMAT_R32G32_FLOAT: bytes = 8; break;
			case DXGI_FORMAT_R32G32B32_FLOAT: bytes = 12; break;
			case DXGI_FORMAT_R32G32B32A32_FLOAT: bytes = 16; break;
			default: return false;
			}
			if (e.offset > vertexStride || bytes > vertexStride - e.offset) return false;
			if ((e.semantic == NativeVertexSemantic::Position || e.semantic == NativeVertexSemantic::Normal) &&
				(e.semanticIndex != 0 || e.format != DXGI_FORMAT_R32G32B32_FLOAT)) return false;
			if (e.semantic == NativeVertexSemantic::Color &&
				(e.semanticIndex > 1 || e.format != DXGI_FORMAT_R8G8B8A8_UNORM)) return false;
			if (e.semantic == NativeVertexSemantic::TexCoord &&
				(e.semanticIndex > 7 || e.format == DXGI_FORMAT_R8G8B8A8_UNORM)) return false;
			if (e.semantic == NativeVertexSemantic::BlendWeight || e.semantic == NativeVertexSemantic::BlendIndex ||
				e.semantic == NativeVertexSemantic::PointSize) return false;
			for (UINT j = 0; j < i; ++j)
				if (elements[j].semantic == e.semantic && elements[j].semanticIndex == e.semanticIndex) return false;
		}
		return true;
	}
};

struct NativeSamplerDesc
{
	NativeD3D12FilterMode minFilter = NativeD3D12FilterMode::Point;
	NativeD3D12FilterMode magFilter = NativeD3D12FilterMode::Point;
	NativeD3D12FilterMode mipFilter = NativeD3D12FilterMode::Point;
	bool clampU = false;
	bool clampV = false;
	UINT maxAnisotropy = 1;
};

struct NativeMaterialDescription
{
	bool enabled = false;
	std::array<NativeMaterialStage, 4> stages = {};
	std::array<NativeMaterialCoordinates, 4> coordinates = {};
	std::array<const NativeD3D12Texture*, 4> textures = {};
	std::array<NativeSamplerDesc, 4> samplers = {};
	UINT32 factor = 0xffffffff;
	bool hasTextureCombine = false;
	bool textureColorTexture = true;
	bool textureColorVertex = true;
	bool textureAlphaTexture = true;
	bool textureAlphaVertex = true;
};

struct NativeDrawSubmission
{
	const void* vertices = nullptr;
	UINT vertexBytes = 0;
	UINT vertexStride = 0;
	UINT vertexCount = 0;
	const unsigned short* indices = nullptr;
	UINT indexCount = 0;
	UINT startIndex = 0;
	UINT baseVertex = 0;
	D3D12_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	NativeVertexLayoutDesc layout;
	NativeMaterialDescription material;
	bool useMaterial = false;
	const NativeD3D12Texture* texture = nullptr;
	const NativeD3D12UploadBuffer* vertexOwner = nullptr;
	const NativeD3D12UploadBuffer* indexOwner = nullptr;

	bool Is_Valid() const
	{
		return vertices != nullptr && indices != nullptr && vertexBytes != 0 &&
			vertexStride != 0 && vertexCount != 0 && indexCount != 0 &&
			vertexCount <= vertexBytes / vertexStride && baseVertex < vertexCount &&
			startIndex == 0 && layout.Supports_Native_Draw(vertexStride);
	}
};

inline bool Describe_Native_Indexed_Range(NativeDrawSubmission& draw, UINT firstIndex,
	UINT indexCount, UINT baseVertex, UINT minVertex, UINT vertexRange,
	D3D12_PRIMITIVE_TOPOLOGY topology)
{
	if (!draw.indices || firstIndex>draw.indexCount || indexCount>draw.indexCount-firstIndex ||
		baseVertex>=draw.vertexCount || minVertex>draw.vertexCount-baseVertex ||
		vertexRange>draw.vertexCount-baseVertex-minVertex || !indexCount || !vertexRange) return false;
	draw.indices += firstIndex;
	draw.indexCount = indexCount;
	draw.baseVertex = baseVertex;
	draw.topology = topology;
	return true;
}

inline void Apply_Native_Material_Description(NativeD3D12Renderer& renderer,
	const NativeMaterialDescription& description)
{
	renderer.SetMaterialEnabled(description.enabled);
	if (!description.enabled)
		return;

	renderer.SetMaterialFactor(description.factor);
	for (UINT stage = 0; stage < description.stages.size(); ++stage)
	{
		const NativeSamplerDesc& sampler = description.samplers[stage];
		renderer.SetMaterialStage(stage, description.stages[stage],
			description.coordinates[stage], description.textures[stage]);
		renderer.SetSamplerState(sampler.minFilter, sampler.magFilter, sampler.mipFilter,
			sampler.clampU, sampler.clampV, sampler.maxAnisotropy);
		renderer.SetMaterialSampler(stage);
	}
	if (description.hasTextureCombine)
		renderer.SetTextureCombine(description.textureColorTexture, description.textureColorVertex,
			description.textureAlphaTexture, description.textureAlphaVertex);
}

inline const NativeD3D12Texture* First_Native_Material_Texture(
	const NativeMaterialDescription& description)
{
	for (const NativeD3D12Texture* texture : description.textures)
		if (texture != nullptr)
			return texture;
	return nullptr;
}

inline bool Submit_Native_Draw(NativeD3D12Renderer& renderer,
	const NativeDrawSubmission& submission)
{
	if (!submission.Is_Valid())
		return false;

	if (submission.useMaterial)
		Apply_Native_Material_Description(renderer, submission.material);
	else
		renderer.SetMaterialEnabled(false);

	const UINT colorOffset = submission.layout.Find_Offset(NativeVertexSemantic::Color, 0);
	const UINT normalOffset = submission.layout.Find_Offset(NativeVertexSemantic::Normal);
	const UINT specularOffset = submission.layout.Find_Offset(NativeVertexSemantic::Color, 1);
	const NativeD3D12Texture* texture = submission.texture;
	if (texture == nullptr && submission.useMaterial)
		texture = First_Native_Material_Texture(submission.material);

	// Material arithmetic is meaningful even with no texture resources (for
	// example factor-colored passes). The basic shader does not evaluate it.
	if (texture != nullptr || (submission.useMaterial && submission.material.enabled))
	{
		UINT texcoordOffset = submission.layout.Find_Offset(NativeVertexSemantic::TexCoord, 0);
		if (submission.useMaterial)
			texcoordOffset = submission.material.coordinates[0].offset;
		// Both textured and untextured submissions interpret indices relative
		// to baseVertex. The textured backend takes an already-offset view.
		const UINT byteOffset = submission.baseVertex * submission.vertexStride;
		return renderer.DrawIndexedTextured(
			static_cast<const unsigned char*>(submission.vertices) + byteOffset,
			submission.vertexBytes - byteOffset,
			submission.vertexStride, submission.vertexCount - submission.baseVertex, texcoordOffset,
			submission.indices, submission.indexCount, submission.topology, texture,
			colorOffset, submission.vertexOwner, submission.indexOwner, normalOffset,
			specularOffset);
	}

	return renderer.DrawIndexed(submission.vertices, submission.vertexBytes, submission.vertexStride,
		submission.vertexCount, submission.indices, submission.indexCount, submission.startIndex,
		submission.baseVertex, submission.topology, colorOffset, submission.vertexOwner,
		submission.indexOwner, normalOffset, specularOffset);
}
