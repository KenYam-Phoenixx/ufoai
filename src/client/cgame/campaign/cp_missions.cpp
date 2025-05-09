/**
 * @file
 * @brief Campaign missions code
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

#include "../../DateTime.h"
#include "../../cl_shared.h"
#include "../../cl_team.h"
#include "../cl_game.h"
#include "../../ui/ui_dataids.h"
#include "cp_campaign.h"
#include "cp_character.h"
#include "cp_geoscape.h"
#include "cp_ufo.h"
#include "cp_alienbase.h"
#include "cp_alien_interest.h"
#include "cp_missions.h"
#include "cp_mission_triggers.h"
#include "cp_time.h"
#include "cp_xvi.h"
#include "save/save_missions.h"
#include "save/save_interest.h"
#include "cp_mission_callbacks.h"
#include "missions/cp_mission_baseattack.h"
#include "missions/cp_mission_buildbase.h"
#include "missions/cp_mission_harvest.h"
#include "missions/cp_mission_intercept.h"
#include "missions/cp_mission_recon.h"
#include "missions/cp_mission_rescue.h"
#include "missions/cp_mission_supply.h"
#include "missions/cp_mission_terror.h"
#include "missions/cp_mission_xvi.h"
#include "missions/cp_mission_ufocarrier.h"

/** @brief possible mission detection status */
typedef enum missionDetectionStatus_s {
	MISDET_CANT_BE_DETECTED,		/**< Mission can't be seen on geoscape */
	MISDET_ALWAYS_DETECTED,			/**< Mission is seen on geoscape, whatever it's position */
	MISDET_MAY_BE_DETECTED			/**< Mission may be seen on geoscape, if a probability test is done */
} missionDetectionStatus_t;

/** Maximum number of loops to choose a mission position (to avoid infinite loops) */
const int MAX_POS_LOOP = 10;

/** Condition limits for crashed UFOs - used for disassemlies */
static const float MIN_CRASHEDUFO_CONDITION = 0.2f;
static const float MAX_CRASHEDUFO_CONDITION = 0.81f;

/*====================================
*
* Prepare battlescape
*
====================================*/

/**
 * @brief Set some needed cvars from a battle definition
 * @param[in] battleParameters battle definition pointer with the needed data to set the cvars to
 * @sa CP_StartSelectedMission
 */
void BATTLE_SetVars (const battleParam_t* battleParameters)
{
	cgi->Cvar_SetValue("ai_singleplayeraliens", battleParameters->aliens);
	cgi->Cvar_SetValue("ai_numcivilians", battleParameters->civilians);
	cgi->Cvar_Set("ai_civilianteam", "%s", battleParameters->civTeam);
	cgi->Cvar_Set("ai_equipment", "%s", battleParameters->alienEquipment);

	/* now store the alien teams in the shared cgi->csi->struct to let the game dll
	 * have access to this data, too */
	cgi->csi->numAlienTeams = 0;
	for (int i = 0; i < battleParameters->alienTeamGroup->numAlienTeams; i++) {
		cgi->csi->alienTeams[i] = battleParameters->alienTeamGroup->alienTeams[i];
		cgi->csi->alienChrTemplates[i] = battleParameters->alienTeamGroup->alienChrTemplates[i];
		cgi->csi->numAlienTeams++;
		if (cgi->csi->numAlienTeams >= MAX_TEAMS_PER_MISSION)
			break;
	}
}

/**
 * @brief Select the mission type and start the map from mission definition
 * @param[in] mission Mission definition to start the map from
 * @param[in] battleParameters Context data of the battle
 * @sa CP_StartSelectedMission
 * @note Also sets the terrain textures
 * @sa Mod_LoadTexinfo
 * @sa B_AssembleMap_f
 */
void BATTLE_Start (mission_t* mission, const battleParam_t* battleParameters)
{
	assert(mission->mapDef->mapTheme);

	/* set the mapZone - this allows us to replace the ground texture
	 * with the suitable terrain texture - just use tex_terrain/dummy for the
	 * brushes you want the terrain textures on
	 * @sa R_ModLoadTexinfo */
	cgi->Cvar_Set("sv_mapzone", "%s", battleParameters->zoneType);

	/* do a quicksave */
	cgi->Cmd_ExecuteString("game_quicksave");

	if (mission->crashed)
		cgi->Cvar_Set("sv_hurtaliens", "1");
	else
		cgi->Cvar_Set("sv_hurtaliens", "0");

	cgi->Cvar_Set("r_overridematerial", "");

	/* base attack maps starts with a dot */
	if (mission->mapDef->mapTheme[0] == '.') {
		const base_t* base = mission->data.base;

		if (mission->category != INTERESTCATEGORY_BASE_ATTACK)
			cgi->Com_Printf("Baseattack map on non-baseattack mission! (id=%s, category=%d)\n", mission->id, mission->category);
		/* assemble a random base */
		if (!base)
			cgi->Com_Error(ERR_DROP, "Baseattack map without base!");
		/* base must be under attack and might not have been destroyed in the meantime. */
		char maps[2048];
		char coords[2048];
		B_AssembleMap(maps, sizeof(maps), coords, sizeof(coords), base);
		cgi->Cvar_Set("r_overridematerial", "baseattack");
		cgi->Cbuf_AddText("map %s \"%s\" \"%s\"\n", (GEO_IsNight(base->pos) ? "night" : "day"), maps, coords);

		return;
	}

	/* Set difficulty level for the battle */
	assert(ccs.curCampaign);
	cgi->Cvar_Delete("g_difficulty");
	cgi->Cvar_SetValue("g_difficulty", ccs.curCampaign->difficulty);

	const char* param = battleParameters->param ? battleParameters->param : (const char*)cgi->LIST_GetRandom(mission->mapDef->params);
	cgi->Cbuf_AddText("map %s %s %s\n", (GEO_IsNight(mission->pos) ? "night" : "day"),
		mission->mapDef->mapTheme, param ? param : "");
}

/**
 * @brief Check if an alien team category may be used for a mission category.
 * @param[in] cat Pointer to the alien team category.
 * @param[in] missionCat Mission category to check.
 * @return True if alien Category may be used for this mission category.
 */
static bool CP_IsAlienTeamForCategory (const alienTeamCategory_t* cat, const interestCategory_t missionCat)
{
	for (int i = 0; i < cat->numMissionCategories; i++) {
		if (missionCat == cat->missionCategories[i])
			return true;
	}

	return false;
}

/**
 * @brief Sets the alien races used for a mission.
 * @param[in] mission Pointer to the mission.
 * @param[out] battleParameters The battlescape parameter the alien team is stored in
 */
static void CP_SetAlienTeamByInterest (mission_t* mission, battleParam_t* battleParameters)
{
	const int MAX_AVAILABLE_GROUPS = 4;
	alienTeamGroup_t* availableGroups[MAX_AVAILABLE_GROUPS];
	int numAvailableGroup = 0;

	/* Find all available alien team groups */
	for (int i = 0; i < ccs.numAlienCategories; i++) {
		alienTeamCategory_t* cat = &ccs.alienCategories[i];

		/* Check if this alien team category may be used */
		if (!CP_IsAlienTeamForCategory(cat, mission->category))
			continue;

		/* Find all available team groups for current alien interest
		 * use mission->initialOverallInterest and not ccs.overallInterest:
		 * the alien team should not change depending on when you encounter it */
		for (int j = 0; j < cat->numAlienTeamGroups; j++) {
			if (cat->alienTeamGroups[j].minInterest <= mission->initialOverallInterest
			 && cat->alienTeamGroups[j].maxInterest >= mission->initialOverallInterest)
				availableGroups[numAvailableGroup++] = &cat->alienTeamGroups[j];
		}
	}

	if (!numAvailableGroup) {
		CP_MissionRemove(mission);
		cgi->Com_Error(ERR_DROP, "CP_SetAlienTeamByInterest: no available alien team for mission '%s': interest = %i -- category = %i",
			mission->id, mission->initialOverallInterest, mission->category);
	}

	/* Pick up one group randomly */
	int pick = rand() % numAvailableGroup;

	/* store this group for latter use */
	battleParameters->alienTeamGroup = availableGroups[pick];
}

/**
 * @brief Check if an alien equipment may be used with a mission.
 * @param[in] mission Pointer to the mission.
 * @param[in] equip Pointer to the alien equipment to check.
 * @param[in] equipPack Equipment definitions that may be used
 * @return True if equipment definition is selectable.
 */
static bool CP_IsAlienEquipmentSelectable (const mission_t* mission, const equipDef_t* equip, linkedList_t* equipPack)
{
	if (mission->initialOverallInterest > equip->maxInterest || mission->initialOverallInterest < equip->minInterest)
		return false;

	LIST_Foreach(equipPack, const char, name) {
		if (Q_strstart(equip->id, name))
			return true;
	}

	return false;
}

/**
 * @brief Set alien equipment for a mission (depends on the interest values)
 * @note This function is used to know which equipment pack described in equipment_missions.ufo should be used
 * @pre Alien team must be already chosen
 * @param[in] mission Pointer to the mission that generates the battle.
 * @param[in] equipPack Equipment definitions that may be used
 * @param[in] battleParameters Context data of the battle
 * @sa CP_SetAlienTeamByInterest
 */
static void CP_SetAlienEquipmentByInterest (const mission_t* mission, linkedList_t* equipPack, battleParam_t* battleParameters)
{
	int i, availableEquipDef = 0;

	/* look for all available fitting alien equipment definitions
	 * use mission->initialOverallInterest and not ccs.overallInterest: the alien equipment should not change depending on
	 * when you encounter it */
	for (i = 0; i < cgi->csi->numEDs; i++) {
		const equipDef_t* ed = &cgi->csi->eds[i];
		if (CP_IsAlienEquipmentSelectable(mission, ed, equipPack))
			availableEquipDef++;
	}

	cgi->Com_DPrintf(DEBUG_CLIENT, "CP_SetAlienEquipmentByInterest: %i available equipment packs for mission %s\n", availableEquipDef, mission->id);

	if (!availableEquipDef)
		cgi->Com_Error(ERR_DROP, "CP_SetAlienEquipmentByInterest: no available alien equipment for mission '%s'", mission->id);

	/* Choose an alien equipment definition -- between 0 and availableStage - 1 */
	const int randomNum = rand() % availableEquipDef;

	availableEquipDef = 0;
	for (i = 0; i < cgi->csi->numEDs; i++) {
		const equipDef_t* ed = &cgi->csi->eds[i];
		if (CP_IsAlienEquipmentSelectable(mission, ed, equipPack)) {
			if (availableEquipDef == randomNum) {
				Com_sprintf(battleParameters->alienEquipment, sizeof(battleParameters->alienEquipment), "%s", ed->id);
				break;
			}
			availableEquipDef++;
		}
	}
}

/**
 * @brief Set number of aliens in mission.
 * @param[in,out] mission Pointer to the mission that generates the battle.
 * @param[in,out] battleParam The battlescape parameter container
 * @sa CP_SetAlienTeamByInterest
 */
