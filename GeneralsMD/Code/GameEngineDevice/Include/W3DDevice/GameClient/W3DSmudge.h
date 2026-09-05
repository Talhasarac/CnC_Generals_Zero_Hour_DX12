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

// FILE: W3DSmudge.h /////////////////////////////////////////////////////////

#pragma once

#ifndef _W3DSMUDGE_H_
#define _W3DSMUDGE_H_

#include "GameClient/Smudge.h"
#include <memory>
class NativeD3D12Texture;

class RenderInfoClass;

class W3DSmudgeManager : public SmudgeManager
{
public:
	W3DSmudgeManager( void );
	virtual ~W3DSmudgeManager();

	virtual void init(void);
	virtual void reset (void);

	void render (RenderInfoClass &rinfo);
	void ReleaseResources(void);
	void ReAcquireResources(void);

private:
	std::unique_ptr<NativeD3D12Texture> m_backgroundTexture;
};

#endif // _W3DSMUDGE_H_
