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

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : ww3d                                                         *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/sortingrenderer.cpp                    $*
 *                                                                                             *
 *              Original Author:: Greg Hjelstrom                                               *
 *                                                                                             *
 *                       Author : Kenny Mitchell                                               * 
 *                                                                                             * 
 *                     $Modtime:: 06/27/02 1:27p                                              $*
 *                                                                                             *
 *                    $Revision:: 2                                                           $*
 *                                                                                             *
 * 06/26/02 KM Matrix name change to avoid MAX conflicts                                       *
 * 06/27/02 KM Changes to max texture stage caps																*
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "sortingrenderer.h"
#include "dx8vertexbuffer.h"
#include "dx8indexbuffer.h"
#include "dx8wrapper.h"
#include "vertmaterial.h"
#include "texture.h"
#include "native_draw_state.h"
#include "native_mesh_geometry.h"
#include "d3d8.h"
#include "statistics.h"
#include <wwprofile.h>
#include <algorithm>

#ifdef _INTERNAL
// for occasional debugging...
// #pragma optimize("", off)
// #pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

bool SortingRendererClass::_EnableTriangleDraw=true;
static unsigned DEFAULT_SORTING_POLY_COUNT = 16384;	// (count * 3) must be less than 65536
static unsigned DEFAULT_SORTING_VERTEX_COUNT = 32768;	// count must be less than 65536

void SortingRendererClass::SetMinVertexBufferSize( unsigned val )
{
	DEFAULT_SORTING_VERTEX_COUNT = val;
	DEFAULT_SORTING_POLY_COUNT = val/2;	//typically have 2:1 vertex:triangle ratio.
}

struct ShortVectorIStruct
{
	unsigned short i;
	unsigned short j;
	unsigned short k;
};

struct TempIndexStruct
{
	ShortVectorIStruct tri;
	unsigned short idx;
	float z;
};

bool operator <(const TempIndexStruct &l, const TempIndexStruct &r) { return l.z < r.z; }
bool operator <=(const TempIndexStruct &l, const TempIndexStruct &r) { return l.z <= r.z; }
bool operator >(const TempIndexStruct &l, const TempIndexStruct &r) { return l.z > r.z; }
bool operator >=(const TempIndexStruct &l, const TempIndexStruct &r) { return l.z >= r.z; }
bool operator ==(const TempIndexStruct &l, const TempIndexStruct &r) { return l.z == r.z; }
// ----------------------------------------------------------------------------
static
void InsertionSort(TempIndexStruct *begin, TempIndexStruct *end)
{
	for (TempIndexStruct *iter = begin + 1; iter < end; ++iter) {
		TempIndexStruct val = iter[0];
		TempIndexStruct *insert = iter;
		while (insert != begin && insert[-1] > val) {
			insert[0] = insert[-1];
			insert -= 1;
		}
		insert[0] = val;
	}
}

// ----------------------------------------------------------------------------
static
void Sort(TempIndexStruct *begin, TempIndexStruct *end)
{
	const int diff = end - begin;
	if (diff <= 16) {
		// Insertion sort has less overhead for small arrays
		InsertionSort(begin, end);
	} else {
		// Choose the median of begin, mid, and (end - 1) as the partitioning element.
		// Rearrange so that *(begin + 1) <= *begin <= *(end - 1).  These will be guard
		// elements.
		TempIndexStruct *mid = begin + diff/2;
		std::swap(mid[0], begin[1]);
		if (begin[1] > end[-1]) {
			std::swap(begin[1], end[-1]);
		}
		if (begin[0] > end[-1]) {
			std::swap(begin[0], end[-1]);
		}																// end[-1] has the largest element
		if (begin[1] > begin[0]) {
			std::swap(begin[1], begin[0]);
		}																// begin[0] has the middle element and begin[1] has the smallest element

		// *begin is now the partitioning element
		TempIndexStruct *begin1 = begin + 1;	// TODO: Temp fix until I find out who is passing me NaN
		TempIndexStruct *end1 = end - 1;			// TODO: Temp fix until I find out who is passing me NaN
		TempIndexStruct *left = begin + 1;
		TempIndexStruct *right = end - 1;
		for (;;) {
#if 0		// TODO: Temp fix until I find out who is passing me NaN.
			do ++left; while (left[0] < begin[0]);		// Scan up to find element >= than partition
			do --right; while (right[0] > begin[0]);	// Scan down to find element <= than partition
#else
			do ++left; while (left < end1 && left[0] < begin[0]);		// Scan up to find element >= than partition
			do --right; while (right > begin1 && right[0] > begin[0]);	// Scan down to find element <= than partition
#endif
			if (right < left) break;									// Pointers crossed.  Partitioning completed.
			std::swap(left[0], right[0]);							// Exchange elements.
		}
		std::swap(begin[0], right[0]);							// Insert partition element

		// Sort the smaller subarray first then the larger
		if (right - begin > end - (right + 1)) {
			Sort(right + 1, end);
			Sort(begin, right);
		} else {
			Sort(begin, right);
			Sort(right + 1, end);
		}
	}
}