static void MIS_CreateAlienTeam (mission_t* mission, battleParam_t* battleParam)
{
	int numAliens;

	assert(mission->posAssigned);

	CP_SetAlienTeamByInterest(mission, battleParam);
	CP_SetAlienEquipmentByInterest(mission, ccs.alienCategories[battleParam->alienTeamGroup->categoryIdx].equipment, battleParam);

	const int min = battleParam->alienTeamGroup->minAlienCount;
	const int max = battleParam->alienTeamGroup->maxAlienCount;
	const int diff = max - min;

	numAliens = min + rand() % (diff + 1);
	numAliens = std::max(1, numAliens);
	if (mission->ufo && mission->ufo->maxTeamSize && numAliens > mission->ufo->maxTeamSize)
		numAliens = mission->ufo->maxTeamSize;
	if (numAliens > mission->mapDef->maxAliens)
		numAliens = mission->mapDef->maxAliens;
	battleParam->aliens = numAliens;
}

/**
 * @brief Create civilian team.
 * @param[in] mission Pointer to the mission that generates the battle
 * @param[out] param The battlescape parameter container
 */
static void CP_CreateCivilianTeam (const mission_t* mission, battleParam_t* param)
{
	nation_t* nation;

	assert(mission->posAssigned);

	param->civilians = GEO_GetCivilianNumberByPosition(mission->pos);

	nation = GEO_GetNation(mission->pos);
	param->nation = nation;
	if (mission->mapDef->civTeam != nullptr) {
		Q_strncpyz(param->civTeam, mission->mapDef->civTeam, sizeof(param->civTeam));
	} else if (nation) {
		/** @todo There should always be a nation, no? Otherwise the mission was placed wrong. */
		Q_strncpyz(param->civTeam, nation->id, sizeof(param->civTeam));
	} else {
		Q_strncpyz(param->civTeam, "europe", sizeof(param->civTeam));
	}
}

/**
 * @brief Create parameters needed for battle. This is the data that is used for starting
 * the tactical part of the mission.
 * @param[in] mission Pointer to the mission that generates the battle
 * @param[out] param The battle parameters to set
 * @param[in] aircraft the aircraft to go to the mission with
 * @sa CP_CreateAlienTeam
 * @sa CP_CreateCivilianTeam
 */
void CP_CreateBattleParameters (mission_t* mission, battleParam_t* param, const aircraft_t* aircraft)
{
	assert(mission->posAssigned);
	assert(mission->mapDef);

	MIS_CreateAlienTeam(mission, param);
	CP_CreateCivilianTeam(mission, param);

	/* Reset parameters */
	cgi->Free(param->param);
	param->param = nullptr;
	param->retriable = true;

	cgi->Cvar_Set("rm_ufo", "");
	cgi->Cvar_Set("rm_drop", "");
	cgi->Cvar_Set("rm_crashed", "");

	param->mission = mission;
	const byte* color = GEO_GetColor(mission->pos, MAPTYPE_TERRAIN, nullptr);
	param->zoneType = cgi->csi->terrainDefs.getTerrainName(color); /* store to terrain type for texture replacement */
	/* Hack: Alienbase is fully indoors (underground) so no weather effects, maybe this should be a mapdef property? */
	if (mission->category == INTERESTCATEGORY_ALIENBASE)
		cgi->Cvar_Set("r_weather", "0");
	else
		cgi->Cvar_Set("r_weather", "%s", cgi->csi->terrainDefs.getWeather(color));

	/* Is there a UFO to recover ? */
	if (mission->ufo) {
		const aircraft_t* ufo = mission->ufo;
		const char* shortUFOType;
		float ufoCondition;

		if (mission->crashed) {
			shortUFOType = cgi->Com_UFOCrashedTypeToShortName(ufo->getUfoType());
			/* Set random map UFO if this is a random map */
			if (mission->mapDef->mapTheme[0] == '+') {
				/* set battleParameters.param to the ufo type: used for ufocrash random map */
				if (Q_streq(mission->mapDef->id, "ufocrash"))
					param->param = cgi->PoolStrDup(shortUFOType, cp_campaignPool, 0);
			}
			ufoCondition = frand() * (MAX_CRASHEDUFO_CONDITION - MIN_CRASHEDUFO_CONDITION) + MIN_CRASHEDUFO_CONDITION;
		} else {
			shortUFOType = cgi->Com_UFOTypeToShortName(ufo->getUfoType());
			ufoCondition = 1.0f;
		}

		Com_sprintf(mission->onwin, sizeof(mission->onwin), "ui_push popup_uforecovery \"%s\" \"%s\" \"%s\" \"%s\" %3.2f",
			UFO_GetName(ufo), ufo->id, ufo->model, (mission->crashed ? _("landed") : _("crashed")), ufoCondition);
		/* Set random map UFO if this is a random map */
		if (mission->mapDef->mapTheme[0] == '+' && !cgi->LIST_IsEmpty(mission->mapDef->ufos)) {
			/* set rm_ufo to the ufo type used */
			cgi->Cvar_Set("rm_ufo", "%s", cgi->Com_GetRandomMapAssemblyNameForCraft(shortUFOType));
		}
	}

	/* Set random map aircraft if this is a random map */
	if (mission->mapDef->mapTheme[0] == '+') {
		if (mission->category == INTERESTCATEGORY_RESCUE) {
			cgi->Cvar_Set("rm_crashed", "%s", cgi->Com_GetRandomMapAssemblyNameForCrashedCraft(mission->data.aircraft->id));
		}
		if (cgi->LIST_ContainsString(mission->mapDef->aircraft, aircraft->id))
			cgi->Cvar_Set("rm_drop", "%s", cgi->Com_GetRandomMapAssemblyNameForCraft(aircraft->id));
	}
}

/*====================================
*
* Get information from mission list
*
====================================*/

/**
 * @brief Get a mission in ccs.missions by Id without error messages.
 * @param[in] missionId Unique string id for the mission
 * @return pointer to the mission or nullptr if Id was nullptr or mission not found
 */
mission_t* CP_GetMissionByIDSilent (const char* missionId)
{
	if (!missionId)
		return nullptr;

	MIS_Foreach(mission) {
		if (Q_streq(mission->id, missionId))
			return mission;
	}

	return nullptr;
}

/**
 * @brief Get a mission in ccs.missions by Id.
 * @param[in] missionId Unique string id for the mission
 * @return pointer to the mission or nullptr if Id was nullptr or mission not found
 */
mission_t* CP_GetMissionByID (const char* missionId)
{
	mission_t* mission = CP_GetMissionByIDSilent(missionId);

	if (!missionId)
		cgi->Com_Printf("CP_GetMissionByID: missionId was nullptr!\n");
	else if (!mission)
		cgi->Com_Printf("CP_GetMissionByID: Could not find mission %s\n", missionId);

	return mission;
}

/**
 * @brief Find mission corresponding to idx
 */
mission_t* MIS_GetByIdx (int id)
{
	MIS_Foreach(mission) {
		if (mission->idx == id)
			return mission;
	}

	return nullptr;
}

/**
 * @brief Find idx corresponding to mission
 */
int MIS_GetIdx (const mission_t* mis)
{
	return mis->idx;
}

/**
 * @brief Returns a short translated name for a mission
 * @param[in] mission Pointer to the mission to get name for
 */
const char* MIS_GetName (const mission_t* mission)
{
	assert(mission);

	if (mission->category == INTERESTCATEGORY_RESCUE)
		if (mission->data.aircraft)
			return va(_("Crashed %s"), mission->data.aircraft->name);

	const nation_t* nation = GEO_GetNation(mission->pos);
	switch (mission->stage) {
	case STAGE_TERROR_MISSION:
		if (mission->data.city)
			return va(_("Alien terror in %s"), _(mission->data.city->name));
		else
			return _("Alien terror");
	case STAGE_BASE_ATTACK:
		if (mission->data.base)
			return va(_("Base attacked: %s"), mission->data.base->name);
		else
			return _("Base attack");
	case STAGE_BASE_DISCOVERED:
		if (nation)
			return va(_("Alien base in %s"), _(nation->name));
		else
			return _("Alien base");
	default:
		break;
	}

	/* mission has an ufo */
	if (mission->ufo) {
		/* which is crashed */
		if (mission->crashed)
			return va(_("Crashed %s"), UFO_GetName(mission->ufo));
		/* not crashed but detected */
		if (mission->ufo->detected && mission->ufo->landed)
			return va(_("Landed %s"), UFO_GetName(mission->ufo));
	}

	/* we know nothing about the mission, maybe only it's location */
	if (nation)
		return va(_("Alien activity in %s"), _(nation->name));
	else
		return _("Alien activity");
}

#ifdef DEBUG
/**
 * @brief Return Name of the category of a mission.
 * @note Not translated yet - only for console usage
 */
static const char* CP_MissionStageToName (const missionStage_t stage)
{
	switch (stage) {
	case STAGE_NOT_ACTIVE:
		return "Not active yet";
	case STAGE_COME_FROM_ORBIT:
		return "UFO coming from orbit";
	case STAGE_RECON_AIR:
		return "Aerial recon underway";
	case STAGE_MISSION_GOTO:
		return "Going to mission position";
	case STAGE_RECON_GROUND:
		return "Ground recon mission underway";
	case STAGE_TERROR_MISSION:
		return "Terror mission underway";
	case STAGE_BUILD_BASE:
		return "Building base";
	case STAGE_BASE_ATTACK:
		return "Attacking a base";
	case STAGE_SUBVERT_GOV:
		return "Subverting a government";
	case STAGE_SUPPLY:
		return "Supplying";
	case STAGE_SPREAD_XVI:
		return "Spreading XVI";
	case STAGE_INTERCEPT:
		return "Intercepting or attacking installation";
	case STAGE_RETURN_TO_ORBIT:
		return "Leaving earth";
	case STAGE_BASE_DISCOVERED:
		return "Base visible";
	case STAGE_HARVEST:
		return "Harvesting";
	case STAGE_OVER:
		return "Mission over";
	}

	/* Can't reach this point */
	return "";
}
#endif

/**
 * @brief Count the number of mission active and displayed on geoscape.
 * @return Number of active mission visible on geoscape
 */
int CP_CountMissionOnGeoscape (void)
{
	int counterVisibleMission = 0;

	MIS_Foreach(mission) {
		if (mission->onGeoscape) {
			counterVisibleMission++;
		}
	}

	return counterVisibleMission;
}


/*====================================
* Functions relative to geoscape
*====================================*/

/**
 * @brief Get mission model that should be shown on the geoscape
 * @param[in] mission Pointer to the mission drawn on geoscape
 * @sa GEO_DrawMarkers
 */
