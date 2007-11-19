/**
 * @file cl_basesummary.c
 * @brief Deals with the Base Summary report.
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

static int BaseSummary_AircraftCount (aircraftType_t aircraftType)
{
	int i, count = 0;
	for (i = 0; i < baseCurrent->numAircraftInBase; i++) {
		if (baseCurrent->aircraft[i].type == aircraftType)
			count++;
	}

	return count;
}

static void BaseSummary_BuildingConstruction (const char* cvarNameBase)
{
	int i, cvarIndex, daysLeft;
	char buffer[128], cvarName[128];
	building_t building;

	cvarIndex = 0;

	memset(buffer, 0, sizeof(buffer));

	/* Reset all construction-related cvars: */
	for (i = 0; i < BASESUMMARY_MAX_CVARS_CONSTRUCTION; i++) {
		Com_sprintf(cvarName, sizeof(cvarName), "%s%d", cvarNameBase, i);
		Cvar_Set(cvarName, buffer);
	}

	for (i = 0; i < gd.numBuildings[baseCurrent->idx]; i++) {
		building = gd.buildings[baseCurrent->idx][i];

		daysLeft = building.timeStart + building.buildTime - ccs.date.day;

		if (building.buildingStatus == B_STATUS_UNDER_CONSTRUCTION && daysLeft > 0) {
			Com_sprintf(buffer, sizeof(buffer), _("%s - %i %s\n"),
				_(building.name), daysLeft, ngettext("day", "days", daysLeft));

			Com_sprintf(cvarName, sizeof(cvarName), "%s%d", cvarNameBase, cvarIndex++);
			Cvar_Set(cvarName, buffer);
		}

		if (cvarIndex == BASESUMMARY_MAX_CVARS_CONSTRUCTION)
			break;
	}
}

static void BaseSummary_BuildingCount (const char* cvarName, const char* desc, buildingType_t buildingType)
{
	char buffer[128];
	int num = B_GetNumberOfBuildingsInBaseByType(baseCurrent->idx, buildingType);

	Com_sprintf(buffer, sizeof(buffer), _("%s: %i"),
		desc,
		(num >= 0) ? num : 0);	/* Use 0 if an error occured. */

	Cvar_Set(cvarName, buffer);
}

static void BaseSummary_BuildingUsage (const char* cvarName, const char* desc, baseCapacities_t baseCapacity, buildingType_t buildingType)
{
	char buffer[128];
	int num = B_GetNumberOfBuildingsInBaseByType(baseCurrent->idx, buildingType);

	Com_sprintf(buffer, sizeof(buffer), _("%s (%i): %i/%i"),
		desc,
		(num >= 0) ? num : 0,	/* Use 0 if an error occured. */
		baseCurrent->capacities[baseCapacity].cur,
		baseCurrent->capacities[baseCapacity].max);

	Cvar_Set(cvarName, buffer);
}

/**
 * @brief Sum up all hired employees of a given type.
 * @param[in] employeeType The type of employees to count.
 * @return The number of employees.
 * @sa BaseSummary_EmployeeTotal
 * @sa E_CountHired
 */
static int BaseSummary_EmployeeCount (employeeType_t employeeType)
{
	return E_CountHired(baseCurrent,employeeType);
}

/**
 * @brief Sum up all hired employees.
 * @return The number of employees in all bases.
 * @sa BaseSummary_EmployeeCount
 * @sa E_CountHired
 */
static int BaseSummary_EmployeeTotal (void)
{
	int cnt = 0;
	int baseIdx;
	employeeType_t type;

	for (baseIdx = 0; baseIdx < gd.numBases; baseIdx++) {
		for (type = EMPL_SOLDIER; type < MAX_EMPL; type++) {
			cnt += E_CountHired(gd.bases + baseIdx, type);
		}
	}

	return cnt;
}

static void BaseSummary_ProductionCurrent (const char* cvarName)
{
	production_queue_t queue;
	production_t production;
	objDef_t objDef;
	char buffer[128];

	memset(buffer, 0, sizeof(buffer));
	Cvar_Set("mn_basesummary_productioncurrent", buffer);

	queue = gd.productions[baseCurrent->idx];
	if (queue.numItems > 0) {
		production=queue.items[0];

		objDef = csi.ods[production.objID];

		Com_sprintf(buffer, sizeof(buffer), _("%s:  Qty: %d, Already produced: %f %%"),
			objDef.name, production.amount, production.percentDone);

		Cvar_Set("mn_basesummary_productioncurrent", buffer);
	}
}

