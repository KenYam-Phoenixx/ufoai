/**
 * @file g_combat.c
 * @brief All parts of the main game logic that are combat related
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

#define MAX_WALL_THICKNESS_FOR_SHOOTING_THROUGH 8

/* uncomment this to enable debugging the reaction fire */
/*#define DEBUG_REACTION*/

qboolean G_ResolveReactionFire(edict_t *target, qboolean force, qboolean endTurn, qboolean doShoot);
static void G_ReactToPreFire(edict_t *target);
static void G_ReactToPostFire(edict_t *target);

int reactionTUs[MAX_EDICTS][REACT_MAX];	/* Defined in g_local.h. See there for more. */

typedef enum {
	ML_WOUND,
	ML_DEATH
} morale_modifiers;

/**
 * @brief test if point is "visible" from team
 * @param[in] team
 * @param[in] point
 */
static qboolean G_TeamPointVis (int team, vec3_t point)
{
	edict_t *from;
	vec3_t eye;
	int i;

	/* test if point is visible from team */
	for (i = 0, from = g_edicts; i < globals.num_edicts; i++, from++)
		if (from->inuse && (from->type == ET_ACTOR || from->type == ET_ACTOR2x2) && !(from->state & STATE_DEAD) && from->team == team && G_FrustumVis(from, point)) {
			/* get viewers eye height */
			VectorCopy(from->origin, eye);
			if (from->state & (STATE_CROUCHED | STATE_PANIC))
				eye[2] += EYE_CROUCH;
			else
				eye[2] += EYE_STAND;

			/* line of sight */
			if (!gi.TestLine(eye, point))
				return qtrue;
		}

	/* not visible */
	return qfalse;
}

/**
 * @brief Applies morale changes to actors around a wounded or killed actor
 * @note only called when mor_panic is not zero
 * @param[in] type
 * @param[in] victim
 * @param[in] attacker
 * @param[in] param
 */
static void G_Morale (int type, edict_t * victim, edict_t * attacker, int param)
{
	edict_t *ent;
	int i, newMorale;
	float mod;

	for (i = 0, ent = g_edicts; i < globals.num_edicts; i++, ent++)
		/* this only applies to ET_ACTOR but not ET_ACTOR2x2 */
		if (ent->inuse && ent->type == ET_ACTOR && !(ent->state & STATE_DEAD) && ent->team != TEAM_CIVILIAN) {
			switch (type) {
			case ML_WOUND:
			case ML_DEATH:
				/* morale damage is damage dependant */
				mod = mob_wound->value * param;
				/* death hurts morale even more than just damage */
				if (type == ML_DEATH)
					mod += mob_death->value;
				/* seeing how someone gets shot increases the morale change */
				if (ent == victim || (G_ActorVis(ent->origin, victim, qfalse) && G_FrustumVis(ent, victim->origin)))
					mod *= mof_watching->value;
				if (ent->team == attacker->team) {
					/* teamkills are considered to be bad form, but won't cause an increased morale boost for the enemy */
					/* morale boost isn't equal to morale loss (it's lower, but morale gets regenerated) */
					if (victim->team == attacker->team)
						mod *= mof_teamkill->value;
					else
						mod *= mof_enemy->value;
				}
				/* seeing a civi die is more "acceptable" */
				if (victim->team == TEAM_CIVILIAN)
					mod *= mof_civilian->value;
				/* if an ally (or in singleplayermode, as human, a civilian) got shot, lower the morale, don't heighten it. */
				if (victim->team == ent->team || (victim->team == TEAM_CIVILIAN && ent->team != TEAM_ALIEN && sv_maxclients->integer == 1))
					mod *= -1;
				/* if you stand near to the attacker or the victim, the morale change is higher. */
				mod *= mor_default->value + pow(0.5, VectorDist(ent->origin, victim->origin) / mor_distance->value)
					* mor_victim->value + pow(0.5, VectorDist(ent->origin, attacker->origin) / mor_distance->value)
					* mor_attacker->value;
				/* morale damage is dependant on the number of living allies */
				mod *= (1 - mon_teamfactor->value)
					+ mon_teamfactor->value * (level.num_spawned[victim->team] + 1)
					/ (level.num_alive[victim->team] + 1);
				/* being hit isn't fun */
				if (ent == victim)
					mod *= mor_pain->value;
				break;
			default:
				Com_Printf("Undefined morale modifier type %i\n", type);
				mod = 0;
			}
			/* clamp new morale */
			/*+0.9 to allow weapons like flamethrowers to inflict panic (typecast rounding) */
			newMorale = ent->morale + (int) (MORALE_RANDOM(mod) + 0.9);
			if (newMorale > GET_MORALE(ent->chr.skills[ABILITY_MIND]))
				ent->morale = GET_MORALE(ent->chr.skills[ABILITY_MIND]);
			else if (newMorale < 0)
				ent->morale = 0;
			else
				ent->morale = newMorale;

			/* send phys data */
			G_SendStats(ent);
		}
}

/**
 * @brief Stores the TUs for reaction fire that are used (if any).
 * @note Normally called on end of turn.
 * @todo Comment on the AddEvent code.
 * @sa G_ClientStateChange
 * @param[in] team Index of team to loop through.
 */
void G_ResetReactionFire (int team)
{
	edict_t *ent;
	int i;

	for (i = 0, ent = g_edicts; i < globals.num_edicts; i++, ent++)
		if (ent->inuse && !(ent->state & STATE_DEAD) && (ent->type == ET_ACTOR || ent->type == ET_ACTOR2x2) && ent->team == team) {
			reactionTUs[ent->number][REACT_FIRED] = 0;	/* reset 'RF used' flag */
			if (ent->state & STATE_REACTION) {
				if ((ent->state & STATE_REACTION_ONCE) && (ent->TU >= TU_REACTION_SINGLE)) {
					/* Enough TUs for single reaction fire available. */
					ent->TU -= TU_REACTION_SINGLE;
					reactionTUs[ent->number][REACT_TUS] = TU_REACTION_SINGLE;	/* Save the used TUs for possible later re-adding. */
				} else if ((ent->state & STATE_REACTION_MANY) && (ent->TU >= TU_REACTION_MULTI)) {
					/* Enough TUs for multi reaction fire available. */
					ent->TU -= TU_REACTION_MULTI;
					reactionTUs[ent->number][REACT_TUS] = TU_REACTION_MULTI;	/* Save the used TUs for possible later re-adding. */
#if 0
/* @todo: this saving might be too powerful with multi-RF. (i.e. mutli-rf is too cheap in a lot of cases) */
				} else if (ent->TU > 0) {
					/* Not enough TUs for reaction fire available. */
					reactionTUs[ent->number][REACT_TUS] = ent->TU;	/* Save the used TUs for possible later re-adding. */
					ent->TU = 0;
#endif
				} else {
					/* No TUs at all available. */
					reactionTUs[ent->number][REACT_TUS] = -1;
				}
			} else {
				reactionTUs[ent->number][REACT_TUS] = 0;	/* Reset saved TUs. */
			}
			ent->state &= ~STATE_SHAKEN;
			gi.AddEvent(G_TeamToPM(ent->team), EV_ACTOR_STATECHANGE);
			gi.WriteShort(ent->number);
			gi.WriteShort(ent->state);
		}
}

/**
 * @param[in] mock pseudo action - only for calculating mock values - NULL for real action
 * @sa G_Damage
 */
static void G_UpdateShotMock (shot_mock_t *mock, edict_t *shooter, edict_t *struck, int damage)
{
	assert(struck->number != shooter->number || mock->allow_self);

	if (damage > 0) {
		if (!struck || !struck->inuse || struck->state & STATE_DEAD)
			return;
		else if (!(struck->visflags & (1 << shooter->team)))
			return;
		else if (struck->team == TEAM_CIVILIAN)
			mock->civilian += 1;
		else if (struck->team == shooter->team)
			mock->friend += 1;
		else if (struck->type == ET_ACTOR || struck->type == ET_ACTOR2x2)
			mock->enemy += 1;
		else
			return;

		mock->damage += damage;
	}
}

/**
 * @brief Update character stats after succesful shoot.
 * @param[in] *attacker Pointer to attacker.
 * @param[in] *fd Pointer to fireDef_t used in shoot.
 * @param[in] *target Pointer to target.
 * @note chr.chrscore is being sent to client in CL_ParseCharacterData()
 * @sa CL_UpdateCharacterSkills
 */
