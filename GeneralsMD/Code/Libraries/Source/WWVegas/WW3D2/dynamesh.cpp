/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*************************************************************************** 
 ***    C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S     *** 
 *************************************************************************** 
 *                                                                         * 
 *                 Project Name : Commando                                 * 
 *                                                                         * 
 *                     $Archive:: /Commando/Code/ww3d2/dynamesh.cpp       $* 
 *                                                                         * 
 *                      $Author:: Greg_h                                  $* 
 *                                                                         * 
 *                     $Modtime:: 12/03/01 4:50p                          $* 
 *                                                                         * 
 *                    $Revision:: 25                                      $* 
 *                                                                         * 
 *-------------------------------------------------------------------------* 
 * Functions:                                                              * 
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "dynamesh.h"
#include "dx8vertexbuffer.h"
#include "dx8indexbuffer.h"
#include "dx8wrapper.h"
#include "sortingrenderer.h"
#include "rinfo.h"
#include "camera.h"
#include "dx8fvf.h"
#include "native_mesh_geometry.h"
#include "native_light_environment.h"
#include "native_pipeline_description.h"
#include "mapper.h"
#include "scene.h"



/*
** DynamicMeshModel implementation
*/

DynamicMeshModel::DynamicMeshModel(unsigned int max_polys, unsigned int max_verts) :
	MeshGeometryClass(),
	DynamicMeshPNum(0),
	DynamicMeshVNum(0),
	MatDesc(NULL),
	MatInfo(NULL)
{
	MatInfo = NEW_REF(MaterialInfoClass, ());

	MatDesc =  W3DNEW MeshMatDescClass;
	MatDesc->Set_Polygon_Count(max_polys);
	MatDesc->Set_Vertex_Count(max_verts);

	Reset_Geometry(max_polys, max_verts);
}

DynamicMeshModel::DynamicMeshModel(unsigned int max_polys, unsigned int max_verts, MaterialInfoClass *mat_info) :
	MeshGeometryClass(),
	DynamicMeshPNum(0),
	DynamicMeshVNum(0),
	MatDesc(NULL),
	MatInfo(NULL)
{
	MatInfo = mat_info;
	MatInfo->Add_Ref();

	MatDesc = W3DNEW MeshMatDescClass;
	MatDesc->Set_Polygon_Count(max_polys);
	MatDesc->Set_Vertex_Count(max_verts);

	Reset_Geometry(max_polys, max_verts);
}

DynamicMeshModel::DynamicMeshModel(const DynamicMeshModel &src) :
	MeshGeometryClass(src),
	DynamicMeshPNum(src.DynamicMeshPNum),
	DynamicMeshVNum(src.DynamicMeshVNum),
	MatDesc(NULL),
	MatInfo(NULL)
{
	// Copy the material info structure.
	MatInfo = NEW_REF(MaterialInfoClass, (*(src.MatInfo)));


	// [SKB: Feb 21 2002 @ 11:47pm] :
	// Moved before the remapping cause I don't like referencing null.
	MatDesc = W3DNEW MeshMatDescClass;

	// remap!
	MaterialRemapperClass remapper(src.MatInfo, MatInfo);
	remapper.Remap_Mesh(src.MatDesc, MatDesc);
}

DynamicMeshModel::~DynamicMeshModel(void)
{
	if (MatDesc) {
		delete MatDesc;
		MatDesc = NULL;
	}
	REF_PTR_RELEASE(MatInfo);
}

void DynamicMeshModel::Compute_Plane_Equations(void)
{
	// Make sure the arrays are allocated before we do this
	get_vert_normals();
	Vector4 * planes = get_planes(true);

	// Set the poly and vertex counts to the dynamic counts, call the base class function, then
	// set them back.
	int old_poly_count = PolyCount;
	int old_vert_count = VertexCount;
	PolyCount = DynamicMeshPNum;
	VertexCount = DynamicMeshVNum;

	MeshGeometryClass::Compute_Plane_Equations(planes);

	PolyCount = old_poly_count;
	VertexCount = old_vert_count;
}