static void BaseSummary_ResearchCurrent (const char* cvarNameBase)
{
	int i,cvarIndex;
	char buffer[128], cvarName[128];
	technology_t tech;

	cvarIndex = 0;

	memset(buffer, 0, sizeof(buffer));

	/* Reset all research-related cvars: */
	for (i = 0; i < BASESUMMARY_MAX_CVARS_RESEARCH; i++) {
		Com_sprintf(cvarName, sizeof(cvarName), "%s%d", cvarNameBase, i);
		Cvar_Set(cvarName, buffer);
	}

	for (i = 0; i < gd.numTechnologies; i++) {
		tech = gd.technologies[i];
		if (tech.base_idx == baseCurrent->idx && (tech.statusResearch == RS_RUNNING ||
			tech.statusResearch == RS_PAUSED)) {
			Com_sprintf(buffer, sizeof(buffer), _("%s - %1.2f%% (%d %s)\n"),
				tech.name, (1 - tech.time / tech.overalltime) * 100,
				tech.scientists, ngettext("scientist", "scientists", tech.scientists));

			Com_sprintf(cvarName, sizeof(cvarName), "%s%d", cvarNameBase, cvarIndex++);
			Cvar_Set(cvarName, buffer);
		}

		if (cvarIndex == BASESUMMARY_MAX_CVARS_RESEARCH)
			break;
	}
}

/**
 * @brief Check if there is a next base.
 * @sa BaseSummary_PrevBase
 * @sa BaseSummary_PrevBase_f
 * @sa BaseSummary_NextBase_f
 * @return qtrue if there is a next base.
 */
static qboolean BaseSummary_NextBase (void)
{
	int i, baseID;
	qboolean found = qfalse;

	if (!baseCurrent)
		return qfalse;

	baseID = baseCurrent->idx;

	i = baseID + 1;
	while (i != baseCurrent->idx) {
		if (i > gd.numBases - 1)
			i = 0;

		if (gd.bases[i].founded) {
			found = qtrue;
			break;
		}

		i++;
	}

	if (i == baseCurrent->idx)
		return qfalse;
	else
		return found;
}

/**
 * @brief Open menu for next base.
 * @sa BaseSummary_NextBase
 * @sa BaseSummary_PrevBase
 * @sa BaseSummary_PrevBase_f
 */
static void BaseSummary_NextBase_f (void)
{
	int i, baseID;

	/* Can be called from everywhere. */
	if (!baseCurrent || !curCampaign)
		return;

	baseID = baseCurrent->idx;

	i = baseID + 1;
	while (i != baseCurrent->idx) {
		if (i > gd.numBases - 1)
		i = 0;

		if (gd.bases[i].founded) {
			Cbuf_AddText(va("mn_pop;mn_select_base %i;mn_push basesummary\n", i));
			break;
		}

		i++;
	}
}

/**
 * @brief Check if there is working previous base.
 * @sa BaseSummary_PrevBase_f
 * @sa BaseSummary_NextBase
 * @sa BaseSummary_NextBase_f
 * @return qtrue if there is working previous base.
 */
static qboolean BaseSummary_PrevBase (void)
{
	int i, baseID;
	qboolean found = qfalse;

	if (!baseCurrent)
		return qfalse;

	baseID = baseCurrent->idx;

	i = baseID - 1;
	while (i != baseCurrent->idx) {
		if (i < 0)
			i = gd.numBases - 1;

		if (gd.bases[i].founded) {
			found = qtrue;
			break;
		}

		i--;
	}

	if (i == baseCurrent->idx)
		return qfalse;
	else
		return found;
}

/**
 * @brief Open menu for previous base.
 * @sa BaseSummary_PrevBase
 * @sa BaseSummary_NextBase
 * @sa BaseSummary_NextBase_f
 */
static void BaseSummary_PrevBase_f (void)
{
	int i, baseID;

	/* Can be called from everywhere. */
	if (!baseCurrent ||!curCampaign)
		return;

	baseID = baseCurrent->idx;

	i = baseID - 1;
	while (i != baseCurrent->idx) {
		if (i < 0)
			i = gd.numBases-1;

		if (gd.bases[i].founded) {
			Cbuf_AddText(va("mn_pop;mn_select_base %i;mn_push basesummary\n", i));
			break;
		}

		i--;
	}
}

/**
 * @brief Base Summary menu init function.
 * @note Command to call this: basesummary_init
 * @note Should be called whenever the Base Summary menu gets active.
 */
