/**
 * @file cp_transfer.c
 * @brief Deals with the Transfer stuff.
 * @note Transfer menu functions prefix: TR_
 * @todo Remove direct access to nodes
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

#include "../cl_shared.h"
#include "cp_campaign.h"
#include "save/save_transfer.h"
#include "cp_transfer_callbacks.h"
#include "../ui/ui_main.h"

/**
 * @brief Iterates through transfers
 * @param[in] lastTransfer Pointer of the transfer to iterate from. call with NULL to get the first one.
 */
transfer_t* TR_GetNext (transfer_t *lastTransfer)
{
	transfer_t *endOfTransfers = &ccs.transfers[ccs.numTransfers];
	transfer_t* transfer;

	if (!ccs.numTransfers)
		return NULL;

	if (!lastTransfer)
		return ccs.transfers;
	assert(lastTransfer >= ccs.transfers);
	assert(lastTransfer < endOfTransfers);

	transfer = lastTransfer;

	transfer++;
	if (transfer >= endOfTransfers)
		return NULL;
	else
		return transfer;
}

/**
 * @brief Unloads transfer cargo when finishing the transfer or destroys it when no buildings/base.
 * @param[in,out] destination The destination base - might be NULL in case the base
 * is already destroyed
 * @param[in] transfer Pointer to transfer in ccs.transfers.
 * @param[in] success True if the transfer reaches dest base, false if the base got destroyed.
 * @sa TR_TransferEnd
 */