const char* MIS_GetModel (const mission_t* mission)
{
	/* Mission shouldn't be drawn on geoscape if mapDef is not defined */
	assert(mission->mapDef);

	if (mission->crashed)
		return "geoscape/ufocrash";

	if (mission->mapDef->storyRelated && mission->category != INTERESTCATEGORY_ALIENBASE)
		return "geoscape/icon_story";

	cgi->Com_DPrintf(DEBUG_CLIENT, "Mission is %s, %d\n", mission->id, mission->category);
	switch (mission->category) {
	case INTERESTCATEGORY_RESCUE:
		return "geoscape/icon_rescue";
	case INTERESTCATEGORY_BUILDING:
	case INTERESTCATEGORY_SUBVERT:
		return "geoscape/icon_build_alien_base";
	case INTERESTCATEGORY_ALIENBASE:
		/** @todo we have two different alienbase models */
		return "geoscape/alienbase";
	case INTERESTCATEGORY_RECON:
		return "geoscape/icon_recon";
	case INTERESTCATEGORY_XVI:
		return "geoscape/icon_xvi";
	case INTERESTCATEGORY_HARVEST:
		return "geoscape/icon_harvest";
	case INTERESTCATEGORY_UFOCARRIER:
		return "geoscape/icon_ufocarrier";
	case INTERESTCATEGORY_TERROR_ATTACK:
		return "geoscape/icon_terror";
	/* Should not be reached, these mission categories are not drawn on geoscape */
	case INTERESTCATEGORY_BASE_ATTACK:
		return "geoscape/base2";
	case INTERESTCATEGORY_SUPPLY:
	case INTERESTCATEGORY_INTERCEPT:
	case INTERESTCATEGORY_INTERCEPTBOMBING:
	case INTERESTCATEGORY_NONE:
	case INTERESTCATEGORY_MAX:
		break;
	}
	cgi->Com_Error(ERR_DROP, "Unknown mission interest category");
}

/**
 * @brief Check if a mission should be visible on geoscape.
 * @param[in] mission Pointer to mission we want to check visibility.
 * @return see missionDetectionStatus_t.
 */
static missionDetectionStatus_t CP_CheckMissionVisibleOnGeoscape (const mission_t* mission)
{
	/* This function could be called before position of the mission is defined */
	if (!mission->posAssigned)
		return MISDET_CANT_BE_DETECTED;

	if (mission->crashed)
		return MISDET_ALWAYS_DETECTED;

	if (mission->ufo && mission->ufo->detected && mission->ufo->landed)
		return MISDET_ALWAYS_DETECTED;

	if (mission->category == INTERESTCATEGORY_RESCUE)
		return MISDET_ALWAYS_DETECTED;

	switch (mission->stage) {
	case STAGE_TERROR_MISSION:
	case STAGE_BASE_DISCOVERED:
		return MISDET_ALWAYS_DETECTED;
	case STAGE_RECON_GROUND:
	case STAGE_SUBVERT_GOV:
	case STAGE_SPREAD_XVI:
	case STAGE_HARVEST:
		if (RADAR_CheckRadarSensored(mission->pos))
			return MISDET_MAY_BE_DETECTED;
		break;
	case STAGE_COME_FROM_ORBIT:
	case STAGE_MISSION_GOTO:
	case STAGE_INTERCEPT:
	case STAGE_RECON_AIR:
	case STAGE_RETURN_TO_ORBIT:
	case STAGE_NOT_ACTIVE:
	case STAGE_BUILD_BASE:
	case STAGE_BASE_ATTACK:
	case STAGE_OVER:
	case STAGE_SUPPLY:
		break;
	}
	return MISDET_CANT_BE_DETECTED;
}

/**
 * @brief Removes a mission from geoscape: make it non visible and call notify functions
 */
void CP_MissionRemoveFromGeoscape (mission_t* mission)
{
	if (!mission->onGeoscape && mission->category != INTERESTCATEGORY_BASE_ATTACK)
		return;

	mission->onGeoscape = false;

	/* Notifications */
	GEO_NotifyMissionRemoved(mission);
	AIR_AircraftsNotifyMissionRemoved(mission);
}

/**
 * @brief Decides which message level to take for the given mission
 * @param[in] mission The mission to chose the message level for
 * @return The message level
 */
static inline messageType_t CP_MissionGetMessageLevel (const mission_t* mission)
{
	switch (mission->stage) {
	case STAGE_BASE_ATTACK:
		return MSG_BASEATTACK;
	case STAGE_TERROR_MISSION:
		return MSG_TERRORSITE;
	default:
		break;
	}

	if (mission->crashed)
		return MSG_CRASHSITE;
	return MSG_STANDARD;
}

/**
 * @brief Assembles a message that is send to the gamer once the given mission is added to geoscape
 * @param[in] mission The mission that was added to the geoscape and for that a message should be created
 * @return The pointer to the static buffer that holds the message.
 */
static inline const char* CP_MissionGetMessage (const mission_t* mission)
{
	if (mission->category == INTERESTCATEGORY_RESCUE) {
		Com_sprintf(cp_messageBuffer, sizeof(cp_messageBuffer), _("Go on a rescue mission for %s to save your soldiers, some of whom may still be alive."), mission->data.aircraft->name);
	} else {
		const nation_t* nation = GEO_GetNation(mission->pos);
		if (nation)
			Com_sprintf(cp_messageBuffer, sizeof(cp_messageBuffer), _("Alien activity has been detected in %s."), _(nation->name));
		else
			Com_sprintf(cp_messageBuffer, sizeof(cp_messageBuffer), _("Alien activity has been detected."));
	}
	return cp_messageBuffer;
}

/**
 * @brief Add a mission to geoscape: make it visible and stop time
 * @param[in] mission Pointer to added mission.
 * @param[in] force true if the mission should be added even for mission needing a probabilty test to be seen.
 * @sa CP_CheckNewMissionDetectedOnGeoscape
 */
void CP_MissionAddToGeoscape (mission_t* mission, bool force)
{
	const missionDetectionStatus_t status = CP_CheckMissionVisibleOnGeoscape(mission);

	/* don't show a mission spawned by a undetected ufo, unless forced : this may be done at next detection stage */
	if (status == MISDET_CANT_BE_DETECTED || (!force && status == MISDET_MAY_BE_DETECTED && mission->ufo && !mission->ufo->detected))
		return;

#ifdef DEBUG
	/* UFO that spawned this mission should be close of mission */
	if (mission->ufo && ((fabs(mission->ufo->pos[0] - mission->pos[0]) > 1.0f) || (fabs(mission->ufo->pos[1] - mission->pos[1]) > 1.0f))) {
		cgi->Com_Printf("Error: mission (stage: %s) spawned is not at the same location as UFO\n", CP_MissionStageToName(mission->stage));
	}
#endif

	/* Notify the player */
	MS_AddNewMessage(_("Notice"), CP_MissionGetMessage(mission), CP_MissionGetMessageLevel(mission));

	mission->onGeoscape = true;
	CP_GameTimeStop();
	GEO_UpdateGeoscapeDock();
}

/**
 * @brief Check if mission has been detected by radar.
 * @note called every @c DETECTION_INTERVAL.
 * @sa RADAR_CheckUFOSensored
 * @return True if a new mission was detected.
 */
bool CP_CheckNewMissionDetectedOnGeoscape (void)
{
	/* Probability to detect UFO each DETECTION_INTERVAL
	 * This correspond to 40 percents each 30 minutes (coded this way to be able to
	 * change DETECTION_INTERVAL without changing the way radar works) */
	const float missionDetectionProbability = 0.000125f * DETECTION_INTERVAL;
	bool newDetection = false;

	MIS_Foreach(mission) {
		const missionDetectionStatus_t status = CP_CheckMissionVisibleOnGeoscape(mission);

		/* only check mission that can be detected, and that are not already detected */
		if (status != MISDET_MAY_BE_DETECTED || mission->onGeoscape)
			continue;

		/* if there is a ufo assigned, it must not be detected */
		assert(!mission->ufo || !mission->ufo->detected);

		if (frand() <= missionDetectionProbability) {
			/* if mission has a UFO, detect the UFO when it takes off */
			if (mission->ufo)
				UFO_DetectNewUFO(mission->ufo);

			/* mission is detected */
			CP_MissionAddToGeoscape(mission, true);

			newDetection = true;
		}
	}
	return newDetection;
}

/**
 * @brief Update all mission visible on geoscape (in base radar range).
 * @note you can't see a mission with aircraft radar.
 * @sa CP_CheckMissionAddToGeoscape
 */
void CP_UpdateMissionVisibleOnGeoscape (void)
{
	MIS_Foreach(mission) {
		if (mission->onGeoscape && CP_CheckMissionVisibleOnGeoscape(mission) == MISDET_CANT_BE_DETECTED) {
			/* remove a mission when radar is destroyed */
			CP_MissionRemoveFromGeoscape(mission);
		} else if (!mission->onGeoscape && CP_CheckMissionVisibleOnGeoscape(mission) == MISDET_ALWAYS_DETECTED) {
			/* only show mission that are always detected: mission that have a probability to be detected will be
			 * tested at next half hour */
			CP_MissionAddToGeoscape(mission, false);
		}
	}
}

/**
 * @brief Removes (temporarily or permanently) a UFO from geoscape: make it land and call notify functions.
 * @param[in] mission Pointer to mission.
 * @param[in] destroyed True if the UFO has been destroyed, false if it's only landed.
 * @note We don't destroy the UFO if mission is not deleted because we can use it later, e.g. if it takes off.
 * @sa UFO_RemoveFromGeoscape
 */
void CP_UFORemoveFromGeoscape (mission_t* mission, bool destroyed)
{
	assert(mission->ufo);

	/* UFO is landed even if it's destroyed: crash site mission is spawned */
	mission->ufo->landed = true;

	/* Notications */
	AIR_AircraftsNotifyUFORemoved(mission->ufo, destroyed);
	GEO_NotifyUFORemoved(mission->ufo, destroyed);
	AIRFIGHT_RemoveProjectileAimingAircraft(mission->ufo);

	if (destroyed) {
		/* remove UFO from radar and update idx of ufo in radar array */
		RADAR_NotifyUFORemoved(mission->ufo, true);

		/* Update UFO idx */
		/** @todo remove me once the ufo list is a linked list */
		MIS_Foreach(removedMission) {
			if (removedMission->ufo && (removedMission->ufo > mission->ufo))
				removedMission->ufo--;
		}

		UFO_RemoveFromGeoscape(mission->ufo);
		mission->ufo = nullptr;
	} else if (mission->ufo->detected && !RADAR_CheckRadarSensored(mission->ufo->pos)) {
		/* maybe we use a high speed time: UFO was detected, but flied out of radar
		 * range since last calculation, and mission is spawned outside radar range */
		RADAR_NotifyUFORemoved(mission->ufo, false);
	}
}


/*====================================
*
* Handling missions
*
*====================================*/

/**
 * @brief Removes a mission from mission global array.
 * @sa UFO_RemoveFromGeoscape
 */
void CP_MissionRemove (mission_t* mission)
{
	/* Destroy UFO */
	if (mission->ufo)
		CP_UFORemoveFromGeoscape(mission, true);		/* for the notifications */

	/* Remove from battle parameters */
	if (mission == ccs.battleParameters.mission)
		ccs.battleParameters.mission = nullptr;

	/* Notifications */
	CP_MissionRemoveFromGeoscape(mission);

	if (!cgi->LIST_Remove(&ccs.missions, mission))
		cgi->Com_Error(ERR_DROP, "CP_MissionRemove: Could not find mission '%s' to remove.\n", mission->id);
}