void DynamicMeshModel::Compute_Vertex_Normals(void)
{
	// Make sure the arrays are allocated before we do this
	Vector3 * vnorms = get_vert_normals();
	get_planes(true);

	// Set the poly and vertex counts to the dynamic counts, call the base class function, then
	// set them back.
	int old_poly_count = PolyCount;
	int old_vert_count = VertexCount;
	PolyCount = DynamicMeshPNum;
	VertexCount = DynamicMeshVNum;

	MeshGeometryClass::Compute_Vertex_Normals(vnorms);

	PolyCount = old_poly_count;
	VertexCount = old_vert_count;
}

void DynamicMeshModel::Compute_Bounds(Vector3 * verts)
{
	// Set the poly and vertex counts to the dynamic counts, call the base class function, then
	// set them back.
	int old_poly_count = PolyCount;
	int old_vert_count = VertexCount;
	PolyCount = DynamicMeshPNum;
	VertexCount = DynamicMeshVNum;

	MeshGeometryClass::Compute_Bounds(verts);

	PolyCount = old_poly_count;
	VertexCount = old_vert_count;
}

void DynamicMeshModel::Reset(void)
{
	Set_Counts(0, 0);
	int polycount = Get_Polygon_Count();
	int vertcount = Get_Vertex_Count();
	Reset_Geometry(polycount, vertcount);
	MatDesc->Reset(polycount, vertcount, 1);
	REF_PTR_RELEASE(MatInfo);
	MatInfo = NEW_REF(MaterialInfoClass, ());
}

