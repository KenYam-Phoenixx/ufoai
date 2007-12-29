/**
 * @file g_client.c
 * @brief Main part of the game logic.
 */

/*
Copyright (C) 2002-2007 UFO: Alien Invasion team.

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

#include "g_local.h"

#define VIS_APPEAR	1
#define VIS_PERISH	2

int turnTeam;	/* Defined in g_local.h Stores level.activeTeam while G_CanReactionFire() is abusing it. */

int reactionFiremode[MAX_EDICTS][RF_MAX]; /* Defined in g_local.h See there for full info. */

/* if actors appear or perish we have to handle the movement of the current actor
 * a little bit different */
static qboolean sentAppearPerishEvent;

/**
 * @brief Generates the player mask
 */
int G_TeamToPM (int team)
{
	player_t *player;
	int player_mask, i;

	player_mask = 0;

	/* don't handle the ai players, here */
	for (i = 0, player = game.players; i < game.sv_maxplayersperteam; i++, player++)
		if (player->inuse && team == player->pers.team)
			player_mask |= (1 << i);

	return player_mask;
}


/**
 * @brief Converts vis mask to player mask
 */
int G_VisToPM (int vis_mask)
{
	player_t *player;
	int player_mask, i;

	player_mask = 0;

	/* don't handle the ai players, here */
	for (i = 0, player = game.players; i < game.sv_maxplayersperteam; i++, player++)
		if (player->inuse && (vis_mask & (1 << player->pers.team)))
			player_mask |= (1 << i);

	return player_mask;
}


/**
 * @brief Send stats to network buffer
 * @sa CL_ActorStats
 */
void G_SendStats (edict_t * ent)
{
	/* extra sanity checks */
	ent->TU = max(ent->TU, 0);
	ent->HP = max(ent->HP, 0);
	ent->STUN = min(ent->STUN, 255);
	ent->morale = max(ent->morale, 0);

	gi.AddEvent(G_TeamToPM(ent->team), EV_ACTOR_STATS);
	gi.WriteShort(ent->number);
	gi.WriteByte(ent->TU);
	gi.WriteShort(ent->HP);
	gi.WriteByte(ent->STUN);
	gi.WriteByte(ent->morale);
}


/**
 * @brief Write an item to the network buffer
 * @sa CL_NetReceiveItem
 */
static void G_WriteItem (item_t item, int container, int x, int y)
{
	assert(item.t != NONE);
	gi.WriteFormat("sbsbbbb", item.t, item.a, item.m, container, x, y, item.rotated);
}

/**
 * @brief Read item from the network buffer
 * @sa CL_NetReceiveItem
 */
static void G_ReadItem (item_t * item, int * container, int * x, int * y)
{
	gi.ReadFormat("sbsbbbb", &item->t, &item->a, &item->m, container, x, y, &item->rotated);
}


/**
 * @brief Write player stats to network buffer
 * @sa G_SendStats
 */
static void G_SendPlayerStats (player_t * player)
{
	edict_t *ent;
	int i;

	for (i = 0, ent = g_edicts; i < globals.num_edicts; i++, ent++)
		if (ent->inuse && (ent->type == ET_ACTOR || ent->type == ET_ACTOR2x2) && ent->team == player->pers.team)
			G_SendStats(ent);
}


/**
 * @brief Network function to update the time units
 * @sa G_SendStats
 */
void G_GiveTimeUnits (int team)
{
	edict_t *ent;
	int i;

	for (i = 0, ent = g_edicts; i < globals.num_edicts; i++, ent++)
		if (ent->inuse && !(ent->state & STATE_DEAD) && (ent->type == ET_ACTOR || ent->type == ET_ACTOR2x2) && ent->team == team) {
			ent->state &= ~STATE_DAZED;
			ent->TU = GET_TU(ent->chr.skills[ABILITY_SPEED]);
			G_SendStats(ent);
		}
}


static void G_SendState (int player_mask, edict_t * ent)
{
	gi.AddEvent(player_mask & G_TeamToPM(ent->team), EV_ACTOR_STATECHANGE);
	gi.WriteShort(ent->number);
	gi.WriteShort(ent->state);

	gi.AddEvent(player_mask & ~G_TeamToPM(ent->team), EV_ACTOR_STATECHANGE);
	gi.WriteShort(ent->number);
	gi.WriteShort(ent->state & STATE_PUBLIC);
}


/**
 * @sa G_EndGame
 * @sa G_AppearPerishEvent
 * @sa CL_InvAdd
 */
void G_SendInventory (int player_mask, edict_t * ent)
{
	invList_t *ic;
	unsigned short nr = 0;
	int j;

	/* test for pointless player mask */
	if (!player_mask)
		return;

	for (j = 0; j < gi.csi->numIDs; j++)
		for (ic = ent->i.c[j]; ic; ic = ic->next)
			nr++;

	/* return if no inventory items to send */
	if (nr == 0 && ent->type != ET_ITEM) {
		return;
	}

	gi.AddEvent(player_mask, EV_INV_ADD);

	gi.WriteShort(ent->number);

	/* size of inventory */
	gi.WriteShort(nr * INV_INVENTORY_BYTES);
	for (j = 0; j < gi.csi->numIDs; j++)
		for (ic = ent->i.c[j]; ic; ic = ic->next) {
			/* send a single item */
			G_WriteItem(ic->item, j, ic->x, ic->y);
		}
}


/**
 * @brief Send the appear or perish event the the affected clients
 * @param[in] playermask These are the affected players or clients
 * In case of e.g. teamplay there can be more than one client affected - thus
 * this is a player mask
 * @param[in] appear Is this event about an appearing actor (or an perishing one)?
 * @param[in] check The edict we are talking about
 * @sa CL_ActorAppear
 */
void G_AppearPerishEvent (int player_mask, int appear, edict_t * check)
{
	int maxMorale;

	sentAppearPerishEvent = qtrue;

	if (appear) {
		/* appear */
		switch (check->type) {
		case ET_ACTOR:
		case ET_ACTOR2x2:
			/* parsed in CL_ActorAppear */
			gi.AddEvent(player_mask, EV_ACTOR_APPEAR);
			gi.WriteShort(check->number);
			gi.WriteByte(check->team);
			gi.WriteByte(check->chr.teamDefIndex);
			gi.WriteByte(check->chr.gender);
			gi.WriteByte(check->pnum);
			gi.WriteGPos(check->pos);
			gi.WriteByte(check->dir);
			if (RIGHT(check)) {
				gi.WriteShort(RIGHT(check)->item.t);
			} else {
				gi.WriteShort(NONE);
			}

			if (LEFT(check)) {
				gi.WriteShort(LEFT(check)->item.t);
			} else {
				gi.WriteShort(NONE);
			}

			gi.WriteShort(check->body);
			gi.WriteShort(check->head);
			gi.WriteByte(check->skin);
			gi.WriteShort(check->state & STATE_PUBLIC);
			gi.WriteByte(check->fieldSize);
			gi.WriteByte(GET_TU(check->chr.skills[ABILITY_SPEED]));
			maxMorale = GET_MORALE(check->chr.skills[ABILITY_MIND]);
			if (maxMorale >= MAX_SKILL)
				maxMorale = MAX_SKILL;
			gi.WriteByte(maxMorale);
			gi.WriteShort(check->chr.maxHP);

			if (player_mask & G_TeamToPM(check->team)) {
				gi.AddEvent(player_mask & G_TeamToPM(check->team), EV_ACTOR_STATECHANGE);
				gi.WriteShort(check->number);
				gi.WriteShort(check->state);
			}
			G_SendInventory(G_TeamToPM(check->team) & player_mask, check);
			break;

		case ET_ITEM:
			gi.AddEvent(player_mask, EV_ENT_APPEAR);
			gi.WriteShort(check->number);
			gi.WriteByte(ET_ITEM);
			gi.WriteGPos(check->pos);
			G_SendInventory(player_mask, check);
			break;
		}
	} else if (check->type == ET_ACTOR || check->type == ET_ACTOR2x2 || check->type == ET_ITEM) {
		/* disappear */
		gi.AddEvent(player_mask, EV_ENT_PERISH);
		gi.WriteShort(check->number);
	}
}


/**
 * @brief Checks whether a point is "visible" from the edicts position
 * @sa FrustomVis
 */
qboolean G_FrustumVis (edict_t * from, vec3_t point)
{
	return FrustomVis(from->origin, from->dir, point);
}


/**
 * @brief tests the visibility between two points
 * @param[in] from  The point to check visibility from
 * @param[to] to    The point to check visibility to
 * @return true if the points are visible from each other, false otherwise.
 */
static qboolean G_LineVis (vec3_t from, vec3_t to)
{
#if 0 /* this version is more accurate the other version is much faster */
	trace_t tr;
	tr = gi.trace(from, NULL, NULL, to, NULL, MASK_SOLID);
	return (tr.fraction >= 1.0);
#else
	return gi.TestLine(from, to);
#endif
}

/**
 * @brief calculate how much check is "visible" from from
 */
float G_ActorVis (vec3_t from, edict_t * check, qboolean full)
{
	vec3_t test, dir;
	float delta;
	int i, n;

	/* start on eye height */
	VectorCopy(check->origin, test);
	if (check->state & STATE_DEAD) {
		test[2] += PLAYER_DEAD;
		delta = 0;
	} else if (check->state & (STATE_CROUCHED | STATE_PANIC)) {
		test[2] += PLAYER_CROUCH - 2;
		delta = (PLAYER_CROUCH - PLAYER_MIN) / 2 - 2;
	} else {
		test[2] += PLAYER_STAND;
		delta = (PLAYER_STAND - PLAYER_MIN) / 2 - 2;
	}

	/* side shifting -> better checks */
	dir[0] = from[1] - check->origin[1];
	dir[1] = check->origin[0] - from[0];
	dir[2] = 0;
	VectorNormalize(dir);
	VectorMA(test, -7, dir, test);

	/* do 3 tests */
	n = 0;
	for (i = 0; i < 3; i++) {
		if (!G_LineVis(from, test)) {
			if (full)
				n++;
			else
				return ACTOR_VIS_100;
		}

		/* look further down or stop */
		if (!delta) {
			if (n > 0)
				return ACTOR_VIS_100;
			else
				return ACTOR_VIS_0;
		}
		VectorMA(test, 7, dir, test);
		test[2] -= delta;
	}

	/* return factor */
	switch (n) {
	case 0:
		return ACTOR_VIS_0;
	case 1:
		return ACTOR_VIS_10;
	case 2:
		return ACTOR_VIS_50;
	default:
		return ACTOR_VIS_100;
	}
}


/**
 * @brief test if check is visible by from
 * from is from team team
 */
static float G_Vis (int team, edict_t * from, edict_t * check, int flags)
{
	vec3_t eye;

	/* if any of them isn't in use, then they're not visible */
	if (!from->inuse || !check->inuse)
		return ACTOR_VIS_0;

	/* only actors and 2x2 units can see anything */
	if ((from->type != ET_ACTOR && from->type != ET_ACTOR2x2) || (from->state & STATE_DEAD))
		return ACTOR_VIS_0;

	/* living team members are always visible */
	if (team >= 0 && check->team == team && !(check->state & STATE_DEAD))
		return ACTOR_VIS_100;

	/* standard team rules */
	if (team >= 0 && from->team != team)
		return ACTOR_VIS_0;

	/* inverse team rules */
	if (team < 0 && (from->team == -team || from->team == TEAM_CIVILIAN || check->team != -team))
		return ACTOR_VIS_0;

	/* check for same pos */
	if (VectorCompare(from->pos, check->pos))
		return ACTOR_VIS_100;

	/* view distance check */
	if (VectorDistSqr(from->origin, check->origin) > MAX_SPOT_DIST * MAX_SPOT_DIST)
		return ACTOR_VIS_0;

	/* view frustum check */
	if (!(flags & VT_NOFRUSTUM) && !G_FrustumVis(from, check->origin))
		return ACTOR_VIS_0;

	/* get viewers eye height */
	VectorCopy(from->origin, eye);
	if (from->state & (STATE_CROUCHED | STATE_PANIC))
		eye[2] += EYE_CROUCH;
	else
		eye[2] += EYE_STAND;

	/* line trace check */
	switch (check->type) {
	case ET_ACTOR:
	case ET_ACTOR2x2:
		return G_ActorVis(eye, check, qfalse);
	case ET_ITEM:
		return !G_LineVis(eye, check->origin);
	default:
		return ACTOR_VIS_0;
	}
}