static void TR_EmptyTransferCargo (base_t *destination, transfer_t *transfer, qboolean success)
{
	int i, j;

	assert(transfer);

	if (transfer->hasItems && success) {	/* Items. */
		if (!B_GetBuildingStatus(destination, B_STORAGE)) {
			Com_sprintf(cp_messageBuffer, sizeof(cp_messageBuffer), _("%s does not have Storage, items are removed!"), destination->name);
			MSO_CheckAddNewMessage(NT_TRANSFER_LOST, _("Transport mission"), cp_messageBuffer, qfalse, MSG_TRANSFERFINISHED, NULL);
			/* Items cargo is not unloaded, will be destroyed in TR_TransferCheck(). */
		} else {
			for (i = 0; i < csi.numODs; i++) {
				const objDef_t *od = INVSH_GetItemByIDX(i);
				if (transfer->itemAmount[od->idx] > 0) {
					if (!strcmp(od->id, ANTIMATTER_TECH_ID))
						B_ManageAntimatter(destination, transfer->itemAmount[od->idx], qtrue);
					else
						B_UpdateStorageAndCapacity(destination, od, transfer->itemAmount[od->idx], qfalse, qtrue);
				}
			}
		}
	}

	if (transfer->hasEmployees && transfer->srcBase) {	/* Employees. (cannot come from a mission) */
		if (!success || !B_GetBuildingStatus(destination, B_QUARTERS)) {	/* Employees will be unhired. */
			if (success) {
				Com_sprintf(cp_messageBuffer, sizeof(cp_messageBuffer), _("%s does not have Living Quarters, employees got unhired!"), destination->name);
				MSO_CheckAddNewMessage(NT_TRANSFER_LOST, _("Transport mission"), cp_messageBuffer, qfalse, MSG_TRANSFERFINISHED, NULL);
			}
			for (i = 0; i < MAX_EMPL; i++) {
				for (j = 0; j < ccs.numEmployees[i]; j++) {
					if (transfer->employeeArray[i][j]) {
						employee_t *employee = transfer->employeeArray[i][j];
						employee->baseHired = transfer->srcBase;	/* Restore back the original baseid. */
						/* every employee that we have transfered should have a
						 * base he is hire in - otherwise we couldn't have transfered him */
						assert(employee->baseHired);
						employee->transfer = qfalse;
						E_UnhireEmployee(employee);
					}
				}
			}
		} else {
			for (i = 0; i < MAX_EMPL; i++) {
				for (j = 0; j < ccs.numEmployees[i]; j++) {
					if (transfer->employeeArray[i][j]) {
						employee_t *employee = transfer->employeeArray[i][j];
						employee->baseHired = transfer->srcBase;	/* Restore back the original baseid. */
						/* every employee that we have transfered should have a
						 * base he is hire in - otherwise we couldn't have transfered him */
						assert(employee->baseHired);
						employee->transfer = qfalse;
						E_UnhireEmployee(employee);
						E_HireEmployee(destination, employee);
					}
				}
			}
		}
	}

	if (transfer->hasAliens && success) {	/* Aliens. */
		if (!B_GetBuildingStatus(destination, B_ALIEN_CONTAINMENT)) {
			Com_sprintf(cp_messageBuffer, sizeof(cp_messageBuffer), _("%s does not have Alien Containment, Aliens are removed!"), destination->name);
			MSO_CheckAddNewMessage(NT_TRANSFER_LOST, _("Transport mission"), cp_messageBuffer, qfalse, MSG_TRANSFERFINISHED, NULL);
			/* Aliens cargo is not unloaded, will be destroyed in TR_TransferCheck(). */
		} else {
			for (i = 0; i < ccs.numAliensTD; i++) {
				if (transfer->alienAmount[i][TRANS_ALIEN_ALIVE] > 0) {
					AL_ChangeAliveAlienNumber(destination, &(destination->alienscont[i]), transfer->alienAmount[i][TRANS_ALIEN_ALIVE]);
				}
				if (transfer->alienAmount[i][TRANS_ALIEN_DEAD] > 0) {
					destination->alienscont[i].amountDead += transfer->alienAmount[i][TRANS_ALIEN_DEAD];
				}
			}
		}
	}

	/** @todo If source base is destroyed during transfer, aircraft doesn't exist anymore.
	 * aircraftArray should contain pointers to aircraftTemplates to avoid this problem, and be removed from
	 * source base as soon as transfer starts */
	if (transfer->hasAircraft && success && transfer->srcBase) {	/* Aircraft. Cannot come from mission */
		for (i = 0; i < ccs.numAircraft; i++) {
			if (transfer->aircraftArray[i] > TRANS_LIST_EMPTY_SLOT) {
				aircraft_t *aircraft = AIR_AircraftGetFromIDX(i);
				assert(aircraft);

				if (AIR_CalculateHangarStorage(aircraft->tpl, destination, 0) > 0) {
					/* Move aircraft */
					AIR_MoveAircraftIntoNewHomebase(aircraft, destination);
				} else {
					/* No space, aircraft will be lost. */
					Com_sprintf(cp_messageBuffer, sizeof(cp_messageBuffer), _("%s does not have enough free space in hangars. Aircraft is lost!"), destination->name);
					MSO_CheckAddNewMessage(NT_TRANSFER_LOST, _("Transport mission"), cp_messageBuffer, qfalse, MSG_TRANSFERFINISHED, NULL);
					AIR_DeleteAircraft(aircraft);
				}
			}
		}
	}
}

/**
 * @brief Starts alien bodies transfer between mission and base.
 * @param[in] base Pointer to the base to send the alien bodies.
 * @sa TR_TransferBaseListClick_f
 * @sa TR_TransferStart_f
 */