void DynamicMeshModel::Render(RenderInfoClass & rinfo,const Matrix3D& world,SceneClass* scene)
{
	auto* native=NativeD3D12Renderer::Active();
	if (!native || !DynamicMeshVNum || !DynamicMeshPNum) return;
	// Process texture reductions:
//	MatInfo->Process_Texture_Reduction();

	unsigned buffer_type=(Get_Flag(MeshGeometryClass::SORT)&& WW3D::Is_Sorting_Enabled()) ? BUFFER_TYPE_DYNAMIC_SORTING : BUFFER_TYPE_DYNAMIC_DX8;

	/*
	** Write the vertex data to the vertex buffer. We assume the FVF contains positions, normals,
	** one texture channel, and the diffuse color channel (color0). If it does not contain all
	** these components, the code will fail.
	*/
	DynamicVBAccessClass dynamic_vb(buffer_type,dynamic_fvf_type,DynamicMeshVNum);
	const FVFInfoClass &fvf_info = dynamic_vb.FVF_Info();
	
	{ // scope for lock

		DynamicVBAccessClass::WriteLockClass lock(&dynamic_vb);
		unsigned char *vertices = (unsigned char*)lock.Get_Formatted_Vertex_Array();			
		const Vector3 *locs = Get_Vertex_Array();
		const Vector3 *normals = Get_Vertex_Normal_Array();
		const Vector2 *uvs = MatDesc->Get_UV_Array_By_Index(0, false);
		const Vector2 *uv1s = MatDesc->Get_UV_Array_By_Index(1, false);
		const unsigned *colors = MatDesc->Get_Color_Array(0, false);
		const static Vector3 default_normal(0.0f, 0.0f, 0.0f);
		const static Vector2 default_uv(0.0f, 0.0f);
		const unsigned int default_color = 0xFFFFFFFF;
		for (int i=0; i < DynamicMeshVNum; i++)
		{
			*(Vector3 *)(vertices + fvf_info.Get_Location_Offset()) = locs[i];
			*(Vector3 *)(vertices + fvf_info.Get_Normal_Offset()) = normals[i];
			if (uvs) {
				*(Vector2 *)(vertices + fvf_info.Get_Tex_Offset(0)) = uvs[i];
			} else {
				*(Vector2 *)(vertices + fvf_info.Get_Tex_Offset(0)) = default_uv;
			}
			if (uv1s) {
				*(Vector2 *)(vertices + fvf_info.Get_Tex_Offset(1)) = uv1s[i];
			} else {
				*(Vector2 *)(vertices + fvf_info.Get_Tex_Offset(1)) = default_uv;
			}

			if (colors) {
				*(unsigned int *)(vertices + fvf_info.Get_Diffuse_Offset()) = colors[i];
			} else {
				*(unsigned int *)(vertices + fvf_info.Get_Diffuse_Offset()) = default_color;
			}
			vertices += fvf_info.Get_FVF_Size();
		}			

	} // end scope for lock

	/*
	** Write index data to index buffers
	*/
	DynamicIBAccessClass dynamic_ib(buffer_type,DynamicMeshPNum * 3);
	const TriIndex *tris = Get_Polygon_Array();

	{ // scope for lock

		DynamicIBAccessClass::WriteLockClass lock(&dynamic_ib);
		unsigned short * indices = lock.Get_Index_Array();
		for (int i=0; i < DynamicMeshPNum; i++)
		{
			indices[i*3 + 0] = (unsigned short)tris[i][0];
			indices[i*3 + 1] = (unsigned short)tris[i][1];
			indices[i*3 + 2] = (unsigned short)tris[i][2];
		}

	} // end scope for lock

	const auto geometry=Describe_Native_Mesh_Geometry(dynamic_vb.Peek_Source_Buffer(),dynamic_ib.Peek_Source_Buffer(),
		dynamic_vb.Get_Native_Vertex_Offset(),DynamicMeshVNum);
	if (!geometry.Is_Valid()) return;
	NativeD3D12ScopedState restore(*native);
	const auto frame=native->CaptureState();
	Matrix3D view; Matrix4x4 projection;
	rinfo.Camera.Get_View_Matrix(&view); rinfo.Camera.Get_D3D_Projection_Matrix(&projection);
	NativeMapperContext context={Matrix4x4(world).Transpose()*Matrix4x4(view).Transpose(),Matrix4x4(view),projection};
	const Matrix4x4 wvp=context.worldView*projection.Transpose();
	native->SetWorldView(reinterpret_cast<const float*>(&context.worldView));
	native->SetWorldViewProjection(reinterpret_cast<const float*>(&wvp));
	native->SetTreeSway(nullptr,0); native->SetDepthBias(0);
	SphereClass sphere(Vector3(0,0,0),0); Get_Bounding_Sphere(&sphere);
	for (unsigned pass=0;pass<Get_Pass_Count();++pass) {
		auto* t0=MatDesc->Get_Texture_Array(pass,0,false);
		auto* t1=MatDesc->Get_Texture_Array(pass,1,false);
		auto* mats=MatDesc->Get_Material_Array(pass,false);
		const ShaderClass* shaders=MatDesc->Get_Shader_Array(pass,false);
		const auto textureAt=[&](unsigned stage,unsigned triangle) {
			auto* array=stage ? t1 : t0;
			return array ? array->Get_Array()[triangle] : MatDesc->Peek_Single_Texture(pass,stage);
		};
		const auto materialAt=[&](unsigned triangle) {
			return mats ? mats->Get_Array()[tris[triangle].I] : MatDesc->Peek_Single_Material(pass);
		};
		const auto shaderAt=[&](unsigned triangle) { return shaders ? shaders[triangle] : MatDesc->Get_Single_Shader(pass); };
		for (unsigned first=0;first<unsigned(DynamicMeshPNum);) {
			TextureClass* owners[2]={textureAt(0,first),textureAt(1,first)};
			VertexMaterialClass* vertexMaterial=materialAt(first);
			const ShaderClass shader=shaderAt(first);
			unsigned end=first+1;
			// Both UV stages delimit a run; ignoring stage one merges unlike materials.
			while (end<unsigned(DynamicMeshPNum) && textureAt(0,end)==owners[0] && textureAt(1,end)==owners[1] &&
				materialAt(end)==vertexMaterial && shaderAt(end)==shader) ++end;
			auto draw=geometry;
			draw.material=shader.Get_Native_Texture_Material(); draw.useMaterial=true;
			for (unsigned stage=0;stage<2;++stage) {
				draw.material.coordinates[stage].offset=draw.layout.Find_Offset(NativeVertexSemantic::TexCoord,stage);
				if (owners[stage] && WW3D::Is_Texturing_Enabled()) {
					draw.material.textures[stage]=owners[stage]->Prepare_Native_Texture();
					draw.material.samplers[stage]=owners[stage]->Get_Filter().Get_Native_Description();
				}
			}
			NativeLightingState lighting=frame.lighting;
			if (rinfo.light_environment) Describe_Native_Light_Environment(*rinfo.light_environment,context.view,lighting);
			if (vertexMaterial) vertexMaterial->Describe_Native_Lighting(lighting,WW3D::Is_Coloring_Enabled());
			else lighting.flags[0]=0;
			lighting.flags[1]=shader.Get_Secondary_Gradient()!=ShaderClass::SECONDARY_GRADIENT_DISABLE;
			native->SetLighting(lighting);
			auto pipeline=shader.Get_Native_Pipeline(ShaderClass::Is_Backface_Culling_Inverted());
			pipeline.colorMask &= frame.renderTargetWriteMask; pipeline.Apply(*native);
			if (scene && scene->Get_Fog_Enable() && shader.Get_Fog_Func()!=ShaderClass::FOG_DISABLE) {
				float start,finish; scene->Get_Fog_Range(&start,&finish);
				const Vector3& c=scene->Get_Fog_Color();
				UINT32 fog=DX8Wrapper::Convert_Color_Clamp(Vector4(c.X,c.Y,c.Z,1));
				if (shader.Get_Fog_Func()==ShaderClass::FOG_WHITE) fog=0xffffff;
				if (shader.Get_Fog_Func()==ShaderClass::FOG_SCALE_FRAGMENT) fog=0;
				native->SetVertexFog(3,start,finish,0,fog,frame.fogRange);
			} else native->SetVertexFog(0,0,1,0,0,false);
			const bool mapped=!vertexMaterial || vertexMaterial->Describe_Native_Mapping(context,draw.layout,draw.material);
			if (mapped && Describe_Native_Indexed_Range(draw,dynamic_ib.Get_Native_Index_Offset()+first*3,
				(end-first)*3,0,0,DynamicMeshVNum,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST)) {
				const bool submitted=buffer_type==BUFFER_TYPE_DYNAMIC_SORTING ?
					SortingRendererClass::Insert_Native_Draw(draw,native->CaptureState(),dynamic_vb.Peek_Source_Buffer(),
						dynamic_ib.Peek_Source_Buffer(),dynamic_vb.Get_Native_Vertex_Offset(),owners,2,&sphere) :
					Submit_Native_Draw(*native,draw);
				WWASSERT(submitted);
			}
			first=end;
		}
	}
}

