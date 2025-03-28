/**
 * @file
 * @brief Campaign mission code
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
#include "../cp_alien_interest.h"
#include "cp_mission_recon.h"
#include "cp_mission_terror.h"

/**
 * @brief Terror attack mission is over and is a success: change interest values.
 * @note Terror attack mission
 */
void CP_TerrorMissionIsSuccess (mission_t* mission)
{
	INT_ChangeIndividualInterest(-0.2f, INTERESTCATEGORY_BASE_ATTACK);
	INT_ChangeIndividualInterest(0.03f, INTERESTCATEGORY_HARVEST);

	CP_MissionRemove(mission);
}

/**
 * @brief Terror attack mission is over and is a failure: change interest values.
 * @note Terror attack mission
 */
void CP_TerrorMissionIsFailure (mission_t* mission)
{
	INT_ChangeIndividualInterest(0.05f, INTERESTCATEGORY_TERROR_ATTACK);
	INT_ChangeIndividualInterest(0.1f, INTERESTCATEGORY_INTERCEPT);
	INT_ChangeIndividualInterest(0.05f, INTERESTCATEGORY_BUILDING);
	INT_ChangeIndividualInterest(0.02f, INTERESTCATEGORY_BASE_ATTACK);

	CP_MissionRemove(mission);
}


/**
 * @brief Run when the mission is spawned.
 */
void CP_TerrorMissionOnSpawn (void)
{
	/* Prevent multiple terror missions per cycle by resetting interest. */
	INT_ChangeIndividualInterest(-1.0f, INTERESTCATEGORY_TERROR_ATTACK);
}


/**
 * @brief Start Terror attack mission.
 * @note Terror attack mission -- Stage 2
 */
void CP_TerrorMissionStart (mission_t* mission)
{
	const DateTime minMissionDelay(2, 0);
	const DateTime maxMissionDelay(3, 0);

	mission->stage = STAGE_TERROR_MISSION;

	mission->finalDate = ccs.date + Date_Random(minMissionDelay, maxMissionDelay);
	/* ufo becomes invisible on geoscape, but don't remove it from ufo global array (may reappear)*/
	if (mission->ufo)
		CP_UFORemoveFromGeoscape(mission, false);
	/* mission appear on geoscape, player can go there */
	CP_MissionAddToGeoscape(mission, false);
}

/**
 * @brief Choose a city for terror mission.
 * @return chosen city in ccs.cities
 */
static city_t* CP_ChooseCity (void)
{
	if (ccs.numCities > 0) {
		const int randnumber = rand() % ccs.numCities;

		return (city_t*) cgi->LIST_GetByIdx(ccs.cities, randnumber);
	}

	return nullptr;
}

static const mission_t* CP_TerrorInCity (const city_t* city)
{
	if (!city)
		return nullptr;

	MIS_Foreach(mission) {
		if (mission->category == INTERESTCATEGORY_TERROR_ATTACK
		 && mission->stage <= STAGE_TERROR_MISSION
		 && Vector2Equal(mission->pos, city->pos))
			return mission;
	}
	return nullptr;
}

/**
 * @brief Set Terror attack mission, and go to Terror attack mission pos.
 * @note Terror attack mission -- Stage 1
 * @note Terror missions can only take place in city: pick one in ccs.cities.
 */
static void CP_TerrorMissionGo (mission_t* mission)
{
	int counter;

	mission->stage = STAGE_MISSION_GOTO;

	/* Choose a map */
	for (counter = 0; counter < MAX_POS_LOOP; counter++) {
		city_t* city = CP_ChooseCity();

		if (!city)
			continue;

		if (GEO_PositionCloseToBase(city->pos))
			continue;

		if (!CP_ChooseMap(mission, city->pos))
			continue;

		if (CP_TerrorInCity(city))
			continue;

		Vector2Copy(city->pos, mission->pos);
		mission->data.city = city;
		mission->posAssigned = true;
		break;
	}
	if (counter >= MAX_POS_LOOP) {
		cgi->Com_DPrintf(DEBUG_CLIENT, "CP_TerrorMissionGo: Could not set position.\n");
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
 * @brief Determine what action should be performed when a Terror attack mission stage ends.
 * @param[in] mission Pointer to the mission which stage ended.
 */
void CP_TerrorMissionNextStage (mission_t* mission)
{
	switch (mission->stage) {
	case STAGE_NOT_ACTIVE:
		/* Create Terror attack mission */
		CP_MissionBegin(mission);
		break;
	case STAGE_COME_FROM_ORBIT:
		/* Go to mission */
		CP_TerrorMissionGo(mission);
		break;
	case STAGE_MISSION_GOTO:
		/* just arrived on a new Terror attack mission: start it */
		CP_TerrorMissionStart(mission);
		break;
	case STAGE_TERROR_MISSION:
		/* Leave earth */
		CP_ReconMissionLeave(mission);
		break;
	case STAGE_RETURN_TO_ORBIT:
		/* mission is over, remove mission */
		CP_TerrorMissionIsSuccess(mission);
		break;
	default:
		cgi->Com_Printf("CP_TerrorMissionNextStage: Unknown stage: %i, removing mission.\n", mission->stage);
		CP_MissionRemove(mission);
		break;
	}
}