/**
 * @brief test if check is visible by team (or if visibility changed?)
 * @sa G_CheckVisTeam
 * @sa AI_FighterCalcGuete
 * @param[in] team the team the edict may become visible for or perish from
 * their view
 * @param[in] check the edict we are talking about - which may become visible
 * or perish
 * @param[in] flags if you wanna check whether the edict may also perish from
 * other players view, you should use the VT_PERISH bits
 * @note If the edict (check) is already visible and flags doesn't contain the
 * bits of VT_PERISH, no further checks are performed - only the VIS_YES bits
 * are returned
 */
int G_TestVis (int team, edict_t * check, int flags)
{
	int i, old;
	edict_t *from;

	/* store old flag */
	old = (check->visflags & (1 << team)) ? 1 : 0;
	if (!(flags & VT_PERISH) && old)
		return VIS_YES;

	/* test if check is visible */
	for (i = 0, from = g_edicts; i < globals.num_edicts; i++, from++)
		if (G_Vis(team, from, check, flags))
			/* visible */
			return VIS_YES | !old;

	/* not visible */
	return old;
}

/**
 * @brief This function sends all the actors to the client that are not visible
 * initially - this is needed because an actor can e.g. produce sounds that are
 * send over the net
 * @note Call this for the first G_CheckVis call for every new actor or player
 * @sa G_CheckVis
 * @sa CL_ActorAdd
 */
void G_SendInvisible (player_t* player)
{
	int i;
	edict_t* ent;
	int team = player->pers.team;

	if (level.num_alive[team]) {
		/* check visibility */
		for (i = 0, ent = g_edicts; i < globals.num_edicts; i++, ent++) {
			if (ent->inuse && ent->team != team
			&& (ent->type == ET_ACTOR || ent->type == ET_ACTOR2x2)) {
				/* not visible for this team - so add the le only */
				if (!(ent->visflags & (1 << team))) {
					/* parsed in CL_ActorAdd */
					Com_DPrintf(DEBUG_GAME, "G_SendInvisible: ent->player: %i - ent->team: %i (%s)\n",
						ent->pnum, ent->team, ent->chr.name);
					gi.AddEvent(P_MASK(player), EV_ACTOR_ADD);
					gi.WriteShort(ent->number);
					gi.WriteByte(ent->team);
					gi.WriteByte(ent->chr.teamDefIndex);
					gi.WriteByte(ent->chr.gender);
					gi.WriteByte(ent->pnum);
					gi.WriteGPos(ent->pos);
					gi.WriteShort(ent->state & STATE_PUBLIC);
					gi.WriteByte(ent->fieldSize);
				}
			}
		}
	}
}

/**
 * @brief
 * @sa G_ClientSpawn
 * @sa G_CheckVisTeam
 * @sa G_AppearPerishEvent
 */
static int G_CheckVisPlayer (player_t* player, qboolean perish)
{
	int vis, i;
	int status = 0;
	edict_t* ent;

	/* check visibility */
	for (i = 0, ent = g_edicts; i < globals.num_edicts; i++, ent++)
		if (ent->inuse) {
			/* check if he's visible */
			vis = G_TestVis(player->pers.team, ent, perish);

			if (vis & VIS_CHANGE) {
				ent->visflags ^= (1 << player->pers.team);
				G_AppearPerishEvent(P_MASK(player), vis & VIS_YES, ent);

				if (vis & VIS_YES) {
					status |= VIS_APPEAR;
					if ((ent->type == ET_ACTOR || ent->type == ET_ACTOR2x2)
					&& !(ent->state & STATE_DEAD) && ent->team != TEAM_CIVILIAN)
						status |= VIS_STOP;
				} else
					status |= VIS_PERISH;
			}
		}

	return status;
}

/**
 * @brief Checks whether an edict appear/perishes for a specific team - also
 * updates the visflags each edict carries
 * @sa G_TestVis
 * @param[in] team Team to check the vis for
 * @param[in] check The edict that you wanna check (and which maybe will appear
 * or perish for the given team).
 * If check is a NULL pointer - all edicts in g_edicts are checked
 * @param[in] perish Also check whether the edict (the actor) is going to become
 * invisible for the given team
 * @return If an actor get visible who's no civilian VIS_STOP is added to the
 * bit mask, VIS_YES means, he is visible, VIS_CHANGE means that the actor
 * flipped its visibility (invisible to visible or vice versa), VIS_PERISH means
 * that the actor (the edict) is getting invisible for the queried team
 * @note the edict may not only be actors, but also items of course
 * @sa G_TeamToPM
 * @sa G_TestVis
 * @sa G_AppearPerishEvent
 * @sa G_CheckVisPlayer
 */
int G_CheckVisTeam (int team, edict_t * check, qboolean perish)
{
	int vis, i, end;
	int status = 0;

	/* decide whether to check all entities or a specific one */
	if (!check) {
		check = g_edicts;
		end = globals.num_edicts;
	} else
		end = 1;

	/* check visibility */
	for (i = 0; i < end; i++, check++)
		if (check->inuse) {
			/* check if he's visible */
			vis = G_TestVis(team, check, perish);

			if (vis & VIS_CHANGE) {
				check->visflags ^= (1 << team);
				G_AppearPerishEvent(G_TeamToPM(team), vis & VIS_YES, check);

				if (vis & VIS_YES) {
					status |= VIS_APPEAR;
					if ((check->type == ET_ACTOR || check->type == ET_ACTOR2x2)
					 && !(check->state & STATE_DEAD) && check->team != TEAM_CIVILIAN)
						status |= VIS_STOP;
				} else
					status |= VIS_PERISH;
			}
		}

	return status;
}


/**
 * @brief Check if the edict appears/perishes for the other teams
 * @sa G_CheckVisTeam
 * @param[in] perish Also check for perishing events
 * @param[in] check The edict we are talking about
 * @return Bitmask of VIS_* values
 * @sa G_CheckVisTeam
 */
int G_CheckVis (edict_t * check, qboolean perish)
{
	int team;
	int status;

	status = 0;
	for (team = 0; team < MAX_TEAMS; team++)
		if (level.num_alive[team])
			status |= G_CheckVisTeam(team, check, perish);

	return status;
}


/**
 * @brief Reset the visflags for all edict in the global list (g_edicts) for the
 * given team - and only for the given team
 */
void G_ClearVisFlags (int team)
{
	edict_t *ent;
	int i, mask;

	mask = ~(1 << team);
	for (i = 0, ent = g_edicts; i < globals.num_edicts; i++, ent++)
		if (ent->inuse)
			ent->visflags &= mask;
}


/**
 * @brief Turns an actor around
 * @param[in] ent the actor (edict) we are talking about
 * @param[in] toDV the direction to turn the edict into
 * @return Bitmask of visible (VIS_*) values
 * @sa G_CheckVisTeam
 */
static int G_DoTurn (edict_t * ent, byte toDV)
{
	float angleDiv;
	const byte *rot;
	int i, num;
	int status;

	assert(ent->dir < DIRECTIONS);

	toDV &= (DIRECTIONS-1);

	/* return if no rotation needs to be done */
	if ((ent->dir) == (toDV))
		return 0;

	/* calculate angle difference */
	angleDiv = dangle[toDV] - dangle[ent->dir];
	if (angleDiv > 180.0)
		angleDiv -= 360.0;
	if (angleDiv < -180.0)
		angleDiv += 360.0;

	/* prepare rotation */
	if (angleDiv > 0) {
		rot = dvleft;
		num = (+angleDiv + 22.5) / 45.0;
	} else {
		rot = dvright;
		num = (-angleDiv + 22.5) / 45.0;
	}

	/* do rotation and vis checks */
	status = 0;

	/* check every angle in the rotation whether something becomes visible */
	for (i = 0; i < num; i++) {
		ent->dir = rot[ent->dir];
		status |= G_CheckVisTeam(ent->team, NULL, qfalse);
#if 0
		/* stop turning if a new living player (which is not in our team) appears */
		if (stop && (status & VIS_STOP))
			break;
#endif
	}

	return status;
}

/**
 * @brief Returns the current active team to the server
 */
int G_GetActiveTeam (void)
{
	return level.activeTeam;
}

/**
 * @brief Checks whether the the requested action is possible
 * @param[in] player Which player (human player) is trying to do the action
 * @param[in] ent Which of his units is trying to do the action.
 * @param[in] TU The time units to check against the ones ent has.
 * @param[in] quiet Don't print the console message if quiet is true.
 * the action with
 * @todo: Integrate into hud - don't use cprintf
 */
qboolean G_ActionCheck (player_t * player, edict_t * ent, int TU, qboolean quiet)
{
	int msglevel;

	/* a generic tester if an action could be possible */
	if (level.activeTeam != player->pers.team) {
		gi.cprintf(player, PRINT_HUD, _("Can't perform action - this isn't your round!\n"));
		return qfalse;
	}

	msglevel = quiet ? PRINT_NONE : PRINT_HUD;

	if (!ent || !ent->inuse) {
		gi.cprintf(player, msglevel, _("Can't perform action - object not present!\n"));
		return qfalse;
	}

	if (ent->type != ET_ACTOR && ent->type != ET_ACTOR2x2) {
		gi.cprintf(player, msglevel, _("Can't perform action - not an actor!\n"));
		return qfalse;
	}

	if (ent->state & STATE_STUN) {
		gi.cprintf(player, msglevel, _("Can't perform action - actor is stunned!\n"));
		return qfalse;
	}

	if (ent->state & STATE_DEAD) {
		gi.cprintf(player, msglevel, _("Can't perform action - actor is dead!\n"));
		return qfalse;
	}

	if (ent->team != player->pers.team) {
		gi.cprintf(player, msglevel, _("Can't perform action - not on same team!\n"));
		return qfalse;
	}

	if (ent->pnum != player->num) {
		gi.cprintf(player, msglevel, _("Can't perform action - no control over allied actors!\n"));
		return qfalse;
	}

	if (TU > ent->TU) {
		return qfalse;
	}

	/* could be possible */
	return qtrue;
}


/**
 * @brief Spawns a new entity at the floor
 * @note This is e.g. used to place dropped weapons/items at the floor
 */
edict_t *G_SpawnFloor (pos3_t pos)
{
	edict_t *floor;

	floor = G_Spawn();
	floor->classname = "item";
	floor->type = ET_ITEM;
	floor->fieldSize = ACTOR_SIZE_NORMAL;
	VectorCopy(pos, floor->pos);
	floor->pos[2] = gi.GridFall(gi.map, floor->pos, floor->fieldSize);
	gi.GridPosToVec(gi.map, floor->pos, floor->origin);
	return floor;
}

/**
 * @sa G_GetFloorItems
 */
static edict_t *G_GetFloorItemsFromPos (pos3_t pos)
{
	edict_t *floor;

	for (floor = g_edicts; floor < &g_edicts[globals.num_edicts]; floor++) {
		if (!floor->inuse || floor->type != ET_ITEM)
			continue;
		if (!VectorCompare(pos, floor->pos))
			continue;

		return floor;
	}
	/* nothing found at this pos */
	return NULL;
}

/**
 * @brief Prepares a list of items on the floor at given entity position.
 * @param[in] ent Pointer to an entity being an actor.
 * @return pointer to edict_t being a floor (with items).
 * @note This function is somehow broken - it returns NULL in some cases of items on the floor.
 */
static edict_t *G_GetFloorItems (edict_t * ent)
{
	edict_t *floor = G_GetFloorItemsFromPos(ent->pos);
	/* found items */
	if (floor) {
		FLOOR(ent) = FLOOR(floor);
		return floor;
	}

	/* no items on ground found */
	FLOOR(ent) = NULL;
	return NULL;
}

#if 0
/**
 * @brief Debug functions that prints the content of a floor (container) at a given position in the map.
 * @param[in] Position of the floor in the map.
 */
static void G_PrintFloorToConsole (pos3_t pos)
{
	edict_t *floor = G_GetFloorItemsFromPos(pos);
	Com_DPrintf(DEBUG_GAME, "G_PrintFloorToConsole: Printing containers from floor at %i,%i,%i.\n", pos[0], pos[1], pos[2]);
	if (floor) {
		INVSH_PrintContainerToConsole(&floor->i);
	} else {
		Com_DPrintf(DEBUG_GAME, "G_PrintFloorToConsole: No Floor items found.\n");
	}
}
#endif

/**
 * @brief Moves an item inside an inventory. Floors are handled special.
 * @input[in] player The player the edict/soldier belongs to.
 * @param[in] num The edict number of the selected/used edict/soldier.
 * @param[in] from The container (-id) the item should be moved from.
 * @param[in] fx x position of the item you want to move in the source container
 * @param[in] fy y position of the item you want to move in the source container
 * @param[in] to The container (-id) the item should be moved to.
 * @param[in] tx x position where you want the item to go in the destination container
 * @param[in] ty y position where you want the item to go in the destination container
 * @param[in] checkaction Set this to qtrue if you want to check for TUs, otherwise qfalse.
 * @param[in] quiet Set this to qfalse to prevent message-flooding.
 * @sa event PA_INVMOVE
 * @sa AI_ActorThink
 */
