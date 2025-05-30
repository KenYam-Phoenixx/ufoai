/**
 * @file
 */

/*
Copyright (C) 2002-2025 UFO: Alien Invasion.

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
#include "../../../../renderer/r_mesh.h"
#include "../../../../renderer/r_mesh_anim.h"
#include "e_event_actorshoot.h"

/**
 * @brief Decides if following events should be delayed. If the projectile has a speed value assigned, the
 * delay is relative to the distance the projectile flies. There are other fire definition related options
 * that might delay the execution of further events.
 */
int CL_ActorDoShootTime (const eventRegister_t* self, dbuffer* msg, eventTiming_t* eventTiming)
{
	int flags, dummy;
	int objIdx, surfaceFlags;
	int weap_fds_idx, fd_idx;
	shoot_types_t shootType;
	vec3_t muzzle, impact;
	int eventTime = eventTiming->shootTime;

	/* read data */
	NET_ReadFormat(msg, self->formatString, &dummy, &dummy, &dummy, &objIdx, &weap_fds_idx, &fd_idx, &shootType, &flags,
			&surfaceFlags, &muzzle, &impact, &dummy);

	const objDef_t* obj = INVSH_GetItemByIDX(objIdx);
	const fireDef_t* fd = FIRESH_GetFiredef(obj, weap_fds_idx, fd_idx);

	if (!(flags & SF_BOUNCED)) {
		/* shooting */
		if (fd->speed > 0.0 && !CL_OutsideMap(impact, UNIT_SIZE * 10)) {
			eventTiming->impactTime = eventTiming->shootTime + 1000 * VectorDist(muzzle, impact) / fd->speed;
		} else {
			eventTiming->impactTime = eventTiming->shootTime;
		}
		if (!cls.isOurRound())
			eventTiming->nextTime = CL_GetNextTime(self, eventTiming, eventTiming->impactTime + 1400);
		else
			eventTiming->nextTime = CL_GetNextTime(self, eventTiming, eventTiming->impactTime + 400);
		if (fd->delayBetweenShots > 0.0)
			eventTiming->shootTime += 1000 / fd->delayBetweenShots;
	} else {
		/* only a bounced shot */
		eventTime = eventTiming->impactTime;
		if (fd->speed > 0.0) {
			eventTiming->impactTime += 1000 * VectorDist(muzzle, impact) / fd->speed;
			eventTiming->nextTime = CL_GetNextTime(self, eventTiming, eventTiming->impactTime);
		}
	}
	eventTiming->parsedDeath = false;

	return eventTime;
}

/**
 * @brief Calculates the muzzle for the current weapon the actor is shooting with
 * @param[in] actor The actor that is shooting. Might not be @c nullptr
 * @param[out] muzzle The muzzle vector to spawn the particle at. Might not be @c nullptr. This is not
 * modified if there is no tag for the muzzle found for the weapon or item the actor has
 * in the hand (also see the given shoot type)
 * @param[in] shootType The shoot type to determine which tag of the actor should be used
 * to resolve the world coordinates. Also used to determine which item (or better which hand)
 * should be used to resolve the actor's item.
 */
static void CL_ActorGetMuzzle (const le_t* actor, vec3_t muzzle, shoot_types_t shootType)
{
	if (actor == nullptr)
		return;

	const Item* weapon;
	const char* tag;
	if (IS_SHOT_RIGHT(shootType)) {
		tag = "tag_rweapon";
		weapon = actor->getRightHandItem();
	} else {
		tag = "tag_lweapon";
		weapon = actor->getLeftHandItem();
	}

	if (!weapon || !weapon->def())
		return;

	const objDef_t* od = weapon->def();
	const model_t* model = cls.modelPool[od->idx];
	if (!model)
		Com_Error(ERR_DROP, "Model for item %s is not precached", od->id);

	/* not every weapon has a muzzle tag assigned */
	if (R_GetTagIndexByName(model, "tag_muzzle") == -1)
		return;

	float modifiedMatrix[16];
	if (!R_GetTagMatrix(actor->model1, tag, actor->as.frame, modifiedMatrix))
		Com_Error(ERR_DROP, "Could not find tag %s for actor model %s", tag, actor->model1->name);

	float mc[16];
	GLMatrixAssemble(actor->origin, actor->angles, mc);

	float matrix[16];
	GLMatrixMultiply(mc, modifiedMatrix, matrix);

	R_GetTagMatrix(model, "tag_muzzle", 0, modifiedMatrix);
	GLMatrixMultiply(matrix, modifiedMatrix, mc);

	muzzle[0] = mc[12];
	muzzle[1] = mc[13];
	muzzle[2] = mc[14];
}

