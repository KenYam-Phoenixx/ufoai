/**
 * @file cp_team_callbacks.c
 * @brief Menu related callback functions for the team menu
 */

/*
All original material Copyright (C) 2002-2010 UFO: Alien Invasion.

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

#include "../cl_shared.h"
#include "../cl_team.h"
#include "../ui/ui_main.h"
#include "../ui/node/ui_node_container.h"	/**< ui_inventory */

#include "cp_campaign.h"
#include "cp_team.h"
#include "cp_team_callbacks.h"

linkedList_t *employeeList;	/** @sa E_GetEmployeeByMenuIndex */
int employeesInCurrentList;

/***********************************************************
 * Bindings
 ***********************************************************/

/* cache soldier list */
static int soldierListSize = 0;
static int soldierListPos = 0;

static qboolean CL_UpdateEmployeeList (employeeType_t employeeType, char *nodeTag, int beginIndex, int drawableListSize)
{
	aircraft_t *aircraft;
	linkedList_t *emplList;
	int id;
	base_t *base = B_GetCurrentSelectedBase();
	const int selected = Cvar_GetInteger("cl_selected");

	/* Check if we are allowed to be here.
	 * We are only allowed to be here if we already set up a base. */
	if (!base) {
		Com_Printf("No base set up\n");
		return qfalse;
	}

	aircraft = base->aircraftCurrent;
	if (!aircraft) {
		return qfalse;
	}

	CL_UpdateActorAircraftVar(aircraft, employeeType);

	soldierListSize = drawableListSize;
	soldierListPos = beginIndex;

	/* Populate employeeList */
	employeesInCurrentList = E_GetHiredEmployees(aircraft->homebase, employeeType, &employeeList);
	emplList = employeeList;
	id = 0;
	while (emplList) {
		const employee_t *employee = (employee_t*)emplList->data;
		qboolean alreadyInOtherShip;
		const aircraft_t *otherShip;
		const int guiId = id - beginIndex;
		containerIndex_t container;

		assert(E_IsHired(employee));
		assert(!employee->transfer);

		/* id lower than viewable item */
		if (guiId < 0) {
			emplList = emplList->next;
			id++;
			continue;
		}
		/* id bigger than viewable item */
		if (guiId >= drawableListSize) {
			emplList = emplList->next;
			id++;
			continue;
		}

		/* Set name of the employee. */
		Cvar_Set(va("mn_ename%i", guiId), employee->chr.name);

		/* Search all aircraft except the current one. */
		otherShip = AIR_IsEmployeeInAircraft(employee, NULL);
		alreadyInOtherShip = (otherShip != NULL) && (otherShip != aircraft);

		/* Update status */
		if (alreadyInOtherShip)
			UI_ExecuteConfunc("aircraft_%s_usedelsewhere %i", nodeTag, guiId);
		else if (AIR_IsEmployeeInAircraft(employee, aircraft))
			UI_ExecuteConfunc("aircraft_%s_assigned %i", nodeTag, guiId);
		else
			UI_ExecuteConfunc("aircraft_%s_unassigned %i", nodeTag, guiId);

		/* Check if the employee has something equipped. */
		for (container = 0; container < csi.numIDs; container++) {
			if (!INVDEF(container)->temp && CONTAINER(&employee->chr, container))
				break;
		}
		if (container < csi.numIDs)
			UI_ExecuteConfunc("aircraft_%s_holdsequip %i", nodeTag, guiId);
		else
			UI_ExecuteConfunc("aircraft_%s_holdsnoequip %i", nodeTag, guiId);

		if (selected == id)
			UI_ExecuteConfunc("aircraft_%s_selected %i", nodeTag, guiId);

		emplList = emplList->next;
		id++;
	}

	UI_ExecuteConfunc("aircraft_%s_list_size %i", nodeTag, id);

	for (; id - beginIndex < drawableListSize; id++) {
		const int guiId = id - beginIndex;
		UI_ExecuteConfunc("aircraft_%s_unusedslot %i", nodeTag, guiId);
		Cvar_Set(va("mn_name%i", guiId), "");
	}

	return qtrue;
}

