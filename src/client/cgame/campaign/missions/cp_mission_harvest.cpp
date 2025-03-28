/**
 * @file
 * @brief Campaign mission
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

#include "../../../DateTime.h"
#include "../../../cl_shared.h"
#include "../cp_campaign.h"
#include "../cp_geoscape.h"
#include "../cp_ufo.h"
#include "../cp_missions.h"
#include "../cp_time.h"
#include "../cp_xvi.h"
#include "../cp_alien_interest.h"
#include "cp_mission_harvest.h"
#include "cp_mission_recon.h"

/**
 * @brief Harvesting mission is over and is a success: change interest values.
 * @note Harvesting mission
 */
static void CP_HarvestMissionIsSuccess (mission_t* mission)
{
	INT_ChangeIndividualInterest(-0.3f, INTERESTCATEGORY_HARVEST);
	INT_ChangeIndividualInterest(0.2f, INTERESTCATEGORY_RECON);
	INT_ChangeIndividualInterest(0.05f, INTERESTCATEGORY_BUILDING);
	if (CP_IsXVIStarted())
		INT_ChangeIndividualInterest(0.1f, INTERESTCATEGORY_XVI);

	CP_MissionRemove(mission);
}

/**
 * @brief Harvesting mission is over and is a failure: change interest values.
 * @note Harvesting mission
 */
void CP_HarvestMissionIsFailure (mission_t* mission)
{
	INT_ChangeIndividualInterest(0.1f, INTERESTCATEGORY_INTERCEPT);
	INT_ChangeIndividualInterest(0.03f, INTERESTCATEGORY_BASE_ATTACK);
	INT_ChangeIndividualInterest(0.03f, INTERESTCATEGORY_TERROR_ATTACK);

	CP_MissionRemove(mission);
}

/**
 * @brief Start Harvesting mission.
 * @note Harvesting mission -- Stage 2
 */
static void CP_HarvestMissionStart (mission_t* mission)
{
	const DateTime minMissionDelay(2, 0);
	const DateTime maxMissionDelay(3, 0);

	mission->stage = STAGE_HARVEST;

	if (mission->ufo) {
		mission->finalDate = ccs.date + Date_Random(minMissionDelay, maxMissionDelay);
		/* ufo becomes invisible on geoscape, but don't remove it from ufo global array (may reappear)*/
		CP_UFORemoveFromGeoscape(mission, false);
	} else {
		/* Go to next stage on next frame */
		mission->finalDate = ccs.date;
	}

	/* mission appear on geoscape, player can go there */
	CP_MissionAddToGeoscape(mission, false);
}

/**
 * @brief Choose nation if needed for given mission.
 * @param[in] mission Pointer to the mission we are creating.
 * @param[out] nationList linkedList that will contain the name of the nation where the mission should take place.
 * @note nationList should be empty if no nation should be favoured.
 * @return True if nationList has been filled, false else.
 */
static bool CP_ChooseNation (const mission_t* mission, linkedList_t** nationList)
{
	int randomNumber, max = 0;
	/* Increase this factor to make probability to select non-infected nation higher
	 * Used to make sure that non-infected nation can still be attacked */
	const int OFFSET = 1;

	if (mission->ufo)
		return false;

	/* favour mission with higher XVI level */
	NAT_Foreach(nation) {
		const nationInfo_t* stats = NAT_GetCurrentMonthInfo(nation);
		max += OFFSET + stats->xviInfection;
	}

	randomNumber = (int) (frand() * (float) max);

	/* Select the corresponding nation */
	NAT_Foreach(nation) {
		const nationInfo_t* stats = NAT_GetCurrentMonthInfo(nation);
		randomNumber -= OFFSET + stats->xviInfection;
		if (randomNumber < 0) {
			cgi->LIST_AddString(nationList, nation->id);
			return true;
		}
	}

	return false;
}

/**
 * @brief Set Harvest mission, and go to mission pos.
 * @note Harvesting attack mission -- Stage 1
 * @todo Remove me when CP_XVIMissionGo will be implemented
 * This function should take a location close to an XVI infection point
 * see gameplay proposal on wiki
 */
void CP_HarvestMissionGo (mission_t* mission)
{
	mission->stage = STAGE_MISSION_GOTO;

	/* Choose a map */
	if (CP_ChooseMap(mission, nullptr)) {
		int counter;
		linkedList_t* nationList = nullptr;
		const bool nationTest = CP_ChooseNation(mission, &nationList);
		for (counter = 0; counter < MAX_POS_LOOP; counter++) {
			if (!CP_GetRandomPosOnGeoscapeWithParameters(mission->pos, mission->mapDef->terrains, mission->mapDef->cultures, mission->mapDef->populations, nationTest ? nationList : nullptr))
				continue;
			if (GEO_PositionCloseToBase(mission->pos))
				continue;
			mission->posAssigned = true;
			break;
		}
		if (counter >= MAX_POS_LOOP) {
			cgi->Com_Printf("CP_HarvestMissionGo: Error, could not set position.\n");
			CP_MissionRemove(mission);
			return;
		}
		cgi->LIST_Delete(&nationList);
	} else {
		cgi->Com_Printf("CP_HarvestMissionGo: No map found, remove mission.\n");
		CP_MissionRemove(mission);
		return;
	}

	if (mission->ufo) {
		CP_MissionDisableTimeLimit(mission);
		UFO_SendToDestination(mission->ufo, mission->pos);
	} else {
		/* Go to next stage on next frame */
		mission->finalDate = ccs.date;
	}
}

/**
 * @brief Determine what action should be performed when a Harvesting mission stage ends.
 * @param[in] mission Pointer to the mission which stage ended.
 */
void CP_HarvestMissionNextStage (mission_t* mission)
{
	switch (mission->stage) {
	case STAGE_NOT_ACTIVE:
		/* Create Harvesting mission */
		CP_MissionBegin(mission);
		break;
	case STAGE_COME_FROM_ORBIT:
		/* Go to mission */
		CP_HarvestMissionGo(mission);
		break;
	case STAGE_MISSION_GOTO:
		/* just arrived on a new Harvesting mission: start it */
		CP_HarvestMissionStart(mission);
		break;
	case STAGE_HARVEST:
		/* Leave earth */
		CP_ReconMissionLeave(mission);
		break;
	case STAGE_RETURN_TO_ORBIT:
		/* mission is over, remove mission */
		CP_HarvestMissionIsSuccess(mission);
		break;
	default:
		cgi->Com_Printf("CP_HarvestMissionNextStage: Unknown stage: %i, removing mission.\n", mission->stage);
		CP_MissionRemove(mission);
		break;
	}
}
