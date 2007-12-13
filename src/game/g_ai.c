/**
 * @file g_ai.c
 * @brief Artificial Intelligence.
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

typedef struct {
	pos3_t to;			/**< grid pos to walk to */
	pos3_t stop;		/**< grid pos to stop at (e.g. hiding spots) */
	byte mode;			/**< shoot_types_t */
	byte shots;			/**< how many shoots can this actor do */
	edict_t *target;	/**< the target edict */
} aiAction_t;

/**
 * @brief Check whether friendly units are in the line of fire when shooting
 * @param[in] ent AI that is trying to shoot
 * @param[in] target Shoot to this location
 * @param[in] spread
 */
static qboolean AI_CheckFF (edict_t * ent, vec3_t target, float spread)
{
	edict_t *check;
	vec3_t dtarget, dcheck, back;
	float cosSpread;
	int i;

	/* spread data */
	if (spread < 1.0)
		spread = 1.0;
	spread *= torad;
	cosSpread = cos(spread);
	VectorSubtract(target, ent->origin, dtarget);
	VectorNormalize(dtarget);
	VectorScale(dtarget, PLAYER_WIDTH / spread, back);

	for (i = 0, check = g_edicts; i < globals.num_edicts; i++, check++)
		if (check->inuse && check->type == ET_ACTOR && ent != check && check->team == ent->team && !(check->state & STATE_DEAD)) {
			/* found ally */
			VectorSubtract(check->origin, ent->origin, dcheck);
			if (DotProduct(dtarget, dcheck) > 0.0) {
				/* ally in front of player */
				VectorAdd(dcheck, back, dcheck);
				VectorNormalize(dcheck);
				if (DotProduct(dtarget, dcheck) > cosSpread)
					return qtrue;
			}
		}

	/* no ally in danger */
	return qfalse;
}


#define GUETE_HIDE			60
#define GUETE_CLOSE_IN		20
#define GUETE_KILL			30
#define GUETE_RANDOM		10
#define GUETE_REACTION_ERADICATION 30
#define GUETE_REACTION_FEAR_FACTOR 20
#define GUETE_CIV_FACTOR	0.25

#define AI_ACTION_NOTHING_FOUND -10000.0

#define CLOSE_IN_DIST		1200.0
#define SPREAD_FACTOR		8.0
#define	SPREAD_NORM(x)		(x > 0 ? SPREAD_FACTOR/(x*torad) : 0)
#define HIDE_DIST			7

/**
 * @sa AI_ActorThink
 * @todo fix firedef stuff
 */
