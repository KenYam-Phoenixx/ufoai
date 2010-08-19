/**
 * @file tracing.c
 * @brief model tracing and bounding
 * @note collision detection code
 */

/*
All original material Copyright (C) 2002-2010 UFO: Alien Invasion.

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

#include "tracing.h"
#include "common.h"
#include "mem.h"

/** @note used for statistics
 * @sa CM_LoadMap (cmodel.c) */
int c_traces, c_brush_traces;

/* static */
static cBspSurface_t nullsurface;

/**
 * @note used as a shortcut so the tile being processed does not need to be repeatedly passed between functions.
 * @sa TR_MakeTracingNode (tracing.c)
 * @sa TR_BuildTracingNode_r (tracing.c)
 * @sa TR_TestLine_r (tracing.c)
 * @sa TR_TestLineDist_r (tracing.c)
 * @sa TR_TestLineMask (tracing.c)
 * @sa TR_InitBoxHull (tracing.c)
 * @sa TR_TestLineDM (tracing.c)
 * @sa CMod_LoadSubmodels (cmodel.c)
 * @sa CMod_LoadSurfaces (cmodel.c)
 * @sa CMod_LoadNodes (cmodel.c)
 * @sa CMod_LoadBrushes (cmodel.c)
 * @sa CMod_LoadLeafs (cmodel.c)
 * @sa CMod_LoadPlanes (cmodel.c)
 * @sa CMod_LoadLeafBrushes (cmodel.c)
 * @sa CMod_LoadBrushSides (cmodel.c)
 * @sa CM_MakeTracingNodes (cmodel.c)
 * @sa CM_InitBoxHull (cmodel.c)
 * @sa CM_CompleteBoxTrace (cmodel.c)
 * @sa CMod_LoadRouting (cmodel.c)
 * @sa CM_AddMapTile (cmodel.c)
 */
TR_TILE_TYPE *curTile;

/** @note loaded map tiles with this assembly.  ufo2map has exactly 1.*/
TR_TILE_TYPE mapTiles[MAX_MAPTILES];

/** @note number of loaded map tiles (map assembly) */
int numTiles = 0;

/** @note For multi-check avoidance. */
static int checkcount;

/** @note used to hold the point on a line that an obstacle is encountered. */
static vec3_t tr_end;

static int leaf_count, leaf_maxcount;
static int *leaf_list;
static int leaf_topnode;

/** @note This is used to create the thead trees in MakeTracingNodes. */
tnode_t *tnode_p;


/*
===============================================================================
TRACING NODES
===============================================================================
*/

/**
 * @brief Converts the disk node structure into the efficient tracing structure for LineTraces
 */
static void TR_MakeTracingNode (int nodenum)
{
	tnode_t *t;				/* the tracing node to build */
	TR_PLANE_TYPE *plane;
	int i;
	TR_NODE_TYPE *node;		/* the node we are investigating */

	t = tnode_p++;

	node = curTile->nodes + nodenum;
#ifdef COMPILE_UFO
	plane = node->plane;
#else
	plane = curTile->planes + node->planenum;
#endif

	t->type = plane->type;
	VectorCopy(plane->normal, t->normal);
	t->dist = plane->dist;

	for (i = 0; i < 2; i++) {
		if (node->children[i] < 0) {	/* is it a leaf ? */
			const int index = -(node->children[i]) - 1;
			const TR_LEAF_TYPE *leaf = &curTile->leafs[index];
			const int contentFlags = leaf->contentFlags & ~(1 << 31);
			if ((contentFlags & (CONTENTS_SOLID | MASK_CLIP)) && !(contentFlags & CONTENTS_PASSABLE))
				t->children[i] = -node->children[i] | (1 << 31);	/* mark as 'blocking' */
			else
				t->children[i] = (1 << 31);				/* mark as 'empty leaf' */
		} else {										/* not a leaf */
			t->children[i] = tnode_p - curTile->tnodes;
			if (t->children[i] > curTile->numnodes) {
				Com_Printf("Exceeded allocated memory for tracing structure (%i > %i)\n",
						t->children[i], curTile->numnodes);
			}
			TR_MakeTracingNode(node->children[i]);		/* recurse further down the tree */
		}
	}
}

/**
 * @sa CMod_LoadNodes
 * @sa R_ModLoadNodes
 */
