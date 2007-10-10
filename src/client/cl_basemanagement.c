/**
 * @file cl_basemanagement.c
 * @brief Handles everything that is located in or accessed trough a base.
 * @note Basemanagement functions prefix: B_

 *
 * See "base/ufos/basemanagement.ufo", "base/ufos/menu_bases.ufo" and "base/ufos/menu_buildings.ufo" for the underlying content.
 * @todo New game does not reset basemangagement
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

#include "client.h"
#include "cl_global.h"

vec3_t newBasePos;
static cvar_t *mn_base_title;
static cvar_t *mn_base_count;
static cvar_t *mn_base_id;

static int BuildingConstructionList[MAX_BUILDINGS];
static int numBuildingConstructionList;

static void B_BuildingInit(void);

/**
 * @brief Count all employees (hired) in the given base
 */
int B_GetEmployeeCount (const base_t* const base)
{
	int cnt = 0;
	employeeType_t type;

	for (type = EMPL_SOLDIER; type < MAX_EMPL; type++)
		cnt += E_CountHired(base, type);
	Com_DPrintf(DEBUG_CLIENT, "B_GetEmployeeCount: %i\n", cnt);

	return cnt;
}

/**
 * @brief Searches the base for a given building type with the given status
 * @param[in] base Base to search
 * @param[in] type Building type to search
 * @param[in] status The status the building should have
 * @param[out] cnt This is a pointer to an int value which will hold the building
 * count of that type with the status you are searching - might also be NULL
 * if you are not interested in this value
 * @note If you are searching for a quarter (e.g.) you should perform a
 * <code>if (base->hasQuarters)</code> check - this is not available for all
 * building types but should speed things up a lot
 * @return true if building with status was found
 */
qboolean B_CheckBuildingTypeStatus (const base_t* const base, buildingType_t type, buildingStatus_t status, int *cnt)
{
	int cntlocal = 0, i;

	for (i = 0; i < gd.numBuildings[base->idx]; i++) {
		if (gd.buildings[base->idx][i].buildingType == type
		 && gd.buildings[base->idx][i].buildingStatus == status) {
			cntlocal++;
			/* don't count any further - the caller doesn't want to know the value */
			if (!cnt)
				return qtrue;
		}
	}

	/* set the cnt pointer if the caller wants to know this value */
	if (cnt)
		*cnt = cntlocal;

	return cntlocal ? qtrue : qfalse;
}

/**
 * @brief Sums up max_employees quarter values
 * @param[in] base The base to count the free space in.
 * @return int The total number of space in quarters.
 */
int B_GetAvailableQuarterSpace (const base_t* const base)
{
	int cnt = 0, i;

	if (base->hasQuarters)
		for (i = 0; i < gd.numBuildings[base->idx]; i++) {
			if (gd.buildings[base->idx][i].buildingType == B_QUARTERS
			&& gd.buildings[base->idx][i].buildingStatus != B_STATUS_NOT_SET)
				cnt += gd.buildings[base->idx][i].maxEmployees;
		}
	Com_DPrintf(DEBUG_CLIENT, "B_GetAvailableQuarterSpace: %i\n", cnt);

	return cnt;
}

/**
 * @note Make sure you are not doing anything with the buildingCurrent pointer
 * in this function, the pointer might already be invalid
 */
static void B_ResetBuildingCurrent (base_t* base)
{
	if (base) {
		base->buildingCurrent = NULL;
		base->buildingToBuild = -1;
	}
	gd.baseAction = BA_NONE;
}

/**
 * @brief Resets the currently selected building.
 *
 * Is called e.g. when leaving the build-menu but also several times from cl_basemanagement.c.
 */
static void B_ResetBuildingCurrent_f (void)
{
	if (Cmd_Argc() == 2)
		gd.instant_build = atoi(Cmd_Argv(1));

	B_ResetBuildingCurrent(baseCurrent);
}

/**
 * @brief Holds the names of valid entries in the basemanagement.ufo file.
 *
 * The valid definition names for BUILDINGS (building_t) in the basemagagement.ufo file.
 * to the appropriate values in the corresponding struct
 */
static const value_t valid_building_vars[] = {
	{"map_name", V_CLIENT_HUNK_STRING, offsetof(building_t, mapPart), 0},	/**< Name of the map file for generating basemap. */
	{"more_than_one", V_BOOL, offsetof(building_t, moreThanOne), MEMBER_SIZEOF(building_t, moreThanOne)},	/**< Is the building allowed to be build more the one time? */
	{"name", V_TRANSLATION_MANUAL_STRING, offsetof(building_t, name), 0},	/**< The displayed building name. */
	{"pedia", V_CLIENT_HUNK_STRING, offsetof(building_t, pedia), 0},	/**< The pedia-id string for the associated pedia entry. */
	{"status", V_INT, offsetof(building_t, buildingStatus), MEMBER_SIZEOF(building_t, buildingStatus)},	/**< The current status of the building. */
	{"image", V_CLIENT_HUNK_STRING, offsetof(building_t, image), 0},	/**< Identifies the image for the building. */
	{"visible", V_BOOL, offsetof(building_t, visible), MEMBER_SIZEOF(building_t, visible)}, /**< Determines whether a building should be listed in the construction list. Set the first part of a building to 1 all others to 0 otherwise all building-parts will be on the list */
	{"needs", V_CLIENT_HUNK_STRING, offsetof(building_t, needs), 0},	/**<  For buildings with more than one part; the other parts of the building needed.*/
	{"fixcosts", V_INT, offsetof(building_t, fixCosts), MEMBER_SIZEOF(building_t, fixCosts)},	/**< Cost to build. */
	{"varcosts", V_INT, offsetof(building_t, varCosts), MEMBER_SIZEOF(building_t, varCosts)},	/**< Costs that will come up by using the building. */
	{"build_time", V_INT, offsetof(building_t, buildTime), MEMBER_SIZEOF(building_t, buildTime)},	/**< How many days it takes to construct the building. */
	{"max_employees", V_INT, offsetof(building_t, maxEmployees), MEMBER_SIZEOF(building_t, maxEmployees)},	/**< How many employees to hire on construction in the first base. */
	{"capacity", V_INT, offsetof(building_t, capacity), MEMBER_SIZEOF(building_t, capacity)},	/**< A size value that is used by many buildings in a different way. */

	/*event handler functions */
	{"onconstruct", V_STRING, offsetof(building_t, onConstruct), 0}, /**< Event handler. */
	{"onattack", V_STRING, offsetof(building_t, onAttack), 0}, /**< Event handler. */
	{"ondestroy", V_STRING, offsetof(building_t, onDestroy), 0}, /**< Event handler. */
	{"onupgrade", V_STRING, offsetof(building_t, onUpgrade), 0}, /**< Event handler. */
	{"onrepair", V_STRING, offsetof(building_t, onRepair), 0}, /**< Event handler. */
	{"onclick", V_STRING, offsetof(building_t, onClick), 0}, /**< Event handler. */
	{"pos", V_POS, offsetof(building_t, pos), MEMBER_SIZEOF(building_t, pos)}, /**< Place of a building. Needed for flag autobuild */
	{"autobuild", V_BOOL, offsetof(building_t, autobuild), MEMBER_SIZEOF(building_t, autobuild)}, /**< Automatically construct this building when a base is set up. Must also set the pos-flag. */
	{"firstbase", V_BOOL, offsetof(building_t, firstbase), MEMBER_SIZEOF(building_t, firstbase)}, /**< Automatically construct this building for the first base you build. Must also set the pos-flag. */
	{NULL, 0, 0, 0}
};

/**
 * @brief Sets a sensor.
 *
 * inc_sensor and dec_sensor are script commands that increase the amount
 * of the radar width for a given base
 */
void B_SetSensor_f (void)
{
	int i = 0;
	int amount = 0;
	base_t* base;

	if (Cmd_Argc() < 3) {
		Com_Printf("Usage: %s <amount> <baseID>\n", Cmd_Argv(0));
		return;
	}

	i = atoi(Cmd_Argv(2));
	if (i >= gd.numBases) {
		Com_Printf("invalid baseID (%s)\n", Cmd_Argv(2));
		return;
	}
	amount = atoi(Cmd_Argv(1));
	base = gd.bases + i;

	if (!Q_strncmp(Cmd_Argv(0), "inc", 3))
		RADAR_ChangeRange(&(base->radar), amount);	/* inc_sensor */
	else if (!Q_strncmp(Cmd_Argv(0), "dec", 3))
		RADAR_ChangeRange(&(base->radar), -amount);	/* dec_sensor */
}

/**
 * @brief Initialises base.
 * @note This command is executed in the init node of the base menu.
 * It is called everytime the base menu pops up and sets the cvars.
 * The current selected base is determined via cvar mn_base_id.
 */
static void B_BaseInit_f (void)
{
	if (!mn_base_id)
		return;

	/* sanity check */
	if (mn_base_id->integer < 0 || mn_base_id->integer > B_GetFoundedBaseCount()) {
		Com_Printf("B_BaseInit_f: mn_base_id value is invalid: %i\n", mn_base_id->integer);
		return;
	}

	baseCurrent = &gd.bases[mn_base_id->integer];

	CL_UpdateCredits(ccs.credits);

	/* activate or deactivate the aircraft button */
	if (AIR_AircraftAllowed()) {
		Cvar_SetValue("mn_base_num_aircraft", baseCurrent->numAircraftInBase);
		Cmd_ExecuteString("set_aircraft_enabled");
	} else {
		Cvar_SetValue("mn_base_num_aircraft", -1);
		Cmd_ExecuteString("set_aircraft_disabled");
	}
	if (BS_BuySellAllowed()) {
		Cvar_SetValue("mn_base_buysell_allowed", qtrue);
		Cmd_ExecuteString("set_buysell_enabled");
	} else {
		Cvar_SetValue("mn_base_buysell_allowed", qfalse);
		Cmd_ExecuteString("set_buysell_disabled");
	}
	if (gd.numBases > 1) {
		Cmd_ExecuteString("set_transfer_enabled");
	} else {
		Cmd_ExecuteString("set_transfer_disabled");
	}
	if (RS_ResearchAllowed()) {
		Cvar_SetValue("mn_base_research_allowed", qtrue);
		Cmd_ExecuteString("set_research_enabled");
	} else {
		Cvar_SetValue("mn_base_research_allowed", qfalse);
		Cmd_ExecuteString("set_research_disabled");
	}
	Cvar_SetValue("mn_base_prod_allowed", PR_ProductionAllowed());
	if (E_HireAllowed()) {
		Cvar_SetValue("mn_base_hire_allowed", qtrue);
		Cmd_ExecuteString("set_hire_enabled");
	} else {
		Cvar_SetValue("mn_base_hire_allowed", qfalse);
		Cmd_ExecuteString("set_hire_disabled");
	}
	if (AC_ContainmentAllowed()) {
		Cvar_SetValue("mn_base_containment_allowed", qtrue);
		Cmd_ExecuteString("set_containment_enabled");
	} else {
		Cvar_SetValue("mn_base_containment_allowed", qfalse);
		Cmd_ExecuteString("set_containment_disabled");
	}
	if (HOS_HospitalAllowed()) {
		Cvar_SetValue("mn_base_hospital_allowed", qtrue);
		Cmd_ExecuteString("set_hospital_enabled");
	} else {
		Cvar_SetValue("mn_base_hospital_allowed", qfalse);
		Cmd_ExecuteString("set_hospital_disabled");
	}
}

/**
 * @brief Removes a building from the given base
 * @param[in] base Base to remove the building in
 * @param[in] building The building to remove
 * @note Also updates capacities and sets the has* values in base_t
 * @sa B_BuildingDestroy_f
 */
qboolean B_BuildingDestroy (base_t* base, building_t* building)
{
	buildingType_t type = building->buildingType;
	baseCapacities_t cap = MAX_CAP; /* init but don't set to first value of enum */
	char onDestroy[MAX_VAR];

	/* Don't allow to destroy an entrance. */
	if (type == B_ENTRANCE)
		return qfalse;

	Q_strncpyz(onDestroy, building->onDestroy, sizeof(onDestroy));

	switch (base->map[(int)building->pos[0]][(int)building->pos[1]]) {
	case BASE_FREESLOT:
	case BASE_INVALID_SPACE:
		assert(0);
		return qfalse;
	default:
		if (building->needs) {
			/* "Child" building is always right to the "parent" building". */
			base->map[(int)building->pos[0]][((int)building->pos[1]) + 1] = BASE_FREESLOT;
		}

		base->map[(int)building->pos[0]][(int)building->pos[1]] = BASE_FREESLOT;
		break;
	}
	building->buildingStatus = B_STATUS_NOT_SET;

	{
		building_t* const buildings = gd.buildings[base->idx];
		const int         cnt_bldgs = gd.numBuildings[base->idx] - 1;
		const int         idx       = building->idx;
		int               row;
		int               col;

		gd.numBuildings[base->idx] = cnt_bldgs;

		memmove(building, building + 1, (cnt_bldgs - idx) * sizeof(*building));
		/* wipe the now vacant last slot */
		memset(&buildings[cnt_bldgs], 0, sizeof(buildings[cnt_bldgs]));

		/* adjust the base map indices for the removed building */
		for (row = 0; row < BASE_SIZE; ++row) {
			for (col = 0; col < BASE_SIZE; ++col) {
				if (base->map[row][col] > idx) {
					--base->map[row][col];
				}
			}
		}
	}
	/* don't use the building pointer after this point - it's zeroed or points to a wrong entry now */

	switch (type) {
	case B_WORKSHOP:
		cap = CAP_WORKSPACE;
		if (B_GetNumberOfBuildingsInBaseByType(base->idx, type) <= 0)
			base->hasWorkshop = qfalse;
		break;
	case B_STORAGE:
		cap = CAP_ITEMS;
		if (B_GetNumberOfBuildingsInBaseByType(base->idx, type) <= 0)
			base->hasStorage = qfalse;
		break;
	case B_ALIEN_CONTAINMENT:
		cap = CAP_ALIENS;
		if (B_GetNumberOfBuildingsInBaseByType(base->idx, type) <= 0)
			base->hasAlienCont = qfalse;
		break;
	case B_LAB:
		cap = CAP_LABSPACE;
		if (B_GetNumberOfBuildingsInBaseByType(base->idx, type) <= 0)
			base->hasLab = qfalse;
		break;
	case B_HOSPITAL:
		cap = CAP_HOSPSPACE;
		if (B_GetNumberOfBuildingsInBaseByType(base->idx, type) <= 0)
			base->hasHospital = qfalse;
		break;
	case B_HANGAR: /* the Dropship Hangar */
		cap = CAP_AIRCRAFTS_BIG;
		if (B_GetNumberOfBuildingsInBaseByType(base->idx, type) <= 0)
			base->hasHangar = qfalse;
		break;
	case B_QUARTERS:
		cap = CAP_EMPLOYEES;
		if (B_GetNumberOfBuildingsInBaseByType(base->idx, type) <= 0)
			base->hasQuarters = qfalse;
		break;
	case B_SMALL_HANGAR:
		cap = CAP_AIRCRAFTS_SMALL;
		if (B_GetNumberOfBuildingsInBaseByType(base->idx, type) <= 0)
			base->hasHangarSmall = qfalse;
		break;
	case B_UFO_HANGAR:
	case B_UFO_SMALL_HANGAR:
		cap = CAP_UFOHANGARS;
		if (B_GetNumberOfBuildingsInBaseByType(base->idx, B_UFO_HANGAR) <= 0)
			base->hasUFOHangar = qfalse;
		if (B_GetNumberOfBuildingsInBaseByType(base->idx, B_UFO_SMALL_HANGAR) <= 0)
			base->hasUFOHangarSmall = qfalse;
		break;
	case B_POWER:
		if (B_GetNumberOfBuildingsInBaseByType(base->idx, type) <= 0)
			base->hasPower = qfalse;
		B_UpdateStatusWithPower(base);
		break;
	case B_COMMAND:
		if (B_GetNumberOfBuildingsInBaseByType(base->idx, type) <= 0)
			base->hasCommand = qfalse;
		break;
	case B_ANTIMATTER:
		cap = CAP_ANTIMATTER;
		if (B_GetNumberOfBuildingsInBaseByType(base->idx, type) <= 0) {
			base->hasAmStorage = qfalse;
			/* Remove antimatter. */
			INV_ManageAntimatter(base, 0, qfalse);
		}
		break;
	case B_DEFENSE_MISSILE:
		if (B_GetNumberOfBuildingsInBaseByType(base->idx, type) <= 0)
			base->hasMissile = qfalse;
		break;
	case B_DEFENSE_LASER:
		if (B_GetNumberOfBuildingsInBaseByType(base->idx, type) <= 0)
			base->hasLaser = qfalse;
		break;
	case B_MISC:
		break;
	default:
		Com_Printf("B_BuildingDestroy: Unknown building type: %i.\n", type);
		break;
	}

	/* call ondestroy trigger */
	if (*onDestroy) {
		Com_DPrintf(DEBUG_CLIENT, "B_BuildingDestroy: %s %i;\n", onDestroy, base->idx);
		Cbuf_AddText(va("%s %i;", onDestroy, base->idx));
	}

	B_BaseInit_f();

	/* Update capacities - but don't update all */
	if (cap != MAX_CAP)
		B_UpdateBaseCapacities(cap, base);

	/* Update production times in queue if we destroyed B_WORKSHOP. */
	if (type == B_WORKSHOP) {
		PR_UpdateProductionTime(base);
	}
	/* Remove aliens if needed. */
	if (type == B_ALIEN_CONTAINMENT) {
		if (!base->hasAlienCont) {	/* Just clean containment. */
			AL_FillInContainment(base);
		} else {	/* Check capacities and remove needed amount. */
			if (base->capacities[CAP_ALIENS].cur - base->capacities[CAP_ALIENS].max > 0)
				AL_RemoveAliens(base, NULL, (base->capacities[CAP_ALIENS].cur - base->capacities[CAP_ALIENS].max), AL_RESEARCH);
		}
	}

	return qtrue;
}