// ----------------------------------------------------------------------------

class SortingNodeStruct : public DLNodeClass<SortingNodeStruct>
{
	W3DMPO_GLUE(SortingNodeStruct)

public:
	RenderStateStruct sorting_state;
	NativeD3D12State native_state;
	NativeMaterialDescription native_material;
	bool has_native_state=false;
	void Capture_Native(const NativeD3D12State& state,const NativeMaterialDescription& material) {
		native_state=state; native_material=material; has_native_state=true;
		// TextureClass can replace its native allocation before a delayed flush.
		// Keep the exact allocation described by this draw, not just its asset owner.
		for (unsigned stage=0;stage<4;++stage) {
			native_state.materialTextures[stage].reset(material.textures[stage] ? material.textures[stage]->ShareResource() : nullptr);
			native_material.textures[stage]=native_state.materialTextures[stage].get();
		}
	}

	SphereClass bounding_sphere;

	Vector3 transformed_center;
	unsigned short start_index;			// First index used in the ib
	unsigned short polygon_count;			// Polygon count to process (3 indices = one polygon)
	unsigned short min_vertex_index;		// First index used in the vb
	unsigned short vertex_count;			// Number of vertices used in vb
};

static DLListClass<SortingNodeStruct> sorted_list;
static DLListClass<SortingNodeStruct> clean_list;
static unsigned total_sorting_vertices;

static SortingNodeStruct* Get_Sorting_Struct()
{

	SortingNodeStruct* state=clean_list.Head();
	if (state) {
		state->Remove();
		return state;
	}
	state=W3DNEW SortingNodeStruct();
	return state;
}

bool SortingRendererClass::Insert_Native_Draw(const NativeDrawSubmission& draw,const NativeD3D12State& nativeState,
	VertexBufferClass* vertices,IndexBufferClass* indices,unsigned vertexOffset,
	TextureClass* const* textures,unsigned textureCount,const SphereClass* sphere)
{
	if (!draw.Is_Valid() || !vertices || !indices || vertices->Type()!=BUFFER_TYPE_SORTING ||
		indices->Type()!=BUFFER_TYPE_SORTING || draw.topology!=D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST ||
		draw.indexCount%3 || textureCount>4 || (textureCount && !textures) ||
		draw.vertexStride!=sizeof(VertexFormatXYZNDUV2) ||
		vertexOffset>vertices->Get_Vertex_Count() || draw.vertexCount>vertices->Get_Vertex_Count()-vertexOffset) return false;
	const auto* source=static_cast<SortingIndexBufferClass*>(indices)->Get_Index_Array();
	const uintptr_t address=reinterpret_cast<uintptr_t>(draw.indices),begin=reinterpret_cast<uintptr_t>(source);
	if (address<begin || (address-begin)%sizeof(unsigned short)) return false;
	const size_t first=(address-begin)/sizeof(unsigned short);
	if (first>indices->Get_Index_Count() || draw.indexCount>indices->Get_Index_Count()-first) return false;
	if (draw.vertices!=static_cast<SortingVertexBufferClass*>(vertices)->Get_Vertex_Array()+vertexOffset) return false;
	unsigned minVertex=draw.vertexCount,maxVertex=0;
	for (unsigned i=0;i<draw.indexCount;++i) {
		const unsigned index=draw.indices[i];
		if (index>=draw.vertexCount-draw.baseVertex) return false;
		minVertex=(std::min)(minVertex,index); maxVertex=(std::max)(maxVertex,index);
	}
	auto* node=Get_Sorting_Struct();
	REF_PTR_SET(node->sorting_state.vertex_buffers[0],vertices);
	REF_PTR_SET(node->sorting_state.index_buffer,indices);
	for (unsigned i=0;i<textureCount;++i) REF_PTR_SET(node->sorting_state.Textures[i],textures[i]);
	node->sorting_state.vertex_buffer_types[0]=BUFFER_TYPE_SORTING;
	node->sorting_state.index_buffer_type=BUFFER_TYPE_SORTING;
	node->sorting_state.vba_offset=vertexOffset; node->sorting_state.vba_count=draw.vertexCount;
	node->sorting_state.iba_offset=0; node->sorting_state.index_base_offset=draw.baseVertex;
	std::memcpy(&node->sorting_state.world,nativeState.worldView.data(),sizeof(Matrix4x4));
	node->sorting_state.view=Matrix4x4(true);
	node->Capture_Native(nativeState,draw.material);
	node->bounding_sphere=sphere ? *sphere : SphereClass(Vector3(0,0,0),0);
	// Match the existing row-vector depth convention used by triangle sorting.
	const Matrix4x4& m=node->sorting_state.world;
	const Vector3& c=node->bounding_sphere.Center;
	node->transformed_center=Vector3(c.X*m[0][0]+c.Y*m[1][0]+c.Z*m[2][0]+m[3][0],
		c.X*m[0][1]+c.Y*m[1][1]+c.Z*m[2][1]+m[3][1],
		c.X*m[0][2]+c.Y*m[1][2]+c.Z*m[2][2]+m[3][2]);
	node->start_index=static_cast<unsigned short>(first); node->polygon_count=draw.indexCount/3;
	node->min_vertex_index=minVertex; node->vertex_count=maxVertex-minVertex+1;
	SortingNodeStruct* next=sorted_list.Head();
	while (next && node->transformed_center.Z<=next->transformed_center.Z) next=next->Succ();
	if (next) {
		if (sorted_list.Head()==sorted_list.Tail()) sorted_list.Add_Head(node);
		else node->Insert_Before(next);
	} else sorted_list.Add_Tail(node);
	return true;
}

