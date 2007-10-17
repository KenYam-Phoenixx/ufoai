/**
 * @file trace.c
 * @brief
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


#include "shared.h"
#include "cmdlib.h"
#include "mathlib.h"
#include "../qbsp.h"
#include "bspfile.h"
#include <stddef.h>

#define	ON_EPSILON	0.1

typedef struct tnode_s {
	int		type;
	vec3_t	normal;
	float	dist;
	int		children[2];
	int		pad;
} tnode_t;

static tnode_t *tnodes, *tnode_p;

static int numtheads = 0;
static int thead[260];
static int theadlevel[260];

static int neededContents = (CONTENTS_SOLID | CONTENTS_STEPON | CONTENTS_ACTORCLIP);
static int forbiddenContents = (CONTENTS_PASSABLE);
static vec3_t tr_end;

/**
 * @brief Converts the disk node structure into the efficient tracing structure
 */
static void MakeTnode (int nodenum)
{
	tnode_t *t;
	dplane_t *plane;
	int i, contentFlags;
	dnode_t *node;

	t = tnode_p++;

	node = dnodes + nodenum;
	plane = dplanes + node->planenum;

	t->type = plane->type;
	VectorCopy(plane->normal, t->normal);
	t->dist = plane->dist;

	for (i = 0; i < 2; i++) {
		if (node->children[i] < 0) {
			contentFlags = dleafs[-node->children[i] - 1].contentFlags & ~(1<<31);
			if ((contentFlags & neededContents) && !(contentFlags & forbiddenContents))
				t->children[i] = 1 | (1<<31); /*-node->children[i] | (1<<31); // leaf+1 */
			else
				t->children[i] = (1<<31);
		} else {
			t->children[i] = tnode_p - tnodes;
			MakeTnode(node->children[i]);
		}
	}
}


/**
 * @brief
 */
static void BuildTnode_r (int node)
{
	assert(node < numnodes);
	if (dnodes[node].planenum == -1) {
		dnode_t *n;
		tnode_t *t;
		vec3_t c0maxs, c1mins;
		int i;

		n = &dnodes[node];

		/* alloc new node */
		t = tnode_p++;

		if (n->children[0] < 0 || n->children[1] < 0)
			Sys_Error("Unexpected leaf");

		VectorCopy(dnodes[n->children[0]].maxs, c0maxs);
		VectorCopy(dnodes[n->children[1]].mins, c1mins);

		/*	Com_Printf("(%i %i : %i %i) (%i %i : %i %i)\n", */
		/*		(int)dnodes[n->children[0]].mins[0], (int)dnodes[n->children[0]].mins[1], (int)dnodes[n->children[0]].maxs[0], (int)dnodes[n->children[0]].maxs[1], */
		/*		(int)dnodes[n->children[1]].mins[0], (int)dnodes[n->children[1]].mins[1], (int)dnodes[n->children[1]].maxs[0], (int)dnodes[n->children[1]].maxs[1] ); */

		for (i = 0; i < 2; i++)
			if (c0maxs[i] <= c1mins[i]) {
				/* create a separation plane */
				t->type = i;
				t->normal[0] = i;
				t->normal[1] = i^1;
				t->normal[2] = 0;
				t->dist = (c0maxs[i] + c1mins[i]) / 2;

				t->children[1] = tnode_p - tnodes;
				BuildTnode_r(n->children[0]);
				t->children[0] = tnode_p - tnodes;
				BuildTnode_r(n->children[1]);
				return;
			}

		/* can't construct such a separation plane */
		t->type = PLANE_NONE;

		for (i = 0; i < 2; i++) {
			t->children[i] = tnode_p - tnodes;
			BuildTnode_r(n->children[i]);
		}
	} else {
		MakeTnode(node);
	}
}


/**
 * @brief Loads the node structure out of a .bsp file to be used for light occlusion
 * @sa CloseTNodes
 */