static void G_UpdateCharacterScore (edict_t *attacker, fireDef_t *fd, edict_t *target)
{
	if (!attacker || !fd || !target)
		return;

	switch (target->team) {
	case TEAM_ALIEN:	/**< Aliens. */
		if (target->HP <= 0)
			attacker->chr.chrscore.alienskilled++;
		else
			attacker->chr.chrscore.aliensstunned++;
		attacker->chr.chrscore.accuracystat++;
		/* Only killing/stunning an alien can lead to skill improve. */
		switch (fd->weaponSkill) {
		case SKILL_CLOSE:
			attacker->chr.chrscore.closekills++;
			break;
		case SKILL_HEAVY:
			attacker->chr.chrscore.heavykills++;
			attacker->chr.chrscore.powerstat++;
			break;
		case SKILL_ASSAULT:
			attacker->chr.chrscore.assaultkills++;
			break;
		case SKILL_SNIPER:
			attacker->chr.chrscore.sniperkills++;
			break;
		case SKILL_EXPLOSIVE:
			attacker->chr.chrscore.explosivekills++;
			break;
		default:
			break;
		}
		break;
	case TEAM_CIVILIAN:	/**< Civilians. */
		if (target->HP <= 0)
			attacker->chr.chrscore.civilianskilled++;
		else
			attacker->chr.chrscore.civiliansstunned++;
		break;
	case TEAM_PHALANX:	/* PHALANX soldiers. */
		if (target->HP <= 0)
			attacker->chr.chrscore.teamkilled++;
		else
			attacker->chr.chrscore.teamstunned++;
		break;
	default:
		break;
	}
}

/**
 * @brief Deals damage of a give type and amount to a target.
 * @param[in] ent @todo ???
 * @param[in] fd The fire definition that defines what type of damage is dealt.
 * @param[in] damage The value of the damage.
 * @param[in] attacker The attacker.
 * @param[in] mock pseudo shooting - only for calculating mock values - NULL for real shots
 * @sa G_SplashDamage
 * @sa G_PrintActorStats
 */
static void G_Damage (edict_t * ent, fireDef_t *fd, int damage, edict_t * attacker, shot_mock_t *mock)
{
	player_t *player = NULL;
	qboolean stun = (gi.csi->ods[fd->obj_idx].dmgtype == gi.csi->damStun);
	qboolean shock = (gi.csi->ods[fd->obj_idx].dmgtype == gi.csi->damShock);

	assert(ent);
	assert(ent->type == ET_ACTOR
			|| ent->type == ET_ACTOR2x2
			|| ent->type == ET_BREAKABLE
			|| ent->type == ET_DOOR);

	/* Breakables are immune to stun & shock damage. */
	if ((stun || shock || mock) && (ent->type == ET_BREAKABLE || ent->type == ET_DOOR))
 		return;

	/* Breakables */
	if (ent->type == ET_BREAKABLE || ent->type == ET_DOOR) {
		if (damage >= ent->HP) {
			gi.AddEvent(PM_ALL, EV_MODEL_EXPLODE);
			gi.WriteShort(ent->mapNum);
			gi.WriteShort(ent->number);
			if (ent->particle && Q_strcmp(ent->particle, "null")) {
				gi.AddEvent(PM_ALL, EV_SPAWN_PARTICLE);
				gi.WriteShort(ent->spawnflags);
				gi.WriteGPos(ent->pos);
				gi.WriteShort((int)strlen(ent->particle));
				gi.WriteString(ent->particle);
			}
			switch (ent->material) {
			case MAT_GLASS:
				gi.PositionedSound(PM_ALL, ent->origin, ent, "misc/breakglass", CHAN_AUTO, 1, 1);
				break;
			case MAT_METAL:
				gi.PositionedSound(PM_ALL, ent->origin, ent, "misc/breakmetal", CHAN_AUTO, 1, 1);
				break;
			case MAT_ELECTRICAL:
				gi.PositionedSound(PM_ALL, ent->origin, ent, "misc/breakelectric", CHAN_AUTO, 1, 1);
				break;
			case MAT_WOOD:
				gi.PositionedSound(PM_ALL, ent->origin, ent, "misc/breakwood", CHAN_AUTO, 1, 1);
				break;
			case MAT_MAX:
				break;
			}
			gi.unlinkentity(ent);
			ent->inuse = qfalse;
			ent->HP = 0;
			G_RecalcRouting(ent);
			G_FreeEdict(ent);
		} else {
			ent->HP = max(ent->HP - damage, 0);
		}
		return;
	}

	/* Actors don't die again. */
	if (ent->state & STATE_DEAD)
		return;

	/* Apply difficulty settings. */
	if (sv_maxclients->integer == 1) {
		if (attacker->team == TEAM_ALIEN && ent->team < TEAM_ALIEN)
			damage *= pow(1.3, difficulty->integer);
		else if (attacker->team < TEAM_ALIEN && ent->team == TEAM_ALIEN)
			damage *= pow(1.3, -difficulty->integer);
	}

	/* Apply armour effects. */
	if (damage > 0) {
		if (ent->i.c[gi.csi->idArmour]) {
			objDef_t *ad = &gi.csi->ods[ent->i.c[gi.csi->idArmour]->item.t];
			Com_DPrintf(DEBUG_GAME, "G_Damage: damage for '%s': %i, dmgweight (%i) protection: %i\n",
				ent->chr.name, damage, fd->dmgweight, ad->protection[fd->dmgweight]);
			damage = max(1, damage - ad->protection[fd->dmgweight]);
		} else {
			Com_DPrintf(DEBUG_GAME, "G_Damage: damage for '%s': %i, dmgweight (%i) protection: 0\n",
				ent->chr.name, damage, fd->dmgweight);
		}
	}

	assert((attacker->team >= 0) && (attacker->team < MAX_TEAMS));
	assert((ent->team >= 0) && (ent->team < MAX_TEAMS));

	if (g_nodamage != NULL && !g_nodamage->integer) {
		/* hit */
		if (mock) {
			G_UpdateShotMock(mock, attacker, ent, damage);
		} else if (stun) {
			ent->STUN += damage;
		} else if (shock) {
			/* Only do this if it's not one from our own team ... they should known that there is a flashbang coming. */
			if (ent->team != attacker->team) {
				player = game.players + ent->pnum;
				/* FIXME: there should be a possible protection, too */
				ent->TU = 0; /* flashbangs kill TUs */
				ent->state |= STATE_DAZED; /* entity is dazed */
				gi.cprintf(player, PRINT_HUD, _("Soldier is dazed!\nEnemy used flashbang!\n"));
				return;
			}
		} else {
			ent->HP = max(ent->HP - damage, 0);
			if (damage < 0) {
				/* @todo: also increase the morale a little bit when
				 * soldier gets healing and morale is lower than max possible */
			}
		}
	}

	if (mock)
		return;

	/* HP shouldn't become negative.
	 * Note: This check needs to be done for every assignment to HP above anyway since a "return" could pop up in between.
	 * I'll leave this one in here just in case. */
	ent->HP = max(ent->HP, 0);

	/* Check death/knockouth. */
	if (ent->HP == 0 || ent->HP <= ent->STUN) {
		G_SendStats(ent);
		/* prints stats for multiplayer to game console */
		if (sv_maxclients->integer > 1) {
			G_PrintActorStats(ent, attacker, fd);
		}

		G_ActorDie(ent, ent->HP == 0 ? STATE_DEAD : STATE_STUN, attacker);

		/* apply morale changes */
		if (mor_panic->integer)
			G_Morale(ML_DEATH, ent, attacker, damage);

		/* count kills */
		if (ent->HP == 0)
			level.num_kills[attacker->team][ent->team]++;
		/* count stuns */
		else
			level.num_stuns[attacker->team][ent->team]++;

		/* count score */
		if (ent->team == TEAM_CIVILIAN)
			attacker->chr.kills[KILLED_CIVILIANS]++;
		else if (attacker->team == ent->team)
			attacker->chr.kills[KILLED_TEAM]++;
		else
			attacker->chr.kills[KILLED_ALIENS]++;
		G_UpdateCharacterScore(attacker, fd, ent);

	} else {
		if (damage > 0 && mor_panic->integer) {
			G_Morale(ML_WOUND, ent, attacker, damage);
		} else { /* medikit, etc. */
			if (ent->HP > GET_HP(ent->chr.skills[ABILITY_POWER]))
				ent->HP = max(GET_HP(ent->chr.skills[ABILITY_POWER]), 0);
		}
		G_SendStats(ent);
	}
}

#ifdef DEBUG
/**
 * @brief Stun all members of a giben team.
 */