static float AI_FighterCalcGuete (edict_t * ent, pos3_t to, aiAction_t * aia)
{
	edict_t *check;
	int move, delta = 0, tu;
	int i, fm, shots, reaction_trap = 0;
	float dist, minDist, nspread;
	float guete, dmg, maxDmg, best_time = -1, vis;
	objDef_t *ad;
	int still_searching = 1;

	int weap_fds_idx; /* Weapon-Firedefs index in fd[x] */
	int fd_idx;	/* firedef index fd[][x]*/

	guete = 0.0;
	memset(aia, 0, sizeof(aiAction_t));
	move = gi.MoveLength(gi.map, to, qtrue);
	tu = ent->TU - move;

	/* test for time */
	if (tu < 0)
		return AI_ACTION_NOTHING_FOUND;

	/* see if we are very well visible by a reacting enemy */
	/* @todo: this is worthless now; need to check all squares along our way! */
	for (i = 0, check = g_edicts; i < globals.num_edicts; i++, check++)
		if (check->inuse && check->type == ET_ACTOR && ent != check
			 && (check->team != ent->team || ent->state & STATE_INSANE)
			 && !(check->state & STATE_DEAD)
			 /* also check if we are in range of the weapon's primary mode */
			 && check->state & STATE_REACTION) {
			qboolean frustum;
			float actorVis;

			actorVis = G_ActorVis(check->origin, ent, qtrue);
			frustum = G_FrustumVis(check, ent->origin);
			if (actorVis > 0.6 && frustum
				&& (VectorDistSqr(check->origin, ent->origin)
					> MAX_SPOT_DIST * MAX_SPOT_DIST))
				reaction_trap++;
		}

	/* don't waste TU's when in reaction fire trap */
	/* learn to escape such traps if move == 2 || move == 3 */
	guete -= move * reaction_trap * GUETE_REACTION_FEAR_FACTOR;

	/* set basic parameters */
	VectorCopy(to, ent->pos);
	VectorCopy(to, aia->to);
	VectorCopy(to, aia->stop);
	gi.GridPosToVec(gi.map, to, ent->origin);

	/* shooting */
	maxDmg = 0.0;
	for (fm = 0; fm < ST_NUM_SHOOT_TYPES; fm++) {
		objDef_t *od = NULL;	/* Ammo pointer */
		int weap_idx = NONE;		/* Weapon index */
		fireDef_t *fd;

		/* optimization: reaction fire is automatic */;
		if (IS_SHOT_REACTION(fm))
			continue;

		if (IS_SHOT_RIGHT(fm) && RIGHT(ent)
			&& RIGHT(ent)->item.m != NONE
			&& gi.csi->ods[RIGHT(ent)->item.t].weapon
			&& (!gi.csi->ods[RIGHT(ent)->item.t].reload
				|| RIGHT(ent)->item.a > 0)) {
			od = &gi.csi->ods[RIGHT(ent)->item.m];
			weap_idx = RIGHT(ent)->item.t;
		} else if (IS_SHOT_LEFT(fm) && LEFT(ent)
			&& (LEFT(ent)->item.m != NONE)
			&& gi.csi->ods[LEFT(ent)->item.t].weapon
			&& (!gi.csi->ods[LEFT(ent)->item.t].reload
				|| LEFT(ent)->item.a > 0)) {
			od = &gi.csi->ods[LEFT(ent)->item.m];
			weap_idx = LEFT(ent)->item.t;
		} else {
			Com_DPrintf(DEBUG_GAME, "AI_FighterCalcGuete: @todo: grenade/knife toss from inventory using empty hand\n");
			/* @todo: grenade/knife toss from inventory using empty hand */
			/* @todo: evaluate possible items to retrieve and pick one, then evaluate an action against the nearby enemies or allies */
		}

		if (!od || weap_idx == NONE)
			continue;

		weap_fds_idx = FIRESH_FiredefsIDXForWeapon(od, weap_idx);
		/* if od was not null and weap_idx was not NONE - then we have a problem here
		 * maybe this is only a maptest and thus no scripts parsed */
		if (weap_fds_idx == -1)
			continue;
		/* FIXME: timed firedefs that bounce around should not be thrown/shooten about the hole distance */
		for (fd_idx = 0; fd_idx < od->numFiredefs[weap_fds_idx]; fd_idx++) {
			fd = &od->fd[weap_fds_idx][fd_idx];

			nspread = SPREAD_NORM((fd->spread[0] + fd->spread[1]) * 0.5 +
				GET_ACC(ent->chr.skills[ABILITY_ACCURACY], fd->weaponSkill));
			/* how many shoots can this actor do */
			shots = tu / fd->time;
			if (shots) {
				/* search best target */
				for (i = 0, check = g_edicts; i < globals.num_edicts; i++, check++)
					if (check->inuse && check->type == ET_ACTOR && ent != check
						&& (check->team != ent->team || ent->state & STATE_INSANE)
						&& !(check->state & STATE_DEAD)) {

						/* don't shoot civilians in mp */
						if (check->team == TEAM_CIVILIAN && sv_maxclients->integer > 1 && !(ent->state & STATE_INSANE))
							continue;

						/* check range */
						dist = VectorDist(ent->origin, check->origin);
						if (dist > fd->range)
							continue;
						/* @todo: Check whether radius and power of fd are to to big for dist */
						/* @todo: Check whether the alien will die when shooting */
						/* don't shoot - we are to close */
						if (dist < fd->splrad)
							continue;

						/* check FF */
						if (AI_CheckFF(ent, check->origin, fd->spread[0]) && !(ent->state & STATE_INSANE))
							continue;

						/* calculate expected damage */
						vis = G_ActorVis(ent->origin, check, qtrue);
						if (vis == 0.0)
							continue;
						dmg = vis * (fd->damage[0] + fd->spldmg[0]) * fd->shots * shots;
						if (nspread && dist > nspread)
							dmg *= nspread / dist;

						/* take into account armour */
						if (check->i.c[gi.csi->idArmour]) {
							ad = &gi.csi->ods[check->i.c[gi.csi->idArmour]->item.t];
							dmg *= 1.0 - ad->protection[ad->dmgtype] * 0.01;
						}

						if (dmg > check->HP
							&& check->state & STATE_REACTION)
							/* reaction shooters eradication bonus */
							dmg = check->HP + GUETE_KILL
								+ GUETE_REACTION_ERADICATION;
						else if (dmg > check->HP)
							/* standard kill bonus */
							dmg = check->HP + GUETE_KILL;

						/* ammo is limited and shooting gives away your position */
						if ((dmg < 25.0 && vis < 0.2) /* too hard to hit */
							|| (dmg < 10.0 && vis < 0.6) /* uber-armour */
							|| dmg < 0.1) /* at point blank hit even with a stick*/
							continue;

						/* civilian malus */
						if (check->team == TEAM_CIVILIAN && !(ent->state & STATE_INSANE))
							dmg *= GUETE_CIV_FACTOR;

						/* add random effects */
						dmg += GUETE_RANDOM * frand();

						/* check if most damage can be done here */
						if (dmg > maxDmg) {
							maxDmg = dmg;
							best_time = fd->time * shots;
							aia->mode = fm;
							aia->shots = shots;
							aia->target = check;
						}
					}
			}
		} /* firedefs */
	}
	/* add damage to guete */
	if (aia->target) {
		guete += maxDmg;
		assert(best_time > 0);
		tu -= best_time;
	}

	if (!(ent->state & STATE_RAGE)) {
		/* hide */
		if (!(G_TestVis(-ent->team, ent, VT_PERISH | VT_NOFRUSTUM) & VIS_YES)) {
			/* is a hiding spot */
			guete += GUETE_HIDE + (aia->target ? GUETE_CLOSE_IN : 0);
		} else if (aia->target && tu >= 2) {
			byte minX, maxX, minY, maxY;
			/* reward short walking to shooting spot, when seen by enemies;
			@todo: do this decently, only penalizing the visible part of walk
			and penalizing much more for reaction shooters around;
			now it may remove some tactical options from aliens,
			e.g. they may now choose only the closer doors;
			however it's still better than going three times around soldier
			and only then firing at him */
			guete += GUETE_CLOSE_IN - move < 0 ? 0 : GUETE_CLOSE_IN - move;

			/* search hiding spot */
			G_MoveCalc(0, to, ent->fieldSize, HIDE_DIST);
			ent->pos[2] = to[2];
			minX = to[0] - HIDE_DIST > 0 ? to[0] - HIDE_DIST : 0;
			minY = to[1] - HIDE_DIST > 0 ? to[1] - HIDE_DIST : 0;
			maxX = to[0] + HIDE_DIST < 254 ? to[0] + HIDE_DIST : 254;
			maxY = to[1] + HIDE_DIST < 254 ? to[1] + HIDE_DIST : 254;

			for (ent->pos[1] = minY; ent->pos[1] <= maxY; ent->pos[1]++) {
				for (ent->pos[0] = minX; ent->pos[0] <= maxX; ent->pos[0]++) {
					/* time */
					delta = gi.MoveLength(gi.map, ent->pos, qfalse);
					if (delta > tu)
						continue;

					/* visibility */
					gi.GridPosToVec(gi.map, ent->pos, ent->origin);
					if (G_TestVis(-ent->team, ent, VT_PERISH | VT_NOFRUSTUM) & VIS_YES)
						continue;

					still_searching = 0;
					break;
				}
				if (!still_searching)
					break;
			}
		}

		if (still_searching) {
			/* nothing found */
			VectorCopy(to, ent->pos);
			gi.GridPosToVec(gi.map, to, ent->origin);
		} else {
			/* found a hiding spot */
			VectorCopy(ent->pos, aia->stop);
			guete += GUETE_HIDE;
			tu -= delta;
			/* @todo: also add bonus for fleeing from reaction fire
			and a huge malus if more than 1 move under reaction */
		}
	}

	/* reward closing in */
	minDist = CLOSE_IN_DIST;
	for (i = 0, check = g_edicts; i < globals.num_edicts; i++, check++)
		if (check->inuse && check->team != ent->team && !(check->state & STATE_DEAD)) {
			dist = VectorDist(ent->origin, check->origin);
			if (dist < minDist)
				minDist = dist;
		}
	guete += GUETE_CLOSE_IN * (1.0 - minDist / CLOSE_IN_DIST);

	return guete;
}


