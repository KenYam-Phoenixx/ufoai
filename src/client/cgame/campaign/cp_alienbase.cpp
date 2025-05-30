/**
 * @file
 * @brief Alien base related functions
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

#include "../../cl_shared.h"
#include "cp_campaign.h"
#include "cp_alienbase.h"
#include "cp_geoscape.h"
#include "cp_missions.h"
#include "save/save_alienbase.h"

#define MAPDEF_ALIENBASE "alienbase"

/**
 * @brief Set new base position
 * @param[out] pos Position of the new base.
 * @note This function generates @c maxLoopPosition random positions, and select among those the one that
 * is the farthest from every other alien bases. This is intended to get a rather uniform distribution
 * of alien bases, while still keeping a random base localisation.
 */
void AB_SetAlienBasePosition (vec2_t pos)
{
	int counter;
	vec2_t randomPos;
	float minDistance = 0.0f;			/**< distance between current selected alien base */
	const int maxLoopPosition = 6;		/**< Number of random position among which the final one will be selected */

	counter = 0;
	while (counter < maxLoopPosition) {
		float distance = 0.0f;

		/* Get a random position */
		CP_GetRandomPosOnGeoscape(randomPos, true);

		/* Alien base must not be too close from phalanx base */
		if (GEO_PositionCloseToBase(randomPos))
			continue;

		/* If this is the first alien base, there's no further condition: select this pos and quit */
		if (AB_Exists()) {
			Vector2Copy(randomPos, pos);
			return;
		}

		/* Calculate minimim distance between THIS position (pos) and all alien bases */
		AB_Foreach(base) {
			const float currentDistance = GetDistanceOnGlobe(base->pos, randomPos);
			if (distance < currentDistance) {
				distance = currentDistance;
			}
		}

		/* If this position is farther than previous ones, select it */
		if (minDistance < distance) {
			Vector2Copy(randomPos, pos);
			minDistance = distance;
		}

		counter++;
	}
	if (counter == maxLoopPosition)
		Vector2Copy(randomPos, pos);
}

/**
 * @brief Build a new alien base
 * @param[in] pos Position of the new base.
 * @return Pointer to the base that has been built.
 */
alienBase_t* AB_BuildBase (const vec2_t pos)
{
	alienBase_t base;
	const float initialStealthValue = 50.0f;				/**< How hard PHALANX will find the base */

	OBJZERO(base);
	Vector2Copy(pos, base.pos);
	base.stealth = initialStealthValue;
	base.idx = ccs.campaignStats.alienBasesBuilt++;

	return &LIST_Add(&ccs.alienBases, base);
}

/**
 * @brief Destroy an alien base.
 * @param[in] base Pointer to the alien base.
 */
void AB_DestroyBase (alienBase_t* base)
{
	assert(base);

	cgi->LIST_Remove(&ccs.alienBases, (void*)base);

	/* Alien loose all their interest in supply if there's no base to send the supply */
	if (!AB_Exists())
		ccs.interest[INTERESTCATEGORY_SUPPLY] = 0;
}

/**
 * @brief Get Alien Base per Idx.
 * @param[in] baseIDX The unique IDX of the alien Base.
 * @return Pointer to the base.
 */
alienBase_t* AB_GetByIDX (int baseIDX)
{
	AB_Foreach(base) {
		if (base->idx == baseIDX)
			return base;
	}
	return nullptr;
}

/**
 * @brief Spawn a new alien base mission after it has been discovered.
 */
void CP_SpawnAlienBaseMission (alienBase_t* alienBase)
{
	/* Don't spawn more than one mission for a base */
	MIS_Foreach(mission) {
		if (mission->category != INTERESTCATEGORY_ALIENBASE)
			continue;
		if (mission->data.alienBase == alienBase)
			return;
	}

	mission_t* newMission = CP_CreateNewMission(INTERESTCATEGORY_ALIENBASE, true);
	if (!newMission) {
		cgi->Com_Printf("CP_SpawnAlienBaseMission: Could not add mission, abort\n");
		return;
	}

	newMission->stage = STAGE_BASE_DISCOVERED;
	newMission->data.alienBase = alienBase;

	newMission->mapDef = cgi->Com_GetMapDefinitionByID(MAPDEF_ALIENBASE);
	if (!newMission->mapDef)
		cgi->Com_Error(ERR_FATAL, "Could not find mapdef " MAPDEF_ALIENBASE);

	Vector2Copy(alienBase->pos, newMission->pos);
	newMission->posAssigned = true;

	/* Alien base stay until it's destroyed */
	CP_MissionDisableTimeLimit(newMission);
	/* mission appears on geoscape, player can go there */
	CP_MissionAddToGeoscape(newMission, false);

	CP_TriggerEvent(ALIENBASE_DISCOVERED, nullptr);
}