void SortingRendererClass::Insert_Native_Triangles(const DynamicVBAccessClass& vertices,
	IndexBufferClass* indices,unsigned short firstIndex,unsigned short polygonCount,unsigned short vertexCount,
	const NativeD3D12State& state,const NativeMaterialDescription& material,TextureBaseClass* textureOwner,
	const SphereClass* sphere)
{
	auto draw=Describe_Native_Mesh_Geometry(vertices.Peek_Source_Buffer(),indices,
		vertices.Get_Native_Vertex_Offset(),vertexCount);
	if (!Describe_Native_Indexed_Range(draw,firstIndex,unsigned(polygonCount)*3,0,0,vertexCount,
		D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST)) { WWASSERT(false); return; }
	draw.material=material; draw.useMaterial=true;
	TextureClass* texture=static_cast<TextureClass*>(textureOwner);
	const bool inserted=Insert_Native_Draw(draw,state,vertices.Peek_Source_Buffer(),indices,
		vertices.Get_Native_Vertex_Offset(),&texture,1,sphere);
	WWASSERT(inserted);
}

// ----------------------------------------------------------------------------
//
// Temporary arrays for the sorting system
//
// ----------------------------------------------------------------------------

static TempIndexStruct* temp_index_array;
static unsigned temp_index_array_count;

static TempIndexStruct* Get_Temp_Index_Array(unsigned count)
{
	if (count < DEFAULT_SORTING_POLY_COUNT)
		count = DEFAULT_SORTING_POLY_COUNT;
	if (count>temp_index_array_count) {
		delete[] temp_index_array;
		temp_index_array=W3DNEWARRAY TempIndexStruct[count];
		temp_index_array_count=count;
	}
	return temp_index_array;
}

// ----------------------------------------------------------------------------
//
// Insert triangles to the sorting system.
//
// ----------------------------------------------------------------------------

