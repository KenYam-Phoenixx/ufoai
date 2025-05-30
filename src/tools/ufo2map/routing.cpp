/**
 * @file
 */

/*
All original material Copyright (C) 2002-2025 UFO: Alien Invasion.

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


#include "bsp.h"
#include "../../common/routing.h"
#include "levels.h"

static Routing Nmap;	/**< The routing tables */

/** @brief world min and max values converted from vec to pos */
static ipos3_t wpMins, wpMaxs;


/**
 * @sa CheckUnitThread
 */
static int CheckUnit (unsigned int unitnum)
{
	int new_z;

	/* get coordinates of that unit */
	const int z = unitnum % PATHFINDING_HEIGHT;
	const int y = (unitnum / PATHFINDING_HEIGHT) % PATHFINDING_WIDTH;
	const int x = (unitnum / PATHFINDING_HEIGHT / PATHFINDING_WIDTH) % PATHFINDING_WIDTH;
	const int actorSize = unitnum / PATHFINDING_HEIGHT / PATHFINDING_WIDTH / PATHFINDING_WIDTH;

	/* test bounds - the size adjustment is needed because large actor cells occupy multiple cell units. */
	if (x > wpMaxs[0] - actorSize || y > wpMaxs[1] - actorSize
	 || x < wpMins[0] || y < wpMins[1]) {
		/* don't enter - outside world */
		return 0;
	}

	/* Call the common CheckUnit function */
	new_z = RT_CheckCell(&mapTiles, Nmap, actorSize + 1, x, y, z, nullptr);

	/* new_z should never be above z. */
	assert(new_z <= z);

	/* Adjust unitnum if this check adjusted multiple cells. */
	return new_z - z;
}

/**
 * @brief A wrapper for CheckUnit that is thread safe.
 * @sa DoRouting
 */
static void CheckUnitThread (unsigned int unitnum)
{
	const int basenum = unitnum * PATHFINDING_HEIGHT;
	int newnum;
	for (newnum = basenum + PATHFINDING_HEIGHT - 1; newnum >= basenum; newnum--)
		newnum += CheckUnit(newnum);
}


/**
 * @sa DoRouting
 */
static void CheckConnectionsThread (unsigned int unitnum)
{
	/* get coordinates of that unit */
	const int numDirs = CORE_DIRECTIONS / (1 + RT_IS_BIDIRECTIONAL);
	const int dir = (unitnum % numDirs) * (RT_IS_BIDIRECTIONAL ? 2 : 1);
	const int y = (unitnum / numDirs) % PATHFINDING_WIDTH;
	const int x = (unitnum / numDirs / PATHFINDING_WIDTH) % PATHFINDING_WIDTH;
	const int actorSize = unitnum / numDirs / PATHFINDING_WIDTH / PATHFINDING_WIDTH;

	/* test bounds - the size adjustment is needed because large actor cells occupy multiple cell units. */
	if (x > wpMaxs[0] - actorSize || y > wpMaxs[1] - actorSize
	 || x < wpMins[0] || y < wpMins[1] ) {
		/* don't enter - outside world */
		/* Com_Printf("x%i y%i z%i dir%i size%i (%i, %i, %i) (%i, %i, %i)\n", x, y, z, dir, size, wpMins[0], wpMins[1], wpMins[2], wpMaxs[0], wpMaxs[1], wpMaxs[2]); */
		return;
	}

	Verb_Printf(VERB_EXTRA, "%i %i %i %i (%i, %i, %i) (%i, %i, %i)\n", x, y, dir, actorSize, wpMins[0], wpMins[1], wpMins[2], wpMaxs[0], wpMaxs[1], wpMaxs[2]);

	RT_UpdateConnectionColumn(&mapTiles, Nmap, actorSize + 1, x, y, dir);

	/* Com_Printf("z:%i nz:%i\n", z, new_z); */
}


/**
 * @brief Calculates the routing of a map
 * @sa CheckUnit
 * @sa CheckConnections
 * @sa ProcessWorldModel
 */
