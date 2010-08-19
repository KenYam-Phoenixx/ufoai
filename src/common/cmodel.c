/**
 * @file cmodel.c
 * @brief model loading and grid oriented movement and scanning
 * @note collision detection code (server side)
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

#include "common.h"
#include "grid.h"
#include "tracing.h"
#include "routing.h"
#include "../shared/parse.h"

/*
===============================================================================
GAME RELATED TRACING USING ENTITIES
===============================================================================
*/

/**
 * @brief Calculates the bounding box for the given bsp model
 * @param[in] model The model to calculate the bbox for
 * @param[out] mins The maxs of the bbox
 * @param[out] maxs The mins of the bbox
 */
static void CM_CalculateBoundingBox (const cBspModel_t* model, vec3_t mins, vec3_t maxs)
{
	/* Quickly calculate the bounds of this model to see if they can overlap. */
	VectorAdd(model->origin, model->mins, mins);
	VectorAdd(model->origin, model->maxs, maxs);
	if (VectorNotEmpty(model->angles)) {
		vec3_t acenter, aoffset;
		const float offset = max(max(fabs(mins[0] - maxs[0]), fabs(mins[1] - maxs[1])), fabs(mins[2] - maxs[2])) / 2.0;
		VectorCenterFromMinsMaxs(mins, maxs, acenter);
		VectorSet(aoffset, offset, offset, offset);
		VectorAdd(acenter, aoffset, maxs);
		VectorSubtract(acenter, aoffset, mins);
	}
}

/**
 * @brief A quick test if the trace might hit the inline model
 * @param[in] start The position to start the trace.
 * @param[in] stop The position where the trace ends.
 * @param[in] model The entity to check
 * @return qtrue - the line isn't anywhere near the model
 */
static qboolean CM_LineMissesModel (const vec3_t start, const vec3_t stop, const cBspModel_t *model)
{
	vec3_t amins, amaxs;
	CM_CalculateBoundingBox(model, amins, amaxs);
	/* If the bounds of the extents box and the line do not overlap, then skip tracing this model. */
	if ((start[0] > amaxs[0] && stop[0] > amaxs[0])
		|| (start[1] > amaxs[1] && stop[1] > amaxs[1])
		|| (start[2] > amaxs[2] && stop[2] > amaxs[2])
		|| (start[0] < amins[0] && stop[0] < amins[0])
		|| (start[1] < amins[1] && stop[1] < amins[1])
		|| (start[2] < amins[2] && stop[2] < amins[2]))
		return qtrue;

	return qfalse;
}

/**
 * @brief Wrapper for TR_TransformedBoxTrace that accepts a tile number,
 * @sa TR_TransformedBoxTrace
 */
trace_t CM_HintedTransformedBoxTrace (const int tile, const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs, const int headnode, const int brushmask, const int brushrejects, const vec3_t origin, const vec3_t angles, const vec3_t rmaShift, const float fraction)
{
	return TR_HintedTransformedBoxTrace(&mapTiles[tile], start, end, mins, maxs, headnode, brushmask, brushrejects, origin, angles, rmaShift, fraction);
}

/**
 * @brief To keep everything totally uniform, bounding boxes are turned into small
 * BSP trees instead of being compared directly.
 */
int CM_HeadnodeForBox (int tile, const vec3_t mins, const vec3_t maxs)
{
	assert(tile < numTiles && tile >= 0);
	return TR_HeadnodeForBox(&mapTiles[tile], mins, maxs);
}

/* TRACING FUNCTIONS */

/**
 * @brief Checks traces against the world and all inline models
 * @param[in] start The position to start the trace.
 * @param[in] stop The position where the trace ends.
 * @param[in] levelmask
 * @sa TR_TestLine
 * @sa CM_InlineModel
 * @sa TR_TransformedBoxTrace
 * @return qtrue - hit something
 * @return qfalse - hit nothing
 */