/**
 * @brief Disable time limit for given mission.
 * @note This function is used for better readibility.
 * @sa CP_CheckNextStageDestination
 * @sa CP_CheckMissionLimitedInTime
 */
void CP_MissionDisableTimeLimit (mission_t* mission)
{
	mission->finalDate = DateTime(0, 0);
}

/**
 * @brief Check if mission should end because of limited time.
 * @note This function is used for better readibility.
 * @sa CP_MissionDisableTimeLimit
 * @return true if function should end after finalDate
 */
bool CP_CheckMissionLimitedInTime (const mission_t* mission)
{
	return mission->finalDate.getDateAsDays() != 0;
}


/*====================================
*
* Notifications
*
====================================*/

/**
 * @brief Notify that a base has been removed.
 */
void CP_MissionNotifyBaseDestroyed (const base_t* base)
{
	MIS_Foreach(mission) {
		/* Check if this is a base attack mission attacking this base */
		if (mission->category == INTERESTCATEGORY_BASE_ATTACK && mission->data.base) {
			if (base == mission->data.base) {
				/* Aimed base has been destroyed, abort mission */
				CP_BaseAttackMissionLeave(mission);
			}
		}
	}
}

/**
 * @brief Notify missions that an installation has been destroyed.
 * @param[in] installation Pointer to the installation that has been destroyed.
 */
void CP_MissionNotifyInstallationDestroyed (const installation_t* installation)
{
	MIS_Foreach(mission) {
		if (mission->category == INTERESTCATEGORY_INTERCEPT && mission->data.installation) {
			if (mission->data.installation == installation)
				CP_InterceptMissionLeave(mission, false);
		}
	}
}

/*====================================
*
* Functions common to several mission type
*
====================================*/

/**
 * @brief Check if a map may be selected for mission.
 * @param[in] mission Pointer to the mission where mapDef should be added
 * @param[in] pos position of the mission (nullptr if the position will be chosen afterwards)
 * @param[in] md The map description data (what it is suitable for)
 * @return false if map is not selectable
 */
static bool CP_MapIsSelectable (const mission_t* mission, const mapDef_t* md, const vec2_t pos)
{
	if (md->storyRelated)
		return false;

	if (!mission->ufo) {
		/* a mission without UFO should not use a map with UFO */
		if (!cgi->LIST_IsEmpty(md->ufos))
			return false;
	} else if (!cgi->LIST_IsEmpty(md->ufos)) {
		/* A mission with UFO should use a map with UFO
		 * first check that list is not empty */
		const ufoType_t type = mission->ufo->getUfoType();
		const char* ufoID;

		if (mission->crashed)
			ufoID = cgi->Com_UFOCrashedTypeToShortName(type);
		else
			ufoID = cgi->Com_UFOTypeToShortName(type);

		if (!cgi->LIST_ContainsString(md->ufos, ufoID))
			return false;
	}

	if (pos && !GEO_PositionFitsTCPNTypes(pos, md->terrains, md->cultures, md->populations, nullptr))
		return false;

	return true;
}

/**
 * @brief Choose a map for given mission.
 * @param[in,out] mission Pointer to the mission where a new map should be added
 * @param[in] pos position of the mission (nullptr if the position will be chosen afterwards)
 * @return false if could not set mission
 */
bool CP_ChooseMap (mission_t* mission, const vec2_t pos)
{
	if (mission->mapDef)
		return true;

	int countMinimal = 0;	/**< Number of maps fulfilling mission conditions and appeared less often during game. */
	int minMapDefAppearance = -1;
	mapDef_t* md = nullptr;
	MapDef_ForeachSingleplayerCampaign(md) {
		/* Check if mission fulfill conditions */
		if (!CP_MapIsSelectable(mission, md, pos))
			continue;

		if (minMapDefAppearance < 0 || md->timesAlreadyUsed < minMapDefAppearance) {
			minMapDefAppearance = md->timesAlreadyUsed;
			countMinimal = 1;
			continue;
		}
		if (md->timesAlreadyUsed > minMapDefAppearance)
			continue;
		countMinimal++;
	}

	if (countMinimal == 0) {
		/* no map fulfill the conditions */
		if (mission->category == INTERESTCATEGORY_RESCUE) {
			/* default map for rescue mission is the rescue random map assembly */
			mission->mapDef = cgi->Com_GetMapDefinitionByID("rescue");
			if (!mission->mapDef)
				cgi->Com_Error(ERR_DROP, "Could not find mapdef: rescue");
			mission->mapDef->timesAlreadyUsed++;
			return true;
		}
		if (mission->crashed) {
			/* default map for crashsite mission is the crashsite random map assembly */
			mission->mapDef = cgi->Com_GetMapDefinitionByID("ufocrash");
			if (!mission->mapDef)
				cgi->Com_Error(ERR_DROP, "Could not find mapdef: ufocrash");
			mission->mapDef->timesAlreadyUsed++;
			return true;
		}

		cgi->Com_Printf("CP_ChooseMap: Could not find map with required conditions:\n");
		cgi->Com_Printf("  ufo: %s -- pos: ", mission->ufo ? cgi->Com_UFOTypeToShortName(mission->ufo->getUfoType()) : "none");
		if (pos)
			cgi->Com_Printf("%s", MapIsWater(GEO_GetColor(pos, MAPTYPE_TERRAIN, nullptr)) ? " (in water) " : "");
		if (pos)
			cgi->Com_Printf("(%.02f, %.02f)\n", pos[0], pos[1]);
		else
			cgi->Com_Printf("none\n");
		return false;
	}

	/* select a map randomly from the selected */
	int randomNum = rand() % countMinimal;
	md = nullptr;
	MapDef_ForeachSingleplayerCampaign(md) {
		/* Check if mission fulfill conditions */
		if (!CP_MapIsSelectable(mission, md, pos))
			continue;
		if (md->timesAlreadyUsed > minMapDefAppearance)
			continue;
		/* There shouldn't be mission fulfilling conditions used less time than minMissionAppearance */
		assert(md->timesAlreadyUsed == minMapDefAppearance);

		if (randomNum == 0) {
			mission->mapDef = md;
			break;
		} else {
			randomNum--;
		}
	}

	/* A mission must have been selected */
	mission->mapDef->timesAlreadyUsed++;
	if (cp_missiontest->integer)
		cgi->Com_Printf("Selected map '%s' (among %i possible maps)\n", mission->mapDef->id, countMinimal);
	else
		cgi->Com_DPrintf(DEBUG_CLIENT, "Selected map '%s' (among %i possible maps)\n", mission->mapDef->id, countMinimal);

	return true;
}

/**
 * @brief Determine what action should be performed when a mission stage ends.
 * @param[in] campaign The campaign data structure
 * @param[in] mission Pointer to the mission which stage ended.
 */
void CP_MissionStageEnd (const campaign_t* campaign, mission_t* mission)
{
	cgi->Com_DPrintf(DEBUG_CLIENT, "Ending mission category %i, stage %i (time: %i day, %i sec)\n",
		mission->category, mission->stage, ccs.date.getDateAsDays(), ccs.date.getTimeAsSeconds());

	/* Crash mission is on the map for too long: aliens die or go away. End mission */
	if (mission->crashed) {
		CP_MissionIsOver(mission);
		return;
	}

	switch (mission->category) {
	case INTERESTCATEGORY_RECON:
		CP_ReconMissionNextStage(mission);
		break;
	case INTERESTCATEGORY_TERROR_ATTACK:
		CP_TerrorMissionNextStage(mission);
		break;
	case INTERESTCATEGORY_BASE_ATTACK:
		CP_BaseAttackMissionNextStage(mission);
		break;
	case INTERESTCATEGORY_BUILDING:
	case INTERESTCATEGORY_SUBVERT:
		CP_BuildBaseMissionNextStage(campaign, mission);
		break;
	case INTERESTCATEGORY_SUPPLY:
		CP_SupplyMissionNextStage(mission);
		break;
	case INTERESTCATEGORY_XVI:
		CP_XVIMissionNextStage(mission);
		break;
	case INTERESTCATEGORY_INTERCEPT:
	case INTERESTCATEGORY_INTERCEPTBOMBING:
		CP_InterceptNextStage(mission);
		break;
	case INTERESTCATEGORY_HARVEST:
		CP_HarvestMissionNextStage(mission);
		break;
	case INTERESTCATEGORY_RESCUE:
		CP_RescueNextStage(mission);
		break;
	case INTERESTCATEGORY_UFOCARRIER:
		CP_UFOCarrierNextStage(mission);
		break;
	case INTERESTCATEGORY_ALIENBASE:
	case INTERESTCATEGORY_NONE:
	case INTERESTCATEGORY_MAX:
		cgi->Com_Printf("CP_MissionStageEnd: Invalid type of mission (%i), remove mission '%s'\n", mission->category, mission->id);
		CP_MissionRemove(mission);
	}
}

/**
 * @brief Mission is finished because Phalanx team won it.
 * @param[in] mission Pointer to the mission that is ended.
 */
void CP_MissionIsOver (mission_t* mission)
{
	switch (mission->category) {
	case INTERESTCATEGORY_RECON:
		CP_ReconMissionIsFailure(mission);
		break;
	case INTERESTCATEGORY_TERROR_ATTACK:
		CP_TerrorMissionIsFailure(mission);
		break;
	case INTERESTCATEGORY_BASE_ATTACK:
		if (mission->stage <= STAGE_BASE_ATTACK)
			CP_BaseAttackMissionIsFailure(mission);
		else
			CP_BaseAttackMissionIsSuccess(mission);
		break;
	case INTERESTCATEGORY_BUILDING:
	case INTERESTCATEGORY_SUBVERT:
		if (mission->stage <= STAGE_BUILD_BASE)
			CP_BuildBaseMissionIsFailure(mission);
		else
			CP_BuildBaseMissionIsSuccess(mission);
		break;
	case INTERESTCATEGORY_SUPPLY:
		if (mission->stage <= STAGE_SUPPLY)
			CP_SupplyMissionIsFailure(mission);
		else
			CP_SupplyMissionIsSuccess(mission);
		break;
	case INTERESTCATEGORY_XVI:
		if (mission->stage <= STAGE_SPREAD_XVI)
			CP_XVIMissionIsFailure(mission);
		else
			CP_XVIMissionIsSuccess(mission);
		break;
	case INTERESTCATEGORY_INTERCEPT:
	case INTERESTCATEGORY_INTERCEPTBOMBING:
		if (mission->stage <= STAGE_INTERCEPT)
			CP_InterceptMissionIsFailure(mission);
		else
			CP_InterceptMissionIsSuccess(mission);
		break;
	case INTERESTCATEGORY_HARVEST:
		CP_HarvestMissionIsFailure(mission);
		break;
	case INTERESTCATEGORY_ALIENBASE:
		CP_BuildBaseMissionBaseDestroyed(mission);
		break;
	case INTERESTCATEGORY_RESCUE:
		CP_MissionRemove(mission);
		break;
	case INTERESTCATEGORY_UFOCARRIER:
	case INTERESTCATEGORY_NONE:
	case INTERESTCATEGORY_MAX:
		cgi->Com_Printf("CP_MissionIsOver: Invalid type of mission (%i), remove mission\n", mission->category);
		CP_MissionRemove(mission);
		break;
	}
}