void G_StunTeam (void)
{
	/* default is to kill all teams */
	int teamToKill = -1, i;
	edict_t *ent;

	/* with a parameter we will be able to kill a specific team */
	if (gi.argc() == 2)
		teamToKill = atoi(gi.argv(1));

	gi.dprintf("G_StunTeam: stun team %i\n", teamToKill);

	for (i = 0, ent = g_edicts; i < globals.num_edicts; i++, ent++)
		if (ent->inuse && (ent->type == ET_ACTOR || ent->type == ET_ACTOR2x2) && !(ent->state & STATE_DEAD)) {
			if (teamToKill >= 0 && ent->team != teamToKill)
				continue;

			/* die */
			G_ActorDie(ent, STATE_STUN, NULL);

			if (teamToKill == TEAM_ALIEN)
				level.num_stuns[TEAM_PHALANX][TEAM_ALIEN]++;
			else
				level.num_stuns[TEAM_ALIEN][teamToKill]++;
		}

	/* check for win conditions */
	G_CheckEndGame();
}
#endif

/**
 * @brief Deals splash damage to a target and its surroundings.
 * @param[in] ent The shooting actor
 * @param[in] fd The fire definition that defines what type of damage is dealt and how big the splash radius is.
 * @param[in] impact The impact vector where the grenade is exploding
 * @param[in] mock pseudo shooting - only for calculating mock values - NULL for real shots
 * @param[in] tr The trace where the grenade hits something (or not)
 */
static void G_SplashDamage (edict_t * ent, fireDef_t * fd, vec3_t impact, shot_mock_t *mock, trace_t* tr)
{
	edict_t *check;
	vec3_t center;
	float dist;
	int damage;
	int i;

	qboolean shock = (gi.csi->ods[fd->obj_idx].dmgtype == gi.csi->damShock);

	assert(fd->splrad);

	for (i = 0, check = g_edicts; i < globals.num_edicts; i++, check++) {
		/* check basic info */
		if (!check->inuse)
			continue;

		/* If we use a blinding weapon we skip the target if it's looking
		 * away from the impact location. */
		if (shock && !G_FrustumVis(check, impact))
			continue;

		if (check->type == ET_ACTOR || check->type == ET_ACTOR2x2)
			VectorCopy(check->origin, center);
		else if (check->type == ET_BREAKABLE || check->type == ET_DOOR) {
			VectorAdd(check->absmin, check->absmax, center);
			VectorScale(center, 0.5, center);
		} else
			continue;

		/* check for distance */
		dist = VectorDist(impact, center);
		dist = dist > UNIT_SIZE / 2 ? dist - UNIT_SIZE / 2 : 0;
		if (dist > fd->splrad)
			continue;

		if (fd->irgoggles && (check->type == ET_ACTOR || check->type == ET_ACTOR2x2)) {
			/* check whether this actor (check) is in the field of view of the 'shooter' (ent) */
			if (G_FrustumVis(ent, check->origin)) {
				if (!mock) {
					G_AppearPerishEvent(~G_VisToPM(check->visflags), 1, check);
					check->visflags |= ~check->visflags;
				}
				continue;
			}
		}

		/* check for walls */
		if ((check->type == ET_ACTOR || check->type == ET_ACTOR2x2) && !G_ActorVis(impact, check, qfalse))
			continue;

		/* do damage */
		if (shock)
			damage = 0;
		else {
			/* REMOVED random component - it's quite random enough already */
			damage = (fd->spldmg[0] /* + fd->spldmg[1] * crand() */) * (1.0 - dist / fd->splrad);
		}

		if (mock)
			mock->allow_self = qtrue;
		G_Damage(check, fd, damage, ent, mock);
		if (mock)
			mock->allow_self = qfalse;
	}

	/* FIXME: splash might also hit other surfaces */
	if (tr && gi.csi->ods[fd->obj_idx].dmgtype == gi.csi->damFire && tr->contentFlags & CONTENTS_BURN) {
		/* sent particle to all players */
		gi.AddEvent(PM_ALL, EV_SPAWN_PARTICLE);
		gi.WriteShort(tr->contentFlags >> 8);
		gi.WritePos(impact);
		gi.WriteShort(7);
		gi.WriteString("burning");
	}
}

#define GRENADE_DT			0.1
#define GRENADE_STOPSPEED	60.0
/**
 * @sa G_ShootSingle
 * @sa Com_GrenadeTarget
 * @param[in] player The shooting player
 * @param[in] ent The shooting actor
 * @param[in] fd The firedefinition the actor is shooting with
 * @param[in] from The position the actor is shooting from
 * @param[in] at The grid position the actor is shooting to (or trying to shoot to)
 * @param[in] mask The team visibility mask (who's seeing the event)
 * @param[in] weapon The weapon to shoot with
 * @param[in] mock pseudo shooting - only for calculating mock values - NULL for real shots
 * @param[in] z_align This value may change the target z height
 */
static void G_ShootGrenade (player_t * player, edict_t * ent, fireDef_t * fd,
	vec3_t from, pos3_t at, int mask, item_t * weapon, shot_mock_t *mock, int z_align)
{
	vec3_t last, target, temp;
	vec3_t startV, curV, oldPos, newPos;
	vec3_t angles;
	float dt, time, speed;
	float acc;
	trace_t tr;
	int bounce;
/*	int i; */
	byte flags;

	/* get positional data */
	VectorCopy(from, last);
	gi.GridPosToVec(gi.map, at, target);
	/* first apply z_align value */
	target[2] -= z_align;

	/* prefer to aim grenades at the ground */
	target[2] -= GROUND_DELTA;

	/* calculate parabola */
	dt = gi.GrenadeTarget(last, target, fd->range, fd->launched, fd->rolled, startV);
	if (!dt) {
		if (!mock)
			gi.cprintf(player, PRINT_CONSOLE, _("Can't perform action - impossible throw!\n"));
		return;
	}

	/* cap start speed */
	speed = VectorLength(startV);
	if (speed > fd->range)
		speed = fd->range;

	/* add random effects and get new dir */
	acc = GET_ACC(ent->chr.skills[ABILITY_ACCURACY], fd->weaponSkill ? ent->chr.skills[fd->weaponSkill] : 0);

	VecToAngles(startV, angles);
	angles[PITCH] += crand() * (fd->spread[0] + acc*(1+fd->modif));
	angles[YAW] += crand() * (fd->spread[1] + acc*(1+fd->modif));
	AngleVectors(angles, startV, NULL, NULL);
	VectorScale(startV, speed, startV);

	/* move */
	VectorCopy(last, oldPos);
	VectorCopy(startV, curV);
	time = 0;
	dt = 0;
	bounce = 0;
	flags = SF_BOUNCING;
	for (;;) {
		/* kinematics */
		VectorMA(oldPos, GRENADE_DT, curV, newPos);
		newPos[2] -= 0.5 * GRAVITY * GRENADE_DT * GRENADE_DT;
		curV[2] -= GRAVITY * GRENADE_DT;

		/* trace */
		tr = gi.trace(oldPos, NULL, NULL, newPos, ent, MASK_SHOT);
		if (tr.fraction < 1.0 || time + dt > 4.0) {
			/* advance time */
			dt += tr.fraction * GRENADE_DT;
			time += dt;
			bounce++;

			if (tr.fraction < 1.0)
				VectorCopy(tr.endpos, newPos);

#if 0
			/* please debug, currently it causes double sounds */
			/* calculate additional visibility */
			for (i = 0; i < MAX_TEAMS; i++)
				if (G_TeamPointVis(i, newPos))
					mask |= 1 << i;
#endif

			if
				/* enough bouncing around */
				(VectorLength(curV) < GRENADE_STOPSPEED || time > 4.0 || bounce > fd->bounce
				 /* or we have sensors that tell us enemy is near */
				 || (!fd->delay && tr.ent && (tr.ent->type == ET_ACTOR || tr.ent->type == ET_ACTOR2x2))) {

				if (!mock) {
					/* explode */
					gi.AddEvent(G_VisToPM(mask), EV_ACTOR_THROW);
					gi.WriteShort(dt * 1000);
					gi.WriteShort(fd->obj_idx);
					gi.WriteByte(fd->weap_fds_idx);
					gi.WriteByte(fd->fd_idx);
					if (tr.ent && (tr.ent->type == ET_ACTOR || tr.ent->type == ET_ACTOR2x2))
						gi.WriteByte(flags | SF_BODY);
					else
						gi.WriteByte(flags | SF_IMPACT);
					gi.WritePos(last);
					gi.WritePos(startV);
				}

				tr.endpos[2] += 10;

				/* check if this is a stone, ammor clip or grenade */
				if (fd->splrad) {
					G_SplashDamage(ent, fd, tr.endpos, mock, &tr);
				} else if (!mock) {
					/* spawn the stone on the floor */
					if (fd->ammo && !fd->splrad && gi.csi->ods[weapon->t].thrown) {
						pos3_t drop;
						edict_t *floor, *actor;

						VecToPos(tr.endpos, drop);

						for (floor = g_edicts; floor < &g_edicts[globals.num_edicts]; floor++) {
							if (floor->inuse
								&& floor->type == ET_ITEM
								&& VectorCompare(drop, floor->pos))
								break;
						}

						if (floor == &g_edicts[globals.num_edicts]) {
							floor = G_SpawnFloor(drop);

							for (actor = g_edicts; actor < &g_edicts[globals.num_edicts]; actor++)
								if (actor->inuse
									 && (actor->type == ET_ACTOR || actor->type == ET_ACTOR2x2)
									 && VectorCompare(drop, actor->pos) )
									FLOOR(actor) = FLOOR(floor);
						} else {
							gi.AddEvent(G_VisToPM(floor->visflags), EV_ENT_PERISH);
							gi.WriteShort(floor->number);
							floor->visflags = 0;
						}
						Com_TryAddToInventory(&floor->i, *weapon, gi.csi->idFloor);

						/* send item info to the clients */
						G_CheckVis(floor, qtrue);
					}
				}
				return;
			}

			if (!mock) {
				/* send */
				gi.AddEvent(G_VisToPM(mask), EV_ACTOR_THROW);
				gi.WriteShort(dt * 1000);
				gi.WriteShort(fd->obj_idx);
				gi.WriteByte(fd->weap_fds_idx);
				gi.WriteByte(fd->fd_idx);
				gi.WriteByte(flags);
				gi.WritePos(last);
				gi.WritePos(startV);
			}
			flags |= SF_BOUNCED;

			/* bounce */
			VectorScale(curV, fd->bounceFac, curV);
			VectorScale(tr.plane.normal, -DotProduct(tr.plane.normal, curV), temp);
			VectorAdd(temp, curV, startV);
			VectorAdd(temp, startV, curV);

			/* prepare next move */
			VectorCopy(tr.endpos, last);
			VectorCopy(tr.endpos, oldPos);
			VectorCopy(curV, startV);
			dt = 0;
		} else {
			dt += GRENADE_DT;
			VectorCopy(newPos, oldPos);
		}
	}
}


