/**
 * @file cp_mission_baseattack.c
 * @brief Campaign mission code
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

#include "../../client.h"
#include "../../menu/m_main.h"
#include "../../menu/m_popup.h"
#include "../cp_campaign.h"
#include "../cp_map.h"
#include "../cp_ufo.h"
#include "../cp_missions.h"
#include "../cp_time.h"
#include "../cp_alien_interest.h"

/**
 * @brief This fake aircraft is used to assign soldiers for a base attack mission
 * @sa CP_BaseAttackStartMission
 * @sa AIR_AddToAircraftTeam
 */
static aircraft_t baseAttackFakeAircraft;

/**
 * @brief Base attack mission is over and is a success (from an alien point of view): change interest values.
 * @note Base attack mission
 * @sa CP_BaseAttackMissionStart
 */
void CP_BaseAttackMissionIsSuccess (mission_t *mission)
{
	CL_ChangeIndividualInterest(+0.3f, INTERESTCATEGORY_RECON);
	CL_ChangeIndividualInterest(+0.1f, INTERESTCATEGORY_TERROR_ATTACK);
	CL_ChangeIndividualInterest(+0.1f, INTERESTCATEGORY_HARVEST);

	CP_MissionRemove(mission);
}

/**
 * @brief Base attack mission is over and is a failure (from an alien point of view): change interest values.
 */
void CP_BaseAttackMissionIsFailure (mission_t *mission)
{
	base_t *base;

	base = (base_t *)mission->data;

	if (base)
		base->baseStatus = BASE_WORKING;
	ccs.mapAction = MA_NONE;

	/* we really don't want to use the fake aircraft anywhere */
	if (base)
		base->aircraftCurrent = &base->aircraft[0];
	ccs.missionAircraft = NULL;

	CL_ChangeIndividualInterest(0.05f, INTERESTCATEGORY_BUILDING);
	/* Restore some alien interest for base attacks that has been removed when
	 * mission has been created */
	CL_ChangeIndividualInterest(0.5f, INTERESTCATEGORY_BASE_ATTACK);

	/* reset ccs.selectedMission */
	MAP_NotifyMissionRemoved(mission);

	CP_MissionRemove(mission);
}

/**
 * @brief Base attack mission just started: change interest values.
 * @note This function is intended to avoid attack on several bases at the same time
 */
void CP_BaseAttackMissionStart (mission_t *mission)
{
	CL_ChangeIndividualInterest(-0.7f, INTERESTCATEGORY_BASE_ATTACK);
}

/**
 * @brief Base attack mission ends: UFO leave earth.
 * @note Base attack mission -- Stage 3
 */
void CP_BaseAttackMissionLeave (mission_t *mission)
{
	mission->stage = STAGE_RETURN_TO_ORBIT;

	if (mission->ufo) {
		CP_MissionDisableTimeLimit(mission);
		UFO_SetRandomDest(mission->ufo);
		/* Display UFO on geoscape if it is detected */
		mission->ufo->landed = qfalse;
	} else {
		/* Go to next stage on next frame */
		mission->finalDate = ccs.date;
	}
}

/**
 * @brief Base attack mission ends: UFO leave earth.
 * @note Base attack mission -- Stage 3
 * @note UFO attacking this base will be redirected when notify function will be called, don't set new destination here.
 */
void CP_BaseAttackMissionDestroyBase (mission_t *mission)
{
	base_t *base;

	base = (base_t *)mission->data;
	assert(base);
	/* Base attack is over, alien won */
	Com_sprintf(cp_messageBuffer, sizeof(cp_messageBuffer), _("Your base: %s has been destroyed! All employees killed and all equipment destroyed."), base->name);
	MS_AddNewMessage(_("Notice"), cp_messageBuffer, qfalse, MSG_STANDARD, NULL);
	B_Destroy(base);
	CL_GameTimeStop();

	/* we really don't want to use the fake aircraft anywhere */
	ccs.missionAircraft = NULL;

	/* HACK This hack is only needed until base will be really destroyed
	 * we must recalculate items in storage because of the items we collected on battlefield */
	B_UpdateStorageCap(base);
	base->aircraftCurrent = NULL;
	base->baseStatus = BASE_WORKING;
}

/**
 * @brief Start Base Attack.
 * @note Base attack mission -- Stage 2
 */
