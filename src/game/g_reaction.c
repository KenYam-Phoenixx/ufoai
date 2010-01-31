/**
 * @file g_reaction.c
 * @brief Reaction fire code
 */

/*
Copyright (C) 2002-2009 UFO: Alien Invasion.

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

/**
 * @brief Get the weapon firing TUs of the item in the right hand of the edict.
 * @return -1 if no firedef was found for the item or the reaction fire mode is not activated for the right hand.
 * @todo why only right hand?
 */
static int G_GetFiringTUsForItem (const edict_t *ent, const edict_t *target, invList_t *invList)
{
	if (invList && invList->item.m && invList->item.t->weapon
	 && (!invList->item.t->reload || invList->item.a > 0)) {
		const fireDef_t *fdArray = FIRESH_FiredefForWeapon(&invList->item);
		if (fdArray == NULL)
			return -1;

		if (ent->chr.RFmode.hand == ACTOR_HAND_RIGHT && ent->chr.RFmode.fmIdx >= 0
		 && ent->chr.RFmode.fmIdx < MAX_FIREDEFS_PER_WEAPON) { /* If a RIGHT-hand firemode is selected and sane. */
			const int fmIdx = ent->chr.RFmode.fmIdx;

			if (fdArray[fmIdx].time + sv_reaction_leftover->integer <= ent->TU
			 && fdArray[fmIdx].range > VectorDist(ent->origin, target->origin)) {
				return fdArray[fmIdx].time + sv_reaction_leftover->integer;
			}
		}
	}

	return -1;
}

/**
 * @brief Get the number of TUs that ent needs to fire at target, also optionally return the firing hand. Used for reaction fire.
 * @param[in] ent The shooter entity.
 * @param[in] target The target entity.
 * @param[out] fire_hand_type If not NULL then this stores the hand (combined with the 'reaction' info) that the shooter will fire with.
 * @param[out] firemode The firemode that will be used for the shot.
 * @returns The number of TUs required to fire or -1 if firing is not possible
 * @sa G_CheckRFTrigger
 */
static int G_GetFiringTUs (edict_t *ent, edict_t *target, int *fire_hand_type, int *firemode)
{
	const int TUs = G_GetFiringTUsForItem(ent, target, RIGHT(ent));
	if (TUs >= 0) {
		/* Get selected (if any) firemode for the weapon */
		if (firemode)
			*firemode = ent->chr.RFmode.fmIdx;

		if (fire_hand_type)
			*fire_hand_type = ST_RIGHT_REACTION;
	}

	return TUs;
}

static qboolean G_ActorHasReactionFireEnabledWeapon (const edict_t *actor)
{
	const invList_t *invList;

	/* no weapon that can be used for reaction fire */
	if (!LEFT(actor) && !RIGHT(actor))
		return qfalse;

	invList = RIGHT(actor);
	if (invList && invList->item.t) {
		const fireDef_t *fd = FIRESH_FiredefForWeapon(&invList->item);
		if (fd->reaction)
			return qtrue;
	}

	invList = LEFT(actor);
	if (invList && invList->item.t) {
		const fireDef_t *fd = FIRESH_FiredefForWeapon(&invList->item);
		if (fd->reaction)
			return qtrue;
	}

	return qfalse;
}

/**
 * @brief Checks if the currently selected firemode is useable with the defined weapon.
 * @param[in] actor The actor to check the firemode for.
 */
static qboolean G_ActorHasWorkingFireModeSet (const edict_t *actor)
{
	const fireDef_t *fd;
	const chrFiremodeSettings_t *fmSettings = &actor->chr.RFmode;

	if (!SANE_FIREMODE(fmSettings))
		return qfalse;

	if (fmSettings->hand == ACTOR_HAND_LEFT) {
		const invList_t *invList = LEFT(actor);
		fd = FIRESH_FiredefForWeapon(&invList->item);
	} else if (fmSettings->hand == ACTOR_HAND_RIGHT) {
		const invList_t *invList = RIGHT(actor);
		fd = FIRESH_FiredefForWeapon(&invList->item);
	} else {
		fd = NULL;
	}

	if (fd == NULL)
		return qfalse;

	if (fd->obj->weapons[fd->weapFdsIdx] == fmSettings->weapon
	 && fmSettings->fmIdx < fd->obj->numFiredefs[fd->weapFdsIdx]) {
		return qtrue;
	}

	return qfalse;
}