void DoRouting (void)
{
	int i;
	byte* data;
	pos3_t pos;

	/* Turn on trace debugging if requested. */
	if (config.generateDebugTrace)
		debugTrace = true;

	/* Record the current mapTiles[0] state so we can remove all CLIPS when done. */
	PushInfo();

	/* build tracing structure */
	EmitBrushes();
	EmitPlanes(); /** This is needed for tracing to work!!! */
	/** @note LEVEL_TRACING is not an actual level */
	MakeTracingNodes(LEVEL_ACTORCLIP + 1);

	/* Reset the whole block of map data to 0 */
	Nmap.init();

	/* get world bounds for optimizing */
	AABB tileBox;
	RT_GetMapSize(&mapTiles, tileBox);
	VecToPos(tileBox.mins, wpMins);
	VecToPos(tileBox.maxs, wpMaxs);

	/* wpMaxs is on a 32 boundary. This causes VecToPos to calculate a pos *above* wpMAxs. We have to compensate. */
	wpMaxs[0]--;
	wpMaxs[1]--;
	/* Note that VecToPos already caps the z value to PATHFINDING_HEIGHT - 1 */
	if (tileBox.maxs[2] - 1 < UNIT_HEIGHT * (PATHFINDING_HEIGHT - 1))
		wpMaxs[2]--;

	/* Verify the world extents are not lopsided. */
	assert(wpMins[0] <= wpMaxs[0]);
	assert(wpMins[1] <= wpMaxs[1]);
	assert(wpMins[2] <= wpMaxs[2]);

	/* scan area heights */
	RunSingleThreadOn(CheckUnitThread, PATHFINDING_WIDTH * PATHFINDING_WIDTH * ACTOR_MAX_SIZE, config.verbosity >= VERB_NORMAL, "UNITCHECK");

	/* scan connections */
	RunSingleThreadOn(CheckConnectionsThread, PATHFINDING_WIDTH * PATHFINDING_WIDTH * (CORE_DIRECTIONS / (1 + RT_IS_BIDIRECTIONAL)) * (ACTOR_MAX_SIZE), config.verbosity >= VERB_NORMAL, "CONNCHECK");

	/* Try to shrink the world bounds along the x and y coordinates */
	for (i = 0; i < 2; i++) {			/* for x and y, but not z */
		int j = i ^ 1;					/* if i points to x, j points to y and vice versa */
		/* Increase the mins */
		while (wpMaxs[j] > wpMins[j]) {
			VectorSet(pos, wpMins[0], wpMins[1], wpMaxs[2]);
			for (pos[i] = wpMins[i]; pos[i] <= wpMaxs[i]; pos[i]++) {	/* for all cells in an x or y row */
				if (Nmap.getFloor(1, pos[0], pos[1], wpMaxs[2]) + wpMaxs[2] * CELL_HEIGHT != -1)	/* no floor ? */
					break;
			}
			if (pos[i] <= wpMaxs[i])	/* found a floor before the end of the row ? */
				break;					/* break while */
			wpMins[j]++;				/* if it was an x-row, increase y-value of mins and vice versa */
		}
		/* Decrease the maxs */
		while (wpMaxs[j] > wpMins[j]) {
			VectorCopy(wpMaxs, pos);
			for (pos[i] = wpMins[i]; pos[i] <= wpMaxs[i]; pos[i]++) {
				if (Nmap.getFloor(1, pos[0], pos[1], wpMaxs[2]) + wpMaxs[2] * CELL_HEIGHT != -1)
					break;
			}
			if (pos[i] <= wpMaxs[i])
				break;
			wpMaxs[j]--;
		}
	}

	/* Output the floor trace file if set */
	if (config.generateTraceFile) {
		RT_WriteCSVFiles(Nmap, baseFilename, GridBox(wpMins, wpMaxs));
	}

	/* store the data */
	data = curTile->routedata;
	for (i = 0; i < 3; i++)
		wpMins[i] = LittleLong(wpMins[i]);
	data = CompressRouting((byte*)wpMins, data, sizeof(wpMins));
	for (i = 0; i < 3; i++)
		wpMaxs[i] = LittleLong(wpMaxs[i]);
	data = CompressRouting((byte*)wpMaxs, data, sizeof(wpMaxs));
	data = CompressRouting((byte*)&Nmap, data, sizeof(Nmap));

	curTile->routedatasize = data - curTile->routedata;

	/* Ensure that we did not exceed our allotment of memory for this data. */
	assert(curTile->routedatasize <= MAX_MAP_ROUTING);

	/* Remove the CLIPS fom the tracing structure by resetting it. */
	PopInfo();
}
