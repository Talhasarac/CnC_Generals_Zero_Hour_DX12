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
 *                 Project Name : WW3D                                                         *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/decalmsh.cpp                           $*
 *                                                                                             *
 *              Original Author:: Greg Hjelstrom                                               *
 *                                                                                             *
 *                      $Author:: Kenny Mitchell                                               * 
 *                                                                                             * 
 *                     $Modtime:: 06/26/02 4:04p                                             $*
 *                                                                                             *
 *                    $Revision:: 24                                                          $*
 *                                                                                             *
 * 06/26/02 KM Matrix name change to avoid MAX conflicts                                       *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   DecalMeshClass::DecalMeshClass -- Constructor                                             *
 *   DecalMeshClass::~DecalMeshClass -- Destructor                                             *
 *   RigidDecalMeshClass::RigidDecalMeshClass -- Constructor                                   *
 *   RigidDecalMeshClass::~RigidDecalMeshClass -- Destructor                                   *
 *   RigidDecalMeshClass::Render -- Render the decals                                          *
 *   RigidDecalMeshClass::Process_Material_Run -- scans the mesh for material runs             *
 *   RigidDecalMeshClass::Create_Decal -- Generate a new decal                                 *
 *   RigidDecalMeshClass::Delete_Decal -- Delete a decal                                       *
 *   SkinDecalMeshClass::SkinDecalMeshClass -- Constructor                                     *
 *   SkinDecalMeshClass::~SkinDecalMeshClass -- Destructor                                     *
 *   SkinDecalMeshClass::Render -- Render the decals                                           *
 *   SkinDecalMeshClass::Create_Decal -- Generate a new decal                                  *
 *   SkinDecalMeshClass::Delete_Decal -- Delete a decal                                        *
 *   SkinDecalMeshClass::Process_Material_Run -- scans the mesh for material runs              *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "decalmsh.h"
#include "decalsys.h"
#include "rinfo.h"
#include "mesh.h"
#include "meshmdl.h"
#include "plane.h"
#include "statistics.h"
#include "simplevec.h"
#include "texture.h"
#include "camera.h"
#include "native_draw_state.h"
#include "native_pipeline_description.h"
#include "native_light_environment.h"
#include <vector>
#include <cstddef>


namespace {
struct NativeDecalVertex {
	float x,y,z,nx,ny,nz;
	UINT32 diffuse;
	float u1,v1,u2,v2;
};

bool DrawNativeDecalRun(CameraClass& camera, MeshClass& parent, const Matrix3D& world,
	const std::vector<NativeDecalVertex>& vertices, const std::vector<unsigned short>& indices,
	int first, int count, TextureClass* texture, VertexMaterialClass* vertexMaterial,
	const ShaderClass& shader)
{
	NativeD3D12Renderer* native = NativeD3D12Renderer::Active();
	if (!native || vertices.empty() || count<=0) return false;
	NativeD3D12ScopedState scope(*native);
	Matrix3D view;
	Matrix4x4 projection;
	camera.Get_View_Matrix(&view);
	camera.Get_D3D_Projection_Matrix(&projection);
	NativeMapperContext context = {
		Matrix4x4(world).Transpose()*Matrix4x4(view).Transpose(), Matrix4x4(view), projection};
	const Matrix4x4 wvp = context.worldView*projection.Transpose();
	native->SetWorldView(reinterpret_cast<const float*>(&context.worldView));
	native->SetWorldViewProjection(reinterpret_cast<const float*>(&wvp));
	native->SetTreeSway(nullptr,0);

	NativeDrawSubmission draw;
	draw.vertices = vertices.data();
	draw.vertexCount = static_cast<UINT>(vertices.size());
	draw.vertexStride = sizeof(NativeDecalVertex);
	draw.vertexBytes = draw.vertexCount*draw.vertexStride;
	draw.indices = indices.data()+first*3;
	draw.indexCount = count*3;
	draw.layout.stride = sizeof(NativeDecalVertex);
	draw.layout.Add(NativeVertexSemantic::Position,0,DXGI_FORMAT_R32G32B32_FLOAT,offsetof(NativeDecalVertex,x));
	draw.layout.Add(NativeVertexSemantic::Normal,0,DXGI_FORMAT_R32G32B32_FLOAT,offsetof(NativeDecalVertex,nx));
	draw.layout.Add(NativeVertexSemantic::Color,0,DXGI_FORMAT_R8G8B8A8_UNORM,offsetof(NativeDecalVertex,diffuse));
	draw.layout.Add(NativeVertexSemantic::TexCoord,0,DXGI_FORMAT_R32G32_FLOAT,offsetof(NativeDecalVertex,u1));
	draw.layout.Add(NativeVertexSemantic::TexCoord,1,DXGI_FORMAT_R32G32_FLOAT,offsetof(NativeDecalVertex,u2));
	draw.material = shader.Get_Native_Texture_Material();
	draw.useMaterial = true;
	if (texture) {
		draw.material.textures[0] = texture->Prepare_Native_Texture();
		draw.material.samplers[0] = texture->Get_Filter().Get_Native_Description();
	}
	draw.material.coordinates[0].offset = offsetof(NativeDecalVertex,u1);
	draw.material.coordinates[1].offset = offsetof(NativeDecalVertex,u2);
	if (vertexMaterial && !vertexMaterial->Describe_Native_Mapping(context,draw.layout,draw.material)) {
		WWASSERT(false); // An unsupported authored mapper must not silently use stale state.
		return false;
	}
	auto lighting = native->CaptureState().lighting;
	if (parent.Get_Lighting_Environment())
		Describe_Native_Light_Environment(*parent.Get_Lighting_Environment(),context.view,lighting);
	if (vertexMaterial) vertexMaterial->Describe_Native_Lighting(lighting,WW3D::Is_Coloring_Enabled());
	else lighting.flags[0] = 0;
	lighting.flags[1] = shader.Get_Secondary_Gradient()!=ShaderClass::SECONDARY_GRADIENT_DISABLE;
	lighting.flags[2] = parent.Get_ObjectScale()!=1.0f;
	native->SetLighting(lighting);
	if (shader.Get_Fog_Func()==ShaderClass::FOG_DISABLE)
		native->SetVertexFog(0,0,1,0,0,false);
	else if (shader.Get_Fog_Func()!=ShaderClass::FOG_ENABLE) {
		const auto state = native->CaptureState();
		native->SetVertexFog(state.fogMode,state.fogParameters[0],state.fogParameters[1],
			state.fogParameters[2],shader.Get_Fog_Func()==ShaderClass::FOG_WHITE ? 0xffffff : 0,state.fogRange);
	}
	shader.Get_Native_Pipeline(ShaderClass::Is_Backface_Culling_Inverted()).Apply(*native);
	native->SetDepthBias(-8);
	return Submit_Native_Draw(*native,draw);
}
}