void G_ClientInvMove (player_t * player, int num, int from, int fx, int fy, int to, int tx, int ty, qboolean checkaction, qboolean quiet)
{
	edict_t *ent, *floor;
	invList_t *ic;
	qboolean newFloor;
	item_t item;
	int mask;
	inventory_action_t ia;
	int msglevel;
	const invList_t* invList;

	ent = g_edicts + num;
	msglevel = quiet ? PRINT_NONE : PRINT_CONSOLE;

	/* store the location of 'from' BEFORE actually moving items with Com_MoveInInventory */
	invList = Com_SearchInInventory(&ent->i, from, fx, fy);

	/* check if action is possible */
	if (checkaction && !G_ActionCheck(player, ent, 1, quiet))
		return;

	/* "get floor ready" - searching for existing floor-edict*/
	floor = G_GetFloorItems(ent); /* Also sets FLOOR(ent) to correct value. */
	if (to == gi.csi->idFloor && !floor) {
		/* We are moving to the floor, but no existing edict for this floor-tile found -> create new one */
		floor = G_SpawnFloor(ent->pos);
		newFloor = qtrue;
	} else if (from == gi.csi->idFloor && !floor) {
		/* We are moving from the floor, but no existing edict for this floor-tile found -> thi should never be the case. */
		Com_Printf("G_ClientInvMove: No source-floor found.\n");
		return;
	} else {
		/* There already exists an edict for this floor-tile. */
		newFloor = qfalse;
	}

	/* search for space */
	if (tx == NONE) {
/*		assert(ty == NONE); */
		if (ty != NONE)
			Com_Printf("G_ClientInvMove: Error: ty != NONE, it is %i.\n", ty);

		ic = Com_SearchInInventory(&ent->i, from, fx, fy);
		if (ic)
			Com_FindSpace(&ent->i, &ic->item, to, &tx, &ty);
	}
	if (tx == NONE) {
/*		assert(ty == NONE); */
		if (ty != NONE)
			Com_Printf("G_ClientInvMove: Error: ty != NONE, it is %i.\n", ty);
		return;
	}

	/* Try to actually move the item and check the return value */
	ia = Com_MoveInInventory(&ent->i, from, fx, fy, to, tx, ty, &ent->TU, &ic);
	switch (ia) {
	case IA_NONE:
		/* No action possible - abort */
		return;
	case IA_NOTIME:
		gi.cprintf(player, msglevel, _("Can't perform action - not enough TUs!\n"));
		return;
	case IA_NORELOAD:
		gi.cprintf(player, msglevel, _("Can't perform action - weapon already fully loaded with the same ammunition!\n")); /* @todo: "or not researched" */
		return;
	default:
		/* Continue below. */
		break;
	}

	/* FIXME: This is impossible - if not we should check MAX_INVDEFS*/
	assert((gi.csi->idFloor >= 0) && (gi.csi->idFloor < MAX_CONTAINERS));

	/* successful inventory change; remove the item in clients */
	if (from == gi.csi->idFloor) {
		/* We removed an item from a floor - check how the client needs to be updated. */
		assert(!newFloor);
		if (FLOOR(ent)) {
			/* There is still something on the floor. */
			FLOOR(floor) = FLOOR(ent); /* @todo: _why_ do we do this here exactly? Shouldn't they be the same already at this point? */
			/* Tell the client to remove the item from the container */
			gi.AddEvent(G_VisToPM(floor->visflags), EV_INV_DEL);
			gi.WriteShort(floor->number);
			gi.WriteByte(from);
			gi.WriteByte(fx);
			gi.WriteByte(fy);
		} else {
			/* Floor is empty, remove the edict (from server+client) if we are not moving to it. */
			if (to != gi.csi->idFloor) {
				gi.AddEvent(G_VisToPM(floor->visflags), EV_ENT_PERISH);
				gi.WriteShort(floor->number);
				G_FreeEdict(floor);
			}
		}
	} else {
		/* Tell the client to remove the item from the container */
		gi.AddEvent(G_TeamToPM(ent->team), EV_INV_DEL);
		gi.WriteShort(num);
		gi.WriteByte(from);
		gi.WriteByte(fx);
		gi.WriteByte(fy);
	}

	/* send tu's */
	G_SendStats(ent);

	item = ic->item;

	if (ia == IA_RELOAD || ia == IA_RELOAD_SWAP) {
		/* reload */
		if (to == gi.csi->idFloor) {
			assert(!newFloor);
			assert(FLOOR(floor) == FLOOR(ent));
			mask = G_VisToPM(floor->visflags);
		} else {
			mask = G_TeamToPM(ent->team);
		}

		/* send ammo message to all --- it's fun to hear that sound */
		gi.AddEvent(PM_ALL, EV_INV_RELOAD);
		/* is this condition below needed? or is 'num' enough?
		 * probably needed so that red rifle on the floor changes color,
		 * but this needs testing. */
		gi.WriteShort(to == gi.csi->idFloor ? floor->number : num);
		gi.WriteByte(gi.csi->ods[item.t].ammo);
		gi.WriteByte(item.m);
		gi.WriteByte(to);
		gi.WriteByte(ic->x);
		gi.WriteByte(ic->y);

		if (ia == IA_RELOAD) {
			gi.EndEvents();
			return;
		} else { /* ia == IA_RELOAD_SWAP */
			if (!invList) {
				Com_Printf("G_ClientInvMove()... Didn't found invList of the item you're moving\n");
				gi.EndEvents();
				return;
			}
			item = invList->item;
			to = from;
			tx = fx;
			ty = fy;
		}
	}

	/* add it */
	if (to == gi.csi->idFloor) {
		/* We moved an item to the floor - check how the client needs to be updated. */
		if (newFloor) {
			/* A new container was created for the floor. */
			assert(FLOOR(ent));
			FLOOR(floor) = FLOOR(ent); /* @todo: _why_ do we do this here exactly? Shouldn't they be the same already at this point? */
			/* Send item info to the clients */
			G_CheckVis(floor, qtrue);
		} else {
			/* Add the item; update floor, because we add at beginning */
			FLOOR(floor) = FLOOR(ent); /* @todo: _why_ do we do this here exactly? Shouldn't they be the same already at this point? */
			/* Tell the client to add the item to the container. */
			gi.AddEvent(G_VisToPM(floor->visflags), EV_INV_ADD);
			gi.WriteShort(floor->number);
			gi.WriteShort(INV_INVENTORY_BYTES);
			G_WriteItem(item, to, tx, ty);
		}
	} else {
		/* Tell the client to add the item to the container. */
		gi.AddEvent(G_TeamToPM(ent->team), EV_INV_ADD);
		gi.WriteShort(num);
		gi.WriteShort(INV_INVENTORY_BYTES);
		G_WriteItem(item, to, tx, ty);
	}

	/* Update reaction firemode when something is moved from/to a hand. */
	if ((from == gi.csi->idRight) || (to == gi.csi->idRight)) {
		Com_DPrintf(DEBUG_GAME, "G_ClientInvMove: Something moved in/out of Right hand.\n");
		gi.AddEvent(G_TeamToPM(ent->team), EV_INV_HANDS_CHANGED);
		gi.WriteShort(num);
		gi.WriteShort(0);	/**< hand=right */
	} else if ((from == gi.csi->idLeft) || (to == gi.csi->idLeft)) {
		Com_DPrintf(DEBUG_GAME, "G_ClientInvMove:  Something moved in/out of Left hand.\n");
		gi.AddEvent(G_TeamToPM(ent->team), EV_INV_HANDS_CHANGED);
		gi.WriteShort(num);
		gi.WriteShort(1);	/**< hand=left */
	}

	/* Other players receive weapon info only. */
	mask = G_VisToPM(ent->visflags) & ~G_TeamToPM(ent->team);
	if (mask) {
		if (from == gi.csi->idRight || from == gi.csi->idLeft) {
			gi.AddEvent(mask, EV_INV_DEL);
			gi.WriteShort(num);
			gi.WriteByte(from);
			gi.WriteByte(fx);
			gi.WriteByte(fy);
		}
		if (to == gi.csi->idRight || to == gi.csi->idLeft) {
			gi.AddEvent(mask, EV_INV_ADD);
			gi.WriteShort(num);
			gi.WriteShort(INV_INVENTORY_BYTES);
			G_WriteItem(item, to, tx, ty);
		}
	}
	gi.EndEvents();
}


/*#define ADJACENT*/
/**
 * @brief Move the whole given inventory to the floor and destroy the items that do not fit there.
 * @param[in] ent Pointer to an edict_t being an actor.
 * @sa G_ActorDie
 */
static void G_InventoryToFloor (edict_t * ent)
{
	invList_t *ic, *next;
	int k;
	edict_t *floor;
#ifdef ADJACENT
	edict_t *floorAdjacent = NULL;
	int i;
#endif

	/* check for items */
	for (k = 0; k < gi.csi->numIDs; k++)
		if (ent->i.c[k])
			break;

	/* edict is not carrying any items */
	if (k >= gi.csi->numIDs)
		return;

	/* find the floor */
	floor = G_GetFloorItems(ent);
	if (!floor) {
		floor = G_SpawnFloor(ent->pos);
	} else {
		/* destroy this edict (send this event to all clients that see the edict) */
		gi.AddEvent(G_VisToPM(floor->visflags), EV_ENT_PERISH);
		gi.WriteShort(floor->number);
		floor->visflags = 0;
	}

	/* drop items */
	/* cycle through all containers */
	for (k = 0; k < gi.csi->numIDs; k++) {
		/* skip floor - we want to drop to floor */
		if (k == gi.csi->idFloor)
			continue;
		/* skip csi->idArmour, we will collect armours using idArmour container,
		   not idFloor */
		if (k == gi.csi->idArmour) {
			if (ent->i.c[gi.csi->idArmour])
				Com_DPrintf(DEBUG_GAME, "G_InventoryToFloor()... this actor has armour: %s\n", gi.csi->ods[ent->i.c[gi.csi->idArmour]->item.t].name);
			continue;
		}
		/* now cycle through all items for the container of the character (or the entity) */
		for (ic = ent->i.c[k]; ic; ic = next) {
			int x, y;
#ifdef ADJACENT
			vec2_t oldPos; /**< if we have to place it to adjacent  */
#endif
			/* Save the next inv-list before it gets overwritten below.
			   Do not put this in the "for" statement,
			   unless you want an endless loop. ;) */
			next = ic->next;
			/* find the coordinates for the current item on floor */
			Com_FindSpace(&floor->i, &ic->item, gi.csi->idFloor, &x, &y);
			if (x == NONE) {
				assert(y == NONE);
				/* Run out of space on the floor or the item is armour
				   destroy the offending item if no adjacent places are free */
				/* store pos for later restoring the original value */
#ifdef ADJACENT
				Vector2Copy(ent->pos, oldPos);
				for (i = 0; i < DIRECTIONS; i++) {
					/* @todo: Check whether movement is possible here - otherwise don't use this field */
					/* extend pos with the direction vectors */
					Vector2Set(ent->pos, ent->pos[0] + dvecs[i][0], ent->pos[0] + dvecs[i][1]);
					/* now try to get a floor entity for that new location */
					floorAdjacent = G_GetFloorItems(ent);
					if (!floorAdjacent) {
						floorAdjacent = G_SpawnFloor(ent->pos);
					} else {
						/* destroy this edict (send this event to all clients that see the edict) */
						gi.AddEvent(G_VisToPM(floorAdjacent->visflags), EV_ENT_PERISH);
						gi.WriteShort(floorAdjacent->number);
						floorAdjacent->visflags = 0;
					}
					Com_FindSpace(&floorAdjacent->i, &ic->item, gi.csi->idFloor, &x, &y);
					if (x != NONE) {
						ic->x = x;
						ic->y = y;
						ic->next = FLOOR(floorAdjacent);
						FLOOR(floorAdjacent) = ic;
						break;
					}
					/* restore original pos */
					Vector2Copy(oldPos, ent->pos);
				}
				/* added to adjacent pos? */
				if (i < DIRECTIONS) {
					/* restore original pos - if no free space, this was done
					 * already in the for loop */
					Vector2Copy(oldPos, ent->pos);
					continue;
				}
#endif
				if (Q_strncmp(gi.csi->ods[ic->item.t].type, "armour", MAX_VAR))
					gi.dprintf("G_InventoryToFloor: Warning: could not drop item to floor: %s\n", gi.csi->ods[ic->item.t].id);
				if (!Com_RemoveFromInventory(&ent->i, k, ic->x, ic->y))
					gi.dprintf("G_InventoryToFloor: Error: could not remove item: %s\n", gi.csi->ods[ic->item.t].id);
			} else {
				ic->x = x;
				ic->y = y;
				ic->next = FLOOR(floor);
				FLOOR(floor) = ic;
#ifdef PARANOID
				Com_DPrintf(DEBUG_GAME, "G_InventoryToFloor: item to floor: %s\n", gi.csi->ods[ic->item.t].id);
#endif
			}
		}

		/* destroy link */
		ent->i.c[k] = NULL;
	}

	FLOOR(ent) = FLOOR(floor);

	if (ent->i.c[gi.csi->idArmour])
		Com_DPrintf(DEBUG_GAME, "At the end of G_InventoryToFloor()... this actor has armour in idArmour container: %s\n", gi.csi->ods[ent->i.c[gi.csi->idArmour]->item.t].name);
	else
		Com_DPrintf(DEBUG_GAME, "At the end of G_InventoryToFloor()... this actor has NOT armour in idArmour container\n");

	/* send item info to the clients */
	G_CheckVis(floor, qtrue);
#ifdef ADJACENT
	if (floorAdjacent)
		G_CheckVis(floorAdjacent, qtrue);
#endif
}


