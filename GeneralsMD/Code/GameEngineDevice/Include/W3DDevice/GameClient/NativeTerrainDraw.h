/* Native D3D12 draw helpers for terrain-owned batches. */

#pragma once

#include "WW3D2/native_d3d12_renderer.h"
#include "WW3D2/dx8vertexbuffer.h"
#include "WW3D2/dx8indexbuffer.h"
#include "WW3D2/texture.h"
#include "WW3D2/camera.h"
#include "WW3D2/native_draw_state.h"

inline bool NativeTerrainDrawDynamic(NativeD3D12Renderer& renderer,
	const DynamicVBAccessClass& vb, const DynamicIBAccessClass& ib,
	UINT vertexCount, UINT indexCount, const NativeVertexLayoutDesc& layout,
	const NativeMaterialDescription& material)
{
	const auto* vertices=vb.Get_Native_Vertex_Buffer();
	const auto* indices=ib.Get_Native_Index_Buffer();
	const size_t vertexOffset=size_t(vb.Get_Native_Vertex_Offset())*layout.stride;
	const size_t indexOffset=size_t(ib.Get_Native_Index_Offset())*sizeof(unsigned short);
	const size_t bytes=size_t(vertexCount)*layout.stride;
	if (!vertices || !indices || vertexCount>vb.Get_Vertex_Count() || indexCount>ib.Get_Index_Count() ||
		vertexOffset>vertices->Size() || bytes>vertices->Size()-vertexOffset || bytes>UINT_MAX ||
		indexOffset>indices->Size() || size_t(indexCount)*2>indices->Size()-indexOffset) return false;
	NativeDrawSubmission draw;
	draw.vertices=static_cast<const unsigned char*>(vertices->Data())+vertexOffset;
	draw.vertexBytes=UINT(bytes); draw.vertexStride=layout.stride; draw.vertexCount=vertexCount;
	draw.indices=reinterpret_cast<const unsigned short*>(static_cast<const unsigned char*>(indices->Data())+indexOffset);
	draw.indexCount=indexCount; draw.layout=layout;
	draw.material=material; draw.useMaterial=true;
	draw.vertexOwner=vertices; draw.indexOwner=indices;
	return Submit_Native_Draw(renderer,draw);
}

inline void NativeTerrainSetCameraMatrices(NativeD3D12Renderer &renderer,
	CameraClass *camera, const Matrix3D &world)
{
	if (camera == NULL)
		return;
	Matrix3D view;
	Matrix4x4 projection;
	camera->Get_View_Matrix(&view);
	camera->Get_D3D_Projection_Matrix(&projection);
	// Engine matrices multiply column vectors; native HLSL consumes row vectors.
	const Matrix4x4 worldView = Matrix4x4(world).Transpose() * Matrix4x4(view).Transpose();
	const Matrix4x4 worldViewProjection = worldView * projection.Transpose();
	renderer.SetWorldView(reinterpret_cast<const float *>(&worldView));
	renderer.SetWorldViewProjection(reinterpret_cast<const float *>(&worldViewProjection));
}

inline void NativeTerrainSetMaterial(NativeD3D12Renderer &renderer,
	bool depthWrite, bool blend, D3D12_CULL_MODE cullMode,
	bool alphaTest = false, UINT8 alphaReference = 0)
{
	renderer.SetFixedFunctionState(cullMode, true, depthWrite,
		D3D12_COMPARISON_FUNC_LESS_EQUAL, blend,
		D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA,
		D3D12_BLEND_OP_ADD, D3D12_COLOR_WRITE_ENABLE_ALL);
	renderer.SetAlphaTestState(alphaTest,
		D3D12_COMPARISON_FUNC_GREATER_EQUAL, alphaReference);
	renderer.SetStencilState(false, D3D12_COMPARISON_FUNC_ALWAYS, 0, 0xff, 0xff,
		D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP);
	renderer.SetTreeSway(NULL, 0);
	renderer.SetLighting(NativeLightingState());
	renderer.SetGrayscale(false);
	renderer.SetMaterialEnabled(false);
	renderer.SetTextureCombine(true, true, true, true);
	renderer.SetSamplerState(NativeD3D12FilterMode::Linear,
		NativeD3D12FilterMode::Linear, NativeD3D12FilterMode::Linear,
		true, true);
}

inline bool NativeTerrainDrawBuffer(NativeD3D12Renderer &renderer,
	DX8VertexBufferClass *vertexBuffer, DX8IndexBufferClass *indexBuffer,
	Int indexOffset, Int indexCount, Int vertexCount, TextureClass *texture,
	Int vertexOffset = 0)
{
	if (vertexBuffer == NULL || indexBuffer == NULL || indexCount <= 0 ||
		vertexCount <= 0 || indexOffset < 0 || vertexOffset < 0)
		return false;

	const NativeD3D12UploadBuffer *nativeVertexBuffer =
		vertexBuffer->Get_Native_Vertex_Buffer();
	const NativeD3D12UploadBuffer *nativeIndexBuffer =
		indexBuffer->Get_Native_Index_Buffer();
	if (nativeVertexBuffer == NULL || nativeIndexBuffer == NULL)
		return false;

	const unsigned stride = vertexBuffer->FVF_Info().Get_FVF_Size();
	const size_t indexByteOffset = static_cast<size_t>(indexOffset) * sizeof(UnsignedShort);
	const size_t indexByteCount = static_cast<size_t>(indexCount) * sizeof(UnsignedShort);
	const size_t vertexByteOffset = static_cast<size_t>(vertexOffset) * stride;
	const size_t vertexByteCount = static_cast<size_t>(vertexCount) * stride;
	if (stride == 0 || vertexByteOffset > nativeVertexBuffer->Size() ||
		vertexByteCount > nativeVertexBuffer->Size() - vertexByteOffset ||
		indexByteOffset > nativeIndexBuffer->Size() ||
		indexByteCount > nativeIndexBuffer->Size() - indexByteOffset)
		return false;

	const auto &fvf = vertexBuffer->FVF_Info();
	const UINT colorOffset = (fvf.Get_FVF() & D3DFVF_DIFFUSE) != 0 ?
		fvf.Get_Diffuse_Offset() : UINT_MAX;
	const void *vertices = static_cast<const unsigned char *>(nativeVertexBuffer->Data()) + vertexByteOffset;
	const unsigned short *indices = reinterpret_cast<const unsigned short *>(
		static_cast<const unsigned char *>(nativeIndexBuffer->Data()) + indexByteOffset);
	const NativeD3D12Texture *nativeTexture = texture != NULL ?
		texture->Prepare_Native_Texture() : NULL;

	if (nativeTexture != NULL)
		return renderer.DrawIndexedTextured(vertices,
			static_cast<UINT>(vertexByteCount), stride,
			static_cast<UINT>(vertexCount), fvf.Get_Tex_Offset(0), indices,
			static_cast<UINT>(indexCount), D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
			nativeTexture, colorOffset, nativeVertexBuffer, nativeIndexBuffer);

	return renderer.DrawIndexed(vertices,
		static_cast<UINT>(vertexByteCount), stride,
		static_cast<UINT>(vertexCount), indices, static_cast<UINT>(indexCount),
		0, 0, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, colorOffset,
		nativeVertexBuffer, nativeIndexBuffer);
}
