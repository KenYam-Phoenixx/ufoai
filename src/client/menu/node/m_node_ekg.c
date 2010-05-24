/**
 * @file m_node_ekg.c
 * @brief Health and morale ekg images for actors
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

#include "../m_nodes.h"
#include "../m_parse.h"
#include "../m_render.h"
#include "m_node_ekg.h"
#include "m_node_abstractnode.h"

#include "../../client.h"

#define EXTRADATA_TYPE ekgExtraData_t
#define EXTRADATA(node) MN_EXTRADATA(node, EXTRADATA_TYPE)

static void MN_EKGNodeDraw (menuNode_t *node)
{
	vec2_t size;
	vec2_t nodepos;
	const image_t *image;

	const char* imageName = MN_GetReferenceString(node, node->image);
	if (!imageName || imageName[0] == '\0')
		return;

	MN_GetNodeAbsPos(node, nodepos);

	image = MN_LoadImage(imageName);
	if (image) {
		const int ekgHeight = node->size[1];
		const int ekgWidth = image->width;
		/* we have different ekg parts in each ekg image... */
		const int ekgImageParts = image->height / node->size[1];
		const int ekgMaxIndex = ekgImageParts - 1;
		/* we change the index of the image part in 20s steps */
		/** @todo this magic number should be replaced with a sane calculation of the value */
		const int ekgDivide = 20;
		/* If we are in the range of (ekgMaxValue + ekgDivide, ekgMaxValue) we are using the first image */
		const int ekgMaxValue = ekgDivide * ekgMaxIndex;
		int ekgValue;
		float current;

		/** @todo these cvars should come from the script */
		/* ekg_morale and ekg_hp are the node names */
		if (node->name[0] == 'm')
			current = Cvar_GetValue("mn_morale") / EXTRADATA(node).scaleCvarValue;
		else
			current = Cvar_GetValue("mn_hp") / EXTRADATA(node).scaleCvarValue;

		ekgValue = min(current, ekgMaxValue);

		EXTRADATA(node).super.texl[1] = (ekgMaxIndex - (int)(ekgValue / ekgDivide)) * ekgHeight;
		EXTRADATA(node).super.texh[1] = EXTRADATA(node).super.texl[1] + ekgHeight;
		EXTRADATA(node).super.texl[0] = -(int) (EXTRADATA(node).scrollSpeed * cls.realtime) % ekgWidth;
		EXTRADATA(node).super.texh[0] = EXTRADATA(node).super.texl[0] + node->size[0];
		/** @todo code is duplicated in the image node code */
		if (node->size[0] && !node->size[1]) {
			const float scale = image->width / node->size[0];
			Vector2Set(size, node->size[0], image->height / scale);
		} else if (node->size[1] && !node->size[0]) {
			const float scale = image->height / node->size[1];
			Vector2Set(size, image->width / scale, node->size[1]);
		} else {
			if (EXTRADATA(node).super.preventRatio) {
				/* maximize the image into the bounding box */
				const float ratio = (float) image->width / (float) image->height;
				if (node->size[1] * ratio > node->size[0]) {
					Vector2Set(size, node->size[0], node->size[0] / ratio);
				} else {
					Vector2Set(size, node->size[1] * ratio, node->size[1]);
				}
			} else {
				Vector2Copy(node->size, size);
			}
		}
		MN_DrawNormImage(nodepos[0], nodepos[1], size[0], size[1],
				EXTRADATA(node).super.texh[0], EXTRADATA(node).super.texh[1], EXTRADATA(node).super.texl[0], EXTRADATA(node).super.texl[1], image);
	}
}

/**
 * @brief Called at the begin of the load from script
 */
static void MN_EKGNodeLoading (menuNode_t *node)
{
	EXTRADATA(node).scaleCvarValue = 1.0f;
	EXTRADATA(node).scrollSpeed= 0.07f;
}

static const value_t properties[] = {
	/* @todo Need documentation */
	{"scrollspeed", V_FLOAT, MN_EXTRADATA_OFFSETOF(ekgExtraData_t, scrollSpeed), MEMBER_SIZEOF(ekgExtraData_t, scrollSpeed)},
	/* @todo Need documentation */
	{"scale", V_FLOAT, MN_EXTRADATA_OFFSETOF(ekgExtraData_t, scaleCvarValue), MEMBER_SIZEOF(ekgExtraData_t, scaleCvarValue)},

	{NULL, V_NULL, 0, 0}
};

void MN_RegisterEKGNode (nodeBehaviour_t* behaviour)
{
	behaviour->name = "ekg";
	behaviour->loading = MN_EKGNodeLoading;
	behaviour->extends = "pic";
	behaviour->draw = MN_EKGNodeDraw;
	behaviour->properties = properties;
	behaviour->extraDataSize = sizeof(EXTRADATA_TYPE);
}