/**
 * @brief Fires straight shots.
 * @param[in] ent The attacker.
 * @param[in] fd The fire definition that is used for the shot.
 * @param[in] from Location of the gun muzzle.
 * @param[in] at Grid coordinate of the target.
 * @param[in] mask Visibility bit-mask (who's seeing the event)
 * @param[in] weapon The weapon the actor is shooting with
 * @param[in] mock pseudo shooting - only for calculating mock values - NULL for real shots
 * @param[in] z_align This value may change the target z height
 */
static void G_ShootSingle (edict_t * ent, fireDef_t * fd, vec3_t from, pos3_t at,
	int mask, item_t * weapon, shot_mock_t *mock, int z_align, int i)
{
	vec3_t dir;	/* Direction from the location of the gun muzzle ("from") to the target ("at") */
	vec3_t angles;	/* ?? @todo The random dir-modifier ?? */
	vec3_t cur_loc;	/* The current location of the projectile. */
	vec3_t impact;	/* The location of the target (-center?) */
	vec3_t temp;
	vec3_t tracefrom;	/* sum */
	trace_t tr;	/* the traceing */
	float acc;	/* Accuracy modifier for the angle of the shot. */
	float range;	/* ?? @todo */
	float gauss1;
	float gauss2;   /* For storing 2 gaussian distributed random values. */
	int bounce;	/* count the bouncing */
	int damage;	/* The damage to be dealt to the target. */
	byte flags;	/* ?? @todo */
	int throughWall; /* shoot through x walls */

	/* Calc direction of the shot. */
	gi.GridPosToVec(gi.map, at, impact);	/* Get the position of the targetted grid-cell. ('impact' is used only temporary here)*/
	impact[2] -= z_align;
	VectorCopy(from, cur_loc);		/* Set current location of the projectile to the starting (muzzle) location. */
	VectorSubtract(impact, cur_loc, dir);	/* Calculate the vector from current location to the target. */
	VectorNormalize(dir);			/* Normalize the vector i.e. make length 1.0 */

	/* ?? @todo: Probably places the starting-location a bit away (cur_loc+8*dir) from the attacker-model/grid.
	 * Might need some change to reflect 2x2 units.
	 * Also might need a check if the distance is bigger than the one to the impact location. */
	VectorMA(cur_loc, sv_shot_origin->value, dir, cur_loc);
	VecToAngles(dir, angles);		/* Get the angles of the direction vector. */

	/* Get accuracy value for this attacker. */
	acc = GET_ACC(ent->chr.skills[ABILITY_ACCURACY], fd->weaponSkill ? ent->chr.skills[fd->weaponSkill] : 0);

	/* Get 2 gaussian distributed random values */
	gaussrand(&gauss1, &gauss2);

	/* Modify the angles with the accuracy modifier as a randomizer-range. If the attacker is crouched this modifier is included as well.  */
	if ((ent->state & STATE_CROUCHED) && fd->crouch) {
		angles[PITCH] += gauss1 * 0.5 * (fd->spread[0] + acc * (1+fd->modif)) * fd->crouch;
		angles[YAW] += gauss2 * 0.5 * (fd->spread[1] + acc * (1+fd->modif)) * fd->crouch;
	} else {
		angles[PITCH] += gauss1 * 0.5 * (fd->spread[0] + acc * (1+fd->modif));
		angles[YAW] += gauss2 * 0.5 * (fd->spread[1] + acc * (1+fd->modif));
	}
	/* Convert changed angles into new direction. */
	AngleVectors(angles, dir, NULL, NULL);

	/* shoot and bounce */
	throughWall = fd->throughWall;
	range = fd->range;
	bounce = 0;
	flags = 0;
	/* healing */
	if (fd->damage[0] < 0)
		damage = fd->damage[0] + (fd->damage[1] * crand());
	else
		damage = max(0, fd->damage[0] + (fd->damage[1] * crand()));
	VectorCopy(cur_loc, tracefrom);
	for (;;) {
		/* Calc 'impact' vector that is located at the end of the range
		 * defined by the fireDef_t. This is not really the impact location,
		 * but rather the 'endofrange' location, see below for another use.*/
		VectorMA(cur_loc, range, dir, impact);

		/* Do the trace from current position of the projectile
		 * to the end_of_range location.*/
		/* mins and maxs should be set via localModel_t don't they? */
		tr = gi.trace(tracefrom, NULL, NULL, impact, ent, MASK_SHOT);

		/* maybe we start the trace from within a brush (e.g. in case of throughWall) */
		if (tr.startsolid)
			break;

		/* _Now_ we copy the correct impact location. */
		VectorCopy(tr.endpos, impact);

		/* set flags when trace hit something */
		if (tr.fraction < 1.0) {
			if (tr.ent && (tr.ent->type == ET_ACTOR || tr.ent->type == ET_ACTOR2x2)
				/* check if we differenciate between body and wall */
				&& !fd->delay)
				flags |= SF_BODY;
			else if (bounce < fd->bounce)
				flags |= SF_BOUNCING;
			else
				flags |= SF_IMPACT;
		}

#if 0
		/* @todo: please debug, currently it causes double sounds */
		/* calculate additional visibility */
		for (k = 0; k < MAX_TEAMS; k++)
			if (G_TeamPointVis(k, impact))
				mask |= 1 << k;
#endif

		/* victims see shots */
		if (tr.ent && (tr.ent->type == ET_ACTOR || tr.ent->type == ET_ACTOR2x2))
			mask |= 1 << tr.ent->team;

		if (!mock) {
			/* send shot */
			gi.AddEvent(G_VisToPM(mask), EV_ACTOR_SHOOT);
			gi.WriteShort(ent->number);
			gi.WriteShort(fd->obj_idx);
			gi.WriteByte(fd->weap_fds_idx);
			gi.WriteByte(fd->fd_idx);
			gi.WriteByte(flags);
			gi.WriteByte(tr.contentFlags);
			gi.WritePos(tracefrom);
			gi.WritePos(impact);
			gi.WriteDir(tr.plane.normal);

			/* send shot sound to the others */
			gi.AddEvent(~G_VisToPM(mask), EV_ACTOR_SHOOT_HIDDEN);
			gi.WriteByte(qfalse);
			gi.WriteShort(fd->obj_idx);
			gi.WriteByte(fd->weap_fds_idx);
			gi.WriteByte(fd->fd_idx);

			if (i == 0 && (gi.csi->ods[fd->obj_idx].dmgtype == gi.csi->damFire
			 || gi.csi->ods[fd->obj_idx].dmgtype == gi.csi->damBlast) && tr.contentFlags & CONTENTS_BURN) {
				/* sent particle to all players */
				gi.AddEvent(PM_ALL, EV_SPAWN_PARTICLE);
				gi.WriteShort(tr.contentFlags >> 8);
				gi.WritePos(impact);
				gi.WriteShort(4);
				gi.WriteString("fire");
			}
		}

		if (tr.fraction < 1.0 && !fd->bounce) {
			/* check for shooting through wall */
			if (throughWall && tr.contentFlags & CONTENTS_SOLID) {
				throughWall--;
				/* reduce damage */
				/* TODO: reduce even more if the wall was hit far away and
				 * not close by the shooting actor */
				damage /= sqrt(fd->throughWall - throughWall + 1);
				VectorMA(tr.endpos, MAX_WALL_THICKNESS_FOR_SHOOTING_THROUGH, dir, tracefrom);
				continue;
			}

			/* do splash damage */
			if (fd->splrad) {
				VectorMA(impact, sv_shot_origin->value, tr.plane.normal, impact);
				G_SplashDamage(ent, fd, impact, mock, &tr);
			}
		}

		/* do damage if the trace hit an entity */
		if (tr.ent && (tr.ent->type == ET_ACTOR || tr.ent->type == ET_ACTOR2x2 || tr.ent->type == ET_BREAKABLE)) {
			G_Damage(tr.ent, fd, damage, ent, mock);
			break;
		}

		/* bounce is checked here to see if the rubber rocket hit walls enough times to wear out*/
		bounce++;
		if (bounce > fd->bounce || tr.fraction >= 1.0)
			break;

		range -= tr.fraction * range;
		VectorCopy(impact, cur_loc);
		VectorScale(tr.plane.normal, -DotProduct(tr.plane.normal, dir), temp);
		VectorAdd(temp, dir, dir);
		VectorAdd(temp, dir, dir);
		flags |= SF_BOUNCED;
	}

	if (!mock) {
		/* spawn the throwable item on the floor but only if it is not depletable */
		if (fd->ammo && !fd->splrad && gi.csi->ods[weapon->t].thrown && !gi.csi->ods[weapon->t].deplete) {
			pos3_t drop;
			edict_t *floor, *actor;

			if (VectorCompare(ent->pos, at)) { /* throw under his own feet */
				VectorCopy(at, drop);
			} else {
				impact[2] -= 20; /* a hack: no-gravity items are flying high */
				VecToPos(impact, drop);
			}

			for (floor = g_edicts; floor < &g_edicts[globals.num_edicts]; floor++) {
				if (floor->inuse
					&& floor->type == ET_ITEM
					&& VectorCompare(drop, floor->pos))
					break;
			}

			if (floor == &g_edicts[globals.num_edicts]) {
				floor = G_SpawnFloor(drop);

				for (actor = g_edicts; actor < &g_edicts[globals.num_edicts]; actor++)
					if (actor->inuse
						 && (actor->type == ET_ACTOR || actor->type == ET_ACTOR2x2)
						 && VectorCompare(drop, actor->pos) )
						FLOOR(actor) = FLOOR(floor);
			} else {
				gi.AddEvent(G_VisToPM(floor->visflags), EV_ENT_PERISH);
				gi.WriteShort(floor->number);
				floor->visflags = 0;
			}
			Com_TryAddToInventory(&floor->i, *weapon, gi.csi->idFloor);

			/* send item info to the clients */
			G_CheckVis(floor, qtrue);
		}
	}
}