static qboolean CM_EntTestLine (const vec3_t start, const vec3_t stop, const int levelmask, const char **entlist)
{
	trace_t trace;
	cBspModel_t *model;
	const char **name;

	/* trace against world first */
	if (TR_TestLine(start, stop, levelmask))
		/* We hit the world, so we didn't make it anyway... */
		return qtrue;

	/* no local models, so we made it. */
	if (!entlist)
		return qfalse;

	for (name = entlist; *name; name++) {
		/* check whether this is really an inline model */
		if (*name[0] != '*')
			Com_Error(ERR_DROP, "name in the inlineList is no inline model: '%s'", *name);
		model = CM_InlineModel(*name);
		assert(model);
		if (model->headnode >= mapTiles[model->tile].numnodes + 6)
			continue;

		/* check if we can safely exclude that the trace can hit the model */
		if (CM_LineMissesModel(start, stop, model))
			continue;

		trace = CM_HintedTransformedBoxTrace(model->tile, start, stop, vec3_origin, vec3_origin,
				model->headnode, MASK_VISIBILILITY, 0, model->origin, model->angles, model->shift, 1.0);
		/* if we started the trace in a wall */
		/* or the trace is not finished */
		if (trace.startsolid || trace.fraction < 1.0)
			return qtrue;
	}

	/* not blocked */
	return qfalse;
}

/**
 * @brief Checks traces against the world and all inline models, gives the hit position back
 * @param[in] start The position to start the trace.
 * @param[in] stop The position where the trace ends.
 * @param[out] end The position where the line hits a object or the stop position if nothing is in the line
 * @param[in] levelmask
 * @sa TR_TestLineDM
 * @sa TR_TransformedBoxTrace
 */
static qboolean CM_EntTestLineDM (const vec3_t start, const vec3_t stop, vec3_t end, const int levelmask, const char **entlist)
{
	trace_t trace;
	cBspModel_t *model;
	const char **name;
	int blocked;
	float fraction = 2.0f;

	/* trace against world first */
	blocked = TR_TestLineDM(start, stop, end, levelmask);
	if (!entlist)
		return blocked;

	for (name = entlist; *name; name++) {
		/* check whether this is really an inline model */
		if (*name[0] != '*') {
			/* Let's see what the data looks like... */
			Com_Error(ERR_DROP, "name in the inlineList is no inline model: '%s' (inlines: %p, name: %p)",
					*name, entlist, name);
		}
		model = CM_InlineModel(*name);
		assert(model);
		if (model->headnode >= mapTiles[model->tile].numnodes + 6)
			continue;

		/* check if we can safely exclude that the trace can hit the model */
		if (CM_LineMissesModel(start, stop, model))
			continue;

		trace = CM_HintedTransformedBoxTrace(model->tile, start, end, vec3_origin, vec3_origin,
				model->headnode, MASK_ALL, 0, model->origin, model->angles, vec3_origin, fraction);
		/* if we started the trace in a wall */
		if (trace.startsolid) {
			VectorCopy(start, end);
			return qtrue;
		}
		/* trace not finishd */
		if (trace.fraction < fraction) {
			blocked = qtrue;
			fraction = trace.fraction;
			VectorCopy(trace.endpos, end);
		}
	}

	/* return result */
	return blocked;
}

/**
 * @brief Performs box traces against the world and all inline models, gives the hit position back
 * @param[in] start The position to start the trace.
 * @param[in] end The position where the trace ends.
 * @param[in] traceBox The minimum/maximum extents of the collision box that is projected.
 * @param[in] levelmask A mask of the game levels to trace against. Mask 0x100 filters clips.
 * @param[in] brushmask Any brush detected must at least have one of these contents.
 * @param[in] brushreject Any brush detected with any of these contents will be ignored.
 * @param[in] list The local models list (a local model has a name starting with * followed by the model number)
 * @return a trace_t with the information of the closest brush intersected.
 * @sa TR_CompleteBoxTrace
 * @sa CM_HintedTransformedBoxTrace
 */