#define DISABLE_CLIPPING	0

/**
** DecalPolyClass - This class is used to clip polygons as they are
** added to a RigidDecalMesh. 
**
** Data needed to add a poly to the decal mesh:
** connectivity - generated on the fly after the poly is clipped
** planeeq - constant for entire poly, copy from source after done
** verts - plug into DecalPolyClass, clip, pull back out
** vnorms - plug into DecalPolyClass, clip, copy back out 
** texcoords - compute after poly is clipped
** material - contstant for entire poly, get from generator
** shader - constant for entire poly, get from generator
** texture - constant for entire poly, get from generator
*/
class DecalPolyClass
{
public:
	void Reset(void);
	void Add_Vertex(const Vector3 & point,const Vector3 & normal);
	void Clip(const PlaneClass & plane,DecalPolyClass & dest) const;

	SimpleDynVecClass<Vector3> Verts;
	SimpleDynVecClass<Vector3> VertNorms;
};


void DecalPolyClass::Reset(void)
{
	Verts.Delete_All(false);
	VertNorms.Delete_All(false);
}

void DecalPolyClass::Add_Vertex(const Vector3 & point,const Vector3 & norm)
{
	Verts.Add(point);
	VertNorms.Add(norm);
}

void DecalPolyClass::Clip(const PlaneClass & plane,DecalPolyClass & dest) const
{
	dest.Reset();

	if (Verts.Count() <= 2) return;

	// temporary variables used in clipping
	int i = 0;
	int iprev = Verts.Count() - 1;
	bool cur_point_in_front;
	bool prev_point_in_front;
	
	float alpha;
	Vector3 int_point;
	Vector3 int_normal;

	// perform clipping
	prev_point_in_front = plane.In_Front(Verts[iprev]);
#if DISABLE_CLIPPING
	prev_point_in_front = true;
#endif

	for (int j=0; j<Verts.Count(); j++) { 
		
		cur_point_in_front = plane.In_Front(Verts[i]);
#if DISABLE_CLIPPING
		cur_point_in_front = true;		
#endif

		if (prev_point_in_front) {

			if (cur_point_in_front) {
			
				// Previous vertex was in front of plane and this vertex is in
				// front of the plane so we emit this vertex.
				dest.Add_Vertex(Verts[i],VertNorms[i]);

			} else { 

				// Previous vert was in front, this vert is behind, compute
				// the intersection and emit the point.
				plane.Compute_Intersection(Verts[iprev],Verts[i],&alpha);
				Vector3::Lerp(Verts[iprev],Verts[i],alpha,&int_point);
				Vector3::Lerp(VertNorms[iprev],VertNorms[i],alpha,&int_normal);
				dest.Add_Vertex(int_point,int_normal);

			}

		} else {

			if (cur_point_in_front) {

				// segment is going from the back halfspace to the front halfspace
				// compute the intersection and emit it, then continue
				// the edge into the front halfspace and emit the end point.
				plane.Compute_Intersection(Verts[iprev],Verts[i],&alpha);
				Vector3::Lerp(Verts[iprev],Verts[i],alpha,&int_point);
				Vector3::Lerp(VertNorms[iprev],VertNorms[i],alpha,&int_normal);
				dest.Add_Vertex(int_point,int_normal);
				dest.Add_Vertex(Verts[i],VertNorms[i]);
			
			} 
		} 

		prev_point_in_front = cur_point_in_front;
		iprev = i;
		i = (i+1)%(Verts.Count());
	}
}