void SortingRendererClass::Insert_Triangles(
	const SphereClass& bounding_sphere,
	unsigned short start_index, 
	unsigned short polygon_count,
	unsigned short min_vertex_index,
	unsigned short vertex_count)
{
	if (!WW3D::Is_Sorting_Enabled()) {
		DX8Wrapper::Draw_Triangles(start_index,polygon_count,min_vertex_index,vertex_count);
		return;
	}

	SNAPSHOT_SAY(("SortingRenderer::Insert(start_i: %d, polygons : %d, min_vi: %d, vertex_count: %d)\n",
		start_index,polygon_count,min_vertex_index,vertex_count));


	DX8_RECORD_SORTING_RENDER(polygon_count,vertex_count);

	SortingNodeStruct* state=Get_Sorting_Struct();

	DX8Wrapper::Get_Render_State(state->sorting_state);
	state->has_native_state=false;
	if (auto* native=NativeD3D12Renderer::Active()) {
		// Freeze rendering inputs when queued, before another object can alter
		// mapper, projection, lighting, or material state. Geometry refs above
		// continue to own the source buffers/textures through the sorted flush.
		DX8Wrapper::Apply_Render_State_Changes();
		DX8Wrapper::Apply_Native_Lighting();
		state->Capture_Native(native->CaptureState(),DX8Wrapper::Build_Native_Material_Description(FVFInfoClass(dynamic_fvf_type)));
	}

 	WWASSERT(
		((state->sorting_state.index_buffer_type==BUFFER_TYPE_SORTING || state->sorting_state.index_buffer_type==BUFFER_TYPE_DYNAMIC_SORTING) &&
		(state->sorting_state.vertex_buffer_types[0]==BUFFER_TYPE_SORTING || state->sorting_state.vertex_buffer_types[0]==BUFFER_TYPE_DYNAMIC_SORTING)));


	state->bounding_sphere=bounding_sphere;
	state->start_index=start_index;
	state->polygon_count=polygon_count;
	state->min_vertex_index=min_vertex_index;
	state->vertex_count=vertex_count;

	SortingVertexBufferClass* vertex_buffer=static_cast<SortingVertexBufferClass*>(state->sorting_state.vertex_buffers[0]);
	WWASSERT(vertex_buffer);
	WWASSERT(state->vertex_count<=vertex_buffer->Get_Vertex_Count());

	Matrix4x4 mtx=state->sorting_state.world*state->sorting_state.view;
	Vector4 transformed_vec;
	Matrix4x4::Transform_Vector(mtx,state->bounding_sphere.Center,&transformed_vec);
	state->transformed_center=Vector3(transformed_vec[0],transformed_vec[1],transformed_vec[2]);

	
	/// @todo lorenzen sez use a bucket sort here... and stop copying so much data so many times

	SortingNodeStruct* node=sorted_list.Head();
	while (node) {
		if (state->transformed_center.Z>node->transformed_center.Z) {
			if (sorted_list.Head()==sorted_list.Tail())
				sorted_list.Add_Head(state);
			else
				state->Insert_Before(node);
			break;
		}
		node=node->Succ();
	}
	if (!node) sorted_list.Add_Tail(state);

#ifdef WWDEBUG
	unsigned short* indices=NULL;
	SortingIndexBufferClass* index_buffer=static_cast<SortingIndexBufferClass*>(state->sorting_state.index_buffer);
	WWASSERT(index_buffer);
	indices=index_buffer->index_buffer;
	WWASSERT(indices);
	indices+=state->start_index;
	indices+=state->sorting_state.iba_offset;

	for (int i=0;i<state->polygon_count;++i) {
		unsigned short idx1=indices[i*3]-state->min_vertex_index;
		unsigned short idx2=indices[i*3+1]-state->min_vertex_index;
		unsigned short idx3=indices[i*3+2]-state->min_vertex_index;
		WWASSERT(idx1<state->vertex_count);
		WWASSERT(idx2<state->vertex_count);
		WWASSERT(idx3<state->vertex_count);
	}
#endif // WWDEBUG
}

// ----------------------------------------------------------------------------
//
// Insert triangles to the sorting system, with no bounding information.
//
// ----------------------------------------------------------------------------

void SortingRendererClass::Insert_Triangles(
	unsigned short start_index, 
	unsigned short polygon_count,
	unsigned short min_vertex_index,
	unsigned short vertex_count)
{
	SphereClass sphere(Vector3(0.0f,0.0f,0.0f),0.0f);
	Insert_Triangles(sphere,start_index,polygon_count,min_vertex_index,vertex_count);
}

// ----------------------------------------------------------------------------
//
// Flush all sorting polygons.
//
// ----------------------------------------------------------------------------

void Release_Refs(SortingNodeStruct* state)
{
	int i;
	for (i=0;i<MAX_VERTEX_STREAMS;++i) {
		REF_PTR_RELEASE(state->sorting_state.vertex_buffers[i]);
	}
	REF_PTR_RELEASE(state->sorting_state.index_buffer);
	REF_PTR_RELEASE(state->sorting_state.material);
	for (i=0;i<MAX_TEXTURE_STAGES;++i)
	{
		REF_PTR_RELEASE(state->sorting_state.Textures[i]);
	}
	state->native_material=NativeMaterialDescription();
	state->native_state=NativeD3D12State();
	state->has_native_state=false;
}

static unsigned overlapping_node_count;
static unsigned overlapping_polygon_count;
static unsigned overlapping_vertex_count;
static const unsigned MAX_OVERLAPPING_NODES=4096;
static SortingNodeStruct* overlapping_nodes[MAX_OVERLAPPING_NODES];

// ----------------------------------------------------------------------------

void SortingRendererClass::Insert_To_Sorting_Pool(SortingNodeStruct* state)
{
	if (overlapping_node_count>=MAX_OVERLAPPING_NODES) {
		Release_Refs(state);
		WWASSERT(0);
		return;
	}

	overlapping_nodes[overlapping_node_count]=state;
	overlapping_vertex_count+=state->vertex_count;
	overlapping_polygon_count+=state->polygon_count;
	overlapping_node_count++;
}

// ----------------------------------------------------------------------------
//static unsigned prevLight = 0xffffffff;