/**
 * @brief Mission is finished because Phalanx team ended it.
 * @param[in] ufocraft Pointer to the UFO involved in this mission
 */
void CP_MissionIsOverByUFO (aircraft_t* ufocraft)
{
	assert(ufocraft->mission);
	CP_MissionIsOver(ufocraft->mission);
}

/**
 * @brief Actions to be done after mission finished
 * @param[in,out] mission Pointer to the finished mission
 * @param[in,out] aircraft Pointer to the dropship done the mission
 * @param[in] won Boolean flag if thew mission was successful (from PHALANX's PoV)
 */
void CP_MissionEndActions (mission_t* mission, aircraft_t* aircraft, bool won)
{
	/* handle base attack mission */
	if (mission->stage == STAGE_BASE_ATTACK) {
		if (won) {
			/* fake an aircraft return to collect goods and aliens */
			B_DumpAircraftToHomeBase(aircraft);

			Com_sprintf(cp_messageBuffer, sizeof(cp_messageBuffer), _("Defence of base: %s successful!"),
					aircraft->homebase->name);
			MS_AddNewMessage(_("Notice"), cp_messageBuffer);
			CP_BaseAttackMissionIsFailure(mission);
		} else
			CP_BaseAttackMissionDestroyBase(mission);

		return;
	}

	if (mission->category == INTERESTCATEGORY_RESCUE) {
		CP_EndRescueMission(mission, aircraft, won);
	}

	AIR_AircraftReturnToBase(aircraft);
	if (won)
		CP_MissionIsOver(mission);
}

/**
 * @brief Closing actions after fighting a battle
 * @param[in] campaign The campaign we play
 * @param[in, out] mission The mission the battle was on
 * @param[in] battleParameters Parameters of the battle
 * @param[in] won if PHALANX won
 * @note both manual and automatic missions call this through won/lost UI screen
 */
void CP_MissionEnd (const campaign_t* campaign, mission_t* mission, const battleParam_t* battleParameters, bool won)
{
	base_t* base;
	aircraft_t* aircraft;
	int numberOfSoldiers = 0; /* DEBUG */

	if (mission->stage == STAGE_BASE_ATTACK) {
		base = mission->data.base;
		assert(base);
		/* HACK */
		aircraft = base->aircraftCurrent;
	} else {
		aircraft = GEO_GetMissionAircraft();
		base = aircraft->homebase;
	}

	/* add the looted goods to base storage and market */
	base->storage = ccs.eMission;

	won ? ccs.campaignStats.missionsWon++ : ccs.campaignStats.missionsLost++;

	CP_HandleNationData(campaign->minhappiness, mission, battleParameters->nation, &(mission->missionResults), won);
	CP_CheckLostCondition(campaign);

	/* update the character stats */
	CHAR_UpdateData(ccs.updateCharacters);
	cgi->LIST_Delete(&ccs.updateCharacters);

	/* update stats */
	CHAR_UpdateStats(base, aircraft);

	E_Foreach(EMPL_SOLDIER, employee) {
		if (AIR_IsEmployeeInAircraft(employee, aircraft))
			numberOfSoldiers++;

		/** @todo replace HP check with some CHRSH->IsDead() function */
		if (employee->isHiredInBase(base) && (employee->chr.HP <= 0))
			E_DeleteEmployee(employee);
	}
	cgi->Com_DPrintf(DEBUG_CLIENT, "CP_MissionEnd - num %i\n", numberOfSoldiers);

	CP_ExecuteMissionTrigger(mission, won);
	CP_MissionEndActions(mission, aircraft, won);
}

/**
 * @brief Check if a stage mission is over when UFO reached destination.
 * @param[in] campaign The campaign data structure
 * @param[in] ufocraft Pointer to the ufo that reached destination.
 * @sa UFO_CampaignRunUFOs
 * @return True if UFO is removed from global array (and therefore pointer ufocraft can't be used anymore).
 */
bool CP_CheckNextStageDestination (const campaign_t* campaign, aircraft_t* ufocraft)
{
	mission_t* mission;

	mission = ufocraft->mission;
	assert(mission);

	switch (mission->stage) {
	case STAGE_COME_FROM_ORBIT:
	case STAGE_MISSION_GOTO:
		CP_MissionStageEnd(campaign, mission);
		return false;
	case STAGE_RETURN_TO_ORBIT:
		CP_MissionStageEnd(campaign, mission);
		return true;
	default:
		/* Do nothing */
		return false;
	}
}

/**
 * @brief Make UFO proceed with its mission when the fight with another aircraft is over (and UFO survived).
 * @param[in] campaign The campaign data structure
 * @param[in] ufo Pointer to the ufo that should proceed a mission.
 */
void CP_UFOProceedMission (const campaign_t* campaign, aircraft_t* ufo)
{
	/* Every UFO on geoscape must have a mission assigned */
	assert(ufo->mission);

	if (ufo->mission->category == INTERESTCATEGORY_INTERCEPT && !ufo->mission->data.aircraft) {
		const int slotIndex = AIRFIGHT_ChooseWeapon(ufo->weapons, ufo->maxWeapons, ufo->pos, ufo->pos);
		/* This is an Intercept mission where UFO attacks aircraft (not installations) */
		/* Keep on looking targets until mission is over, unless no more ammo */
		ufo->status = AIR_TRANSIT;
		if (slotIndex != AIRFIGHT_WEAPON_CAN_NEVER_SHOOT)
			UFO_SetRandomDest(ufo);
		else
			CP_MissionStageEnd(campaign, ufo->mission);
	} else if (ufo->mission->stage < STAGE_MISSION_GOTO ||
		ufo->mission->stage >= STAGE_RETURN_TO_ORBIT) {
		UFO_SetRandomDest(ufo);
	} else {
		UFO_SendToDestination(ufo, ufo->mission->pos);
	}
}

/**
 * @brief Spawn a new crash site after a UFO has been destroyed.
 * @param[in,out] ufo The ufo to spawn a crash site mission for
 */
void CP_SpawnCrashSiteMission (aircraft_t* ufo)
{
	/* How long the crash mission will stay before aliens leave / die */
	const DateTime minCrashDelay(7, 0);
	const DateTime maxCrashDelay(14, 0);
	mission_t* mission;

	mission = ufo->mission;
	if (!mission)
		cgi->Com_Error(ERR_DROP, "CP_SpawnCrashSiteMission: No mission correspond to ufo '%s'", ufo->id);

	mission->crashed = true;

	/* Reset mapDef. CP_ChooseMap don't overwrite if set */
	mission->mapDef = nullptr;
	if (!CP_ChooseMap(mission, ufo->pos)) {
		cgi->Com_Printf("CP_SpawnCrashSiteMission: No map found, remove mission.\n");
		CP_MissionRemove(mission);
		return;
	}

	Vector2Copy(ufo->pos, mission->pos);
	mission->posAssigned = true;

	mission->finalDate = ccs.date + Date_Random(minCrashDelay, maxCrashDelay);
	/* ufo becomes invisible on geoscape, but don't remove it from ufo global array
	 * (may be used to know what items are collected from battlefield)*/
	CP_UFORemoveFromGeoscape(mission, false);
	/* mission appear on geoscape, player can go there */
	CP_MissionAddToGeoscape(mission, false);
}


/**
 * @brief Spawn a new rescue mission for a crashed (phalanx) aircraft
 * @param[in] aircraft The crashed aircraft to spawn the rescue mission for.
 * @param[in] ufo The UFO that shot down the phalanx aircraft, can also
 * be @c nullptr if the UFO was destroyed.
 * @note Don't use ufo's old mission pointer after this call! It might have been removed.
 * @todo Don't spawn rescue mission every time! It should depend on pilot's manoeuvring (piloting) skill
 */
void CP_SpawnRescueMission (aircraft_t* aircraft, aircraft_t* ufo)
{
	mission_t* mission;
	mission_t* oldMission;
	int survivors = 0;

	/* Handle events about crash */
	/* Do this first, if noone survived the crash => no mission to spawn */

	bool pilotSurvived = AIR_PilotSurvivedCrash(aircraft);
	if (!pilotSurvived) {
		Employee* pilot = AIR_GetPilot(aircraft);
		/** @todo don't "kill" everyone - this should depend on luck and a little bit on the skills */
		E_DeleteEmployee(pilot);
	}

	LIST_Foreach(aircraft->acTeam, Employee, employee) {
#if 0
		const character_t* chr = &employee->chr;
		const chrScoreGlobal_t* score = &chr->score;
		/** @todo don't "kill" everyone - this should depend on luck and a little bit on the skills */
		E_DeleteEmployee(employee);
#else
		(void)employee;
#endif
		survivors++;
	}

	aircraft->status = AIR_CRASHED;

	/* after we set this to AIR_CRASHED we can get the next 'valid'
	 * aircraft to correct the pointer in the homebase */
	if (aircraft->homebase->aircraftCurrent == aircraft)
		aircraft->homebase->aircraftCurrent = AIR_GetFirstFromBase(aircraft->homebase);

	/* a crashed aircraft is no longer using capacity of the hangars */
	const baseCapacities_t capType = AIR_GetHangarCapacityType(aircraft);
	if (capType != MAX_CAP)
		CAP_AddCurrent(aircraft->homebase, capType, -1);

	if (GEO_IsAircraftSelected(aircraft))
		GEO_SetSelectedAircraft(nullptr);

	/* Check if ufo was destroyed too */
	if (!ufo) {
		cgi->Com_Printf("CP_SpawnRescueMission: UFO was also destroyed.\n");
		/** @todo find out what to do in this case */
		AIR_DestroyAircraft(aircraft, pilotSurvived);
		return;
	}

	if (survivors == 0) {
		AIR_DestroyAircraft(aircraft, pilotSurvived);
		return;
	}

	/* create the mission */
	mission = CP_CreateNewMission(INTERESTCATEGORY_RESCUE, true);
	if (!mission)
		cgi->Com_Error(ERR_DROP, "CP_SpawnRescueMission: mission could not be created");

	/* Reset mapDef. CP_ChooseMap don't overwrite if set */
	mission->mapDef = nullptr;
	if (!CP_ChooseMap(mission, aircraft->pos)) {
		cgi->Com_Printf("CP_SpawnRescueMission: Cannot set mapDef for mission %s, removing.\n", mission->id);
		CP_MissionRemove(mission);
		return;
	}

	mission->data.aircraft = aircraft;

	/* UFO drops it's previous mission and goes for the crashed aircraft */
	oldMission = ufo->mission;
	oldMission->ufo = nullptr;
	ufo->mission = mission;
	CP_MissionRemove(oldMission);

	mission->ufo = ufo;
	mission->stage = STAGE_MISSION_GOTO;
	Vector2Copy(aircraft->pos, mission->pos);

	/* Stage will finish when UFO arrives at destination */
	CP_MissionDisableTimeLimit(mission);
}