void CP_BaseAttackStartMission (mission_t *mission)
{
	int i;
	base_t *base = (base_t *)mission->data;
	linkedList_t *hiredSoldiersInBase = NULL, *pos;

	assert(base);

	mission->stage = STAGE_BASE_ATTACK;

	CP_MissionDisableTimeLimit(mission);

	if (mission->ufo) {
		/* ufo becomes invisible on geoscape, but don't remove it from ufo global array (may reappear)*/
		CP_UFORemoveFromGeoscape(mission, qfalse);
	}

	/* we always need at least one command centre in the base - because the
	 * phalanx soldiers have their starting positions here.
	 * There should also always be an entrance - the aliens start there
	 * but we don't need to check that as entrance can't be destroyed */
	if (!B_GetNumberOfBuildingsInBaseByBuildingType(base, B_COMMAND)) {
		/** @todo handle command centre properly */
		Com_DPrintf(DEBUG_CLIENT, "CP_BaseAttackStartMission: Base '%s' has no Command Center: it can't defend itself. Destroy base.\n", base->name);
		CP_BaseAttackMissionDestroyBase(mission);
		return;
	}

	base->baseStatus = BASE_UNDER_ATTACK;
	ccs.campaignStats.basesAttacked++;

#if 0
	/** @todo implement onattack: add it to basemanagement.ufo and implement functions */
	if (base->onAttack[0] != '\0')
		/* execute next frame */
		Cbuf_AddText(va("%s %i", base->onAttack, base->id));
#endif

	MAP_SelectMission(mission);
	ccs.selectedMission->active = qtrue;
	ccs.mapAction = MA_BASEATTACK;
	Com_DPrintf(DEBUG_CLIENT, "Base attack: %s at %.0f:%.0f\n", ccs.selectedMission->id, ccs.selectedMission->pos[0], ccs.selectedMission->pos[1]);

	/* Fill the fake aircraft */
	memset(&baseAttackFakeAircraft, 0, sizeof(baseAttackFakeAircraft));
	baseAttackFakeAircraft.homebase = base;
	VectorCopy(base->pos, baseAttackFakeAircraft.pos);				/* needed for transfer of alien corpses */
	/** @todo EMPL_ROBOT */
	baseAttackFakeAircraft.maxTeamSize = MAX_ACTIVETEAM;			/* needed to spawn soldiers on map */
	E_GetHiredEmployees(base, EMPL_SOLDIER, &hiredSoldiersInBase);

	if (!hiredSoldiersInBase) {
		Com_DPrintf(DEBUG_CLIENT, "CP_BaseAttackStartMission: Base '%s' has no soldiers: it can't defend itself. Destroy base.\n", base->name);
		CP_BaseAttackMissionDestroyBase(mission);
		return;
	}

	for (i = 0, pos = hiredSoldiersInBase; i < MAX_ACTIVETEAM && pos; i++, pos = pos->next)
		AIR_AddToAircraftTeam(&baseAttackFakeAircraft, (employee_t *)pos->data);

	LIST_Delete(&hiredSoldiersInBase);
	base->aircraftCurrent = &baseAttackFakeAircraft;
	ccs.missionAircraft = &baseAttackFakeAircraft;
	/** @todo remove me - this is not needed because we are using the base->aircraftCurrent
	 * pointer for resolving the aircraft*/
	ccs.interceptAircraft = &baseAttackFakeAircraft;	/* needed for updating soldier stats sa CL_UpdateCharacterStats*/
	B_SetCurrentSelectedBase(base);						/* needed for equipment menu */

	Com_sprintf(popupText, sizeof(popupText), _("Base '%s' is under attack! What to do ?"), base->name);
	MN_RegisterText(TEXT_POPUP, popupText);

	CL_GameTimeStop();
	MN_PushWindow("popup_baseattack", NULL);
}


/**
 * @brief Check and start baseattack missions
 * @sa CP_BaseAttackStartMission
 */
void CP_CheckBaseAttacks_f (void)
{
	linkedList_t *missionlist = ccs.missions;
	base_t *base = NULL;

	if (Cmd_Argc() == 2) {
		const int baseIDX = atoi(Cmd_Argv(1));
		if (baseIDX >= 0 && baseIDX < MAX_BASES)
			base = B_GetFoundedBaseByIDX(baseIDX);
		else
			Com_Printf("CP_CheckBaseAttacks_f: Invalid base index: %i\n", baseIDX);
	}

	while (missionlist) {
		mission_t *mission = (mission_t*) missionlist->data;

		if (mission->category == INTERESTCATEGORY_BASE_ATTACK && mission->stage == STAGE_BASE_ATTACK) {
			if (base && ((base_t*) mission->data != base))
				continue;
			CP_BaseAttackStartMission(mission);
		}
		missionlist = missionlist->next;
	}
}