void TR_TransferAlienAfterMissionStart (const base_t *base, aircraft_t *transferAircraft)
{
	int i, j;
	transfer_t *transfer;
	float time;
	char message[256];
	int alienCargoTypes;
	aliensTmp_t *cargo;

	const technology_t *breathingTech;
	qboolean alienBreathing = qfalse;
	const objDef_t *alienBreathingObjDef;

	breathingTech = RS_GetTechByID(BREATHINGAPPARATUS_TECH);
	if (!breathingTech)
		Com_Error(ERR_DROP, "AL_AddAliens: Could not get breathing apparatus tech definition");
	alienBreathing = RS_IsResearched_ptr(breathingTech);
	alienBreathingObjDef = INVSH_GetItemByID(breathingTech->provides);
	if (!alienBreathingObjDef)
		Com_Error(ERR_DROP, "AL_AddAliens: Could not get breathing apparatus item definition");

	if (!base) {
		Com_Printf("TR_TransferAlienAfterMissionStart_f: No base selected!\n");
		return;
	}

	if (ccs.numTransfers >= MAX_TRANSFERS) {
		Com_DPrintf(DEBUG_CLIENT, "TR_TransferAlienAfterMissionStart: Max transfers reached.");
		return;
	}

	transfer = &ccs.transfers[ccs.numTransfers];

	if (transfer->active)
		Com_Error(ERR_DROP, "Transfer idx %i shouldn't be active.", ccs.numTransfers);

	/* Initialize transfer.
	 * calculate time to go from 1 base to another : 1 day for one quarter of the globe*/
	time = GetDistanceOnGlobe(base->pos, transferAircraft->pos) / 90.0f;
	transfer->event.day = ccs.date.day + floor(time);	/* add day */
	time = (time - floor(time)) * SECONDS_PER_DAY;	/* convert remaining time in second */
	transfer->event.sec = ccs.date.sec + round(time);
	/* check if event is not the following day */
	if (transfer->event.sec > SECONDS_PER_DAY) {
		transfer->event.sec -= SECONDS_PER_DAY;
		transfer->event.day++;
	}
	transfer->destBase = B_GetFoundedBaseByIDX(base->idx);	/* Destination base. */
	transfer->srcBase = NULL;	/* Source base. */
	transfer->active = qtrue;
	ccs.numTransfers++;

	alienCargoTypes = AL_GetAircraftAlienCargoTypes(transferAircraft);
	cargo = AL_GetAircraftAlienCargo(transferAircraft);
	for (i = 0; i < alienCargoTypes; i++, cargo++) {		/* Aliens. */
		if (!alienBreathing) {
			cargo->amountDead += cargo->amountAlive;
			cargo->amountAlive = 0;
		}
		if (cargo->amountAlive > 0) {
			for (j = 0; j < ccs.numAliensTD; j++) {
				if (!CHRSH_IsTeamDefAlien(&csi.teamDef[j]))
					continue;
				if (base->alienscont[j].teamDef == cargo->teamDef) {
					transfer->hasAliens = qtrue;
					transfer->alienAmount[j][TRANS_ALIEN_ALIVE] = cargo->amountAlive;
					cargo->amountAlive = 0;
					break;
				}
			}
		}
		if (cargo->amountDead > 0) {
			for (j = 0; j < ccs.numAliensTD; j++) {
				if (!CHRSH_IsTeamDefAlien(&csi.teamDef[j]))
					continue;
				if (base->alienscont[j].teamDef == cargo->teamDef) {
					transfer->hasAliens = qtrue;
					transfer->alienAmount[j][TRANS_ALIEN_DEAD] = cargo->amountDead;

					/* If we transfer aliens from battlefield add also breathing apparatur(s) */
					transfer->hasItems = qtrue;
					transfer->itemAmount[alienBreathingObjDef->idx] += cargo->amountDead;
					cargo->amountDead = 0;
					break;
				}
			}
		}
	}
	AL_SetAircraftAlienCargoTypes(transferAircraft, 0);

	Com_sprintf(message, sizeof(message), _("Transport mission started, cargo is being transported to %s"), transfer->destBase->name);
	MSO_CheckAddNewMessage(NT_TRANSFER_ALIENBODIES_DEFERED, _("Transport mission"), message, qfalse, MSG_TRANSFERFINISHED, NULL);
	UI_PopWindow(qfalse);
}

/**
 * @brief Ends the transfer.
 * @param[in] transfer Pointer to transfer in ccs.transfers
 */