/**
 * @brief Init the teamlist checkboxes
 * @sa CL_UpdateActorAircraftVar
 * @todo Make this function use a temporary list with all list-able employees
 * instead of using ccs.employees[][] directly. See also CL_Select_f->SELECT_MODE_TEAM
 */
static void CL_UpdateSoldierList_f (void)
{
	qboolean result;
	int drawableListSize;
	int beginIndex;

	if (Cmd_Argc() != 3) {
		Com_Printf("Usage: %s <drawableSize> <firstIndex>\n", Cmd_Argv(0));
		return;
	}
	drawableListSize = atoi(Cmd_Argv(1));
	beginIndex = atoi(Cmd_Argv(2));

	result = CL_UpdateEmployeeList(EMPL_SOLDIER, "soldier", beginIndex, drawableListSize);
	if (!result)
		UI_PopWindow(qfalse);
}

/**
 * @brief Init the teamlist checkboxes
 * @sa CL_UpdateActorAircraftVar
 * @todo Make this function use a temporary list with all list-able employees
 * instead of using ccs.employees[][] directly. See also CL_Select_f->SELECT_MODE_TEAM
 */
static void CL_UpdatePilotList_f (void)
{
	qboolean result;
	int drawableListSize;
	int beginIndex;
	if (Cmd_Argc() != 3) {
		Com_Printf("Usage: %s <drawableSize> <firstIndex>\n", Cmd_Argv(0));
		return;
	}
	drawableListSize = atoi(Cmd_Argv(1));
	beginIndex = atoi(Cmd_Argv(2));

	result = CL_UpdateEmployeeList(EMPL_PILOT, "pilot", beginIndex, drawableListSize);
	if (!result)
		UI_PopWindow(qfalse);
}

/**
 * @brief Displays actor equipment and unused items in proper (filter) category.
 * @note This function is called everytime the equipment screen for the team pops up.
 * @todo Do we allow EMPL_ROBOTs to be equipable? Or is simple buying of ammo enough (similar to original UFO/XCOM)?
 * In the first case the EMPL_SOLDIER stuff below needs to be changed.
 */
static void CL_UpdateEquipmentMenuParameters_f (void)
{
	equipDef_t unused;
	int p;
	aircraft_t *aircraft, *aircraftInBase;
	base_t *base = B_GetCurrentSelectedBase();
	size_t size;

	if (!base)
		return;

	aircraft = base->aircraftCurrent;
	if (!aircraft)
		return;

	/* no soldiers are assigned to the current aircraft. */
	if (AIR_GetTeamSize(aircraft) == 0) {
		UI_PopWindow(qfalse);
		return;
	}

	Cvar_ForceSet("cl_selected", "0");

	/** @todo Skip EMPL_ROBOT (i.e. ugvs) for now . */
	p = CL_UpdateActorAircraftVar(aircraft, EMPL_SOLDIER);
	if (p > 0)
		ui_inventory = &chrDisplayList.chr[0]->i;
	else
		ui_inventory = NULL;

	size = MAX_ACTIVETEAM;
	for (; p < size; p++)
		UI_ExecuteConfunc("equipdisable %i", p);

	/* manage inventory */
	unused = aircraft->homebase->storage; /* copied, including arrays inside! */

	/* clean up aircraft crew for upcoming mission */
	CL_CleanTempInventory(aircraft->homebase);

	aircraftInBase = NULL;
	while ((aircraftInBase = AIR_GetNextFromBase(aircraft->homebase, aircraftInBase)) != NULL)
		CL_CleanupAircraftCrew(aircraftInBase, &unused);

	UI_ContainerNodeUpdateEquipment(&aircraft->homebase->bEquipment, &unused);
}

/**
 * @brief Adds or removes a pilot to/from an aircraft.
 * @sa E_EmployeeHire_f
 */
