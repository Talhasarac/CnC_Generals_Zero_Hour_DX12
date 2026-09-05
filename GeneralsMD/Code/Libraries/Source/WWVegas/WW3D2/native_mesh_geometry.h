#pragma once

#include "dx8wrapper.h" // Engine buffer-storage tags, not a rendering dependency.
#include "dx8vertexbuffer.h"
#include "dx8indexbuffer.h"
#include "native_draw_state.h"

// Expose either immutable CPU sorting storage or native upload storage without
// installing global buffer bindings. Queued callers must retain the source refs.
inline NativeDrawSubmission Describe_Native_Mesh_Geometry(VertexBufferClass* vertices,
	IndexBufferClass* indices,unsigned vertexOffset,unsigned vertexCount)
{
	NativeDrawSubmission draw;
	if (!vertices || !indices || vertexOffset>vertices->Get_Vertex_Count() ||
		vertexCount>vertices->Get_Vertex_Count()-vertexOffset) return draw;
	draw.layout=vertices->FVF_Info().Build_Native_Layout();
	draw.vertexStride=draw.layout.stride;
	draw.vertexCount=vertexCount;
	draw.vertexBytes=vertexCount*draw.vertexStride;
	draw.indexCount=indices->Get_Index_Count();
	if (vertices->Type()==BUFFER_TYPE_SORTING && indices->Type()==BUFFER_TYPE_SORTING) {
		draw.vertices=static_cast<SortingVertexBufferClass*>(vertices)->Get_Vertex_Array()+vertexOffset;
		draw.indices=static_cast<SortingIndexBufferClass*>(indices)->Get_Index_Array();
	} else if (vertices->Type()==BUFFER_TYPE_DX8 && indices->Type()==BUFFER_TYPE_DX8) {
		draw.vertexOwner=static_cast<DX8VertexBufferClass*>(vertices)->Get_Native_Vertex_Buffer();
		draw.indexOwner=static_cast<DX8IndexBufferClass*>(indices)->Get_Native_Index_Buffer();
		if (!draw.vertexOwner || !draw.indexOwner) return NativeDrawSubmission();
		draw.vertices=static_cast<const unsigned char*>(draw.vertexOwner->Data())+size_t(vertexOffset)*draw.vertexStride;
		draw.indices=static_cast<const unsigned short*>(draw.indexOwner->Data());
	}
	return draw;
}