static DecalPolyClass _DecalPoly0;
static DecalPolyClass _DecalPoly1;


/*
** DecalMeshClass Implementation
*/

/***********************************************************************************************
 * DecalMeshClass::DecalMeshClass -- Constructor                                               *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   1/26/00    gth : Created.                                                                 *
 *=============================================================================================*/
DecalMeshClass::DecalMeshClass(MeshClass * parent,DecalSystemClass * system) :
	Parent(parent),
	DecalSystem(system)
{
	WWASSERT(Parent != NULL);
	WWASSERT(DecalSystem != NULL);
}


/***********************************************************************************************
 * DecalMeshClass::~DecalMeshClass -- Destructor                                               *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   1/26/00    gth : Created.                                                                 *
 *=============================================================================================*/
DecalMeshClass::~DecalMeshClass(void)
{
}


/*
** RigidDecalMeshClass Implementation
*/


/***********************************************************************************************
 * RigidDecalMeshClass::RigidDecalMeshClass -- Constructor                                     *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   1/31/00    NH : Created.                                                                  *
 *=============================================================================================*/
RigidDecalMeshClass::RigidDecalMeshClass(MeshClass * parent, DecalSystemClass * system) :
	DecalMeshClass(parent, system)
{
}


/***********************************************************************************************
 * RigidDecalMeshClass::~RigidDecalMeshClass -- Destructor                                     *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   1/31/00    NH : Created.                                                                  *
 *=============================================================================================*/
RigidDecalMeshClass::~RigidDecalMeshClass(void)
{
	int i;

	// Notify the system that this decal mesh is being destroyed.
	for (i=0; i<Decals.Count(); i++) {
		DecalSystem->Decal_Mesh_Destroyed(Decals[i].DecalID,this);
	}

	// Release all of our references.  The memory in the arrays will automatically be 
	// released by the SimpleDynVecClass...
	for (i=0; i<Polys.Count(); i++) {
		REF_PTR_RELEASE(Textures[i]);
	}

	for (i=0; i<Verts.Count(); i++) {
		REF_PTR_RELEASE(VertexMaterials[i]);
	}
}


/***********************************************************************************************
 * RigidDecalMeshClass::Render -- Render the decals                                            *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   1/26/00    gth : Created.                                                                 *
 *=============================================================================================*/
void RigidDecalMeshClass::Render(CameraClass& camera)
{
	if (Decals.Count()==0 || !WW3D::Are_Decals_Enabled()) return;

	// These transient CPU arrays are uploaded by the native renderer's frame
	// allocator. Skins keep the existing deformation; no legacy VB/state is bound.
	std::vector<NativeDecalVertex> vertices(Verts.Count());
	for (int i=0; i<Verts.Count(); ++i) {
		const Vector3& p = Verts[i];
		const Vector3& normal = VertNorms[i];
		vertices[i] = {p.X,p.Y,p.Z,normal.X,normal.Y,normal.Z,0xffffffff,
			TexCoords[i].X,TexCoords[i].Y,0,0};
	}
	std::vector<unsigned short> indices(Polys.Count()*3);
	for (int i=0; i<Polys.Count(); ++i) {
		indices[i*3] = static_cast<unsigned short>(Polys[i].I);
		indices[i*3+1] = static_cast<unsigned short>(Polys[i].J);
		indices[i*3+2] = static_cast<unsigned short>(Polys[i].K);
	}
	for (int first=0; first<Polys.Count();) {
		const int next = Process_Material_Run(first);
		DrawNativeDecalRun(camera,*Parent,Parent->Get_Transform(),
			vertices,indices,first,next-first,Textures[first],VertexMaterials[Polys[first].I],Shaders[first]);
		first = next;
	}
}