/*====================================
*
* Spawn new missions
*
====================================*/

/**
 * @brief Decides if the mission should be spawned from the ground (without UFO)
 * @param[in] mission Pointer to the mission
 */
static bool MIS_IsSpawnedFromGround (const mission_t* mission)
{
	assert(mission);

	switch (mission->category) {
	/* missions can't be spawned from ground */
	case INTERESTCATEGORY_BUILDING:
	case INTERESTCATEGORY_SUBVERT:
	case INTERESTCATEGORY_SUPPLY:
	case INTERESTCATEGORY_INTERCEPT:
	case INTERESTCATEGORY_INTERCEPTBOMBING:
	case INTERESTCATEGORY_HARVEST:
		return false;
	/* missions can be spawned from ground */
	case INTERESTCATEGORY_RECON:
	case INTERESTCATEGORY_TERROR_ATTACK:
	case INTERESTCATEGORY_BASE_ATTACK:
	case INTERESTCATEGORY_XVI:
		break;
	/* missions not spawned this way */
	case INTERESTCATEGORY_ALIENBASE:
	case INTERESTCATEGORY_UFOCARRIER:
	case INTERESTCATEGORY_RESCUE:
	case INTERESTCATEGORY_NONE:
	case INTERESTCATEGORY_MAX:
		cgi->Com_Error(ERR_DROP, "MIS_IsSpawnedFromGround: Wrong mission category %i", mission->category);
	}

	/* Roll the random number */
	const float XVI_PARAM = 10.0f;		/**< Typical XVI average value for spreading mission from earth */
	float groundProbability;
	float randNumber = frand();

	/* The higher the XVI rate, the higher the probability to have a mission spawned from ground */
	groundProbability = 1.0f - exp(-CP_GetAverageXVIRate() / XVI_PARAM);
	groundProbability = std::max(0.1f, groundProbability);

	return randNumber < groundProbability;
}

/**
 * @brief mission begins: UFO arrive on earth.
 * @param[in] mission The mission to change the state for
 * @note Stage 0 -- This function is common to several mission category.
 * @sa CP_MissionChooseUFO
 * @return true if mission was created, false else.
 */
bool CP_MissionBegin (mission_t* mission)
{
	mission->stage = STAGE_COME_FROM_ORBIT;

	if (MIS_IsSpawnedFromGround(mission)) {
		mission->ufo = nullptr;
		/* Mission starts from ground: no UFO. Go to next stage on next frame (skip UFO arrives on earth) */
		mission->finalDate = ccs.date;
	} else {
		ufoType_t ufoType = CP_MissionChooseUFO(mission);
		if (ufoType == UFO_NONE) {
			CP_MissionRemove(mission);
			return false;
		}
		mission->ufo = UFO_AddToGeoscape(ufoType, nullptr, mission);
		if (!mission->ufo) {
			cgi->Com_Printf("CP_MissionBegin: Could not add UFO '%s', remove mission %s\n",
				cgi->Com_UFOTypeToShortName(ufoType), mission->id);
			CP_MissionRemove(mission);
			return false;
		}
		/* Stage will finish when UFO arrives at destination */
		CP_MissionDisableTimeLimit(mission);
	}

	mission->idx = ++ccs.campaignStats.missions;
	return true;
}

/**
 * @brief Choose UFO type for a given mission category.
 * @param[in] mission Pointer to the mission where the UFO will be added
 * @sa CP_MissionChooseUFO
 * @sa CP_SupplyMissionCreate
 * @return ufoType_t of the UFO spawning the mission, UFO_NONE if the mission is spawned from ground
 */
ufoType_t CP_MissionChooseUFO (const mission_t* mission)
{
	ufoType_t ufoTypes[UFO_MAX];
	int numTypes = 0;

	switch (mission->category) {
	case INTERESTCATEGORY_RECON:
		numTypes = UFO_GetAvailableUFOsForMission(mission->category, ufoTypes);
		break;
	case INTERESTCATEGORY_TERROR_ATTACK:
		numTypes = UFO_GetAvailableUFOsForMission(mission->category, ufoTypes);
		break;
	case INTERESTCATEGORY_BASE_ATTACK:
		numTypes = UFO_GetAvailableUFOsForMission(mission->category, ufoTypes);
		break;
	case INTERESTCATEGORY_BUILDING:
	case INTERESTCATEGORY_SUBVERT:
		if (CP_BasemissionIsSubvertingGovernmentMission(mission)) {
			/* This is a subverting government mission */
			numTypes = UFO_GetAvailableUFOsForMission(INTERESTCATEGORY_SUBVERT, ufoTypes);
		} else {
			/* This is a Building base mission */
			numTypes = UFO_GetAvailableUFOsForMission(INTERESTCATEGORY_BUILDING, ufoTypes);
		}
		break;
	case INTERESTCATEGORY_SUPPLY:
		numTypes = UFO_GetAvailableUFOsForMission(mission->category, ufoTypes);
		break;
	case INTERESTCATEGORY_XVI:
		numTypes = UFO_GetAvailableUFOsForMission(mission->category, ufoTypes);
		break;
	case INTERESTCATEGORY_INTERCEPT:
	case INTERESTCATEGORY_INTERCEPTBOMBING:
		numTypes = UFO_GetAvailableUFOsForMission(mission->category, ufoTypes);
		break;
	case INTERESTCATEGORY_HARVEST:
		numTypes = UFO_GetAvailableUFOsForMission(mission->category, ufoTypes);
		break;
	case INTERESTCATEGORY_ALIENBASE:
		/* can't be spawned: alien base is the result of base building mission */
	case INTERESTCATEGORY_UFOCARRIER:
	case INTERESTCATEGORY_RESCUE:
	case INTERESTCATEGORY_NONE:
	case INTERESTCATEGORY_MAX:
		cgi->Com_Error(ERR_DROP, "CP_MissionChooseUFO: Wrong mission category %i", mission->category);
	}

	const short ufoIdsNum = cgi->Com_GetUFOIdsNum();
	if (numTypes > ufoIdsNum)
		cgi->Com_Error(ERR_DROP, "CP_MissionChooseUFO: Too many values UFOs (%i/%i)", numTypes, ufoIdsNum);

	if (numTypes <= 0)
		return UFO_NONE;

	/* Roll the random number */
	const float randNumber = frand();

	/* If we reached this point, then mission will be spawned from space: choose UFO */
	int idx = (int) (numTypes * randNumber);
	if (idx >= numTypes) {
		idx = numTypes -1;
	}

	return ufoTypes[idx];
}

/**
 * @brief Set mission name.
 * @note that mission name must be unique in mission global array
 * @param[out] mission The mission to set the name for
 * @sa CP_CreateNewMission
 */
static inline void CP_SetMissionName (mission_t* mission)
{
	int num = 0;

	do {
		Com_sprintf(mission->id, sizeof(mission->id), "cat%i_interest%i_%i",
			mission->category, mission->initialOverallInterest, num);

		/* if mission list is empty, the id is unique for sure */
		if (cgi->LIST_IsEmpty(ccs.missions))
			return;

		/* check whether a mission with the same id already exists in the list
		 * if so, generate a new name by using an increased num values - otherwise
		 * just use the mission->id we just stored - it should be unique now */
		if (!CP_GetMissionByIDSilent(mission->id))
			break;

		num++;
	} while (num); /* fake condition */
}

/**
 * @brief Create a new mission of given category.
 * @param[in] category category of the mission
 * @param[in] beginNow true if the mission should begin now
 * @sa CP_SpawnNewMissions
 * @sa CP_MissionStageEnd
 */
mission_t* CP_CreateNewMission (interestCategory_t category, bool beginNow)
{
	mission_t mission;
	const DateTime minNextSpawningDate(0, 0);
	const DateTime maxNextSpawningDate(3, 0);	/* Delay between 2 mission spawning */

	/* Some event are non-occurrence */
	if (category <= INTERESTCATEGORY_NONE || category >= INTERESTCATEGORY_MAX)
		return nullptr;

	OBJZERO(mission);

	/* First fill common datas between all type of missions */
	mission.category = category;
	mission.initialOverallInterest = ccs.overallInterest;
	mission.initialIndividualInterest = ccs.interest[category];
	mission.stage = STAGE_NOT_ACTIVE;
	mission.ufo = nullptr;
	if (beginNow)
		mission.startDate = ccs.date;
	else
		mission.startDate = ccs.date + Date_Random(minNextSpawningDate, maxNextSpawningDate);
	mission.finalDate = mission.startDate;
	mission.idx = ++ccs.campaignStats.missions;

	CP_SetMissionName(&mission);

	/* Handle mission specific, spawn-time routines */
	if (mission.category == INTERESTCATEGORY_BUILDING)
		CP_BuildBaseMissionOnSpawn();
	else if (mission.category == INTERESTCATEGORY_BASE_ATTACK)
		CP_BaseAttackMissionOnSpawn();
	else if (mission.category == INTERESTCATEGORY_TERROR_ATTACK)
		CP_TerrorMissionOnSpawn();

	/* Add mission to global array */
	return &LIST_Add(&ccs.missions, mission);
}

/**
 * @brief Select new mission type.
 * @sa CP_SpawnNewMissions
 */
static interestCategory_t CP_SelectNewMissionType (void)
{
	int sum = 0;
	int i, randomNumber;

	for (i = 0; i < INTERESTCATEGORY_MAX; i++)
		sum += ccs.interest[i];

	randomNumber = (int) (frand() * (float) sum);

	for (i = 0; randomNumber >= 0; i++)
		randomNumber -= ccs.interest[i];

	return (interestCategory_t)(i - 1);
}

/**
 * @brief Spawn new missions.
 * @sa CP_CampaignRun
 * @note daily called
 */