#define GUETE_CIV_RANDOM	30
#define GUETE_RUN_AWAY		50
#define GUETE_CIV_LAZINESS	5
#define RUN_AWAY_DIST		160


/**
 * @sa AI_ActorThink
 * @note Even civilians can use weapons if the teamdef allows this
 */
static float AI_CivilianCalcGuete (edict_t * ent, pos3_t to, aiAction_t * aia)
{
	edict_t *check;
	int i, move, tu;
	float dist, minDist;
	float guete;
	teamDef_t* teamDef;

	/* set basic parameters */
	guete = 0.0;
	memset(aia, 0, sizeof(aiAction_t));
	VectorCopy(to, ent->pos);
	VectorCopy(to, aia->to);
	VectorCopy(to, aia->stop);
	gi.GridPosToVec(gi.map, to, ent->origin);

	move = gi.MoveLength(gi.map, to, qtrue);
	tu = ent->TU - move;

	/* test for time */
	if (tu < 0)
		return AI_ACTION_NOTHING_FOUND;

	/* check whether this civilian can use weapons */
	if (ent->chr.teamDefIndex >= 0) {
		teamDef = &gi.csi->teamDef[ent->chr.teamDefIndex];
		if (ent->state & ~STATE_PANIC && teamDef->weapons)
			return AI_FighterCalcGuete(ent, to, aia);
	} else
		Com_Printf("AI_CivilianCalcGuete: Error - civilian team with no teamdef\n");

	/* run away */
	minDist = RUN_AWAY_DIST;
	for (i = 0, check = g_edicts; i < globals.num_edicts; i++, check++)
		if (check->inuse && check->team != ent->team && !(check->state & STATE_DEAD)) {
			dist = VectorDist(ent->origin, check->origin);
			if (dist < minDist)
				minDist = dist;
		}
	guete += GUETE_RUN_AWAY * minDist / RUN_AWAY_DIST;

	/* add laziness */
	if (ent->TU)
		guete += GUETE_CIV_LAZINESS * tu / ent->TU;

	/* add random effects */
	guete += GUETE_CIV_RANDOM * frand();

	return guete;
}