/***********************************************************************************************
 * RigidDecalMeshClass::Process_Material_Run -- scans the mesh for material runs               *
 *                                                                                             *
 *    This function will install the materials for poly[start_index] and scan forward for      *
 *    the next material change.  It will return the start index for the next material change   *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   2/22/2001  gth : Created.                                                                 *
 *=============================================================================================*/
int RigidDecalMeshClass::Process_Material_Run(int start_index)
{

	int next_index = start_index;
	while (	(next_index < Polys.Count()) && 
				(Textures[next_index] == Textures[start_index]) &&
				(Shaders[next_index] == Shaders[start_index]) &&
				(VertexMaterials[Polys[next_index].I] == VertexMaterials[Polys[start_index].I]))
	{
		next_index++;
	}
	return next_index;
}

/***********************************************************************************************
 * RigidDecalMeshClass::Create_Decal -- Generate a new decal                                   *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 * All Decals on a mesh must be generated from the same DecalSystemClass!                      *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   1/26/00    gth : Created.                                                                 *
 *=============================================================================================*/
bool RigidDecalMeshClass::Create_Decal
(
	DecalGeneratorClass *			generator,
	const OBBoxClass &				localbox,
	SimpleDynVecClass<uint32> &	apt,
	const DynamicVectorClass<Vector3> * world_vertex_locs
)
{
	// Native rasterizer depth bias handles coplanar geometry without modifying
	// the authored decal positions or consulting legacy device capabilities.
	Vector3 zbias_offset(0.0f,0.0f,0.0f);
	// NOTE: world_vertex_locs/norms should not be set for this class
	WWASSERT(world_vertex_locs == 0);

	int i,j;
	WWASSERT(generator->Peek_Decal_System() == DecalSystem);
	
	/*
	** If any polys were collected, add a new MeshDecalStruct
	*/
	if (apt.Count() == 0) {
		return false;
	}

	DecalStruct newdecal;
	newdecal.DecalID = generator->Get_Decal_ID();
	newdecal.FaceStartIndex = Polys.Count();		// start faces at the end of the current array
	newdecal.FaceCount = 0;								// init facecount to zero
	newdecal.VertexStartIndex = Verts.Count();	// start vertices at the end of the current array
	newdecal.VertexCount = 0;							// init vertcount to zero
	
	/*
	** Grab pointers to the parent mesh's components
	*/
	MeshModelClass * model = Parent->Peek_Model();
	const TriIndex * src_polys		= model->Get_Polygon_Array();
	const Vector3 * src_verts		= model->Get_Vertex_Array();
	const Vector3 * src_vnorms		= model->Get_Vertex_Normal_Array();

	/*
	** Grab a pointer to the material settings
	*/
	MaterialPassClass * material = generator->Get_Material();
	
	/*
	** Set up the generator for our coordinate system
	*/
	generator->Set_Mesh_Transform(Parent->Get_Transform());
	
	/*
	** Compute the clipping planes
	*/
	PlaneClass planes[4];
	Vector3 extent;

	Matrix3x3::Rotate_Vector(localbox.Basis,Vector3(localbox.Extent.X,0,0),&extent);
	Vector3 direction(localbox.Basis.Get_X_Vector());
	
	planes[0].Set(-direction,localbox.Center + extent);
	planes[1].Set(direction,localbox.Center - extent);
	
	Matrix3x3::Rotate_Vector(localbox.Basis,Vector3(0,localbox.Extent.Y,0),&extent);
	direction.Set(localbox.Basis.Get_Y_Vector());
	
	planes[2].Set(-direction,localbox.Center + extent);
	planes[3].Set(direction,localbox.Center - extent);

	/*
	** Generate the faces and per-face info
	*/
	bool added_polys = false;
	Vector3 pdir = localbox.Basis.Get_Z_Vector();

	for (i=0; i<apt.Count(); i++) {

		/*
		** check if the polygon is backfacing
		*/
		PlaneClass plane;
		model->Compute_Plane(apt[i],&plane);

		float dot = Vector3::Dot_Product(plane.N,pdir);
		if (dot > generator->Get_Backface_Threshhold()) {
			/*
			** Copy src_polys[apt[i]] into our clip polygon
			*/
			_DecalPoly0.Reset();
			const TriIndex & poly = src_polys[apt[i]];
			for (j=0; j<3; j++) {
				_DecalPoly0.Add_Vertex(src_verts[poly[j]] + zbias_offset,src_vnorms[poly[j]]);
			}

			/*
			** Clip against the edges of the bounding box
			*/
			_DecalPoly0.Clip(planes[0],_DecalPoly1);
			_DecalPoly1.Clip(planes[1],_DecalPoly0);
			_DecalPoly0.Clip(planes[2],_DecalPoly1);
			_DecalPoly1.Clip(planes[3],_DecalPoly0);

			/*
			** Check if the clipped polygon is empty or degenerate
			*/
			if (_DecalPoly0.Verts.Count() >= 3) {

				/*
				** Extract triangles from the clipped polygon
				*/
				int first_vert = Verts.Count();

				for (j=1; j<_DecalPoly0.Verts.Count()-1; j++) {

					/*
					** Check if this triangle is degenerate (Sutherland-Hodgeman can sometimes create degenerate tris)
					*/
					// TODO

					/*
					** Add the triangle, its plane equation, and the per-tri materials
					*/
					added_polys = true;
					Polys.Add(TriIndex(first_vert,first_vert + j,first_vert + j + 1));
					Shaders.Add(material->Peek_Shader());
					Textures.Add(material->Get_Texture());					// Get_Texture gives us a reference...
				}

				/*
				** Extract verts from the clipped polygon
				*/
				for (j=0; j<_DecalPoly0.Verts.Count(); j++) {

					Verts.Add(_DecalPoly0.Verts[j]);
					_DecalPoly0.VertNorms[j].Normalize();
					VertNorms.Add(_DecalPoly0.VertNorms[j]);
					VertexMaterials.Add(material->Get_Material());	// Get_Material gives us a ref.

					/*
					** Compute the uv coordinates for this vertex
					*/
					Vector3 stq;
					generator->Compute_Texture_Coordinate(Verts[Verts.Count()-1],&stq);
					TexCoords.Add(Vector2(stq.X,stq.Y));
				}
			}
		}
	}

	if (added_polys) {
		newdecal.FaceCount = Polys.Count() - newdecal.FaceStartIndex;
		newdecal.VertexCount = Verts.Count() - newdecal.VertexStartIndex;
		Decals.Add(newdecal);

		/*
		** tell the generator that we added a decal
		*/
		generator->Add_Mesh(Parent);
	} 

	material->Release_Ref();
	
#ifdef WWDEBUG	
	/*
	** Some paranoid debug code: ensure all tris have valid vertex indices
	*/
	int poly_count = Polys.Count();
	int vert_count = Verts.Count();
	for (int poly_idx = 0; poly_idx < poly_count; poly_idx++) {
		WWASSERT (Polys[poly_idx].I < vert_count);
		WWASSERT (Polys[poly_idx].I >= 0);
		WWASSERT (Polys[poly_idx].J < vert_count);
		WWASSERT (Polys[poly_idx].J >= 0);
		WWASSERT (Polys[poly_idx].K < vert_count);
		WWASSERT (Polys[poly_idx].K >= 0);
	}
#endif

	/*
	** Only return true if we actually added a decal
	*/
	return added_polys;
}