void CP_SpawnNewMissions (void)
{
	int spawn_delay = DELAY_BETWEEN_MISSION_SPAWNING;
	ccs.lastMissionSpawnedDelay++;

	/* Halve the spawn delay in the early game so players see UFOs and get right into action */
	if (ccs.overallInterest < EARLY_UFO_RUSH_INTEREST) {
		spawn_delay = (int) (spawn_delay / 3);
	}

	if (ccs.lastMissionSpawnedDelay > spawn_delay) {
		float nonOccurrence;
		/* Select the amount of missions that will be spawned in the next cycle. */

		/* Each mission has a certain probability to not occur. This provides some randomness to the mission density.
		 * However, once the campaign passes a certain point, this effect rapidly diminishes. This means that by the
		 * end of the game, ALL missions will spawn, quickly overrunning the player. */
		if (ccs.overallInterest > FINAL_OVERALL_INTEREST)
			nonOccurrence = ccs.curCampaign->ufoReductionRate / pow(((ccs.overallInterest - FINAL_OVERALL_INTEREST / 30) + 1.0f), 2);
		else
			nonOccurrence = ccs.curCampaign->ufoReductionRate;

		/* Increase the alien's interest in supplying their bases for this cycle.
		 * The more bases, the greater their interest in supplying them. */
		INT_ChangeIndividualInterest(AB_GetAlienBaseNumber() * 0.1f, INTERESTCATEGORY_SUPPLY);

		/* Calculate the amount of missions to be spawned this cycle.
		 * Note: This is a function over css.overallInterest. It looks like this:
		 * http://www.wolframalpha.com/input/?i=Plot%5B40%2B%285-40%29%2A%28%28x-1000%29%2F%2820-1000%29%29%5E2%2C+%7Bx%2C+0%2C+1100%7D%5D
		 */
		int newMissionNum = (int) (ccs.curCampaign->maxMissions + (ccs.curCampaign->minMissions - ccs.curCampaign->maxMissions) * pow(float((ccs.overallInterest - FINAL_OVERALL_INTEREST) / (ccs.curCampaign->initialInterest - FINAL_OVERALL_INTEREST)), 2));
		cgi->Com_DPrintf(DEBUG_CLIENT, "interest = %d, new missions = %d\n", ccs.overallInterest, newMissionNum);
		for (int i = 0; i < newMissionNum; i++) {
			if (frand() > nonOccurrence) {
				const interestCategory_t type = CP_SelectNewMissionType();
				CP_CreateNewMission(type, false);
			}
		}

		ccs.lastMissionSpawnedDelay -= spawn_delay;
	}
}

/**
 * @brief Initialize spawning delay.
 * @sa CP_SpawnNewMissions
 * @note only called when new game is started, in order to spawn new event on the beginning of the game.
 */
void CP_InitializeSpawningDelay (void)
{
	ccs.lastMissionSpawnedDelay = DELAY_BETWEEN_MISSION_SPAWNING;

	ccs.missionSpawnCallback();
}


/*====================================
*
* Debug functions
*
====================================*/

#ifdef DEBUG
/**
 * @brief Debug function for creating a mission.
 */
static void MIS_SpawnNewMissions_f (void)
{
	interestCategory_t category;
	int type = 0;

	if (cgi->Cmd_Argc() < 2) {
		cgi->Com_Printf("Usage: %s <category> [<type>]\n", cgi->Cmd_Argv(0));
		for (int i = INTERESTCATEGORY_RECON; i < INTERESTCATEGORY_MAX; i++) {
			category = (interestCategory_t)i;
			cgi->Com_Printf("...%i: %s", category, INT_InterestCategoryToName(category));
			if (category == INTERESTCATEGORY_RECON)
				cgi->Com_Printf(" <0:Random, 1:Aerial, 2:Ground>");
			else if (category == INTERESTCATEGORY_BUILDING)
				cgi->Com_Printf(" <0:Subverse Government, 1:Build Base>");
			else if (category == INTERESTCATEGORY_INTERCEPT)
				cgi->Com_Printf(" <0:Intercept aircraft, 1:Attack installation>");
			cgi->Com_Printf("\n");
		}
		return;
	}

	if (cgi->Cmd_Argc() >= 3)
		type = atoi(cgi->Cmd_Argv(2));

	category = (interestCategory_t)atoi(cgi->Cmd_Argv(1));

	if (category == INTERESTCATEGORY_MAX)
		return;

	if (category == INTERESTCATEGORY_ALIENBASE) {
		/* spawning an alien base is special */
		vec2_t pos;
		alienBase_t* base;
		AB_SetAlienBasePosition(pos);				/* get base position */
		base = AB_BuildBase(pos);					/* build base */
		if (!base) {
			cgi->Com_Printf("CP_BuildBaseSetUpBase: could not create base\n");
			return;
		}
		CP_SpawnAlienBaseMission(base);				/* make base visible */
		return;
	} else if (category == INTERESTCATEGORY_RESCUE) {
		const base_t* base = B_GetFoundedBaseByIDX(0);
		aircraft_t* aircraft;
		if (!base) {
			cgi->Com_Printf("No base yet\n");
			return;
		}

		aircraft = AIR_GetFirstFromBase(base);
		if (!aircraft) {
			cgi->Com_Printf("No aircraft in base\n");
			return;
		}
		CP_SpawnRescueMission(aircraft, nullptr);
		return;
	}

	mission_t* mission = CP_CreateNewMission(category, true);
	if (!mission) {
		cgi->Com_Printf("CP_SpawnNewMissions_f: Could not add mission, abort\n");
		return;
	}

	if (type) {
		switch (category) {
		case INTERESTCATEGORY_RECON:
			/* Start mission */
			if (!CP_MissionBegin(mission))
				return;
			if (type == 1 && mission->ufo)
				/* Aerial mission */
				CP_ReconMissionAerial(mission);
			else
				/* This is a ground mission */
				CP_ReconMissionGroundGo(mission);
			break;
		case INTERESTCATEGORY_BUILDING:
			if (type == 1)
				mission->initialOverallInterest = ccs.curCampaign->alienBaseInterest + 1;
			break;
		case INTERESTCATEGORY_INTERCEPT:
			/* Start mission */
			if (!CP_MissionBegin(mission))
				return;
			if (type == 1) {
				ufoType_t ufoType = UFO_GetOneAvailableUFOForMission(INTERESTCATEGORY_INTERCEPTBOMBING);	/* the first one will do */
				mission->ufo->setUfoType(ufoType);
				CP_InterceptGoToInstallation(mission);
			} else {
				CP_InterceptAircraftMissionSet(mission);
			}
			break;
		default:
			cgi->Com_Printf("Type is not implemented for this category.\n");
		}
	}
	cgi->Com_Printf("Spawned mission with id '%s'\n", mission->id);
}

/**
 * @brief Changes the map for an already spawned mission
 */
static void MIS_MissionSetMap_f (void)
{
	mapDef_t* mapDef;
	mission_t* mission;
	if (cgi->Cmd_Argc() < 3) {
		cgi->Com_Printf("Usage: %s <missionid> <mapdef>\n", cgi->Cmd_Argv(0));
		return;
	}
	mission = CP_GetMissionByID(cgi->Cmd_Argv(1));
	mapDef = cgi->Com_GetMapDefinitionByID(cgi->Cmd_Argv(2));
	if (mapDef == nullptr) {
		cgi->Com_Printf("Could not find mapdef for %s\n", cgi->Cmd_Argv(2));
		return;
	}
	mission->mapDef = mapDef;
}

/**
 * @brief List all current mission to console.
 * @note Use with debug_missionlist
 */
static void MIS_MissionList_f (void)
{
	bool noMission = true;

	MIS_Foreach(mission) {
		cgi->Com_Printf("mission: '%s'\n", mission->id);
		cgi->Com_Printf("...category %i. '%s' -- stage %i. '%s'\n", mission->category,
			INT_InterestCategoryToName(mission->category), mission->stage, CP_MissionStageToName(mission->stage));
		cgi->Com_Printf("...mapDef: '%s'\n", mission->mapDef ? mission->mapDef->id : "No mapDef defined");
		cgi->Com_Printf("...start (day = %i, sec = %i), ends (day = %i, sec = %i)\n",
			mission->startDate.getDateAsDays(), mission->startDate.getTimeAsSeconds(), mission->finalDate.getDateAsDays(), mission->finalDate.getTimeAsSeconds());
		cgi->Com_Printf("...pos (%.02f, %.02f)%s -- mission %son Geoscape\n", mission->pos[0], mission->pos[1], mission->posAssigned ? "(assigned Pos)" : "", mission->onGeoscape ? "" : "not ");
		if (mission->ufo)
			cgi->Com_Printf("...UFO: %s (%i/%i)\n", mission->ufo->id, (int) (mission->ufo - ccs.ufos), ccs.numUFOs - 1);
		else
			cgi->Com_Printf("...UFO: no UFO\n");
		noMission = false;
	}
	if (noMission)
		cgi->Com_Printf("No mission currently in game.\n");
}

/**
 * @brief Debug function for deleting all mission in global array.
 * @note called with debug_missiondeleteall
 */
static void MIS_DeleteMissions_f (void)
{
	MIS_Foreach(mission) {
		CP_MissionRemove(mission);
	}

	if (ccs.numUFOs != 0) {
		cgi->Com_Printf("CP_DeleteMissions_f: Error, there are still %i UFO in game afer removing all missions. Force removal.\n", ccs.numUFOs);
		while (ccs.numUFOs)
			UFO_RemoveFromGeoscape(ccs.ufos);
	}
}

/**
 * @brief Debug function to delete a mission
 * @note called with debug_missiondelete
 */
static void MIS_DeleteMission_f (void)
{
	mission_t* mission;

	if (cgi->Cmd_Argc() < 2) {
		cgi->Com_Printf("Usage: %s <missionid>\n", cgi->Cmd_Argv(0));
		return;
	}
	mission = CP_GetMissionByID(cgi->Cmd_Argv(1));

	if (!mission)
		return;

	CP_MissionRemove(mission);
}
#endif

/**
 * @brief Save callback for savegames in XML Format
 * @param[out] parent XML Node structure, where we write the information to
 */