void MakeTnodes (int levels)
{
	int i;
	size_t size;

	size = (numnodes + 1) * sizeof(tnode_t);
	/* 32 byte align the structs */
	size = (size + 31) &~ 31;

	tnodes = malloc(size);
	tnode_p = tnodes;
	numtheads = 0;

	for (i = 0; i < levels; i++) {
		if (!dmodels[i].numfaces)
			continue;

		thead[numtheads] = tnode_p - tnodes;
		theadlevel[numtheads] = i;
		numtheads++;
		assert(numtheads < 260);
		BuildTnode_r(dmodels[i].headnode);
	}
}


/**
 * @brief
 */
static int TestLine_r (int node, const vec3_t start, const vec3_t stop)
{
	tnode_t *tnode;
	float front, back, frac;
	vec3_t mid;
	int side, r;

	if (node & (1 << 31))
		return node & ~(1 << 31);	/* leaf node */

	tnode = &tnodes[node];
	switch (tnode->type) {
	case PLANE_X:
		front = start[0] - tnode->dist;
		back = stop[0] - tnode->dist;
		break;
	case PLANE_Y:
		front = start[1] - tnode->dist;
		back = stop[1] - tnode->dist;
		break;
	case PLANE_Z:
		front = start[2] - tnode->dist;
		back = stop[2] - tnode->dist;
		break;
	case PLANE_NONE:
		r = TestLine_r(tnode->children[0], start, stop);
		if (r)
			return r;
		return TestLine_r(tnode->children[1], start, stop);
		break;
	default:
		front = (start[0]*tnode->normal[0] + start[1]*tnode->normal[1] + start[2]*tnode->normal[2]) - tnode->dist;
		back = (stop[0]*tnode->normal[0] + stop[1]*tnode->normal[1] + stop[2]*tnode->normal[2]) - tnode->dist;
		break;
	}

	if (front >= -ON_EPSILON && back >= -ON_EPSILON)
		return TestLine_r(tnode->children[0], start, stop);

	if (front < ON_EPSILON && back < ON_EPSILON)
		return TestLine_r(tnode->children[1], start, stop);

	side = front < 0;

	frac = front / (front-back);

	mid[0] = start[0] + (stop[0] - start[0])*frac;
	mid[1] = start[1] + (stop[1] - start[1])*frac;
	mid[2] = start[2] + (stop[2] - start[2])*frac;

	r = TestLine_r(tnode->children[side], start, mid);
	if (r)
		return r;
	return TestLine_r(tnode->children[!side], mid, stop);
}

/**
 * @brief
 */
static int TestLineDist_r (int node, const vec3_t start, const vec3_t stop)
{
	tnode_t *tnode;
	float front, back, frac;
	vec3_t mid;
	int side, r;

	if (node & (1 << 31)) {
		r = node & ~(1 << 31);
		if (r)
			VectorCopy(start, tr_end);
		return r;	/* leaf node */
	}

	tnode = &tnodes[node];
	switch (tnode->type) {
	case PLANE_X:
		front = start[0] - tnode->dist;
		back = stop[0] - tnode->dist;
		break;
	case PLANE_Y:
		front = start[1] - tnode->dist;
		back = stop[1] - tnode->dist;
		break;
	case PLANE_Z:
		front = start[2] - tnode->dist;
		back = stop[2] - tnode->dist;
		break;
	case PLANE_NONE:
		r = TestLineDist_r(tnode->children[0], start, stop);
		if (r)
			VectorCopy(tr_end, mid);
		side = TestLineDist_r(tnode->children[1], start, stop);
		if (side && r) {
			if (VectorNearer(mid, tr_end, start)) {
				VectorCopy(mid, tr_end);
				return r;
			}
			else return side;
		}
		if (r) {
			VectorCopy(mid, tr_end);
			return r;
		}
		return side;
		break;
	default:
		front = (start[0]*tnode->normal[0] + start[1]*tnode->normal[1] + start[2]*tnode->normal[2]) - tnode->dist;
		back = (stop[0]*tnode->normal[0] + stop[1]*tnode->normal[1] + stop[2]*tnode->normal[2]) - tnode->dist;
		break;
	}

	if (front >= -ON_EPSILON && back >= -ON_EPSILON)
		return TestLineDist_r(tnode->children[0], start, stop);

	if (front < ON_EPSILON && back < ON_EPSILON)
		return TestLineDist_r(tnode->children[1], start, stop);

	side = front < 0;

	frac = front / (front-back);

	mid[0] = start[0] + (stop[0] - start[0])*frac;
	mid[1] = start[1] + (stop[1] - start[1])*frac;
	mid[2] = start[2] + (stop[2] - start[2])*frac;

	r = TestLineDist_r(tnode->children[side], start, mid);
	if (r)
		return r;
	return TestLineDist_r(tnode->children[!side], mid, stop);
}

