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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: W3DCustomEdging.cpp ////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Westwood Studios Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2001 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
// Project:   RTS3
//
// File name: W3DCustomEdging.cpp
//
// Created:   John Ahlquist, May 2001
//
// Desc:      Draw buffer to handle all the custom tile edges in a scene.
//
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//         Includes                                                      
//-----------------------------------------------------------------------------
#include "W3DDevice/GameClient/W3DCustomEdging.h"

#include <stdio.h>
#include <string.h>
#include <assetmgr.h>
#include <texture.h>
#include "common/GlobalData.h"
#include "common/RandomValue.h"
#include "W3DDevice/GameClient/TerrainTex.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "W3DDevice/GameClient/W3DDynamicLight.h"
#include "WW3D2/Camera.h"
#include "WW3D2/DX8Wrapper.h"
#include "WW3D2/DX8Renderer.h"
#include "WW3D2/Mesh.h"
#include "WW3D2/MeshMdl.h"
#include "W3DDevice/GameClient/NativeTerrainDraw.h"
#include "WW3D2/native_pipeline_description.h"
#include "W3DDevice/GameClient/W3DShaderManager.h"

//-----------------------------------------------------------------------------
//         Private Data                                                     
//-----------------------------------------------------------------------------
// A W3D shader that does alpha, texturing, tests zbuffer, doesn't update zbuffer.
#define SC_ALPHA_DETAIL ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_DISABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_SRC_ALPHA, \
	ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::ALPHATEST_ENABLE, ShaderClass::CULL_MODE_DISABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

static ShaderClass detailAlphaTestShader(SC_ALPHA_DETAIL);


#define SC_NO_ALPHA ( SHADE_CNST(ShaderClass::PASS_ALWAYS, ShaderClass::DEPTH_WRITE_DISABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_ONE, \
	ShaderClass::DSTBLEND_ZERO, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_DISABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

static ShaderClass detailShader(SC_NO_ALPHA);

#define SC_DETAIL_BLEND ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_ENABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_ONE, \
	ShaderClass::DSTBLEND_ZERO, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_ENABLE, ShaderClass::DETAILCOLOR_SCALE, ShaderClass::DETAILALPHA_DISABLE) )

static ShaderClass detailOpaqueShader(SC_DETAIL_BLEND);

/*
#define SC_ALPHA_MIRROR ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_DISABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_SRC_ALPHA, \
	ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE, ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_DISABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

static ShaderClass mirrorAlphaShader(SC_ALPHA_DETAIL);

// ShaderClass::PASS_ALWAYS, 

#define SC_ALPHA_2D ( SHADE_CNST(PASS_ALWAYS, DEPTH_WRITE_DISABLE, COLOR_WRITE_ENABLE, \
	SRCBLEND_SRC_ALPHA, DSTBLEND_ONE_MINUS_SRC_ALPHA, FOG_DISABLE, GRADIENT_DISABLE, \
	SECONDARY_GRADIENT_DISABLE, TEXTURING_ENABLE, DETAILCOLOR_DISABLE, DETAILALPHA_DISABLE, \
	ALPHATEST_DISABLE, CULL_MODE_ENABLE, DETAILCOLOR_DISABLE, DETAILALPHA_DISABLE) )
ShaderClass ShaderClass::_PresetAlpha2DShader(SC_ALPHA_2D);
*/
//-----------------------------------------------------------------------------
//         Private Functions                                               
//-----------------------------------------------------------------------------