void DynamicMeshModel::Initialize_Texture_Array(int pass, int stage, TextureClass *texture)
{
	TexBufferClass * texlist = MatDesc->Get_Texture_Array(pass, 0, true);
	for (int lp = 0; lp < PolyCount; lp++) {
		texlist->Set_Element(lp, texture);
	}
}

void DynamicMeshModel::Initialize_Material_Array(int pass, VertexMaterialClass *vmat)
{
	MatBufferClass * vertmatlist = MatDesc->Get_Material_Array(pass, true);
	for (int lp = 0; lp < VertexCount; lp++) {
		vertmatlist->Set_Element(lp, vmat);
	}
}

void DynamicMeshClass::Render(RenderInfoClass & rinfo)
{
	if (Is_Not_Hidden_At_All() == false)	return;	

	// test for an empty mesh..
	if (PolyCount == 0 ) return;

	// If static sort lists are enabled and this mesh has a sort level, put it on the list instead
	// of rendering it.

	if (WW3D::Are_Static_Sort_Lists_Enabled() && SortLevel != SORT_LEVEL_NONE) {

		WW3D::Add_To_Static_Sort_List(this, SortLevel);

	} else {

		const FrustumClass & frustum = rinfo.Camera.Get_Frustum();

		if (CollisionMath::Overlap_Test(frustum, Get_Bounding_Box()) != CollisionMath::OUTSIDE) {
			Model->Render(rinfo,Transform,Peek_Scene());
		}
	}
}