void TR_BuildTracingNode_r (int node, int level)
{
	assert(node < curTile->numnodes + 6); /* +6 => bbox */

	/**
	 *  We are checking for a leaf in the tracing node.  For ufo2map, planenum == PLANENUMLEAF.
	 *  For the game, plane will be NULL.
	 */
#ifdef COMPILE_UFO
	if (!curTile->nodes[node].plane) {
#else
	if (curTile->nodes[node].planenum == PLANENUM_LEAF) {
#endif
		TR_NODE_TYPE *n;
		tnode_t *t;
		vec3_t c0maxs, c1mins;
		int i;

		n = &curTile->nodes[node];

		/* alloc new node */
		t = tnode_p++;

		/* no leafs here */
		if (n->children[0] < 0 || n->children[1] < 0)
#ifdef COMPILE_UFO
			Com_Error(ERR_DROP, "Unexpected leaf");
#else
			Sys_Error("Unexpected leaf");
#endif

		VectorCopy(curTile->nodes[n->children[0]].maxs, c0maxs);
		VectorCopy(curTile->nodes[n->children[1]].mins, c1mins);

#if 0
		Com_Printf("(%i %i : %i %i) (%i %i : %i %i)\n",
			(int)curTile->nodes[n->children[0]].mins[0], (int)curTile->nodes[n->children[0]].mins[1],
			(int)curTile->nodes[n->children[0]].maxs[0], (int)curTile->nodes[n->children[0]].maxs[1],
			(int)curTile->nodes[n->children[1]].mins[0], (int)curTile->nodes[n->children[1]].mins[1],
			(int)curTile->nodes[n->children[1]].maxs[0], (int)curTile->nodes[n->children[1]].maxs[1]);
#endif

		for (i = 0; i < 2; i++)
			if (c0maxs[i] <= c1mins[i]) {
				/* create a separation plane */
				t->type = i;
				VectorSet(t->normal, i, (i ^ 1), 0);
				t->dist = (c0maxs[i] + c1mins[i]) / 2;

				t->children[1] = tnode_p - curTile->tnodes;
				TR_BuildTracingNode_r(n->children[0], level);
				t->children[0] = tnode_p - curTile->tnodes;
				TR_BuildTracingNode_r(n->children[1], level);
				return;
			}

		/* can't construct such a separation plane */
		t->type = PLANE_NONE;

		for (i = 0; i < 2; i++) {
			t->children[i] = tnode_p - curTile->tnodes;
			TR_BuildTracingNode_r(n->children[i], level);
		}
	} else {
		/* Make a lookup table for TR_CompleteBoxTrace */
		curTile->cheads[curTile->numcheads].cnode = node;
		curTile->cheads[curTile->numcheads].level = level;
		curTile->numcheads++;
		assert(curTile->numcheads <= MAX_MAP_NODES);
		/* Make the tracing node. */
		TR_MakeTracingNode(node);
	}
}


/*
===============================================================================
LINE TRACING - TEST FOR BRUSH PRESENCE
===============================================================================
*/


/**
 * @param[in] tile The map tile containing the structures to be traced.
 * @param[in] node Node index
 * @param[in] start The position to start the trace.
 * @param[in] stop The position where the trace ends.
 * @return zero if the line is not blocked, else a positive value
 * @sa TR_TestLineDist_r
 * @sa CM_TestLine
 */
static int TR_TestLine_r (TR_TILE_TYPE *tile, int node, const vec3_t start, const vec3_t stop)
{
	tnode_t *tnode;
	float front, back;
	int r;

	/* negative numbers indicate leaf nodes. Empty leaf nodes are marked as (1 << 31).
	 * Turning off that bit makes us return 0 or the positive node number to indicate blocking. */
	if (node & (1 << 31))
		return node & ~(1 << 31);

	tnode = &tile->tnodes[node];
	assert(tnode);
	switch (tnode->type) {
	case PLANE_X:
	case PLANE_Y:
	case PLANE_Z:
		front = start[tnode->type] - tnode->dist;
		back = stop[tnode->type] - tnode->dist;
		break;
	case PLANE_NONE:
		r = TR_TestLine_r(tile, tnode->children[0], start, stop);
		if (r)
			return r;
		return TR_TestLine_r(tile, tnode->children[1], start, stop);
	default:
		front = DotProduct(start, tnode->normal) - tnode->dist;
		back = DotProduct(stop, tnode->normal) - tnode->dist;
		break;
	}

	if (front >= -ON_EPSILON && back >= -ON_EPSILON)
		return TR_TestLine_r(tile, tnode->children[0], start, stop);
	else if (front < ON_EPSILON && back < ON_EPSILON)
		return TR_TestLine_r(tile, tnode->children[1], start, stop);
	else {
		const int side = front < 0;
		const float frac = front / (front - back);
		vec3_t mid;

		VectorInterpolation(start, stop, frac, mid);

		r = TR_TestLine_r(tile, tnode->children[side], start, mid);
		if (r)
			return r;
		return TR_TestLine_r(tile, tnode->children[!side], mid, stop);
	}
}


/**
 * @brief Tests to see if a line intersects any brushes in a tile.
 * @param[in] tile The map tile containing the structures to be traced.
 * @param[in] start The position to start the trace.
 * @param[in] stop The position where the trace ends.
 * @param[in] levelmask
 * @return true if the line is blocked
 * @note This function uses levels and levelmasks.  The levels are as following:
 * 0-255: brushes are assigned to a level based on their assigned viewing levels.  A brush with
 *    no levels assigned will be stuck in 0, a brush viewable from all 8 levels will be in 255, and
 *    so on.  Each brush will only appear in one level.
 * 256: weaponclip-level
 * 257: actorclip-level
 *
 * The levelmask is used to determine which levels AND which, if either, clip to trace through.
 * The mask bits are as follows:
 * 0x0FF: Level bits.  If any bits are set then a brush's level ANDed with the levelmask, then
 *     that level is traced.  It could possibly be used to speed up traces.
 * 0x100: Actorclip bit.  If this bit is set, the actorclip level will be traced.
 * 0x200: Weaponclip bit.  If this bit is set, the weaponclip level will be traced.
 */
static qboolean TR_TileTestLine (TR_TILE_TYPE *tile, const vec3_t start, const vec3_t stop, const int levelmask)
{
	const int corelevels = (levelmask & TL_FLAG_REGULAR_LEVELS);
	int i;

	/* loop over all theads */
	for (i = 0; i < tile->numtheads; i++) {
		const int level = tile->theadlevel[i];
		if (level && corelevels && !(level & corelevels))
			continue;
		if (level == LEVEL_LIGHTCLIP)	/* lightclips are only used in ufo2map, and it does not use this function */
			continue;
		if (level == LEVEL_ACTORCLIP && !(levelmask & TL_FLAG_ACTORCLIP))
			continue;
		if (level == LEVEL_WEAPONCLIP && !(levelmask & TL_FLAG_WEAPONCLIP))
			continue;
		if (TR_TestLine_r(tile, tile->thead[i], start, stop))
			return qtrue;
	}
	return qfalse;
}

#ifdef COMPILE_MAP
/**
 * @brief Checks traces against a single-tile map, optimized for ufo2map. This trace is only for visible levels.
 * @param[in] start The position to start the trace.
 * @param[in] stop The position where the trace ends.
 * @sa TR_TestLine
 * @sa GatherSampleLight
 * @return qfalse if not blocked
 */
qboolean TR_TestLineSingleTile (const vec3_t start, const vec3_t stop, int *headhint)
{
	int i;
	static int shared_lastthead = 0;
	int lastthead = *headhint;

	if (!lastthead) {
		lastthead = shared_lastthead;
		*headhint = lastthead;
	}

	assert(numTiles == 1);

	/* ufo2map does many traces to the same endpoint.
	 * Often an occluding node will be found in the same thead
	 * as the last trace, so test that one first. */
	if (mapTiles[0].theadlevel[lastthead] <= LEVEL_LASTLIGHTBLOCKING
		&& TR_TestLine_r(&mapTiles[0], mapTiles[0].thead[lastthead], start, stop))
		return qtrue;

	for (i = 0; i < mapTiles[0].numtheads; i++) {
		const int level = mapTiles[0].theadlevel[i];
		if (i == lastthead)
			continue;
		if (level > LEVEL_LASTLIGHTBLOCKING)
			continue;
		if (TR_TestLine_r(&mapTiles[0], mapTiles[0].thead[i], start, stop)) {
			shared_lastthead = *headhint = i;
			return qtrue;
		}
	}
	return qfalse;
}
#endif


/**
 * @brief Checks traces against the world
 * @param[in] start The position to start the trace.
 * @param[in] stop The position where the trace ends.
 * @param[in] levelmask Indicates which special levels, if any, to include in the trace.
 * @note Special levels are LEVEL_ACTORCLIP and LEVEL_WEAPONCLIP.
 * @sa TR_TestLine_r
 * @return qfalse if not blocked
 */
qboolean TR_TestLine (const vec3_t start, const vec3_t stop, const int levelmask)
{
	int tile;

	for (tile = 0; tile < numTiles; tile++) {
		if (TR_TileTestLine(&mapTiles[tile], start, stop, levelmask))
			return qtrue;
	}
	return qfalse;
}


/*
===============================================================================
LINE TRACING - TEST FOR BRUSH LOCATION
===============================================================================
*/


/**
 * @param[in] tile The map tile containing the structures to be traced.
 * @param[in] node Node index
 * @param[in] start The position to start the trace.
 * @param[in] stop The position where the trace ends.
 * @sa TR_TestLine_r
 * @sa TR_TestLineDM
 */
static int TR_TestLineDist_r (TR_TILE_TYPE *tile, int node, const vec3_t start, const vec3_t stop)
{
	tnode_t *tnode;
	float front, back;
	vec3_t mid;
	float frac;
	int side;
	int r;

	if (node & (1 << 31)) {
		r = node & ~(1 << 31);
		if (r)
			VectorCopy(start, tr_end);
		return r;				/* leaf node */
	}

	tnode = &tile->tnodes[node];
	assert(tnode);
	switch (tnode->type) {
	case PLANE_X:
	case PLANE_Y:
	case PLANE_Z:
		front = start[tnode->type] - tnode->dist;
		back = stop[tnode->type] - tnode->dist;
		break;
	case PLANE_NONE:
		r = TR_TestLineDist_r(tile, tnode->children[0], start, stop);
		if (r)
			VectorCopy(tr_end, mid);
		side = TR_TestLineDist_r(tile, tnode->children[1], start, stop);
		if (side && r) {
			if (VectorNearer(mid, tr_end, start)) {
				VectorCopy(mid, tr_end);
				return r;
			} else {
				return side;
			}
		}

		if (r) {
			VectorCopy(mid, tr_end);
			return r;
		}

		return side;
	default:
		front = (start[0] * tnode->normal[0] + start[1] * tnode->normal[1] + start[2] * tnode->normal[2]) - tnode->dist;
		back = (stop[0] * tnode->normal[0] + stop[1] * tnode->normal[1] + stop[2] * tnode->normal[2]) - tnode->dist;
		break;
	}

	if (front >= -ON_EPSILON && back >= -ON_EPSILON)
		return TR_TestLineDist_r(tile, tnode->children[0], start, stop);

	if (front < ON_EPSILON && back < ON_EPSILON)
		return TR_TestLineDist_r(tile, tnode->children[1], start, stop);

	side = front < 0;

	frac = front / (front - back);

	VectorInterpolation(start, stop, frac, mid);

	r = TR_TestLineDist_r(tile, tnode->children[side], start, mid);
	if (r)
		return r;
	return TR_TestLineDist_r(tile, tnode->children[!side], mid, stop);
}

/**
 * @brief Checks traces against the world, gives hit position back
 * @param[in] tile The map tile containing the structures to be traced.
 * @param[in] start The position to start the trace.
 * @param[in] stop The position where the trace ends.
 * @param[out] end The position where the trace hits a object or the stop position if nothing is in the line.
 * @param[in] levelmask The bitmask of the levels (1<<[0-7]) to trace for
 * @sa TR_TestLineDM
 * @sa CL_ActorMouseTrace
 * @return qfalse if no connection between start and stop - 1 otherwise
 */
static qboolean TR_TileTestLineDM (TR_TILE_TYPE *tile, const vec3_t start, const vec3_t stop, vec3_t end, const int levelmask)
{
#ifdef COMPILE_MAP
	const int corelevels = (levelmask & TL_FLAG_REGULAR_LEVELS);
#endif
	int i;

	VectorCopy(stop, end);

	for (i = 0; i < tile->numtheads; i++) {
#ifdef COMPILE_MAP
		const int level = tile->theadlevel[i];
		if (level && corelevels && !(level & levelmask))
			continue;
/*		if (level == LEVEL_LIGHTCLIP)
			continue;*/
		if (level == LEVEL_ACTORCLIP && !(levelmask & TL_FLAG_ACTORCLIP))
			continue;
		if (level == LEVEL_WEAPONCLIP && !(levelmask & TL_FLAG_WEAPONCLIP))
			continue;
#endif
		if (TR_TestLineDist_r(tile, tile->thead[i], start, stop))
			if (VectorNearer(tr_end, end, start))
				VectorCopy(tr_end, end);
	}

	if (VectorCompareEps(end, stop, EQUAL_EPSILON))
		return qfalse;
	else
		return qtrue;
}


/**
 * @brief Checks traces against the world, gives hit position back
 * @param[in] start The position to start the trace.
 * @param[in] stop The position where the trace ends.
 * @param[out] end The position where the trace hits a object or the stop position if nothing is in the line.
 * @param[in] levelmask Indicates which special levels, if any, to include in the trace.
 * @sa TR_TestLineDM
 * @sa CL_ActorMouseTrace
 * @return qfalse if no connection between start and stop - 1 otherwise
 */
qboolean TR_TestLineDM (const vec3_t start, const vec3_t stop, vec3_t end, const int levelmask)
{
	int tile;
	vec3_t t_end;

	VectorCopy(stop, end);
	VectorCopy(stop, t_end);

	for (tile = 0; tile < numTiles; tile++) {
		if (TR_TileTestLineDM(&mapTiles[tile], start, stop, t_end, levelmask)) {
			if (VectorNearer(t_end, end, start))
				VectorCopy(t_end, end);
		}
	}

	if (VectorCompareEps(end, stop, EQUAL_EPSILON))
		return qfalse;
	else
		return qtrue;
}


/*
===============================================================================
BOX TRACING
===============================================================================
*/


/**
 * @brief Returns PSIDE_FRONT, PSIDE_BACK, or PSIDE_BOTH
 */
int TR_BoxOnPlaneSide (const vec3_t mins, const vec3_t maxs, const TR_PLANE_TYPE *plane)
{
	int side, i;
	vec3_t corners[2];
	vec_t dist1, dist2;

	/* axial planes are easy */
	if (AXIAL(plane)) {
		side = 0;
		if (maxs[plane->type] > plane->dist + PLANESIDE_EPSILON)
			side |= PSIDE_FRONT;
		if (mins[plane->type] < plane->dist - PLANESIDE_EPSILON)
			side |= PSIDE_BACK;
		return side;
	}

	/* create the proper leading and trailing verts for the box */

	for (i = 0; i < 3; i++) {
		if (plane->normal[i] < 0) {
			corners[0][i] = mins[i];
			corners[1][i] = maxs[i];
		} else {
			corners[1][i] = mins[i];
			corners[0][i] = maxs[i];
		}
	}

	dist1 = DotProduct(plane->normal, corners[0]) - plane->dist;
	dist2 = DotProduct(plane->normal, corners[1]) - plane->dist;
	side = 0;
	if (dist1 >= PLANESIDE_EPSILON)
		side = PSIDE_FRONT;
	if (dist2 < PLANESIDE_EPSILON)
		side |= PSIDE_BACK;

	return side;
}



/**
 * @brief To keep everything totally uniform, bounding boxes are turned into small
 * BSP trees instead of being compared directly.
 */
int TR_HeadnodeForBox (mapTile_t *tile, const vec3_t mins, const vec3_t maxs)
{
	tile->box_planes[0].dist = maxs[0];
	tile->box_planes[1].dist = -maxs[0];
	tile->box_planes[2].dist = mins[0];
	tile->box_planes[3].dist = -mins[0];
	tile->box_planes[4].dist = maxs[1];
	tile->box_planes[5].dist = -maxs[1];
	tile->box_planes[6].dist = mins[1];
	tile->box_planes[7].dist = -mins[1];
	tile->box_planes[8].dist = maxs[2];
	tile->box_planes[9].dist = -maxs[2];
	tile->box_planes[10].dist = mins[2];
	tile->box_planes[11].dist = -mins[2];

	assert(tile->box_headnode < MAX_MAP_NODES);
	return tile->box_headnode;
}

/**
 * @brief Fills in a list of all the leafs touched
 * call with topnode set to the headnode, returns with topnode
 * set to the first node that splits the box
 */
static void TR_BoxLeafnums_r (boxtrace_t *traceData, int nodenum)
{
	TR_PLANE_TYPE *plane;
	TR_NODE_TYPE *node;
	int s;
	TR_TILE_TYPE *myTile = traceData->tile;

	while (1) {
		if (nodenum <= LEAFNODE) {
			if (leaf_count >= leaf_maxcount) {
/*				Com_Printf("CM_BoxLeafnums_r: overflow\n"); */
				return;
			}
			leaf_list[leaf_count++] = -1 - nodenum;
			return;
		}

		assert(nodenum < myTile->numnodes + 6); /* +6 => bbox */
		node = &myTile->nodes[nodenum];
#ifdef COMPILE_UFO
		plane = node->plane;
#else
		plane = myTile->planes + node->planenum;
#endif

		s = TR_BoxOnPlaneSide(traceData->absmins, traceData->absmaxs, plane);
		if (s == PSIDE_FRONT)
			nodenum = node->children[0];
		else if (s == PSIDE_BACK)
			nodenum = node->children[1];
		else {					/* go down both */
			if (leaf_topnode == LEAFNODE)
				leaf_topnode = nodenum;
			TR_BoxLeafnums_r(traceData, node->children[0]);
			nodenum = node->children[1];
		}
	}
}

/**
 * @param[in] traceData both parameters and results of the trace
 * @param[in] headnode if < 0 we are in a leaf node
 */
static int TR_BoxLeafnums_headnode (boxtrace_t *traceData, int *list, int listsize, int headnode, int *topnode)
{
	leaf_list = list;
	leaf_count = 0;
	leaf_maxcount = listsize;

	leaf_topnode = LEAFNODE;

	assert(headnode < traceData->tile->numnodes + 6); /* +6 => bbox */
	TR_BoxLeafnums_r(traceData, headnode);

	if (topnode)
		*topnode = leaf_topnode;

	return leaf_count;
}


/**
 * @param[in,out] traceData both parameters and results of the trace
 * @param[in] brush the brush that is being examined
 * @param[in] leaf the leafnode the brush that is being examined belongs to
 * @brief This function checks to see if any sides of a brush intersect the line from p1 to p2 or are located within
 *  the perpendicular bounding box from mins to maxs originating from the line. It also check to see if the line
 *  originates from inside the brush, terminates inside the brush, or is completely contained within the brush.
 */
static void TR_ClipBoxToBrush (boxtrace_t *traceData, cBspBrush_t *brush, TR_LEAF_TYPE *leaf)
{
	int i, j;
	TR_PLANE_TYPE *clipplane;
	int clipplanenum;
	float dist;
	float enterfrac, leavefrac;
	vec3_t ofs;
	float d1, d2;
	qboolean getout, startout;
	TR_BRUSHSIDE_TYPE *leadside;
	TR_TILE_TYPE *myTile = traceData->tile;

	enterfrac = -1.0;
	leavefrac = 1.0;
	clipplane = NULL;

	if (!brush || !brush->numsides)
		return;

	c_brush_traces++;

	getout = qfalse;
	startout = qfalse;
	leadside = NULL;
	clipplanenum = 0;

	for (i = 0; i < brush->numsides; i++) {
		TR_BRUSHSIDE_TYPE *side = &myTile->brushsides[brush->firstbrushside + i];
#ifdef COMPILE_UFO
		TR_PLANE_TYPE *plane = side->plane;
#else
		TR_PLANE_TYPE *plane = myTile->planes + side->planenum;
#endif

		/** @todo special case for axial */
		if (!traceData->ispoint) {	/* general box case */
			/* push the plane out appropriately for mins/maxs */
			for (j = 0; j < 3; j++) {
				if (plane->normal[j] < 0)
					ofs[j] = traceData->maxs[j];
				else
					ofs[j] = traceData->mins[j];
			}
			dist = DotProduct(ofs, plane->normal);
			dist = plane->dist - dist;
		} else {				/* special point case */
			dist = plane->dist;
		}

		d1 = DotProduct(traceData->start, plane->normal) - dist;
		d2 = DotProduct(traceData->end, plane->normal) - dist;

		if (d2 > 0)
			getout = qtrue;		/* endpoint is not in solid */
		if (d1 > 0)
			startout = qtrue;	/* startpoint is not in solid */

		/* if completely in front of face, no intersection */
		if (d1 > 0 && d2 >= d1)
			return;

		if (d1 <= 0 && d2 <= 0)
			continue;

		/* crosses face */
		if (d1 > d2) {			/* enter */
			const float f = (d1 - DIST_EPSILON) / (d1 - d2);
			if (f > enterfrac) {
				enterfrac = f;
				clipplane = plane;
#ifdef COMPILE_MAP
				clipplanenum = side->planenum;
#endif
				leadside = side;
			}
		} else {				/* leave */
			const float f = (d1 + DIST_EPSILON) / (d1 - d2);
			if (f < leavefrac)
				leavefrac = f;
		}
	}

	if (!startout) {			/* original point was inside brush */
		traceData->trace.startsolid = qtrue;
		if (!getout)
			traceData->trace.allsolid = qtrue;
		traceData->trace.leafnum = leaf - myTile->leafs;
		return;
	}
	if (enterfrac < leavefrac) {
		if (enterfrac > -1 && enterfrac < traceData->trace.fraction) {
			if (enterfrac < 0)
				enterfrac = 0;
			traceData->trace.fraction = enterfrac;
			traceData->trace.plane = *clipplane;
			traceData->trace.planenum = clipplanenum;
#ifdef COMPILE_UFO
			traceData->trace.surface = leadside->surface;
#endif
			traceData->trace.contentFlags = brush->contentFlags;
			traceData->trace.leafnum = leaf - myTile->leafs;
		}
	}
}

/**
 * @sa CM_TraceToLeaf
 */
static void TR_TestBoxInBrush (boxtrace_t *traceData, cBspBrush_t * brush)
{
	int i, j;
	TR_PLANE_TYPE *plane;
	float dist;
	vec3_t ofs;
	float d1;
	TR_TILE_TYPE *myTile = traceData->tile;

	if (!brush || !brush->numsides)
		return;

	for (i = 0; i < brush->numsides; i++) {
		TR_BRUSHSIDE_TYPE *side = &myTile->brushsides[brush->firstbrushside + i];
#ifdef COMPILE_UFO
		plane = side->plane;
#else
		plane = myTile->planes + side->planenum;
#endif

		/** @todo special case for axial */
		/* general box case */
		/* push the plane out appropriately for mins/maxs */
		for (j = 0; j < 3; j++) {
			if (plane->normal[j] < 0)
				ofs[j] = traceData->maxs[j];
			else
				ofs[j] = traceData->mins[j];
		}
		dist = DotProduct(ofs, plane->normal);
		dist = plane->dist - dist;

		d1 = DotProduct(traceData->start, plane->normal) - dist;

		/* if completely in front of face, no intersection */
		if (d1 > 0)
			return;
	}

	/* inside this brush */
	traceData->trace.startsolid = traceData->trace.allsolid = qtrue;
	traceData->trace.fraction = 0;
	traceData->trace.contentFlags = brush->contentFlags;
}


/**
 * @param[in] traceData both parameters and results of the trace
 * @param[in] leafnum the leaf index that we are looking in for a hit against
 * @sa CM_ClipBoxToBrush
 * @sa CM_TestBoxInBrush
 * @sa CM_RecursiveHullCheck
 * @brief This function checks if the specified leaf matches any mask specified in traceData.contents. and does
 *  not contain any mask specified in traceData.rejects  If so, each brush in the leaf is examined to see if it
 *  is intersected by the line drawn in TR_RecursiveHullCheck or is within the bounding box set in trace_mins and
 *  trace_maxs with the origin on the line.
 */
static void TR_TraceToLeaf (boxtrace_t *traceData, int leafnum)
{
	int k;
	TR_LEAF_TYPE *leaf;
	TR_TILE_TYPE *myTile = traceData->tile;

	assert(leafnum > LEAFNODE);
	assert(leafnum <= myTile->numleafs);

	leaf = &myTile->leafs[leafnum];

	if (traceData->contents != MASK_ALL && (!(leaf->contentFlags & traceData->contents) || (leaf->contentFlags & traceData->rejects)))
		return;

	/* trace line against all brushes in the leaf */
	for (k = 0; k < leaf->numleafbrushes; k++) {
		const int brushnum = myTile->leafbrushes[leaf->firstleafbrush + k];
		cBspBrush_t *b = &myTile->brushes[brushnum];

		if (b->checkcount == checkcount)
			continue;			/* already checked this brush in another leaf */
		b->checkcount = checkcount;

		if (traceData->contents != MASK_ALL && (!(b->contentFlags & traceData->contents) || (b->contentFlags & traceData->rejects)))
			continue;

		TR_ClipBoxToBrush(traceData, b, leaf);
		if (!traceData->trace.fraction)
			return;
	}
}


/**
 * @sa CM_TestBoxInBrush
 */
static void TR_TestInLeaf (boxtrace_t *traceData, int leafnum)
{
	int k;
	const TR_LEAF_TYPE *leaf;
	TR_TILE_TYPE *myTile = traceData->tile;

	assert(leafnum > LEAFNODE);
	assert(leafnum <= myTile->numleafs);

	leaf = &myTile->leafs[leafnum];
	/* If this leaf contains no flags we need to look for, then skip it. */
	if (!(leaf->contentFlags & traceData->contents)) /* || (leaf->contentFlags & traceData->rejects) */
		return;

	/* trace line against all brushes in the leaf */
	for (k = 0; k < leaf->numleafbrushes; k++) {
		const int brushnum = myTile->leafbrushes[leaf->firstleafbrush + k];
		cBspBrush_t *b = &myTile->brushes[brushnum];
		if (b->checkcount == checkcount)
			continue;			/* already checked this brush in another leaf */
		b->checkcount = checkcount;

		if (!(traceData->contents && b->contentFlags & traceData->contents) || (b->contentFlags & traceData->rejects))
			continue;
		TR_TestBoxInBrush(traceData, b);
		if (!traceData->trace.fraction)
			return;
	}
}


/**
 * @param[in] traceData both parameters and results of the trace
 * @param[in] num the node index that we are looking in for a hit
 * @param[in] p1f based on the original line, the fraction traveled to reach the start vector
 * @param[in] p2f based on the original line, the fraction traveled to reach the end vector
 * @param[in] p1 start vector
 * @param[in] p2 end vector
 * @sa CM_BoxTrace
 * @sa CM_TraceToLeaf
 * @brief This recursive function traces through the bsp tree looking to see if a brush can be
 *  found that intersects the line from p1 to p2, including a bounding box (plane) offset that
 *  is perpendicular to the line.  If the node of the tree is a leaf, the leaf is checked.  If
 *  not, it is determined which side(s) of the tree need to be traversed, and this function is
 *  called again.  The bounding box mentioned earlier is set in TR_BoxTrace, and propagated
 *  using trace_extents.  Trace_extents is specifically how far from the line a bsp node needs
 *  to be in order to be included or excluded in the search.
 */
static void TR_RecursiveHullCheck (boxtrace_t *traceData, int num, float p1f, float p2f, const vec3_t p1, const vec3_t p2)
{
	TR_NODE_TYPE *node;
	TR_PLANE_TYPE *plane;
	float t1, t2, offset;
	float frac, frac2;
	int side;
	float midf;
	vec3_t mid;
	TR_TILE_TYPE *myTile = traceData->tile;

	if (traceData->trace.fraction <= p1f)
		return;					/* already hit something nearer */

	/* if < 0, we are in a leaf node */
	if (num <= LEAFNODE) {
		TR_TraceToLeaf(traceData, LEAFNODE - num);
		return;
	}

	assert(num < MAX_MAP_NODES);

	/* find the point distances to the seperating plane
	 * and the offset for the size of the box */
	node = myTile->nodes + num;

#ifdef COMPILE_UFO
	plane = node->plane;
#else
	assert(node->planenum < MAX_MAP_PLANES);
	plane = myTile->planes + node->planenum;
#endif

	if (AXIAL(plane)) {
		const int type = plane->type;
		t1 = p1[type] - plane->dist;
		t2 = p2[type] - plane->dist;
		offset = traceData->extents[type];
	} else {
		t1 = DotProduct(plane->normal, p1) - plane->dist;
		t2 = DotProduct(plane->normal, p2) - plane->dist;
		if (traceData->ispoint)
			offset = 0;
		else
			offset = fabs(traceData->extents[0] * plane->normal[0])
				+ fabs(traceData->extents[1] * plane->normal[1])
				+ fabs(traceData->extents[2] * plane->normal[2]);
	}

	/* see which sides we need to consider */
	if (t1 >= offset && t2 >= offset) {
		TR_RecursiveHullCheck(traceData, node->children[0], p1f, p2f, p1, p2);
		return;
	} else if (t1 < -offset && t2 < -offset) {
		TR_RecursiveHullCheck(traceData, node->children[1], p1f, p2f, p1, p2);
		return;
	}

	/* put the crosspoint DIST_EPSILON pixels on the near side */
	/** @note t1 and t2 refer to the distance the endpoints of the line are from the bsp dividing plane for this node.
	 * Additionally, frac and frac2 are the fractions of the CURRENT PIECE of the line that was being tested, and will
	 * add to (approximately) 1.  When midf is calculated, frac and frac2 are converted to reflect the fraction of the
	 * WHOLE line being traced.  However, the interpolated vector is based on the CURRENT endpoints so uses frac and
	 * frac2 to find the actual splitting point.
	 */
	if (t1 < t2) {
		const float idist = 1.0 / (t1 - t2);
		side = 1;
		frac2 = (t1 + offset + DIST_EPSILON) * idist;
		frac = (t1 - offset + DIST_EPSILON) * idist;
	} else if (t1 > t2) {
		const float idist = 1.0 / (t1 - t2);
		side = 0;
		frac2 = (t1 - offset - DIST_EPSILON) * idist;
		frac = (t1 + offset + DIST_EPSILON) * idist;
	} else {
		side = 0;
		frac = 1;
		frac2 = 0;
	}

	/* move up to the node */
	if (frac < 0)
		frac = 0;
	else if (frac > 1)
		frac = 1;

	midf = p1f + (p2f - p1f) * frac;
	VectorInterpolation(p1, p2, frac, mid);
	TR_RecursiveHullCheck(traceData, node->children[side], p1f, midf, p1, mid);

	/* go past the node */
	if (frac2 < 0)
		frac2 = 0;
	else if (frac2 > 1)
		frac2 = 1;

	midf = p1f + (p2f - p1f) * frac2;
	VectorInterpolation(p1, p2, frac2, mid);
	TR_RecursiveHullCheck(traceData, node->children[side ^ 1], midf, p2f, mid, p2);
}

/**
 * @brief This function traces a line from start to end.  It returns a trace_t indicating what portion of the line
 * can be traveled from start to end before hitting a brush that meets the criteria in brushmask.  The point that
 * this line intersects that brush is also returned.
 * There is a special case when start and end are the same vector.  In this case, the bounding box formed by mins
 * and maxs offset from start is examined for any brushes that meet the criteria.  The first brush found inside
 * the bounding box is returned.
 * There is another special case when mins and maxs are both origin vectors (0, 0, 0).  In this case, the
 * @param[in] start trace start vector
 * @param[in] end trace end vector
 * @param[in] mins box mins
 * @param[in] maxs box maxs
 * @param[in] tile Tile to check (normally 0 - except in assembled maps)
 * @param[in] headnode if < 0 we are in a leaf node
 * @param[in] brushmask brushes the trace should stop at (see MASK_*)
 * @param[in] brushreject brushes the trace should ignore (see MASK_*)
 * @param[in] fraction The furthest distance needed to trace before we stop.
 * @sa TR_TransformedBoxTrace
 * @sa TR_CompleteBoxTrace
 * @sa TR_RecursiveHullCheck
 * @sa TR_BoxLeafnums_headnode
 */
static trace_t TR_BoxTrace (TR_TILE_TYPE *tile, const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs, const int headnode, const int brushmask, const int brushreject, const float fraction)
{
	int i;
	vec3_t offset, amins, amaxs, astart, aend;
	boxtrace_t traceData;

	checkcount++;	/* for multi-check avoidance */
	c_traces++;		/* for statistics, may be zeroed */

#ifdef COMPILE_UFO
	if (headnode >= tile->numnodes + 6)
		Com_Error(ERR_DROP, "headnode (%i) is out of bounds: %i", headnode, tile->numnodes + 6);
#else
	assert(headnode < tile->numnodes + 6); /* +6 => bbox */
#endif

	/* fill in a default trace */
	memset(&traceData.trace, 0, sizeof(traceData.trace));
	traceData.trace.fraction = min(fraction, 1.0f); /* Use 1 or fraction, whichever is lower. */
	traceData.trace.surface = &(nullsurface);

	if (!tile->numnodes)		/* map not loaded */
		return traceData.trace;

	/* Optimize the trace by moving the line to be traced across into the origin of the box trace. */
	/* Calculate the offset needed to center the trace about the line */
	VectorCenterFromMinsMaxs(mins, maxs, offset);

	/* Now remove the offset from bmin and bmax (effectively centering the trace box about the origin of the line)
	 * and add the offset to the trace line (effectively repositioning the trace box at the desired coordinates) */
	VectorSubtract(mins, offset, amins);
	VectorSubtract(maxs, offset, amaxs);
	VectorAdd(start, offset, astart);
	VectorAdd(end, offset, aend);

	traceData.contents = brushmask;
	traceData.rejects = brushreject;
	traceData.tile = tile;
	VectorCopy(astart, traceData.start);
	VectorCopy(aend, traceData.end);
	VectorCopy(amins, traceData.mins);
	VectorCopy(amaxs, traceData.maxs);

	/* check for position test special case */
	if (VectorCompare(astart, aend)) {
		int leafs[MAX_LEAFS];
		int numleafs;
		int topnode;

		VectorAdd(astart, amaxs, traceData.absmaxs);
		VectorAdd(astart, amins, traceData.absmins);
		/* Removed because if creates a buffer- is this needed?
		for (i = 0; i < 3; i++) {
			/ * expand the box by 1 * /
			traceData.absmins[i] -= 1;
			traceData.absmaxs[i] += 1;
		}
		*/

		numleafs = TR_BoxLeafnums_headnode(&traceData, leafs, MAX_LEAFS, headnode, &topnode);
		for (i = 0; i < numleafs; i++) {
			TR_TestInLeaf(&traceData, leafs[i]);
			if (traceData.trace.allsolid)
				break;
		}
		VectorCopy(start, traceData.trace.endpos);
		return traceData.trace;
	}

	/* check for point special case */
	if (VectorCompare(amins, vec3_origin) && VectorCompare(amaxs, vec3_origin)) {
		traceData.ispoint = qtrue;
		VectorClear(traceData.extents);
	} else {
		traceData.ispoint = qfalse;
		VectorCopy(amaxs, traceData.extents);
	}

	/* general sweeping through world */
	/** @todo Would Interpolating aend to traceData.fraction and passing traceData.fraction instead of 1.0 make this faster? */
	TR_RecursiveHullCheck(&traceData, headnode, 0.0, 1.0, astart, aend);

	if (traceData.trace.fraction >= 1.0) {
		VectorCopy(aend, traceData.trace.endpos);
	} else {
		VectorInterpolation(traceData.start, traceData.end, traceData.trace.fraction, traceData.trace.endpos);
	}
	/* Now un-offset the end position. */
	VectorSubtract(traceData.trace.endpos, offset, traceData.trace.endpos);
	return traceData.trace;
}

/**
 * @param[in] start trace start vector
 * @param[in] end trace end vector
 * @param[in] mins box mins
 * @param[in] maxs box maxs
 * @param[in] tile Tile to check (normally 0 - except in assembled maps)
 * @param[in] headnode if < 0 we are in a leaf node
 * @param[in] brushmask brushes the trace should stop at (see MASK_*)
 * @param[in] brushreject brushes the trace should ignore (see MASK_*)
 * @param[in] origin center for rotating objects
 * @param[in] angles current rotation status (in degrees) for rotating objects
 * @param[in] rmaShift how much the object was shifted by the RMA process (needed for doors)
 * @param[in] fraction The furthest distance needed to trace before we stop.
 * @brief Handles offseting and rotation of the end points for moving and rotating entities
 * @sa CM_BoxTrace
 */
trace_t TR_HintedTransformedBoxTrace (TR_TILE_TYPE *tile, const vec3_t start, const vec3_t end, const vec3_t mins,
		const vec3_t maxs, const int headnode, const int brushmask, const int brushreject, const vec3_t origin,
		const vec3_t angles, const vec3_t rmaShift, const float fraction)
{
	trace_t trace;
	vec3_t start_l, end_l;
	vec3_t a;
	vec3_t forward, right, up;
	vec3_t temp;
	qboolean rotated;

	/* subtract origin offset */
	VectorSubtract(start, origin, start_l);
	VectorSubtract(end, origin, end_l);

	/* rotate start and end into the models frame of reference */
	if (headnode != tile->box_headnode && VectorNotEmpty(angles)) {
		rotated = qtrue;
	} else {
		rotated = qfalse;
	}

	if (rotated) {
		AngleVectors(angles, forward, right, up);

		VectorCopy(start_l, temp);
		start_l[0] = DotProduct(temp, forward);
		start_l[1] = -DotProduct(temp, right);
		start_l[2] = DotProduct(temp, up);

		VectorCopy(end_l, temp);
		end_l[0] = DotProduct(temp, forward);
		end_l[1] = -DotProduct(temp, right);
		end_l[2] = DotProduct(temp, up);
	}

	/* When tracing through a model, we want to use the nodes, planes etc. as calculated by ufo2map.
	 * But nodes and planes have been shifted in case of an RMA. At least for doors we need to undo the shift. */
	if (VectorNotEmpty(origin)) {					/* only doors seem to have their origin set */
		VectorAdd(start_l, rmaShift, start_l);		/* undo the shift */
		VectorAdd(end_l, rmaShift, end_l);
	}

	/* sweep the box through the model */
	trace = TR_BoxTrace(tile, start_l, end_l, mins, maxs, headnode, brushmask, brushreject, fraction);
	trace.mapTile = (ptrdiff_t)(tile - mapTiles);
	assert(trace.mapTile >= 0);
	assert(trace.mapTile < numTiles);

	if (rotated && trace.fraction != 1.0) {
		/** @todo figure out how to do this with existing angles */
		VectorNegate(angles, a);
		AngleVectors(a, forward, right, up);

		VectorCopy(trace.plane.normal, temp);
		trace.plane.normal[0] = DotProduct(temp, forward);
		trace.plane.normal[1] = -DotProduct(temp, right);
		trace.plane.normal[2] = DotProduct(temp, up);
	}

	VectorInterpolation(start, end, trace.fraction, trace.endpos);

	return trace;
}

/**
 * @param[in] myTile The tile being traced
 * @param[in] start trace start vector
 * @param[in] end trace end vector
 * @param[in] mins box mins
 * @param[in] maxs box maxs
 * @param[in] levelmask Selects which submodels get scanned.
 * @param[in] brushmask brushes the trace should stop at (see MASK_*)
 * @param[in] brushreject brushes the trace should ignore (see MASK_*)
 * @param[in] fraction The furthest distance needed to trace before we stop.
 *    We assume that a brush was already hit at fraction.
 * @brief Traces all submodels in the specified tile.  Provides for a short
 *   circuit if the trace tries to move past fraction to save time.
 */
static trace_t TR_TileBoxTrace (TR_TILE_TYPE *myTile, const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs, const int levelmask, const int brushmask, const int brushreject)
{
	trace_t newtr, tr;
	int i;
	cBspHead_t *h;

	memset(&tr, 0, sizeof(tr));
	/* ensure that the first trace is set in every case */
	tr.fraction = 2.0f;

	/* trace against all loaded map tiles */
	for (i = 0, h = myTile->cheads; i < myTile->numcheads; i++, h++) {
		/* This code uses levelmask to limit by maplevel.  Supposedly maplevels 1-255
		 * are bitmasks of game levels 1-8.  0 is a special case repeat of 255.
		 * However a levelmask including 0x100 is usually included so the CLIP levels are
		 * examined. */
		if (h->level && h->level <= LEVEL_LASTVISIBLE && levelmask && !(h->level & levelmask))
			continue;

		assert(h->cnode < myTile->numnodes + 6); /* +6 => bbox */
		newtr = TR_BoxTrace(myTile, start, end, mins, maxs, h->cnode, brushmask, brushreject, tr.fraction);

		/* memorize the trace with the minimal fraction */
		if (newtr.fraction == 0.0)
			return newtr;
		if (newtr.fraction < tr.fraction)
			tr = newtr;
	}
	return tr;
}

#ifdef COMPILE_MAP

/**
 * @param[in] start trace start vector
 * @param[in] end trace end vector
 * @param[in] mins box mins
 * @param[in] maxs box maxs
 * @param[in] levelmask Selects which submodels get scanned.
 * @param[in] brushmask brushes the trace should stop at (see MASK_*)
 * @param[in] brushreject brushes the trace should ignore (see MASK_*)
 * @brief Traces all submodels in the first tile.  Used by ufo2map.
 */
trace_t TR_SingleTileBoxTrace (const vec3_t start, const vec3_t end, const box_t* traceBox, const int levelmask, const int brushmask, const int brushreject)
{
	trace_t tr;
	/* Trace the whole line against the first tile. */
	tr = TR_TileBoxTrace(&mapTiles[0], start, end, traceBox->mins, traceBox->maxs, levelmask, brushmask, brushreject);
	tr.mapTile = 0;
	return tr;
}

#else

/**
 * @param[in] start trace start vector
 * @param[in] end trace end vector
 * @param[in] mins box mins
 * @param[in] maxs box maxs
 * @param[in] levelmask Selects which submodels get scanned.
 * @param[in] brushmask brushes the trace should stop at (see MASK_*)
 * @param[in] brushreject brushes the trace should ignore (see MASK_*)
 * @brief Traces all submodels in all tiles.  Used by ufo and ufo_ded.
 */
trace_t TR_CompleteBoxTrace (const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs, const int levelmask, const int brushmask, const int brushreject)
{
	trace_t newtr, tr;
	int tile, i;
	vec3_t smin, smax, emin, emax, wpmins, wpmaxs;
	const vec3_t offset = {UNIT_SIZE / 2, UNIT_SIZE / 2, UNIT_HEIGHT / 2};

	memset(&tr, 0, sizeof(tr));
	tr.fraction = 2.0f;

	/* Prep the mins and maxs */
	for (i = 0; i < 3; i++) {
		smin[i] = start[i] + min(mins[i], maxs[i]);
		smax[i] = start[i] + max(mins[i], maxs[i]);
		emin[i] = end[i] + min(mins[i], maxs[i]);
		emax[i] = end[i] + max(mins[i], maxs[i]);
	}

	/* trace against all loaded map tiles */
	for (tile = 0; tile < numTiles; tile++) {
		TR_TILE_TYPE *myTile = &mapTiles[tile];
		PosToVec(myTile->wpMins, wpmins);
		VectorSubtract(wpmins, offset, wpmins);
		PosToVec(myTile->wpMaxs, wpmaxs);
		VectorAdd(wpmaxs, offset, wpmaxs);
		/* If the trace is completely outside of the tile, then skip it. */
		if (smax[0] < wpmins[0] && emax[0] < wpmins[0])
			continue;
		if (smax[1] < wpmins[1] && emax[1] < wpmins[1])
			continue;
		if (smax[2] < wpmins[2] && emax[2] < wpmins[2])
			continue;
		if (smin[0] > wpmaxs[0] && emin[0] > wpmaxs[0])
			continue;
		if (smin[1] > wpmaxs[1] && emin[1] > wpmaxs[1])
			continue;
		if (smin[2] > wpmaxs[2] && emin[2] > wpmaxs[2])
			continue;
		newtr = TR_TileBoxTrace(myTile, start, end, mins, maxs, levelmask, brushmask, brushreject);
		newtr.mapTile = tile;

		/* memorize the trace with the minimal fraction */
		if (newtr.fraction == 0.0)
			return newtr;
		if (newtr.fraction < tr.fraction)
			tr = newtr;
	}
	return tr;
}
#endif