/**
 * @brief Update stealth value of one alien base due to one aircraft.
 * @param[in] aircraft Pointer to the aircraft_t.
 * @param[in] base Pointer to the alien base.
 * @note base stealth decreases if it is inside an aircraft radar range, and even more if it's
 * inside @c radarratio times radar range.
 * @sa UFO_UpdateAlienInterestForOneBase
 */
static void AB_UpdateStealthForOneBase (const aircraft_t* aircraft, alienBase_t* base)
{
	float distance;
	float probability = 0.0001f;			/**< base probability, will be modified below */
	const float radarratio = 0.4f;			/**< stealth decreases faster if base is inside radarratio times radar range */
	const float decreasingFactor = 5.0f;	/**< factor applied when outside @c radarratio times radar range */

	/* base is already discovered */
	if (base->stealth < 0)
		return;

	/* aircraft can't find base if it's too far */
	distance = GetDistanceOnGlobe(aircraft->pos, base->pos);
	if (distance > aircraft->radar.range)
		return;

	/* the bigger the base, the higher the probability to find it */
	probability *= base->supply;

	/* decrease probability if the base is far from aircraft */
	if (distance > aircraft->radar.range * radarratio)
		probability /= decreasingFactor;

	/* probability must depend on DETECTION_INTERVAL (in case we change the value) */
	probability *= DETECTION_INTERVAL;

	base->stealth -= probability;

	/* base discovered ? */
	if (base->stealth < 0) {
		base->stealth = -10.0f;		/* just to avoid rounding errors */
		CP_SpawnAlienBaseMission(base);
	}
}

/**
 * @brief Update stealth value of every base for every aircraft.
 * @note Called every @c DETECTION_INTERVAL
 * @sa CP_CampaignRun
 * @sa UFO_UpdateAlienInterestForOneBase
 */
void AB_UpdateStealthForAllBase (void)
{
	base_t* base = nullptr;
	while ((base = B_GetNext(base)) != nullptr) {
		AIR_ForeachFromBase(aircraft, base) {
			/* Only aircraft on geoscape can detect alien bases */
			if (!AIR_IsAircraftOnGeoscape(aircraft))
				continue;

			AB_Foreach(alienBase)
				AB_UpdateStealthForOneBase(aircraft, alienBase);
		}
	}
}

/**
 * @brief Nations help in searching alien base.
 * @note called once per day, but will update stealth only every @c daysPerWeek day
 * @sa CP_CampaignRun
 */
void AB_BaseSearchedByNations (void)
{
	const int daysPerWeek = 7;			/**< delay (in days) between base stealth update */
	float probability = 1.0f;			/**< base probability, will be modified below */
	const float xviLevel = 20.0f;			/**< xviInfection value of nation that will divide probability to
							  *  find alien base by 2 */

	/* Stealth is updated only once a week */
	if (ccs.date.getDateAsDays() % daysPerWeek)
		return;

	AB_Foreach(base) {
		const nation_t* nation = GEO_GetNation(base->pos);

		/* If nation is a lot infected, it won't help in finding base (government infected) */
		if (nation) {
			const nationInfo_t* stats = NAT_GetCurrentMonthInfo(nation);
			if (stats->xviInfection)
				probability /= 1.0f + stats->xviInfection / xviLevel;
		}

		/* the bigger the base, the higher the probability to find it */
		probability *= base->supply;

		base->stealth -= probability;
		if (base->stealth < 0) {
			base->stealth = -10.0f;		/* just to avoid rounding errors */
			CP_SpawnAlienBaseMission(base);
		}
	}
}

/**
 * @brief Check if a supply mission is possible.
 * @return True if there is at least one base to supply.
 */
bool AB_CheckSupplyMissionPossible (void)
{
	return AB_Exists();
}

/**
 * @brief Choose Alien Base that should be supplied.
 * @return Pointer to the base.
 */
alienBase_t* AB_ChooseBaseToSupply (void)
{
	const int baseCount = AB_GetAlienBaseNumber();

	if (baseCount <= 0) {
		cgi->Com_Printf("AB_ChooseBaseToSupply: no bases exists (basecount: %d)\n", baseCount);
		return nullptr;
	}

	const int selected = rand() % baseCount;

	int i = 0;
	AB_Foreach(alienBase) {
		if (i == selected)
			return alienBase;
		i++;
	}
	return nullptr;
}

/**
 * @brief Supply a base.
 * @param[in] base Pointer to the supplied base.
 * @param[in] decreaseStealth If the stealth level of the base should be decreased.
 */