//=============================================================================
// W3DCustomEdging::loadEdgingsInVertexAndIndexBuffers
//=============================================================================
/** Loads the trees into the vertex buffer for drawing. */
//=============================================================================
void W3DCustomEdging::loadEdgingsInVertexAndIndexBuffers(WorldHeightMap *pMap, Int minX, Int maxX, Int minY, Int maxY)
{
	if (!m_indexEdging || !m_vertexEdging || !m_initialized) {
		return;
	}
	if (!m_anythingChanged) {
		return;
	}
	m_anythingChanged = false;
	m_curNumEdgingVertices = 0;
	m_curNumEdgingIndices = 0;
	VertexFormatXYZDUV2 *vb;
	UnsignedShort *ib;
	// Lock the buffers.
	DX8IndexBufferClass::WriteLockClass lockIdxBuffer(m_indexEdging);
	DX8VertexBufferClass::WriteLockClass lockVtxBuffer(m_vertexEdging);
	vb=(VertexFormatXYZDUV2*)lockVtxBuffer.Get_Vertex_Array();
	ib = lockIdxBuffer.Get_Index_Array();

	UnsignedShort *curIb = ib;

	VertexFormatXYZDUV2 *curVb = vb;

	if (minX<0) minX = 0;
	if (minY<0) minY = 0;
	if (maxX >= pMap->getXExtent()) maxX = pMap->getXExtent()-1;
	if (maxY >= pMap->getYExtent()) maxY = pMap->getYExtent()-1;
	Int row;
	Int column;
	try {
	for (row=minY; row<maxY-1; row++) {
		for (column = minX; column < maxX-1; column++) {
			Int cellNdx = column+row*pMap->getXExtent();
			Int blend = pMap->m_blendTileNdxes[cellNdx];
			if (blend == 0) continue; // no blend.

			if (pMap->m_blendedTiles[blend].customBlendEdgeClass<0) continue; // alpha blend.
			Int i, j;
			Real uOffset;
			Real vOffset;

			if (pMap->m_blendedTiles[blend].horiz) {
				uOffset = 0;
				vOffset = 0.25f * (1 + (row&1));
				if (pMap->m_blendedTiles[blend].inverted) {
					uOffset = 0.75f;
				}
			} else if (pMap->m_blendedTiles[blend].vert) {
				vOffset = 0.75;
				uOffset = 0.25f * (1 + (column&1));
				if (pMap->m_blendedTiles[blend].inverted) {
					vOffset = 0.0f;
				}
			} else if (pMap->m_blendedTiles[blend].rightDiagonal) {
				if (pMap->m_blendedTiles[blend].longDiagonal) {
					vOffset = 0.25;
					uOffset = 0.5;
					if (pMap->m_blendedTiles[blend].inverted) {
						uOffset = 0.5f;
						vOffset = 0.5f;
					}
				} else {
					vOffset = .75;
					uOffset = 0;
					if (pMap->m_blendedTiles[blend].inverted) {
						uOffset = 0.0f;
						vOffset = 0.0f;
					}
				}
			} else if (pMap->m_blendedTiles[blend].leftDiagonal) {
				if (pMap->m_blendedTiles[blend].longDiagonal) {
					uOffset = 0.25f;
					vOffset = 0.25f;
					if (pMap->m_blendedTiles[blend].inverted) {
						uOffset = 0.25f;
						vOffset = 0.5f;
					}
				} else {
					vOffset = 0.75;
					uOffset = 0.75f;
					if (pMap->m_blendedTiles[blend].inverted) {
						uOffset = 0.75f;
						vOffset = 0.0f;
					}
				}
			}	else {
				continue;
			}
			Region2D range;
			pMap->getUVForBlend(pMap->m_blendedTiles[blend].customBlendEdgeClass, &range);

			uOffset = range.lo.x + range.width()*uOffset;
			vOffset = range.lo.y + range.height()*vOffset;

			UnsignedByte alpha[4];
			float UA[4], VA[4];
			Bool flipForBlend;
			pMap->getAlphaUVData(column-pMap->getDrawOrgX(), row-pMap->getDrawOrgY(), UA, VA, alpha, &flipForBlend, false);


			Int startVertex = m_curNumEdgingVertices;
			for (j=0; j<2; j++) {
				for (i=0; i<2; i++) {
					if (m_curNumEdgingVertices >= MAX_EDGE_VERTEX) return;
					cellNdx = column+i+(row+j)*pMap->getXExtent();

					Int diffuse = TheTerrainRenderObject->getStaticDiffuse(column+i, row+j);
					curVb->diffuse = 0x80000000 + (diffuse&0x00FFFFFF); // set alpha to 5b.
					Real theZ; 
					theZ = ((float)pMap->getDataPtr()[cellNdx])*MAP_HEIGHT_SCALE;
					Real X = (column+i)*MAP_XY_FACTOR; 
					Real Y = (row+j)*MAP_XY_FACTOR;
					curVb->u2 = uOffset + i*0.25f*range.width();
					curVb->v2 = vOffset + (1-j)*0.25f*range.height();
					Int ndx;
					if (j==0) ndx=i;
					if (j==1) ndx = 3-i;
					curVb->u1 = UA[ndx];
					curVb->v1 = VA[ndx];
					curVb->x = X;
					curVb->y = Y;
					curVb->z = theZ;
					curVb++;
					m_curNumEdgingVertices++;
				}
			}
			Int yOffset = 2;
			if (m_curNumEdgingIndices+6 > MAX_EDGE_INDEX) return;
#ifdef FLIP_TRIANGLES // jba - reduces "diamonding" in some cases, not others.  Better cliffs, though.
			if (flipForBlend) {
				*curIb++ = startVertex + 1;
 				*curIb++ = startVertex + yOffset;
				*curIb++ = startVertex ;
 				*curIb++ = startVertex + 1;
 				*curIb++ = startVertex + 1+yOffset;
				*curIb++ = startVertex + yOffset;
			}	
			else 
#endif
			{
				*curIb++ = startVertex;
				*curIb++ = startVertex + 1+yOffset;
				*curIb++ = startVertex + yOffset;
				*curIb++ = startVertex ;
				*curIb++ = startVertex + 1;
				*curIb++ = startVertex + 1+yOffset;
			}
			m_curNumEdgingIndices+=6;
		}
	}
	IndexBufferExceptionFunc();
	} catch(...) {
		IndexBufferExceptionFunc();
	}
	m_anythingChanged = false;
}

