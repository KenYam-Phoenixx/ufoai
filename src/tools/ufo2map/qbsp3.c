/**
 * @file qbsp3.c
 */

/*
Copyright (C) 1997-2001 Id Software, Inc.

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


#include "qbsp.h"

int entity_num;


/**
 * @sa ProcessModels
 */
static void ProcessWorldModel (void)
{
	entity_t *e;

	e = &entities[entity_num];

	brush_start = e->firstbrush;
	brush_end = brush_start + e->numbrushes;

	ClearBounds(worldMins, worldMaxs);
	nummodels = NUMMODELS;

	/* process levels */
	U2M_ProgressBar(ProcessLevel, NUMMODELS, qtrue, "LEVEL");

	/* calculate routing */
	DoRouting();
}


/**
 * @sa ProcessModels
 */
static void ProcessSubModel (int entityNum)
{
	entity_t *e;
	int start, end;
	tree_t *tree;
	bspbrush_t *list;
	vec3_t mins, maxs;

	BeginModel(entityNum);

	e = &entities[entityNum];

	start = e->firstbrush;
	end = start + e->numbrushes;

#if 0
	if (!strcmp("func_detail", ValueForKey(e, "classname"))) {
	}
#endif

	mins[0] = mins[1] = mins[2] = -4096;
	maxs[0] = maxs[1] = maxs[2] = 4096;
	list = MakeBspBrushList(start, end, -1, mins, maxs);
	if (!config.nocsg)
		list = ChopBrushes(list);
	tree = BrushBSP(list, mins, maxs);
	if (tree->headnode->planenum == PLANENUM_LEAF)
		Sys_Error("No head node bmodel of %s\n", ValueForKey(e, "classname"));
	MakeTreePortals(tree);
	MarkVisibleSides(tree, start, end);
	MakeFaces(tree->headnode);
	FixTjuncs(tree->headnode);
	WriteBSP(tree->headnode);
	FreeTree(tree);

	EndModel();
}


/**
 * @sa ProcessWorldModel
 * @sa ProcessSubModel
 */
void ProcessModels (const char *filename)
{
	BeginBSPFile();

	for (entity_num = 0; entity_num < num_entities; entity_num++) {
		if (!entities[entity_num].numbrushes)
			continue;

		Sys_FPrintf(SYS_VRB, "############### model %i ###############\n", nummodels);
		if (entity_num == 0)
			ProcessWorldModel();
		else
			ProcessSubModel(entity_num);

		if (!config.verboseentities)
			config.verbose = qfalse;	/* don't bother printing submodels */
	}

	EndBSPFile(filename);
}
