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

// FILE: W3DSnow.h /////////////////////////////////////////////////////////

#ifndef _W3DSNOW_H_
#define _W3DSNOW_H_

#include "GameClient/Snow.h"

#include <memory>
class NativeD3D12UploadBuffer;
class RenderInfoClass;
class TextureClass;

class W3DSnowManager : public SnowManager
{
  public :

	W3DSnowManager(void);
	~W3DSnowManager(void);

	virtual void init( void );
	virtual void reset( void );
	virtual void update ( void);
	virtual void updateIniSettings(void);
	
	void	render(RenderInfoClass &rinfo);
	void	renderAsQuads(RenderInfoClass &rinfo, Int cubeOriginX, Int cubeOriginY, Int cubeDimX, Int cubeDimY);
	void	ReleaseResources(void);
	Bool	ReAcquireResources(void);

 private:
	std::unique_ptr<NativeD3D12UploadBuffer> m_indexBuffer;
	TextureClass *m_snowTexture;
	Real m_snowCeiling;	///<height at the top of the cube with camera at center.
	Real m_heightTraveled;	///<height that snow flake traveled this frame.
	Int m_totalRendered;	///<total number of snow particles rendered this frame - only for profiling.
};

#endif // _W3DSNOW_H_