static void Apply_Render_State(RenderStateStruct& render_state)
{



	DX8Wrapper::Set_Shader(render_state.shader);

	DX8Wrapper::Set_Material(render_state.material);

	for (int i=0;i<DX8Wrapper::Get_Current_Caps()->Get_Max_Textures_Per_Pass();++i) 
	{
		DX8Wrapper::Set_Texture(i,render_state.Textures[i]);
	}

	DX8Wrapper::_Set_DX8_Transform(D3DTS_WORLD,render_state.world);
	DX8Wrapper::_Set_DX8_Transform(D3DTS_VIEW,render_state.view);



  if (!render_state.material->Get_Lighting())
    return;
  //prevLight = render_state.lightsHash;

	if (render_state.LightEnable[0]) 
  {
    
    DX8Wrapper::Set_DX8_Light(0,&render_state.Lights[0]);
		if (render_state.LightEnable[1]) 
    {
			DX8Wrapper::Set_DX8_Light(1,&render_state.Lights[1]);
			if (render_state.LightEnable[2]) 
      {
				DX8Wrapper::Set_DX8_Light(2,&render_state.Lights[2]);
				if (render_state.LightEnable[3]) 
					DX8Wrapper::Set_DX8_Light(3,&render_state.Lights[3]);
				else 
					DX8Wrapper::Set_DX8_Light(3,NULL);
			}
			else 
				DX8Wrapper::Set_DX8_Light(2,NULL);
		}
		else 
			DX8Wrapper::Set_DX8_Light(1,NULL);
	}
	else 
		DX8Wrapper::Set_DX8_Light(0,NULL);


}

// ----------------------------------------------------------------------------