/**
 * @brief We are doing the real destroy of a buliding here
 * @sa B_BuildingDestroy
 * @sa B_NewBuilding
 */
static void B_BuildingDestroy_f (void)
{
	if (!baseCurrent || !baseCurrent->buildingCurrent)
		return;

	B_BuildingDestroy(baseCurrent, baseCurrent->buildingCurrent);

	B_ResetBuildingCurrent(baseCurrent);
}

/**
 * @brief Mark a building for destruction - you only have to confirm it now
 * @note Also calls the ondestroy trigger
 */
void B_MarkBuildingDestroy (base_t* base, building_t* building)
{
	assert(base);
	assert(building);

	base->buildingCurrent = building;

	MN_PushMenu("building_destroy");
}

/**
 * @brief Displays the status of a building for baseview.
 *
 * updates the cvar mn_building_status which is used in some menus to display
 * the building status
 * @note also script command function binding for 'building_status'
 */
void B_BuildingStatus (base_t* base, building_t* building)
{
	/* int daysLeft; */
	int numberOfBuildings = 0;

	assert(building);
	assert(base);

	/**
	 * @todo Was this planned to be used anywhere (e.g. for B_STATUS_UNDER_CONSTRUCTION text)
	 * or was it removed intentionally?
	 * daysLeft = building->timeStart + building->buildTime - ccs.date.day;
	*/

	Cvar_Set("mn_building_status", _("Not set"));

	switch (building->buildingStatus) {
	case B_STATUS_NOT_SET:
		numberOfBuildings = B_GetNumberOfBuildingsInBaseByTypeIDX(base->idx, building->type_idx);
		if (numberOfBuildings >= 0)
			Cvar_Set("mn_building_status", va(_("Already %i in base"), numberOfBuildings));
		break;
	case B_STATUS_UNDER_CONSTRUCTION:
		Cvar_Set("mn_building_status", "");
		break;
	case B_STATUS_CONSTRUCTION_FINISHED:
		Cvar_Set("mn_building_status", _("Construction finished"));
		break;
	case B_STATUS_WORKING:
		Cvar_Set("mn_building_status", _("Working 100%"));
		break;
	case B_STATUS_DOWN:
		Cvar_Set("mn_building_status", _("Down"));
		break;
	default:
		break;
	}
}

/**
 * @brief Console callback for B_BuildingStatus
 * @sa B_BuildingStatus
 */
static void B_BuildingStatus_f (void)
{
	/* maybe someone called this command before the buildings are parsed?? */
	if (!baseCurrent || !baseCurrent->buildingCurrent)
		return;

	B_BuildingStatus(baseCurrent, baseCurrent->buildingCurrent);
}

/**
 * @brief  Hires some employees of appropriate type for a building
 * @param[in] building in which building
 * @param[in] num how many employees, if -1, hire building->maxEmployees
 * @sa B_SetUpBase
 */
static void B_HireForBuilding (base_t* base, building_t * building, int num)
{
	employeeType_t employeeType;

	assert(base);

	if (num < 0)
		num = building->maxEmployees;

	if (num) {
		switch (building->buildingType) {
		case B_WORKSHOP:
			employeeType = EMPL_WORKER;
			break;
		case B_ALIEN_CONTAINMENT:
		case B_LAB:
			employeeType = EMPL_SCIENTIST;
			break;
		case B_HOSPITAL:
			employeeType = EMPL_MEDIC;
			break;
		case B_HANGAR: /* the Dropship Hangar */
			employeeType = EMPL_SOLDIER;
			break;
		case B_QUARTERS:
			return;
		case B_MISC:
			Com_DPrintf(DEBUG_CLIENT, "B_HireForBuilding: Misc building type: %i with employees: %i.\n", building->buildingType, num);
			return;
		default:
			Com_DPrintf(DEBUG_CLIENT, "B_HireForBuilding: Unknown building type: %i.\n", building->buildingType);
			return;
		}
		if (num > gd.numEmployees[employeeType])
			num = gd.numEmployees[employeeType];
		for (;num--;)
			if (!E_HireEmployeeByType(base, employeeType)) {
				Com_DPrintf(DEBUG_CLIENT, "B_HireForBuilding: Hiring %i employee(s) of type %i failed.\n", num, employeeType);
				return;
			}
	}
}

/**
 * @brief Updates base status for particular buildings as well as capacities.
 * @param[in] *building Pointer to building.
 * @param[in] *base Pointer to base with given building.
 * @param[in] status Enum of buildingStatus_t which is status of given building.
 * @note This function checks whether a building has B_STATUS_WORKING status, and
 * @note then updates base status for particular buildings and base capacities.
 */
static void B_UpdateBaseBuildingStatus (building_t* building, base_t* base, buildingStatus_t status)
{
	assert(base);
	assert(building);

	building->buildingStatus = status;

	switch (building->buildingType) {
	case B_ALIEN_CONTAINMENT:
		if ((building->buildingStatus == B_STATUS_WORKING) && (base->hasPower))
			base->hasAlienCont = qtrue;
		B_UpdateBaseCapacities(CAP_ALIENS, base);
		break;
	case B_QUARTERS:
		if (building->buildingStatus == B_STATUS_WORKING)
			base->hasQuarters = qtrue;
		B_UpdateBaseCapacities(CAP_EMPLOYEES, base);
		break;
	case B_STORAGE:
		if ((building->buildingStatus == B_STATUS_WORKING) && (base->hasPower))
			base->hasStorage = qtrue;
		B_UpdateBaseCapacities(CAP_ITEMS, base);
		break;
	case B_LAB:
		if ((building->buildingStatus == B_STATUS_WORKING) && (base->hasPower))
			base->hasLab = qtrue;
		B_UpdateBaseCapacities(CAP_LABSPACE, base);
		break;
	case B_WORKSHOP:
		if ((building->buildingStatus == B_STATUS_WORKING) && (base->hasPower))
			base->hasWorkshop = qtrue;
		B_UpdateBaseCapacities(CAP_WORKSPACE, base);
		/* Update production times in queue. */
		PR_UpdateProductionTime(base);
		break;
	case B_HOSPITAL:
		if ((building->buildingStatus == B_STATUS_WORKING) && (base->hasPower)) {
			/* If this is first hospital in base, setup relevant arrays. */
			if (B_GetNumberOfBuildingsInBaseByType(base->idx, building->buildingType) == 1) {
				memset(base->hospitalList, -1, sizeof(base->hospitalList));
				memset(base->hospitalListCount, 0, sizeof(base->hospitalListCount));
				memset(base->hospitalMissionList, -1, sizeof(base->hospitalMissionList));
				base->hospitalMissionListCount = 0;
			}
			base->hasHospital = qtrue;
		}
		B_UpdateBaseCapacities(CAP_HOSPSPACE, base);
		break;
	case B_HANGAR:
		if ((building->buildingStatus == B_STATUS_WORKING) && (base->hasPower))
			base->hasHangar = qtrue;
		B_UpdateBaseCapacities(CAP_AIRCRAFTS_BIG, base);
		break;
	case B_SMALL_HANGAR:
		if ((building->buildingStatus == B_STATUS_WORKING) && (base->hasPower))
			base->hasHangarSmall = qtrue;
		B_UpdateBaseCapacities(CAP_AIRCRAFTS_SMALL, base);
		break;
	case B_UFO_HANGAR:
		if ((building->buildingStatus == B_STATUS_WORKING) && (base->hasPower))
			base->hasUFOHangar = qtrue;
		B_UpdateBaseCapacities(CAP_UFOHANGARS, base);
		break;
	case B_UFO_SMALL_HANGAR:
		if ((building->buildingStatus == B_STATUS_WORKING) && (base->hasPower))
			base->hasUFOHangarSmall = qtrue;
		B_UpdateBaseCapacities(CAP_UFOHANGARS, base);
		break;
	case B_POWER:
		if (building->buildingStatus == B_STATUS_WORKING)
			base->hasPower = qtrue;
		B_UpdateStatusWithPower(base);
		break;
	case B_COMMAND:
		if ((building->buildingStatus == B_STATUS_WORKING) && (base->hasPower))
			base->hasCommand = qtrue;
		break;
	case B_ANTIMATTER:
		if ((building->buildingStatus == B_STATUS_WORKING) && (base->hasPower))
			base->hasAmStorage = qtrue;
		break;
	case B_DEFENSE_MISSILE:
		if ((building->buildingStatus == B_STATUS_WORKING) && (base->hasPower))
			base->hasMissile = qtrue;
		break;
	case B_DEFENSE_LASER:
		if ((building->buildingStatus == B_STATUS_WORKING) && (base->hasPower))
			base->hasLaser = qtrue;
		break;
	default:
		break;
	}
	/* @todo: this should be an user option defined in Game Options. */
	CL_GameTimeStop();
}

/**
 * @brief Setup new base
 * @sa CL_NewBase
 */
void B_SetUpBase (base_t* base)
{
	int i;
	building_t *building = NULL;
	aircraft_t *aircraft = NULL;

	assert(base);
	/* Resent current capacities. */
	for (i = 0; i < MAX_CAP; i++)
		base->capacities[i].cur = 0;

	/* update the building-list */
	B_BuildingInit();
	Com_DPrintf(DEBUG_CLIENT, "Set up for %i\n", base->idx);

	/* this cvar is used for disabling the base build button on geoscape if MAX_BASES (8) was reached */
	Cvar_Set("mn_base_count", va("%i", mn_base_count->integer + 1));

	/* this cvar is needed by B_SetBuildingByClick below*/
	Cvar_SetValue("mn_base_id", base->idx);

	for (i = 0; i < gd.numBuildingTypes; i++) {
		if (gd.buildingTypes[i].autobuild
			|| (gd.numBases == 1
				&& gd.buildingTypes[i].firstbase
				&& cl_start_buildings->integer)) {
			/* @todo: implement check for moreThanOne */
			building = &gd.buildings[base->idx][gd.numBuildings[base->idx]];
			*building = gd.buildingTypes[i];
			/* self-link to building-list in base */
			building->idx = gd.numBuildings[base->idx];
			gd.numBuildings[base->idx]++;
			/* Link to the base. */
			building->base_idx = base->idx;
			Com_DPrintf(DEBUG_CLIENT, "Base %i new building:%s (%i) at (%.0f:%.0f)\n", base->idx, building->id, i, building->pos[0], building->pos[1]);
			base->buildingCurrent = building;
			/* fake a click to basemap */
			B_SetBuildingByClick((int) building->pos[0], (int) building->pos[1]);
			B_UpdateBaseBuildingStatus(building, base, B_STATUS_WORKING);
			/* Now buy two first aircrafts if it is our first base. */
			if (gd.numBases == 1 && building->buildingType == B_HANGAR) {
				Cbuf_AddText("aircraft_new craft_drop_firebird\n");
				aircraft = AIR_GetAircraft("craft_drop_firebird");
				CL_UpdateCredits(ccs.credits - aircraft->price);
			}
			if (gd.numBases == 1 && building->buildingType == B_SMALL_HANGAR) {
				Cbuf_AddText("aircraft_new craft_inter_stiletto\n");
				aircraft = AIR_GetAircraft("craft_inter_stiletto");
				CL_UpdateCredits(ccs.credits - aircraft->price);
			}

			/* now call the onconstruct trigger */
			if (*building->onConstruct) {
				base->buildingCurrent = building;
				Com_DPrintf(DEBUG_CLIENT, "B_SetUpBase: %s %i;\n", building->onConstruct, base->idx);
				Cbuf_AddText(va("%s %i;", building->onConstruct, base->idx));
			}

			/* update the building-list */
			B_BuildingInit();

			if (cl_start_employees->integer)
				B_HireForBuilding(base, building, -1);
		}
	}
	/* if no autobuild, set up zero build time for the first base */
	if (gd.numBases == 1 && !cl_start_buildings->integer)
		gd.instant_build = 1;
	/* Set up default buy/sell factors for this base. */
	base->sellfactor = 5;
	base->buyfactor = 1;
	/* the first base never has invalid fields */
	if (base->idx > 0) {
		int *mapPtr = &base->map[rand() % BASE_SIZE][rand() % BASE_SIZE];
		/* set this field to invalid if there is no building yet */
		if (*mapPtr == BASE_FREESLOT)
			*mapPtr = BASE_INVALID_SPACE;
	}

	/* a new base is not discovered (yet) */
	base->isDiscovered = qfalse;

	/* intialise hit points */
	base->batteryDamage = MAX_BATTERY_DAMAGE;
	base->baseDamage = MAX_BASE_DAMAGE;
	BDEF_InitialiseBaseSlots(base);
}

/**
 * @brief Returns the building in the global building-types list that has the unique name buildingID.
 *
 * @param[in] buildingName The unique id of the building (building_t->id).
 *
 * @return building_t If a building was found it is returned, if no id was give the current building is returned, otherwise->NULL.
 */
building_t *B_GetBuildingType (const char *buildingName)
{
	int i = 0;

	assert(buildingName);
	for (i = 0; i < gd.numBuildingTypes; i++)
		if (!Q_strcasecmp(gd.buildingTypes[i].id, buildingName))
			return &gd.buildingTypes[i];

	Com_Printf("Building %s not found\n", buildingName);
	return NULL;
}

/**
 * @brief Checks whether you have enough credits to build this building
 * @param costs buildcosts of the building
 * @return qboolean true - enough credits
 * @return qboolean false - not enough credits
 *
 * @sa B_ConstructBuilding
 * @sa B_NewBuilding
 * Checks whether the given costs are bigger than the current available credits
 */
static inline qboolean B_CheckCredits (int costs)
{
	if (costs > ccs.credits)
		return qfalse;
	return qtrue;
}

/**
 * @brief Builds new building.
 * @sa B_MarkBuildingDestroy
 * @sa B_CheckCredits
 * @sa CL_UpdateCredits
 * @return qboolean
 * @sa B_NewBuilding
 * Checks whether the player has enough credits to construct the current selected
 * building before starting construction.
 */
static qboolean B_ConstructBuilding (base_t* base)
{
	building_t *building_to_build = NULL;

	/* maybe someone call this command before the buildings are parsed?? */
	if (!base || !base->buildingCurrent)
		return qfalse;

	/* enough credits to build this? */
	if (!B_CheckCredits(base->buildingCurrent->fixCosts)) {
		Com_DPrintf(DEBUG_CLIENT, "B_ConstructBuilding: Not enough credits to build: '%s'\n", base->buildingCurrent->id);
		B_ResetBuildingCurrent(base);
		return qfalse;
	}

	Com_DPrintf(DEBUG_CLIENT, "Construction of %s is starting\n", base->buildingCurrent->id);

	/* second building part */
	if (base->buildingToBuild >= 0) {
		building_to_build = &gd.buildingTypes[base->buildingToBuild];
		building_to_build->buildingStatus = B_STATUS_UNDER_CONSTRUCTION;
		base->buildingToBuild = -1;
	}
	if (!gd.instant_build) {
		base->buildingCurrent->buildingStatus = B_STATUS_UNDER_CONSTRUCTION;
		base->buildingCurrent->timeStart = ccs.date.day;
	} else {
		B_UpdateBaseBuildingStatus(base->buildingCurrent, base, B_STATUS_WORKING);
	}

	CL_UpdateCredits(ccs.credits - base->buildingCurrent->fixCosts);
	B_BaseInit_f();
	return qtrue;
}

/**
 * @brief Build new building.
 * @sa B_MarkBuildingDestroy
 * @sa B_ConstructBuilding
 */
static void B_NewBuilding (base_t* base)
{
	/* maybe someone call this command before the buildings are parsed?? */
	if (!base || !base->buildingCurrent)
		return;

	if (base->buildingCurrent->buildingStatus < B_STATUS_UNDER_CONSTRUCTION)
		/* credits are updated in the construct function */
		if (B_ConstructBuilding(base)) {
			B_BuildingStatus(base, base->buildingCurrent);
			Com_DPrintf(DEBUG_CLIENT, "B_NewBuilding: buildingCurrent->buildingStatus = %i\n", base->buildingCurrent->buildingStatus);
		}
}

/**
 * @brief Set the currently selected building.
 *
 * @param[in] row Set building (baseCurrent->buildingCurrent) to row
 * @param[in] col Set building (baseCurrent->buildingCurrent) to col
 */