trace_t CM_EntCompleteBoxTrace (const vec3_t start, const vec3_t end, const box_t* traceBox, int levelmask, int brushmask, int brushreject, const char **list)
{
	trace_t trace, newtr;
	cBspModel_t *model;
	const char **name;
	vec3_t bmins, bmaxs;

	/* trace against world first */
	trace = TR_CompleteBoxTrace(start, end, traceBox->mins, traceBox->maxs, levelmask, brushmask, brushreject);
	if (!list || trace.fraction == 0.0)
		return trace;

	/* Find the original bounding box for the tracing line. */
	VectorSet(bmins, min(start[0], end[0]), min(start[1], end[1]), min(start[2], end[2]));
	VectorSet(bmaxs, max(start[0], end[0]), max(start[1], end[1]), max(start[2], end[2]));
	/* Now increase the bounding box by mins and maxs in both directions. */
	VectorAdd(bmins, traceBox->mins, bmins);
	VectorAdd(bmaxs, traceBox->maxs, bmaxs);
	/* Now bmins and bmaxs specify the whole volume to be traced through. */

	for (name = list; *name; name++) {
		vec3_t amins, amaxs;

		/* check whether this is really an inline model */
		if (*name[0] != '*')
			Com_Error(ERR_DROP, "name in the inlineList is no inline model: '%s'", *name);
		model = CM_InlineModel(*name);
		assert(model);
		if (model->headnode >= mapTiles[model->tile].numnodes + 6)
			continue;

		/* Quickly calculate the bounds of this model to see if they can overlap. */
		CM_CalculateBoundingBox(model, amins, amaxs);

		/* If the bounds of the extents box and the line do not overlap, then skip tracing this model. */
		if (bmins[0] > amaxs[0]
		 || bmins[1] > amaxs[1]
		 || bmins[2] > amaxs[2]
		 || bmaxs[0] < amins[0]
		 || bmaxs[1] < amins[1]
		 || bmaxs[2] < amins[2])
			continue;

		newtr = CM_HintedTransformedBoxTrace(model->tile, start, end, traceBox->mins, traceBox->maxs,
				model->headnode, brushmask, brushreject, model->origin, model->angles, model->shift, trace.fraction);

		/* memorize the trace with the minimal fraction */
		if (newtr.fraction == 0.0)
			return newtr;
		if (newtr.fraction < trace.fraction)
			trace = newtr;
	}
	return trace;
}

/**
 * @brief This function recalculates the routing surrounding the entity name.
 * @sa CM_InlineModel
 * @sa CM_CheckUnit
 * @sa CM_UpdateConnection
 * @sa CMod_LoadSubmodels
 * @sa Grid_RecalcBoxRouting
 * @param[in] map The routing map (either server or client map)
 * @param[in] name Name of the inline model to compute the mins/maxs for
 * @param[in] list The local models list (a local model has a name starting with * followed by the model number)
 */
void CM_RecalcRouting (routing_t *map, const char *name, const char **list)
{
	Grid_RecalcRouting(map, name, list);
}


/**
 * @brief Checks traces against the world and all inline models
 * @param[in] start The position to start the trace.
 * @param[in] stop The position where the trace ends.
 * @param[in] levelmask
 * @param[in] entlist of entities that might be on this line
 * @sa CM_EntTestLine
 * @return qtrue - hit something
 * @return qfalse - hit nothing
 */
qboolean CM_TestLineWithEnt (const vec3_t start, const vec3_t stop, const int levelmask, const char **entlist)
{
	/* do the line test */
	const qboolean hit = CM_EntTestLine(start, stop, levelmask, entlist);
	return hit;
}

/**
 * @brief Checks traces against the world and all inline models
 * @param[in] start The position to start the trace.
 * @param[in] stop The position where the trace ends.
 * @param[out] end The position where the trace hits something
 * @param[in] levelmask The bsp level that is used for tracing in (see @c TL_FLAG_*)
 * @param[in] entlist of entities that might be on this line
 * @sa CM_EntTestLineDM
 * @return qtrue - hit something
 * @return qfalse - hit nothing
 */
qboolean CM_TestLineDMWithEnt (const vec3_t start, const vec3_t stop, vec3_t end, const int levelmask, const char **entlist)
{
	/* do the line test */
	const qboolean hit = CM_EntTestLineDM(start, stop, end, levelmask, entlist);
	return hit;
}