static void G_GetShotOrigin (edict_t *shooter, fireDef_t *fd, vec3_t dir, vec3_t shotOrigin)
{
	/* get weapon position */
	gi.GridPosToVec(gi.map, shooter->pos, shotOrigin);
	/* adjust height: */
	shotOrigin[2] += fd->shotOrg[1];
	/* adjust horizontal: */
	if (fd->shotOrg[0] != 0) {
		float x, y, length;

		/* get "right" and "left" of a unit(rotate dir 90 on the x-y plane): */
		x = dir[1];
		y = -dir[0];
		length = sqrt(dir[0] * dir[0] + dir[1] * dir[1]);
		/* assign adjustments: */
		shotOrigin[0] += x * fd->shotOrg[0] / length;
		shotOrigin[1] += y * fd->shotOrg[0] / length;
	}
}

/**
 * @sa G_ClientShoot
 */
static qboolean G_GetShotFromType (edict_t *ent, int type, int firemode, item_t **weapon, int *container, fireDef_t **fd)
{
	int weapon_fd_idx;

	if (type >= ST_NUM_SHOOT_TYPES)
		gi.error("G_GetShotFromType: unknown shoot type %i.\n", type);

	if (IS_SHOT_HEADGEAR(type)) {
		if (!HEADGEAR(ent))
			return qfalse;
		*weapon = &HEADGEAR(ent)->item;
		*container = gi.csi->idHeadgear;
	} else if (IS_SHOT_RIGHT(type)) {
		if (!RIGHT(ent))
			return qfalse;
		*weapon = &RIGHT(ent)->item;
		*container = gi.csi->idRight;
	} else {
		if (!LEFT(ent))
			return qfalse;
		*weapon = &LEFT(ent)->item;
		*container = gi.csi->idLeft;
	}

	if ((*weapon)->m == NONE) {
		/* This weapon does not use ammo, check for existing firedefs in the weapon. */
		if (&gi.csi->ods[(*weapon)->t].numWeapons > 0) {
			/* Get firedef from the weapon entry instead */
			Com_DPrintf(DEBUG_GAME, "od->numWeapons: %i\n", gi.csi->ods[(*weapon)->t].numWeapons);
			weapon_fd_idx = FIRESH_FiredefsIDXForWeapon(&gi.csi->ods[(*weapon)->t], (*weapon)->t);
			Com_DPrintf(DEBUG_GAME, "weapon_fd_idx: %i (%s), firemode: %i\n", weapon_fd_idx, gi.csi->ods[(*weapon)->t].name, firemode);
			assert(weapon_fd_idx >= 0);
			/* fd = od[weapon_fd_idx][firemodeidx] */
			*fd = &gi.csi->ods[(*weapon)->t].fd[weapon_fd_idx][firemode];
		} else {
			*weapon = NULL;
			return qfalse;
		}
	} else {
		/* Get firedef from the ammo entry. */
		weapon_fd_idx = FIRESH_FiredefsIDXForWeapon(&gi.csi->ods[(*weapon)->m], (*weapon)->t);
		assert(weapon_fd_idx >= 0);
		/* fd = od[weapon_fd_idx][firemodeidx] */
		*fd = &gi.csi->ods[(*weapon)->m].fd[weapon_fd_idx][firemode];
	}

	return qtrue;
}

/**
 * @brief Setup for shooting, either real or mock
 * @param[in] player @todo: The player this action belongs to (i.e. either the ai or the player)
 * @param[in] num @todo: The index number of the 'inventory' that is used for the shot (i.e. left or right hand)
 * @param[in] at Position to fire on.
 * @param[in] type What type of shot this is (left, right reaction-left etc...).
 * @param[in] firemode The firemode index of the ammo for the used weapon (objDef.fd[][x])  .
 * @param[in] mock pseudo shooting - only for calculating mock values - NULL for real shots
 * @param[in] allowReaction Set to qtrue to check whether this has forced any reaction fire, otherwise qfalse.
 * @return qtrue if everthing went ok (i.e. the shot(s) where fired ok), otherwise qfalse.
 * @param[in] z_align This value may change the target z height
 */