static void TR_TransferEnd (transfer_t *transfer)
{
	base_t* destination = transfer->destBase;
	assert(destination);

	if (!destination->founded) {
		TR_EmptyTransferCargo(NULL, transfer, qfalse);
		MSO_CheckAddNewMessage(NT_TRANSFER_LOST, _("Transport mission"), _("The destination base no longer exists! Transfer carge was lost, personnel has been discharged."), qfalse, MSG_TRANSFERFINISHED, NULL);
		/** @todo what if source base is lost? we won't be able to unhire transfered employees. */
	} else {
		char message[256];
		TR_EmptyTransferCargo(destination, transfer, qtrue);
		Com_sprintf(message, sizeof(message), _("Transport mission ended, unloading cargo in %s"), destination->name);
		MSO_CheckAddNewMessage(NT_TRANSFER_COMPLETED_SUCCESS, _("Transport mission"), message, qfalse, MSG_TRANSFERFINISHED, NULL);
	}
	transfer->active = qfalse;
}

/**
 * @brief Starts a transfer
 * @param[in] srcBase start transfer from this base
 * @param[in] transData Container holds transfer details
 */
void TR_TransferStart (base_t *srcBase, struct transferData_s *transData)
{
	transfer_t *transfer;
	float time;
	int i;
	int j;

	if (!transData->transferBase || !srcBase) {
		Com_Printf("TR_TransferStart_f: No base selected!\n");
		return;
	}

	/* don't start any empty transport */
	if (!transData->trCargoCountTmp) {
		return;
	}

	if (ccs.numTransfers >= MAX_TRANSFERS) {
		Com_DPrintf(DEBUG_CLIENT, "TR_TransferStart_f: Max transfers reached.");
		return;
	}

	transfer = &ccs.transfers[ccs.numTransfers];

	if (transfer->active)
		Com_Error(ERR_DROP, "Transfer idx %i shouldn't be active.", ccs.numTransfers);

	/* Initialize transfer. */
	/* calculate time to go from 1 base to another : 1 day for one quarter of the globe*/
	time = GetDistanceOnGlobe(transData->transferBase->pos, srcBase->pos) / 90.0f;
	transfer->event.day = ccs.date.day + floor(time);	/* add day */
	time = (time - floor(time)) * SECONDS_PER_DAY;	/* convert remaining time in second */
	transfer->event.sec = ccs.date.sec + round(time);
	/* check if event is not the following day */
	if (transfer->event.sec > SECONDS_PER_DAY) {
		transfer->event.sec -= SECONDS_PER_DAY;
		transfer->event.day++;
	}
	transfer->destBase = transData->transferBase;	/* Destination base. */
	assert(transfer->destBase);
	transfer->srcBase = srcBase;	/* Source base. */
	transfer->active = qtrue;
	ccs.numTransfers++;

	for (i = 0; i < csi.numODs; i++) {	/* Items. */
		if (transData->trItemsTmp[i] > 0) {
			transfer->hasItems = qtrue;
			transfer->itemAmount[i] = transData->trItemsTmp[i];
		}
	}
	/* Note that the employee remains hired in source base during the transfer, that is
	 * it takes Living Quarters capacity, etc, but it cannot be used anywhere. */
	for (i = 0; i < MAX_EMPL; i++) {		/* Employees. */
		for (j = 0; j < ccs.numEmployees[i]; j++) {
			if (transData->trEmployeesTmp[i][j]) {
				employee_t *employee = transData->trEmployeesTmp[i][j];
				transfer->hasEmployees = qtrue;

				assert(E_IsInBase(employee, srcBase));

				E_ResetEmployee(employee);
				transfer->employeeArray[i][j] = employee;
				employee->baseHired = NULL;
				employee->transfer = qtrue;
			}
		}
	}
	/** @todo This doesn't work - it would require to store the aliens as the
	 * first entries in the teamDef array - and this is not guaranteed. The
	 * alienAmount array may not contain more than numAliensTD entries though */
	for (i = 0; i < ccs.numAliensTD; i++) {		/* Aliens. */
		if (!CHRSH_IsTeamDefAlien(&csi.teamDef[i]))
			continue;
		if (transData->trAliensTmp[i][TRANS_ALIEN_ALIVE] > 0) {
			transfer->hasAliens = qtrue;
			transfer->alienAmount[i][TRANS_ALIEN_ALIVE] = transData->trAliensTmp[i][TRANS_ALIEN_ALIVE];
		}
		if (transData->trAliensTmp[i][TRANS_ALIEN_DEAD] > 0) {
			transfer->hasAliens = qtrue;
			transfer->alienAmount[i][TRANS_ALIEN_DEAD] = transData->trAliensTmp[i][TRANS_ALIEN_DEAD];
		}
	}
	memset(transfer->aircraftArray, TRANS_LIST_EMPTY_SLOT, sizeof(transfer->aircraftArray));
	for (i = 0; i < ccs.numAircraft; i++) {	/* Aircraft. */
		if (transData->trAircraftsTmp[i] > TRANS_LIST_EMPTY_SLOT) {
			aircraft_t *aircraft = AIR_AircraftGetFromIDX(i);
			aircraft->status = AIR_TRANSFER;
			AIR_RemoveEmployees(aircraft);
			transfer->hasAircraft = qtrue;
			transfer->aircraftArray[i] = i;
		} else {
			transfer->aircraftArray[i] = TRANS_LIST_EMPTY_SLOT;
		}
	}

	/* Recheck if production/research can be done on srcbase (if there are workers/scientists) */
	PR_ProductionAllowed(srcBase);
	RS_ResearchAllowed(srcBase);
}

