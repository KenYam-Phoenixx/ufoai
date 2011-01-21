/**
 * @file e_event_actormove.c
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

#include "../../../../client.h"
#include "../../../cl_localentity.h"
#include "e_event_actormove.h"

int CL_ActorDoMoveTime (const eventRegister_t *self, struct dbuffer *msg, const int dt)
{
#if 0
	le_t *le;
	int number, i;
	int time = cl.time;

	number = NET_ReadShort(msg);
	/* get le */
	le = LE_Get(number);
	if (!le)
		LE_NotFoundError(number);

	for (i = 0; i < le->pathLength; i++) {
		const dvec_t dvec = le->dvtab[i];
		const byte dir = getDVdir(dvec);
		time += LE_ActorGetStepTime(le, dir, le->pathPos);
	}

	return time;
#else
	return cl.time;
#endif
}

/**
 * @brief Moves actor.
 * @param[in] self Pointer to the event structure that is currently executed
 * @param[in] msg The netchannel message
 * @sa LET_PathMove
 * @note EV_ACTOR_MOVE
 */
void CL_ActorDoMove (const eventRegister_t *self, struct dbuffer *msg)
{
	le_t *le;
	int number, i, newPathLength;

	number = NET_ReadShort(msg);
	/* get le */
	le = LE_Get(number);
	if (!le)
		LE_NotFoundError(number);

	if (!LE_IsActor(le))
		Com_Error(ERR_DROP, "Can't move, LE doesn't exist or is not an actor (number: %i, type: %i)\n",
			number, le->type);

	if (LE_IsDead(le))
		Com_Error(ERR_DROP, "Can't move, actor on team %i dead", le->team);

	/* lock this le for other events, the corresponding unlock is in LE_DoEndPathMove() */
	LE_Lock(le);
	newPathLength = NET_ReadByte(msg);
	if (le->pathLength > 0) {
		if (le->pathLength == le->pathPos) {
			LE_DoEndPathMove(le);
			le->pathLength = le->pathPos = 0;
		} else {
			Com_Error(ERR_DROP, "Actor (entnum: %i) on team %i is still moving (%i steps left).  Times: %i, %i, %i",
					le->entnum, le->team, le->pathLength - le->pathPos, le->startTime, le->endTime, cl.time);
		}
	}

	le->pathLength = newPathLength;
	if (le->pathLength >= MAX_LE_PATHLENGTH)
		Com_Error(ERR_DROP, "Overflow in pathLength");

	/* Also get the final position */
	le->newPos[0] = NET_ReadByte(msg);
	le->newPos[1] = NET_ReadByte(msg);
	le->newPos[2] = NET_ReadByte(msg);

	for (i = 0; i < le->pathLength; i++) {
		le->dvtab[i] = NET_ReadShort(msg); /** Don't adjust dv values here- the whole thing is needed to move the actor! */
		le->speed[i] = NET_ReadShort(msg);
		le->pathContents[i] = NET_ReadShort(msg);
	}

	/* activate PathMove function */
	FLOOR(le) = NULL;
	LE_SetThink(le, LET_StartPathMove);
	le->pathPos = 0;
	le->startTime = cl.time;
	le->endTime = cl.time;
}