qboolean G_CanEnableReactionFire (const edict_t *ent)
{
	/* check ent is a suitable shooter */
	if (!ent->inuse || !G_IsLivingActor(ent))
		return qfalse;

	if (G_MatchIsRunning() && ent->team != level.activeTeam)
		return qfalse;

	/* actor may not carry weapons at all - so no further checking is needed */
	if (!ent->chr.teamDef->weapons)
		return qfalse;

	if (!G_ActorHasReactionFireEnabledWeapon(ent))
		return qfalse;

	if (!G_ActorHasWorkingFireModeSet(ent))
		return qfalse;

	return qtrue;
}

/**
 * @brief Check whether ent can reaction fire at target, i.e. that it can see it and neither is dead etc.
 * @param[in] ent The entity that might be firing
 * @param[in] target The entity that might be fired at
 * @returns Whether 'ent' can actually fire at 'target'
 */
static qboolean G_CanReactionFire (edict_t *ent, edict_t *target)
{
	float actorVis;
	qboolean frustum;

	/* an entity can't reaction fire at itself */
	if (ent == target)
		return qfalse;

	/* Don't react in your own turn */
	if (ent->team == level.activeTeam)
		return qfalse;

	/* check ent has reaction fire enabled */
	if (!G_IsShaken(ent) && !(ent->state & STATE_REACTION))
		return qfalse;

	/* check in range and visible */
	if (VectorDistSqr(ent->origin, target->origin) > MAX_SPOT_DIST * MAX_SPOT_DIST)
		return qfalse;

	actorVis = G_ActorVis(ent->origin, target, qtrue);
	frustum = G_FrustumVis(ent, target->origin);
	if (actorVis <= 0.2 || !frustum)
		return qfalse;

	/* If reaction fire is triggered by a friendly unit
	 * and the shooter is still sane, don't shoot;
	 * well, if the shooter isn't sane anymore... */
	if (G_IsCivilian(target) || target->team == ent->team)
		if (!G_IsShaken(ent) || (float) ent->morale / mor_shaken->value > frand())
			return qfalse;

	/* okay do it then */
	return qtrue;
}

/**
 * @brief Check whether 'target' has just triggered any new reaction fire
 * @param[in] target The entity triggering fire
 * @returns qtrue if some entity initiated firing
 * @sa G_CanReactionFire
 * @sa G_GetFiringTUs
 */
static qboolean G_CheckRFTrigger (edict_t *target)
{
	edict_t *ent = NULL;
	int tus;
	qboolean queued = qfalse;
	int firemode = -1;

	/* check all possible shooters */
	while ((ent = G_EdictsGetNextInUse(ent))) {
		/* not if ent has reaction target already */
		if (ent->reactionTarget)
			continue;

		/* check whether reaction fire is possible */
		if (!G_CanReactionFire(ent, target))
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

		/** @todo generate an 'interrupt'? */
	}
	return queued;
}

/**
 * @brief Perform the reaction fire shot
 * @param[in] player The player this action belongs to (the human player or the ai)
 * @param[in] shooter The actor that is trying to shoot
 * @param[in] at Position to fire on.
 * @param[in] type What type of shot this is (left, right reaction-left etc...).
 * @param[in] firemode The firemode index of the ammo for the used weapon (objDef.fd[][x])  .
 * @return qtrue if everthing went ok (i.e. the shot(s) where fired ok), otherwise qfalse.
 * @sa G_ClientShoot
 */
static qboolean G_FireWithJudgementCall (player_t *player, edict_t *shooter, pos3_t at, int type, int firemode)
{
	const int minhit = shooter->reaction_minhit;
	shot_mock_t mock;
	int ff, i, maxff;

	if (G_IsInsane(shooter))
		maxff = 100;
	else if (G_IsRaged(shooter))
		maxff = 60;
	else if (G_IsPaniced(shooter))
		maxff = 30;
	else if (G_IsShaken(shooter))
		maxff = 15;
	else
		maxff = 5;

	memset(&mock, 0, sizeof(mock));
	for (i = 0; i < 100; i++)
		G_ClientShoot(player, shooter, at, type, firemode, &mock, qfalse, 0);

	ff = mock.friendCount + (G_IsAlien(shooter) ? 0 : mock.civilian);
	if (ff <= maxff && mock.enemyCount >= minhit)
		return G_ClientShoot(player, shooter, at, type, firemode, NULL, qfalse, 0);

	return qfalse;
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

	/* check whether this ent has a reaction fire queued */
	if (!ent->reactionTarget)
		return qfalse;

	/* ent can't use RF if is in STATE_DAZED (flashbang impact) */
	if (G_IsDazed(ent))
		return qfalse;

	/* ent can't take a reaction shot if it's not possible - and check that
	 * the target is still alive*/
	if (!G_CanReactionFire(ent, ent->reactionTarget) || G_IsDead(ent->reactionTarget)) {
		ent->reactionTarget = NULL;
		return qfalse;
	}

	/* check ent can fire (necessary? covered by G_CanReactionFire?) */
	tus = G_GetFiringTUs(ent, ent->reactionTarget, &fire_hand_type, &firemode);
	if (tus < 0)
		return qfalse;

	/* Check if a firemode has been set by the client. */
	if (firemode < 0)
		return qfalse;

	/* Get player. */
	player = G_PLAYER_FROM_ENT(ent);
	if (!player)
		return qfalse;

	/* take the shot */
	if (mock)
		/* if just pretending then this is far enough */
		return qtrue;

	/* Change active team for this shot only. */
	team = level.activeTeam;
	level.activeTeam = ent->team;

	/* take the shot */
	tookShot = G_FireWithJudgementCall(player, ent, ent->reactionTarget->pos, fire_hand_type, firemode);

	/* Revert active team. */
	level.activeTeam = team;

	/* clear any shakenness */
	if (tookShot) {
		ent->state &= ~STATE_SHAKEN;
		/* Save the fact that the ent has fired. */
		ent->reactionFired += 1;
	}
	return tookShot;
}