void SortingRendererClass::Flush_Sorting_Pool()
{
	if (!overlapping_node_count) return;

	SNAPSHOT_SAY(("SortingSystem - Flush \n"));

	// Fill dynamic index buffer with sorting index buffer vertices
	TempIndexStruct* tis=Get_Temp_Index_Array(overlapping_polygon_count);

	unsigned vertexAllocCount = overlapping_vertex_count;
	if (DynamicVBAccessClass::Get_Default_Vertex_Count() < DEFAULT_SORTING_VERTEX_COUNT)
		vertexAllocCount = DEFAULT_SORTING_VERTEX_COUNT;	//make sure that we force the DX8 dynamic vertex buffer to maximum size
	if (overlapping_vertex_count > vertexAllocCount)
		vertexAllocCount = overlapping_vertex_count;
	WWASSERT(DEFAULT_SORTING_VERTEX_COUNT == 1 || vertexAllocCount <= DEFAULT_SORTING_VERTEX_COUNT);
	DynamicVBAccessClass dyn_vb_access(BUFFER_TYPE_DYNAMIC_DX8,dynamic_fvf_type,vertexAllocCount/*overlapping_vertex_count*/);
	{
		DynamicVBAccessClass::WriteLockClass lock(&dyn_vb_access);
		VertexFormatXYZNDUV2* dest_verts=(VertexFormatXYZNDUV2 *)lock.Get_Formatted_Vertex_Array();

		unsigned polygon_array_offset=0;
		unsigned vertex_array_offset=0;
		for (unsigned node_id=0;node_id<overlapping_node_count;++node_id) {
			SortingNodeStruct* state=overlapping_nodes[node_id];
			VertexFormatXYZNDUV2* src_verts=NULL;
			SortingVertexBufferClass* vertex_buffer=static_cast<SortingVertexBufferClass*>(state->sorting_state.vertex_buffers[0]);
			WWASSERT(vertex_buffer);
			src_verts=vertex_buffer->VertexBuffer;
			WWASSERT(src_verts);
			src_verts+=state->sorting_state.vba_offset;
			src_verts+=state->sorting_state.index_base_offset;
			src_verts+=state->min_vertex_index;

			// If you have a crash in here and "dest_verts" points to illegal memory area,
			// it is because D3D is in illegal state, and the only known cure is rebooting.
			// This illegal state is usually caused by Quake3-engine powered games such as MOHAA.
			memcpy(dest_verts, src_verts, sizeof(VertexFormatXYZNDUV2)*state->vertex_count);
			dest_verts += state->vertex_count;

			const Matrix4x4 mtx=state->sorting_state.world*state->sorting_state.view;

			unsigned short* indices=NULL;
			SortingIndexBufferClass* index_buffer=static_cast<SortingIndexBufferClass*>(state->sorting_state.index_buffer);
			WWASSERT(index_buffer);
			indices=index_buffer->index_buffer;
			WWASSERT(indices);
			indices+=state->start_index;
			indices+=state->sorting_state.iba_offset;

			if (mtx[0][2] == 0.0f && mtx[1][2] == 0.0f && mtx[3][2] == 0.0f && mtx[2][2] == 1.0f) {
				// The common case for particle systems.
				for (int i=0;i<state->polygon_count;++i) {
					unsigned short idx1=indices[i*3]-state->min_vertex_index;
					unsigned short idx2=indices[i*3+1]-state->min_vertex_index;
					unsigned short idx3=indices[i*3+2]-state->min_vertex_index;
					WWASSERT(idx1<state->vertex_count);
					WWASSERT(idx2<state->vertex_count);
					WWASSERT(idx3<state->vertex_count);
					const VertexFormatXYZNDUV2 *v1 = src_verts + idx1;
					const VertexFormatXYZNDUV2 *v2 = src_verts + idx2;
					const VertexFormatXYZNDUV2 *v3 = src_verts + idx3;
					unsigned array_index=i+polygon_array_offset;
					WWASSERT(array_index<overlapping_polygon_count);
					TempIndexStruct *tis_ptr = tis + array_index;
					tis_ptr->tri.i = idx1 + vertex_array_offset;
					tis_ptr->tri.j = idx2 + vertex_array_offset;
					tis_ptr->tri.k = idx3 + vertex_array_offset;
					tis_ptr->idx = node_id;
					tis_ptr->z = (v1->z + v2->z + v3->z)/3.0f;
					DEBUG_ASSERTCRASH((! _isnan(tis_ptr->z) && _finite(tis_ptr->z)), ("Triangle has invalid center"));
				}
			} else {
				for (int i=0;i<state->polygon_count;++i) {
					unsigned short idx1=indices[i*3]-state->min_vertex_index;
					unsigned short idx2=indices[i*3+1]-state->min_vertex_index;
					unsigned short idx3=indices[i*3+2]-state->min_vertex_index;
					WWASSERT(idx1<state->vertex_count);
					WWASSERT(idx2<state->vertex_count);
					WWASSERT(idx3<state->vertex_count);
					const VertexFormatXYZNDUV2 *v1 = src_verts + idx1;
					const VertexFormatXYZNDUV2 *v2 = src_verts + idx2;
					const VertexFormatXYZNDUV2 *v3 = src_verts + idx3;
					unsigned array_index=i+polygon_array_offset;
					WWASSERT(array_index<overlapping_polygon_count);
					TempIndexStruct *tis_ptr = tis + array_index;
					tis_ptr->tri.i = idx1 + vertex_array_offset;
					tis_ptr->tri.j = idx2 + vertex_array_offset;
					tis_ptr->tri.k = idx3 + vertex_array_offset;
					tis_ptr->idx = node_id;
					tis_ptr->z = (mtx[0][2]*(v1->x + v2->x + v3->x) +
												mtx[1][2]*(v1->y + v2->y + v3->y) +
												mtx[2][2]*(v1->z + v2->z + v3->z))/3.0f + mtx[3][2];
					DEBUG_ASSERTCRASH((! _isnan(tis_ptr->z) && _finite(tis_ptr->z)), ("Triangle has invalid center"));
				}
			}

			state->min_vertex_index=vertex_array_offset;

			polygon_array_offset+=state->polygon_count;
			vertex_array_offset+=state->vertex_count;
		}
	}

	Sort(tis, tis + overlapping_polygon_count);

/*	///@todo: Add code to break up rendering into multiple index buffer fills to allow more than 65536/3 triangles.  -MW
	int total_overlapping_polygon_count = overlapping_polygon_count;
	while (  > 0)
	{
		if ((total_overlapping_polygon_count*3) > 65535)
		{	//overflowed the index buffer, must break into multiple batches
			overlapping_polygon_count = 65535/3;
		}
		else
			overlapping_polygon_count = total_overlapping_polygon_count;

		//insert rendering code here!!

		total_overlapping_polygon_count -= overlapping_polygon_count;
	}
*/
	unsigned polygonAllocCount = overlapping_polygon_count;
	if ((unsigned)(DynamicIBAccessClass::Get_Default_Index_Count()/3) < DEFAULT_SORTING_POLY_COUNT)
		polygonAllocCount = DEFAULT_SORTING_POLY_COUNT;	//make sure that we force the DX8 index buffer to maximum size
	if (overlapping_polygon_count > polygonAllocCount)
		polygonAllocCount = overlapping_polygon_count;
	WWASSERT(DEFAULT_SORTING_POLY_COUNT <= 1 || polygonAllocCount <= DEFAULT_SORTING_POLY_COUNT);

	DynamicIBAccessClass dyn_ib_access(BUFFER_TYPE_DYNAMIC_DX8,polygonAllocCount*3);
	{
		DynamicIBAccessClass::WriteLockClass lock(&dyn_ib_access);
		ShortVectorIStruct* sorted_polygon_index_array=(ShortVectorIStruct*)lock.Get_Index_Array();

		try {
		for (unsigned a=0;a<overlapping_polygon_count;++a) {
			sorted_polygon_index_array[a]=tis[a].tri;
		}
		IndexBufferExceptionFunc();
		} catch(...) {
			IndexBufferExceptionFunc();
		}
	}

	// Set index buffer and render!

	auto* native=NativeD3D12Renderer::Active();
	if (!native) {
		DX8Wrapper::Set_Index_Buffer(dyn_ib_access,0);
		DX8Wrapper::Set_Vertex_Buffer(dyn_vb_access);
		DX8Wrapper::Apply_Render_State_Changes();
	}
	const auto renderRun=[&](SortingNodeStruct* state,unsigned first,unsigned count) {
		if (!native || !state->has_native_state) {
			Apply_Render_State(state->sorting_state);
			DX8Wrapper::Draw_Triangles(first*3,count,state->min_vertex_index,state->vertex_count);
			return;
		}
		if (!_EnableTriangleDraw) return;
		const auto* vb=dyn_vb_access.Get_Native_Vertex_Buffer();
		const auto* ib=dyn_ib_access.Get_Native_Index_Buffer();
		const size_t vo=size_t(dyn_vb_access.Get_Native_Vertex_Offset())*sizeof(VertexFormatXYZNDUV2);
		const size_t io=(size_t(dyn_ib_access.Get_Native_Index_Offset())+first*3)*sizeof(unsigned short);
		const size_t bytes=size_t(overlapping_vertex_count)*sizeof(VertexFormatXYZNDUV2);
		if (!vb || !ib || vo>vb->Size() || bytes>vb->Size()-vo || io>ib->Size() ||
			size_t(count)*6>ib->Size()-io) { WWASSERT(false); return; }
		NativeD3D12ScopedState restore(*native);
		native->RestoreState(state->native_state);
		NativeDrawSubmission draw;
		draw.vertices=static_cast<const unsigned char*>(vb->Data())+vo;
		draw.vertexBytes=UINT(bytes); draw.vertexStride=sizeof(VertexFormatXYZNDUV2);
		draw.vertexCount=overlapping_vertex_count;
		draw.indices=reinterpret_cast<const unsigned short*>(static_cast<const unsigned char*>(ib->Data())+io);
		draw.indexCount=count*3;
		draw.layout=FVFInfoClass(dynamic_fvf_type).Build_Native_Layout();
		draw.material=state->native_material; draw.useMaterial=true;
		draw.vertexOwner=vb; draw.indexOwner=ib;
		Submit_Native_Draw(*native,draw);
	};

	unsigned count_to_render=1;
	unsigned start_index=0;
	unsigned node_id=tis[0].idx;
	for (unsigned i=1;i<overlapping_polygon_count;++i) {
		if (node_id!=tis[i].idx) {
			SortingNodeStruct* state=overlapping_nodes[node_id];
			renderRun(state,start_index,count_to_render);

			count_to_render=0;
			start_index=i;
			node_id=tis[i].idx;
		}
		count_to_render++;	//keep track of number of polygons of same kind
	}

	// Render any remaining polygons...
	if (count_to_render) {
		SortingNodeStruct* state=overlapping_nodes[node_id];
		renderRun(state,start_index,count_to_render);
	}

	// Release all references and return nodes back to the clean list for the frame...
	for (node_id=0;node_id<overlapping_node_count;++node_id) {
		SortingNodeStruct* state=overlapping_nodes[node_id];
		Release_Refs(state);
		clean_list.Add_Head(state);
	}
	overlapping_node_count=0;
	overlapping_polygon_count=0;
	overlapping_vertex_count=0;

	SNAPSHOT_SAY(("SortingSystem - Done flushing\n"));

}