void B_SetBuildingByClick (int row, int col)
{
	building_t *building = NULL;
	building_t *secondBuildingPart = NULL;

#ifdef DEBUG
	if (!baseCurrent)
		Sys_Error("no current base\n");
	if (!baseCurrent->buildingCurrent)
		Sys_Error("no current building\n");
#endif
	if (!B_CheckCredits(baseCurrent->buildingCurrent->fixCosts)) {
		MN_Popup(_("Notice"), _("Not enough credits to build this\n"));
		return;
	}

	/*@todo: this is bad style (baseCurrent->buildingCurrent shouldn't link to gd.buildingTypes at all ... it's just not logical) */
	/* if the building is in gd.buildingTypes[] */
	if (baseCurrent->buildingCurrent->base_idx < 0) {
		building = &gd.buildings[baseCurrent->idx][gd.numBuildings[baseCurrent->idx]];

		/* copy building from type-list to base-buildings-list */
		*building = gd.buildingTypes[baseCurrent->buildingCurrent->type_idx];

		/* self-link to building-list in base */
		building->idx = gd.numBuildings[baseCurrent->idx];
		gd.numBuildings[baseCurrent->idx]++;

		/* Link to the base. */
		building->base_idx = baseCurrent->idx;
		baseCurrent->buildingCurrent = building;
	}

	if (0 <= row && row < BASE_SIZE && 0 <= col && col < BASE_SIZE) {
		switch (baseCurrent->map[row][col]) {
		case BASE_FREESLOT:
			if (baseCurrent->buildingCurrent->needs)
				secondBuildingPart = B_GetBuildingType(baseCurrent->buildingCurrent->needs);
			if (secondBuildingPart) {
				if (col + 1 == BASE_SIZE) {
					if (baseCurrent->map[row][col-1] != BASE_FREESLOT) {
						Com_DPrintf(DEBUG_CLIENT, "Can't place this building here - the second part overlapped with another building or invalid field\n");
						return;
					}
					col--;
				} else if (baseCurrent->map[row][col+1] != BASE_FREESLOT) {
					if (baseCurrent->map[row][col-1] != BASE_FREESLOT || !col) {
						Com_DPrintf(DEBUG_CLIENT, "Can't place this building here - the second part overlapped with another building or invalid field\n");
						return;
					}
					col--;
				}

				baseCurrent->map[row][col + 1] = baseCurrent->buildingCurrent->idx;
				baseCurrent->buildingToBuild = secondBuildingPart->idx;
				/* where is this building located in our base? */
				secondBuildingPart->pos[1] = col + 1;
				secondBuildingPart->pos[0] = row;
			} else {
				baseCurrent->buildingToBuild = -1;
			}
			/* credits are updated here, too */
			B_NewBuilding(baseCurrent);

			baseCurrent->map[row][col] = baseCurrent->buildingCurrent->idx;

			/* where is this building located in our base? */
			baseCurrent->buildingCurrent->pos[0] = row;
			baseCurrent->buildingCurrent->pos[1] = col;

			B_ResetBuildingCurrent(baseCurrent);
			B_BuildingInit();	/* update the building-list */
			break;
		case BASE_INVALID_SPACE:
			Com_DPrintf(DEBUG_CLIENT, "This base field is marked as invalid - you can't bulid here\n");
			break;
		default:
			Com_DPrintf(DEBUG_CLIENT, "There is already a building\n");
			Com_DPrintf(DEBUG_CLIENT, "Building: %i at (row:%i, col:%i)\n", baseCurrent->map[row][col], row, col);
		}
	} else
		Com_DPrintf(DEBUG_CLIENT, "Invalid coordinates\n");
}

/**
 * @brief Places the current building in the base (x/y give via console).
 */
static void B_SetBuilding_f (void)
{
	int row, col;

	if (Cmd_Argc() < 3) {
		Com_Printf("Usage: %s <x> <y>\n", Cmd_Argv(0));
		return;
	}

	/* maybe someone call this command before the buildings are parsed?? */
	if (!baseCurrent || !baseCurrent->buildingCurrent)
		return;

	row = atoi(Cmd_Argv(1));
	col = atoi(Cmd_Argv(2));

	/* emulate the mouseclick with the given coordinates */
	B_SetBuildingByClick(row, col);
}

/**
 * @brief Build building from the list of those available.
 */
static void B_NewBuildingFromList_f (void)
{
	/* maybe someone call this command before the buildings are parsed?? */
	if (!baseCurrent || !baseCurrent->buildingCurrent)
		return;

	if (baseCurrent->buildingCurrent->buildingStatus < B_STATUS_UNDER_CONSTRUCTION)
		B_NewBuilding(baseCurrent);
}

/**
 * @brief Draws a building.
 */
static void B_DrawBuilding (base_t* base, building_t* building)
{
	/* maybe someone call this command before the buildings are parsed?? */
	if (!base || !building)
		return;

	*buildingText = '\0';

	B_BuildingStatus(base, building);

	Com_sprintf(buildingText, sizeof(buildingText), "%s\n", _(building->name));

	if (building->buildingStatus < B_STATUS_UNDER_CONSTRUCTION && building->fixCosts)
		Com_sprintf(buildingText, sizeof(buildingText), _("Costs:\t%i c\n"), building->fixCosts);

	if (building->buildingStatus == B_STATUS_UNDER_CONSTRUCTION)
		Q_strcat(buildingText, va(ngettext("%i Day to build\n", "%i Days to build\n", building->buildTime), building->buildTime), sizeof(buildingText));

	if (building->varCosts)
		Q_strcat(buildingText, va(_("Running Costs:\t%i c\n"), building->varCosts), sizeof(buildingText));

	if (building->name)
		Cvar_Set("mn_building_name", _(building->name));

	if (building->image)
		Cvar_Set("mn_building_image", building->image);
	else
		Cvar_Set("mn_building_image", "base/empty");

	/* link into menu text array */
	menuText[TEXT_BUILDING_INFO] = buildingText;
}

/**
 * @brief Handles the list of constructable buildings.
 *
 * @param[in] building Add this building to the construction list
 * Called everytime a building was constructed and thus maybe other buildings get available.
 * menuText[TEXT_BUILDINGS] is a pointer to baseCurrent->allBuildingsList which will be displayed in the build-screen.
 * This way every base can hold its own building list.
 * The content is updated everytime B_BuildingInit is called (i.e everytime the buildings-list is dispplayed/updated)
 */
static void B_BuildingAddToList (building_t * building)
{
	assert(baseCurrent);
	assert(building->name);

	Q_strcat(baseCurrent->allBuildingsList, va("%s\n", _(building->name)), MAX_LIST_CHAR);
	BuildingConstructionList[numBuildingConstructionList] = building->type_idx;
	numBuildingConstructionList++;
}

/**
 * @brief Counts the number of buildings of a particular type in a base.
 *
 * @param[in] base_idx Which base
 * @param[in] type_idx Which buildingtype
 * @sa B_GetNumberOfBuildingsInBaseByType
 * @return The number of buildings.
 * @return -1 on error (e.g. base index out of range)
 * @todo type index sanity check?
 */
int B_GetNumberOfBuildingsInBaseByTypeIDX (int base_idx, int type_idx)
{
	int i;
	int NumberOfBuildings = 0;

	if (base_idx < 0 || base_idx >= gd.numBases) {
		Com_Printf("B_GetNumberOfBuildingsInBaseByTypeIDX: Bad base-index given: %i (numbases %i)\n", base_idx, gd.numBases);
		return -1;
	}

	for (i = 0; i < gd.numBuildings[base_idx]; i++) {
		if (gd.buildings[base_idx][i].type_idx == type_idx
		 && gd.buildings[base_idx][i].buildingStatus != B_STATUS_NOT_SET)
			NumberOfBuildings++;
	}
	Com_DPrintf(DEBUG_CLIENT, "B_GetNumOfBuildType: base: '%s' - num_b: %i - type_idx: %s\n", gd.bases[base_idx].name, NumberOfBuildings, gd.buildingTypes[type_idx].id);
	return NumberOfBuildings;
}

/**
 * @brief Counts the number of buildings of a particular type in a base.
 *
 * @param[in] base_idx Which base
 * @param[in] type Building type value
 * @sa B_GetNumberOfBuildingsInBaseByTypeIDX
 * @return The number of buildings.
 * @return -1 on error (e.g. base index out of range)
 */
int B_GetNumberOfBuildingsInBaseByType (int base_idx, buildingType_t type)
{
	int i;
	int NumberOfBuildings = 0;

	if (base_idx < 0 || base_idx >= gd.numBases) {
		Com_Printf("B_GetNumberOfBuildingsInBaseByType: Bad base-index given: %i (numbases: %i)\n", base_idx, gd.numBases);
		return -1;
	}

	for (i = 0; i < gd.numBuildings[base_idx]; i++) {
		if (gd.buildings[base_idx][i].buildingType == type
		 && gd.buildings[base_idx][i].buildingStatus != B_STATUS_NOT_SET)
			NumberOfBuildings++;
	}
	return NumberOfBuildings;
}

/**
 * @brief Get the maximum status of a building.
 *
 * This function is mostly used to check if the construction of a building with a given type is finished.
 * e.g.: "if (B_GetMaximumBuildingStatus(base_idx, B_LAB) >= B_STATUS_CONSTRUCTION_FINISHED) { ... }"
 *
 * @param[in] base_idx Which base
 * @param[in] buildingType Which buildingtype
 * @return The max./highest building status found.
 */
static buildingStatus_t B_GetMaximumBuildingStatus (int base_idx, buildingType_t buildingType)
{
	int i;
	buildingStatus_t status = B_STATUS_NOT_SET;

	if (base_idx < 0 || base_idx >= gd.numBases) {
		Com_Printf("B_GetMaximumBuildingStatus: Bad base-index given: %i (numbases %i)\n", base_idx, gd.numBases);
		return -1;
	}

	for (i = 0; i < gd.numBuildings[base_idx]; i++) {
		if (gd.buildings[base_idx][i].buildingType == buildingType)
			if (gd.buildings[base_idx][i].buildingStatus > status)
				status = gd.buildings[base_idx][i].buildingStatus;
	}
	return status;
}

/**
 * @brief Update the building-list.
 */
static void B_BuildingInit (void)
{
	int i;
	int numSameBuildings;
	building_t *buildingType = NULL;

	/* maybe someone call this command before the bases are parsed?? */
	if (!baseCurrent)
		return;

	Com_DPrintf(DEBUG_CLIENT, "B_BuildingInit: Updating b-list for '%s' (%i)\n", baseCurrent->name, baseCurrent->idx);
	Com_DPrintf(DEBUG_CLIENT, "B_BuildingInit: Buildings in base: %i\n", gd.numBuildings[baseCurrent->idx]);

	/* initialising the vars used in B_BuildingAddToList */
	memset(baseCurrent->allBuildingsList, 0, sizeof(baseCurrent->allBuildingsList));
	menuText[TEXT_BUILDINGS] = baseCurrent->allBuildingsList;
	numBuildingConstructionList = 0;

	for (i = 0; i < gd.numBuildingTypes; i++) {
		buildingType = &gd.buildingTypes[i];
		/*make an entry in list for this building */

		if (buildingType->visible) {
			numSameBuildings = B_GetNumberOfBuildingsInBaseByTypeIDX(baseCurrent->idx, buildingType->type_idx);

			if (buildingType->moreThanOne) {
				/* skip if limit of BASE_SIZE*BASE_SIZE exceeded */
				if (numSameBuildings >= BASE_SIZE * BASE_SIZE)
					continue;
			} else if (numSameBuildings > 0) {
				continue;
			}

			/* if the building is researched add it to the list */
			if (RS_IsResearched_idx(buildingType->tech)) {
				if (buildingType->dependsBuilding < 0
					|| B_GetMaximumBuildingStatus(baseCurrent->idx, buildingType->buildingType) >= B_STATUS_CONSTRUCTION_FINISHED) {
					B_BuildingAddToList(buildingType);
				}
			} else {
				Com_DPrintf(DEBUG_CLIENT, "Building not researched yet %s (tech idx: %i)\n", buildingType->id, buildingType->tech);
			}
		}
	}
	if (baseCurrent->buildingCurrent)
		B_DrawBuilding(baseCurrent, baseCurrent->buildingCurrent);
}

/**
 * @brief Gets the type of building by its index.
 *
 * @param[in] base Pointer to base_t (base has to be founded already)
 * @param[in] idx The index of the building in gd.buildings[]
 * @return buildings_t pointer to gd.buildings[idx]
 */
building_t *B_GetBuildingByIdx (base_t* base, int idx)
{
	assert(base);
	assert(idx > BASE_FREESLOT);
	return &gd.buildings[base->idx][idx];
}

/**
 * @brief Opens up the 'pedia if you right click on a building in the list.
 *
 * @todo Really only do this on rightclick.
 * @todo Left click should show building-status.
 */
static void B_BuildingInfoClick_f (void)
{
	if (baseCurrent && baseCurrent->buildingCurrent) {
		Com_DPrintf(DEBUG_CLIENT, "B_BuildingInfoClick_f: %s - %i\n", baseCurrent->buildingCurrent->id, baseCurrent->buildingCurrent->buildingStatus);
		UP_OpenWith(baseCurrent->buildingCurrent->pedia);
	}
}

/**
 * @brief Script function for clicking the building list text field.
 */
static void B_BuildingClick_f (void)
{
	int num;
	building_t *building = NULL;

	if (Cmd_Argc() < 2 || !baseCurrent) {
		Com_Printf("Usage: %s <arg>\n", Cmd_Argv(0));
		return;
	}

	/* which building? */
	num = atoi(Cmd_Argv(1));

	Com_DPrintf(DEBUG_CLIENT, "B_BuildingClick_f: listnumber %i base %i\n", num, baseCurrent->idx);

	if (num > numBuildingConstructionList || num < 0) {
		Com_DPrintf(DEBUG_CLIENT, "B_BuildingClick_f: max exceeded %i/%i\n", num, numBuildingConstructionList);
		return;
	}

	building = &gd.buildingTypes[BuildingConstructionList[num]];

	baseCurrent->buildingCurrent = building;
	B_DrawBuilding(baseCurrent, building);

	gd.baseAction = BA_NEWBUILDING;
}

/**
 * @brief Copies an entry from the building description file into the list of building types.
 *
 * Parses one "building" entry in the basemanagement.ufo file and writes it into the next free entry in bmBuildings[0], which is the list of buildings in the first base (building_t).
 *
 * @param[in] id Unique test-id of a building_t. This is parsed from "building xxx" -> id=xxx.
 * @param[in] text @todo: document this ... It appears to be the whole following text that is part of the "building" item definition in .ufo.
 * @param[in] link Bool value that decides whether to link the tech pointer in or not
 * @sa CL_ParseScriptFirst (link is false here)
 * @sa CL_ParseScriptSecond (link it true here)
 */
