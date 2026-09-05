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
 *                     $Archive:: /Commando/Code/ww3d2/dx8fvf.h                               $*
 *                                                                                             *
 *              Original Author:: Jani Penttinen                                               *
 *                                                                                             *
 *                      $Author:: Kenny Mitchell                                               * 
 *                                                                                             * 
 *                     $Modtime:: 06/26/02 5:06p                                             $*
 *                                                                                             *
 *                    $Revision:: 7                                                          $*
 *                                                                                             *
 * 06/26/02 KM VB Vertex format update for shaders                                       *
 * 07/17/02 KM VB Vertex format update for displacement mapping                               *
 * 08/01/02 KM VB Vertex format update for cube mapping                               *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "dx8fvf.h"
#include "wwstring.h"

static constexpr unsigned PositionBytes(unsigned fvf)
{
	switch (fvf & D3DFVF_POSITION_MASK) {
	case D3DFVF_XYZ: return 12;
	case D3DFVF_XYZRHW: return 16;
	case D3DFVF_XYZB1: return 16;
	case D3DFVF_XYZB2: return 20;
	case D3DFVF_XYZB3: return 24;
	case D3DFVF_XYZB4: return 28;
	case D3DFVF_XYZB5: return 32;
	default: return 0;
	}
}
static constexpr unsigned TextureCoordinateBytes(unsigned fvf, unsigned stage)
{
	// The packed encoding is 0=2, 1=3, 2=4, 3=1 components, not 1..4.
	const unsigned encoded = (fvf >> (16 + stage*2)) & 3;
	return (encoded == 3 ? 1 : encoded + 2) * sizeof(float);
}
static constexpr unsigned Get_FVF_Vertex_Size(unsigned fvf)
{
	unsigned size = PositionBytes(fvf);
	if (fvf & D3DFVF_NORMAL) size += 12;
	if (fvf & D3DFVF_PSIZE) size += 4;
	if (fvf & D3DFVF_DIFFUSE) size += 4;
	if (fvf & D3DFVF_SPECULAR) size += 4;
	const unsigned count = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
	for (unsigned i=0;i<count;++i) size += TextureCoordinateBytes(fvf,i);
	return size;
}
static_assert(Get_FVF_Vertex_Size(D3DFVF_XYZ|D3DFVF_DIFFUSE|D3DFVF_TEX1)==24, "UI vertex layout");
static_assert(Get_FVF_Vertex_Size(D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_DIFFUSE|D3DFVF_TEX2)==44, "sorting vertex layout");
static_assert(Get_FVF_Vertex_Size(D3DFVF_XYZB4|D3DFVF_LASTBETA_UBYTE4|D3DFVF_NORMAL|D3DFVF_TEX1)==48, "skinned vertex layout");
static_assert(TextureCoordinateBytes(D3DFVF_TEXCOORDSIZE1(0),0)==4, "one-component UV");
static_assert(TextureCoordinateBytes(D3DFVF_TEXCOORDSIZE3(0),0)==12, "three-component UV");
static_assert(TextureCoordinateBytes(D3DFVF_TEXCOORDSIZE4(0),0)==16, "four-component UV");

FVFInfoClass::FVFInfoClass(unsigned FVF_, unsigned vertex_size)
	: FVF(FVF_), fvf_size(FVF ? Get_FVF_Vertex_Size(FVF) : vertex_size)
{
	location_offset = 0;
	blend_offset = 3*sizeof(float);
	normal_offset = PositionBytes(FVF);
	diffuse_offset = normal_offset + ((FVF&D3DFVF_NORMAL)?12:0) + ((FVF&D3DFVF_PSIZE)?4:0);
	specular_offset = diffuse_offset + ((FVF&D3DFVF_DIFFUSE)?4:0);
	texcoord_offset[0] = specular_offset + ((FVF&D3DFVF_SPECULAR)?4:0);
	for (unsigned i=1;i<D3DDP_MAXTEXCOORD;++i)
		texcoord_offset[i] = texcoord_offset[i-1]+TextureCoordinateBytes(FVF,i-1);
}