/**
 * @brief Notify that an aircraft has been removed.
 * @param[in] aircraft Aircraft that was removed from the game
 * @sa AIR_DeleteAircraft
 */
void TR_NotifyAircraftRemoved (const aircraft_t *aircraft)
{
	transfer_t *transfer = NULL;

	assert(aircraft->idx >= 0 && aircraft->idx < MAX_AIRCRAFT);
	while ((transfer = TR_GetNext(transfer))) {
		int tmp = ccs.numAircraft;
		/* skip non active transfer */
		if (!transfer->active)
			continue;
		if (!transfer->hasAircraft)
			continue;
		REMOVE_ELEM_MEMSET(transfer->aircraftArray, aircraft->idx, tmp, TRANS_LIST_EMPTY_SLOT);
	}
}

/**
 * @brief Checks whether given transfer should be processed.
 * @sa CL_CampaignRun
 */
void TR_TransferCheck (void)
{
	transfer_t *transfer = NULL;
	while ((transfer = TR_GetNext(transfer))) {
		if (!transfer->active)
			continue;
		if (transfer->event.day == ccs.date.day && ccs.date.sec >= transfer->event.sec) {
			const ptrdiff_t idx = (ptrdiff_t)(transfer - ccs.transfers);
			assert(transfer->destBase);
			TR_TransferEnd(transfer);
			/** @todo make it TR_Remove() */
			REMOVE_ELEM(ccs.transfers, idx, ccs.numTransfers);
			return;
		}
	}
}

#ifdef DEBUG
/**
 * @brief Lists an/all active transfer(s)
 */