#define AI_MAX_DIST	30
/**
 * @brief Attempts to find the best action for an alien. Moves the alien
 * into the starting position for that action and returns the action.
 * @param[in] player
 * @param[in] ent
 */
static aiAction_t AI_PrepBestAction (player_t * player, edict_t * ent)
{
	aiAction_t aia, bestAia;
	pos3_t oldPos, to;
	vec3_t oldOrigin;
	edict_t *checkPoint = NULL;
	int xl, yl, xh, yh;
	int i = 0;
	float guete, best;

	/* calculate move table */
	G_MoveCalc(0, ent->pos, ent->fieldSize, MAX_ROUTE);
	gi.MoveStore(gi.map);

	/* set borders */
	xl = (int) ent->pos[0] - AI_MAX_DIST;
	if (xl < 0)
		xl = 0;
	yl = (int) ent->pos[1] - AI_MAX_DIST;
	if (yl < 0)
		yl = 0;
	xh = (int) ent->pos[0] + AI_MAX_DIST;
	if (xh > WIDTH)
		xl = WIDTH;
	yh = (int) ent->pos[1] + AI_MAX_DIST;
	if (yh > WIDTH)
		yh = WIDTH;

	/* search best action */
	best = AI_ACTION_NOTHING_FOUND;
	VectorCopy(ent->pos, oldPos);
	VectorCopy(ent->origin, oldOrigin);

	/* evaluate moving to every possible location in the search area,
	   including combat considerations */
	for (to[2] = 0; to[2] < HEIGHT; to[2]++)
		for (to[1] = yl; to[1] < yh; to[1]++)
			for (to[0] = xl; to[0] < xh; to[0]++)
				if (gi.MoveLength(gi.map, to, qtrue) <= ent->TU) {
					if (ent->team == TEAM_CIVILIAN || ent->state & STATE_PANIC)
						guete = AI_CivilianCalcGuete(ent, to, &aia);
					else
						guete = AI_FighterCalcGuete(ent, to, &aia);

					if (guete > best) {
						bestAia = aia;
						best = guete;
					}
				}

	if (ent->team == TEAM_CIVILIAN) {
		while ((checkPoint = G_FindRadius(checkPoint, ent->origin, 768, ET_CIVILIANTARGET)) != NULL) {
			/* the lower the count value - the nearer the final target */
			if (checkPoint->count < ent->count) {
				i++;
				Com_DPrintf(DEBUG_GAME, "civ found civtarget with %i\n", checkPoint->count);
				/* test for time */
				if (ent->TU - gi.MoveLength(gi.map, checkPoint->pos, qtrue) < 0) {
					Com_DPrintf(DEBUG_GAME, "civtarget too far away (%i)\n", checkPoint->count);
					/* FIXME: Nevertheless walk to that direction */
					continue;
				}

				ent->count = checkPoint->count;
				VectorCopy(checkPoint->pos, bestAia.to);
			}
		}
		/* reset the count value for this civilian to restart the search */
		if (!i)
			ent->count = 100;
	}

	VectorCopy(oldPos, ent->pos);
	VectorCopy(oldOrigin, ent->origin);

	/* nothing found to do */
	if (best == AI_ACTION_NOTHING_FOUND) {
		bestAia.target = NULL;
		return bestAia;
	}

	/* do the move */
	/* FIXME: Why 0 - and not ent->team? */
	G_ClientMove(player, 0, ent->number, bestAia.to, qfalse, QUIET);

#if 0
	Com_DPrintf(DEBUG_GAME, "(%i %i %i) (%i %i %i)\n",
		(int)bestAia.to[0], (int)bestAia.to[1], (int)bestAia.to[2],
		(int)bestAia.stop[0], (int)bestAia.stop[1], (int)bestAia.stop[2]);
#endif
	return bestAia;
}