/**
 * @brief
 */
int TestLine (const vec3_t start, const vec3_t stop)
{
	int i;

	for (i = 0; i < numtheads; i++) {
		if (TestLine_r(thead[i], start, stop))
			return 1;
	}
	return 0;
}

/**
 * @brief
 * @param[in] start
 * @param[in] stop
 * @param[in] levels don't check levels higher than LEVEL_LASTVISIBLE + levels
 * @note levels:
 * 256: weaponclip-level
 * 257: actorclip-level
 * 258: stepon-level
 * 259: tracing structure
 */
int TestLineMask (const vec3_t start, const vec3_t stop, int levels)
{
	int i;

	/* loop over all theads */
	for (i = 0; i < numtheads; i++) {
		if (theadlevel[i] > LEVEL_LASTVISIBLE + levels)
			continue;

		if (TestLine_r(thead[i], start, stop))
			return 1;
	}
	return 0;
}

/**
 * @brief
 */
int TestLineDM (const vec3_t start, const vec3_t stop, vec3_t end, int levels)
{
	int i;

	VectorCopy(stop, end);

	for (i = 0; i < numtheads; i++) {
		if (theadlevel[i] > LEVEL_LASTVISIBLE + levels)
			continue;

		if (TestLineDist_r(thead[i], start, stop))
			if (VectorNearer(tr_end, end, start))
				VectorCopy(tr_end, end);
	}

	if (VectorCompare(end, stop))
		return 0;
	else
		return 1;
}

/**
 * @brief
 */
static int TestContents_r (int node, const vec3_t pos)
{
	tnode_t	*tnode;
	float	front;
	int		r;

	if (node & (1 << 31))
		return node & ~(1 << 31);	/* leaf node */

	tnode = &tnodes[node];
	switch (tnode->type) {
	case PLANE_X:
		front = pos[0] - tnode->dist;
		break;
	case PLANE_Y:
		front = pos[1] - tnode->dist;
		break;
	case PLANE_Z:
		front = pos[2] - tnode->dist;
		break;
	case PLANE_NONE:
		r = TestContents_r(tnode->children[0], pos);
		if (r)
			return r;
		return TestContents_r(tnode->children[1], pos);
		break;
	default:
		front = (pos[0]*tnode->normal[0] + pos[1]*tnode->normal[1] + pos[2]*tnode->normal[2]) - tnode->dist;
		break;
	}

	if (front >= 0)
		return TestContents_r(tnode->children[0], pos);
	else
		return TestContents_r(tnode->children[1], pos);
}


/**
 * @brief Step height check
 * @sa TestContents_r
 */
qboolean TestContents (const vec3_t pos)
{
	int i;

	/* loop over all theads */
	for (i = numtheads - 1; i >= 0; i--) {
		if (theadlevel[i] != 258) /* only check stepon here */
			continue;

		if (TestContents_r(thead[i], pos))
			return qtrue;
		break;
	}
	return qfalse;
}

/**
 * @brief
 * @sa MakeTnodes
 */
void CloseTNodes (void)
{
	if (tnodes)
		free(tnodes);
	tnodes = NULL;
}