/**
 * @brief Choose Base that will be attacked
 * @return Pointer to the base, NULL if no base set
 * @note Base attack mission -- Stage 1
 */
static base_t *CP_BaseAttackChooseBase (void)
{
	float randomNumber;
	float sum = 0.0f;
	int baseIdx;
	base_t *base = NULL;

	/* Choose randomly a base depending on alienInterest values for those bases */
	for (baseIdx = 0; baseIdx < ccs.numBases; baseIdx++) {
		base = B_GetFoundedBaseByIDX(baseIdx);
		if (!base)
			continue;
		sum += base->alienInterest;
	}
	randomNumber = frand() * sum;
	for (baseIdx = 0; baseIdx < ccs.numBases; baseIdx++) {
		base = B_GetFoundedBaseByIDX(baseIdx);
		if (!base)
			continue;
		randomNumber -= base->alienInterest;
		if (randomNumber < 0)
			break;
	}

	/* Make sure we have a base */
	assert(base && (randomNumber < 0));

	/* base is already under attack */
	if (base->baseStatus == BASE_UNDER_ATTACK)
		return NULL;
	/* base not (yet) working */
	if (!B_GetBuildingStatus(base, B_COMMAND))
		return NULL;

	return base;
}

/**
 * @brief Set base attack mission, and go to base position.
 * @note Base attack mission -- Stage 1
 */
static void CP_BaseAttackGoToBase (mission_t *mission)
{
	base_t *base;

	mission->stage = STAGE_MISSION_GOTO;

	base = CP_BaseAttackChooseBase();
	if (!base) {
		Com_Printf("CP_BaseAttackGoToBase: no base found\n");
		CP_MissionRemove(mission);
		return;
	}
	mission->data = (void *)base;

	mission->mapDef = Com_GetMapDefinitionByID("baseattack");
	if (!mission->mapDef) {
		CP_MissionRemove(mission);
		Com_Error(ERR_DROP, "Could not find mapdef baseattack");
		return;
	}

	Vector2Copy(base->pos, mission->pos);
	mission->posAssigned = qtrue;

	Com_sprintf(mission->location, sizeof(mission->location), "%s", base->name);

	if (mission->ufo) {
		CP_MissionDisableTimeLimit(mission);
		UFO_SendToDestination(mission->ufo, mission->pos);
	} else {
		/* Go to next stage on next frame */
		mission->finalDate = ccs.date;
	}
}

/**
 * @brief Fill an array with available UFOs for Base Attack mission type.
 * @param[in] mission Pointer to the mission we are currently creating.
 * @param[out] ufoTypes Array of ufoType_t that may be used for this mission.
 * @note Base Attack mission -- Stage 0
 * @return number of elements written in @c ufoTypes
 */
int CP_BaseAttackMissionAvailableUFOs (const mission_t const *mission, ufoType_t *ufoTypes)
{
	int num = 0;
	ufoTypes[num++] = UFO_FIGHTER;
	if (UFO_ShouldAppearOnGeoscape(UFO_BOMBER))
		ufoTypes[num++] = UFO_BOMBER;

	return num;
}

/**
 * @brief Determine what action should be performed when a Base Attack mission stage ends.
 * @param[in] mission Pointer to the mission which stage ended.
 */
void CP_BaseAttackMissionNextStage (mission_t *mission)
{
	switch (mission->stage) {
	case STAGE_NOT_ACTIVE:
		/* Create mission */
		CP_MissionCreate(mission);
		break;
	case STAGE_COME_FROM_ORBIT:
		/* Choose a base to attack and go to this base */
		CP_BaseAttackGoToBase(mission);
		break;
	case STAGE_MISSION_GOTO:
		/* just arrived on base location: attack it */
		CP_BaseAttackStartMission(mission);
		break;
	case STAGE_BASE_ATTACK:
		/* Leave earth */
		CP_BaseAttackMissionDestroyBase(mission);
		break;
	case STAGE_RETURN_TO_ORBIT:
		/* mission is over, remove mission */
		CP_BaseAttackMissionIsSuccess(mission);
		break;
	default:
		Com_Printf("CP_BaseAttackMissionNextStage: Unknown stage: %i, removing mission.\n", mission->stage);
		CP_MissionRemove(mission);
		break;
	}
}