bool DynamicMeshClass::End_Vertex()
{
	// check that we have room for a new vertex
	WWASSERT(VertCount < Model->Get_Vertex_Count());

	// if we are a multi-material object record the material
	int pass = Get_Pass_Count();
	while (pass--) {
		if (MultiVertexMaterial[pass]) {
			VertexMaterialClass *mat = Peek_Material_Info()->Get_Vertex_Material(VertexMaterialIdx[pass]);
			Model->Set_Material(VertCount, mat, pass);
			REF_PTR_RELEASE(mat);
		}

	}

	// if we are multi colored, record the color
	for (int color_array_index = 0; color_array_index < MAX_COLOR_ARRAYS; color_array_index++) {
		if (MultiVertexColor[color_array_index]) {
//			Vector4 * color = &((Model->Get_Color_Array(color_array_index))[VertCount]);
//			color->X = CurVertexColor[color_array_index].X;
//			color->Y = CurVertexColor[color_array_index].Y;
//			color->Z = CurVertexColor[color_array_index].Z;
//			color->W = CurVertexColor[color_array_index].W;
			unsigned * color = &((Model->Get_Color_Array(color_array_index))[VertCount]);
			*color=DX8Wrapper::Convert_Color_Clamp(CurVertexColor[color_array_index]);
		}
	}

	// mark this vertex as being complete
	VertCount++;
	TriVertexCount++;

	// if we have 3 or more vertices, add a new poly
	if (TriVertexCount >= 3) {
	
		// check that we have room for a new poly
		WWASSERT(PolyCount < Model->Get_Polygon_Count());

		// set vertex indices
		TriIndex *poly = &(Model->Get_Non_Const_Polygon_Array())[PolyCount];
		if (TriMode == TRI_MODE_STRIPS) {
			(*poly)[0] = VertCount-3;
			(*poly)[1] = VertCount-2;
			(*poly)[2] = VertCount-1;

			// for every other tri, reverse vertex order
			if (Flip_Face()) {
				(*poly)[1] = VertCount-1;
				(*poly)[2] = VertCount-2;
			}
		} else {
			(*poly)[0] = FanVertex;
			(*poly)[1] = VertCount-2;
			(*poly)[2] = VertCount-1;
		}

		// check each pass
		int pass = Get_Pass_Count();
		while (pass--) {

			// If we are multi texture
			if (MultiTexture[pass]) {
				TextureClass *tex = Peek_Material_Info()->Get_Texture(TextureIdx[pass]);
				Model->Set_Texture(PolyCount, tex, pass);
				REF_PTR_RELEASE(tex);
			}
		}

		// increase the count and record that we have a new material
		PolyCount++;
		Model->Set_Counts(PolyCount, VertCount);
	}
	return true;
}