qboolean G_ClientShoot (player_t * player, int num, pos3_t at, int type,
	int firemode, shot_mock_t *mock, qboolean allowReaction, int z_align)
{
	fireDef_t *fd = NULL;
	edict_t *ent;
	item_t *weapon = NULL;
	vec3_t dir, center, target, shotOrigin;
	int i, ammo, prev_dir = 0, reaction_leftover, shots;
	int container = 0, mask;
	qboolean quiet;

	ent = g_edicts + num;
	quiet = (mock != NULL);

	if (!G_GetShotFromType(ent, type, firemode, &weapon, &container, &fd)) {
		if (!weapon && !quiet)
			gi.cprintf(player, PRINT_CONSOLE, _("Can't perform action - object not activable!\n"));
		return qfalse;
	}

	ammo = weapon->a;
	reaction_leftover = IS_SHOT_REACTION(type) ? sv_reaction_leftover->value : 0;

	/* check if action is possible */
	if (!G_ActionCheck(player, ent, fd->time + reaction_leftover, quiet))
		return qfalse;

	/* Don't allow to shoot yourself */
	if (VectorCompare(ent->pos, at))
		return qfalse;

	/* check that we're not firing a twohanded weapon with one hand! */
	if (gi.csi->ods[weapon->t].fireTwoHanded &&	LEFT(ent)) {
		if (!quiet)
			gi.cprintf(player, PRINT_CONSOLE, _("Can't perform action - weapon cannot be fired one handed!\n"));
		return qfalse;
	}

	/* check we're not out of ammo */
	if (!ammo && fd->ammo && !gi.csi->ods[weapon->t].thrown) {
		if (!quiet)
			gi.cprintf(player, PRINT_CONSOLE, _("Can't perform action - no ammo!\n"));
		return qfalse;
	}

	/* check target is not out of range */
	gi.GridPosToVec(gi.map, at, target);
	if (fd->range < VectorDist(ent->origin, target)) {
		if (!quiet)
			gi.cprintf(player, PRINT_HUD, _("Can't perform action - target out of range!\n"));
		return qfalse;
	}

	/* fire shots */
	shots = fd->shots;
	if (fd->ammo && !gi.csi->ods[weapon->t].thrown) {
		if (ammo < fd->ammo) {
			shots = fd->shots * ammo / fd->ammo;
			ammo = 0;
		} else {
			ammo -= fd->ammo;
		}
		if (shots < 1) {
			if (!quiet)
				gi.cprintf(player, PRINT_CONSOLE, _("Can't perform action - not enough ammo!\n"));
			return qfalse;
		}
	}

	/* rotate the player */
	if (mock)
		prev_dir = ent->dir;
	VectorSubtract(at, ent->pos, dir);
	ent->dir = AngleToDV((int) (atan2(dir[1], dir[0]) * todeg));

	if (!mock) {
		G_CheckVisTeam(ent->team, NULL, qfalse);

		gi.AddEvent(G_VisToPM(ent->visflags), EV_ACTOR_TURN);
		gi.WriteShort(num);
		gi.WriteByte(ent->dir);
	}

	/* calculate visibility */
	target[2] -= z_align;
	VectorSubtract(target, ent->origin, dir);
	VectorMA(ent->origin, 0.5, dir, center);
	mask = 0;
	for (i = 0; i < MAX_TEAMS; i++)
		if (ent->visflags & (1 << i) || G_TeamPointVis(i, target) || G_TeamPointVis(i, center))
			mask |= 1 << i;

	if (!mock) {
		/* check whether this has forced any reaction fire */
		if (allowReaction) {
			G_ReactToPreFire(ent);
			if (ent->state & STATE_DEAD)
				/* dead men can't shoot */
				return qfalse;
		}

		/* start shoot */
		gi.AddEvent(G_VisToPM(mask), EV_ACTOR_START_SHOOT);
		gi.WriteShort(ent->number);
		gi.WriteShort(fd->obj_idx);
		gi.WriteByte(fd->weap_fds_idx);
		gi.WriteByte(fd->fd_idx);
		gi.WriteGPos(ent->pos);
		gi.WriteGPos(at);

		/* send shot sound to the others */
		gi.AddEvent(~G_VisToPM(mask), EV_ACTOR_SHOOT_HIDDEN);
		gi.WriteByte(qtrue);
		gi.WriteShort(fd->obj_idx);
		gi.WriteByte(fd->weap_fds_idx);
		gi.WriteByte(fd->fd_idx);

		/* ammo... */
		if (fd->ammo) {
			if (ammo > 0 || !gi.csi->ods[weapon->t].thrown) {
				gi.AddEvent(G_VisToPM(ent->visflags), EV_INV_AMMO);
				gi.WriteShort(num);
				gi.WriteByte(ammo);
				gi.WriteByte(weapon->m);
				weapon->a = ammo;
				if (IS_SHOT_RIGHT(type))
					gi.WriteByte(gi.csi->idRight);
				else
					gi.WriteByte(gi.csi->idLeft);
			} else { /* delete the knife or the rifle without ammo */
				gi.AddEvent(G_VisToPM(ent->visflags), EV_INV_DEL);
				gi.WriteShort(num);
				gi.WriteByte(container);
				assert(gi.csi->ids[container].single);
				INVSH_EmptyContainer(&ent->i, container);
			}
			/* x and y value */
			gi.WriteByte(0);
			gi.WriteByte(0);
		}

		/* remove throwable oneshot && deplete weapon from inventory */
		if (gi.csi->ods[weapon->t].thrown && gi.csi->ods[weapon->t].oneshot && gi.csi->ods[weapon->t].deplete) {
			gi.AddEvent(G_VisToPM(ent->visflags), EV_INV_DEL);
			gi.WriteShort(num);
			gi.WriteByte(container);
			assert(gi.csi->ids[container].single);
			INVSH_EmptyContainer(&ent->i, container);
			/* x and y value */
			gi.WriteByte(0);
			gi.WriteByte(0);
		}
	}

	G_GetShotOrigin(ent, fd, dir, shotOrigin);

	/* fire all shots */
	for (i = 0; i < shots; i++)
		if (fd->gravity)
			G_ShootGrenade(player, ent, fd, shotOrigin, at, mask, weapon, mock, z_align);
		else
			G_ShootSingle(ent, fd, shotOrigin, at, mask, weapon, mock, z_align, i);

	if (!mock) {
		/* send TUs if ent still alive */
		if (ent->inuse && !(ent->state & STATE_DEAD)) {
			ent->TU = max(ent->TU - fd->time, 0);
			G_SendStats(ent);
		}

		/* end events */
		gi.EndEvents();

		/* check for win/draw conditions */
		G_CheckEndGame();

		/* check for Reaction fire against the shooter */
		if (allowReaction)
			G_ReactToPostFire(ent);
	} else {
		ent->dir = prev_dir;
	}
	return qtrue;
}

/**
 * @brief @todo: This seems to be the function that is called for reaction fire isn't it?
 * @param[in] player @todo: The player this action belongs to (i.e. either the ai or the player)
 * @param[in] num @todo: The index number of the 'inventory' that is used for the shot (i.e. left or right hand)
 * @param[in] at Position to fire on.
 * @param[in] type What type of shot this is (left, right reaction-left etc...).
 * @param[in] firemode The firemode index of the ammo for the used weapon (objDef.fd[][x])  .
 * @return qtrue if everthing went ok (i.e. the shot(s) where fired ok), otherwise qfalse.
 * @sa G_ReactionFire (Not there anymore?)
 * @sa G_ClientShoot
 */
static qboolean G_FireWithJudgementCall (player_t * player, int num, pos3_t at, int type, int firemode)
{
	shot_mock_t mock;
	edict_t *shooter;
	int ff, i, maxff, minhit;

	shooter = g_edicts + num;

	minhit = shooter->reaction_minhit;
	if (shooter->state & STATE_INSANE)
		maxff = 100;
	else if (shooter->state & STATE_RAGE)
		maxff = 60;
	else if (shooter->state & STATE_PANIC)
		maxff = 30;
	else if (shooter->state & STATE_SHAKEN)
		maxff = 15;
	else
		maxff = 5;

	memset(&mock, 0, sizeof(mock));
	for (i = 0; i < 100; i++)
		G_ClientShoot(player, num, at, type, firemode, &mock, qfalse, 0);

	Com_DPrintf(DEBUG_GAME, "G_FireWithJudgementCall: Hit: %d/%d FF+Civ: %d+%d=%d/%d Self: %d.\n",
		mock.enemy, minhit, mock.friend, mock.civilian, mock.friend + mock.civilian, maxff, mock.self);

	ff = mock.friend + (shooter->team == TEAM_ALIEN ? 0 : mock.civilian);
	if (ff <= maxff && mock.enemy >= minhit)
		return G_ClientShoot(player, num, at, type, firemode, NULL, qfalse, 0);
	else
		return qfalse;
}

/**
 * @brief Check whether ent can reaction fire at target, i.e. that it can see it and neither is dead etc.
 * @param[in] ent The entity that might be firing
 * @param[in] target The entity that might be fired at
 * @param[out] reason If not null then it prints the reason that reaction fire wasn't possible
 * @returns Whether 'ent' can actually fire at 'target'
 */