static void BaseSummary_Init (void)
{
	if (!baseCurrent) {
		Com_Printf("No base selected\n");
		return;
	} else {
		/* State of next and prev AC buttons. */
		if (BaseSummary_NextBase())
			Cbuf_AddText("basesummary_nextbb\n");
		else
			Cbuf_AddText("basesummary_nextbg\n");
		if (BaseSummary_PrevBase())
			Cbuf_AddText("basesummary_prevbb\n");
		else
			Cbuf_AddText("basesummary_prevbg\n");

		/* Current base: */
		Cvar_Set("mn_basesummary_name", baseCurrent->name);

		Cvar_SetValue("mn_basesummary_interceptorcount", BaseSummary_AircraftCount(AIRCRAFT_INTERCEPTOR));
		Cvar_SetValue("mn_basesummary_transportercount", BaseSummary_AircraftCount(AIRCRAFT_TRANSPORTER));

		Cvar_SetValue("mn_basesummary_soldiercount", BaseSummary_EmployeeCount(EMPL_SOLDIER));
		Cvar_SetValue("mn_basesummary_scientistcount", BaseSummary_EmployeeCount(EMPL_SCIENTIST));
		Cvar_SetValue("mn_basesummary_workercount", BaseSummary_EmployeeCount(EMPL_WORKER));
		Cvar_SetValue("mn_basesummary_mediccount", BaseSummary_EmployeeCount(EMPL_MEDIC));
		Cvar_SetValue("mn_basesummary_robotcount", BaseSummary_EmployeeCount(EMPL_ROBOT));

		BaseSummary_BuildingUsage("mn_basesummary_capacity_aliens", _("Alien Containment"), CAP_ALIENS, B_ALIEN_CONTAINMENT);
		BaseSummary_BuildingUsage("mn_basesummary_capacity_hangarsmall", _("Small Hangar"), CAP_AIRCRAFTS_SMALL, B_SMALL_HANGAR);
		BaseSummary_BuildingUsage("mn_basesummary_capacity_hangarlarge", _("Large Hangar"), CAP_AIRCRAFTS_BIG, B_HANGAR);
		BaseSummary_BuildingUsage("mn_basesummary_capacity_hangarufo", _("Large UFO Hangar"), CAP_UFOHANGARS, B_UFO_HANGAR);
		/** @todo UFO Small Hangar? */
		BaseSummary_BuildingUsage("mn_basesummary_capacity_hospital", _("Hospital"), CAP_HOSPSPACE, B_HOSPITAL);
		BaseSummary_BuildingUsage("mn_basesummary_capacity_labs", _("Laboratory"), CAP_LABSPACE, B_LAB);
		BaseSummary_BuildingUsage("mn_basesummary_capacity_quarters", _("Living Quarters"), CAP_EMPLOYEES, B_QUARTERS);
		BaseSummary_BuildingUsage("mn_basesummary_capacity_workshops", _("Workshop"), CAP_WORKSPACE, B_WORKSHOP);
		BaseSummary_BuildingUsage("mn_basesummary_capacity_storage", _("Storage"), CAP_ITEMS, B_STORAGE);

		BaseSummary_BuildingCount("mn_basesummary_buildingcount_misc", _("Miscellaneous"), B_MISC);
		BaseSummary_BuildingCount("mn_basesummary_buildingcount_lab", _("Laboratory"), B_LAB);
		BaseSummary_BuildingCount("mn_basesummary_buildingcount_quarters", _("Living Quarters"), B_QUARTERS);
		BaseSummary_BuildingCount("mn_basesummary_buildingcount_storage", _("Storage"), B_STORAGE);
		BaseSummary_BuildingCount("mn_basesummary_buildingcount_workshop", _("Workshop"), B_WORKSHOP);
		BaseSummary_BuildingCount("mn_basesummary_buildingcount_hospital", _("Hospital"), B_HOSPITAL);
		BaseSummary_BuildingCount("mn_basesummary_buildingcount_hangarlarge", _("Large Hangar"), B_HANGAR);
		BaseSummary_BuildingCount("mn_basesummary_buildingcount_aliens", _("Alien Containment"), B_ALIEN_CONTAINMENT);
		BaseSummary_BuildingCount("mn_basesummary_buildingcount_hangarsmall", _("Small Hangar"), B_SMALL_HANGAR);
		BaseSummary_BuildingCount("mn_basesummary_buildingcount_hangarlargeufo", _("Large UFO Hangar"), B_UFO_HANGAR);
		BaseSummary_BuildingCount("mn_basesummary_buildingcount_hangarsmallufo", _("Small UFO Hangar"), B_UFO_SMALL_HANGAR);
		BaseSummary_BuildingCount("mn_basesummary_buildingcount_power", _("Power Plant"), B_POWER);
		BaseSummary_BuildingCount("mn_basesummary_buildingcount_command", _("Command Center"), B_COMMAND);
		BaseSummary_BuildingCount("mn_basesummary_buildingcount_antimatter", _("Antimatter Storage"), B_ANTIMATTER);

		BaseSummary_BuildingConstruction("mn_basesummary_construction");
		BaseSummary_ProductionCurrent("mn_basesummary_productioncurrent");
		BaseSummary_ResearchCurrent("mn_basesummary_researchcurrent");

		/* Totals: */
		Cvar_SetValue("mn_basesummary_totals_basecount", gd.numBases);
		Cvar_SetValue("mn_basesummary_totals_aircraft",gd.numAircraft);
		Cvar_SetValue("mn_basesummary_totals_personnel",BaseSummary_EmployeeTotal());
	}
}

/**
 * @brief Defines commands and cvars for the base statistics menu(s).
 * @sa MN_ResetMenus
 */
void BaseSummary_Reset (void)
{
	/* add commands */
	Cmd_AddCommand("basesummary_init", BaseSummary_Init, "Init function for Base Statistics menu");
	Cmd_AddCommand("basesummary_nextbase", BaseSummary_NextBase_f, "Opens Base Statistics menu in next base");
	Cmd_AddCommand("basesummary_prevbase", BaseSummary_PrevBase_f, "Opens Base Statistics menu in previous base");

	baseCurrent = NULL;
}