/**
 * @brief Check all entities to see whether target has caused reaction fire to resolve.
 * @param[in] target The entity that might be resolving reaction fire
 * @param[in] mock If true then don't actually fire
 * @returns whether any entity fired (or would fire) upon target
 * @sa G_ReactToMove
 * @sa G_ReactToPostFire
 */
static qboolean G_CheckRFResolution (edict_t *target, qboolean mock)
{
	edict_t *ent = NULL;
	qboolean fired = qfalse, shoot = qfalse;

	/* check all possible shooters */
	while ((ent = G_EdictsGetNextInUse(ent))) {
		if (!ent->reactionTarget)
			continue;

		shoot = qfalse;

		/* check whether target has changed (i.e. the player is making a move with a different entity) */
		if (ent->reactionTarget != target)
			shoot = qtrue;

		/* check whether target is out of time */
		if (!shoot && ent->reactionTarget->TU < ent->reactionTUs)
			shoot = qtrue;

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
	/* Check to see whether this resolves any reaction fire */
	const qboolean fired = G_CheckRFResolution(target, mock);

	/* Check to see whether this triggers any reaction fire */
	G_CheckRFTrigger(target);

	return fired;
}

/**
 * @brief Called when 'target' is about to fire, this forces a 'draw' to decide who gets to fire first
 * @param[in] target The entity about to fire
 * @sa G_ClientShoot
 */
void G_ReactToPreFire (edict_t *target)
{
	edict_t *ent = NULL;
	int entTUs, targTUs;
	int firemode = -4;

	/* check all ents to see who wins and who loses a draw */
	while ((ent = G_EdictsGetNextInUse(ent))) {
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
		if (entTUs < 0 || firemode < 0) {
			/* can't reaction fire if no TUs to fire */
			ent->reactionTarget = NULL;
			continue;
		}

		/* see who won */
		if (entTUs >= targTUs) {
			/* target wins, so delay ent */
			ent->reactionTUs = max(0, target->TU - (entTUs - targTUs)); /* target gets the difference in TUs */
			ent->reactionNoDraw = qtrue; 								/* so ent can't lose the TU battle again */
		} else {
			/* ent wins so take the shot */
			G_ResolveRF(ent, qfalse);
		}
	}
}

/**
 * @brief Called after 'target' has fired, this might trigger more reaction fire or resolve outstanding reaction fire (because target is out of time)
 * @param[in] target The entity that has just fired
 * @sa G_ClientShoot
 */
void G_ReactToPostFire (edict_t *target)
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
	edict_t *ent = NULL;

	/* resolve all outstanding reaction firing if possible */
	while ((ent = G_EdictsGetNextInUse(ent))) {
		if (!ent->reactionTarget)
			continue;

		G_ResolveRF(ent, qfalse);
		ent->reactionTarget = NULL;
		ent->reactionFired = 0;
	}
}

/**
 * @brief Guess! Reset all "shaken" states on end of turn?
 * @note Normally called on end of turn.
 * @sa G_ClientStateChange
 * @param[in] team Index of team to loop through.
 */
void G_ResetReactionFire (int team)
{
	edict_t *ent = NULL;

	while ((ent = G_EdictsGetNextLivingActorOfTeam(ent, team))) {
		/** @todo why do we send the state here and why do we change the "shaken" state? */
		ent->state &= ~STATE_SHAKEN;
		gi.AddEvent(G_TeamToPM(ent->team), EV_ACTOR_STATECHANGE);
		gi.WriteShort(ent->number);
		gi.WriteShort(ent->state);
	}
}
