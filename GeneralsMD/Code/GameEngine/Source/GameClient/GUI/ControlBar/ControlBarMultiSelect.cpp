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

// FILE: ControlBarMultiSelect.cpp ////////////////////////////////////////////////////////////////
// Author: Colin Day, March 2002
// Desc:   Context sensitive GUI for when you select mutiple objects.  What we do is show
//				 the commands that you can use between them all
///////////////////////////////////////////////////////////////////////////////////////////////////

// USER INCLUDES //////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/ThingTemplate.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/InGameUI.h"
#include "GameLogic/Object.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif




//-------------------------------------------------------------------------------------------------
/** Populate the visible command bar with commands that are common to all the objects
	* that are selected in the UI */
//-------------------------------------------------------------------------------------------------
void ControlBar::populateMultiSelect( void )
{

	// sanity
	DEBUG_ASSERTCRASH( TheInGameUI->getSelectCount() > 1,
										 ("populateMultiSelect: Can't populate multiselect context cause there are only '%d' things selected\n",
										  TheInGameUI->getSelectCount()) );

	//
	// group the selection by unit type for the right HUD, then let the focused type drive
	// the whole context: its portrait, its command set (build pages included) and the strip
	// of type cameos.  Tab/Shift-Tab move the focus.
	//
	populateMultiSelectUnitList();

}  // end populateMultiSelect

//-------------------------------------------------------------------------------------------------
/** Show what a multi-selection holds in the right HUD: one cameo per selected unit type on the
	* small strip windows, each with a count badge.  Tab/Shift-Tab move the focus between the
	* types; the focused type gets the portrait window and its cameo is the lit one. */
//-------------------------------------------------------------------------------------------------
void ControlBar::populateMultiSelectUnitList( void )
{

	// remember what was focused - a repopulate (a unit died, a build page turned) should not
	// yank the player onto another type
	const ThingTemplate *focusedTemplate = NULL;
	if( m_multiSelectFocus >= 0 && m_multiSelectFocus < m_multiSelectGroupCount )
		focusedTemplate = m_multiSelectGroupTemplate[ m_multiSelectFocus ];

	m_multiSelectGroupCount = 0;

	const DrawableList *selectedDrawables = TheInGameUI->getAllSelectedDrawables();
	for( DrawableListCIt it = selectedDrawables->begin(); it != selectedDrawables->end(); ++it )
	{
		Drawable *draw = *it;
		Object *obj = draw ? draw->getObject() : NULL;

		// same filter the command population uses
		if( obj == NULL || obj->isKindOf( KINDOF_IGNORED_IN_GUI ) ||
				obj->getStatusBits().test( OBJECT_STATUS_SOLD ) )
			continue;

		// find this unit's type group, or open a new one
		const ThingTemplate *thing = draw->getTemplate();
		Int g;
		for( g = 0; g < m_multiSelectGroupCount; g++ )
			if( m_multiSelectGroupTemplate[ g ] == thing )
				break;
		if( g == m_multiSelectGroupCount )
		{
			// ponytail: MAX_MULTI_SELECT_GROUPS unit types; a selection with more than that
			// drops the tail types from the display
			if( m_multiSelectGroupCount >= MAX_MULTI_SELECT_GROUPS )
				continue;
			m_multiSelectGroupTemplate[ g ] = thing;
			m_multiSelectGroupSize[ g ] = 0;
			m_multiSelectGroupFirst[ g ] = draw->getID();
			m_multiSelectGroupCount++;
		}
		m_multiSelectGroupSize[ g ]++;

	}  // end for, selected drawables

	// one cell per type, laid out n x n over the right HUD
	layoutMultiSelectTiles( m_multiSelectGroupCount );

	// stay on the type that was focused if it is still selected
	Int focus = 0;
	for( Int g = 0; g < m_multiSelectGroupCount; g++ )
		if( m_multiSelectGroupTemplate[ g ] == focusedTemplate )
			focus = g;

	m_multiSelectFocus = focus;
	setMultiSelectFocus( focus );

}  // end populateMultiSelectUnitList

//-------------------------------------------------------------------------------------------------
/** Focus one type group of the multi-selection: its portrait and count go to the portrait
	* window, and the strip below shows every group's cameo with the focused one lit */
//-------------------------------------------------------------------------------------------------
void ControlBar::setMultiSelectFocus( Int index )
{

	if( m_multiSelectGroupCount == 0 )
		return;

	// wrap in both directions
	m_multiSelectFocus = ( index % m_multiSelectGroupCount + m_multiSelectGroupCount )
											 % m_multiSelectGroupCount;

	Drawable *draw = TheGameClient->findDrawableByID( m_multiSelectGroupFirst[ m_multiSelectFocus ] );
	Object *obj = draw ? draw->getObject() : NULL;
	if( obj == NULL )
		return;		// the next selection change repopulates the groups

	//
	// the focused type's representative drives the whole context: the command area shows its
	// command set (a builder's structure pages included), and updateContextMultiSelect keeps
	// judging those buttons through the single-selection update
	//
	m_currentSelectedDrawable = draw;
	populateCommand( obj );

	// the previous representative's production queue does not belong to this one; the update
	// re-shows the panel if the new representative is producing
	m_contextParent[ CP_BUILD_QUEUE ]->winHide( TRUE );

	// ... and the 3x3 type grid in the right HUD
	updateMultiSelectStrip();

}  // end setMultiSelectFocus