/***********************************************************************************************
 * RigidDecalMeshClass::Delete_Decal -- Delete a decal                                         *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   1/26/00    gth : Created.                                                                 *
 *=============================================================================================*/
bool RigidDecalMeshClass::Delete_Decal(uint32 id)
{
	/*
	** Find the MeshDecal which matches the given id
	*/
	int decal_index = -1;
	for (int i = 0;i < Decals.Count(); i++) {
		if (Decals[i].DecalID == id) {
			decal_index = i;
			break;
		}
	}

	if (decal_index == -1) {
		return false;
	}

	DecalStruct * decal = &Decals[decal_index];

	/*
	** Remove all geometry used by this decal 
	*/
	Polys.Delete_Range(decal->FaceStartIndex,decal->FaceCount);
	Verts.Delete_Range(decal->VertexStartIndex,decal->VertexCount);
	VertNorms.Delete_Range(decal->VertexStartIndex,decal->VertexCount);
	TexCoords.Delete_Range(decal->VertexStartIndex,decal->VertexCount);

	/*
	** Re-index the remaining triangle vertex indices
	*/
	for (int poly_index = 0; poly_index < Polys.Count(); poly_index++) {
		if (Polys[poly_index].I > decal->VertexStartIndex) Polys[poly_index].I -= decal->VertexCount;
		if (Polys[poly_index].J > decal->VertexStartIndex) Polys[poly_index].J -= decal->VertexCount;
		if (Polys[poly_index].K > decal->VertexStartIndex) Polys[poly_index].K -= decal->VertexCount;
	}

	/*
	** Remove all materials used by this decal (remember to release refs!)
	*/
	for (int fi=decal->FaceStartIndex; fi<decal->FaceCount; fi++) {
		REF_PTR_RELEASE(Textures[fi]);
	}
	for (int vi=decal->VertexStartIndex; vi<decal->VertexCount; vi++) {
		REF_PTR_RELEASE(VertexMaterials[vi]);
	}
	Shaders.Delete_Range(decal->FaceStartIndex,decal->FaceCount);
	Textures.Delete_Range(decal->FaceStartIndex,decal->FaceCount);
	VertexMaterials.Delete_Range(decal->VertexStartIndex,decal->VertexCount);

	/*
	** Remove MeshDecal and refresh all other decal indices
	*/
	for (int di=decal_index+1; di<Decals.Count(); di++) {
		Decals[di].FaceStartIndex -= decal->FaceCount;
		Decals[di].VertexStartIndex -= decal->VertexCount;
	}
	Decals.Delete(decal_index);

#ifdef WWDEBUG	
	/*
	** Some paranoid debug code: ensure all tris have valid vertex indices
	*/
	int poly_count = Polys.Count();
	int vert_count = Verts.Count();
	for (int poly_idx = 0; poly_idx < poly_count; poly_idx++) {
		WWASSERT (Polys[poly_idx].I < vert_count);
		WWASSERT (Polys[poly_idx].I >= 0);
		WWASSERT (Polys[poly_idx].J < vert_count);
		WWASSERT (Polys[poly_idx].J >= 0);
		WWASSERT (Polys[poly_idx].K < vert_count);
		WWASSERT (Polys[poly_idx].K >= 0);
	}
#endif

	return true;
}