void B_ParseBuildings (const char *name, const char **text, qboolean link)
{
	building_t *building = NULL;
	building_t *dependsBuilding = NULL;
	technology_t *tech_link = NULL;
	const value_t *vp = NULL;
	const char *errhead = "B_ParseBuildings: unexpected end of file (names ";
	const char *token = NULL;
	int i;

	/* get id list body */
	token = COM_Parse(text);
	if (!*text || *token != '{') {
		Com_Printf("B_ParseBuildings: building \"%s\" without body ignored\n", name);
		return;
	}
	if (gd.numBuildingTypes >= MAX_BUILDINGS) {
		Com_Printf("B_ParseBuildings: too many buildings\n");
		gd.numBuildingTypes = MAX_BUILDINGS;	/* just in case it's bigger. */
		return;
	}

	if (!link) {
		for (i = 0; i < gd.numBuildingTypes; i++) {
			if (!Q_strcmp(gd.buildingTypes[i].id, name)) {
				Com_Printf("B_ParseBuildings: Second buliding with same name found (%s) - second ignored\n", name);
				return;
			}
		}

		/* new entry */
		building = &gd.buildingTypes[gd.numBuildingTypes];
		memset(building, 0, sizeof(building_t));
		building->id = Mem_PoolStrDup(name, cl_localPool, CL_TAG_REPARSE_ON_NEW_GAME);

		Com_DPrintf(DEBUG_CLIENT, "...found building %s\n", building->id);

		/*set standard values */
		building->type_idx = gd.numBuildingTypes;
		building->idx = -1;
		building->base_idx = -1;
		building->tech = -1;
		building->dependsBuilding = -1;
		building->visible = qtrue;

		gd.numBuildingTypes++;
		do {
			/* get the name type */
			token = COM_EParse(text, errhead, name);
			if (!*text)
				break;
			if (*token == '}')
				break;

			/* get values */
			if (!Q_strncmp(token, "type", MAX_VAR)) {
				token = COM_EParse(text, errhead, name);
				if (!*text)
					return;

				if (!Q_strncmp(token, "lab", MAX_VAR)) {
					building->buildingType = B_LAB;
				} else if (!Q_strncmp(token, "hospital", MAX_VAR)) {
					building->buildingType = B_HOSPITAL;
				} else if (!Q_strncmp(token, "aliencont", MAX_VAR)) {
					building->buildingType = B_ALIEN_CONTAINMENT;
				} else if (!Q_strncmp(token, "workshop", MAX_VAR)) {
					building->buildingType = B_WORKSHOP;
				} else if (!Q_strncmp(token, "storage", MAX_VAR)) {
					building->buildingType = B_STORAGE;
				} else if (!Q_strncmp(token, "hangar", MAX_VAR)) {
					building->buildingType = B_HANGAR;
				} else if (!Q_strncmp(token, "smallhangar", MAX_VAR)) {
					building->buildingType = B_SMALL_HANGAR;
				} else if (!Q_strncmp(token, "ufohangar", MAX_VAR)) {
					building->buildingType = B_UFO_HANGAR;
				} else if (!Q_strncmp(token, "smallufohangar", MAX_VAR)) {
					building->buildingType = B_UFO_SMALL_HANGAR;
				} else if (!Q_strncmp(token, "quarters", MAX_VAR)) {
					building->buildingType = B_QUARTERS;
				} else if (!Q_strncmp(token, "workshop", MAX_VAR)) {
					building->buildingType = B_WORKSHOP;
				} else if (!Q_strncmp(token, "power", MAX_VAR)) {
					building->buildingType = B_POWER;
				} else if (!Q_strncmp(token, "command", MAX_VAR)) {
					building->buildingType = B_COMMAND;
				} else if (!Q_strncmp(token, "amstorage", MAX_VAR)) {
					building->buildingType = B_ANTIMATTER;
				} else if (!Q_strncmp(token, "entrance", MAX_VAR)) {
					building->buildingType = B_ENTRANCE;
				} else if (!Q_strncmp(token, "missile", MAX_VAR)) {
					building->buildingType = B_DEFENSE_MISSILE;
				}
			} else {
				/* no linking yet */
				if (!Q_strncmp(token, "depends", MAX_VAR)) {
					token = COM_EParse(text, errhead, name);
					if (!*text)
						return;
				} else {
					for (vp = valid_building_vars; vp->string; vp++)
						if (!Q_strncmp(token, vp->string, sizeof(vp->string))) {
							/* found a definition */
							token = COM_EParse(text, errhead, name);
							if (!*text)
								return;

							switch (vp->type) {
							case V_NULL:
								break;
							case V_TRANSLATION_MANUAL_STRING:
								token++;
							case V_CLIENT_HUNK_STRING:
								Mem_PoolStrDupTo(token, (char**) ((char*)building + (int)vp->ofs), cl_localPool, CL_TAG_REPARSE_ON_NEW_GAME);
								break;
							default:
								Com_ParseValue(building, token, vp->type, vp->ofs, vp->size);
								break;
							}
							break;
						}
				}
			}

			if (!vp->string)
				Com_Printf("B_ParseBuildings: unknown token \"%s\" ignored (building %s)\n", token, name);

		} while (*text);
	} else {
		building = B_GetBuildingType(name);
		if (!building)			/* i'm paranoid */
			Sys_Error("B_ParseBuildings: Could not find building with id %s\n", name);

		tech_link = RS_GetTechByProvided(name);
		if (tech_link) {
			building->tech = tech_link->idx;
		} else {
			if (building->visible)
				/* @todo: are the techs already parsed? */
				Com_DPrintf(DEBUG_CLIENT, "B_ParseBuildings: Could not find tech that provides %s\n", name);
		}

		do {
			/* get the name type */
			token = COM_EParse(text, errhead, name);
			if (!*text)
				break;
			if (*token == '}')
				break;
			/* get values */
			if (!Q_strncmp(token, "depends", MAX_VAR)) {
				dependsBuilding = B_GetBuildingType(COM_EParse(text, errhead, name));
				if (!dependsBuilding)
					Sys_Error("Could not find building depend of %s\n", building->id);
				building->dependsBuilding = dependsBuilding->idx;
				if (!*text)
					return;
			}
		} while (*text);
	}
}

/**
 * @brief Gets a lab in the given base
 * @note You can run more than one research in a lab
 * @param[in] base_id The number/id of the base to search in.
 * @return The lab or NULL if base has no lab
 */
building_t *B_GetLabBuildingFromBase (int base_idx)
{
	int i;
	building_t *building = NULL;

	for (i = 0; i < gd.numBuildings[base_idx]; i++) {
		building = &gd.buildings[base_idx][i];
		if (building->buildingType == B_LAB)
			return building;
	}
	return NULL;
}

/**
 * @brief Clears a base with all its characters
 * @sa CL_ResetCharacters
 * @sa CL_GenerateCharacter
 */
void B_ClearBase (base_t *const base)
{
	int i;

	CL_ResetCharacters(base);

	memset(base, 0, sizeof(base_t));

	/* only go further if we have a active campaign */
	if (!curCampaign)
		return;

	/* setup team */
	if (!E_CountUnhired(EMPL_SOLDIER)) {
		/* should be multiplayer (campaignmode @todo) or singleplayer */
		Com_DPrintf(DEBUG_CLIENT, "B_ClearBase: create %i soldiers\n", curCampaign->soldiers);
		for (i = 0; i < curCampaign->soldiers; i++)
			E_CreateEmployee(EMPL_SOLDIER);
		Com_DPrintf(DEBUG_CLIENT, "B_ClearBase: create %i scientists\n", curCampaign->scientists);
		for (i = 0; i < curCampaign->scientists; i++)
			E_CreateEmployee(EMPL_SCIENTIST);
		Com_DPrintf(DEBUG_CLIENT, "B_ClearBase: create %i robots\n", curCampaign->ugvs);
		for (i = 0; i < curCampaign->ugvs; i++)
			E_CreateEmployee(EMPL_ROBOT);
		Com_DPrintf(DEBUG_CLIENT, "B_ClearBase: create %i workers\n", curCampaign->workers);
		for (i = 0; i < curCampaign->workers; i++)
			E_CreateEmployee(EMPL_WORKER);
		Com_DPrintf(DEBUG_CLIENT, "B_ClearBase: create %i medics\n", curCampaign->medics);
		for (i = 0; i < curCampaign->medics; i++)
			E_CreateEmployee(EMPL_MEDIC);
	}

	memset(base->map, BASE_FREESLOT, sizeof(int) * BASE_SIZE * BASE_SIZE);
}

/**
 * @brief Reads information about bases.
 * @sa CL_ParseScriptFirst
 * @note write into cl_localPool - free on every game restart and reparse
 */
void B_ParseBases (const char *name, const char **text)
{
	const char *errhead = "B_ParseBases: unexpected end of file (names ";
	const char *token;
	base_t *base;

	gd.numBaseNames = 0;

	/* get token */
	token = COM_Parse(text);

	if (!*text || *token != '{') {
		Com_Printf("B_ParseBases: base \"%s\" without body ignored\n", name);
		return;
	}
	do {
		/* add base */
		if (gd.numBaseNames > MAX_BASES) {
			Com_Printf("B_ParseBases: too many bases\n");
			return;
		}

		/* get the name */
		token = COM_EParse(text, errhead, name);
		if (!*text)
			break;
		if (*token == '}')
			break;

		base = &gd.bases[gd.numBaseNames];
		memset(base, 0, sizeof(base_t));
		base->idx = gd.numBaseNames;
		base->buildingToBuild = -1;
		memset(base->map, BASE_FREESLOT, sizeof(int) * BASE_SIZE * BASE_SIZE);

		/* get the title */
		token = COM_EParse(text, errhead, name);
		if (!*text)
			break;
		if (*token == '}')
			break;
		if (*token == '_')
			token++;
		Q_strncpyz(base->name, _(token), sizeof(base->name));
		Com_DPrintf(DEBUG_CLIENT, "Found base %s\n", base->name);
		B_ResetBuildingCurrent(base);
		gd.numBaseNames++; /* FIXME: Use this value instead of MAX_BASES in the for loops */
	} while (*text);

	mn_base_title = Cvar_Get("mn_base_title", "", 0, NULL);
}

/**
 * @brief Draws a base.
 * @sa MN_DrawMenus
 */
void B_DrawBase (menuNode_t * node)
{
	int x, y, xHover = -1, yHover = -1, widthHover = 1;
	int width, height, row, col, time, colSecond;
	static vec4_t color = { 0.5f, 1.0f, 0.5f, 1.0 };
	char image[MAX_QPATH];
	building_t *building = NULL, *secondBuilding = NULL, *hoverBuilding = NULL;

	if (!baseCurrent)
		MN_PopMenu(qfalse);

	width = node->size[0] / BASE_SIZE;
	height = (node->size[1] + BASE_SIZE * 20) / BASE_SIZE;

	for (row = 0; row < BASE_SIZE; row++) {
		for (col = 0; col < BASE_SIZE; col++) {
			/* 20 is the height of the part where the images overlap */
			x = node->pos[0] + col * width;
			y = node->pos[1] + row * height - row * 20;

			baseCurrent->posX[row][col] = x;
			baseCurrent->posY[row][col] = y;

			switch (baseCurrent->map[row][col]) {
			case BASE_INVALID_SPACE:
				building = NULL;
				Q_strncpyz(image, "base/invalid", sizeof(image));
				break;
			case BASE_FREESLOT:
				building = NULL;
				Q_strncpyz(image, "base/grid", sizeof(image));
				break;
			default:
				building = B_GetBuildingByIdx(baseCurrent, baseCurrent->map[row][col]);
				secondBuilding = NULL;

				if (!building)
					Sys_Error("Error in DrawBase - no building with id %i\n", baseCurrent->map[row][col]);

				if (!building->used) {
					if (building->needs)
						building->used = 1;
					if (building->image) {	/* @todo:DEBUG */
						Q_strncpyz(image, building->image, sizeof(image));
					} else {
						/*Com_DPrintf(DEBUG_CLIENT, "B_DrawBase: no image found for building %s / %i\n",building->id ,building->idx); */
					}
				} else if (building->needs) {
					secondBuilding = B_GetBuildingType(building->needs);
					if (!secondBuilding)
						Sys_Error("Error in ufo-scriptfile - could not find the needed building\n");
					Q_strncpyz(image, secondBuilding->image, sizeof(image));
					building->used = 0;
				}
				break;
			}

			if (*image)
				R_DrawNormPic(x, y, width, height, 0, 0, 0, 0, 0, qfalse, image);

			/* check for hovering building name or outline border */
			if (mousePosX > x && mousePosX < x + width && mousePosY > y && mousePosY < y + height - 20) {
				switch (baseCurrent->map[row][col]) {
				case BASE_FREESLOT:
					if (gd.baseAction == BA_NEWBUILDING && xHover == -1) {
						assert(baseCurrent->buildingCurrent);
						colSecond = col;
						if (baseCurrent->buildingCurrent->needs) {
							if (colSecond + 1 == BASE_SIZE) {
								if (baseCurrent->map[row][colSecond - 1] == BASE_FREESLOT)
									colSecond--;
							} else if (baseCurrent->map[row][colSecond + 1] != BASE_FREESLOT) {
								if (baseCurrent->map[row][colSecond - 1] == BASE_FREESLOT)
									colSecond--;
							} else {
								colSecond++;
							}
							if (colSecond != col) {
								if (colSecond < col)
									xHover = node->pos[0] + colSecond * width;
								else
									xHover = x;
								widthHover = 2;
							}
						} else
							xHover = x;
						yHover = y;
					}
					break;
				default:
					hoverBuilding = building;
					break;
				}
			}

			/* only draw for first part of building */
			if (building && !secondBuilding) {
				switch (building->buildingStatus) {
				case B_STATUS_DOWN:
				case B_STATUS_CONSTRUCTION_FINISHED:
					break;
				case B_STATUS_UNDER_CONSTRUCTION:
					time = building->buildTime - (ccs.date.day - building->timeStart);
					R_FontDrawString("f_small", 0, x + 10, y + 10, x + 10, y + 10, node->size[0], 0, node->texh[0], va(ngettext("%i day left", "%i days left", time), time), 0, 0, NULL, qfalse);
					break;
				default:
					break;
				}
			}
		}
	}
	if (hoverBuilding) {
		R_Color(color);
		R_FontDrawString("f_small", 0, mousePosX + 3, mousePosY, mousePosX + 3, mousePosY, node->size[0], 0, node->texh[0], _(hoverBuilding->name), 0, 0, NULL, qfalse);
		R_Color(NULL);
	}
	if (xHover != -1) {
		if (widthHover == 1) {
			Q_strncpyz(image, "base/hover", sizeof(image));
			R_DrawNormPic(xHover, yHover, width, height, 0, 0, 0, 0, 0, qfalse, image);
		} else {
			Com_sprintf(image, sizeof(image), "base/hover%i", widthHover);
			R_DrawNormPic(xHover, yHover, width * widthHover, height, 0, 0, 0, 0, 0, qfalse, image);
		}
	}
}

/**
 * @brief Renames a base.
 */
static void B_RenameBase_f (void)
{
	if (Cmd_Argc() < 2) {
		Com_Printf("Usage: %s <name>\n", Cmd_Argv(0));
		return;
	}

	if (baseCurrent)
		Q_strncpyz(baseCurrent->name, Cmd_Argv(1), sizeof(baseCurrent->name));
}

/**
 * @brief Cycles to the next base.
 * @sa B_PrevBase
 * @sa B_SelectBase_f
 */
static void B_NextBase_f (void)
{
	int baseID;

	if (!mn_base_id)
		return;

	baseID = mn_base_id->integer;

	Com_DPrintf(DEBUG_CLIENT, "cur-base=%i num-base=%i\n", baseID, gd.numBases);
	if (baseID < gd.numBases - 1)
		baseID++;
	else
		baseID = 0;
	Com_DPrintf(DEBUG_CLIENT, "new-base=%i\n", baseID);
	if (!gd.bases[baseID].founded)
		return;
	else
		Cmd_ExecuteString(va("mn_select_base %i", baseID));
}

/**
 * @brief Cycles to the previous base.
 * @sa B_NextBase
 * @sa B_SelectBase_f
 */
static void B_PrevBase_f (void)
{
	int baseID;

	if (!mn_base_id)
		return;

	baseID = mn_base_id->integer;

	Com_DPrintf(DEBUG_CLIENT, "cur-base=%i num-base=%i\n", baseID, gd.numBases);
	if (baseID > 0)
		baseID--;
	else
		baseID = gd.numBases - 1;
	Com_DPrintf(DEBUG_CLIENT, "new-base=%i\n", baseID);

	/* this must be false - but i'm paranoid' */
	if (!gd.bases[baseID].founded)
		return;
	else
		Cmd_ExecuteString(va("mn_select_base %i", baseID));
}

/**
 * @brief Called when a base is opened or a new base is created on geoscape.
 *
 * For a new base the baseID is -1.
 */
static void B_SelectBase_f (void)
{
	int baseID;

	if (Cmd_Argc() < 2) {
		Com_Printf("Usage: %s <baseID>\n", Cmd_Argv(0));
		return;
	}
	baseID = atoi(Cmd_Argv(1));

	/* set up a new base */
	/* called from *.ufo with -1 */
	if (baseID < 0) {
		gd.mapAction = MA_NEWBASE;
		baseID = gd.numBases;
		Com_DPrintf(DEBUG_CLIENT, "B_SelectBase_f: new baseID is %i\n", baseID);
		if (baseID < MAX_BASES) {
			baseCurrent = &gd.bases[baseID];
			baseCurrent->idx = baseID;
			Com_DPrintf(DEBUG_CLIENT, "B_SelectBase_f: baseID is valid for base: %s\n", baseCurrent->name);
			Cmd_ExecuteString("set_base_to_normal");
		} else {
			Com_Printf("MaxBases reached\n");
			/* select the first base in list */
			baseCurrent = &gd.bases[0];
			gd.mapAction = MA_NONE;
		}
	} else if (baseID < MAX_BASES) {
		Com_DPrintf(DEBUG_CLIENT, "B_SelectBase_f: select base with id %i\n", baseID);
		baseCurrent = &gd.bases[baseID];
		if (baseCurrent->founded) {
			gd.mapAction = MA_NONE;
			MN_PushMenu("bases");
			AIR_AircraftSelect(NULL);
			switch (baseCurrent->baseStatus) {
			case BASE_UNDER_ATTACK:
				Cvar_Set("mn_base_status_name", _("Base is under attack"));
				Cmd_ExecuteString("set_base_under_attack");
				break;
			default:
				Cmd_ExecuteString("set_base_to_normal");
				break;
			}
		} else {
			gd.mapAction = MA_NEWBASE;
		}
	} else
		return;

	/**
	 * this is only needed when we are going to be show up the base
	 * in our base view port
	 */
	if (gd.mapAction != MA_NEWBASE) {
		assert(baseCurrent);
		Cvar_SetValue("mn_base_id", baseCurrent->idx);
		Cvar_Set("mn_base_title", baseCurrent->name);
		Cvar_SetValue("mn_numbases", gd.numBases);
		Cvar_SetValue("mn_base_status_id", baseCurrent->baseStatus);
	}
}

#undef RIGHT
#undef HOLSTER
#define RIGHT(e) ((e)->inv->c[csi.idRight])
#define HOLSTER(e) ((e)->inv->c[csi.idHolster])

/**
 * @brief Swaps skills of the initial team of soldiers so that they match inventories
 * @todo This currently always uses exactly the first two firemodes (see fmode1+fmode2) for calculation. This needs to be adapted to support less (1) or more 3+ firemodes. I think the function will even  break on only one firemode .. never tested it.
 * @todo i think currently also the different ammo/firedef types for each weapon (different weaponr_fd_idx and weaponr_fd_idx values) are ignored.
 */