static void TR_ListTransfers_f (void)
{
	int transIdx = -1;
	int i = 0;
	transfer_t *transfer = NULL;

	if (Cmd_Argc() == 2) {
		transIdx = atoi(Cmd_Argv(1));
		if (transIdx < 0 || transIdx > MAX_TRANSFERS) {
			Com_Printf("Usage: %s [transferIDX]\nWithout parameter it lists all.\n", Cmd_Argv(0));
			return;
		}
	}

	if (!ccs.numTransfers)
		Com_Printf("No active transfers.\n");

	for (; (transfer = TR_GetNext(transfer)); i++) {
		dateLong_t date;

		if (transIdx >= 0 && i != transIdx)
			continue;
		if (!transfer->active)
			continue;

		/* @todo: we need a strftime feature to make this easier */
		CL_DateConvertLong(&transfer->event, &date);

		Com_Printf("Transfer #%d\n", i);
		Com_Printf("...From %d (%s) To %d (%s) Arrival: %04i-%02i-%02i %02i:%02i:%02i\n",
			(transfer->srcBase) ? transfer->srcBase->idx : -1,
			(transfer->srcBase) ? transfer->srcBase->name : "(null)",
			(transfer->destBase) ? transfer->destBase->idx : -1,
			(transfer->destBase) ? transfer->destBase->name : "(null)",
			date.year, date.month, date.day, date.hour, date.min, date.sec);

		/* ItemCargo */
		if (transfer->hasItems) {
			int j;
			Com_Printf("...ItemCargo:\n");
			for (j = 0; j < csi.numODs; j++) {
				const objDef_t *od = INVSH_GetItemByIDX(j);
				if (transfer->itemAmount[od->idx])
					Com_Printf("......%s: %i\n", od->id, transfer->itemAmount[od->idx]);
			}
		}
		/* Carried Employees */
		if (transfer->hasEmployees) {
			int j;
			Com_Printf("...Carried Employee:\n");
			for (j = 0; j < MAX_EMPL; j++) {
				int k;
				for (k = 0; k < MAX_EMPLOYEES; k++) {
					const struct employee_s *employee = transfer->employeeArray[j][k];
					if (!employee)
						continue;
					if (employee->ugv) {
						/** @todo: improve ugv listing when they're implemented */
						Com_Printf("......ugv: %s [idx: %i]\n", employee->ugv->id, employee->idx);
					} else {
						Com_Printf("......%s (%s) / %s [idx: %i ucn: %i]\n", employee->chr.name,
							E_GetEmployeeString(employee->type),
							(employee->nation) ? employee->nation->id : "(nonation)",
							employee->idx, employee->chr.ucn);
						if (!E_IsHired(employee))
							Com_Printf("Warning: employee^ not hired!\n");
						if (!employee->transfer)
							Com_Printf("Warning: employee^ not marked as being transfered!\n");
					}
				}
			}
		}
		/* AlienCargo */
		if (transfer->hasAliens) {
			int j;
			Com_Printf("...AlienCargo:\n");
			for (j = 0; j < csi.numTeamDefs; j++) {
				if (transfer->alienAmount[j][TRANS_ALIEN_ALIVE] + transfer->alienAmount[j][TRANS_ALIEN_DEAD])
					Com_Printf("......%s alive: %i dead: %i\n", csi.teamDef[j].id, transfer->alienAmount[j][TRANS_ALIEN_ALIVE], transfer->alienAmount[j][TRANS_ALIEN_DEAD]);
			}
		}
		/* Transfered Aircraft */
		if (transfer->hasAircraft) {
			int j;
			Com_Printf("...Transfered Aircraft:\n");
			for (j = 0; j < ccs.numAircraft; j++) {
				const aircraft_t *aircraft;
				if (transfer->aircraftArray[j] == TRANS_LIST_EMPTY_SLOT)
					continue;
				aircraft = AIR_AircraftGetFromIDX(transfer->aircraftArray[j]);
				Com_Printf("......%s [idx: %i]\n", (aircraft) ? aircraft->id : "(null)", j);
			}
		}

	}
}
#endif

/**
 * @brief Save callback for xml savegames
 * @param[out] p XML Node structure, where we write the information to
 * @sa TR_LoadXML
 * @sa SAV_GameSaveXML
 */