/*
** Temporary Buffers
** These buffers are used by the skin code for temporary storage of the deformed vertices and 
** vertex normals.  
*/
static SimpleVecClass<Vector3>	_TempVertexBuffer;
static SimpleVecClass<Vector3>	_TempNormalBuffer;


/*
** SkinDecalMeshClass Implementation
*/


/***********************************************************************************************
 * SkinDecalMeshClass::SkinDecalMeshClass -- Constructor                                       *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   1/31/00    NH : Created.                                                                  *
 *=============================================================================================*/
SkinDecalMeshClass::SkinDecalMeshClass(MeshClass * parent, DecalSystemClass * system) :
	DecalMeshClass(parent, system)
{
}


/***********************************************************************************************
 * SkinDecalMeshClass::~SkinDecalMeshClass -- Destructor                                       *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   1/31/00    NH : Created.                                                                  *
 *=============================================================================================*/
SkinDecalMeshClass::~SkinDecalMeshClass(void)
{
	int i;

	// Notify the system that this decal mesh is being destroyed.
	for (i=0; i<Decals.Count(); i++) {
		DecalSystem->Decal_Mesh_Destroyed(Decals[i].DecalID,this);
	}

	// Release all of our references.  The memory in the arrays will automatically be 
	// released by the SimpleDynVecClass...
	for (i=0; i<Polys.Count(); i++) {
		REF_PTR_RELEASE(Textures[i]);
	}

	for (i=0; i<ParentVertexIndices.Count(); i++) {
		REF_PTR_RELEASE(VertexMaterials[i]);
	}
}


/***********************************************************************************************
 * SkinDecalMeshClass::Render -- Render the decals                                             *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   1/31/00    NH : Created.                                                                  *
 *=============================================================================================*/
void SkinDecalMeshClass::Render(CameraClass& camera)
{
	if (Decals.Count()==0 || !WW3D::Are_Decals_Enabled()) return;

	MeshModelClass* model = Parent->Peek_Model();
	if (model->Get_Flag(MeshModelClass::SORT)) {
		WWDEBUG_SAY(("ERROR: decals applied to a sorted mesh!\n"));
		return;
	}
	_TempVertexBuffer.Uninitialised_Grow(model->Get_Vertex_Count());
	_TempNormalBuffer.Uninitialised_Grow(model->Get_Vertex_Count());
	Parent->Get_Deformed_Vertices(&(_TempVertexBuffer[0]),&(_TempNormalBuffer[0]));
	// These transient CPU arrays are uploaded by the native renderer's frame
	// allocator. Skins keep the existing deformation; no legacy VB/state is bound.
	std::vector<NativeDecalVertex> vertices(ParentVertexIndices.Count());
	for (int i=0; i<ParentVertexIndices.Count(); ++i) {
		const Vector3& p = _TempVertexBuffer[ParentVertexIndices[i]];
		const Vector3& normal = _TempNormalBuffer[ParentVertexIndices[i]];
		vertices[i] = {p.X,p.Y,p.Z,normal.X,normal.Y,normal.Z,0xffffffff,
			TexCoords[i].X,TexCoords[i].Y,0,0};
	}
	std::vector<unsigned short> indices(Polys.Count()*3);
	for (int i=0; i<Polys.Count(); ++i) {
		indices[i*3] = static_cast<unsigned short>(Polys[i].I);
		indices[i*3+1] = static_cast<unsigned short>(Polys[i].J);
		indices[i*3+2] = static_cast<unsigned short>(Polys[i].K);
	}
	for (int first=0; first<Polys.Count();) {
		const int next = Process_Material_Run(first);
		DrawNativeDecalRun(camera,*Parent,Matrix3D::Identity,
			vertices,indices,first,next-first,Textures[first],VertexMaterials[Polys[first].I],Shaders[first]);
		first = next;
	}
}