static void CL_SwapSkills (chrList_t *team)
{
	int j, i1, i2, skill, no1, no2, tmp1, tmp2;
	character_t *cp1, *cp2;
	int weaponr_fd_idx, weaponh_fd_idx;
	const byte fmode1 = 0;
	const byte fmode2 = 1;

	j = team->num;
	while (j--) {
		/* running the loops below is not enough, we need transitive closure */
		/* I guess num times is enough --- could anybody prove this? */
		/* or perhaps 2 times is enough as long as weapons have 1 skill? */
		for (skill = ABILITY_NUM_TYPES; skill < SKILL_NUM_TYPES; skill++) {
			for (i1 = 0; i1 < team->num - 1; i1++) {
				cp1 = team->chr[i1];
				weaponr_fd_idx = -1;
				weaponh_fd_idx = -1;
				if (RIGHT(cp1) && RIGHT(cp1)->item.m != NONE && RIGHT(cp1)->item.t != NONE)
					weaponr_fd_idx = FIRESH_FiredefsIDXForWeapon(&csi.ods[RIGHT(cp1)->item.m], RIGHT(cp1)->item.t);
				if (HOLSTER(cp1) && HOLSTER(cp1)->item.m != NONE && HOLSTER(cp1)->item.t != NONE)
					weaponh_fd_idx = FIRESH_FiredefsIDXForWeapon(&csi.ods[HOLSTER(cp1)->item.m], HOLSTER(cp1)->item.t);
				/* disregard left hand, or dual-wielding guys are too good */

				if (weaponr_fd_idx < 0 || weaponh_fd_idx < 0) {
					/* @todo: Is there a better way to check for this case? */
					Com_DPrintf(DEBUG_CLIENT, "CL_SwapSkills: Bad or no firedef indices found (weaponr_fd_idx=%i and weaponh_fd_idx=%i)... skipping\n", weaponr_fd_idx, weaponh_fd_idx);
				} else {
					no1 = 2 * (RIGHT(cp1) && skill == csi.ods[RIGHT(cp1)->item.m].fd[weaponr_fd_idx][fmode1].weaponSkill)
						+ 2 * (RIGHT(cp1) && skill == csi.ods[RIGHT(cp1)->item.m].fd[weaponr_fd_idx][fmode2].weaponSkill)
						+ (HOLSTER(cp1) && csi.ods[HOLSTER(cp1)->item.t].reload
						   && skill == csi.ods[HOLSTER(cp1)->item.m].fd[weaponh_fd_idx][fmode1].weaponSkill)
						+ (HOLSTER(cp1) && csi.ods[HOLSTER(cp1)->item.t].reload
						   && skill == csi.ods[HOLSTER(cp1)->item.m].fd[weaponh_fd_idx][fmode2].weaponSkill);

					for (i2 = i1 + 1; i2 < team->num; i2++) {
						cp2 = team->chr[i2];
						weaponr_fd_idx = -1;
						weaponh_fd_idx = -1;

						if (RIGHT(cp2) && RIGHT(cp2)->item.m != NONE && RIGHT(cp2)->item.t != NONE)
							weaponr_fd_idx = FIRESH_FiredefsIDXForWeapon(&csi.ods[RIGHT(cp2)->item.m], RIGHT(cp2)->item.t);
						if (HOLSTER(cp2) && HOLSTER(cp2)->item.m != NONE && HOLSTER(cp2)->item.t != NONE)
							weaponh_fd_idx = FIRESH_FiredefsIDXForWeapon(&csi.ods[HOLSTER(cp2)->item.m], HOLSTER(cp2)->item.t);

						if (weaponr_fd_idx < 0 || weaponh_fd_idx < 0) {
							/* @todo: Is there a better way to check for this case? */
							Com_DPrintf(DEBUG_CLIENT, "CL_SwapSkills: Bad or no firedef indices found (weaponr_fd_idx=%i and weaponh_fd_idx=%i)... skipping\n", weaponr_fd_idx, weaponh_fd_idx);
						} else {
							no2 = 2 * (RIGHT(cp2) && skill == csi.ods[RIGHT(cp2)->item.m].fd[weaponr_fd_idx][fmode1].weaponSkill)
								+ 2 * (RIGHT(cp2) && skill == csi.ods[RIGHT(cp2)->item.m].fd[weaponr_fd_idx][fmode2].weaponSkill)
								+ (HOLSTER(cp2) && csi.ods[HOLSTER(cp2)->item.t].reload
								   && skill == csi.ods[HOLSTER(cp2)->item.m].fd[weaponh_fd_idx][fmode1].weaponSkill)
								+ (HOLSTER(cp2) && csi.ods[HOLSTER(cp2)->item.t].reload
								   && skill == csi.ods[HOLSTER(cp2)->item.m].fd[weaponh_fd_idx][fmode2].weaponSkill);

							if (no1 > no2 /* more use of this skill */
								 || (no1 && no1 == no2)) { /* or earlier on list */
								tmp1 = cp1->skills[skill];
								tmp2 = cp2->skills[skill];
								cp1->skills[skill] = max(tmp1, tmp2);
								cp2->skills[skill] = min(tmp1, tmp2);

								switch (skill) {
								case SKILL_CLOSE:
									tmp1 = cp1->skills[ABILITY_SPEED];
									tmp2 = cp2->skills[ABILITY_SPEED];
									cp1->skills[ABILITY_SPEED] = max(tmp1, tmp2);
									cp2->skills[ABILITY_SPEED] = min(tmp1, tmp2);
									break;
								case SKILL_HEAVY:
									tmp1 = cp1->skills[ABILITY_POWER];
									tmp2 = cp2->skills[ABILITY_POWER];
									cp1->skills[ABILITY_POWER] = max(tmp1, tmp2);
									cp2->skills[ABILITY_POWER] = min(tmp1, tmp2);
									break;
								case SKILL_ASSAULT:
									/* no related basic attribute */
									break;
								case SKILL_SNIPER:
									tmp1 = cp1->skills[ABILITY_ACCURACY];
									tmp2 = cp2->skills[ABILITY_ACCURACY];
									cp1->skills[ABILITY_ACCURACY] = max(tmp1, tmp2);
									cp2->skills[ABILITY_ACCURACY] = min(tmp1, tmp2);
									break;
								case SKILL_EXPLOSIVE:
									tmp1 = cp1->skills[ABILITY_MIND];
									tmp2 = cp2->skills[ABILITY_MIND];
									cp1->skills[ABILITY_MIND] = max(tmp1, tmp2);
									cp2->skills[ABILITY_MIND] = min(tmp1, tmp2);
									break;
								default:
									Sys_Error("CL_SwapSkills: illegal skill %i.\n", skill);
								}
							} else if (no1 < no2) {
								tmp1 = cp1->skills[skill];
								tmp2 = cp2->skills[skill];
								cp2->skills[skill] = max(tmp1, tmp2);
								cp1->skills[skill] = min(tmp1, tmp2);

								switch (skill) {
								case SKILL_CLOSE:
									tmp1 = cp1->skills[ABILITY_SPEED];
									tmp2 = cp2->skills[ABILITY_SPEED];
									cp2->skills[ABILITY_SPEED] = max(tmp1, tmp2);
									cp1->skills[ABILITY_SPEED] = min(tmp1, tmp2);
									break;
								case SKILL_HEAVY:
									tmp1 = cp1->skills[ABILITY_POWER];
									tmp2 = cp2->skills[ABILITY_POWER];
									cp2->skills[ABILITY_POWER] = max(tmp1, tmp2);
									cp1->skills[ABILITY_POWER] = min(tmp1, tmp2);
									break;
								case SKILL_ASSAULT:
									break;
								case SKILL_SNIPER:
									tmp1 = cp1->skills[ABILITY_ACCURACY];
									tmp2 = cp2->skills[ABILITY_ACCURACY];
									cp2->skills[ABILITY_ACCURACY] = max(tmp1, tmp2);
									cp1->skills[ABILITY_ACCURACY] = min(tmp1, tmp2);
									break;
								case SKILL_EXPLOSIVE:
									tmp1 = cp1->skills[ABILITY_MIND];
									tmp2 = cp2->skills[ABILITY_MIND];
									cp2->skills[ABILITY_MIND] = max(tmp1, tmp2);
									cp1->skills[ABILITY_MIND] = min(tmp1, tmp2);
									break;
								default:
									Sys_Error("CL_SwapSkills: illegal skill %i.\n", skill);
								}
							}
						} /* if xx_fd_xx < 0 */
					} /* for */
				} /* if xx_fd_xx < 0 */
			}
		}
	}
}

/**
 * @brief Assigns initial team of soldiers with equipment to aircraft
 * @note If assign_initial is called with one parameter (e.g. a 1), this is for
 * multiplayer - assign_initial with no parameters is for singleplayer
 */
static void B_AssignInitial_f (void)
{
	int i;
	aircraft_t *aircraft;

	if (!baseCurrent) {
		if (ccs.singleplayer)
			return;
		aircraft = AIR_AircraftGetFromIdx(0);
		assert(aircraft);
		baseCurrent = aircraft->homebase;
	}
	assert(baseCurrent);

	if (!ccs.singleplayer) {
		CL_ResetTeamInBase();
		Cvar_Set("mn_teamname", _("NewTeam"));
	}

	for (i = MAX_TEAMLIST; --i >= 0;)
		Cbuf_AddText(va("team_hire %i;", i));

	Cbuf_AddText("pack_initial;");
	if (!ccs.singleplayer)
		MN_PushMenu("team");
}

/**
 * @brief Assigns initial soldier equipment for the first base
 */
static void B_PackInitialEquipment_f (void)
{
	int i, p, container, price = 0;
	equipDef_t *ed;
	character_t *chr;
	invList_t *ic, *next;
	base_t *base = baseCurrent;
	int aircraft_idxInBase;
	aircraft_t *aircraft = NULL;
	const char *name = curCampaign ? cl_initial_equipment->string : Cvar_VariableString("equip");
	chrList_t chr_list_temp;

	/* check syntax */
	if (Cmd_Argc() > 1) {
		Com_Printf("Usage: %s\n", Cmd_Argv(0));
		return;
	}

	if (!base)
		return;
	aircraft_idxInBase = base->aircraftCurrent;

	for (i = 0, ed = csi.eds; i < csi.numEDs; i++, ed++)
		if (!Q_strncmp(name, ed->name, MAX_VAR))
			break;

	if (i == csi.numEDs) {
		Com_DPrintf(DEBUG_CLIENT, "B_PackInitialEquipment_f: Initial Phalanx equipment %s not found.\n", name);
	} else if ((aircraft_idxInBase >= 0) && (aircraft_idxInBase < base->numAircraftInBase)) {
		aircraft = &base->aircraft[base->aircraftCurrent];
		chr_list_temp.num = 0;
		for (i = 0; i < aircraft->maxTeamSize; i++) {
			if (aircraft->teamIdxs[i] != -1) {
				chr = &gd.employees[aircraft->teamTypes[i]][aircraft->teamIdxs[i]].chr;
				/* pack equipment */
				Com_DPrintf(DEBUG_CLIENT, "B_PackInitialEquipment_f: Packing initial equipment for %s.\n", chr->name);
				INVSH_EquipActor(chr->inv, ed->num, MAX_OBJDEFS, name, chr);
				Com_DPrintf(DEBUG_CLIENT, "B_PackInitialEquipment_f: armour: %i, weapons: %i\n", chr->armour, chr->weapons);
				chr_list_temp.chr[chr_list_temp.num] = chr;
				chr_list_temp.num++;
			}
		}
		CL_AddCarriedToEq(aircraft, &base->storage);
		CL_SwapSkills(&chr_list_temp);

		/* pay for the initial equipment */
		for (container = 0; container < csi.numIDs; container++) {
			for (p = 0; p < aircraft->maxTeamSize; p++) {
				if (aircraft->teamIdxs[p] != -1) {
					chr = &gd.employees[aircraft->teamTypes[i]][aircraft->teamIdxs[i]].chr;
					for (ic = chr->inv->c[container]; ic; ic = next) {
						item_t item = ic->item;
						price += csi.ods[item.t].price;
						Com_DPrintf(DEBUG_CLIENT, "B_PackInitialEquipment_f()... adding price for %s, price: %i\n", csi.ods[item.t].id, price);
						next = ic->next;
					}
				}
			}
		}
		CL_UpdateCredits(ccs.credits - price);
	}
}

/* FIXME: This value is in menu_geoscape, too */
/*		 make this variable?? */
#define BASE_COSTS 100000

/**
 * @brief Constructs a new base.
 * @sa CL_NewBase
 */
static void B_BuildBase_f (void)
{
	const nation_t *nation = NULL;
	assert(baseCurrent);
	assert(!baseCurrent->founded);
	assert(ccs.singleplayer);

	if (ccs.credits - BASE_COSTS > 0) {
		if (CL_NewBase(baseCurrent, newBasePos)) {
			Com_DPrintf(DEBUG_CLIENT, "B_BuildBase_f: numBases: %i\n", gd.numBases);
			baseCurrent->idx = gd.numBases - 1;
			baseCurrent->founded = qtrue;
			baseCurrent->numAircraftInBase = 0;
			baseCurrent->baseStatus = BASE_WORKING;
			stats.basesBuild++;
			gd.mapAction = MA_NONE;
			CL_UpdateCredits(ccs.credits - BASE_COSTS);
			Q_strncpyz(baseCurrent->name, mn_base_title->string, sizeof(baseCurrent->name));
			nation = MAP_GetNation(baseCurrent->pos);
			if (nation)
				Com_sprintf(messageBuffer, sizeof(messageBuffer), _("A new base has been built: %s (nation: %s)"), mn_base_title->string, _(nation->name));
			else
				Com_sprintf(messageBuffer, sizeof(messageBuffer), _("A new base has been built: %s"), mn_base_title->string);
			MN_AddNewMessage(_("Base built"), messageBuffer, qfalse, MSG_CONSTRUCTION, NULL);
			Radar_Initialise(&(baseCurrent->radar), 0);
			AL_FillInContainment(baseCurrent);

			/* initial base equipment */
			if (gd.numBases == 1) {
				INV_InitialEquipment(baseCurrent, curCampaign);
				CL_GameTimeFast();
				CL_GameTimeFast();
			}
			Cbuf_AddText(va("mn_select_base %i;", baseCurrent->idx));
			return;
		}
	} else {
		Q_strncpyz(messageBuffer, _("Not enough credits to set up a new base."), sizeof(messageBuffer));
		MN_AddNewMessage(_("Base built"), messageBuffer, qfalse, MSG_CONSTRUCTION, NULL);
	}
}

/**
 * @brief Sets the baseStatus to BASE_NOT_USED
 * @param[in] base Which base should be resetted?
 * @sa CL_CampaignRemoveMission
 */
void B_BaseResetStatus (base_t* const base)
{
	assert(base);
	base->baseStatus = BASE_NOT_USED;
	if (gd.mapAction == MA_BASEATTACK)
		gd.mapAction = MA_NONE;
}

/**
 * @brief Initiates an attack on a base.
 * @sa B_BaseAttack_f
 * @sa CL_CampaignAddMission
 * @param[in] base Which base is under attack?
 */
void B_BaseAttack (base_t* const base)
{
	mission_t* ms = NULL;

	assert(base);

	/* base is already under attack - don't spawn a new mission */
	if (base->baseStatus == BASE_UNDER_ATTACK)
		return;

	/* HACK FIXME */
	if (!base->numAircraftInBase) {
		Com_Printf("B_BaseAttack: FIXME: This base (%s) can not be set under attack - because there are not crafts in this base\n", base->name);
		return;
	}

	ms = CL_AddMission(va("baseattack%i:%i", (int)base->pos[0], (int)base->pos[1]));
	if (!ms) {
		Com_DPrintf(DEBUG_CLIENT, "B_BaseAttack: Could not set base %s under attack\n", base->name);
		return;
	}

	if (CP_SpawnBaseAttackMission(base, ms, NULL)) {
		base->baseStatus = BASE_UNDER_ATTACK;
		stats.basesAttacked++;
	}

#if 0							/*@todo: run eventhandler for each building in base */
	if (b->onAttack)
		Cbuf_AddText(va("%s %i", b->onAttack, b->id));
#endif
}

/**
 * @brief Initiates an attack on a base.
 * @sa B_BaseAttack
 */
static void B_BaseAttack_f (void)
{
	int whichBaseID;

	if (Cmd_Argc() < 2) {
		Com_Printf("Usage: %s <baseID>\n", Cmd_Argv(0));
		return;
	}

	whichBaseID = atoi(Cmd_Argv(1));

	if (whichBaseID >= 0 && whichBaseID < gd.numBases) {
		B_BaseAttack(&gd.bases[whichBaseID]);
	}
}

/**
 * @brief Builds a base map for tactical combat.
 * @sa SV_AssembleMap
 * @sa B_BaseAttack
 * @note Do we need day and night maps here, too? Sure!
 * @todo Search a empty field and add a alien craft there, also add alien
 * spawn points around the craft, also some trees, etc. for their cover
 * @todo Add soldier spawn points, the best place is quarters.
 * @todo We need to get rid of the tunnels to nirvana.
 */