static qboolean G_CanReactionFire (edict_t *ent, edict_t *target, char *reason)
{
	float actorVis;
	qboolean frustum;

	/* an entity can't reaction fire at itself */
	if (ent == target) {
#ifdef DEBUG_REACTION
		if (reason)
			Com_sprintf(reason, sizeof(reason), "Cannot fire on self");
		return qfalse;
#endif
	}

	/* check ent is a suitable shooter */
	if (!ent->inuse || (ent->type != ET_ACTOR && ent->type != ET_ACTOR2x2) || (ent->state & STATE_DEAD)) {
#ifdef DEBUG_REACTION
		if (reason)
			Com_sprintf(reason, sizeof(reason), "Shooter is not ent, is non-actor or is dead");
#endif
		return qfalse;
	}

	/* check ent has reaction fire enabled */
	if (!(ent->state & STATE_SHAKEN) && !(ent->state & STATE_REACTION_MANY) &&
			(!(ent->state & STATE_REACTION_ONCE) || reactionTUs[ent->number][REACT_FIRED])) {
#ifdef DEBUG_REACTION
		if (reason)
			Com_sprintf(reason, sizeof(reason), "Shooter does not have reaction fire enabled, or has already fired too often");
#endif
		return qfalse;
	}

	/* check in range and visible */
	if (VectorDistSqr(ent->origin, target->origin) > MAX_SPOT_DIST * MAX_SPOT_DIST) {
#ifdef DEBUG_REACTION
		if (reason)
			Com_sprintf(reason, sizeof(reason), "Target is out of range");
#endif
		return qfalse;
	}

	actorVis = G_ActorVis(ent->origin, target, qtrue);
	frustum = G_FrustumVis(ent, target->origin);
	if (actorVis <= 0.2 || !frustum) {
#ifdef DEBUG_REACTION
		if (reason)
			Com_sprintf(reason, sizeof(reason), "Target is not visible");
#endif
		return qfalse;
	}

	/* If reaction fire is triggered by a friendly unit
	 * and the shooter is still sane, don't shoot;
	 * well, if the shooter isn't sane anymore... */
	if (target->team == TEAM_CIVILIAN || target->team == ent->team)
		if (!(ent->state & STATE_SHAKEN) || (float) ent->morale / mor_shaken->value > frand()) {
#ifdef DEBUG_REACTION
			if (reason)
				Com_sprintf(reason, sizeof(reason), "Shooter will not fire on friendly");
#endif
			return qfalse;
		}

	/* Don't react in your own turn, trust your commander. Can't use
	 * level.activeTeam, because this function touches it recursively. */
	if (ent->team == turnTeam) {
#ifdef DEBUG_REACTION
		if (reason)
			Com_sprintf(reason, sizeof(reason), "It's the shooter's turn");
#endif
		return qfalse;
	}

	/* okay do it then */
	return qtrue;
}

/**
 * @brief Get the number of TUs that ent needs to fire at target, also optionally return the firing hand. Used for reaction fire.
 * @param[in] ent The shooter entity.
 * @param[in] target The target entity.
 * @param[out] fire_hand_type If not NULL then this stores the hand (combind with the 'reaction' info) that the shooter will fire with.
 * @param[out] firemode The firemode that will be used for the shot.
 * @returns The number of TUs required to fire or -1 if firing is not possible
 */
static int G_GetFiringTUs (edict_t *ent, edict_t *target, int *fire_hand_type, int *firemode)
{
	int weapon_fd_idx;
	int tmp = -2;

	/* The caller doesn't use this parameter, use a temporary one instead. */
	if (!firemode)
		firemode = &tmp;

	/* Fire the weapon in the right hand if everything is ok. */
	if (RIGHT(ent) && (RIGHT(ent)->item.m != NONE)
	 && gi.csi->ods[RIGHT(ent)->item.t].weapon
	 && (!gi.csi->ods[RIGHT(ent)->item.t].reload || RIGHT(ent)->item.a > 0) ) {
		weapon_fd_idx = FIRESH_FiredefsIDXForWeapon(&gi.csi->ods[RIGHT(ent)->item.m], RIGHT(ent)->item.t);
		assert(weapon_fd_idx >= 0);

		if (reactionFiremode[ent->number][RF_HAND] == 0
		 && reactionFiremode[ent->number][RF_FM] >= 0
		 && reactionFiremode[ent->number][RF_FM] < MAX_FIREDEFS_PER_WEAPON) { /* If a RIGHT-hand firemode is selected and sane. */
			*firemode = reactionFiremode[ent->number][RF_FM]; /* Get selected (if any) firemode for the weapon in the right hand. */

			if (gi.csi->ods[RIGHT(ent)->item.m].fd[weapon_fd_idx][*firemode].time + sv_reaction_leftover->integer <= ent->TU
			 && gi.csi->ods[RIGHT(ent)->item.m].fd[weapon_fd_idx][*firemode].range > VectorDist(ent->origin, target->origin) ) {
				if (fire_hand_type) {
					*fire_hand_type = ST_RIGHT_REACTION;
				}

				Com_DPrintf(DEBUG_GAME, "G_GetFiringTUs: right entnumber:%i firemode:%i entteam:%i\n", ent->number, *firemode, ent->team);
				return gi.csi->ods[RIGHT(ent)->item.m].fd[weapon_fd_idx][*firemode].time + sv_reaction_leftover->integer;
			}
		}
	}

	/* Fire the weapon in the left hand if everything is ok. */
	if (LEFT(ent) && (LEFT(ent)->item.m != NONE)
	 && gi.csi->ods[LEFT(ent)->item.t].weapon
	 && (!gi.csi->ods[LEFT(ent)->item.t].reload || LEFT(ent)->item.a > 0) ) {
		weapon_fd_idx = FIRESH_FiredefsIDXForWeapon(&gi.csi->ods[LEFT(ent)->item.m], LEFT(ent)->item.t);
		assert(weapon_fd_idx >= 0);

		if (reactionFiremode[ent->number][RF_HAND] == 1
		 && reactionFiremode[ent->number][RF_FM] >= 0
		 && reactionFiremode[ent->number][RF_FM] < MAX_FIREDEFS_PER_WEAPON) { /* If a LEFT-hand firemode is selected and sane. */
			*firemode = reactionFiremode[ent->number][RF_FM]; /* Get selected firemode for the weapon in the left hand. */

			if (gi.csi->ods[LEFT(ent)->item.m].fd[weapon_fd_idx][*firemode].time + sv_reaction_leftover->integer <= ent->TU
			 && gi.csi->ods[LEFT(ent)->item.m].fd[weapon_fd_idx][*firemode].range > VectorDist(ent->origin, target->origin)) {
				if (fire_hand_type) {
					*fire_hand_type = ST_LEFT_REACTION;
				}

				Com_DPrintf(DEBUG_GAME, "G_GetFiringTUs: left entnumber:%i firemode:%i entteam:%i\n", ent->number, *firemode, ent->team);
				return gi.csi->ods[LEFT(ent)->item.m].fd[weapon_fd_idx][*firemode].time + sv_reaction_leftover->integer;
			}
		}
	}
	return -1;
}

/**
 * @brief Check whether 'target' has just triggered any new reaction fire
 * @param[in] target The entity triggering fire
 * @returns qtrue if some entity initiated firing
 */
static qboolean G_CheckRFTrigger (edict_t *target)
{
	edict_t *ent;
	int i, tus;
	qboolean queued = qfalse;
	int firemode = -3;

	/* check all possible shooters */
	for (i = 0, ent = g_edicts; i < globals.num_edicts; i++, ent++) {
		/* not if ent has reaction target already */
		if (ent->reactionTarget)
			continue;

		/* check whether reaction fire is possible */
		if (!G_CanReactionFire(ent, target, NULL))
			continue;

		/* see how quickly ent can fire (if it can fire at all) */
		tus = G_GetFiringTUs(ent, target, NULL, &firemode);
		if (tus < 0)
			continue;

		/* Check if a firemode has been set by the client. */
		if (firemode < 0)
			continue;

		/* queue a reaction fire to take place */
		ent->reactionTarget = target;
		target->reactionAttacker = ent;
		ent->reactionTUs = max(0, target->TU - (tus / 4.0));
		ent->reactionNoDraw = qfalse;
		queued = qtrue;

		/* FIXME: generate an 'interrupt'? */
#ifdef DEBUG_REACTION
		Com_Printf("Entity %s begins reaction fire on %s\n", ent->chr.name, target->chr.name);
#endif
	}
	return queued;
}