/******************************************************************
**
** DynamicMeshClass
**
*******************************************************************/
DynamicMeshClass::DynamicMeshClass(int max_poly, int max_vert) :
	Model(NULL),
	PolyCount(0),
	VertCount(0),
	TriVertexCount(0),
	FanVertex(0),
	TriMode(TRI_MODE_STRIPS),
	SortLevel(SORT_LEVEL_NONE)
{
	int pass = MAX_PASSES;
	while (pass--) {
		MultiTexture[pass] = false;
		TextureIdx[pass] = -1;

		MultiVertexMaterial[pass] = false;
		VertexMaterialIdx[pass] = -1;
	}

	for (int color_array_index = 0; color_array_index < MAX_COLOR_ARRAYS; color_array_index++) {
		MultiVertexColor[color_array_index] = false;
		CurVertexColor[color_array_index].Set(1.0f, 1.0f, 1.0f, 1.0f);
	}

	Model = NEW_REF(DynamicMeshModel, (max_poly, max_vert));
}

DynamicMeshClass::DynamicMeshClass(int max_poly, int max_vert, MaterialInfoClass *mat_info) :
	Model(NULL),
	PolyCount(0),
	VertCount(0),
	TriVertexCount(0),
	FanVertex(0),
	TriMode(TRI_MODE_STRIPS),
	SortLevel(SORT_LEVEL_NONE)
{
	int pass = MAX_PASSES;
	while (pass--) {
		MultiTexture[pass] = false;
		TextureIdx[pass] = -1;

		MultiVertexMaterial[pass] = false;
		VertexMaterialIdx[pass] = -1;
	}

	for (int color_array_index = 0; color_array_index < MAX_COLOR_ARRAYS; color_array_index++) {
		MultiVertexColor[color_array_index] = false;
		CurVertexColor[color_array_index].Set(1.0f, 1.0f, 1.0f, 1.0f);
	}

	Model = NEW_REF(DynamicMeshModel, (max_poly, max_vert, mat_info));
}

DynamicMeshClass::DynamicMeshClass(const DynamicMeshClass & src) :
	RenderObjClass(src),
	Model(NULL),
	PolyCount(src.PolyCount),
	VertCount(src.VertCount),
	TriVertexCount(src.TriVertexCount),
	FanVertex(src.FanVertex),
	TriMode(src.TriMode),
	SortLevel(src.SortLevel)
{
	int pass = MAX_PASSES;
	while (pass--) {
		MultiTexture[pass] = src.MultiTexture[pass];
		TextureIdx[pass] = src.TextureIdx[pass];

		MultiVertexMaterial[pass] = src.MultiVertexMaterial[pass];
		VertexMaterialIdx[pass] = src.VertexMaterialIdx[pass];

		MultiVertexColor[pass] = src.MultiVertexColor[pass];
		CurVertexColor[pass]  = src.CurVertexColor[pass];
	}

	for (int color_array_index = 0; color_array_index < MAX_COLOR_ARRAYS; color_array_index++) {
		MultiVertexColor[color_array_index] = src.MultiVertexColor[color_array_index];
		CurVertexColor[color_array_index] = src.CurVertexColor[color_array_index];
	}

	Model = NEW_REF(DynamicMeshModel, (*(src.Model)));
}

void DynamicMeshClass::Resize(int max_polys, int max_verts)
{
	Reset();

	REF_PTR_RELEASE(Model);
	Model = NEW_REF(DynamicMeshModel, (max_polys, max_verts));

	// reset all the texture & vertex material indices
	int pass = MAX_PASSES;
	while (pass--) {
		TextureIdx[pass] = -1;
		VertexMaterialIdx[pass] = -1;
		MultiVertexMaterial[pass] = false;
	}
}

DynamicMeshClass::~DynamicMeshClass()
{	
	REF_PTR_RELEASE(Model);
}

RenderObjClass * DynamicMeshClass::Clone(void) const
{
	return NEW_REF(DynamicMeshClass, (*this));
}

void DynamicMeshClass::Location(float x, float y, float z)
{
	Vector3 * loc = Model->Get_Vertex_Array();
	assert(loc);

	loc[VertCount].X = x;
	loc[VertCount].Y = y;
	loc[VertCount].Z = z;
}