static void B_AssembleMap_f (void)
{
	int row, col;
	char baseMapPart[1024];
	building_t *entry;
	char maps[2024];
	char coords[2048];
	int setUnderAttack = 0, baseID = 0;
	base_t* base = baseCurrent;

	if (Cmd_Argc() < 2)
		Com_DPrintf(DEBUG_CLIENT, "Usage: %s <baseID> <setUnderAttack>\n", Cmd_Argv(0));
	else {
		if (Cmd_Argc() == 3)
			setUnderAttack = atoi(Cmd_Argv(2));
		baseID = atoi(Cmd_Argv(1));
		if (baseID < 0 || baseID >= gd.numBases) {
			Com_DPrintf(DEBUG_CLIENT, "Invalid baseID: %i\n", baseID);
			return;
		}
		base = &gd.bases[baseID];
	}

	if (!base) {
		Com_Printf("B_AssembleMap_f: No base to assemble\n");
		return;
	}

	if (setUnderAttack) {
		B_BaseAttack(base);
		Com_DPrintf(DEBUG_CLIENT, "Set base %i under attack\n", base->idx);
	}

	/* reset menu text */
	menuText[TEXT_STANDARD] = NULL;

	*maps = '\0';
	*coords = '\0';

	/* reset the used flag */
	for (row = 0; row < BASE_SIZE; row++)
		for (col = 0; col < BASE_SIZE; col++) {
			if (base->map[row][col] != BASE_FREESLOT && base->map[row][col] != BASE_INVALID_SPACE) {
				entry = B_GetBuildingByIdx(base, base->map[row][col]);
				entry->used = 0;
			}
		}

	/* @todo: If a building is still under construction, it will be assembled as a finished part */
	/* otherwise we need mapparts for all the maps under construction */
	for (row = 0; row < BASE_SIZE; row++)
		for (col = 0; col < BASE_SIZE; col++) {
			baseMapPart[0] = '\0';

			if (base->map[row][col] != BASE_FREESLOT && base->map[row][col] != BASE_INVALID_SPACE) {
				entry = B_GetBuildingByIdx(base, base->map[row][col]);

				/* basemaps with needs are not (like the images in B_DrawBase) two maps - but one */
				/* this is why we check the used flag and continue if it was set already */
				if (!entry->used && entry->needs) {
					entry->used = 1;
				} else if (entry->needs) {
					Com_DPrintf(DEBUG_CLIENT, "B_AssembleMap_f: '%s' needs '%s' (used: %i)\n", entry->id, entry->needs, entry->used );
					entry->used = 0;
					continue;
				}

				if (entry->mapPart)
					Q_strncpyz(baseMapPart, va("b/%s", entry->mapPart), sizeof(baseMapPart));
				else
					Com_Printf("B_AssembleMap_f: Error - map has no mapPart set. Building '%s'\n'", entry->id);
			} else
				Q_strncpyz(baseMapPart, "b/empty", sizeof(baseMapPart));

			if (*baseMapPart) {
				Q_strcat(maps, baseMapPart, sizeof(maps));
				Q_strcat(maps, " ", sizeof(maps));
				/* basetiles are 16 units in each direction
				 * 512 / UNIT_SIZE = 16
				 * 512 is the size in the mapeditor and the worldplane for a
				 * single base map tile */
				Q_strcat(coords, va("%i %i ", col * 16, (BASE_SIZE - row - 1) * 16), sizeof(coords));
			}
		}
	/* set maxlevel for base attacks to 5 */
	map_maxlevel_base = 6;

	if (curCampaign)
		SAV_QuickSave();

	Cbuf_AddText(va("map \"%s\" \"%s\"\n", maps, coords));
}

/**
 * @brief Cleans all bases but restore the base names
 * @sa CL_GameNew
 */
void B_NewBases (void)
{
	/* reset bases */
	int i;
	char title[MAX_VAR];

	for (i = 0; i < MAX_BASES; i++) {
		Q_strncpyz(title, gd.bases[i].name, sizeof(title));
		B_ClearBase(&gd.bases[i]);
		Q_strncpyz(gd.bases[i].name, title, sizeof(title));
	}
}

/**
 * @brief Builds a random base
 *
 * call B_AssembleMap with a random base over script command 'base_assemble_rand'
 */
static void B_AssembleRandomBase_f (void)
{
	int setUnderAttack = 0;
	int randomBase = rand() % gd.numBases;
	if (Cmd_Argc() < 2)
		Com_DPrintf(DEBUG_CLIENT, "Usage: %s <setUnderAttack>\n", Cmd_Argv(0));
	else
		setUnderAttack = atoi(Cmd_Argv(1));

	if (!gd.bases[randomBase].founded) {
		Com_Printf("Base with id %i was not founded or already destroyed\n", randomBase);
		return;
	}

	Cbuf_AddText(va("base_assemble %i %i\n", randomBase, setUnderAttack));
}

#ifdef DEBUG
/**
 * @brief Just lists all buildings with their data
 *
 * Just for debugging purposes - not needed in game
 * @todo To be extended for load/save purposes
 */
static void B_BuildingList_f (void)
{
	int i, j, k;
	base_t *base;
	building_t *building;

	/*maybe someone call this command before the buildings are parsed?? */
	if (!baseCurrent) {
		Com_Printf("No base selected\n");
		return;
	}

	for (i = 0, base = gd.bases; i < gd.numBases; i++, base++) {
		if (base->founded == qfalse)
			continue;

		building = &gd.buildings[base->idx][i];
		Com_Printf("\nBase id %i: %s\n", i, base->name);
		for (j = 0; j < gd.numBuildings[base->idx]; j++) {
			Com_Printf("...Building: %s #%i - id: %i\n", building->id, B_GetNumberOfBuildingsInBaseByTypeIDX(baseCurrent->idx, building->buildingType),
				building->idx);
			Com_Printf("...image: %s\n", building->image);
			Com_Printf(".....Status:\n");
			for (k = 0; k < BASE_SIZE * BASE_SIZE; k++) {
				if (k > 1 && k % BASE_SIZE == 0)
					Com_Printf("\n");
				Com_Printf("%i ", building->buildingStatus);
				if (!building->buildingStatus)
					break;
			}
			Com_Printf("\n");
			building++;
		}
	}
}
#endif

#ifdef DEBUG
/**
 * @brief Just lists all bases with their data
 *
 * Just for debugging purposes - not needed in game
 * @todo To be extended for load/save purposes
 */
static void B_BaseList_f (void)
{
	int i, row, col, j;
	base_t *base;

	for (i = 0, base = gd.bases; i < MAX_BASES; i++, base++) {
		Com_Printf("Base id %i\n", base->idx);
		Com_Printf("Base title %s\n", base->name);
		Com_Printf("Base founded %i\n", base->founded);
		Com_Printf("Base numAircraftInBase %i\n", base->numAircraftInBase);
		Com_Printf("Base numMissileBattery %i\n", base->maxBatteries);
		Com_Printf("Base numLaserBattery %i\n", base->maxLasers);
		Com_Printf("Base sensorWidth %i\n", base->radar.range);
		Com_Printf("Base numSensoredAircraft %i\n", base->radar.numUfos);
		Com_Printf("Base aircraft %i\n", base->numAircraftInBase);
		for (j = 0; j < base->numAircraftInBase; j++) {
			Com_Printf("Base aircraft-team %i\n", base->aircraft[j].teamSize);
		}
		Com_Printf("Base pos %f:%f\n", base->pos[0], base->pos[1]);
		Com_Printf("Base map:\n");
		for (row = 0; row < BASE_SIZE; row++) {
			if (row)
				Com_Printf("\n");
			for (col = 0; col < BASE_SIZE; col++)
				Com_Printf("%i ", base->map[row][col]);
		}
		Com_Printf("\n");
	}
}
#endif

/**
 * @brief Sets the title of the base.
 */
static void B_SetBaseTitle_f (void)
{
	Com_DPrintf(DEBUG_CLIENT, "B_SetBaseTitle_f: #bases: %i\n", gd.numBases);
	if (gd.numBases < MAX_BASES)
		Cvar_Set("mn_base_title", gd.bases[gd.numBases].name);
	else {
		MN_AddNewMessage(_("Notice"), _("You've reached the base limit."), qfalse, MSG_STANDARD, NULL);
		MN_PopMenu(qfalse);		/* remove the new base popup */
	}
}

/**
 * @brief Creates console command to change the name of a base.
 * Copies the value of the cvar mn_base_title over as the name of the
 * current selected base
 */
static void B_ChangeBaseName_f (void)
{
	/* maybe called without base initialized or active */
	if (!baseCurrent)
		return;

	Q_strncpyz(baseCurrent->name, Cvar_VariableString("mn_base_title"), sizeof(baseCurrent->name));
}

/**
 * @brief Checks whether the building menu or the pedia entry should be called when baseCurrent->buildingCurrent is set
 */
static void B_BuildingOpen_f (void)
{
	if (baseCurrent && baseCurrent->buildingCurrent) {
		if (baseCurrent->buildingCurrent->buildingStatus != B_STATUS_WORKING) {
			UP_OpenWith(baseCurrent->buildingCurrent->pedia);
		} else {
			/* FIXME: Some buildings are even allowed to be opened when base is under attack */
			if (baseCurrent->baseStatus == BASE_UNDER_ATTACK)
				return;
			switch (baseCurrent->buildingCurrent->buildingType) {
			case B_LAB:
				MN_PushMenu("research");
				break;
			case B_HOSPITAL:
				MN_PushMenu("hospital");
				break;
			case B_ALIEN_CONTAINMENT:
				MN_PushMenu("aliencont");
				break;
			case B_QUARTERS:
				MN_PushMenu("employees");
				break;
			case B_WORKSHOP:
				MN_PushMenu("production");
				break;
			case B_DEFENSE_LASER:
			case B_DEFENSE_MISSILE:
				MN_PushMenu("basedefense");
				break;
			case B_HANGAR:
			case B_SMALL_HANGAR:
				if (baseCurrent->numAircraftInBase)
					MN_PushMenu("aircraft");
				else {
					MN_PushMenu("buyaircraft");
					/* transfer is only possible when there are at least two bases */
					if (gd.numBases > 1)
						MN_Popup(_("Note"), _("No aircraft in this base - You first have to purchase or transfer an aircraft\n"));
					else
						MN_Popup(_("Note"), _("No aircraft in this base - You first have to purchase an aircraft\n"));
				}
				break;
			case B_STORAGE:
			case B_ANTIMATTER:
				MN_PushMenu("buy");
				break;
			default:
				UP_OpenWith(baseCurrent->buildingCurrent->pedia);
				break;
			}
		}
	} else {
		Com_Printf("Usage: Only call me from baseview\n");
	}
}

/**
 * @brief This function checks whether a user build the max allowed amount of bases
 * if yes, the MN_PopMenu will pop the base build menu from the stack
 */
static void B_CheckMaxBases_f (void)
{
	if (gd.numBases >= MAX_BASES) {
		MN_PopMenu(qfalse);
	}
}

/**
 * @brief Get building type by base capacity.
 * @param[in] cap Enum type of baseCapacities_t.
 * @return Enum type of buildingType_t.
 * @sa B_UpdateBaseCapacities
 * @sa B_PrintCapacities_f
 */
static buildingType_t B_GetBuildingTypeByCapacity (baseCapacities_t cap)
{
	switch (cap) {
	case CAP_ALIENS:
		return B_ALIEN_CONTAINMENT;
	case CAP_AIRCRAFTS_SMALL:
		return B_SMALL_HANGAR;
	case CAP_AIRCRAFTS_BIG:
		return B_HANGAR;
	case CAP_EMPLOYEES:
		return B_QUARTERS;
	case CAP_ITEMS:
		return B_STORAGE;
	case CAP_LABSPACE:
		return B_LAB;
	case CAP_WORKSPACE:
		return B_WORKSHOP;
	case CAP_HOSPSPACE:
		return B_HOSPITAL;
	case CAP_UFOHANGARS:
		return B_UFO_HANGAR;	/* Note, that CAP_UFOHANGARS also uses B_SMALL_UFO_HANGAR. */
	case CAP_ANTIMATTER:
		return B_ANTIMATTER;
	default:
		return B_MISC;
	}
}

#ifdef DEBUG
/**
 * @brief Debug function for printing all capacities in given base.
 */
static void B_PrintCapacities_f (void)
{
	int i, j;
	base_t *base = NULL;
	buildingType_t building;

	if (Cmd_Argc() < 2) {
		Com_Printf("Usage: %s <baseID>\n", Cmd_Argv(0));
		return;
	}

	i = atoi(Cmd_Argv(1));
	if (i >= gd.numBases) {
		Com_Printf("invalid baseID (%s)\n", Cmd_Argv(1));
		return;
	}
	base = gd.bases + i;
	for (i = 0; i < MAX_CAP; i++) {
		building = B_GetBuildingTypeByCapacity(i);
		for (j = 0; j < gd.numBuildingTypes; j++) {
			if (gd.buildingTypes[j].buildingType == building)
				break;
		}
		if (i == CAP_UFOHANGARS)
			Com_Printf("Building: UFO Hangars, capacity max: %i, capacity cur: %i\n",
			base->capacities[i].max, base->capacities[i].cur);
		else
			Com_Printf("Building: %s, capacity max: %i, capacity cur: %i\n",
			gd.buildingTypes[j].id, base->capacities[i].max, base->capacities[i].cur);
	}
}
#endif

/**
 * @brief Resets console commands.
 * @sa MN_ResetMenus
 */
void B_ResetBaseManagement (void)
{
	Com_DPrintf(DEBUG_CLIENT, "Reset basemanagement\n");

	/* add commands and cvars */
	Cmd_AddCommand("mn_prev_base", B_PrevBase_f, "Go to the previous base");
	Cmd_AddCommand("mn_next_base", B_NextBase_f, "Go to the next base");
	Cmd_AddCommand("mn_select_base", B_SelectBase_f, NULL);
	Cmd_AddCommand("mn_build_base", B_BuildBase_f, NULL);
	Cmd_AddCommand("new_building", B_NewBuildingFromList_f, NULL);
	Cmd_AddCommand("set_building", B_SetBuilding_f, NULL);
	Cmd_AddCommand("mn_setbasetitle", B_SetBaseTitle_f, NULL);
	Cmd_AddCommand("bases_check_max", B_CheckMaxBases_f, NULL);
	Cmd_AddCommand("rename_base", B_RenameBase_f, "Rename the current base");
	Cmd_AddCommand("base_attack", B_BaseAttack_f, NULL);
	Cmd_AddCommand("base_changename", B_ChangeBaseName_f, "Called after editing the cvar base name");
	Cmd_AddCommand("base_init", B_BaseInit_f, NULL);
	Cmd_AddCommand("base_assemble", B_AssembleMap_f, "Called to assemble the current selected base");
	Cmd_AddCommand("base_assemble_rand", B_AssembleRandomBase_f, NULL);
	Cmd_AddCommand("building_open", B_BuildingOpen_f, NULL);
	Cmd_AddCommand("building_init", B_BuildingInit, NULL);
	Cmd_AddCommand("building_status", B_BuildingStatus_f, NULL);
	Cmd_AddCommand("building_destroy", B_BuildingDestroy_f, "Function to destroy a buliding (select via right click in baseview first)");
	Cmd_AddCommand("buildinginfo_click", B_BuildingInfoClick_f, NULL);
	Cmd_AddCommand("buildings_click", B_BuildingClick_f, NULL);
	Cmd_AddCommand("reset_building_current", B_ResetBuildingCurrent_f, NULL);
	Cmd_AddCommand("pack_initial", B_PackInitialEquipment_f, NULL);
	Cmd_AddCommand("assign_initial", B_AssignInitial_f, NULL);
#ifdef DEBUG
	Cmd_AddCommand("debug_baselist", B_BaseList_f, "Print base information to the game console");
	Cmd_AddCommand("debug_buildinglist", B_BuildingList_f, "Print building information to the game console");
	Cmd_AddCommand("debug_capacities", B_PrintCapacities_f, "Debug function to show all capacities in given base");
#endif

	mn_base_count = Cvar_Get("mn_base_count", "0", 0, "Current amount of build bases");
	mn_base_id = Cvar_Get("mn_base_id", "-1", 0, "Internal id of the current selected base");
}

/**
 * @brief Counts the number of bases.
 * @return The number of founded bases.
 */
int B_GetFoundedBaseCount (void)
{
	int i, cnt = 0;

	for (i = 0; i < MAX_BASES; i++) {
		if (!gd.bases[i].founded)
			continue;
		cnt++;
	}

	return cnt;
}

/**
 * @brief Updates base data
 * @sa CL_CampaignRun
 * @note called every "day"
 * @sa AIRFIGHT_ProjectileHitsBase
 */
void B_UpdateBaseData (void)
{
	building_t *b = NULL;
	int i, j;
	int newBuilding = 0, new;

	for (i = 0; i < MAX_BASES; i++) {
		if (!gd.bases[i].founded)
			continue;
		for (j = 0; j < gd.numBuildings[i]; j++) {
			b = &gd.buildings[i][j];
			if (!b)
				continue;
			new = B_CheckBuildingConstruction(b, i);
			newBuilding += new;
			if (new) {
				Com_sprintf(messageBuffer, MAX_MESSAGE_TEXT, _("Construction of %s building finished in base %s."), _(b->name), gd.bases[i].name);
				MN_AddNewMessage(_("Building finished"), messageBuffer, qfalse, MSG_CONSTRUCTION, NULL);
			}
		}

		/* Repair base buildings */
		if (gd.bases[i].batteryDamage <= MAX_BATTERY_DAMAGE) {
			gd.bases[i].batteryDamage += 20;
			if (gd.bases[i].batteryDamage > MAX_BATTERY_DAMAGE)
				gd.bases[i].batteryDamage = MAX_BATTERY_DAMAGE;
		}
		if (gd.bases[i].baseDamage <= MAX_BASE_DAMAGE) {
			gd.bases[i].baseDamage += 20;
			if (gd.bases[i].baseDamage > MAX_BASE_DAMAGE)
				gd.bases[i].baseDamage = MAX_BASE_DAMAGE;
		}
	}
}