// ----------------------------------------------------------------------------

void SortingRendererClass::Flush()
{
	WWPROFILE("SortingRenderer::Flush");
	Matrix4x4 old_view;
	Matrix4x4 old_world;
	DX8Wrapper::Get_Transform(D3DTS_VIEW,old_view);
	DX8Wrapper::Get_Transform(D3DTS_WORLD,old_world);

	while (SortingNodeStruct* state=sorted_list.Head()) {
		state->Remove();
		
		if ((state->sorting_state.index_buffer_type==BUFFER_TYPE_SORTING || state->sorting_state.index_buffer_type==BUFFER_TYPE_DYNAMIC_SORTING) &&
			(state->sorting_state.vertex_buffer_types[0]==BUFFER_TYPE_SORTING || state->sorting_state.vertex_buffer_types[0]==BUFFER_TYPE_DYNAMIC_SORTING)) {
			Insert_To_Sorting_Pool(state);
		}
		else {
			DX8Wrapper::Set_Render_State(state->sorting_state);
			DX8Wrapper::Draw_Triangles(state->start_index,state->polygon_count,state->min_vertex_index,state->vertex_count);
			DX8Wrapper::Release_Render_State();
			Release_Refs(state);
			clean_list.Add_Head(state);
		}
	}

	bool old_enable=DX8Wrapper::_Is_Triangle_Draw_Enabled();
	DX8Wrapper::_Enable_Triangle_Draw(_EnableTriangleDraw);
	Flush_Sorting_Pool();
	DX8Wrapper::_Enable_Triangle_Draw(old_enable);

	DX8Wrapper::Set_Index_Buffer(0,0);
	DX8Wrapper::Set_Vertex_Buffer(0);
	total_sorting_vertices=0;

	DynamicIBAccessClass::_Reset(false);
	DynamicVBAccessClass::_Reset(false);


	DX8Wrapper::Set_Transform(D3DTS_VIEW,old_view);
	DX8Wrapper::Set_Transform(D3DTS_WORLD,old_world);

}