/***********************************************************************************************
 * SkinDecalMeshClass::Process_Material_Run -- scans the mesh for material runs                *
 *                                                                                             *
 *    This function will install the materials for poly[start_index] and scan forward for      *
 *    the next material change.  It will return the start index for the next material change   *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   2/22/2001  gth : Created.                                                                 *
 *=============================================================================================*/
int SkinDecalMeshClass::Process_Material_Run(int start_index)
{

	int next_index = start_index;
	while (	(next_index < Polys.Count()) && 
				(Textures[next_index] == Textures[start_index]) &&
				(Shaders[next_index] == Shaders[start_index]) &&
				(VertexMaterials[Polys[next_index].I] == VertexMaterials[Polys[start_index].I]))
	{
		next_index++;
	}
	return next_index;
}


/***********************************************************************************************
 * SkinDecalMeshClass::Create_Decal -- Generate a new decal                                    *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 * All Decals on a mesh must be generated from the same DecalSystemClass!                      *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   1/31/00    NH : Created.                                                                  *
 *=============================================================================================*/
bool SkinDecalMeshClass::Create_Decal(DecalGeneratorClass * generator,
	const OBBoxClass & localbox, SimpleDynVecClass<uint32> & apt,
	const DynamicVectorClass<Vector3> * world_vertex_locs)
{
	int i;
	WWASSERT(generator->Peek_Decal_System() == DecalSystem);

	// The dynamically updated vertex locations are needed - we have no static geometry
	WWASSERT(world_vertex_locs);
	
	/*
	** If any polys were collected, add a new MeshDecalStruct
	*/
	if (apt.Count() == 0) {
		return false;
	}

	DecalStruct newdecal;
	newdecal.DecalID = generator->Get_Decal_ID();
	newdecal.FaceStartIndex = Polys.Count();						// start faces at the end of the current array
	newdecal.FaceCount = 0;												// init facecount to zero
	newdecal.VertexStartIndex = ParentVertexIndices.Count();	// start vertices at the end of the current array
	newdecal.VertexCount = 0;											// init vertcount to zero
	
	/*
	** Grab pointers to the parent mesh's components
	*/
	MeshModelClass * model = Parent->Peek_Model();
	const TriIndex * src_polys = model->Get_Polygon_Array();

	/*
	** Grab a pointer to the material settings
	*/
	MaterialPassClass * material = generator->Get_Material();
	
	/*
	** Set up the generator for the world coordinate system (the deformed vertices are in worldspace)
	*/
	generator->Set_Mesh_Transform(Matrix3D::Identity);
	
	/*
	** Generate the faces and per-face info (remember to add-ref's)
	** TODO: rewrite this to take advantage of vertex sharing...
	*/
	int face_size_hint = Polys.Count() + apt.Count();
	int first_vert = ParentVertexIndices.Count();
	for (i = 0; i < apt.Count(); i++) {
		int offset = first_vert + i * 3;
		Polys.Add(TriIndex(offset, offset + 1, offset + 2), face_size_hint);
		
		Shaders.Add(material->Peek_Shader(), face_size_hint);
		Textures.Add(material->Get_Texture(), face_size_hint);		// Get_Texture gives us a reference...
	}

	/*
	** Copy the vertices and per-vertex info
	** TODO: rewrite this to take advantage of vertex sharing...
	*/
	int vertex_size_hint = ParentVertexIndices.Count() + 3 * apt.Count();

	for (i = 0; i < apt.Count(); i++) {
		int face_index = apt[i];
		for (int vi = 0; vi < 3; vi++) {

			/*
			** Copy data for this vertex
			*/
			ParentVertexIndices.Add(src_polys[face_index][vi], vertex_size_hint);
			VertexMaterials.Add(material->Get_Material(), vertex_size_hint);		// Get_Material gives us a ref.

			/*
			** Compute the uv coordinates for this vertex
			*/
			Vector3 stq;
			generator->Compute_Texture_Coordinate((*world_vertex_locs)[ParentVertexIndices[ParentVertexIndices.Count() - 1]], &stq);
			TexCoords.Add(Vector2(stq.X,stq.Y));

		}
	}

	newdecal.FaceCount = Polys.Count() - newdecal.FaceStartIndex;
	newdecal.VertexCount = ParentVertexIndices.Count() - newdecal.VertexStartIndex;
	Decals.Add(newdecal);

	material->Release_Ref();

	/*
	** tell the generator that we added a MeshDecal
	*/
	generator->Add_Mesh(Parent);

#ifdef WWDEBUG	
	/*
	** Some paranoid debug code: ensure all tris have valid vertex indices
	*/
	int poly_count = Polys.Count();
	int vert_count = ParentVertexIndices.Count();
	for (int poly_idx = 0; poly_idx < poly_count; poly_idx++) {
		WWASSERT (Polys[poly_idx].I < vert_count);
		WWASSERT (Polys[poly_idx].I >= 0);
		WWASSERT (Polys[poly_idx].J < vert_count);
		WWASSERT (Polys[poly_idx].J >= 0);
		WWASSERT (Polys[poly_idx].K < vert_count);
		WWASSERT (Polys[poly_idx].K >= 0);
	}
#endif
	
	// WWDEBUG_SAY(("Decal mesh now has: %d polys\r\n",Polys.Count()));
	return true;
}