//-------------------------------------------------------------------------------------------------
/** Keep the 3x3 unit-type grid of a multi-selection alive.  Called every frame: the
	* single-selection update trades the portrait back and forth with the build queue panel
	* and setPortraitByObject hides the grid cells, so the multi-select look has to be
	* re-applied after them. */
//-------------------------------------------------------------------------------------------------
void ControlBar::updateMultiSelectStrip( void )
{

	if( m_currContext != CB_CONTEXT_MULTI_SELECT || m_multiSelectGroupCount == 0 )
		return;

	//
	// a selection of one unit type reads like a single selection: its portrait, upgrade
	// cameos and all, with how many are selected written on the portrait
	//
	if( m_multiSelectGroupCount == 1 )
	{
		Drawable *draw = TheGameClient->findDrawableByID( m_multiSelectGroupFirst[ 0 ] );
		Object *obj = draw ? draw->getObject() : NULL;
		setPortraitByObject( obj );		// also hides the grid cells
		if( obj )
			GadgetButtonSetCount( m_rightHUDCameoWindow, m_multiSelectGroupSize[ 0 ] );
		return;
	}

	// the grid replaces the portrait while a mixed multi-selection is up
	setPortraitByObject( NULL );

	// one cell per selected type with its count; the focused type is the lit one, the rest
	// wear the darkened overlay state
	for( size_t i = 0; i < m_multiSelectTiles.size(); i++ )
	{
		GameWindow *win = m_multiSelectTiles[ i ];
		if( win == NULL )
			continue;

		if( (Int)i < m_multiSelectGroupCount )
		{
			win->winHide( FALSE );
			win->winSetEnabledImage( 0, m_multiSelectGroupTemplate[ i ]->getButtonImage() );
			win->winEnable( (Int)i == m_multiSelectFocus );
			GadgetButtonSetCount( win, m_multiSelectGroupSize[ i ] );
		}
		else
			win->winHide( TRUE );
	}

}  // end updateMultiSelectStrip

//-------------------------------------------------------------------------------------------------
/** Make sure 'count' grid cells exist and lay them out n x n over the right HUD, n the
	* smallest square that holds them: one type fills the HUD, 2-4 get 2x2, 5-9 get 3x3, and
	* so on.  Cells are plain windows created in code (ControlBar.wnd has none to spare) that
	* borrow the push button image draw, so cameos, count badges and the darkened disabled
	* state work. */
//-------------------------------------------------------------------------------------------------
void ControlBar::layoutMultiSelectTiles( Int count )
{
	if( m_rightHUDWindow == NULL || count <= 0 )
		return;

	Int n = 1;
	while( n * n < count )
		n++;

	ICoord2D hudSize;
	m_rightHUDWindow->winGetSize( &hudSize.x, &hudSize.y );
	Int cellW = hudSize.x / n;
	Int cellH = hudSize.y / n;

	for( Int i = 0; i < count; i++ )
	{
		if( (Int)m_multiSelectTiles.size() <= i )
		{
			GameWindow *tile = TheWindowManager->winCreate( m_rightHUDWindow,
													WIN_STATUS_ENABLED | WIN_STATUS_USE_OVERLAY_STATES | WIN_STATUS_HIDDEN,
													0, 0, cellW, cellH, GameWinDefaultSystem );
			if( tile )
				tile->winSetDrawFunc( TheWindowManager->getPushButtonImageDrawFunc() );
			m_multiSelectTiles.push_back( tile );
		}

		GameWindow *tile = m_multiSelectTiles[ i ];
		if( tile == NULL )
			continue;
		Int col = i % n;
		Int row = i / n;
		tile->winSetPosition( col * cellW + 1, row * cellH + 1 );
		tile->winSetSize( cellW - 2, cellH - 2 );
	}

}  // end layoutMultiSelectTiles

//-------------------------------------------------------------------------------------------------
/** Tab (+1) / Shift-Tab (-1): walk the focus through the multi-selected units */
//-------------------------------------------------------------------------------------------------
void ControlBar::cycleMultiSelectFocus( Int direction )
{

	if( m_currContext != CB_CONTEXT_MULTI_SELECT )
		return;

	setMultiSelectFocus( m_multiSelectFocus + direction );

}  // end cycleMultiSelectFocus

//-------------------------------------------------------------------------------------------------
/** Update logic for the multi select context sensitive GUI.  The command area mirrors the
	* focused type's representative, so the single-selection update does all the work:
	* availability, build queue panel, reload clocks, queue count badges. */
//-------------------------------------------------------------------------------------------------
void ControlBar::updateContextMultiSelect( void )
{

	// the representative can die between the deselect event and the UI re-evaluation
	if( m_currentSelectedDrawable == NULL || m_currentSelectedDrawable->getObject() == NULL )
		return;

	updateContextCommand();

	// the update may have traded the portrait for a build queue - keep the type strip alive
	updateMultiSelectStrip();

}  // end updateContextMultiSelect