/**
 * @brief The think function for the ai controlled aliens
 * @param[in] player
 * @param[in] ent
 * @sa AI_FighterCalcGuete
 * @sa AI_CivilianCalcGuete
 * @sa G_ClientMove
 * @sa G_ClientShoot
 */
void AI_ActorThink (player_t * player, edict_t * ent)
{
	aiAction_t bestAia;

#ifdef PARANOID
	Com_DPrintf(DEBUG_GAME, "AI_ActorThink: (ent %i, frame %i)\n", ent->number, level.framenum);
#endif

	/* if a weapon can be reloaded we attempt to do so if TUs permit, otherwise drop it */
	if (!(ent->state & STATE_PANIC)) {
		if (RIGHT(ent) && gi.csi->ods[RIGHT(ent)->item.t].reload && RIGHT(ent)->item.a == 0) {
			if (G_ClientCanReload(game.players + ent->pnum, ent->number, gi.csi->idRight)) {
#ifdef PARANOID
				Com_DPrintf(DEBUG_GAME, "AI_ActorThink: Reloading right hand weapon\n");
#endif
				G_ClientReload(player, ent->number, ST_RIGHT_RELOAD, QUIET);
			} else {
#ifdef PARANOID
				Com_DPrintf(DEBUG_GAME, "AI_ActorThink: Dropping right hand weapon\n");
#endif
				G_ClientInvMove(game.players + ent->pnum, ent->number, gi.csi->idRight, 0, 0, gi.csi->idFloor, NONE, NONE, qtrue, QUIET);
			}
		}
		if (LEFT(ent) && gi.csi->ods[LEFT(ent)->item.t].reload && LEFT(ent)->item.a == 0) {
			if (G_ClientCanReload(game.players + ent->pnum, ent->number, gi.csi->idLeft)) {
#ifdef PARANOID
				Com_DPrintf(DEBUG_GAME, "AI_ActorThink: Reloading left hand weapon\n");
#endif
				G_ClientReload(player, ent->number, ST_LEFT_RELOAD, QUIET);
			} else {
#ifdef PARANOID
				Com_DPrintf(DEBUG_GAME, "AI_ActorThink: Dropping left hand weapon\n");
#endif
				G_ClientInvMove(game.players + ent->pnum, ent->number, gi.csi->idLeft, 0, 0, gi.csi->idFloor, NONE, NONE, qtrue, QUIET);
			}
		}
	}

	/* if both hands are empty, attempt to get a weapon out of backpack if TUs permit */
	if (ent->chr.weapons && !LEFT(ent) && !RIGHT(ent)) {
		G_ClientGetWeaponFromInventory(player, ent->number, QUIET);
		if (LEFT(ent) || RIGHT(ent))
			Com_DPrintf(DEBUG_GAME, "AI_ActorThink: Got weapon from inventory\n");
	}

	bestAia = AI_PrepBestAction(player, ent);

	/* shoot('n'hide) */
	if (bestAia.target) {
		/* @todo: check whether shoot is needed or enemy died already;
		   use the remaining TUs for reaction fire */
		while (bestAia.shots) {
			(void)G_ClientShoot(player, ent->number, bestAia.target->pos, bestAia.mode, 0, NULL, qtrue, 0); /* 0 = first firemode */
			bestAia.shots--;
			if (bestAia.target->state & STATE_DEAD) {
				bestAia = AI_PrepBestAction(player, ent);
				if (!bestAia.target)
					return;
			}
		}
		G_ClientMove(player, ent->team, ent->number, bestAia.stop, qfalse, QUIET);
	}
}