void AB_SupplyBase (alienBase_t* base, bool decreaseStealth)
{
	const float decreasedStealthValue = 5.0f;				/**< How much stealth is reduced because Supply UFO was seen */

	assert(base);

	base->supply++;
	if (decreaseStealth && base->stealth >= 0.0f)
		base->stealth -= decreasedStealthValue;
}

/**
 * @brief Check number of alien bases.
 * @return number of alien bases.
 */
int AB_GetAlienBaseNumber (void)
{
	return cgi->LIST_Count(ccs.alienBases);
}

#ifdef DEBUG
/**
 * @brief Print Alien Bases information to game console
 */
static void AB_AlienBaseDiscovered_f (void)
{
	AB_Foreach(base) {
		base->stealth = -10.0f;
		CP_SpawnAlienBaseMission(base);
	}
}

/**
 * @brief Print Alien Bases information to game console
 * @note called with debug_listalienbase
 */
static void AB_AlienBaseList_f (void)
{
	AB_Foreach(base) {
		cgi->Com_Printf("Alien Base: %i\n", base->idx);
		cgi->Com_Printf("...pos: (%f, %f)\n", base->pos[0], base->pos[1]);
		cgi->Com_Printf("...supply: %i\n", base->supply);
		if (base->stealth < 0)
			cgi->Com_Printf("...base discovered\n");
		else
			cgi->Com_Printf("...stealth: %f\n", base->stealth);
	}
}
#endif

/**
 * @brief Load callback for alien base data
 * @param[in] p XML Node structure, where we get the information from
 * @sa AB_SaveXML
 */
bool AB_LoadXML (xmlNode_t* p)
{
	int i; /**< @todo this is for old saves now only */
	xmlNode_t* n, *s;

	n = cgi->XML_GetNode(p, SAVE_ALIENBASE_ALIENBASES);
	if (!n)
		return false;

	for (i = 0, s = cgi->XML_GetNode(n, SAVE_ALIENBASE_BASE); s; i++, s = cgi->XML_GetNextNode(s, n, SAVE_ALIENBASE_BASE)) {
		alienBase_t base;

		base.idx = cgi->XML_GetInt(s, SAVE_ALIENBASE_IDX, -1);
		if (base.idx < 0) {
			cgi->Com_Printf("Invalid or no IDX defined for Alienbase %d.\n", i);
			return false;
		}
		if (!cgi->XML_GetPos2(s, SAVE_ALIENBASE_POS, base.pos)) {
			cgi->Com_Printf("Position is invalid for Alienbase (idx %d)\n", base.idx);
			return false;
		}
		base.supply = cgi->XML_GetInt(s, SAVE_ALIENBASE_SUPPLY, 0);
		base.stealth = cgi->XML_GetFloat(s, SAVE_ALIENBASE_STEALTH, 0.0);
		LIST_Add(&ccs.alienBases, base);
	}

	return true;
}

/**
 * @brief Save callback for alien base data
 * @param[out] p XML Node structure, where we write the information to
 * @sa AB_LoadXML
 */
bool AB_SaveXML (xmlNode_t* p)
{
	xmlNode_t* n = cgi->XML_AddNode(p, SAVE_ALIENBASE_ALIENBASES);

	AB_Foreach(base) {
		xmlNode_t* s = cgi->XML_AddNode(n, SAVE_ALIENBASE_BASE);
		cgi->XML_AddInt(s, SAVE_ALIENBASE_IDX, base->idx);
		cgi->XML_AddPos2(s, SAVE_ALIENBASE_POS, base->pos);
		cgi->XML_AddIntValue(s, SAVE_ALIENBASE_SUPPLY, base->supply);
		cgi->XML_AddFloatValue(s, SAVE_ALIENBASE_STEALTH, base->stealth);
	}

	return true;
}

static const cmdList_t debugAlienBaseCmds[] = {
#ifdef DEBUG
	{"debug_listalienbase", AB_AlienBaseList_f, "Print Alien Bases information to game console"},
	{"debug_alienbasevisible", AB_AlienBaseDiscovered_f, "Set all alien bases to discovered"},
#endif
	{nullptr, nullptr, nullptr}
};
/**
 * @brief Init actions for alienbase-subsystem
 * @sa UI_InitStartup
 */
void AB_InitStartup (void)
{
	cgi->Cmd_TableAddList(debugAlienBaseCmds);
}

/**
 * @brief Closing actions for alienbase-subsystem
 */
void AB_Shutdown (void)
{
	cgi->LIST_Delete(&ccs.alienBases);

	cgi->Cmd_TableRemoveList(debugAlienBaseCmds);
}