static void CL_AssignPilot_f (void)
{
	employee_t *employee;
	base_t *base = B_GetCurrentSelectedBase();
	aircraft_t *aircraft;
	int relativeId = 0;
	int num;
	const employeeType_t employeeType = EMPL_PILOT;

	/* check syntax */
	if (Cmd_Argc() < 2 || Cmd_Argc() > 3) {
		Com_Printf("Usage: %s <num> <relative_id>\n", Cmd_Argv(0));
		return;
	}

	if (Cmd_Argc() == 3)
		relativeId = atoi(Cmd_Argv(2));

	num = atoi(Cmd_Argv(1)) + relativeId;
	if (num >= E_CountHired(base, employeeType))
		return;

	/* In case we didn't populate the list with E_GenerateHiredEmployeesList before. */
	if (!employeeList)
		return;

	employee = E_GetEmployeeByMenuIndex(num);
	if (!employee)
		Com_Error(ERR_DROP, "CL_AssignPilot_f: No employee at list-pos %i (base: %i)", num, base->idx);

	aircraft = base->aircraftCurrent;
	if (!aircraft)
		return;

	if (aircraft->pilot == NULL) {
		aircraft->pilot = employee;
	} else if (aircraft->pilot == employee) {
		aircraft->pilot = NULL;
	}

	CL_UpdateActorAircraftVar(aircraft, employeeType);

	UI_ExecuteConfunc("aircraft_status_change");
	Cmd_ExecuteString(va("pilot_select %i %i", num - relativeId, relativeId));
}

/**
 * @brief Adds or removes a soldier to/from an aircraft.
 * @sa E_EmployeeHire_f
 */
static void CL_AssignSoldier_f (void)
{
	base_t *base = B_GetCurrentSelectedBase();
	aircraft_t *aircraft;
	int relativeId = 0;
	int num;
	const employeeType_t employeeType = EMPL_SOLDIER;

	/* check syntax */
	if (Cmd_Argc() < 2 || Cmd_Argc() > 3) {
		Com_Printf("Usage: %s <num> <relative_id>\n", Cmd_Argv(0));
		return;
	}

	if (Cmd_Argc() == 3)
		relativeId = atoi(Cmd_Argv(2));

	num = atoi(Cmd_Argv(1)) + relativeId;
	if (num >= E_CountHired(base, employeeType))
		return;

	/* In case we didn't populate the list with E_GenerateHiredEmployeesList before. */
	if (!employeeList)
		return;

	aircraft = base->aircraftCurrent;
	if (!aircraft)
		return;

	AIM_AddEmployeeFromMenu(aircraft, num);
	CL_UpdateActorAircraftVar(aircraft, employeeType);

	UI_ExecuteConfunc("aircraft_status_change");
	Cbuf_AddText(va("team_select %i %i\n", num - relativeId, relativeId));
}

static void CL_ActorPilotSelect_f (void)
{
	employee_t *employee;
	character_t *chr;
	int num;
	int relativeId = 0;
	const employeeType_t employeeType = EMPL_PILOT;
	base_t *base = B_GetCurrentSelectedBase();

	if (!base)
		return;

	/* check syntax */
	if (Cmd_Argc() < 2 || Cmd_Argc() > 3) {
		Com_Printf("Usage: %s <num> <relative_id>\n", Cmd_Argv(0));
		return;
	}

	if (Cmd_Argc() == 3)
		relativeId = atoi(Cmd_Argv(2));

	num = atoi(Cmd_Argv(1)) + relativeId;
	if (num >= E_CountHired(base, employeeType)) {
		UI_ExecuteConfunc("reset_character_cvars");
		return;
	}

	employee = E_GetEmployeeByMenuIndex(num);
	if (!employee)
		Com_Error(ERR_DROP, "CL_ActorPilotSelect_f: No employee at list-pos %i (base: %i)", num, base->idx);

	chr = &employee->chr;

	/* now set the cl_selected cvar to the new actor id */
	Cvar_ForceSet("cl_selected", va("%i", num));

	/* set info cvars */
	CL_UpdateCharacterValues(chr, "mn_");
	UI_ExecuteConfunc("update_pilot_list %i %i", soldierListSize, soldierListPos);
}