//-----------------------------------------------------------------------------
//         Public Functions                                                
//-----------------------------------------------------------------------------

//=============================================================================
// W3DCustomEdging::~W3DCustomEdging
//=============================================================================
/** Destructor. Releases w3d assets. */
//=============================================================================
W3DCustomEdging::~W3DCustomEdging(void)
{
	freeEdgingBuffers();
}

//=============================================================================
// W3DCustomEdging::W3DCustomEdging
//=============================================================================
/** Constructor. Sets m_initialized to true if it finds the w3d models it needs
for the trees. */
//=============================================================================
W3DCustomEdging::W3DCustomEdging(void)
{
	m_initialized = false;
	m_vertexEdging = NULL;
	m_indexEdging = NULL;
	clearAllEdging();
	allocateEdgingBuffers();
	m_initialized = true;
}


//=============================================================================
// W3DCustomEdging::freeEdgingBuffers
//=============================================================================
/** Frees the index and vertex buffers. */
//=============================================================================
void W3DCustomEdging::freeEdgingBuffers(void)
{
	REF_PTR_RELEASE(m_vertexEdging);
	REF_PTR_RELEASE(m_indexEdging);
}

//=============================================================================
// W3DCustomEdging::allocateEdgingBuffers
//=============================================================================
/** Allocates the index and vertex buffers. */
//=============================================================================
void W3DCustomEdging::allocateEdgingBuffers(void)
{
	m_vertexEdging=NEW_REF(DX8VertexBufferClass,(DX8_FVF_XYZDUV2,MAX_EDGE_VERTEX+4,DX8VertexBufferClass::USAGE_DYNAMIC));
	m_indexEdging=NEW_REF(DX8IndexBufferClass,(2*MAX_EDGE_INDEX+4, DX8IndexBufferClass::USAGE_DYNAMIC));
	m_curNumEdgingVertices=0;
	m_curNumEdgingIndices=0;
	//m_edgeTexture = MSGNEW("TextureClass") TextureClass("EdgingTemplate.tga","EdgingTemplate.tga", TextureClass::MIP_LEVELS_3);
}

//=============================================================================
// W3DCustomEdging::clearAllEdging
//=============================================================================
/** Removes all trees. */
//=============================================================================
void W3DCustomEdging::clearAllEdging(void)
{
	m_curNumEdgingVertices=0;				  
	m_curNumEdgingIndices=0;
	m_anythingChanged = true;
}




