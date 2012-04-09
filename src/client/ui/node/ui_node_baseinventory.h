/**
 * @file
 */

/*
Copyright (C) 2002-2011 UFO: Alien Invasion.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#ifndef CLIENT_UI_UI_NODE_BASEINVENTORY_H
#define CLIENT_UI_UI_NODE_BASEINVENTORY_H

#include "ui_node_container.h"
#include "ui_node_abstractscrollable.h"

class uiBaseInventoryNode : public uiContainerNode {
	void draw(uiNode_t* node) OVERRIDE;
	void drawTooltip(uiNode_t* node, int x, int y) OVERRIDE;
	void mouseDown(uiNode_t* node, int x, int y, int button) OVERRIDE;
	void mouseUp(uiNode_t* node, int x, int y, int button) OVERRIDE;
	bool scroll(uiNode_t* node, int deltaX, int deltaY) OVERRIDE;
	void capturedMouseMove(uiNode_t* node, int x, int y) OVERRIDE;
	void windowOpened(uiNode_t* node, linkedList_t *params) OVERRIDE;
	void loading(uiNode_t* node) OVERRIDE;
	void loaded(uiNode_t* node) OVERRIDE;
	bool dndEnter(uiNode_t* node) OVERRIDE;
	bool dndMove(uiNode_t* node, int x, int y) OVERRIDE;
	void dndLeave(uiNode_t* node) OVERRIDE;
};

/* prototypes */
struct uiBehaviour_s;

typedef struct baseInventoryExtraData_s {
	containerExtraData_t super;

	/** A filter */
	int filterEquipType;

	int columns;
	qboolean displayWeapon;
	qboolean displayAmmo;
	qboolean displayUnavailableItem;
	qboolean displayAmmoOfWeapon;
	qboolean displayUnavailableAmmoOfWeapon;
	qboolean displayAvailableOnTop;

	/* scroll status */
	uiScroll_t scrollY;
	/* scroll callback when the status change */
	struct uiAction_s *onViewChange;

} baseInventoryExtraData_t;

/**
 * @note super must be at 0 else inherited function will crash.
 */
CASSERT(offsetof(baseInventoryExtraData_t, super) == 0);

void UI_RegisterBaseInventoryNode(struct uiBehaviour_s *behaviour);

#endif