/*
** For moving a vertex after the DynaMesh has already been created.
*/
void DynamicMeshClass::Move_Vertex(int index, float x, float y, float z)
{
	Vector3 * loc = Model->Get_Vertex_Array();
	assert(loc);
	loc[index][0] = x;
	loc[index][1] = y;
	loc[index][2] = z;
}	 

/*
** Get a vertex value.
*/
void DynamicMeshClass::Get_Vertex(int index, float &x, float &y, float &z)
{
	Vector3 * loc = Model->Get_Vertex_Array();
	assert(loc);
	x = loc[index][0];
	y = loc[index][1];
	z = loc[index][2];
}


/*
** Offset the entire mesh
*/
void DynamicMeshClass::Translate_Vertices(const Vector3 & offset)
{
	Vector3 * loc = Model->Get_Vertex_Array();
	assert(loc);
	for (int i=0; i < Get_Num_Vertices(); i++) {
		loc[i].X += offset.X;
		loc[i].Y += offset.Y;
		loc[i].Z += offset.Z;
	}
	
	Set_Dirty_Bounds();
	Set_Dirty_Planes();
}

int DynamicMeshClass::Set_Vertex_Material(int idx, int pass)
{
	assert(idx < Peek_Material_Info()->Vertex_Material_Count());
	VertexMaterialIdx[pass] = idx;
	if (!MultiVertexMaterial[pass]) {
		// WWASSERT( VertexMaterialIdx[pass] == 0);
		VertexMaterialClass *mat = Peek_Material_Info()->Get_Vertex_Material(VertexMaterialIdx[pass]);
		Model->Set_Single_Material(mat, pass);
		mat->Release_Ref();
	}
	return VertexMaterialIdx[pass];
}

int DynamicMeshClass::Set_Vertex_Material(VertexMaterialClass *material, bool dont_search, int pass)
{
	// Check if same as the last vertex material
	if (Peek_Material_Info()->Vertex_Material_Count() && (VertexMaterialIdx[pass] != -1) && Peek_Material_Info()->Peek_Vertex_Material(VertexMaterialIdx[pass]) == material) {
		return VertexMaterialIdx[pass];
	}

	// if there are vertex materials in the list then we may have just jumped
	// to becoming a multi-vertex-material object.  Take care of that here.
	if ((!MultiVertexMaterial[pass]) && Peek_Material_Info()->Vertex_Material_Count() && (VertexMaterialIdx[pass] != -1) && Peek_Material_Info()->Peek_Vertex_Material(VertexMaterialIdx[pass]) != material) {

		// allocate the array of per-vertex vertex material overrides
		VertexMaterialClass *mat = Peek_Material_Info()->Get_Vertex_Material(VertexMaterialIdx[pass]);
		Model->Initialize_Material_Array(pass, mat);
		mat->Release_Ref();

		// flag that we need to write the per -vertex vertex material override array
		MultiVertexMaterial[pass] = true;
	}

	// add the material to the material info class if we cant find it in the 
	// list.  if we are not supposed to search the list for it then just add
	// it.
	if (!dont_search) {
		int found = 0;
		for (int lp = 0; lp < Peek_Material_Info()->Vertex_Material_Count(); lp ++) {
			VertexMaterialClass *mat = Peek_Material_Info()->Get_Vertex_Material(lp);
			if (material == mat) {
				VertexMaterialIdx[pass] = lp;
				found = true;
				mat->Release_Ref();
				break;
			}
			mat->Release_Ref();
		}
		if (!found) {
			Peek_Material_Info()->Add_Vertex_Material(material);
			VertexMaterialIdx[pass] = Peek_Material_Info()->Vertex_Material_Count() - 1;
		}
	} else {
		Peek_Material_Info()->Add_Vertex_Material(material);
		VertexMaterialIdx[pass] = Peek_Material_Info()->Vertex_Material_Count() - 1;
	}

	if (!MultiVertexMaterial[pass]) {
		Model->Set_Single_Material(Peek_Material_Info()->Peek_Vertex_Material(VertexMaterialIdx[pass]), pass);
	}
	return(VertexMaterialIdx[pass]);
}