/**
 * @brief Shoot with weapon.
 * @sa CL_ActorShoot
 * @sa CL_ActorShootHidden
 * @todo Improve detection of left- or right animation.
 * @sa EV_ACTOR_SHOOT
 */
void CL_ActorDoShoot (const eventRegister_t* self, dbuffer* msg)
{
	vec3_t muzzle, impact;
	int flags, normal, shooterEntnum, victimEntnum;
	int objIdx;
	int first;
	weaponFireDefIndex_t weapFdsIdx;
	fireDefIndex_t fdIdx;
	int surfaceFlags;
	shoot_types_t shootType;

	/* read data */
	NET_ReadFormat(msg, self->formatString, &shooterEntnum, &victimEntnum, &first, &objIdx, &weapFdsIdx, &fdIdx, &shootType, &flags, &surfaceFlags, &muzzle, &impact, &normal);

	le_t* leVictim;
	if (victimEntnum != SKIP_LOCAL_ENTITY) {
		leVictim = LE_Get(victimEntnum);
		if (!leVictim)
			LE_NotFoundError(victimEntnum);
	} else {
		leVictim = nullptr;
	}

	/* get shooter le */
	le_t* leShooter = LE_Get(shooterEntnum);

	/* get the fire def */
	const objDef_t* obj = INVSH_GetItemByIDX(objIdx);
	const fireDef_t* fd = FIRESH_GetFiredef(obj, weapFdsIdx, fdIdx);

	CL_ActorGetMuzzle(leShooter, muzzle, shootType);

	/* add effect le */
	LE_AddProjectile(fd, flags, muzzle, impact, normal, leVictim);

	/* start the sound */
	if ((first || !fd->soundOnce) && fd->fireSound != nullptr && !(flags & SF_BOUNCED))
		S_LoadAndPlaySample(fd->fireSound, muzzle, fd->fireAttenuation, SND_VOLUME_WEAPONS);

	/* do actor related stuff */
	if (!leShooter)
		return; /* maybe hidden or inuse is false? */

	if (!LE_IsActor(leShooter))
		Com_Error(ERR_DROP, "Can't shoot, LE not an actor (type: %i)", leShooter->type);

	/* no animations for hidden actors */
	if (leShooter->type == ET_ACTORHIDDEN)
		return;

	if (LE_IsDead(leShooter)) {
		Com_DPrintf(DEBUG_CLIENT, "Can't shoot, actor dead or stunned.\n");
		return;
	}

	/* Animate - we have to check if it is right or left weapon usage. */
	if (IS_SHOT_RIGHT(shootType)) {
		R_AnimChange(&leShooter->as, leShooter->model1, LE_GetAnim("shoot", leShooter->right, leShooter->left, leShooter->state));
		R_AnimAppend(&leShooter->as, leShooter->model1, LE_GetAnim("stand", leShooter->right, leShooter->left, leShooter->state));
	} else if (IS_SHOT_LEFT(shootType)) {
		R_AnimChange(&leShooter->as, leShooter->model1, LE_GetAnim("shoot", leShooter->left, leShooter->right, leShooter->state));
		R_AnimAppend(&leShooter->as, leShooter->model1, LE_GetAnim("stand", leShooter->left, leShooter->right, leShooter->state));
	} else if (IS_SHOT_HEADGEAR(shootType)) {
		if (fd->irgoggles) {
			leShooter->state |= RF_IRGOGGLESSHOT;
			if (LE_IsSelected(leShooter))
				refdef.rendererFlags |= RDF_IRGOGGLES;
		}
	} else {
		/* no animation for headgear (yet) */
		Com_Error(ERR_DROP, "CL_ActorDoShoot: Invalid shootType given (entnum: %i, shootType: %i).\n", shootType, shooterEntnum);
	}
}