pos_t *fb_list[MAX_FORBIDDENLIST];
int fb_length;

/**
 * @brief Build the forbidden list for the pathfinding (server side).
 * @param[in] team The team number if the list should be calculated from the eyes of that team. Use 0 to ignore team.
 * @note The forbidden list (fb_list and fb_length) is a
 * list of entity positions that are occupied by an entity.
 * This list is checked everytime an actor wants to walk there.
 * @sa G_MoveCalc
 * @sa Grid_CheckForbidden
 * @sa CL_BuildForbiddenList <- shares quite some code
 * @note This is used for pathfinding.
 * It is a list of where the selected unit can not move to because others are standing there already.
 */
static void G_BuildForbiddenList (int team)
{
	edict_t *ent = NULL;
	int vis_mask;
	int i;

	fb_length = 0;

	/* team visibility */
	if (team)
		vis_mask = 1 << team;
	else
		vis_mask = 0xFFFFFFFF;

	for (i = 0, ent = g_edicts; i < globals.num_edicts; i++, ent++) {
		if (!ent->inuse)
			continue;
		/* Dead 2x2 unit will stop walking, too. */
		/**
		 * @todo Just a note for the future.
		 * If we get any  that does not block the map when dead this si the place to look.
		 */
		if (((ent->type == ET_ACTOR && !(ent->state & STATE_DEAD)) || ent->type == ET_ACTOR2x2) && (ent->visflags & vis_mask)) {
			fb_list[fb_length++] = ent->pos;
			fb_list[fb_length++] = (byte*)&ent->fieldSize;
		}
	}

	if (fb_length > MAX_FORBIDDENLIST)
		gi.error("G_BuildForbiddenList: list too long\n");
}

/**
 * @brief @todo: writeme
 * @param[in] team The current team (see G_BuildForbiddenList)
 * @param[in] from Position in the map to start the move-calculation from.
 * @param[in] distance The distance to calculate the move for.
 * @sa G_BuildForbiddenList
 */
void G_MoveCalc (int team, pos3_t from, int size, int distance)
{
	G_BuildForbiddenList(team);
	gi.MoveCalc(gi.map, from, size, distance, fb_list, fb_length);
}


/**
 * @brief Check whether there is already an edict on the field where the actor
 * is trying to get with the next move
 * @param[in] dv Direction to walk to. See the "dvecs" array and DIRECTIONS for more info.
 * @param[in] from starting point to walk from
 * @return qtrue if there is something blocking this direction.
 * @return qfalse if moving into this direction is ok.
 *
 * @todo Add support for 2x2 units. See Grid_CheckForbidden for an example on how to do that.
 * @todo But maybe this check isn't even needed anymore? Shouldn't Grid_CheckForbidden have prevented this?
 */
static qboolean G_CheckMoveBlock (pos3_t from, int dv)
{
	edict_t *ent = NULL;
	pos3_t pos;
	int i;

	/* Get target position. */
	VectorCopy(from, pos);

	/* Calculate the field in the given direction. */
	PosAddDV(pos, dv);

	/* Search for blocker. */
	/** @todo 2x2 support needed? */
	for (i = 0, ent = g_edicts; i < globals.num_edicts; i++, ent++)
		if (ent->inuse && (ent->type == ET_ACTOR || ent->type == ET_ACTOR2x2) && !(ent->state & STATE_DEAD) && VectorCompare(pos, ent->pos))
			return qtrue;

	/* Nothing blocked - moving to this direction should be ok. */
	return qfalse;
}

/**
 * @brief @todo: writeme
 * @param[in] player Player who is moving an actor
 * @param[in] visTeam
 * @param[in] num Edict index to move
 * @param[in] to
 * @param[in] stop
 * @param[in] quiet Don't print the console message (G_ActionCheck) if quiet is true.
 * @sa CL_ActorStartMove
 * @todo 2x2 units?
 * @sa PA_MOVE
 */
void G_ClientMove (player_t * player, int visTeam, int num, pos3_t to, qboolean stop, qboolean quiet)
{
	edict_t *ent;
	int status, initTU;
	byte dvtab[MAX_DVTAB];
	byte dv, numdv, length, steps;
	pos3_t pos;
	float div, tu;
	int contentFlags;
	vec3_t pointTrace;
	char* stepAmount = NULL;

	ent = g_edicts + num;

	/* check if action is possible */
	if (!G_ActionCheck(player, ent, 2, quiet))
		return;

	/* calculate move table */
	G_MoveCalc(visTeam, ent->pos, ent->fieldSize, MAX_ROUTE);
	length = gi.MoveLength(gi.map, to, qfalse);

	/* length of ROUTING_NOT_REACHABLE means not reachable */
	if (length && length < ROUTING_NOT_REACHABLE) {
		/* start move */
		gi.AddEvent(G_TeamToPM(ent->team), EV_ACTOR_START_MOVE);
		gi.WriteShort(ent->number);
		ent->think = G_PhysicsStep;
		ent->nextthink = level.time;
		ent->speed = (ent->state & STATE_CROUCHED) ? 50 : 100;

		/* assemble dv-encoded move data */
		VectorCopy(to, pos);
		numdv = 0;
		tu = 0;
		initTU = ent->TU;

		while ((dv = gi.MoveNext(gi.map, pos)) < ROUTING_NOT_REACHABLE) {
			/* store the inverted dv */
			/* (invert by flipping the first bit and add the old height) */
			assert(numdv < MAX_DVTAB);
			dvtab[numdv++] = ((dv ^ 1) & (DIRECTIONS - 1)) | (pos[2] << 3);
			PosAddDV(pos, dv);
		}

		if (VectorCompare(pos, ent->pos)) {
			/* everything ok, found valid route */
			/* create movement related events */
			steps = 0;
			sentAppearPerishEvent = qfalse;

			FLOOR(ent) = NULL;

			/* BEWARE: do not print anything (even with DPrintf)
			 * in functions called in this loop
			 * without setting 'steps = 0' afterwards;
			 * also do not send events except G_AppearPerishEvent
			 * without manually setting 'steps = 0' */
			while (numdv > 0) {
				/* get next dv */
				numdv--;

				/* turn around first */
				status = G_DoTurn(ent, dvtab[numdv]);
				if (status) {
					/* send the turn */
					gi.AddEvent(G_VisToPM(ent->visflags), EV_ACTOR_TURN);
					gi.WriteShort(ent->number);
					gi.WriteByte(ent->dir);
				}
				if (stop && (status & VIS_STOP))
					break;
				if (status || sentAppearPerishEvent) {
					steps = 0;
					sentAppearPerishEvent = qfalse;
				}

				/* check for "blockers" */
				if (G_CheckMoveBlock(ent->pos, dvtab[numdv]))
					break;

				/* decrease TUs */
				div = ((dvtab[numdv] & (DIRECTIONS - 1)) < 4) ? TU_MOVE_STRAIGHT : TU_MOVE_DIAGONAL;
				if (ent->state & STATE_CROUCHED)
					div *= 1.5;
				if ((int) (tu + div) > ent->TU)
					break;
				tu += div;

				/* move */
				PosAddDV(ent->pos, dvtab[numdv]);
				gi.GridPosToVec(gi.map, ent->pos, ent->origin);
				VectorCopy(ent->origin, pointTrace);
				pointTrace[2] += PLAYER_MIN;

				contentFlags = gi.PointContents(pointTrace);

				/* link it at new position */
				gi.linkentity(ent);

				/* write move header if not yet done */
				if (!steps) {
					gi.AddEvent(G_VisToPM(ent->visflags), EV_ACTOR_MOVE);
					gi.WriteShort(num);
					stepAmount = gi.WriteDummyByte(0);
				}

				assert(stepAmount);

				if (ent->moveinfo.steps >= MAX_DVTAB) {
					ent->moveinfo.steps = 0;
					ent->moveinfo.currentStep = 0;
				}
				ent->moveinfo.contentFlags[ent->moveinfo.steps] = contentFlags;
				ent->moveinfo.visflags[ent->moveinfo.steps] = ent->visflags;
				ent->moveinfo.steps++;

				/* we are using another steps here, because the steps
				 * in the moveinfo maybe are not 0 again */
				steps++;
				/* store steps in netchannel */
				*stepAmount = steps;

				/* write move header and always one step after another - because the next step
				 * might already be the last one due to some stop event */
				gi.WriteByte(dvtab[numdv]);
				gi.WriteShort(contentFlags);

				/* check if player appears/perishes, seen from other teams */
				G_CheckVis(ent, qtrue);

				/* check for anything appearing, seen by "the moving one" */
				status = G_CheckVisTeam(ent->team, NULL, qfalse);

				/* Set ent->TU because the reaction code relies on ent->TU being accurate. */
				ent->TU = max(0, initTU - (int) tu);

				/* check for reaction fire */
				if (G_ReactToMove(ent, qtrue)) {
					if (G_ReactToMove(ent, qfalse))
						status |= VIS_STOP;
					/* @todo Let the camera center on the attacker if he's visible
					 * if he's invisible let the target turn in the shooting direction
					 * of the attacker (G_Turn) - also let all team members now
					 * about the situation. Otherwise they may move (e.g. in a
					 * coop game) and trigger the reaction fire which may kill
					 * the targetted actor
					 */
					steps = 0;
					sentAppearPerishEvent = qfalse;
				}

				/* Restore ent->TU because the movement code relies on it not being modified! */
				ent->TU = initTU;

				/* check for death */
				if (ent->state & STATE_DEAD)
					return;

				if (stop && (status & VIS_STOP))
					break;

				if (sentAppearPerishEvent) {
					steps = 0;
					sentAppearPerishEvent = qfalse;
				}
			}

			/* submit the TUs / round down */
			ent->TU = max(0, initTU - (int) tu);
			G_SendStats(ent);

			/* end the move */
			G_GetFloorItems(ent);
			gi.EndEvents();
		}
	}
}


/**
 * @brief Sends the actual actor turn event over the netchannel
 */
static void G_ClientTurn (player_t * player, int num, int dv)
{
	edict_t *ent;

	ent = g_edicts + num;

	/* check if action is possible */
	if (!G_ActionCheck(player, ent, TU_TURN, NOISY))
		return;

	/* check if we're already facing that direction */
	if (ent->dir == dv)
		return;

	/* start move */
	gi.AddEvent(G_TeamToPM(ent->team), EV_ACTOR_START_MOVE);
	gi.WriteShort(ent->number);

	/* do the turn */
	G_DoTurn(ent, (byte) dv);
	ent->TU -= TU_TURN;

	/* send the turn */
	gi.AddEvent(G_VisToPM(ent->visflags), EV_ACTOR_TURN);
	gi.WriteShort(ent->number);
	gi.WriteByte(ent->dir);

	/* send the new TUs */
	G_SendStats(ent);

	/* end the event */
	gi.EndEvents();
}


/**
 * @brief Changes the thate of a player/soldier.
 * @param[in,out] player The player who controlls the actor
 * @param[in] num The index of the edict in the global g_edicts array
 * @param[in] reqState The bit-map of the requested state change
 * @param[in] checkaction only activate the events - network stuff is handled in the calling function
 * don't even use the G_ActionCheck function
 * @note Use checkaction true only for e.g. spawning values
 */