/**
 * @brief Every server frame one single actor is handled - always in the same order
 * @sa G_RunFrame
 */
void AI_Run (void)
{
	player_t *player;
	edict_t *ent;
	int i, j;

	/* don't run this too often to prevent overflows */
	if (level.framenum % 10)
		return;

	/* set players to ai players and cycle over all of them */
	for (i = 0, player = game.players + game.sv_maxplayersperteam; i < game.sv_maxplayersperteam; i++, player++)
		if (player->inuse && player->pers.ai && level.activeTeam == player->pers.team) {
			/* find next actor to handle */
			if (!player->pers.last)
				ent = g_edicts;
			else
				ent = player->pers.last + 1;

			for (j = ent - g_edicts; j < globals.num_edicts; j++, ent++)
				if (ent->inuse && ent->team == player->pers.team && ent->type == ET_ACTOR && !(ent->state & STATE_DEAD) && ent->TU) {
					AI_ActorThink(player, ent);
					player->pers.last = ent;
					return;
				}

			/* nothing left to do, request endround */
			if (j >= globals.num_edicts) {
				G_ClientEndRound(player, QUIET);
				player->pers.last = g_edicts + globals.num_edicts;
			}
			return;
		}
}

#define MAX_SPAWNPOINTS		64
static int spawnPoints[MAX_SPAWNPOINTS];

/**
 * @brief Spawn civilians and aliens
 * @param[in] player
 * @param[in] numSpawn
 * @sa AI_CreatePlayer
 */
