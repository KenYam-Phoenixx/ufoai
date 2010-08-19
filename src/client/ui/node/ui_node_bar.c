/**
 * @file ui_node_bar.c
 * @brief The bar node display a graphical horizontal slider.
 * We can use it to allow the user to select a value in a range. Or
 * we can use it do only display a value (in this case, you must disable it).
 */

/*
Copyright (C) 2002-2010 UFO: Alien Invasion.

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
#include "../ui_main.h"
#include "../ui_input.h"
#include "../ui_render.h"
#include "../ui_actions.h"
#include "ui_node_bar.h"
#include "ui_node_abstractvalue.h"
#include "ui_node_abstractnode.h"

#include "../../input/cl_keys.h"

#define EXTRADATA_TYPE barExtraData_t
#define EXTRADATA(node) UI_EXTRADATA(node, EXTRADATA_TYPE)

static void UI_BarNodeDraw (uiNode_t *node)
{
	vec4_t color;
	float fac;
	vec2_t nodepos;
	const float min = UI_GetReferenceFloat(node, EXTRADATA(node).super.min);
	const float max = UI_GetReferenceFloat(node, EXTRADATA(node).super.max);
	const float value = UI_GetReferenceFloat(node, EXTRADATA(node).super.value);

	UI_GetNodeAbsPos(node, nodepos);

	if (node->state && !EXTRADATA(node).readOnly) {
		Vector4Copy(node->color, color);
	} else {
		VectorScale(node->color, 0.8, color);
		color[3] = node->color[3];
	}

	/* shoud it return an error? */
	if (max > min)
		fac = (value - min) / (max - min);
	else
		fac = 1;
	if (fac <= 0 || fac > 1)
		return;

	switch (EXTRADATA(node).orientation) {
	case ALIGN_UC:
		UI_DrawFill(nodepos[0], nodepos[1] + node->size[1] - fac * node->size[1], node->size[0], fac * node->size[1], color);
		break;
	case ALIGN_LC:
		UI_DrawFill(nodepos[0], nodepos[1], node->size[0], fac * node->size[1], color);
		break;
	case ALIGN_CR:
		UI_DrawFill(nodepos[0], nodepos[1], fac * node->size[0], node->size[1], color);
		break;
	case ALIGN_CL:
		UI_DrawFill(nodepos[0] + node->size[0] - fac * node->size[0], nodepos[1], fac * node->size[0], node->size[1], color);
		break;
	default:
		Com_Printf("UI_BarNodeDraw: Orientation %d not supported\n", EXTRADATA(node).orientation);
	}
}

/**
 * @brief Called when the node is captured by the mouse
 */
static void UI_BarNodeCapturedMouseMove (uiNode_t *node, int x, int y)
{
	char var[MAX_VAR];
	vec2_t pos;

	UI_NodeAbsoluteToRelativePos(node, &x, &y);

	if (x < 0)
		x = 0;
	else if (x > node->size[0])
		x = node->size[0];
	if (y < 0)
		y = 0;
	else if (y > node->size[1])
		y = node->size[1];

	UI_GetNodeAbsPos(node, pos);
	Q_strncpyz(var, EXTRADATA(node).super.value, sizeof(var));
	/* no cvar? */
	if (!strncmp(var, "*cvar:", 6)) {
		/* normalize it */
		float frac;
		const float min = UI_GetReferenceFloat(node, EXTRADATA(node).super.min);
		const float max = UI_GetReferenceFloat(node, EXTRADATA(node).super.max);

		switch (EXTRADATA(node).orientation) {
		case ALIGN_UC:
			frac = (node->size[1] - (float) y) / node->size[1];
			break;
		case ALIGN_LC:
			frac = (float) y / node->size[1];
			break;
		case ALIGN_CR:
			frac = (float) x / node->size[0];
			break;
		case ALIGN_CL:
			frac = (node->size[0] - (float) x) / node->size[0];
			break;
		default:
			frac = 0;
			Com_Printf("UI_BarNodeCapturedMouseMove: Orientation %d not supported\n", EXTRADATA(node).orientation);
		}
		UI_SetCvar(&var[6], NULL, min + frac * (max - min));
	}

	/* callback */
	if (node->onChange)
		UI_ExecuteEventActions(node, node->onChange);
}

static void UI_BarNodeMouseDown (uiNode_t *node, int x, int y, int button)
{
	if (node->disabled || EXTRADATA(node).readOnly)
		return;

	if (button == K_MOUSE1) {
		UI_SetMouseCapture(node);
		UI_BarNodeCapturedMouseMove(node, x, y);
	}
}

static void UI_BarNodeMouseUp (uiNode_t *node, int x, int y, int button)
{
	if (button == K_MOUSE1)
		UI_MouseRelease();
}

/**
 * @brief Called before loading. Used to set default attribute values
 */
static void UI_BarNodeLoading (uiNode_t *node)
{
	Vector4Set(node->color, 1, 1, 1, 1);
	EXTRADATA(node).orientation = ALIGN_CR;
}

/**
 * @brief Valid properties for a bar node
 */
static const value_t properties[] = {
	/**
	 * Orientation of the bar. Default value "cr". Other available values are "uc", "lc", "cr", "cl"
	 */
	{"direction", V_UI_ALIGN, UI_EXTRADATA_OFFSETOF(barExtraData_t, orientation), MEMBER_SIZEOF(barExtraData_t, orientation)},
	/**
	 *  if true, the user can't edit the content
	 */
	{"readonly", V_BOOL, UI_EXTRADATA_OFFSETOF(barExtraData_t, readOnly),  MEMBER_SIZEOF(barExtraData_t, readOnly)},

	{NULL, V_NULL, 0, 0}
};

void UI_RegisterBarNode (uiBehaviour_t *behaviour)
{
	behaviour->name = "bar";
	behaviour->extends = "abstractvalue";
	behaviour->properties = properties;
	behaviour->draw = UI_BarNodeDraw;
	behaviour->loading = UI_BarNodeLoading;
	behaviour->mouseDown = UI_BarNodeMouseDown;
	behaviour->mouseUp = UI_BarNodeMouseUp;
	behaviour->capturedMouseMove = UI_BarNodeCapturedMouseMove;
	behaviour->extraDataSize = sizeof(EXTRADATA_TYPE);
}