int DynamicMeshClass::Set_Texture(int idx, int pass)
{
	WWASSERT(idx < Peek_Material_Info()->Texture_Count());
	TextureIdx[pass] = idx;
	if (!MultiTexture[pass]) {
		TextureClass *tex = Peek_Material_Info()->Get_Texture(TextureIdx[pass]);
		Model->Set_Single_Texture(tex, pass);
		tex->Release_Ref();
	}
	return TextureIdx[pass];
}

int DynamicMeshClass::Set_Texture(TextureClass *texture, bool dont_search, int pass)
{
	// Check if same as the last texture
	if (Peek_Material_Info()->Texture_Count() && (TextureIdx[pass] != -1) && Peek_Material_Info()->Peek_Texture(TextureIdx[pass]) == texture) {
		return TextureIdx[pass];
	}

	// if there are textures in the list then we may have just jumped
	// to becoming a multi-texture object.  Take care of that here.
	if ((!MultiTexture[pass]) && Peek_Material_Info()->Texture_Count() && (TextureIdx[pass] != -1) && Peek_Material_Info()->Peek_Texture(TextureIdx[pass]) != texture) {

		// allocate the array of per polygon material over-rides
		TextureClass *tex = Peek_Material_Info()->Get_Texture(TextureIdx[pass]);
		Model->Initialize_Texture_Array(pass, 0, tex);
		tex->Release_Ref();

		// flag that we need to write the per polygon material overide array
		MultiTexture[pass] = true;
	}

	// add the material to the material info class if we cant find it in the 
	// list.  if we are not supposed to search the list for it then just add
	// it.
	if (!dont_search) {
		int found = 0;
		for (int lp = 0; lp < Peek_Material_Info()->Texture_Count(); lp ++) {
			TextureClass *tex = Peek_Material_Info()->Get_Texture(lp);
			if (texture == tex) {
				TextureIdx[pass] = lp;
				found = true;
				tex->Release_Ref();
				break;
			}
			tex->Release_Ref();
		}
		if (!found) {
			Peek_Material_Info()->Add_Texture(texture);
			TextureIdx[pass] = Peek_Material_Info()->Texture_Count() - 1;
		}
	} else {
		Peek_Material_Info()->Add_Texture(texture);
		TextureIdx[pass] = Peek_Material_Info()->Texture_Count() - 1;
	}

	if (!MultiTexture[pass]) {
		TextureClass *tex = Peek_Material_Info()->Get_Texture(TextureIdx[pass]);
		Model->Set_Single_Texture(tex, pass);
		tex->Release_Ref();
	}
	return(TextureIdx[pass]);
}

/*
**
*/
// Remap locations to match a screen
void DynamicScreenMeshClass::Location( float x, float y, float z)	
{	
	DynamicMeshClass::Location( (x * 2) - 1, Aspect - (y * 2 * Aspect), 0); 
}

// For moving a vertex after the DynaMesh has already been created.
void DynamicScreenMeshClass::Move_Vertex(int index, float x, float y, float z)	
{	
	DynamicMeshClass::Move_Vertex( index, (x * 2) - 1, Aspect - (y * 2 * Aspect), 0); 
}

// Set position
void DynamicScreenMeshClass::Set_Position(const Vector3 &v)	
{ 
	DynamicMeshClass::Set_Position(Vector3(v.X * 2, -(v.Y * 2 * Aspect), 0)); 
}

void DynamicScreenMeshClass::Reset( void )		
{	
	Reset_Flags();	
	Reset_Mesh_Counters();	
}