static void G_SpawnAIPlayer (player_t * player, int numSpawn)
{
	edict_t *ent;
	equipDef_t *ed = &gi.csi->eds[0];
	int i, j, numPoints, team;
	char name[MAX_VAR];

	/* search spawn points */
	team = player->pers.team;
	numPoints = 0;
	for (i = 0, ent = g_edicts; i < globals.num_edicts; i++, ent++)
		if (ent->inuse && ent->type == ET_ACTORSPAWN && ent->team == team)
			spawnPoints[numPoints++] = i;

	/* check spawn point number */
	if (numPoints < numSpawn) {
		Com_Printf("Not enough spawn points for team %i\n", team);
		numSpawn = numPoints;
	}

	/* prepare equipment */
	if (team != TEAM_CIVILIAN) {
		Q_strncpyz(name, gi.cvar_string("ai_equipment"), sizeof(name));
		for (i = 0, ed = gi.csi->eds; i < gi.csi->numEDs; i++, ed++)
			if (!Q_strncmp(name, ed->name, MAX_VAR))
				break;
		if (i == gi.csi->numEDs)
			ed = &gi.csi->eds[0];
	}

	/* spawn players */
	for (j = 0; j < numSpawn; j++) {
		assert(numPoints > 0);
		/* select spawnpoint */
		while (ent->type != ET_ACTORSPAWN)
			ent = &g_edicts[spawnPoints[rand() % numPoints]];

		/* spawn */
		level.num_spawned[team]++;
		level.num_alive[team]++;
		if (team != TEAM_CIVILIAN) {
			if (gi.csi->numAlienTeams) {
				int alienTeam = rand() % gi.csi->numAlienTeams;
				assert(gi.csi->alienTeams[alienTeam]);
				ent->chr.skin = gi.GetCharacterValues(gi.csi->alienTeams[alienTeam]->id, &ent->chr);
			} else
				ent->chr.skin = gi.GetCharacterValues(gi.cvar_string("ai_alien"), &ent->chr);

			ent->type = ET_ACTOR;
			ent->pnum = player->num;
			gi.linkentity(ent);

			/* skills; @todo: more power to Ortnoks, more mind to Tamans */
			CHRSH_CharGenAbilitySkills(&ent->chr, team, EMPL_SOLDIER, sv_maxclients->integer >= 2);
			ent->chr.skills[ABILITY_MIND] += 100;
			if (ent->chr.skills[ABILITY_MIND] >= MAX_SKILL)
				ent->chr.skills[ABILITY_MIND] = MAX_SKILL;

			ent->chr.HP = GET_HP(ent->chr.skills[ABILITY_POWER]);
			ent->HP = ent->chr.HP;
			ent->chr.morale = GET_MORALE(ent->chr.skills[ABILITY_MIND]);
			if (ent->chr.morale >= MAX_SKILL)
				ent->chr.morale = MAX_SKILL;
			ent->morale = ent->chr.morale;
			ent->STUN = 0;

			/* pack equipment */
			if (ent->chr.weapons)	/* actor can handle equipment */
				INVSH_EquipActor(&ent->i, ed->num, MAX_OBJDEFS, name, &ent->chr);
			else if (ent->chr.teamDefIndex >= 0)
				/* actor cannot handle equipment */
				INVSH_EquipActorMelee(&ent->i, &ent->chr);
			else
				Com_Printf("G_SpawnAIPlayer: actor with no equipment\n");

			/* set model */
			ent->chr.inv = &ent->i;
			ent->body = gi.modelindex(CHRSH_CharGetBody(&ent->chr));
			ent->head = gi.modelindex(CHRSH_CharGetHead(&ent->chr));
			ent->skin = ent->chr.skin;
		} else {
			CHRSH_CharGenAbilitySkills(&ent->chr, team, EMPL_SOLDIER, sv_maxclients->integer >= 2);
			ent->chr.HP = GET_HP(ent->chr.skills[ABILITY_POWER]) / 2;
			ent->HP = ent->chr.HP;
			ent->chr.morale = GET_MORALE(ent->chr.skills[ABILITY_MIND]);
			ent->morale = (ent->chr.morale > 45 ? 45 : ent->chr.morale); /* low morale for civilians */
			ent->STUN = 0;

			ent->chr.skin = gi.GetCharacterValues(gi.cvar_string("ai_civilian"), &ent->chr);
			ent->chr.inv = &ent->i;
			/* FIXME: Maybe we have civilians with armour, too - police and so on */
			ent->body = gi.modelindex(CHRSH_CharGetBody(&ent->chr));
			ent->head = gi.modelindex(CHRSH_CharGetHead(&ent->chr));
			ent->skin = ent->chr.skin;
			ent->type = ET_ACTOR;
			ent->pnum = player->num;
			gi.linkentity(ent);
		}
	}
	/* show visible actors */
	G_ClearVisFlags(team);
	G_CheckVis(NULL, qfalse);

	/* give time */
	G_GiveTimeUnits(team);
}


/**
 * @brief Spawn civilians and aliens
 * @param[in] team
 * @sa G_SpawnAIPlayer
 * @return player_t pointer
 * @note see cvars ai_numaliens, ai_numcivilians, ai_numactors
 */
player_t *AI_CreatePlayer (int team)
{
	player_t *p;
	int i;

	if (!sv_ai->integer) {
		Com_Printf("AI deactivated - set sv_ai cvar to 1 to activate it\n");
		return NULL;
	}

	/* set players to ai players and cycle over all of them */
	for (i = 0, p = game.players + game.sv_maxplayersperteam; i < game.sv_maxplayersperteam; i++, p++)
		if (!p->inuse) {
			memset(p, 0, sizeof(player_t));
			p->inuse = qtrue;
			p->num = p - game.players;
			p->pers.team = team;
			p->pers.ai = qtrue;
			if (team == TEAM_CIVILIAN)
				G_SpawnAIPlayer(p, ai_numcivilians->integer);
			else if (sv_maxclients->integer == 1)
				G_SpawnAIPlayer(p, ai_numaliens->integer);
			else
				G_SpawnAIPlayer(p, ai_numactors->integer);
			Com_Printf("Created AI player (team %i)\n", team);
			return p;
		}

	/* nothing free */
	return NULL;
}
