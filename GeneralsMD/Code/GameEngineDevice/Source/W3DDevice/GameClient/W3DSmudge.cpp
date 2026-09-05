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

// W3DSmudge.cpp ////////////////////////////////////////////////////////////////////////////////
// Smudge System implementation
// Author: Mark Wilczynski, June 2003
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "Lib/Basetype.h"
#include "always.h"
#include "W3DDevice/GameClient/W3DSmudge.h"
#include "WW3D2/native_d3d12_renderer.h"
#include "WW3D2/native_pipeline_description.h"
#include "WW3D2/rinfo.h"
#include "WW3D2/camera.h"
#include "WW3D2/sortingrenderer.h"
#include <algorithm>
#include <vector>

SmudgeManager *TheSmudgeManager = NULL;

namespace {
constexpr unsigned SmudgeBatchSize = 500;
struct SmudgeVertex {
	Vector3 position;
	UINT32 color;
	Vector2 uv;
};
}

W3DSmudgeManager::W3DSmudgeManager() {}
W3DSmudgeManager::~W3DSmudgeManager() { ReleaseResources(); }
void W3DSmudgeManager::init() { SmudgeManager::init(); ReAcquireResources(); }
void W3DSmudgeManager::reset() { SmudgeManager::reset(); }
void W3DSmudgeManager::ReleaseResources() { m_backgroundTexture.reset(); }
void W3DSmudgeManager::ReAcquireResources()
{
	ReleaseResources();
	m_hardwareSupportStatus = SMUDGE_SUPPORT_UNKNOWN;
}

void W3DSmudgeManager::render(RenderInfoClass& rinfo)
{
	NativeD3D12Renderer* native = NativeD3D12Renderer::Active();
	if (!native || !m_usedSmudgeSetList.Head()) return;
	bool hasSmudges = false;
	for (SmudgeSet* set=m_usedSmudgeSetList.Head();set;set=set->Succ())
		if (set->getUsedSmudgeList().Head()) { hasSmudges = true; break; }
	if (!hasSmudges) return;
	// The copy must include translucent particles, not just opaque terrain.
	SortingRendererClass::Flush();
	NativeD3D12ScopedState state(*native);
	const auto viewport = native->CaptureState().viewport;
	const UINT width = native->RenderTargetWidth(), height = native->RenderTargetHeight();
	if (!width || !height || viewport.Width <= 0 || viewport.Height <= 0) return;
	if (!m_backgroundTexture || m_backgroundTexture->Width() != width ||
		m_backgroundTexture->Height() != height || m_backgroundTexture->Format() != native->RenderTargetFormat())
		m_backgroundTexture.reset(native->CreateTexture2D(width,height,1,native->RenderTargetFormat()));
	// No CPU readback, fence wait, or sampling the resource currently bound as RTV.
	if (!m_backgroundTexture || !native->CopyCurrentRenderTarget(*m_backgroundTexture)) return;
	m_hardwareSupportStatus = SMUDGE_SUPPORT_YES;

	Matrix3D view;
	Matrix4x4 projection;
	rinfo.Camera.Get_View_Matrix(&view);
	rinfo.Camera.Get_D3D_Projection_Matrix(&projection);
	const Matrix4x4 identity(true), nativeProjection = projection.Transpose();
	native->SetWorldView(reinterpret_cast<const float*>(&identity));
	native->SetWorldViewProjection(reinterpret_cast<const float*>(&nativeProjection));
	NativePipelineDescription pipeline;
	pipeline.depthWrite = false;
	pipeline.blend = true;
	pipeline.source = D3D12_BLEND_SRC_ALPHA;
	pipeline.destination = D3D12_BLEND_INV_SRC_ALPHA;
	pipeline.Apply(*native);
	native->SetMaterialEnabled(false);
	// Keep the existing warm tint; opacity comes from vertices, never target alpha.
	native->SetTextureCombine(true,true,false,true);
	native->SetSamplerState(NativeD3D12FilterMode::Linear,NativeD3D12FilterMode::Linear,
		NativeD3D12FilterMode::Point,true,true);
	native->SetTreeSway(NULL,0);
	native->SetLighting(NativeLightingState());
	native->SetGrayscale(false);
	native->SetVertexFog(0,0,1,1,0,false);
	native->SetStencilState(false,D3D12_COMPARISON_FUNC_ALWAYS,0,255,255,
		D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP);

	std::vector<SmudgeVertex> vertices;
	vertices.reserve(SmudgeBatchSize*5);
	std::vector<unsigned short> indices(SmudgeBatchSize*12);
	const unsigned short fan[] = {0,4,3,3,4,2,2,4,1,1,4,0};
	for (unsigned i=0;i<SmudgeBatchSize;++i)
		for (unsigned j=0;j<12;++j) indices[i*12+j] = static_cast<unsigned short>(i*5+fan[j]);
	auto flush = [&]() {
		if (vertices.empty()) return;
		native->DrawIndexedTextured(vertices.data(),static_cast<UINT>(vertices.size()*sizeof(SmudgeVertex)),
			sizeof(SmudgeVertex),static_cast<UINT>(vertices.size()),offsetof(SmudgeVertex,uv),
			indices.data(),static_cast<UINT>(vertices.size()/5*12),D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
			m_backgroundTexture.get(),offsetof(SmudgeVertex,color));
		vertices.clear();
	};
	const Vector3 offsets[] = {Vector3(-.5f,.5f,0),Vector3(-.5f,-.5f,0),
		Vector3(.5f,-.5f,0),Vector3(.5f,.5f,0)};
	for (SmudgeSet* set=m_usedSmudgeSetList.Head();set;set=set->Succ()) {
		for (Smudge* smudge=set->getUsedSmudgeList().Head();smudge;smudge=smudge->Succ()) {
			Vector3 center;
			Matrix3D::Transform_Vector(view,smudge->m_pos,&center);
			const Vector4 clip = projection * center;
			if (clip.W <= .0001f || clip.Z < 0 || smudge->m_size <= 0) continue;
			SmudgeVertex quad[5];
			Vector2 distortion = smudge->m_offset;
			for (unsigned i=0;i<4;++i) {
				quad[i].position = center+offsets[i]*smudge->m_size;
				const Vector4 projected = projection*quad[i].position;
				const float x = (projected.X/projected.W+1)*.5f;
				const float y = (1-projected.Y/projected.W)*.5f;
				quad[i].uv.Set((viewport.TopLeftX+x*viewport.Width)/width,
					(viewport.TopLeftY+y*viewport.Height)/height);
				quad[i].color = 0x00ffeedd;
				if (x < 0 || x > 1) distortion.X = 0;
				if (y < 0 || y > 1) distortion.Y = 0;
			}
			quad[4].position = center;
			quad[4].uv.Set(quad[0].uv.X+(quad[3].uv.X-quad[0].uv.X)*(.5f+distortion.X),
				quad[0].uv.Y+(quad[1].uv.Y-quad[0].uv.Y)*(.5f+distortion.Y));
			const float opacity = (std::max)(0.f,(std::min)(1.f,smudge->m_opacity));
			quad[4].color = (UINT32(opacity*255)<<24)|0x00ffeedd;
			vertices.insert(vertices.end(),quad,quad+5);
			if (vertices.size() == SmudgeBatchSize*5) flush();
		}
	}
	flush();
}