void FVFInfoClass::Get_FVF_Name(StringClass& fvfname) const
{
	switch (Get_FVF()) {
	case DX8_FVF_XYZ: fvfname="D3DFVF_XYZ"; break;
	case DX8_FVF_XYZN: fvfname="D3DFVF_XYZ|D3DFVF_NORMAL"; break;
	case DX8_FVF_XYZNUV1: fvfname="D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_TEX1"; break;
	case DX8_FVF_XYZNUV2: fvfname="D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_TEX2"; break;
	case DX8_FVF_XYZNDUV1: fvfname="D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_TEX1|D3DFVF_DIFFUSE"; break;
	case DX8_FVF_XYZNDUV2: fvfname="D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_TEX2|D3DFVF_DIFFUSE"; break;
	case DX8_FVF_XYZDUV1: fvfname="D3DFVF_XYZ|D3DFVF_TEX1|D3DFVF_DIFFUSE"; break;
	case DX8_FVF_XYZDUV2: fvfname="D3DFVF_XYZ|D3DFVF_TEX2|D3DFVF_DIFFUSE"; break;
	case DX8_FVF_XYZUV1: fvfname="D3DFVF_XYZ|D3DFVF_TEX1"; break;
	case DX8_FVF_XYZUV2: fvfname="D3DFVF_XYZ|D3DFVF_TEX2"; break;
	case DX8_FVF_XYZNDUV1TG3 : fvfname="(D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_DIFFUSE|D3DFVF_TEX4|D3DFVF_TEXCOORDSIZE2(0)|D3DFVF_TEXCOORDSIZE3(1)|D3DFVF_TEXCOORDSIZE3(2)|D3DFVF_TEXCOORDSIZE3(3))"; break;
	case DX8_FVF_XYZNUV2DMAP :	fvfname="(D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_TEX3|D3DFVF_TEXCOORDSIZE1(0)|D3DFVF_TEXCOORDSIZE4(1)|D3DFVF_TEXCOORDSIZE2(2))"; break;
	case DX8_FVF_XYZNDCUBEMAP : fvfname="(D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_DIFFUSE|D3DFVF_TEX1|D3DFVFTEXCOORDSIZE3(0)"; break;
	default: fvfname="Unknown!";
	}
}

const NativeVertexLayoutDesc& FVFInfoClass::Build_Native_Layout() const
{
	if (nativeLayoutFVF == FVF && nativeLayoutStride == fvf_size) return nativeLayout;
	nativeLayoutFVF = FVF;
	nativeLayoutStride = fvf_size;
	nativeLayout = NativeVertexLayoutDesc();
	NativeVertexLayoutDesc& layout = nativeLayout;
	const unsigned position = FVF & D3DFVF_POSITION_MASK;
	if (position != D3DFVF_XYZ)
	{
		// Native D3D12 mesh submission currently consumes CPU-deformed XYZ
		// vertices.  Keep unsupported legacy position encodings explicit rather
		// than silently describing a byte layout the native shader cannot read.
		layout.valid = false;
		return layout;
	}

	layout.stride = fvf_size;
	layout.Add(NativeVertexSemantic::Position, 0, DXGI_FORMAT_R32G32B32_FLOAT,
		location_offset);
	if (FVF & D3DFVF_NORMAL)
		layout.Add(NativeVertexSemantic::Normal, 0, DXGI_FORMAT_R32G32B32_FLOAT,
			normal_offset);
	if (FVF & D3DFVF_DIFFUSE)
		layout.Add(NativeVertexSemantic::Color, 0, DXGI_FORMAT_R8G8B8A8_UNORM,
			diffuse_offset);
	if (FVF & D3DFVF_SPECULAR)
		layout.Add(NativeVertexSemantic::Color, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
			specular_offset);

	const unsigned count = (FVF & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
	for (unsigned stage = 0; stage < count && stage < D3DDP_MAXTEXCOORD; ++stage)
	{
		const unsigned encoded = (FVF >> (16 + stage * 2)) & 3;
		const DXGI_FORMAT format = encoded == 3 ? DXGI_FORMAT_R32_FLOAT :
			(encoded == 0 ? DXGI_FORMAT_R32G32_FLOAT :
			(encoded == 1 ? DXGI_FORMAT_R32G32B32_FLOAT : DXGI_FORMAT_R32G32B32A32_FLOAT));
		layout.Add(NativeVertexSemantic::TexCoord, stage, format, texcoord_offset[stage]);
	}
	return layout;
}