/**
 * @brief Checks whether the construction of a building is finished.
 *
 * Calls the onConstruct functions and assign workers, too.
 */
int B_CheckBuildingConstruction (building_t * building, int base_idx)
{
	int newBuilding = 0;

	if (building->buildingStatus == B_STATUS_UNDER_CONSTRUCTION) {
		if (building->timeStart && (building->timeStart + building->buildTime) <= ccs.date.day) {
			B_UpdateBaseBuildingStatus(building, &gd.bases[base_idx], B_STATUS_WORKING);

			if (*building->onConstruct) {
				gd.bases[base_idx].buildingCurrent = building;
				Com_DPrintf(DEBUG_CLIENT, "B_CheckBuildingConstruction: %s %i;\n", building->onConstruct, base_idx);
				Cbuf_AddText(va("%s %i;", building->onConstruct, base_idx));
			}

			newBuilding++;
		}
	}
	if (newBuilding)
		/*update the building-list */
		B_BuildingInit();

	return newBuilding;
}

/**
 * @brief Update buildings status when we gain or loose power.
 * @param[in] *base Pointer to the base with newly constructed power supply.
 */
void B_UpdateStatusWithPower (base_t *base)
{
	int i;

	assert(base);
	for (i = 0; i < gd.numBuildings[base->idx]; i++) {
		if (gd.buildings[base->idx][i].buildingType == B_POWER)
			continue;
		if (gd.buildings[base->idx][i].buildingStatus == B_STATUS_WORKING)
			B_UpdateBaseBuildingStatus(&gd.buildings[base->idx][i], &gd.bases[base->idx], B_STATUS_WORKING);
	}
}

/**
 * @brief Counts the number of soldiers in given aircraft.
 * @param[in] aircraft Pointer to the aircraft, for which we return the amount of soldiers.
 * @return Amount of soldiers.
 */
int B_GetNumOnTeam (aircraft_t *aircraft)
{
	assert(aircraft);
	return aircraft->teamSize;
}

/**
 * @brief Returns the aircraft pointer from the given base and perform some sanity checks
 * @param[in] base Base the to get the aircraft from - may not be null
 * @param[in] index Base aircraft index
 */
aircraft_t *B_GetAircraftFromBaseByIndex (base_t* base, int index)
{
	assert(base);
	if (index < base->numAircraftInBase) {
		return &base->aircraft[index];
	} else {
		Com_DPrintf(DEBUG_CLIENT, "B_GetAircraftFromBaseByIndex: error: index bigger then number of aircrafts in this base\n");
		return NULL;
	}
}

/**
 * @brief Do anything when dropship returns to base
 * @param[in] aircraft Returning aircraft.
 * @param[in] base Base, where aircraft lands.
 * @note Place here any stuff, which should be called
 * @note when Drophip returns to base.
 * @sa CL_CampaignRunAircraft
 */
void CL_DropshipReturned (base_t* base, aircraft_t* aircraft)
{
	HOS_ReaddEmployeesInHospital(aircraft);		/**< Try to readd soldiers to hospital. */
	/* Don't call cargo functions if aircraft is not a transporter. */
	if (aircraft->type != AIRCRAFT_TRANSPORTER)
		return;
	AL_AddAliens(aircraft);			/**< Add aliens to Alien Containment. */
	AL_CountAll();				/**< Count all alive aliens. */
	INV_SellOrAddItems(aircraft);		/**< Sell collected items or add them to storage. */
	RS_MarkResearchable(qfalse);		/**< Mark new technologies researchable. */

	/* Now empty alien/item cargo just in case. */
	memset(aircraft->aliencargo, 0, sizeof(aircraft->aliencargo));
	memset(aircraft->itemcargo, 0, sizeof(aircraft->itemcargo));
	aircraft->alientypes = 0;
}

/**
 * @brief Check if the item has been collected (i.e it is in the storage) in the given base.
 * @param[in] item_idx The index of the item in the item-list.
 * @param[in] base The base to search in.
 * @return amount Number of available items in base
 * @note Formerly known as RS_ItemInBase.
 */
int B_ItemInBase (int item_idx, base_t *base)
{
	equipDef_t *ed = NULL;

	if (!base)
		return -1;

	ed = &base->storage;

	if (!ed)
		return -1;

	/* Com_DPrintf(DEBUG_CLIENT, "B_ItemInBase: DEBUG idx %s\n",  csi.ods[item_idx].id); */

	return ed->num[item_idx];
}

/**
 * @brief Updates base capacities.
 * @param[in] cap Enum type of baseCapacities_t.
 * @param[in] *base Pointer to the base.
 * @sa B_UpdateBaseBuildingStatus
 * @sa B_BuildingDestroy_f
 */
void B_UpdateBaseCapacities (baseCapacities_t cap, base_t *base)
{
	int i, j, capacity = 0, b_idx = -1;
	buildingType_t building;

	/* Get building type. */
	building = B_GetBuildingTypeByCapacity(cap);

	switch (cap) {
	case CAP_ALIENS:		/**< Update Aliens capacity in base. */
	case CAP_EMPLOYEES:		/**< Update employees capacity in base. */
	case CAP_LABSPACE:		/**< Update laboratory space capacity in base. */
	case CAP_WORKSPACE:		/**< Update workshop space capacity in base. */
	case CAP_HOSPSPACE:		/**< Update hospital space capacity in base. */
	case CAP_ITEMS:			/**< Update items capacity in base. */
	case CAP_AIRCRAFTS_SMALL:	/**< Update aircrafts capacity in base. */
	case CAP_AIRCRAFTS_BIG:		/**< Update aircrafts capacity in base. */
	case CAP_ANTIMATTER:		/**< Update antimatter capacity in base. */
		/* Reset given capacity in current base. */
		base->capacities[cap].max = 0;
		/* Get building capacity. */
		for (i = 0; i < gd.numBuildingTypes; i++) {
			if (gd.buildingTypes[i].buildingType == building) {
				capacity = gd.buildingTypes[i].capacity;
				Com_DPrintf(DEBUG_CLIENT, "Building: %s capacity: %i\n", gd.buildingTypes[i].id, capacity);
				b_idx = i;
				break;
			}
		}
		/* Finally update capacity. */
		for (j = 0; j < gd.numBuildings[base->idx]; j++) {
			if ((gd.buildings[base->idx][j].buildingType == building)
			&& (gd.buildings[base->idx][j].buildingStatus != B_STATUS_NOT_SET)) {
				base->capacities[cap].max += capacity;
			}
		}
		/* First base gets extra space for employees. */
		if ((gd.numBases == 1) && (base->idx == 0) && (cap == CAP_EMPLOYEES))
			base->capacities[cap].max += 13;
		if (b_idx != -1)
			Com_DPrintf(DEBUG_CLIENT, "B_UpdateBaseCapacities()... updated capacity of %s: %i\n",
				gd.buildingTypes[b_idx].id, base->capacities[cap].max);
		break;
	case CAP_UFOHANGARS:	/**< Base capacities for UFO hangars. */
		/* Reset given capacity in current base. */
		base->capacities[cap].max = 0;
		/* Get B_UFO_HANGAR capacity. */
		for (i = 0; i < gd.numBuildingTypes; i++) {
			if (gd.buildingTypes[i].buildingType == building) {
				capacity = gd.buildingTypes[i].capacity;
				Com_DPrintf(DEBUG_CLIENT, "Building: %s capacity: %i\n", gd.buildingTypes[i].id, capacity);
				break;
			}
		}
		/* Add capacity of B_UFO_HANGAR. */
		for (j = 0; j < gd.numBuildings[base->idx]; j++) {
			if ((gd.buildings[base->idx][j].buildingType == building)
			&& (gd.buildings[base->idx][j].buildingStatus != B_STATUS_NOT_SET)) {
				base->capacities[cap].max += capacity;
			}
		}
		/* Now check if base has B_UFO_SMALL_HANGAR and update capacity as well. */
		if (base->hasUFOHangarSmall) {
			for (i = 0; i < gd.numBuildingTypes; i++) {
				if (gd.buildingTypes[i].buildingType == B_UFO_SMALL_HANGAR) {
					capacity = gd.buildingTypes[i].capacity;
					Com_DPrintf(DEBUG_CLIENT, "Building: %s capacity: %i\n", gd.buildingTypes[i].id, capacity);
					break;
				}
			}
			for (j = 0; j < gd.numBuildings[base->idx]; j++) {
				if ((gd.buildings[base->idx][j].buildingType == B_UFO_SMALL_HANGAR)
				&& (gd.buildings[base->idx][j].buildingStatus != B_STATUS_NOT_SET)) {
					base->capacities[cap].max += capacity;
				}
			}
		}
		break;
	case MAX_CAP:			/**< Update all capacities in base. */
		Com_DPrintf(DEBUG_CLIENT, "B_UpdateBaseCapacities()... going to update ALL capacities.\n");
		/* Loop through all capacities and update them. */
		for (i = 0; i < cap; i++) {
			B_UpdateBaseCapacities(i, base);
		}
		break;
	default:
		Sys_Error("Unknown capacity limit for this base: %i \n", cap);
		break;
	}
}

/**
 * @brief Saves an item slot
 * @sa B_LoadItemSlots
 * @sa B_Save
 */
static void B_SaveItemSlots (aircraftSlot_t* slot, int num, int* targets, sizebuf_t* sb)
{
	int i;
	int ammoIdx;

	for (i = 0; i < num; i++) {
		if (slot[i].itemIdx >= 0) {
			ammoIdx = slot[i].ammoIdx;
			MSG_WriteString(sb, csi.ods[slot[i].itemIdx].id);
			MSG_WriteShort(sb, slot[i].ammoLeft);
			MSG_WriteShort(sb, slot[i].delayNextShot);
			MSG_WriteShort(sb, slot[i].installationTime);
			/* if there is no ammo MSG_WriteString will write an empty string */
			MSG_WriteString(sb, ammoIdx >= 0 ? csi.ods[ammoIdx].id : "");
		} else {
			MSG_WriteString(sb, "");
			MSG_WriteShort(sb, 0);
			MSG_WriteShort(sb, 0);
			MSG_WriteShort(sb, 0);
			/* if there is no ammo MSG_WriteString will write an empty string */
			MSG_WriteString(sb, "");
		}
		if (targets) {
			/* save target of this weapon */
			MSG_WriteShort(sb, targets[i]);
		}
	}
}


/**
 * @brief Save callback for savegames
 * @sa B_Load
 * @sa SAV_GameSave
 */
qboolean B_Save (sizebuf_t* sb, void* data)
{
	int i, k, l;
	base_t *b;
	aircraft_t *aircraft;
	building_t *building;

	MSG_WriteShort(sb, gd.numAircraft);
	MSG_WriteByte(sb, gd.numBases);
	for (i = 0; i < gd.numBases; i++) {
		b = &gd.bases[i];
		MSG_WriteString(sb, b->name);
		MSG_WritePos(sb, b->pos);
		MSG_WriteByte(sb, b->founded);
		MSG_WriteByte(sb, b->hasHangar);
		MSG_WriteByte(sb, b->hasLab);
		MSG_WriteByte(sb, b->hasHospital);
		MSG_WriteByte(sb, b->hasMissile);
		MSG_WriteByte(sb, b->hasLaser);
		MSG_WriteByte(sb, b->hasAlienCont);
		MSG_WriteByte(sb, b->hasStorage);
		MSG_WriteByte(sb, b->hasQuarters);
		MSG_WriteByte(sb, b->hasWorkshop);
		MSG_WriteByte(sb, b->hasHangarSmall);
		MSG_WriteByte(sb, b->hasUFOHangar);
		MSG_WriteByte(sb, b->hasUFOHangarSmall);
		MSG_WriteByte(sb, b->hasPower);
		MSG_WriteByte(sb, b->hasCommand);
		MSG_WriteByte(sb, b->hasAmStorage);
		for (k = 0; k < presaveArray[PRE_BASESI]; k++)
			for (l = 0; l < presaveArray[PRE_BASESI]; l++) {
				MSG_WriteShort(sb, b->map[k][l]);
				MSG_WriteShort(sb, b->posX[k][l]);
				MSG_WriteShort(sb, b->posY[k][l]);
			}
		for (k = 0; k < presaveArray[PRE_MAXBUI]; k++) {
			building = &gd.buildings[i][k];
			MSG_WriteByte(sb, building->type_idx);
			MSG_WriteByte(sb, building->buildingStatus);
			MSG_WriteLong(sb, building->timeStart);
			MSG_WriteLong(sb, building->buildTime);
			MSG_Write2Pos(sb, building->pos);
		}
		MSG_WriteShort(sb, gd.numBuildings[i]);
		MSG_WriteByte(sb, b->baseStatus);
		MSG_WriteByte(sb, b->isDiscovered);

		MSG_WriteByte(sb, b->maxBatteries);
		B_SaveItemSlots(b->batteries, b->maxBatteries, b->targetMissileIdx, sb);

		MSG_WriteByte(sb, b->maxLasers);
		B_SaveItemSlots(b->lasers, b->maxLasers, b->targetLaserIdx, sb);

		MSG_WriteShort(sb, b->aircraftCurrent); /* might be -1 */
		MSG_WriteByte(sb, b->numAircraftInBase);
		for (k = 0; k < b->numAircraftInBase; k++) {
			aircraft = &b->aircraft[k];
			MSG_WriteString(sb, aircraft->id);
			MSG_WriteShort(sb, aircraft->idx);
			MSG_WriteByte(sb, aircraft->status);
			MSG_WriteLong(sb, aircraft->fuel);
			MSG_WritePos(sb, aircraft->pos);
			MSG_WriteShort(sb, aircraft->time);
			MSG_WriteShort(sb, aircraft->point);
			MSG_WriteByte(sb, aircraft->hangar);
			/* Save target of the ufo */
			if (aircraft->aircraftTarget)
				MSG_WriteShort(sb, aircraft->aircraftTarget - gd.ufos);
			else
				MSG_WriteShort(sb, -1);
			/* save weapon slots */
			MSG_WriteByte(sb, aircraft->maxWeapons);
			B_SaveItemSlots(aircraft->weapons, aircraft->maxWeapons, NULL, sb);

			/* save shield slots - currently only one */
			MSG_WriteByte(sb, 1);
			if (aircraft->shield.itemIdx >= 0) {
				assert(aircraft->shield.itemIdx < csi.numODs);
				MSG_WriteString(sb, csi.ods[aircraft->shield.itemIdx].id);
				MSG_WriteShort(sb, aircraft->shield.installationTime);
			} else {
				MSG_WriteString(sb, "");
				MSG_WriteShort(sb, 0);
			}
			/* save electronics slots */
			MSG_WriteByte(sb, aircraft->maxElectronics);
			for (l = 0; l < aircraft->maxElectronics; l++) {
				if (aircraft->electronics[l].itemIdx >= 0) {
					assert(aircraft->electronics[l].itemIdx < csi.numODs);
					MSG_WriteString(sb, csi.ods[aircraft->electronics[l].itemIdx].id);
					MSG_WriteShort(sb, aircraft->electronics[l].installationTime);
				} else {
					MSG_WriteString(sb, "");
					MSG_WriteShort(sb, 0);
				}
			}

			/* Save team on board */
			/** @note presaveArray[PRE_ACTTEA]==MAX_ACTIVETEAM and NOT teamSize */
			for (l = 0; l < presaveArray[PRE_ACTTEA]; l++)
				MSG_WriteShort(sb, aircraft->teamIdxs[l]);
			for (l = 0; l < presaveArray[PRE_ACTTEA]; l++)
				MSG_WriteShort(sb, aircraft->teamTypes[l]);

			MSG_WriteShort(sb, aircraft->numUpgrades);
			MSG_WriteShort(sb, aircraft->radar.range);
			MSG_WriteShort(sb, aircraft->route.numPoints);
			MSG_WriteFloat(sb, aircraft->route.distance);
			for (l = 0; l < aircraft->route.numPoints; l++)
				MSG_Write2Pos(sb, aircraft->route.point[l]);
			MSG_WriteShort(sb, aircraft->alientypes);
			MSG_WriteShort(sb, aircraft->itemtypes);
			/* Save only needed if aircraft returns from a mission. */
			switch (aircraft->status) {
			case AIR_RETURNING:
				/* aliencargo */
				for (l = 0; l < aircraft->alientypes; l++) {
					assert(*aircraft->aliencargo[l].alientype);
					MSG_WriteString(sb, aircraft->aliencargo[l].alientype);
					MSG_WriteShort(sb, aircraft->aliencargo[l].amount_alive);
					MSG_WriteShort(sb, aircraft->aliencargo[l].amount_dead);
				}
				/* itemcargo */
				for (l = 0; l < aircraft->itemtypes; l++) {
					MSG_WriteString(sb, csi.ods[aircraft->itemcargo[l].idx].id);
					MSG_WriteShort(sb, aircraft->itemcargo[l].amount);
				}
				break;
			case AIR_MISSION:
				assert(aircraft->mission);
				assert(aircraft->mission->def);
				MSG_WriteString(sb, aircraft->mission->def->name);
				break;
			}
			MSG_WritePos(sb, aircraft->direction);
			for (l = 0; l < presaveArray[PRE_AIRSTA]; l++)
				MSG_WriteLong(sb, aircraft->stats[l]);
		}
		MSG_WriteByte(sb, b->equipType);

		/* store equipment */
		for (k = 0; k < presaveArray[PRE_NUMODS]; k++) {
			MSG_WriteString(sb, csi.ods[k].id);
			MSG_WriteShort(sb, b->storage.num[k]);
			MSG_WriteByte(sb, b->storage.num_loose[k]);
		}

		MSG_WriteShort(sb, b->radar.range);

		/* Alien Containment. */
		for (k = 0; k < presaveArray[PRE_NUMALI]; k++) {
			MSG_WriteString(sb, b->alienscont[k].alientype);
			MSG_WriteShort(sb, b->alienscont[k].amount_alive);
			MSG_WriteShort(sb, b->alienscont[k].amount_dead);
		}

		/* Base capacities. */
		for (k = 0; k < presaveArray[PRE_MAXCAP]; k++) {
			MSG_WriteShort(sb, b->capacities[k].cur);
			MSG_WriteShort(sb, b->capacities[k].max);
		}

		/* Buy/Sell factors. */
		MSG_WriteByte(sb, b->buyfactor);
		MSG_WriteByte(sb, b->sellfactor);

		/* Hospital stuff. */
		for (k = 0; k < presaveArray[PRE_EMPTYP]; k++) {
			MSG_WriteShort(sb, b->hospitalListCount[k]);
			for (l = 0; l < presaveArray[PRE_MAXEMP]; l++) {
				MSG_WriteShort(sb, b->hospitalList[k][l]);
			}
		}
		MSG_WriteShort(sb, b->hospitalMissionListCount);
		for (k = 0; k < presaveArray[PRE_MAXEMP]; k++) {
			MSG_WriteShort(sb, b->hospitalMissionList[k]);
		}
	}
	return qtrue;
}