static void G_ClientStateChange (player_t * player, int num, int reqState, qboolean checkaction)
{
	edict_t *ent;

	ent = g_edicts + num;

	/* Check if any action is possible. */
	if (checkaction && !G_ActionCheck(player, ent, 0, NOISY))
		return;

	if (!reqState)
		return;

	switch (reqState) {
	case STATE_CROUCHED: /* toggle between crouch/stand */
		/* Check if player has enough TUs (TU_CROUCH TUs for crouch/uncrouch). */
		if (!checkaction || G_ActionCheck(player, ent, TU_CROUCH, NOISY)) {
			ent->state ^= STATE_CROUCHED;
			ent->TU -= TU_CROUCH;
			/* Link it. */
			if (ent->state & STATE_CROUCHED)
				VectorSet(ent->maxs, PLAYER_WIDTH, PLAYER_WIDTH, PLAYER_CROUCH);
			else
				VectorSet(ent->maxs, PLAYER_WIDTH, PLAYER_WIDTH, PLAYER_STAND);
			gi.linkentity(ent);
		}
		break;
	case ~STATE_REACTION: /* request to turn off reaction fire */
		if ((ent->state & STATE_REACTION_MANY) || (ent->state & STATE_REACTION_ONCE)) {
			if (ent->state & STATE_SHAKEN)
				gi.cprintf(player, PRINT_CONSOLE, _("Currently shaken, won't let their guard down.\n"));
			else {
				/* Turn off reaction fire and give the soldier back his TUs if it used some. */
				ent->state &= ~STATE_REACTION;

				if (reactionTUs[ent->number][REACT_TUS] > 0) {
					/* TUs where used for activation. */
					ent->TU += reactionTUs[ent->number][0];
				} else if (reactionTUs[ent->number][REACT_TUS] < 0) {
					/* No TUs where used for activation. */
					/* Don't give TUs back because none where used up (reaction fire was already active from previous turn) */
				} else {
					/* reactionTUs[ent->number][REACT_TUS] == 0) */
					/* This should never be the case.  */
					Com_DPrintf(DEBUG_GAME, "G_ClientStateChange: 0 value saved for reaction while reaction is activated.\n");
				}
			}
		}
		break;
	case STATE_REACTION_MANY: /* request to turn on multi-reaction fire mode */
		/* We can assume that the previous mode is STATE_REACTION_ONCE here.
		 * If the gui behaviour ever changes we need to check for the previosu state in a better way here.
		 */
		if ((ent->TU >= (TU_REACTION_MULTI - reactionTUs[ent->number][REACT_TUS]))
			&& (ent->state & STATE_REACTION_ONCE)) {
			/* We have enough TUs for switching to multi-rf and the previous state is STATE_REACTION_ONCE */

			/* Disable single reaction fire and remove saved TUs. */
 			ent->state &= ~STATE_REACTION;
			if (reactionTUs[ent->number][REACT_TUS] > 0) {
				ent->TU += reactionTUs[ent->number][REACT_TUS];
				reactionTUs[ent->number][REACT_TUS] = 0;
			}

			/* enable multi reaction fire */
 			ent->state |= STATE_REACTION_MANY;
			ent->TU -= TU_REACTION_MULTI;
			reactionTUs[ent->number][REACT_TUS] = TU_REACTION_MULTI;
		} else {
			/* @todo: this is a workaround for now.
			the multi-rf button was clicked (i.e. single-rf is activated right now), but there are not enough TUs left for it.
			so the only sane action if the button is clicked is to disable everything in order to make it work at all.
			@todo: dsiplay correct "disable" button and directly call this function with "disable rf" parameters
			*/
			G_ClientStateChange(player, num, ~STATE_REACTION, qtrue); /**< Turn off RF */
		}
		break;
	case STATE_REACTION_ONCE: /* request to turn on single-reaction fire mode */
		if (!checkaction || G_ActionCheck(player, ent, TU_REACTION_SINGLE, NOISY)) {
			/* Turn on reaction fire and save the used TUs to the list. */
			ent->state |= STATE_REACTION_ONCE;

#if 0 /* @todo: do we really want to enable this? i don't think we can get soemthing useable out of it .. the whole "save TUs until nex round" needs a cleanup. */
			if (reactionTUs[ent->number][REACT_TUS] > 0) {
				/* TUs where saved for this turn (either the full TU_REACTION or some remaining TUs from the shot. This was done either in the last turn or this one. */
				ent->TU -= reactionTUs[ent->number][REACT_TUS];
			} else

			if (reactionTUs[ent->number][REACT_TUS] == 0) {
#endif
				/* Reaction fire was not triggered in the last turn. */
				ent->TU -= TU_REACTION_SINGLE;
				reactionTUs[ent->number][REACT_TUS] = TU_REACTION_SINGLE;
#if 0	/* @todo: Same here */
			}  else {
				/* reactionTUs[ent->number][REACT_TUS] < 0 */
				/* Reaction fire was triggered in the last turn,
				   and has used 0 TU from this one.
				   Can be activated without TU-loss. */
			}
#endif
		}
		break;
	default:
		Com_Printf("G_ClientStateChange: unknown request %i, ignoring\n", reqState);
		return;
	}

	/* only activate the events - network stuff is handled in the calling function */
	if (!checkaction)
		return;

	/* Send the state change. */
	G_SendState(G_VisToPM(ent->visflags), ent);

	/* Check if the player appears/perishes, seen from other teams. */
	G_CheckVis(ent, qtrue);

	/* Calc new vis for this player. */
	G_CheckVisTeam(ent->team, NULL, qfalse);

	/* Send the new TUs. */
	G_SendStats(ent);

	/* End the event. */
	gi.EndEvents();
}

/**
 * @sa G_MoraleStopPanic
 * @sa G_MoraleRage
 * @sa G_MoraleStopRage
 * @sa G_MoraleBehaviour
 */
static void G_MoralePanic (edict_t * ent, qboolean sanity, qboolean quiet)
{
	gi.cprintf(game.players + ent->pnum, PRINT_CONSOLE, _("%s panics!\n"), ent->chr.name);

	/* drop items in hands */
	if (!sanity && ent->chr.weapons) {
		if (RIGHT(ent))
			G_ClientInvMove(game.players + ent->pnum, ent->number, gi.csi->idRight, 0, 0, gi.csi->idFloor, NONE, NONE, qtrue, quiet);
		if (LEFT(ent))
			G_ClientInvMove(game.players + ent->pnum, ent->number, gi.csi->idLeft, 0, 0, gi.csi->idFloor, NONE, NONE, qtrue, quiet);
	}

	/* get up */
	ent->state &= ~STATE_CROUCHED;
	VectorSet(ent->maxs, PLAYER_WIDTH, PLAYER_WIDTH, PLAYER_STAND);

	/* send panic */
	ent->state |= STATE_PANIC;
	G_SendState(G_VisToPM(ent->visflags), ent);

	/* center view */
	gi.AddEvent(G_VisToPM(ent->visflags), EV_CENTERVIEW);
	gi.WriteGPos(ent->pos);

	/* move around a bit, try to avoid opponents */
	AI_ActorThink(game.players + ent->pnum, ent);

	/* kill TUs */
	ent->TU = 0;
}

/**
 * @brief Stops the panic state of an actor
 * @note This is only called when cvar mor_panic is not zero
 * @sa G_MoralePanic
 * @sa G_MoraleRage
 * @sa G_MoraleStopRage
 * @sa G_MoraleBehaviour
 */
static void G_MoraleStopPanic (edict_t * ent, qboolean quiet)
{
	if (((ent->morale) / mor_panic->value) > (m_panic_stop->value * frand()))
		ent->state &= ~STATE_PANIC;
	else
		G_MoralePanic(ent, qtrue, quiet);
}

/**
 * @sa G_MoralePanic
 * @sa G_MoraleStopPanic
 * @sa G_MoraleStopRage
 * @sa G_MoraleBehaviour
 */
static void G_MoraleRage (edict_t * ent, qboolean sanity)
{
	if (sanity)
		ent->state |= STATE_RAGE;
	else
		ent->state |= STATE_INSANE;
	G_SendState(G_VisToPM(ent->visflags), ent);

	if (sanity)
		gi.bprintf(PRINT_CONSOLE, _("%s is on a rampage.\n"), ent->chr.name);
	else
		gi.bprintf(PRINT_CONSOLE, _("%s is consumed by mad rage!\n"), ent->chr.name);
	AI_ActorThink(game.players + ent->pnum, ent);
}

/**
 * @brief Stops the rage state of an actor
 * @note This is only called when cvar mor_panic is not zero
 * @sa G_MoralePanic
 * @sa G_MoraleRage
 * @sa G_MoraleStopPanic
 * @sa G_MoraleBehaviour
 */
static void G_MoraleStopRage (edict_t * ent, qboolean quiet)
{
	if (((ent->morale) / mor_panic->value) > (m_rage_stop->value * frand())) {
		ent->state &= ~STATE_INSANE;
		G_SendState(G_VisToPM(ent->visflags), ent);
	} else
		G_MoralePanic(ent, qtrue, quiet);	/*regains sanity */
}

/**
 * @brief Applies morale behaviour on actors
 * @note only called when mor_panic is not zero
 * @sa G_MoralePanic
 * @sa G_MoraleRage
 * @sa G_MoraleStopRage
 * @sa G_MoraleStopPanic
 */
static void G_MoraleBehaviour (int team, qboolean quiet)
{
	edict_t *ent;
	int i, newMorale;
	qboolean sanity;

	for (i = 0, ent = g_edicts; i < globals.num_edicts; i++, ent++)
		/* this only applies to ET_ACTOR but not to ET_ACTOR2x2 */
		if (ent->inuse && ent->type == ET_ACTOR && ent->team == team && !(ent->state & STATE_DEAD)) {
			/* civilians have a 1:1 chance to randomly run away in multiplayer */
			if (sv_maxclients->integer >= 2 && level.activeTeam == TEAM_CIVILIAN && 0.5 > frand())
				G_MoralePanic(ent, qfalse, quiet);
			/* multiplayer needs enabled sv_enablemorale */
			/* singleplayer has this in every case */
			if ((sv_maxclients->integer >= 2 && sv_enablemorale->integer == 1)
				|| sv_maxclients->integer == 1) {
				/* if panic, determine what kind of panic happens: */
				if (ent->morale <= mor_panic->value && !(ent->state & STATE_PANIC) && !(ent->state & STATE_RAGE)) {
					if ((float) ent->morale / mor_panic->value > (m_sanity->value * frand()))
						sanity = qtrue;
					else
						sanity = qfalse;
					if ((float) ent->morale / mor_panic->value > (m_rage->value * frand()))
						G_MoralePanic(ent, sanity, quiet);
					else
						G_MoraleRage(ent, sanity);
					/* if shaken, well .. be shaken; */
				} else if (ent->morale <= mor_shaken->value && !(ent->state & STATE_PANIC) && !(ent->state & STATE_RAGE)) {
					ent->TU -= TU_REACTION_SINGLE; /* @todo: Comment-me: Why do we modify reaction stuff here? */
					/* shaken is later reset along with reaction fire */
					ent->state |= STATE_SHAKEN | STATE_REACTION_MANY;
					G_SendState(G_VisToPM(ent->visflags), ent);
					gi.cprintf(game.players + ent->pnum, PRINT_CONSOLE, _("%s is currently shaken.\n"), ent->chr.name);
				} else {
					if (ent->state & STATE_PANIC)
						G_MoraleStopPanic(ent, quiet);
					else if (ent->state & STATE_RAGE)
						G_MoraleStopRage(ent, quiet);
				}
			}
			/* set correct bounding box */
			if (ent->state & (STATE_CROUCHED | STATE_PANIC))
				VectorSet(ent->maxs, PLAYER_WIDTH, PLAYER_WIDTH, PLAYER_CROUCH);
			else
				VectorSet(ent->maxs, PLAYER_WIDTH, PLAYER_WIDTH, PLAYER_STAND);

			/* moraleregeneration, capped at max: */
			newMorale = ent->morale + MORALE_RANDOM(mor_regeneration->value);
			if (newMorale > GET_MORALE(ent->chr.skills[ABILITY_MIND]))
				ent->morale = GET_MORALE(ent->chr.skills[ABILITY_MIND]);
			else
				ent->morale = newMorale;

			/* send phys data and state: */
			G_SendStats(ent);
			gi.EndEvents();
		}
}


/**
 * @brief Reload weapon with actor.
 * @param[in] hand
 * @sa AI_ActorThink
 */