qboolean TR_SaveXML (mxml_node_t *p)
{
	transfer_t *transfer = NULL;
	mxml_node_t *n = mxml_AddNode(p, SAVE_TRANSFER_TRANSFERS);

	while ((transfer = TR_GetNext(transfer))) {
		int j;
		mxml_node_t *s;

		s = mxml_AddNode(n, SAVE_TRANSFER_TRANSFER);
		mxml_AddInt(s, SAVE_TRANSFER_DAY, transfer->event.day);
		mxml_AddInt(s, SAVE_TRANSFER_SEC, transfer->event.sec);
		if (!transfer->destBase) {
			Com_Printf("Could not save transfer, no destBase is set\n");
			return qfalse;
		}
		mxml_AddInt(s, SAVE_TRANSFER_DESTBASE, transfer->destBase->idx);
		/* scrBase can be NULL if this is alien (mission->base) transport 
		 * @sa TR_TransferAlienAfterMissionStart */
		if (transfer->srcBase)
			mxml_AddInt(s, SAVE_TRANSFER_SRCBASE, transfer->srcBase->idx);
		/* save items */
		if (transfer->hasItems) {
			for (j = 0; j < MAX_OBJDEFS; j++) {
				if (transfer->itemAmount[j] > 0) {
					const objDef_t *od = INVSH_GetItemByIDX(j);
					mxml_node_t *ss = mxml_AddNode(s, SAVE_TRANSFER_ITEM);

					assert(od);
					mxml_AddString(ss, SAVE_TRANSFER_ITEMID, od->id);
					mxml_AddInt(ss, SAVE_TRANSFER_AMOUNT, transfer->itemAmount[j]);
				}
			}
		}
		/* save aliens */
		if (transfer->hasAliens) {
			for (j = 0; j < ccs.numAliensTD; j++) {
				if (transfer->alienAmount[j][TRANS_ALIEN_ALIVE] > 0
				 || transfer->alienAmount[j][TRANS_ALIEN_DEAD] > 0)
				{
					teamDef_t *team = ccs.alienTeams[j];
					mxml_node_t *ss = mxml_AddNode(s, SAVE_TRANSFER_ALIEN);

					assert(team);
					mxml_AddString(ss, SAVE_TRANSFER_ALIENID, team->id);
					mxml_AddIntValue(ss, SAVE_TRANSFER_ALIVEAMOUNT, transfer->alienAmount[j][TRANS_ALIEN_ALIVE]);
					mxml_AddIntValue(ss, SAVE_TRANSFER_DEADAMOUNT, transfer->alienAmount[j][TRANS_ALIEN_DEAD]);
				}
			}
		}
		/* save employee */
		if (transfer->hasEmployees) {
			for (j = 0; j < MAX_EMPL; j++) {
				int k;
				for (k = 0; k < MAX_EMPLOYEES; k++) {
					const employee_t *empl = transfer->employeeArray[j][k];
					if (empl) {
						mxml_node_t *ss = mxml_AddNode(s, SAVE_TRANSFER_EMPLOYEE);

						mxml_AddInt(ss, SAVE_TRANSFER_UCN, empl->chr.ucn);
					}
				}
			}
		}
		/* save aircraft */
		if (transfer->hasAircraft) {
			for (j = 0; j < ccs.numAircraft; j++) {
				if (transfer->aircraftArray[j] > TRANS_LIST_EMPTY_SLOT) {
					mxml_node_t *ss = mxml_AddNode(s, SAVE_TRANSFER_AIRCRAFT);
					mxml_AddInt(ss, SAVE_TRANSFER_ID, j);
				}
			}
		}
	}
	return qtrue;
}

/**
 * @brief Load callback for xml savegames
 * @param[in] p XML Node structure, where we get the information from
 * @sa TR_SaveXML
 * @sa SAV_GameLoadXML
 */
