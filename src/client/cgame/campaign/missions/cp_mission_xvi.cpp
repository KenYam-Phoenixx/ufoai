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

#include "cp_mission_xvi.h"
#include "../../../DateTime.h"
#include "../../../cl_shared.h"
#include "../cp_campaign.h"
#include "../cp_missions.h"
#include "../cp_time.h"
#include "../cp_alien_interest.h"
#include "../cp_ufo.h"
#include "cp_mission_recon.h"
#include "cp_mission_harvest.h"

/**
 * @brief XVI Spreading mission is over and is a success: change interest values.
 * @note XVI Spreading mission
 */
void CP_XVIMissionIsSuccess (mission_t* mission)
{
	INT_ChangeIndividualInterest(-0.3f, INTERESTCATEGORY_XVI);
	INT_ChangeIndividualInterest(0.1f, INTERESTCATEGORY_HARVEST);
	INT_ChangeIndividualInterest(0.05f, INTERESTCATEGORY_BUILDING);

	CP_MissionRemove(mission);
}

/**
 * @brief XVI Spreading mission is over and is a failure: change interest values.
 * @note XVI Spreading mission
 */
void CP_XVIMissionIsFailure (mission_t* mission)
{
	INT_ChangeIndividualInterest(0.1f, INTERESTCATEGORY_INTERCEPT);
	INT_ChangeIndividualInterest(0.05f, INTERESTCATEGORY_TERROR_ATTACK);
	INT_ChangeIndividualInterest(0.01f, INTERESTCATEGORY_BASE_ATTACK);

	CP_MissionRemove(mission);
}

/**
 * @brief Start XVI Spreading mission.
 * @note XVI Spreading mission -- Stage 2
 */
static void CP_XVIMissionStart (mission_t* mission)
{
	mission->stage = STAGE_SPREAD_XVI;

	if (mission->ufo) {
		const DateTime minMissionDelay(2, 0);
		const DateTime maxMissionDelay(3, 0);
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
 * @brief Determine what action should be performed when a XVI Spreading mission stage ends.
 * @param[in] mission Pointer to the mission which stage ended.
 */
void CP_XVIMissionNextStage (mission_t* mission)
{
	switch (mission->stage) {
	case STAGE_NOT_ACTIVE:
		/* Create XVI Spreading mission */
		CP_MissionBegin(mission);
		break;
	case STAGE_COME_FROM_ORBIT:
		/* Go to mission */
		CP_HarvestMissionGo(mission);
		break;
	case STAGE_MISSION_GOTO:
		/* just arrived on a new XVI Spreading mission: start it */
		CP_XVIMissionStart(mission);
		break;
	case STAGE_SPREAD_XVI:
		/* Leave earth */
		CP_ReconMissionLeave(mission);
		break;
	case STAGE_RETURN_TO_ORBIT:
		/* mission is over, remove mission */
		CP_XVIMissionIsSuccess(mission);
		break;
	default:
		cgi->Com_Printf("CP_XVIMissionNextStage: Unknown stage: %i, removing mission.\n", mission->stage);
		CP_MissionRemove(mission);
		break;
	}
}