/***********************************************************************************************
 * SkinDecalMeshClass::Delete_Decal -- Delete a decal                                         *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   1/31/00    NH : Created.                                                                  *
 *=============================================================================================*/
bool SkinDecalMeshClass::Delete_Decal(uint32 id)
{
	/*
	** Find the MeshDecal which matches the given id
	*/
	int decal_index = -1;
	for (int i = 0;i < Decals.Count(); i++) {
		if (Decals[i].DecalID == id) {
			decal_index = i;
			break;
		}
	}

	if (decal_index == -1) {
		return false;
	}

	DecalStruct * decal = &Decals[decal_index];

	/*
	** Remove all geometry used by this decal 
	*/
	Polys.Delete_Range(decal->FaceStartIndex, decal->FaceCount);
	ParentVertexIndices.Delete_Range(decal->VertexStartIndex, decal->VertexCount);
	TexCoords.Delete_Range(decal->VertexStartIndex, decal->VertexCount);

	/*
	** Re-index the remaining triangle vertex indices
	*/
	for (int poly_index = 0; poly_index < Polys.Count(); poly_index++) {
		if (Polys[poly_index].I > decal->VertexStartIndex) Polys[poly_index].I -= decal->VertexCount;
		if (Polys[poly_index].J > decal->VertexStartIndex) Polys[poly_index].J -= decal->VertexCount;
		if (Polys[poly_index].K > decal->VertexStartIndex) Polys[poly_index].K -= decal->VertexCount;
	}

	/*
	** Remove all materials used by this decal (remember to release refs!)
	*/
	for (int fi = decal->FaceStartIndex; fi < decal->FaceCount; fi++) {
		REF_PTR_RELEASE(Textures[fi]);
	}
	for (int vi=decal->VertexStartIndex; vi<decal->VertexCount; vi++) {
		REF_PTR_RELEASE(VertexMaterials[vi]);
	}
	Shaders.Delete_Range(decal->FaceStartIndex,decal->FaceCount);
	Textures.Delete_Range(decal->FaceStartIndex,decal->FaceCount);
	VertexMaterials.Delete_Range(decal->VertexStartIndex,decal->VertexCount);

	/*
	** Remove MeshDecal and refresh all other decal indices
	*/
	for (int di=decal_index+1; di<Decals.Count(); di++) {
		Decals[di].FaceStartIndex -= decal->FaceCount;
		Decals[di].VertexStartIndex -= decal->VertexCount;
	}
	Decals.Delete(decal_index);

#ifdef WWDEBUG	
	/*
	** Some paranoid debug code: ensure all tris have valid vertex indices
	*/
	int poly_count = Polys.Count();
	int vert_count = ParentVertexIndices.Count();
	for (int poly_idx = 0; poly_idx < poly_count; poly_idx++) {
		WWASSERT (Polys[poly_idx].I < vert_count);
		WWASSERT (Polys[poly_idx].I >= 0);
		WWASSERT (Polys[poly_idx].J < vert_count);
		WWASSERT (Polys[poly_idx].J >= 0);
		WWASSERT (Polys[poly_idx].K < vert_count);
		WWASSERT (Polys[poly_idx].K >= 0);
	}
#endif

	return true;
}
