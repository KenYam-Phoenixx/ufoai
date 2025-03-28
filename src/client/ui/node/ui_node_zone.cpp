/**
 * @file
 * @brief The <code>zone</code> node allow to create an hidden active node.
 * Currently we only use it to support repeat mouse actions without merging
 * the code which managing this feature.
 */

/*
Copyright (C) 2002-2025 UFO: Alien Invasion.

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

#include "../ui_nodes.h"
#include "../ui_parse.h"
#include "../ui_behaviour.h"
#include "../ui_input.h"
#include "../ui_timer.h"
#include "../ui_actions.h"
#include "ui_node_zone.h"
#include "ui_node_window.h"

#include "../../input/cl_keys.h"

#include "../../../common/scripts_lua.h"

#define EXTRADATA_TYPE zoneExtraData_t
#define EXTRADATA(node) UI_EXTRADATA(node, EXTRADATA_TYPE)

static uiTimer_t* capturedTimer;

static void UI_ZoneNodeRepeat (uiNode_t* node, uiTimer_t* timer)
{
	if (node->onClick) {
		UI_ExecuteEventActions(node, node->onClick);
	}
}

void uiZoneNode::onMouseDown (uiNode_t* node, int x, int y, int button)
{
	if (!EXTRADATA(node).repeat)
		return;
	if (button == K_MOUSE1) {
		UI_SetMouseCapture(node);
		capturedTimer = UI_AllocTimer(node, EXTRADATA(node).clickDelay, UI_ZoneNodeRepeat);
		UI_TimerStart(capturedTimer);
	}
}

void uiZoneNode::onMouseUp (uiNode_t* node, int x, int y, int button)
{
	if (!EXTRADATA(node).repeat)
		return;
	if (button == K_MOUSE1) {
		UI_MouseRelease();
	}
}

/**
 * @brief Called when the node have lost the captured node
 * We clean cached data
 */
void uiZoneNode::onCapturedMouseLost (uiNode_t* node)
{
	if (capturedTimer) {
		UI_TimerRelease(capturedTimer);
		capturedTimer = nullptr;
	}
}

/**
 * @brief Call before the script initialized the node
 */
void uiZoneNode::onLoading (uiNode_t* node)
{
	EXTRADATA(node).clickDelay = 1000;
}

void UI_RegisterZoneNode (uiBehaviour_t* behaviour)
{
	behaviour->name = "zone";
	behaviour->manager = UINodePtr(new uiZoneNode());
	behaviour->extraDataSize = sizeof(EXTRADATA_TYPE);
	behaviour->lua_SWIG_typeinfo = UI_SWIG_TypeQuery("uiZoneNode_t *");

	/* If true, the <code>onclick</code> call back is called more than one time if the user does not release the button. */
	UI_RegisterExtradataNodeProperty(behaviour, "repeat", V_BOOL, zoneExtraData_t, repeat);
	/* Delay is used between 2 calls of <code>onclick</code>. */
	UI_RegisterExtradataNodeProperty(behaviour, "clickdelay", V_INT, zoneExtraData_t, clickDelay);
}