/**
 * @brief Resolve the reaction fire for an entity, this checks that the entity can fire and then takes the shot
 * @param[in] ent The entity to resolve reaction fire for
 * @param[in] mock If true then don't actually fire
 * @return true if the entity fired (or would have fired if mock), false otherwise
 */
static qboolean G_ResolveRF (edict_t *ent, qboolean mock)
{
	player_t *player;
	int tus, fire_hand_type, team;
	int firemode = -1;
	qboolean tookShot;
	char reason[64];

	/* check whether this ent has a reaction fire queued */
	if (!ent->reactionTarget) {
#ifdef DEBUG_REACTION
		if (!mock)
			Com_Printf("Not resolving reaction fire for %s because ent has no target (which shouldn't happen)\n", ent->chr.name);
#endif
		return qfalse;
	}

	/* ent can't use RF if is in STATE_DAZED (flashbang impact) */
	if (ent->state & STATE_DAZED) {
#ifdef DEBUG
		Com_Printf("This entity is in STATE_DAZED, will not use reaction fire.\n");
#endif
		return qfalse;
	}

	/* ent can't take a reaction shot if it's not possible */
	if (!G_CanReactionFire(ent, ent->reactionTarget, reason)) {
		ent->reactionTarget = NULL;
#ifdef DEBUG_REACTION
		if (!mock)
			Com_Printf("Not resolving reaction fire for %s because '%s'\n", ent->chr.name, reason);
#endif
		return qfalse;
	}

	/* check the target is still alive */
	if (ent->reactionTarget->state & STATE_DEAD) {
		ent->reactionTarget = NULL;
#ifdef DEBUG_REACTION
		if (!mock)
			Com_Printf("Not resolving reaction fire for %s because target is dead\n", ent->chr.name);
#endif
		return qfalse;
	}

	/* check ent can fire (necessary? covered by G_CanReactionFire?) */
	tus = G_GetFiringTUs(ent, ent->reactionTarget, &fire_hand_type, &firemode);
	if (tus < 0) {
#ifdef DEBUG_REACTION
		if (!mock)
			Com_Printf("Cancelling resolution because %s cannot fire\n", ent->chr.name);
#endif
		return qfalse;
	}

	/* Check if a firemode has been set by the client. */
	if (firemode < 0) {
		if (!mock)
			Com_DPrintf(DEBUG_GAME, "G_ResolveRF: Cancelling resolution because %s has not set a reaction-firemode (%i).\n", ent->chr.name, firemode);
		return qfalse;
	}

	/* Get player. */
	player = game.players + ent->pnum;
	if (!player) {
#ifdef DEBUG_REACTION
		if (!mock)
			Com_Printf("Cancelling resolution because %s has no player\n", ent->chr.name);
#endif
		return qfalse;
	}

	/* take the shot */
	if (mock)
		/* if just pretending then this is far enough */
		return qtrue;

	/* Change active team for this shot only. */
	team = level.activeTeam;
	level.activeTeam = ent->team;

	/* take the shot */
	Com_DPrintf(DEBUG_GAME, "G_ResolveRF: reaction shot: fd:%i\n", firemode);
	tookShot = G_FireWithJudgementCall(player, ent->number, ent->reactionTarget->pos, fire_hand_type, firemode);

	/* Revert active team. */
	level.activeTeam = team;

	/* clear any shakenness */
	if (tookShot) {
		ent->state &= ~STATE_SHAKEN;
		reactionTUs[ent->number][REACT_FIRED] += 1; /* Save the fact that the ent has fired. */
	} else {
#ifdef DEBUG_REACTION
		Com_Printf("Cancelling resolution because %s judged it unwise to fire\n", ent->chr.name);
#endif
	}
	return tookShot;
}

/**
 * @brief check all entities to see whether target has caused reaction fire to resolve.
 * @param[in] target The entity that might be resolving reaction fire
 * @param[in] mock If true then don't actually fire
 * @returns whether any entity fired (or would fire) upon target
 * @sa G_ReactToMove
 * @sa G_ReactToPostFire
 */
static qboolean G_CheckRFResolution (edict_t *target, qboolean mock)
{
	edict_t *ent;
	int i;
	qboolean fired = qfalse, shoot = qfalse;

	/* check all possible shooters */
	for (i = 0, ent = g_edicts; i < globals.num_edicts; i++, ent++) {
		if (!ent->reactionTarget)
			continue;

		shoot = qfalse;

		/* check whether target has changed (i.e. the player is making a move with a different entity) */
		if (ent->reactionTarget != target) {
#ifdef DEBUG_REACTION
			if (!mock)
				Com_Printf("Resolving reaction fire for %s because target has changed\n", ent->chr.name);
#endif
			shoot = qtrue;
		}

		/* check whether target is out of time */
		if (!shoot  && ent->reactionTarget->TU < ent->reactionTUs) {
#ifdef DEBUG_REACTION
			if (!mock)
				Com_Printf("Resolving reaction fire for %s because target is out of time\n", ent->chr.name);
#endif
			shoot = qtrue;
		}

		/* okay do it */
		if (shoot)
			fired |= G_ResolveRF(ent, mock);
	}
	return fired;
}

/**
 * @brief Called when 'target' moves, possibly triggering or resolving reaction fire
 * @param[in] target The target entity
 * @param[in] mock If true then don't actually fire just say whether someone would
 * @returns true If any shots were (or would be) taken
 * @sa G_ClientMove
 */
qboolean G_ReactToMove (edict_t *target, qboolean mock)
{
	qboolean fired;

	/* Check to see whether this resolves any reaction fire */
	fired = G_CheckRFResolution(target, mock);

	/* Check to see whether this triggers any reaction fire */
	G_CheckRFTrigger(target);
	return fired;
}

/**
 * @brief Called when 'target' is about to fire, this forces a 'draw' to decide who gets to fire first
 * @param[in] target The entity about to fire
 * @sa G_ClientShoot
 */
static void G_ReactToPreFire (edict_t *target)
{
	edict_t *ent;
	int i, entTUs, targTUs;
	int firemode = -4;

	/* check all ents to see who wins and who loses a draw */
	for (i = 0, ent = g_edicts; i < globals.num_edicts; i++, ent++) {
		if (!ent->reactionTarget)
			continue;
		if (ent->reactionTarget != target) {
			/* if the entity has changed then resolve the reaction fire */
			G_ResolveRF(ent, qfalse);
			continue;
		}
		/* check this ent hasn't already lost the draw */
		if (ent->reactionNoDraw)
			continue;

		/* draw!! */
		entTUs = G_GetFiringTUs(ent, target, NULL, &firemode);
		targTUs = G_GetFiringTUs(target, ent, NULL, NULL);
		if ((entTUs < 0) || (firemode < 0)) {
			/* can't reaction fire if no TUs to fire */
			ent->reactionTarget = NULL;
			continue;
		}

		/* see who won */
		if (entTUs >= targTUs) {
			/* target wins, so delay ent */
			ent->reactionTUs = max(0, target->TU - (entTUs - targTUs)); /* target gets the difference in TUs */
			ent->reactionNoDraw = qtrue; 								/* so ent can't lose the TU battle again */
#ifdef DEBUG_REACTION
			Com_Printf("Entity %s was out-drawn\n", ent->chr.name);
#endif
		} else {
			/* ent wins so take the shot */
#ifdef DEBUG_REACTION
			Com_Printf("Entity %s won the draw\n", ent->chr.name);
#endif
			G_ResolveRF(ent, qfalse);
		}
	}
}

/**
 * @brief Called after 'target' has fired, this might trigger more reaction fire or resolve outstanding reaction fire (because target is out of time)
 * @param[in] target The entity that has just fired
 * @sa G_ClientShoot
 */
static void G_ReactToPostFire (edict_t *target)
{
	/* same as movement, but never mocked */
	G_ReactToMove(target, qfalse);
}

/**
 * @brief Called at the end of turn, all outstanding reaction fire is resolved
 * @sa G_ClientEndRound
 */
void G_ReactToEndTurn (void)
{
	edict_t *ent;
	int i;

	/* resolve all outstanding reaction firing if possible */
	for (i = 0, ent = g_edicts; i < globals.num_edicts; i++, ent++) {
		if (!ent->reactionTarget)
			continue;
#ifdef DEBUG_REACTION
		Com_Printf("Resolving reaction fire for %s because the other player ended their turn\n", ent->chr.name);
#endif
		G_ResolveRF(ent, qfalse);
		ent->reactionTarget = NULL;
	}
}