void G_ClientReload (player_t *player, int entnum, shoot_types_t st, qboolean quiet)
{
	edict_t *ent;
	invList_t *ic;
	int hand;
	int weapon, x, y, tu;
	int container, bestContainer;

	ent = g_edicts + entnum;

	/* search for clips and select the one that is available easily */
	x = 0;
	y = 0;
	tu = 100;
	bestContainer = NONE;
	hand = st == ST_RIGHT_RELOAD ? gi.csi->idRight : gi.csi->idLeft;

	if (ent->i.c[hand]) {
		weapon = ent->i.c[hand]->item.t;
	} else if (hand == gi.csi->idLeft
			&& gi.csi->ods[ent->i.c[gi.csi->idRight]->item.t].holdTwoHanded) {
		/* Check for two-handed weapon */
		hand = gi.csi->idRight;
		weapon = ent->i.c[hand]->item.t;
	}
	else
		return;

	assert(weapon != NONE);

	/* LordHavoc: Check if item is researched when in singleplayer? I don't think this is really a
	 * cheat issue as in singleplayer there is no way to inject fake client commands in the virtual
	 * network buffer, and in multiplayer everything is researched */

	for (container = 0; container < gi.csi->numIDs; container++) {
		if (gi.csi->ids[container].out < tu) {
			/* Once we've found at least one clip, there's no point
			 * searching other containers if it would take longer
			 * to retrieve the ammo from them than the one
			 * we've already found. */
			for (ic = ent->i.c[container]; ic; ic = ic->next)
				if (INVSH_LoadableInWeapon(&gi.csi->ods[ic->item.t], weapon)) {
					x = ic->x;
					y = ic->y;
					tu = gi.csi->ids[container].out;
					bestContainer = container;
					break;
				}
		}
	}

	/* send request */
	if (bestContainer != NONE)
		G_ClientInvMove(player, entnum, bestContainer, x, y, hand, 0, 0, qtrue, quiet);
}

/**
 * @brief Returns true if actor can reload weapon
 * @param[in] hand
 * @sa AI_ActorThink
 */
qboolean G_ClientCanReload (player_t *player, int entnum, shoot_types_t st)
{
	edict_t *ent;
	invList_t *ic;
	int hand, weapon;
	int container;

	ent = g_edicts + entnum;

	/* search for clips and select the one that is available easily */
	hand = st == ST_RIGHT_RELOAD ? gi.csi->idRight : gi.csi->idLeft;

	if (ent->i.c[hand]) {
		weapon = ent->i.c[hand]->item.t;
	} else if (hand == gi.csi->idLeft
	 && gi.csi->ods[ent->i.c[gi.csi->idRight]->item.t].holdTwoHanded) {
		/* Check for two-handed weapon */
		hand = gi.csi->idRight;
		weapon = ent->i.c[hand]->item.t;
	} else
		return qfalse;

	assert(weapon != NONE);

	for (container = 0; container < gi.csi->numIDs; container++)
		for (ic = ent->i.c[container]; ic; ic = ic->next)
			if (INVSH_LoadableInWeapon(&gi.csi->ods[ic->item.t], weapon))
				return qtrue;
	return qfalse;
}

/**
 * @brief Retrieve or collect weapon from any linked container for the actor
 * @note This function will also collect items from floor containers when the actor
 * is standing on a given point.
 * @sa AI_ActorThink
 */
void G_ClientGetWeaponFromInventory (player_t *player, int entnum, qboolean quiet)
{
	edict_t *ent;
	invList_t *ic;
	int hand;
	int x, y, tu;
	int container, bestContainer;

	ent = g_edicts + entnum;
	/* e.g. bloodspiders are not allowed to carry or collect weapons */
	if (!ent->chr.weapons)
		return;

	/* search for weapons and select the one that is available easily */
	x = 0;
	y = 0;
	tu = 100;
	bestContainer = NONE;
	hand = gi.csi->idRight;

	for (container = 0; container < gi.csi->numIDs; container++) {
		if (gi.csi->ids[container].out < tu) {
			/* Once we've found at least one clip, there's no point
			 * searching other containers if it would take longer
			 * to retrieve the ammo from them than the one
			 * we've already found. */
			for (ic = ent->i.c[container]; ic; ic = ic->next) {
				assert(ic->item.t != NONE);
				if (gi.csi->ods[ic->item.t].weapon && (ic->item.a > 0 || !gi.csi->ods[ic->item.t].reload)) {
					x = ic->x;
					y = ic->y;
					tu = gi.csi->ids[container].out;
					bestContainer = container;
					break;
				}
			}
		}
	}

	/* send request */
	if (bestContainer != NONE)
		G_ClientInvMove(player, entnum, bestContainer, x, y, hand, 0, 0, qtrue, quiet);
}

/**
 * @brief Report and handle death of an actor
 */
void G_ActorDie (edict_t * ent, int state, edict_t *attacker)
{
	assert(ent);

	Com_DPrintf(DEBUG_GAME, "G_ActorDie: kill actor on team %i\n", ent->team);
	switch (state) {
	case STATE_DEAD:
		ent->state |= (1 + rand() % MAX_DEATH);
		break;
	case STATE_STUN:
		ent->STUN = 0;
		ent->state = state;
		break;
	default:
		gi.dprintf("G_ActorDie: unknown state %i\n", state);
		break;
	}
	VectorSet(ent->maxs, PLAYER_WIDTH, PLAYER_WIDTH, PLAYER_DEAD);
	gi.linkentity(ent);
	level.num_alive[ent->team]--;
	/* send death */
	gi.AddEvent(G_VisToPM(ent->visflags), EV_ACTOR_DIE);
	gi.WriteShort(ent->number);
	gi.WriteShort(ent->state);

	/* handle inventory - drop everything to floor edict (but not the armour) if actor can handle equipment */
	if (ent->chr.weapons)
		G_InventoryToFloor(ent);

	/* check if the player appears/perishes, seen from other teams */
	G_CheckVis(ent, qtrue);

	/* check if the attacker appears/perishes, seen from other teams */
	if (attacker)
		G_CheckVis(attacker, qtrue);

	/* calc new vis for this player */
	G_CheckVisTeam(ent->team, NULL, qfalse);
}

#ifdef DEBUG
/**
 * @brief This function does not add statistical values. Because there is no attacker.
 * The same counts for morale states - they are not affected.
 * @note: This is a debug function to let a hole team die
 */
void G_KillTeam (void)
{
	/* default is to kill all teams */
	int teamToKill = -1, i;
	edict_t *ent;

	/* with a parameter we will be able to kill a specific team */
	if (gi.argc() == 2)
		teamToKill = atoi(gi.argv(1));

	Com_DPrintf(DEBUG_GAME, "G_KillTeam: kill team %i\n", teamToKill);

	for (i = 0, ent = g_edicts; i < globals.num_edicts; i++, ent++)
		if (ent->inuse && (ent->type == ET_ACTOR || ent->type == ET_ACTOR2x2) && !(ent->state & STATE_DEAD)) {
			if (teamToKill >= 0 && ent->team != teamToKill)
				continue;

			/* die */
			G_ActorDie(ent, STATE_DEAD, NULL);
		}

	/* check for win conditions */
	G_CheckEndGame();
}
#endif

/**
 * @brief The client sent us a message that he did something. We now execute the related fucntion(s) adn notify him if neccessary.
 * @param[in] player The player that sent us the message (@todo: is this correct?)
 */
int G_ClientAction (player_t * player)
{
	int action;
	int num;
	pos3_t pos;
	int i;
	int firemode;
	int from, fx, fy, to, tx, ty;
	int hand, fd_idx;

	/* read the header */
	action = gi.ReadByte();
	num = gi.ReadShort();
	switch (action) {
	case PA_NULL:
		/* do nothing on a null action */
		break;

	case PA_TURN:
		gi.ReadFormat(pa_format[PA_TURN], &i);
		G_ClientTurn(player, num, i);
		break;

	case PA_MOVE:
		gi.ReadFormat(pa_format[PA_MOVE], &pos);
		G_ClientMove(player, player->pers.team, num, pos, qtrue, NOISY);
		break;

	case PA_STATE:
		gi.ReadFormat(pa_format[PA_STATE], &i);
		G_ClientStateChange(player, num, i, qtrue);
		break;

	case PA_SHOOT:
		gi.ReadFormat(pa_format[PA_SHOOT], &pos, &i, &firemode, &from);
		(void)G_ClientShoot(player, num, pos, i, firemode, NULL, qtrue, from);
		break;

	case PA_INVMOVE:
		gi.ReadFormat(pa_format[PA_INVMOVE], &from, &fx, &fy, &to, &tx, &ty);
		G_ClientInvMove(player, num, from, fx, fy, to, tx, ty, qtrue, NOISY);
		break;

	case PA_REACT_SELECT:
		hand = -1;
		fd_idx = -1;
		gi.ReadFormat(pa_format[PA_REACT_SELECT], &hand, &fd_idx);
		Com_DPrintf(DEBUG_GAME, "G_ClientAction: entnum:%i hand:%i fd:%i\n", num, hand, fd_idx);
		/* @todo: Add check for correct player here (player==g_edicts[num]->team ???) */
		reactionFiremode[num][RF_HAND] = hand;
		reactionFiremode[num][RF_FM] = fd_idx;
		break;

	default:
		gi.error("G_ClientAction: Unknown action!\n");
		break;
	}
	return action;
}


/**
 * @brief Sets the teamnum var for this match
 * @param[in] player Pointer to connected player
 * @todo: Check whether there are enough free spawnpoints in all cases
 */
static void G_GetTeam (player_t * player)
{
	player_t *p;
	int i, j;
	int playersInGame = 0;

	/* number of currently connected players (no ai players) */
	for (j = 0, p = game.players; j < game.sv_maxplayersperteam; j++, p++)
		if (p->inuse)
			playersInGame++;

	/* player has already a team */
	if (player->pers.team) {
		gi.dprintf("You are already on team %i\n", player->pers.team);
		return;
	}

	/* randomly assign a teamnumber in deathmatch games */
	if (playersInGame <= 1 && sv_maxclients->integer > 1 && !sv_teamplay->integer) {
		int spawnCheck[MAX_TEAMS];
		int spawnSpots = 0;
		int randomSpot = -1;
		/* skip civilian teams */
		for (i = TEAM_PHALANX; i < MAX_TEAMS; i++) {
			spawnCheck[i] = 0;
			/* check whether there are spawnpoints for this team */
			if (level.num_spawnpoints[i])
				spawnCheck[spawnSpots++] = i;
		}
		/* we need at least 2 different team spawnpoints for multiplayer */
		if (spawnSpots <= 1) {
			gi.dprintf("G_GetTeam: Not enough spawn spots in map!\n");
			player->pers.team = -1;
			return;
		}
		/* assign random valid team number */
		randomSpot = (int)(frand() * (spawnSpots - 1) + 0.5);
		player->pers.team = spawnCheck[randomSpot];
		gi.dprintf("You have been randomly assigned to team %i\n", player->pers.team);
		return;
	}

	/* find a team */
	if (sv_maxclients->integer == 1)
		player->pers.team = TEAM_PHALANX;
	else if (sv_teamplay->integer) {
		/* set the team specified in the userinfo */
		gi.dprintf("Get a team for teamplay for %s\n", player->pers.netname);
		i = atoi(Info_ValueForKey(player->pers.userinfo, "teamnum"));
		/* civilians are at team zero */
		if (i > 0 && sv_maxteams->integer >= i) {
			player->pers.team = i;
			gi.bprintf(PRINT_CHAT, "serverconsole: %s has chosen team %i\n", player->pers.netname, i);
		} else {
			gi.dprintf("Team %i is not valid - choose a team between 1 and %i\n", i, sv_maxteams->integer);
			player->pers.team = DEFAULT_TEAMNUM;
		}
	} else {
		qboolean teamAvailable;
		/* search team */
		gi.dprintf("Getting a multiplayer team for %s\n", player->pers.netname);
		for (i = 1; i < MAX_TEAMS; i++) {
			if (level.num_spawnpoints[i]) {
				teamAvailable = qtrue;
				/* check if team is in use (only human controlled players) */
				/* FIXME: If someone left the game and rejoins he should get his "old" team back */
				/*        maybe we could identify such a situation */
				for (j = 0, p = game.players; j < game.sv_maxplayersperteam; j++, p++)
					if (p->inuse && p->pers.team == i) {
						Com_DPrintf(DEBUG_GAME, "Team %i is already in use\n", i);
						/* team already in use */
						teamAvailable = qfalse;
						break;
					}
				if (teamAvailable)
					break;
			}
		}

		/* set the team */
		if (i < MAX_TEAMS) {
			/* remove ai player */
			for (j = 0, p = game.players + game.sv_maxplayersperteam; j < game.sv_maxplayersperteam; j++, p++)
				if (p->inuse && p->pers.team == i) {
					gi.bprintf(PRINT_CONSOLE, "Removing ai player...");
					p->inuse = qfalse;
					break;
				}
			Com_DPrintf(DEBUG_GAME, "Assigning %s to Team %i\n", player->pers.netname, i);
			player->pers.team = i;
		} else {
			Com_Printf("No free team - disconnecting '%s'\n", player->pers.netname);
			G_ClientDisconnect(player);
		}
	}
}