qboolean TR_LoadXML (mxml_node_t *p)
{
	mxml_node_t *n, *s;

	n = mxml_GetNode(p, SAVE_TRANSFER_TRANSFERS);
	if (!n)
		return qfalse;

	assert(ccs.numBases);

	for (s = mxml_GetNode(n, SAVE_TRANSFER_TRANSFER); s && ccs.numTransfers < MAX_TRANSFERS; s = mxml_GetNextNode(s, n, SAVE_TRANSFER_TRANSFER)) {
		mxml_node_t *ss;
		transfer_t *transfer = &ccs.transfers[ccs.numTransfers];

		transfer->destBase = B_GetBaseByIDX(mxml_GetInt(s, SAVE_TRANSFER_DESTBASE, BYTES_NONE));
		if (!transfer->destBase) {
			Com_Printf("Error: Transfer has no destBase set\n");
			return qfalse;
		}
		transfer->srcBase = B_GetBaseByIDX(mxml_GetInt(s, SAVE_TRANSFER_SRCBASE, BYTES_NONE));

		transfer->event.day = mxml_GetInt(s, SAVE_TRANSFER_DAY, 0);
		transfer->event.sec = mxml_GetInt(s, SAVE_TRANSFER_SEC, 0);
		transfer->active = qtrue;

		/* Initializing some variables */
		transfer->hasItems = qfalse;
		transfer->hasEmployees = qfalse;
		transfer->hasAliens = qfalse;
		transfer->hasAircraft = qfalse;
		memset(transfer->itemAmount, 0, sizeof(transfer->itemAmount));
		memset(transfer->alienAmount, 0, sizeof(transfer->alienAmount));
		memset(transfer->employeeArray, 0, sizeof(transfer->employeeArray));
		memset(transfer->aircraftArray, TRANS_LIST_EMPTY_SLOT, sizeof(transfer->aircraftArray));

		/* load items */
		/* If there is at last one element, hasItems is true */
		ss = mxml_GetNode(s, SAVE_TRANSFER_ITEM);
		if (ss) {
			transfer->hasItems = qtrue;
			for (; ss; ss = mxml_GetNextNode(ss, s, SAVE_TRANSFER_ITEM)) {
				const char *itemId = mxml_GetString(ss, SAVE_TRANSFER_ITEMID);
				const objDef_t *od = INVSH_GetItemByID(itemId);

				if (od)
					transfer->itemAmount[od->idx] = mxml_GetInt(ss, SAVE_TRANSFER_AMOUNT, 1);
			}
		}
		/* load aliens */
		ss = mxml_GetNode(s, SAVE_TRANSFER_ALIEN);
		if (ss) {
			transfer->hasAliens = qtrue;
			for (; ss; ss = mxml_GetNextNode(ss, s, SAVE_TRANSFER_ALIEN)) {
				const int alive = mxml_GetInt(ss, SAVE_TRANSFER_ALIVEAMOUNT, 0);
				const int dead  = mxml_GetInt(ss, SAVE_TRANSFER_DEADAMOUNT, 0);
				const char *id = mxml_GetString(ss, SAVE_TRANSFER_ALIENID);
				int j;

				/* look for alien teamDef */
				for (j = 0; j < ccs.numAliensTD; j++) {
					if (ccs.alienTeams[j] && !strcmp(id, ccs.alienTeams[j]->id))
						break;
				}

				if (j < ccs.numAliensTD) {
					transfer->alienAmount[j][TRANS_ALIEN_ALIVE] = alive;
					transfer->alienAmount[j][TRANS_ALIEN_DEAD] = dead;
				} else {
					Com_Printf("CL_LoadXML: AlienId '%s' is invalid\n", id);
				}
			}
		}
		/* load employee */
		ss = mxml_GetNode(s, SAVE_TRANSFER_EMPLOYEE);
		if (ss) {
			transfer->hasEmployees = qtrue;
			for (; ss; ss = mxml_GetNextNode(ss, s, SAVE_TRANSFER_EMPLOYEE)) {
				const int ucn = mxml_GetInt(ss, SAVE_TRANSFER_UCN, -1);
				employee_t *empl = E_GetEmployeeFromChrUCN(ucn);

				if (!empl) {
					Com_Printf("Error: No employee found with UCN: %i\n", ucn);
					return qfalse;
				}

				transfer->employeeArray[empl->type][empl->idx] = empl;
				transfer->employeeArray[empl->type][empl->idx]->transfer = qtrue;
			}
		}
		/* load aircraft */
		ss = mxml_GetNode(s, SAVE_TRANSFER_AIRCRAFT);
		if (ss) {
			transfer->hasAircraft = qtrue;
			for (; ss; ss = mxml_GetNextNode(ss, s, SAVE_TRANSFER_AIRCRAFT)) {
				const int j = mxml_GetInt(ss, SAVE_TRANSFER_ID, -1);

				if (j >= 0 && j < ccs.numAircraft)
					transfer->aircraftArray[j] = j;
			}
		}
		ccs.numTransfers++;
	}

	return qtrue;
}

/**
 * @brief Defines commands and cvars for the Transfer menu(s).
 * @sa UI_InitStartup
 */
void TR_InitStartup (void)
{
	/* add commands */
#ifdef DEBUG
	Cmd_AddCommand("debug_listtransfers", TR_ListTransfers_f, "Lists an/all active transfer(s)");
#endif
}