static void CL_ActorTeamSelect_f (void)
{
	employee_t *employee;
	character_t *chr;
	int num;
	int relativeId = 0;
	base_t *base = B_GetCurrentSelectedBase();

	if (!base)
		return;

	/* check syntax */
	if (Cmd_Argc() < 2 || Cmd_Argc() > 3) {
		Com_Printf("Usage: %s <num> <relative_id>\n", Cmd_Argv(0));
		return;
	}

	if (Cmd_Argc() == 3)
		relativeId = atoi(Cmd_Argv(2));

	num = atoi(Cmd_Argv(1)) + relativeId;
	if (num >= E_CountHired(base, EMPL_SOLDIER)) {
		UI_ExecuteConfunc("reset_character_cvars");
		return;
	}

	employee = E_GetEmployeeByMenuIndex(num);
	if (!employee)
		Com_Error(ERR_DROP, "CL_ActorTeamSelect_f: No employee at list-pos %i (base: %i)", num, base->idx);

	chr = &employee->chr;

	/* now set the cl_selected cvar to the new actor id */
	Cvar_ForceSet("cl_selected", va("%i", num));

	/* set info cvars */
	CL_UpdateCharacterValues(chr, "mn_");
	UI_ExecuteConfunc("update_soldier_list %i %i", soldierListSize, soldierListPos);
}

#ifdef DEBUG
static void CL_TeamListDebug_f (void)
{
	base_t *base;
	aircraft_t *aircraft;
	linkedList_t *l;

	aircraft = ccs.missionAircraft;
	base = aircraft->homebase;

	if (!base) {
		Com_Printf("Build and select a base first\n");
		return;
	}

	if (!aircraft) {
		Com_Printf("Buy/build an aircraft first.\n");
		return;
	}

	Com_Printf("%i members in the current team", AIR_GetTeamSize(aircraft));
	for (l = aircraft->acTeam; l != NULL; l = l->next) {
		const employee_t *employee = (const employee_t *)l->data;
		const character_t *chr = &employee->chr;
		Com_Printf("ucn %i - employee->idx: %i\n", chr->ucn, employee->idx);
	}
}
#endif

void CP_TEAM_InitCallbacks (void)
{
	Cmd_AddCommand("team_updateequip", CL_UpdateEquipmentMenuParameters_f, NULL);
	Cmd_AddCommand("update_soldier_list", CL_UpdateSoldierList_f, NULL);
	Cmd_AddCommand("update_pilot_list", CL_UpdatePilotList_f, NULL);

	Cmd_AddCommand("team_hire", CL_AssignSoldier_f, "Add/remove already hired actor to the aircraft");
	Cmd_AddCommand("pilot_hire", CL_AssignPilot_f, "Add/remove already hired pilot to an aircraft");
	Cmd_AddCommand("team_select", CL_ActorTeamSelect_f, "Select a soldier in the team creation menu");
	Cmd_AddCommand("pilot_select", CL_ActorPilotSelect_f, "Select a pilot in the team creation menu");
#ifdef DEBUG
	Cmd_AddCommand("debug_teamlist", CL_TeamListDebug_f, "Debug function to show all hired and assigned teammembers");
#endif

	soldierListPos = 0;
	soldierListSize = 0;
}

void CP_TEAM_ShutdownCallbacks (void)
{
	Cmd_RemoveCommand("team_updateequip");
	Cmd_RemoveCommand("update_soldier_list");
	Cmd_RemoveCommand("update_pilot_list");
	Cmd_RemoveCommand("team_hire");
	Cmd_RemoveCommand("pilot_hire");
	Cmd_RemoveCommand("team_select");
	Cmd_RemoveCommand("pilot_select");
#ifdef DEBUG
	Cmd_RemoveCommand("debug_teamlist");
#endif
}