/**
 * @brief Returns the assigned team number of the player
 */
int G_ClientGetTeamNum (player_t * player)
{
	assert(player);
	return player->pers.team;
}

/**
 * @brief Returns the preferred team number for the player
 */
int G_ClientGetTeamNumPref (player_t * player)
{
	assert(player);
	return atoi(Info_ValueForKey(player->pers.userinfo, "teamnum"));
}

/**
 * @brief Tests if all teams are connected for a multiplayer match and if so,
 * randomly assigns the first turn to a team.
 * @sa SVCmd_StartGame_f
 */
static void G_ClientTeamAssign (player_t * player)
{
	int i, j, teamCount = 1;
	int playerCount = 0;
	int knownTeams[MAX_TEAMS];
	player_t *p;
	knownTeams[0] = player->pers.team;

	/* return with no action if activeTeam already assigned or if in single-player mode */
	if (level.activeTeam != -1 || sv_maxclients->integer == 1)
		return;

	/* count number of currently connected unique teams and players (human controlled players only) */
	for (i = 0, p = game.players; i < game.sv_maxplayersperteam; i++, p++) {
		if (p->inuse && p->pers.team > 0) {
			playerCount++;
			for (j = 0; j < teamCount; j++) {
				if (p->pers.team == knownTeams[j])
					break;
			}
			if (j == teamCount)
				knownTeams[teamCount++] = p->pers.team;
		}
	}

	Com_DPrintf(DEBUG_GAME, "G_ClientTeamAssign: Players in game: %i, Unique teams in game: %i\n", playerCount, teamCount);

	/* if all teams/players have joined the game, randomly assign which team gets the first turn */
	if ((sv_teamplay->integer && teamCount >= sv_maxteams->integer) || playerCount >= sv_maxclients->integer) {
		char buffer[MAX_VAR] = "";
		G_PrintStats(va("Starting new game: %s", level.mapname));
		level.activeTeam = knownTeams[(int)(frand() * (teamCount - 1) + 0.5)];
		turnTeam = level.activeTeam;
		for (i = 0, p = game.players; i < game.sv_maxplayersperteam; i++, p++) {
			if (p->inuse) {
				if (p->pers.team == level.activeTeam) {
					Q_strcat(buffer, p->pers.netname, sizeof(buffer));
					Q_strcat(buffer, " ", sizeof(buffer));
				} else
					/* all the others are set to waiting */
					p->ready = qtrue;
				if (p->pers.team)
					G_PrintStats(va("Team %i: %s", p->pers.team, p->pers.netname));
			}
		}
		G_PrintStats(va("Team %i got the first round", turnTeam));
		gi.bprintf(PRINT_CONSOLE, _("Team %i (%s) will get the first turn.\n"), turnTeam, buffer);
	}
}

/**
 * @brief Find valid actor spawn fields for this player.
 * @note Already used spawn-point are not found because ent->type is changed in G_ClientTeamInfo.
 * @param[in] player The player to spawn the actors for.
 * @param[in] type The type of spawn-point so search for (ET_ACTORSPAWN or ET_ACTOR2x2SPAWN)
 * @return A pointer to a found spawn point or NULL if nothing was found or on error.
 */
static edict_t *G_ClientGetFreeSpawnPoint (player_t * player, int spawnType)
{
	int i;
	edict_t *ent;

	/* Abort for non-spawnpoints */
	assert(spawnType == ET_ACTORSPAWN || spawnType == ET_ACTOR2x2SPAWN);

	for (i = 0, ent = g_edicts; i < globals.num_edicts; i++, ent++)
		if ((ent->type == spawnType)
		 && (player->pers.team == ent->team))
			return ent;

	return NULL;
}

/**
 * @brief Call this if you want to skip some actor netchannel data
 * @note The fieldsize is not skipped
 * @sa G_ClientTeamInfo
 */
static inline void G_ClientSkipActorInfo (void)
{
	int k, j;

	gi.ReadShort(); /* ucn */
	for (k = 0; k < 4; k++)
		gi.ReadString(); /* name, path, body, head */
	gi.ReadByte(); /* skin */
	gi.ReadShort(); /* HP */
	gi.ReadShort(); /* maxHP */
	gi.ReadByte(); /* teamDefIndex */
	gi.ReadByte(); /* gender */
	gi.ReadByte(); /* STUN */
	gi.ReadByte(); /* morale */
	for (k = 0; k < SKILL_NUM_TYPES; k++)
		gi.ReadByte(); /* skills */
	for (k = 0; k < KILLED_NUM_TYPES; k++)
		gi.ReadShort(); /* kills */
	gi.ReadShort(); /* assigned missions */
	j = gi.ReadShort();
	/* @todo: skip j bytes instead of reading and ignoring */
	for (k = 0; k < j; k++)
		gi.ReadByte(); /* inventory */
}

/**
 * @brief The client lets the server spawn the actors for a given player by sending their information (models, inventory, etc..) over the network.
 * @param[in] player The player to spawn the actors for.
 * @sa CL_SendCurTeamInfo
 * @sa globals.ClientTeamInfo
 * @sa clc_teaminfo
 */
void G_ClientTeamInfo (player_t * player)
{
	edict_t *ent;
	int i, k, length;
	int container, x, y;
	item_t item;
	int dummyFieldSize = 0;

	/* find a team */
	G_GetTeam(player);

	length = gi.ReadByte();	/* Get the length of data that the clients sent. */

	for (i = 0; i < length; i++) {
		/* Search for a spawn point for each entry the client sent
		 * Don't allow spawning of soldiers if:
		 * + the player is not in a valid team
		 * +++ Multiplayer
		 * + the game is already running (activeTeam != -1)
		 * + the sv_maxsoldiersperplayer limit is hit (e.g. the assembled team is bigger than the allowed number of soldiers)
		 * + the team already hit the max allowed amount of soldiers
		 */
		if (player->pers.team != -1
		 && (sv_maxclients->integer == 1
			|| (level.activeTeam == -1 && i < sv_maxsoldiersperplayer->integer
			 && level.num_spawned[player->pers.team] < sv_maxsoldiersperteam->integer))) {
			/* Here the client tells the server the information for the spawned actor(s). */

			/* Receive fieldsize and get matching spawnpoint. */
			dummyFieldSize = gi.ReadByte();
			switch (dummyFieldSize) {
			case ACTOR_SIZE_NORMAL:
				/* Find valid actor spawn fields for this player. */
				ent = G_ClientGetFreeSpawnPoint(player, ET_ACTORSPAWN);
				if (!ent) {
					gi.dprintf("G_ClientTeamInfo: Could not spawn actor because no useable spawn-point is avaialble (%i)\n", dummyFieldSize);
					G_ClientSkipActorInfo();
					continue;
				}
				ent->type = ET_ACTOR;
				break;
			case ACTOR_SIZE_2x2:
				/* Find valid actor spawn fields for this player. */
				ent = G_ClientGetFreeSpawnPoint(player, ET_ACTOR2x2SPAWN);
				if (!ent) {
					gi.dprintf("G_ClientTeamInfo: Could not spawn actor because no useable spawn-point is avaialble (%i)\n", dummyFieldSize);
					G_ClientSkipActorInfo();
					continue;
				}
				ent->type = ET_ACTOR2x2;
				ent->morale = 100;
				break;
			default:
				gi.error("G_ClientTeamInfo: unknown fieldSize for edict (%i)\n", dummyFieldSize);
			}

			level.num_alive[ent->team]++;
			level.num_spawned[ent->team]++;
			ent->pnum = player->num;

			ent->chr.fieldSize = dummyFieldSize;
			ent->fieldSize = ent->chr.fieldSize;

			Com_DPrintf(DEBUG_GAME, "Player: %i - team %i - size: %i\n", player->num, ent->team, ent->fieldSize);

			gi.linkentity(ent);

			/* model */
			ent->chr.ucn = gi.ReadShort();
			Q_strncpyz(ent->chr.name, gi.ReadString(), sizeof(ent->chr.name));
			Q_strncpyz(ent->chr.path, gi.ReadString(), sizeof(ent->chr.path));
			Q_strncpyz(ent->chr.body, gi.ReadString(), sizeof(ent->chr.body));
			Q_strncpyz(ent->chr.head, gi.ReadString(), sizeof(ent->chr.head));
			ent->chr.skin = gi.ReadByte();

			Com_DPrintf(DEBUG_GAME, "G_ClientTeamInfo: name: %s, path: %s, body: %s, head: %s, skin: %i\n",
				ent->chr.name, ent->chr.path, ent->chr.body, ent->chr.head, ent->chr.skin);

			ent->chr.HP = gi.ReadShort();
			ent->chr.maxHP = gi.ReadShort();
			ent->chr.teamDefIndex = gi.ReadByte();
			ent->chr.gender = gi.ReadByte();
			ent->chr.STUN = gi.ReadByte();
			ent->chr.morale = gi.ReadByte();

			/* new attributes */
			for (k = 0; k < SKILL_NUM_TYPES; k++)
				ent->chr.skills[k] = gi.ReadByte();

			/* scores */
			for (k = 0; k < KILLED_NUM_TYPES; k++)
				ent->chr.kills[k] = gi.ReadShort();
			ent->chr.assigned_missions = gi.ReadShort();

			ent->chr.chrscore.alienskilled = gi.ReadByte();
			ent->chr.chrscore.aliensstunned = gi.ReadByte();
			ent->chr.chrscore.civilianskilled = gi.ReadByte();
			ent->chr.chrscore.civiliansstunned = gi.ReadByte();
			ent->chr.chrscore.teamkilled = gi.ReadByte();
			ent->chr.chrscore.teamstunned = gi.ReadByte();
			ent->chr.chrscore.closekills = gi.ReadByte();
			ent->chr.chrscore.heavykills = gi.ReadByte();
			ent->chr.chrscore.assaultkills = gi.ReadByte();
			ent->chr.chrscore.sniperkills = gi.ReadByte();
			ent->chr.chrscore.explosivekills = gi.ReadByte();
			ent->chr.chrscore.accuracystat = gi.ReadByte();
			ent->chr.chrscore.powerstat = gi.ReadByte();

			/* inventory */
			{
				int nr = gi.ReadShort() / INV_INVENTORY_BYTES;

				for (; nr-- > 0;) {
					G_ReadItem(&item, &container, &x, &y);
					Com_DPrintf(DEBUG_GAME, "G_ClientTeamInfo: t=%i:a=%i:m=%i (x=%i:y=%i)\n", item.t, item.a, item.m, x, y);

					Com_AddToInventory(&ent->i, item, container, x, y, 1);
					Com_DPrintf(DEBUG_GAME, "G_ClientTeamInfo: add %s to inventory (container %i - idArmour: %i)\n", gi.csi->ods[ent->i.c[container]->item.t].id, container, gi.csi->idArmour);
				}
			}

			/* set models */
			ent->chr.inv = &ent->i;
			ent->body = gi.modelindex(CHRSH_CharGetBody(&ent->chr));
			ent->head = gi.modelindex(CHRSH_CharGetHead(&ent->chr));
			ent->skin = ent->chr.skin;

			/* set initial vital statistics */
			ent->HP = ent->chr.HP;
			ent->morale = ent->chr.morale;

			/* FIXME: for now, heal fully upon entering mission */
			ent->morale = GET_MORALE(ent->chr.skills[ABILITY_MIND]);

			ent->reaction_minhit = 30; /* @todo: allow later changes from GUI */
		} else {
			/* just do nothing with the info */
			gi.ReadByte(); /* fieldSize */
			G_ClientSkipActorInfo();
		}
	}
	G_ClientTeamAssign(player);
}

/**
 * @brief Counts the still living actors for a player
 */
static int G_PlayerSoldiersCount (player_t* player)
{
	int i, cnt = 0;
	edict_t *ent;

	for (i = 0, ent = g_edicts; i < globals.num_edicts; i++, ent++)
		if (ent->inuse && !(ent->state & STATE_DEAD) && (ent->type == ET_ACTOR || ent->type == ET_ACTOR2x2) && ent->pnum == player->num)
			cnt++;

	return cnt;
}

/**
 * @brief Check whether a forced round end should be executed
 */