// ----------------------------------------------------------------------------

void SortingRendererClass::Deinit()
{
	SortingNodeStruct *head = NULL;

	//
	//	Flush the sorted list
	//
	while ((head = sorted_list.Head ()) != NULL) {
		sorted_list.Remove_Head ();
		delete head;
	}

	//
	//	Flush the clean list
	//
	while ((head = clean_list.Head ()) != NULL) {
		clean_list.Remove_Head ();
		delete head;
	}

	delete[] temp_index_array;
	temp_index_array=NULL;
	temp_index_array_count=0;
}


// ----------------------------------------------------------------------------
//
// Insert a VolumeParticle triangle into the sorting system.
//
// ----------------------------------------------------------------------------

void SortingRendererClass::Insert_VolumeParticle(
	const SphereClass& bounding_sphere,
	unsigned short start_index, 
	unsigned short polygon_count,
	unsigned short min_vertex_index,
	unsigned short vertex_count,
	unsigned short layerCount)
{
	if (!WW3D::Is_Sorting_Enabled()) {
		DX8Wrapper::Draw_Triangles(start_index,polygon_count,min_vertex_index,vertex_count);
		return;
	}

	//FOR VOLUME_PARTICLE LOGIC:
	// WE MUST MULTIPLY THE VERTCOUNT AND POLYCOUNT BY THE VOLUME_PARTICLE DEPTH
	DX8_RECORD_SORTING_RENDER( polygon_count * layerCount,vertex_count * layerCount);//THIS IS VOLUME_PARTICLE SPECIFIC

	SortingNodeStruct* state=Get_Sorting_Struct();
	DX8Wrapper::Get_Render_State(state->sorting_state);

 	WWASSERT(
		((state->sorting_state.index_buffer_type==BUFFER_TYPE_SORTING || state->sorting_state.index_buffer_type==BUFFER_TYPE_DYNAMIC_SORTING) &&
		(state->sorting_state.vertex_buffer_types[0]==BUFFER_TYPE_SORTING || state->sorting_state.vertex_buffer_types[0]==BUFFER_TYPE_DYNAMIC_SORTING)));

	state->bounding_sphere=bounding_sphere;
	state->start_index=start_index;
	state->min_vertex_index=min_vertex_index;
	state->polygon_count=polygon_count * layerCount;//THIS IS VOLUME_PARTICLE SPECIFIC
	state->vertex_count=vertex_count * layerCount;//THIS IS VOLUME_PARTICLE SPECIFIC

	SortingVertexBufferClass* vertex_buffer=static_cast<SortingVertexBufferClass*>(state->sorting_state.vertex_buffers[0]);
	WWASSERT(vertex_buffer);
	WWASSERT(state->vertex_count<=vertex_buffer->Get_Vertex_Count());

	// Transform the center point to view space for sorting

	Matrix4x4 mtx=state->sorting_state.world*state->sorting_state.view;
	Vector4 transformed_vec;
	Matrix4x4::Transform_Vector(mtx,state->bounding_sphere.Center,&transformed_vec);
	state->transformed_center=Vector3(transformed_vec[0],transformed_vec[1],transformed_vec[2]);


	// BUT WHAT IS THE DEAL WITH THE VERTCOUNT AND POLYCOUNT BEING N BUT TRANSFORMED CENTER COUNT == 1

	//THE TRANSFORMED CENTER[2] IS THE ZBUFFER DEPTH
	
	/// @todo lorenzen sez use a bucket sort here... and stop copying so much data so many times

	SortingNodeStruct* node=sorted_list.Head();
	while (node) {
		if (state->transformed_center.Z>node->transformed_center.Z) {
			if (sorted_list.Head()==sorted_list.Tail())
				sorted_list.Add_Head(state);
			else
				state->Insert_Before(node);
			break;
		}
		node=node->Succ();
	}
	if (!node) sorted_list.Add_Tail(state);
}
