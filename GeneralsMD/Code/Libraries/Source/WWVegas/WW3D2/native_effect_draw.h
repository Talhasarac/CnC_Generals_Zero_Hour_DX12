#pragma once
#include "native_draw_state.h"
#include "native_pipeline_description.h"
#include "dx8wrapper.h"
#include "sortingrenderer.h"
#include "dx8vertexbuffer.h"
#include "dx8indexbuffer.h"
#include "camera.h"
#include "texture.h"
#include "shader.h"
#include "vertmaterial.h"
#include "mapper.h"

inline bool Submit_Native_Effect(CameraClass& camera,const ShaderClass& shader,TextureClass* texture,
	const DynamicVBAccessClass& vertices,IndexBufferClass* indices,UINT firstIndex,UINT triangles,UINT count,bool viewSpace,
	const SphereClass* sphere=nullptr,const Matrix3D* transform=nullptr,VertexMaterialClass* vertexMaterial=nullptr)
{
	auto* native=NativeD3D12Renderer::Active();
	if (!native || !indices || !triangles || !count || firstIndex>indices->Get_Index_Count() ||
		triangles>(indices->Get_Index_Count()-firstIndex)/3 || count>vertices.Get_Vertex_Count()) return false;
	NativeD3D12ScopedState restore(*native);
	const auto frame=native->CaptureState();
	Matrix3D view; Matrix4x4 projection;
	camera.Get_View_Matrix(&view); camera.Get_D3D_Projection_Matrix(&projection);
	const Matrix4x4 worldView=(transform ? Matrix4x4(*transform).Transpose() : Matrix4x4(true))*
		(viewSpace ? Matrix4x4(true) : Matrix4x4(view).Transpose());
	const Matrix4x4 wvp=worldView*projection.Transpose();
	native->SetWorldView(reinterpret_cast<const float*>(&worldView));
	native->SetWorldViewProjection(reinterpret_cast<const float*>(&wvp));
	NativeLightingState lighting;
	if (vertexMaterial) vertexMaterial->Describe_Native_Lighting(lighting,WW3D::Is_Coloring_Enabled());
	lighting.flags[1]=shader.Get_Secondary_Gradient()!=ShaderClass::SECONDARY_GRADIENT_DISABLE;
	native->SetLighting(lighting); native->SetTreeSway(nullptr,0); native->SetDepthBias(0);
	if (shader.Get_Fog_Func()==ShaderClass::FOG_DISABLE) native->SetVertexFog(0,0,1,0,0,false);
	auto pipeline=shader.Get_Native_Pipeline(ShaderClass::Is_Backface_Culling_Inverted());
	pipeline.colorMask &= frame.renderTargetWriteMask; pipeline.Apply(*native);
	const auto layout=vertices.FVF_Info().Build_Native_Layout();
	auto material=shader.Get_Native_Texture_Material();
	material.coordinates[0].offset=layout.Find_Offset(NativeVertexSemantic::TexCoord);
	if (texture && WW3D::Is_Texturing_Enabled()) {
		material.textures[0]=texture->Prepare_Native_Texture();
		material.samplers[0]=texture->Get_Filter().Get_Native_Description();
		if (!material.textures[0]) return false;
	}
	const NativeMapperContext mapping={worldView,Matrix4x4(view),projection};
	if (vertexMaterial && !vertexMaterial->Describe_Native_Mapping(mapping,layout,material)) return false;
	if (vertices.Get_Type()==BUFFER_TYPE_DYNAMIC_SORTING) {
		SortingRendererClass::Insert_Native_Triangles(vertices,indices,firstIndex,triangles,count,native->CaptureState(),material,texture,sphere);
		return true;
	}
	const auto* vb=vertices.Get_Native_Vertex_Buffer();
	if (indices->Type()!=BUFFER_TYPE_DX8) return false;
	const auto* ib=static_cast<DX8IndexBufferClass*>(indices)->Get_Native_Index_Buffer();
	const size_t vo=size_t(vertices.Get_Native_Vertex_Offset())*layout.stride;
	const size_t io=size_t(firstIndex)*2;
	if (!vb || !ib || vo>vb->Size() || size_t(count)*layout.stride>vb->Size()-vo ||
		io>ib->Size() || size_t(triangles)*6>ib->Size()-io) return false;
	NativeDrawSubmission draw;
	draw.vertices=static_cast<const unsigned char*>(vb->Data())+vo;
	draw.vertexBytes=count*layout.stride; draw.vertexStride=layout.stride; draw.vertexCount=count;
	draw.indices=reinterpret_cast<const unsigned short*>(static_cast<const unsigned char*>(ib->Data())+io);
	draw.indexCount=triangles*3; draw.layout=layout; draw.useMaterial=true; draw.material=material;
	draw.vertexOwner=vb; draw.indexOwner=ib;
	return Submit_Native_Draw(*native,draw);
}