void G_ForceEndRound (void)
{
	player_t *p;
	int i, diff;

	/* check for roundlimits in multiplayer only */
	if (!sv_roundtimelimit->integer || sv_maxclients->integer == 1)
		return;

	if (level.time != ceil(level.time))
		return;

	diff = level.roundstartTime + sv_roundtimelimit->integer - level.time;
	switch (diff) {
	case 240:
		gi.bprintf(PRINT_HUD, _("4 minutes left until forced round end\n"));
		return;
	case 180:
		gi.bprintf(PRINT_HUD, _("3 minutes left until forced round end\n"));
		return;
	case 120:
		gi.bprintf(PRINT_HUD, _("2 minutes left until forced round end\n"));
		return;
	case 60:
		gi.bprintf(PRINT_HUD, _("1 minute left until forced round end\n"));
		return;
	case 30:
		gi.bprintf(PRINT_HUD, _("30 seconds left until forced round end\n"));
		return;
	case 15:
		gi.bprintf(PRINT_HUD, _("15 seconds left until forced round end\n"));
		return;
	}

	/* active team still has time left */
	if (level.time < level.roundstartTime + sv_roundtimelimit->integer)
		return;

	gi.bprintf(PRINT_HUD, _("Current active team hit the max round time\n"));

	/* set all team members to ready (only human players) */
	for (i = 0, p = game.players; i < game.sv_maxplayersperteam; i++, p++)
		if (p->inuse && p->pers.team == level.activeTeam) {
			G_ClientEndRound(p, NOISY);
			level.nextEndRound = level.framenum;
		}

	level.roundstartTime = level.time;
}

/**
 * @sa G_PlayerSoldiersCount
 */
void G_ClientEndRound (player_t * player, qboolean quiet)
{
	player_t *p;
	qboolean sanity = qfalse;
	int i, lastTeam, nextTeam;

	/* inactive players can't end their inactive round :) */
	if (level.activeTeam != player->pers.team)
		return;

	/* check for "team oszillation" */
	if (level.framenum < level.nextEndRound)
		return;
	level.nextEndRound = level.framenum + 20;

	/* only use this for teamplay matches like coopX or 2on2 and above
	 * also skip this for ai players, this is only called when all ai actors
	 * have finished their 'thinking' */
	if (!player->pers.ai && sv_teamplay->integer) {
		/* check if all team members are ready */
		if (!player->ready) {
			player->ready = qtrue;
			/* don't send this for alien and civilian teams */
			if (player->pers.team != TEAM_CIVILIAN
			 && player->pers.team != TEAM_ALIEN) {
				gi.AddEvent(PM_ALL, EV_ENDROUNDANNOUNCE | EVENT_INSTANTLY);
				gi.WriteByte(player->num);
				gi.WriteByte(player->pers.team);
				gi.EndEvents();
			}
		}
		for (i = 0, p = game.players; i < game.sv_maxplayersperteam * 2; i++, p++)
			if (p->inuse && p->pers.team == level.activeTeam
				&& !p->ready && G_PlayerSoldiersCount(p) > 0)
				return;
	} else {
		player->ready = qtrue;
	}

	/* clear any remaining reaction fire */
	G_ReactToEndTurn();

	/* give his actors their TUs */
	G_GiveTimeUnits(level.activeTeam);

	/* let all the invisible players perish now */
	G_CheckVisTeam(level.activeTeam, NULL, qtrue);

	lastTeam = player->pers.team;
	level.activeTeam = -1;

	p = NULL;
	while (level.activeTeam == -1) {
		/* search next team */
		nextTeam = -1;

		for (i = lastTeam + 1; i != lastTeam; i++) {
			if (i >= MAX_TEAMS) {
				if (!sanity)
					sanity = qtrue;
				else {
					Com_Printf("Not enough spawn positions in this map\n");
					break;
				}
				i = 0;
			}

			if (((level.num_alive[i] || (level.num_spawnpoints[i] && !level.num_spawned[i]))
				 && i != lastTeam)) {
				nextTeam = i;
				break;
			}
		}

		if (nextTeam == -1) {
/*			gi.bprintf(PRINT_CONSOLE, "Can't change round - no living actors left.\n"); */
			level.activeTeam = lastTeam;
			gi.EndEvents();
			return;
		}

		/* search corresponding player (even ai players) */
		for (i = 0, p = game.players; i < game.sv_maxplayersperteam * 2; i++, p++)
			if (p->inuse && p->pers.team == nextTeam) {
				/* found next player */
				level.activeTeam = nextTeam;
				break;
			}

		if (level.activeTeam == -1 && sv_ai->integer && ai_autojoin->integer) {
			/* no corresponding player found - create ai player */
			p = AI_CreatePlayer(nextTeam);
			if (p)
				level.activeTeam = nextTeam;
		}

		lastTeam = nextTeam;
	}
	turnTeam = level.activeTeam;

	/* communicate next player in row to clients */
	gi.AddEvent(PM_ALL, EV_ENDROUND);
	gi.WriteByte(level.activeTeam);

	/* store the round start time to be able to abort the round after a give time */
	level.roundstartTime = level.time;

	/* apply morale behaviour, reset reaction fire */
	G_ResetReactionFire(level.activeTeam);
	if (mor_panic->integer)
		G_MoraleBehaviour(level.activeTeam, quiet);

	/* start ai */
	p->pers.last = NULL;

	/* finish off events */
	gi.EndEvents();

	/* reset ready flag (even ai players) */
	for (i = 0, p = game.players; i < game.sv_maxplayersperteam * 2; i++, p++)
		if (p->inuse && p->pers.team == level.activeTeam)
			p->ready = qfalse;
}

/**
 * @sa CL_EntEdict
 */
static void G_SendVisibleEdicts (int team)
{
	int i;
	edict_t *ent;
	qboolean end = qfalse;

	/* make every edict visible thats not an actor or a 2x2 unit */
	for (i = 0, ent = g_edicts; i < globals.num_edicts; ent++, i++) {
		if (!ent->inuse)
			continue;
		/* don't add actors here */
		if (ent->type == ET_BREAKABLE || ent->type == ET_DOOR) {
			gi.AddEvent(G_TeamToPM(team), EV_ENT_EDICT);
			gi.WriteShort(ent->type);
			gi.WriteShort(ent->number);
			gi.WriteShort(ent->modelindex);
			ent->visflags |= ~ent->visflags;
			end = qtrue;
		}
	}

	if (end)
		gi.EndEvents();
}

/**
 * @brief This functions starts the client
 * @sa G_ClientSpawn
 * @sa CL_StartGame
 */
void G_ClientBegin (player_t* player)
{
	/* this doesn't belong here, but it works */
	if (!level.routed) {
		level.routed = qtrue;
		G_CompleteRecalcRouting();
	}

	/* FIXME: This should be a client side error */
	if (!P_MASK(player)) {
		gi.bprintf(PRINT_CONSOLE, "%s tried to join - but server is full\n", player->pers.netname);
		return;
	}

	player->began = qtrue;

	level.numplayers++;
	gi.configstring(CS_PLAYERCOUNT, va("%i", level.numplayers));

	/*Com_Printf("G_ClientBegin: player: %i - pnum: %i , game.sv_maxplayersperteam: %i	\n", P_MASK(player), player->num, game.sv_maxplayersperteam);*/
	/* spawn camera (starts client rendering) */
	gi.AddEvent(P_MASK(player), EV_START | EVENT_INSTANTLY);
	gi.WriteByte(sv_teamplay->integer);

	/* send events */
	gi.EndEvents();

	/* set the net name */
	gi.configstring(CS_PLAYERNAMES + player->num, player->pers.netname);

	/* inform all clients */
	gi.bprintf(PRINT_CONSOLE, "%s has joined team %i\n", player->pers.netname, player->pers.team);
}

/**
 * @brief Sets the team, init the TU and sends the player stats. Returns
 * true if player spawns.
 * @sa G_SendPlayerStats
 * @sa G_GetTeam
 * @sa G_GiveTimeUnits
 * @sa G_ClientBegin
 * @sa CL_Reset
 */
qboolean G_ClientSpawn (player_t * player)
{
	edict_t *ent;
	int i;

	if (player->spawned) {
		gi.bprintf(PRINT_CONSOLE, "%s already spawned.\n", player->pers.netname);
		G_ClientDisconnect(player);
		return qfalse;
	}

	/* @todo: Check player->pers.team here */
	if (level.activeTeam == -1) {
		/* activate round if in single-player */
		if (sv_maxclients->integer == 1) {
			level.activeTeam = player->pers.team;
			turnTeam = level.activeTeam;
		} else {
			/* return since not all multiplayer teams have joined */
			/* (G_ClientTeamAssign sets level.activeTeam once all teams have joined) */
			return qfalse;
		}
	}

	player->spawned = qtrue;

	/* do all the init events here... */
	/* reset the data */
	gi.AddEvent(P_MASK(player), EV_RESET | EVENT_INSTANTLY);
	gi.WriteByte(player->pers.team);
	gi.WriteByte(level.activeTeam);

	/* show visible actors and add invisible actor */
	G_ClearVisFlags(player->pers.team);
	G_CheckVisPlayer(player, qfalse);
	G_SendInvisible(player);

	/* set initial state to reaction fire activated for the other team */
	if (sv_maxclients->integer > 1 && level.activeTeam != player->pers.team)
		for (i = 0, ent = g_edicts; i < globals.num_edicts; i++, ent++)
			if (ent->inuse && (ent->type == ET_ACTOR || ent->type == ET_ACTOR2x2))
				G_ClientStateChange(player, i, STATE_REACTION_ONCE, qfalse);

	/* submit stats */
	G_SendPlayerStats(player);

	/* send things like doors and breakables */
	G_SendVisibleEdicts(player->pers.team);

	/* give time units */
	G_GiveTimeUnits(player->pers.team);

	/* send events */
	gi.EndEvents();

	if (sv_maxclients->integer > 1 && level.activeTeam != player->pers.team)
		for (i = 0, ent = g_edicts; i < globals.num_edicts; i++, ent++)
			if (ent->inuse && ent->team == player->pers.team && (ent->type == ET_ACTOR || ent->type == ET_ACTOR2x2)) {
				gi.AddEvent(player->pers.team, EV_ACTOR_STATECHANGE);
				gi.WriteShort(ent->number);
				gi.WriteShort(ent->state);
			}

	/* inform all clients */
	gi.bprintf(PRINT_CONSOLE, "%s has taken control over team %i.\n", player->pers.netname, player->pers.team);
	return qtrue;
}


/**
 * @brief called whenever the player updates a userinfo variable.
 * @note The game can override any of the settings in place (forcing skins or names, etc) before copying it off.
 */
void G_ClientUserinfoChanged (player_t * player, char *userinfo)
{
	const char *s;

	/* check for malformed or illegal info strings */
	if (!Info_Validate(userinfo))
		Q_strncpyz(userinfo, "\\name\\badinfo", sizeof(userinfo));

	/* set name */
	s = Info_ValueForKey(userinfo, "name");
	Q_strncpyz(player->pers.netname, s, sizeof(player->pers.netname));

	Q_strncpyz(player->pers.userinfo, userinfo, sizeof(player->pers.userinfo));

	/* send the updated config string */
	gi.configstring(CS_PLAYERNAMES + player->num, player->pers.netname);
}


qboolean G_ClientConnect (player_t * player, char *userinfo)
{
	const char *value;

	/* check to see if they are on the banned IP list */
	value = Info_ValueForKey(userinfo, "ip");
	if (SV_FilterPacket(value)) {
		Info_SetValueForKey(userinfo, "rejmsg", "Banned.");
		return qfalse;
	}

	value = Info_ValueForKey(userinfo, "password");
	if (*password->string && Q_strcmp(password->string, "none")
	 && Q_strcmp(password->string, value)) {
		Info_SetValueForKey(userinfo, "rejmsg", "Password required or incorrect.");
		return qfalse;
	}

	/* fix for fast reconnects after a disconnect */
	if (player->inuse) {
		gi.bprintf(PRINT_CONSOLE, "%s already in use.\n", player->pers.netname);
		G_ClientDisconnect(player);
	}

	/* reset persistant data */
	memset(&player->pers, 0, sizeof(client_persistant_t));
	G_ClientUserinfoChanged(player, userinfo);

	gi.bprintf(PRINT_CHAT, "%s is connecting...\n", player->pers.netname);
	return qtrue;
}

void G_ClientDisconnect (player_t * player)
{
	/* only if the player already sent his began */
	if (player->began) {
		level.numplayers--;
		gi.configstring(CS_PLAYERCOUNT, va("%i", level.numplayers));

		if (level.activeTeam == player->pers.team)
			G_ClientEndRound(player, NOISY);

		/* if no more players are connected - stop the server */
		if (!level.numplayers)
			level.intermissionTime = level.time + 10.0f;
	}

	player->began = qfalse;
	player->spawned = qfalse;
	player->ready = qfalse;

	gi.bprintf(PRINT_CONSOLE, "%s disconnected.\n", player->pers.netname);
}
