/**
 * @file m_node_panel.c
 * @todo clean up all properties
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

#include "../m_render.h"
#include "m_node_rows.h"
#include "m_node_abstractnode.h"

#define EXTRADATA_TYPE rowsExtraData_t
#define EXTRADATA(node) MN_EXTRADATA(node, EXTRADATA_TYPE)
#define EXTRADATACONST(node) MN_EXTRADATACONST(node, EXTRADATA_TYPE)

/**
 * @brief Handles Button draw
 */
static void MN_RowsNodeDraw (menuNode_t *node)
{
	int current = 0;
	int i = EXTRADATA(node).current;
	vec2_t pos;
	MN_GetNodeAbsPos(node, pos);

	while (current < node->size[1]) {
		const float *color;
		const int height = min(EXTRADATA(node).lineHeight, node->size[1] - current);

		if (i % 2)
			color = node->color;
		else
			color = node->selectedColor;
		MN_DrawFill(pos[0], pos[1] + current, node->size[0], height, color);
		current += height;
		i++;
	}
}

static void MN_RowsNodeLoaded (menuNode_t *node)
{
	/* prevent infinite loop into the draw */
	if (EXTRADATA(node).lineHeight == 0) {
		EXTRADATA(node).lineHeight = 10;
	}
}

static const value_t properties[] = {
	/* Background color for odd elements */
	{"color1", V_COLOR, offsetof(menuNode_t, color), MEMBER_SIZEOF(menuNode_t, color)},
	/* Background color for even elements */
	{"color2", V_COLOR, offsetof(menuNode_t, selectedColor), MEMBER_SIZEOF(menuNode_t, selectedColor)},
	/* Element height */
	{"lineheight", V_INT, MN_EXTRADATA_OFFSETOF(rowsExtraData_t, lineHeight), MEMBER_SIZEOF(rowsExtraData_t, lineHeight)},
	/* Element number on the top of the list. It is used to scroll the node content. */
	{"current", V_INT, MN_EXTRADATA_OFFSETOF(rowsExtraData_t, current), MEMBER_SIZEOF(rowsExtraData_t, current)},
	{NULL, V_NULL, 0, 0}
};

void MN_RegisterRowsNode (nodeBehaviour_t *behaviour)
{
	behaviour->name = "rows";
	behaviour->draw = MN_RowsNodeDraw;
	behaviour->loaded = MN_RowsNodeLoaded;
	behaviour->properties = properties;
	behaviour->extraDataSize = sizeof(EXTRADATA_TYPE);
}