//=============================================================================
// W3DCustomEdging::drawEdging
//=============================================================================
/** Draws the trees.  Uses camera to cull. */
//=============================================================================
void W3DCustomEdging::drawEdging(CameraClass& camera, const Matrix3D& world, WorldHeightMap *pMap, Int minX, Int maxX, Int minY, Int maxY,
		TextureClass * terrainTexture, TextureClass * cloudTexture, TextureClass * noiseTexture) 
{
	static Bool foo = false;
	if (foo) {
		return;
	}
	//m_anythingChanged = true;
	loadEdgingsInVertexAndIndexBuffers(pMap, minX, maxX, minY, maxY);

	if (m_curNumEdgingIndices == 0) {
		return;
	}
	auto* native=NativeD3D12Renderer::Active();
	TextureClass* edgeTex=pMap->getEdgeTerrainTexture();
	if (!native || !terrainTexture || !edgeTex || !m_vertexEdging || !m_indexEdging) return;
	const auto* vb=m_vertexEdging->Get_Native_Vertex_Buffer();
	const auto* ib=m_indexEdging->Get_Native_Index_Buffer();
	const auto layout=FVFInfoClass(DX8_FVF_XYZDUV2).Build_Native_Layout();
	const size_t first=size_t(m_curEdgingIndexOffset)*2;
	if (!vb || !ib || first>ib->Size() || size_t(m_curNumEdgingIndices)*2>ib->Size()-first ||
		size_t(m_curNumEdgingVertices)*layout.stride>vb->Size()) return;
	NativeD3D12ScopedState restore(*native);
	NativeTerrainSetCameraMatrices(*native,&camera,world);
	native->SetLighting(NativeLightingState()); native->SetTreeSway(nullptr,0);
	native->SetVertexFog(0,0,1,0,0,false); native->SetDepthBias(0);
	NativeDrawSubmission draw;
	draw.vertices=vb->Data(); draw.vertexBytes=UINT(vb->Size()); draw.vertexStride=layout.stride;
	draw.vertexCount=m_curNumEdgingVertices; draw.layout=layout;
	draw.indices=reinterpret_cast<const unsigned short*>(static_cast<const unsigned char*>(ib->Data())+first);
	draw.indexCount=m_curNumEdgingIndices; draw.vertexOwner=vb; draw.indexOwner=ib; draw.useMaterial=true;
	const auto* edge=edgeTex->Prepare_Native_Texture();
	const auto* terrain=terrainTexture->Prepare_Native_Texture();
	if (!edge || !terrain) return;
	const UINT uv0=layout.Find_Offset(NativeVertexSemantic::TexCoord);
	const UINT uv1=layout.Find_Offset(NativeVertexSemantic::TexCoord,1);
	const auto makeMaterial=[&](const NativeD3D12Texture* color,UINT uv,bool mask) {
		auto material=detailAlphaTestShader.Get_Native_Texture_Material();
		material.textures[0]=color; material.coordinates[0].offset=uv;
		material.samplers[0]=terrainTexture->Get_Filter().Get_Native_Description();
		// The edge texture encodes the two sides of the split around alpha 128.
		material.stages[0].alphaOp=NativeMaterialOp::Select1;
		material.stages[0].alphaArg1=UINT(NativeMaterialSource::Texture);
		if (mask) {
			material.textures[1]=edge; material.coordinates[1].offset=uv1;
			material.samplers[1]=edgeTex->Get_Filter().Get_Native_Description();
			material.stages[1].colorOp=NativeMaterialOp::Select1;
			material.stages[1].colorArg1=UINT(NativeMaterialSource::Current);
			material.stages[1].alphaOp=NativeMaterialOp::Select1;
			material.stages[1].alphaArg1=UINT(NativeMaterialSource::Texture);
		}
		return material;
	};
	auto pipeline=detailAlphaTestShader.Get_Native_Pipeline();
	pipeline.colorMask &= native->CaptureState().renderTargetWriteMask;
	pipeline.Apply(*native);
	draw.material=makeMaterial(terrain,uv0,true);
	native->SetAlphaTestState(true,D3D12_COMPARISON_FUNC_LESS_EQUAL,0x7b);
	Submit_Native_Draw(*native,draw);
	draw.material=makeMaterial(edge,uv1,false);
	draw.material.samplers[0]=edgeTex->Get_Filter().Get_Native_Description();
	native->SetAlphaTestState(true,D3D12_COMPARISON_FUNC_GREATER_EQUAL,0x84);
	Submit_Native_Draw(*native,draw);
	// Modulation coordinates are authored explicitly, never inherited from the
	// terrain's last sampler or mapper. Each optional pass is independent.
	for (UINT pass=0;pass<2;++pass) {
		TextureClass* texture=pass==0 ? cloudTexture : noiseTexture;
		if (!texture) continue;
		const auto* modulation=texture->Prepare_Native_Texture();
		if (!modulation) continue;
		draw.material=makeMaterial(modulation,UINT_MAX,true);
		auto uv=pass==0 ? W3DShaderManager::nativeCloudCoordinates() : W3DShaderManager::nativeNoiseCoordinates();
		Matrix4x4 projected; std::memcpy(&projected,uv.matrix.data(),sizeof(projected));
		projected=Matrix4x4(world).Transpose()*projected;
		std::memcpy(uv.matrix.data(),&projected,sizeof(projected));
		draw.material.coordinates[0]=uv;
		draw.material.samplers[0]=texture->Get_Filter().Get_Native_Description();
		pipeline.blend=true; pipeline.source=D3D12_BLEND_DEST_COLOR; pipeline.destination=D3D12_BLEND_ZERO;
		pipeline.depthWrite=false; pipeline.Apply(*native);
		native->SetAlphaTestState(true,D3D12_COMPARISON_FUNC_NOT_EQUAL,0x80);
		Submit_Native_Draw(*native,draw);
	}
}