/**
 * @brief Loads the missile and laser slots data
 * @sa B_Load
 * @sa B_SaveItemSlots
 */
static void B_LoadItemSlots (base_t* base, aircraftSlot_t* slot, int num, int* targets, sizebuf_t* sb)
{
	int i;
	technology_t *tech;

	for (i = 0; i < num; i++) {
		tech = RS_GetTechByProvided(MSG_ReadString(sb));
		/* base is NULL here to not check against the storage amounts - they
		 * are already loaded in the campaign load function and set to the value
		 * after the craftitem was already removed from the initial game - thus
		 * there might not be any of these items in the storage at this point */
		/* @todo: Check whether storage and capacities needs updating now */
		if (tech)
			AII_AddItemToSlot(NULL, tech, slot + i);
		slot[i].ammoLeft = MSG_ReadShort(sb);
		slot[i].delayNextShot = MSG_ReadShort(sb);
		slot[i].installationTime = MSG_ReadShort(sb);
		tech = RS_GetTechByProvided(MSG_ReadString(sb));
		if (tech)
			slot[i].ammoIdx = AII_GetAircraftItemByID(tech->provides);
		else
			slot[i].ammoIdx = -1;
		if (targets) {
			/* read target of this weapon */
			targets[i] = MSG_ReadShort(sb);
		}
	}
}

/**
 * @brief Load callback for savegames
 * @sa B_Save
 * @sa SAV_GameLoad
 * @sa B_LoadItemSlots
 */
qboolean B_Load (sizebuf_t* sb, void* data)
{
	int i, bases, k, l, m, amount;
	base_t *b;
	const char *s;
	aircraft_t *aircraft;
	building_t *building;
	technology_t *tech;
	byte *color;

	gd.numAircraft = MSG_ReadShort(sb);
	bases = MSG_ReadByte(sb);
	for (i = 0; i < bases; i++) {
		b = &gd.bases[i];
		Q_strncpyz(b->name, MSG_ReadStringRaw(sb), sizeof(b->name));
		MSG_ReadPos(sb, b->pos);
		if (b->founded) {
			color = MAP_GetColor(b->pos, MAPTYPE_TERRAIN);
			b->mapZone = MAP_GetTerrainType(color);
		}
		b->founded = MSG_ReadByte(sb);
		b->hasHangar = MSG_ReadByte(sb);
		b->hasLab = MSG_ReadByte(sb);
		b->hasHospital = MSG_ReadByte(sb);
		b->hasMissile = MSG_ReadByte(sb);
		b->hasLaser = MSG_ReadByte(sb);
		b->hasAlienCont = MSG_ReadByte(sb);
		b->hasStorage = MSG_ReadByte(sb);
		b->hasQuarters = MSG_ReadByte(sb);
		b->hasWorkshop = MSG_ReadByte(sb);
		b->hasHangarSmall = MSG_ReadByte(sb);
		b->hasUFOHangar = MSG_ReadByte(sb);
		b->hasUFOHangarSmall = MSG_ReadByte(sb);
		b->hasPower = MSG_ReadByte(sb);
		b->hasCommand = MSG_ReadByte(sb);
		b->hasAmStorage = MSG_ReadByte(sb);
		for (k = 0; k < presaveArray[PRE_BASESI]; k++)
			for (l = 0; l < presaveArray[PRE_BASESI]; l++) {
				b->map[k][l] = MSG_ReadShort(sb);
				b->posX[k][l] = MSG_ReadShort(sb);
				b->posY[k][l] = MSG_ReadShort(sb);
			}
		for (k = 0; k < presaveArray[PRE_MAXBUI]; k++) {
			building = &gd.buildings[i][k];
			*building = gd.buildingTypes[MSG_ReadByte(sb)];
			building->idx = k;
			building->base_idx = i;
			building->buildingStatus = MSG_ReadByte(sb);
			building->timeStart = MSG_ReadLong(sb);
			building->buildTime = MSG_ReadLong(sb);
			MSG_Read2Pos(sb, building->pos);
		}
		gd.numBuildings[i] = MSG_ReadShort(sb);
		b->baseStatus = MSG_ReadByte(sb);
		b->isDiscovered = MSG_ReadByte(sb);
		BDEF_InitialiseBaseSlots(b);

		/* read missile battery slots */
		b->maxBatteries = MSG_ReadByte(sb);
		B_LoadItemSlots(b, b->batteries, b->maxBatteries, b->targetMissileIdx, sb);

		/* read laser battery slots */
		b->maxLasers = MSG_ReadByte(sb);
		B_LoadItemSlots(b, b->lasers, b->maxLasers, b->targetLaserIdx, sb);

		b->aircraftCurrent = MSG_ReadShort(sb);
		b->numAircraftInBase = MSG_ReadByte(sb);
		for (k = 0; k < b->numAircraftInBase; k++) {
			aircraft = AIR_GetAircraft(MSG_ReadString(sb));
			if (!aircraft)
				return qfalse;
			b->aircraft[k] = *aircraft;
			aircraft = &b->aircraft[k];
			aircraft->idx = MSG_ReadShort(sb);
			aircraft->homebase = b;
			/* this is the aircraft array id in current base */
			aircraft->idxInBase = k;
			aircraft->status = MSG_ReadByte(sb);
			aircraft->fuel = MSG_ReadLong(sb);
			MSG_ReadPos(sb, aircraft->pos);
			aircraft->time = MSG_ReadShort(sb);
			aircraft->point = MSG_ReadShort(sb);
			aircraft->hangar = MSG_ReadByte(sb);
			/* load aircraft target */
			amount = MSG_ReadShort(sb);
			if (amount == -1)
				aircraft->aircraftTarget = NULL;
			else
				aircraft->aircraftTarget = gd.ufos + amount;
			/* read weapon slot */
			amount = MSG_ReadByte(sb);
			/* only read aircraft->maxWeapons here - skip the rest in the following loop */
			B_LoadItemSlots(b, aircraft->weapons, min(amount, aircraft->maxWeapons), NULL, sb);
			/* just in case there are less slots in new game that in saved one */
			for (l = aircraft->maxWeapons; l < amount; l++) {
				MSG_ReadString(sb);
				MSG_ReadShort(sb);
				MSG_ReadShort(sb);
				MSG_ReadShort(sb);
				MSG_ReadString(sb);
			}
			/* check for shield slot */
			/* there is only one shield - but who knows - breaking the savegames if this changes
			 * isn't worth it */
			amount = MSG_ReadByte(sb);
			if (amount) {
				tech = RS_GetTechByProvided(MSG_ReadString(sb));
				if (tech)
					AII_AddItemToSlot(b, tech, &aircraft->shield);
				aircraft->shield.installationTime = MSG_ReadShort(sb);
			}
			/* read electronics slot */
			amount = MSG_ReadByte(sb);
			for (l = 0; l < amount; l++) {
				/* check that there are enough slots in this aircraft */
				if (l < aircraft->maxElectronics) {
					tech = RS_GetTechByProvided(MSG_ReadString(sb));
					if (tech)
						AII_AddItemToSlot(b, tech, aircraft->electronics + l);
					aircraft->electronics[l].installationTime = MSG_ReadShort(sb);
				} else {
					MSG_ReadString(sb);
					MSG_ReadShort(sb);
				}
			}

			/* Load team on board */
			/** @note presaveArray[PRE_ACTTEA]==MAX_ACTIVETEAM and NOT teamSize */
			aircraft->teamSize = 0;
			for (l = 0; l < presaveArray[PRE_ACTTEA]; l++) {
				aircraft->teamIdxs[l] = MSG_ReadShort(sb);
				if (aircraft->teamIdxs[l] >= 0)
					aircraft->teamSize++;
			}
			for (l = 0; l < presaveArray[PRE_ACTTEA]; l++)
				aircraft->teamTypes[l] = MSG_ReadShort(sb);

			aircraft->numUpgrades = MSG_ReadShort(sb);
			aircraft->radar.range = MSG_ReadShort(sb);
			aircraft->route.numPoints = MSG_ReadShort(sb);
			aircraft->route.distance = MSG_ReadFloat(sb);
			for (l = 0; l < aircraft->route.numPoints; l++)
				MSG_Read2Pos(sb, aircraft->route.point[l]);
			/* Load only needed if aircraft returns from a mission. */
			aircraft->alientypes = MSG_ReadShort(sb);
			aircraft->itemtypes = MSG_ReadShort(sb);
			switch (aircraft->status) {
			case AIR_RETURNING:
				/* aliencargo */
				for (l = 0; l < aircraft->alientypes; l++) {
					Q_strncpyz(aircraft->aliencargo[l].alientype, MSG_ReadString(sb), sizeof(aircraft->aliencargo[l].alientype));
					aircraft->aliencargo[l].amount_alive = MSG_ReadShort(sb);
					aircraft->aliencargo[l].amount_dead = MSG_ReadShort(sb);
				}
				/* itemcargo */
				for (l = 0; l < aircraft->itemtypes; l++) {
					s = MSG_ReadString(sb);
					m = INVSH_GetItemByID(s);
					if (m == -1 || m >= MAX_OBJDEFS) {
						Com_Printf("B_Load: Could not find item '%s'\n", s);
						MSG_ReadShort(sb);
					} else {
						aircraft->itemcargo[l].idx = m;
						aircraft->itemcargo[l].amount = MSG_ReadShort(sb);
					}
				}
				break;
			case AIR_MISSION:
				aircraft->missionID = Mem_PoolStrDup(MSG_ReadString(sb), cl_localPool, 0);
			}
			MSG_ReadPos(sb, aircraft->direction);
			for (l = 0; l < presaveArray[PRE_AIRSTA]; l++)
				aircraft->stats[l] = MSG_ReadLong(sb);
		}

		b->equipType = MSG_ReadByte(sb);

		/* read equipment */
		for (k = 0; k < presaveArray[PRE_NUMODS]; k++) {
			s = MSG_ReadString(sb);
			l = INVSH_GetItemByID(s);
			if (l == -1 || l >= MAX_OBJDEFS) {
				Com_Printf("B_Load: Could not find item '%s'\n", s);
				MSG_ReadShort(sb);
				MSG_ReadByte(sb);
			} else {
				b->storage.num[l] = MSG_ReadShort(sb);
				b->storage.num_loose[l] = MSG_ReadByte(sb);
			}
		}

		b->radar.range = MSG_ReadShort(sb);

		/* Alien Containment. */
		AL_FillInContainment(b);	/* Fill Alien Containment with default values. */
		for (k = 0; k < presaveArray[PRE_NUMALI]; k++) {
			s = MSG_ReadString(sb);
			for (l = 0; l < csi.numTeamDefs; l++) {
				if (!csi.teamDef[l].alien)
					continue;
				if (!Q_strncmp(s, csi.teamDef[l].name, MAX_VAR))
					break;
			}
			if (l == csi.numTeamDefs) {
				Com_Printf("B_Load: Could not find teamDef: %s - skipping the aliencont (%i)\n", s, csi.numTeamDefs);
				MSG_ReadShort(sb);
				MSG_ReadShort(sb);
			} else {
				b->alienscont[k].amount_alive = MSG_ReadShort(sb);
				b->alienscont[k].amount_dead = MSG_ReadShort(sb);
			}
		}

		/* Base capacities. */
		for (k = 0; k < presaveArray[PRE_MAXCAP]; k++) {
			b->capacities[k].cur = MSG_ReadShort(sb);
			b->capacities[k].max = MSG_ReadShort(sb);
		}

		/* Buy/Sell factors. */
		b->buyfactor = MSG_ReadByte(sb);
		b->sellfactor = MSG_ReadByte(sb);

		/* Hospital stuff. */
		for (k = 0; k < presaveArray[PRE_EMPTYP]; k++) {
			b->hospitalListCount[k] = MSG_ReadShort(sb);
			for (l = 0; l < presaveArray[PRE_MAXEMP]; l++) {
				b->hospitalList[k][l] = MSG_ReadShort(sb);
			}
		}
		b->hospitalMissionListCount = MSG_ReadShort(sb);
		for (k = 0; k < presaveArray[PRE_MAXEMP]; k++) {
			b->hospitalMissionList[k] = MSG_ReadShort(sb);
		}

		/* clear the mess of stray loaded pointers */
		memset(&b->equipByBuyType, 0, sizeof(inventory_t));

		/* some functions needs the baseCurrent pointer set */
		baseCurrent = b;
	}
	gd.numBases = bases;

	return qtrue;
}

/**
 * @brief Update the storage amount and the capacities for the storages in the base
 * @param[in] base The base which storage and capacity should be updated
 * @param[in] objIDX The idx in csi.ods
 * @param[in] amount Amount to be added to removed
 * @param[in] reset Set this to true (amount is not needed) if you want to reset the
 * storage amount and capacities (e.g. in case of a base ransack)
 * @param[in] ignorecap qtrue if we won't check freespace but will just add items.
 * @sa CL_BaseRansacked
 */
qboolean B_UpdateStorageAndCapacity (base_t* base, int objIDX, int amount, qboolean reset, qboolean ignorecap)
{
	assert(base);
	assert(objIDX < csi.numODs);
	if (reset) {
		base->storage.num[objIDX] = 0;
		base->storage.num_loose[objIDX] = 0; /* FIXME: needed? */
		base->capacities[CAP_ITEMS].cur = 0;
	} else {
		if (ignorecap) {
			base->storage.num[objIDX] += amount;
			if (csi.ods[objIDX].size > 0)
				base->capacities[CAP_ITEMS].cur += (amount * csi.ods[objIDX].size);
			if (base->capacities[CAP_ITEMS].cur < 0)
				base->capacities[CAP_ITEMS].cur = 0;
		} else if (base->capacities[CAP_ITEMS].max - base->capacities[CAP_ITEMS].cur >= (csi.ods[objIDX].size * amount)) {
			base->storage.num[objIDX] += amount;
			if (csi.ods[objIDX].size > 0)
				base->capacities[CAP_ITEMS].cur += (amount * csi.ods[objIDX].size);
			if (base->capacities[CAP_ITEMS].cur < 0)
				base->capacities[CAP_ITEMS].cur = 0;
		} else {
			Com_DPrintf(DEBUG_CLIENT, "B_UpdateStorageAndCapacity: Not enough storage space (item: %i, amount: %i)\n", objIDX, amount);
			return qfalse;
		}
	}

	return qtrue;
}

/**
 * @brief Checks the parsed buildings for errors
 * @return false if there are errors - true otherwise
 */
qboolean B_ScriptSanityCheck (void)
{
	int i, error = 0;
	building_t* b;

	for (i = 0, b = gd.buildingTypes; i < gd.numBuildingTypes; i++, b++) {
		if (!b->mapPart && b->visible) {
			error++;
			Com_Printf("...... no mappart for building '%s' given\n", b->id);
		}
		if (!b->name) {
			error++;
			Com_Printf("...... no name for building '%s' given\n", b->id);
		}
		if (!b->image) {
			error++;
			Com_Printf("...... no image for building '%s' given\n", b->id);
		}
		if (!b->pedia) {
			error++;
			Com_Printf("...... no pedia link for building '%s' given\n", b->id);
		} else if (!RS_GetTechByID(b->pedia)) {
			error++;
			Com_Printf("...... could not get pedia entry tech (%s) for building '%s'\n", b->pedia, b->id);
		}
	}
	if (!error)
		return qtrue;
	else
		return qfalse;
}