bool MIS_SaveXML (xmlNode_t* parent)
{
	xmlNode_t* missionsNode = cgi->XML_AddNode(parent, SAVE_MISSIONS);

	cgi->Com_RegisterConstList(saveInterestConstants);
	cgi->Com_RegisterConstList(saveMissionConstants);
	MIS_Foreach(mission) {
		xmlNode_t* missionNode = cgi->XML_AddNode(missionsNode, SAVE_MISSIONS_MISSION);

		cgi->XML_AddInt(missionNode, SAVE_MISSIONS_MISSION_IDX, mission->idx);
		cgi->XML_AddString(missionNode, SAVE_MISSIONS_ID, mission->id);
		if (mission->mapDef)
			cgi->XML_AddString(missionNode, SAVE_MISSIONS_MAPDEF_ID, mission->mapDef->id);
		cgi->XML_AddBool(missionNode, SAVE_MISSIONS_ACTIVE, mission->active);
		cgi->XML_AddBool(missionNode, SAVE_MISSIONS_POSASSIGNED, mission->posAssigned);
		cgi->XML_AddBool(missionNode, SAVE_MISSIONS_CRASHED, mission->crashed);
		cgi->XML_AddString(missionNode, SAVE_MISSIONS_ONWIN, mission->onwin);
		cgi->XML_AddString(missionNode, SAVE_MISSIONS_ONLOSE, mission->onlose);
		cgi->XML_AddString(missionNode, SAVE_MISSIONS_CATEGORY, cgi->Com_GetConstVariable(SAVE_INTERESTCAT_NAMESPACE, mission->category));
		cgi->XML_AddString(missionNode, SAVE_MISSIONS_STAGE, cgi->Com_GetConstVariable(SAVE_MISSIONSTAGE_NAMESPACE, mission->stage));
		switch (mission->category) {
		case INTERESTCATEGORY_BASE_ATTACK:
			if (mission->stage == STAGE_MISSION_GOTO || mission->stage == STAGE_BASE_ATTACK) {
				const base_t* base = mission->data.base;
				/* save IDX of base under attack if required */
				cgi->XML_AddShort(missionNode, SAVE_MISSIONS_BASEINDEX, base->idx);
			}
			break;
		case INTERESTCATEGORY_INTERCEPT:
			if (mission->stage == STAGE_MISSION_GOTO || mission->stage == STAGE_INTERCEPT) {
				const installation_t* installation = mission->data.installation;
				if (installation)
					cgi->XML_AddShort(missionNode, SAVE_MISSIONS_INSTALLATIONINDEX, installation->idx);
			}
			break;
		case INTERESTCATEGORY_RESCUE:
			{
				const aircraft_t* aircraft = mission->data.aircraft;
				cgi->XML_AddShort(missionNode, SAVE_MISSIONS_CRASHED_AIRCRAFT, aircraft->idx);
			}
			break;
		case INTERESTCATEGORY_ALIENBASE:
		case INTERESTCATEGORY_BUILDING:
		case INTERESTCATEGORY_SUPPLY:
			{
				/* save IDX of alien base if required */
				const alienBase_t* base = mission->data.alienBase;
				/* there may be no base is the mission is a subverting government */
				if (base)
					cgi->XML_AddShort(missionNode, SAVE_MISSIONS_ALIENBASEINDEX, base->idx);
			}
			break;
		default:
			break;
		}
		cgi->XML_AddShort(missionNode, SAVE_MISSIONS_INITIALOVERALLINTEREST, mission->initialOverallInterest);
		cgi->XML_AddShort(missionNode, SAVE_MISSIONS_INITIALINDIVIDUALINTEREST, mission->initialIndividualInterest);
		cgi->XML_AddDate(missionNode, SAVE_MISSIONS_STARTDATE, mission->startDate.getDateAsDays(), mission->startDate.getTimeAsSeconds());
		cgi->XML_AddDate(missionNode, SAVE_MISSIONS_FINALDATE, mission->finalDate.getDateAsDays(), mission->finalDate.getTimeAsSeconds());
		cgi->XML_AddPos2(missionNode, SAVE_MISSIONS_POS, mission->pos);
		cgi->XML_AddBoolValue(missionNode, SAVE_MISSIONS_ONGEOSCAPE, mission->onGeoscape);
	}
	cgi->Com_UnregisterConstList(saveInterestConstants);
	cgi->Com_UnregisterConstList(saveMissionConstants);

	return true;
}

/**
 * @brief Load callback for savegames in XML Format
 * @param[in] parent XML Node structure, where we get the information from
 */
bool MIS_LoadXML (xmlNode_t* parent)
{
	xmlNode_t* missionNode;
	xmlNode_t* node;

	cgi->Com_RegisterConstList(saveInterestConstants);
	cgi->Com_RegisterConstList(saveMissionConstants);
	missionNode = cgi->XML_GetNode(parent, SAVE_MISSIONS);
	for (node = cgi->XML_GetNode(missionNode, SAVE_MISSIONS_MISSION); node;
			node = cgi->XML_GetNextNode(node, missionNode, SAVE_MISSIONS_MISSION)) {
		const char* name;
		mission_t mission;
		bool defaultAssigned = false;
		const char* categoryId = cgi->XML_GetString(node, SAVE_MISSIONS_CATEGORY);
		const char* stageId = cgi->XML_GetString(node, SAVE_MISSIONS_STAGE);

		OBJZERO(mission);

		Q_strncpyz(mission.id, cgi->XML_GetString(node, SAVE_MISSIONS_ID), sizeof(mission.id));
		mission.idx = cgi->XML_GetInt(node, SAVE_MISSIONS_MISSION_IDX, 0);
		if (mission.idx <= 0) {
			cgi->Com_Printf("mission has invalid or no index\n");
			continue;
		}

		name = cgi->XML_GetString(node, SAVE_MISSIONS_MAPDEF_ID);
		if (name && name[0] != '\0') {
			mission.mapDef = cgi->Com_GetMapDefinitionByID(name);
			if (!mission.mapDef) {
				cgi->Com_Printf("Warning: mapdef \"%s\" for mission \"%s\" doesn't exist. Removing mission!\n", name, mission.id);
				continue;
			}
		} else {
			mission.mapDef = nullptr;
		}

		if (!cgi->Com_GetConstIntFromNamespace(SAVE_INTERESTCAT_NAMESPACE, categoryId, (int*) &mission.category)) {
			cgi->Com_Printf("Invalid mission category '%s'\n", categoryId);
			continue;
		}

		if (!cgi->Com_GetConstIntFromNamespace(SAVE_MISSIONSTAGE_NAMESPACE, stageId, (int*) &mission.stage)) {
			cgi->Com_Printf("Invalid mission stage '%s'\n", stageId);
			continue;
		}

		mission.active = cgi->XML_GetBool(node, SAVE_MISSIONS_ACTIVE, false);
		Q_strncpyz(mission.onwin, cgi->XML_GetString(node, SAVE_MISSIONS_ONWIN), sizeof(mission.onwin));
		Q_strncpyz(mission.onlose, cgi->XML_GetString(node, SAVE_MISSIONS_ONLOSE), sizeof(mission.onlose));

		mission.initialOverallInterest = cgi->XML_GetInt(node, SAVE_MISSIONS_INITIALOVERALLINTEREST, 0);
		mission.initialIndividualInterest = cgi->XML_GetInt(node, SAVE_MISSIONS_INITIALINDIVIDUALINTEREST, 0);

		cgi->XML_GetPos2(node, SAVE_MISSIONS_POS, mission.pos);

		switch (mission.category) {
		case INTERESTCATEGORY_BASE_ATTACK:
			if (mission.stage == STAGE_MISSION_GOTO || mission.stage == STAGE_BASE_ATTACK) {
				/* Load IDX of base under attack */
				base_t* base = B_GetBaseByIDX(cgi->XML_GetInt(node, SAVE_MISSIONS_BASEINDEX, -1));
				if (base) {
					if (mission.stage == STAGE_BASE_ATTACK && !B_IsUnderAttack(base))
						cgi->Com_Printf("......warning: base %i (%s) is supposedly under attack but base status doesn't match!\n", base->idx, base->name);
					mission.data.base = base;
				} else
					cgi->Com_Printf("......warning: Missing BaseIndex\n");
			}
			break;
		case INTERESTCATEGORY_INTERCEPT:
			if (mission.stage == STAGE_MISSION_GOTO || mission.stage == STAGE_INTERCEPT)
				mission.data.installation = INS_GetByIDX(cgi->XML_GetInt(node, SAVE_MISSIONS_INSTALLATIONINDEX, -1));
			break;
		case INTERESTCATEGORY_RESCUE:
			{
				const int aircraftIdx = cgi->XML_GetInt(node, SAVE_MISSIONS_CRASHED_AIRCRAFT, -1);
				mission.data.aircraft = AIR_AircraftGetFromIDX(aircraftIdx);
				if (mission.data.aircraft == nullptr) {
					cgi->Com_Printf("Error while loading rescue mission (missionidx %i, aircraftidx: %i, category: %i, stage: %i)\n",
							mission.idx, aircraftIdx, mission.category, mission.stage);
					continue;
				}
			}
			break;
		case INTERESTCATEGORY_TERROR_ATTACK:
			if (mission.stage == STAGE_MISSION_GOTO || mission.stage == STAGE_TERROR_MISSION)
				mission.data.city = CITY_GetByPos(mission.pos);
			break;
		case INTERESTCATEGORY_ALIENBASE:
		case INTERESTCATEGORY_BUILDING:
		case INTERESTCATEGORY_SUPPLY:
			{
				int baseIdx = cgi->XML_GetInt(node, SAVE_MISSIONS_ALIENBASEINDEX, -1);
				alienBase_t* alienBase = AB_GetByIDX(baseIdx);
				if (alienBase) {
					if (mission.category != INTERESTCATEGORY_SUPPLY) {
						mission_t* duplicate = nullptr;
						MIS_Foreach(previousMission) {
							if (previousMission->category != mission.category)
								continue;
							if (previousMission->data.alienBase == alienBase) {
								duplicate = previousMission;
								break;
							}
						}
						if (duplicate) {
							cgi->Com_Printf("Error loading Alien Base mission (missionidx %i, baseidx: %i, category: %i, stage: %i): is the same as mission (missionidx %i, baseidx: %i, category: %i, stage: %i)\n",
								mission.idx, baseIdx, mission.category, mission.stage,
								duplicate->idx, duplicate->data.alienBase->idx, duplicate->category, duplicate->stage);
							continue;
						}
					}
					mission.data.alienBase = alienBase;
				} else {
					if (!CP_BasemissionIsSubvertingGovernmentMission(&mission) && mission.stage >= STAGE_BUILD_BASE) {
						cgi->Com_Printf("Error loading Alien Base mission (missionidx %i, baseidx: %i, category: %i, stage: %i): no such base\n",
							mission.idx, baseIdx, mission.category, mission.stage);
						continue;
					}
				}
			}
			break;
		default:
			break;
		}

		int date;
		int time;
		cgi->XML_GetDate(node, SAVE_MISSIONS_STARTDATE, &date , &time);
		mission.startDate = DateTime(date, time);
		cgi->XML_GetDate(node, SAVE_MISSIONS_FINALDATE, &date, &time);
		mission.finalDate = DateTime(date, time);
		cgi->XML_GetPos2(node, SAVE_MISSIONS_POS, mission.pos);

		mission.crashed = cgi->XML_GetBool(node, SAVE_MISSIONS_CRASHED, false);
		mission.onGeoscape = cgi->XML_GetBool(node, SAVE_MISSIONS_ONGEOSCAPE, false);

		if (mission.pos[0] > 0.001 || mission.pos[1] > 0.001)
			defaultAssigned = true;
		mission.posAssigned = cgi->XML_GetBool(node, SAVE_MISSIONS_POSASSIGNED, defaultAssigned);
		/* Add mission to global array */
		LIST_Add(&ccs.missions, mission);
	}
	cgi->Com_UnregisterConstList(saveInterestConstants);
	cgi->Com_UnregisterConstList(saveMissionConstants);

	return true;
}

static const cmdList_t debugMissionCmds[] = {
#ifdef DEBUG
	{"debug_missionsetmap", MIS_MissionSetMap_f, "Changes the map for a spawned mission"},
	{"debug_missionadd", MIS_SpawnNewMissions_f, "Add a new mission"},
	{"debug_missiondeleteall", MIS_DeleteMissions_f, "Remove all missions from global array"},
	{"debug_missiondelete", MIS_DeleteMission_f, "Remove mission by a given name"},
	{"debug_missionlist", MIS_MissionList_f, "Debug function to show all missions"},
#endif
	{nullptr, nullptr, nullptr}
};
/**
 * @brief Init actions for missions-subsystem
 * @sa UI_InitStartup
 */
void MIS_InitStartup (void)
{
	MIS_InitCallbacks();
	cgi->Cmd_TableAddList(debugMissionCmds);
}

/**
 * @brief Closing actions for missions-subsystem
 */
void MIS_Shutdown (void)
{
	cgi->LIST_Delete(&ccs.missions);

	MIS_ShutdownCallbacks();
	cgi->Cmd_TableRemoveList(debugMissionCmds);
}
