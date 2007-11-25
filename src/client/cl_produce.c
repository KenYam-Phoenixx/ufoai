/**
 * @file cl_produce.c
 * @brief Single player production stuff
 * @note Production stuff functions prefix: PR_
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

/** @brief Holds the current active production category (which is buytype). */
static int produceCategory = BUY_WEAP_PRI;

/** @brief Holds the current active selected queue index/objID. */
static qboolean selectedQueueItem 	= qfalse;
static int selectedIndex 			= NONE;

/** @brief Used in production costs (to allow reducing prices below 1x). */
static const int PRODUCE_FACTOR = 1;
static const int PRODUCE_DIVISOR = 2;

/** @brief Default amount of workers, the produceTime for technologies is defined. */
/** @note producetime for technology entries is the time for PRODUCE_WORKERS amount of workers. */
static const int PRODUCE_WORKERS = 10;

/** @brief Number of blank lines between queued items and tech list. */
static const int QUEUE_SPACERS = 2;

static cvar_t* mn_production_limit;		/**< Maximum items in queue. */
static cvar_t* mn_production_workers;		/**< Amount of hired workers in base. */

static qboolean production_disassembling;	/**< are in disassembling state? */

static menuNode_t *node1, *node2, *prodlist;

/**
 * @brief Conditions for disassembling.
 * @param[in] comp Pointer to components definition.
 * @return qtrue if disassembling is ready, qfalse otherwise.
 */
static qboolean PR_ConditionsDisassembly (base_t* base, components_t *comp)
{
	objDef_t *od;

	assert(base);
	assert(comp);

	od = &csi.ods[comp->assembly_idx];
	assert(od);

	if (RS_IsResearched_ptr(od->tech) && (base->storage.num[comp->assembly_idx] > 0))
		return qtrue;
	else
		return qfalse;
}

/**
 * @brief Calculates the fraction (percentage) of production of an item in 1 hour.
 * @param[in] base Pointer to the base with given production.
 * @param[in] tech Pointer to the technology for given production.
 * @param[in] comp Pointer to components definition.
 * @param[in] disassembly True if calculations for disassembling, false otherwise.
 * @sa PR_ProductionRun
 * @sa PR_ProductionInfo
 * @return 0 if the production does not make any progress, 1 if the whole item is built in 1 hour
 */
static float PR_CalculateProductionPercentDone (const base_t *base, technology_t *tech, components_t *comp, qboolean disassembly)
{
	signed int allworkers = 0, maxworkers = 0;
	signed int timeDefault = 0;
	float fraction = 0;
	assert(base);
	assert(tech);

	/* Check how many workers hired in this base. */
	allworkers = E_CountHired(base, EMPL_WORKER);
	/* We will not use more workers than base capacity. */
	if (allworkers > base->capacities[CAP_WORKSPACE].max) {
		maxworkers = base->capacities[CAP_WORKSPACE].max;
	} else {
		maxworkers = allworkers;
	}

	if (!disassembly) {
		assert(tech);
		timeDefault = tech->produceTime;	/* This is the default production time for 10 workers. */
	} else {
		assert(comp);
		timeDefault = comp->time;		/* This is the default disassembly time for 10 workers. */
	}

	if (maxworkers == PRODUCE_WORKERS) {
		/* No need to calculate: timeDefault is for PRODUCE_WORKERS workers. */
		fraction = 1.0f / timeDefault;
		Com_DPrintf(DEBUG_CLIENT, "PR_CalculatePercentDone()... workers: %i, tech: %s, percent: %f\n",
		maxworkers, tech->id, fraction);
		return fraction;
	} else {
		/* Calculate the fraction of item produced for our amount of workers. */
		/* NOTE: I changed algorithm for a more realistic one, variing like maxworkers^2 -- Kracken 2007/11/18
		 * now, production time is divided by 4 each time you double the number of worker */
		fraction = (float) maxworkers / (PRODUCE_WORKERS * timeDefault);
		fraction = fraction * maxworkers / PRODUCE_WORKERS;
		Com_DPrintf(DEBUG_CLIENT, "PR_CalculatePercentDone()... workers: %i, tech: %s, percent: %f\n",
		maxworkers, tech->id, fraction);
		/* Don't allow to return fraction greater than 1 (you still need at least 1 hour to produce an item). */
		if (fraction > 1.0f)
			return 1;
		else
			return fraction;
	}
}

/**
 * @brief Checks if the production requirements are met for a defined amount.
 * @param[in] amount How many items are planned to be produced.
 * @param[in] req The production requirements of the item that is to be produced.
 * @return 0: If nothing can be produced. 1+: If anything can be produced. 'amount': Maximum.
 */
static int PR_RequirementsMet (int amount, requirements_t *req, base_t* base)
{
	int a, i;
	int producible_amount = 0;
	qboolean producible = qfalse;

	for (a = 0; a < amount; a++) {
		producible = qtrue;
		for (i = 0; i < req->numLinks; i++) {
			if (req->type[i] == RS_LINK_ITEM) {
				/* The same code is used in "RS_RequirementsMet" */
				Com_DPrintf(DEBUG_CLIENT, "PR_RequirementsMet: %s / %i\n", req->id[i], req->idx[i]);
				if (B_ItemInBase(req->idx[i], base) < req->amount[i]) {
					producible = qfalse;
				}
			}
		}
		if (producible)
			producible_amount++;
		else
			break;
	}
	return producible_amount;
}

/**
 * @brief Remove or add the required items from/to the current base.
 * @param[in] amount How many items are planned to be added (positive number) or removed (negative number).
 * @param[in] req The production requirements of the item that is to be produced. Thes included numbers are multiplied with 'amount')
 * @todo This doesn't check yet if there are more items removed than are in the base-storage (might be fixed if we used a storage-fuction with checks, otherwise we can make it a 'contition' in order to run this function.
 */
static void PR_UpdateRequiredItemsInBasestorage (base_t* base, int amount, requirements_t *req)
{
	int i;
	equipDef_t *ed = NULL;

	if (!base)
		return;

	ed = &base->storage;
	if (!ed)
		return;

	if (amount == 0)
		return;

	for (i = 0; i < req->numLinks; i++) {
		if (req->type[i] == RS_LINK_ITEM) {
			if (amount > 0) {
				/* Add items to the base-storage. */
				ed->num[req->idx[i]] += (req->amount[i] * amount);
			} else { /* amount < 0 */
				/* Remove items from the base-storage. */
				ed->num[req->idx[i]] -= (req->amount[i] * -amount);
			}
		}
	}
}

/**
 * @brief Add a new item to the bottom of the production queue.
 * @param[in] queue
 * @param[in] objID Index of object to produce (in csi.ods[]).
 * @param[in] amount Desired amount to produce.
 * @param[in] disassembling True if this is disassembling, false if production.
 * @return
 */
static production_t *PR_QueueNew (base_t* base, production_queue_t *queue, signed int objID, signed int amount, qboolean disassembling)
{
	int numWorkshops = 0;
	objDef_t *od = NULL;
	aircraft_t *aircraft = NULL;
	production_t *prod = NULL;

	assert(base);

	if (E_CountHired(base, EMPL_WORKER) <= 0) {
		MN_Popup(_("Not enough workers"), _("You cannot queue productions without workers hired in this base.\n\nHire workers."));
		return NULL;
	}

	numWorkshops = max(B_GetNumberOfBuildingsInBaseByType(base->idx, B_WORKSHOP), 0);

	if (queue->numItems >= numWorkshops * MAX_PRODUCTIONS_PER_WORKSHOP) {
		MN_Popup(_("Not enough workshops"), _("You cannot queue more items.\nBuild more workshops.\n"));
		return NULL;
	}

	/* initialize */
	prod = &queue->items[queue->numItems];
	if (produceCategory != BUY_AIRCRAFT) {
		od = &csi.ods[objID];
		assert(od->tech);
	} else
		aircraft = &aircraft_samples[objID];

	/* We cannot queue new aircraft if no free hangar space. */
	if (produceCategory == BUY_AIRCRAFT) {
		if (!base->hasCommand) {
			MN_Popup(_("Hangars not ready"), _("You cannot queue aircraft.\nNo command centre in this base.\n"));
			return NULL;
		} else if (!base->hasHangar && !base->hasHangarSmall) {
			MN_Popup(_("Hangars not ready"), _("You cannot queue aircraft.\nNo hangars in this base.\n"));
			return NULL;
		}
		if (AIR_CalculateHangarStorage(objID, base, 0) <= 0) {
			MN_Popup(_("Hangars not ready"), _("You cannot queue aircraft.\nNo free space in hangars.\n"));
			return NULL;
		}
	}

	prod->objID = objID;
	prod->amount = amount;
	if (disassembling) {	/* Disassembling. */
		prod->production = qfalse;
		/* We have to remove amount of items being disassembled from base storage. */
		base->storage.num[objID] -= amount;
		/* Now find related components definition. */
		prod->percentDone = 0.0f;
	} else {	/* Production. */
		prod->production = qtrue;
		if (produceCategory == BUY_AIRCRAFT) {
			prod->aircraft = qtrue;
			if (aircraft->tech->produceTime < 0)
				return NULL;
			else
				prod->percentDone = 0.0f;
		} else {
			/* Don't try to add to queue an item which is not producible. */
			if (od->tech->produceTime < 0)
				return NULL;
			else
				prod->percentDone = 0.0f;
		}
	}

	queue->numItems++;
	return prod;
}


/**
 * @brief Delete the selected entry from the queue.
 * @param[in] queue Pointer to the queue.
 * @param[in] index Selected index in queue.
 * @param[in] baseidx Index of base in gd.bases[], where the queue is.
 */
static void PR_QueueDelete (base_t* base, production_queue_t *queue, int index)
{
	int i;
	objDef_t *od = NULL;
	production_t *prod = NULL;

	prod = &queue->items[index];

	assert(base);

	if (prod->items_cached && !prod->aircraft) {
		/* Get technology of the item in the selected queue-entry. */
		od = &csi.ods[prod->objID];
		if (od->tech) {
			/* Add all items listed in the prod.-requirements /multiplied by amount) to the storage again. */
			PR_UpdateRequiredItemsInBasestorage(base, prod->amount, &od->tech->require_for_production);
		} else {
			Com_DPrintf(DEBUG_CLIENT, "PR_QueueDelete: Problem getting technology entry for %i\n", index);
		}
		prod->items_cached = qfalse;
	}

	/* Readd disassembly to base storage. */
	if (!prod->production)
		base->storage.num[prod->objID] += prod->amount;

	queue->numItems--;
	if (queue->numItems < 0)
		queue->numItems = 0;

	/* Copy up other items. */
	for (i = index; i < queue->numItems; i++) {
		queue->items[i] = queue->items[i + 1];
	}
}

/**
 * @brief Moves the given queue item in the given direction.
 * @param[in] queue
 * @param[in] index
 * @param[in] dir
 */
static void PR_QueueMove (production_queue_t *queue, int index, int dir)
{
	int newIndex = index + dir;
	int i;
	production_t saved;

	if (newIndex == index)
		return;

	saved = queue->items[index];

	/* copy up */
	for (i = index; i < newIndex; i++) {
		queue->items[i] = queue->items[i+1];
	}

	/* copy down */
	for (i = index; i > newIndex; i--) {
		queue->items[i] = queue->items[i-1];
	}

	/* insert item */
	queue->items[newIndex] = saved;
}

/**
 * @brief Queues the next production in the queue.
 * @param[in] base The index of the base
 */
static void PR_QueueNext (base_t* base)
{
	production_queue_t *queue = &gd.productions[base->idx];

	PR_QueueDelete(base, queue, 0);
	if (queue->numItems == 0) {
		selectedQueueItem = qfalse;
		selectedIndex = NONE;
		Com_sprintf(messageBuffer, sizeof(messageBuffer), _("Production queue for base %s is empty"), base->name);
		MN_AddNewMessage(_("Production queue empty"), messageBuffer, qfalse, MSG_PRODUCTION, NULL);
		CL_GameTimeStop();
		return;
	} else if (selectedIndex >= queue->numItems) {
		selectedIndex = queue->numItems - 1;
	}
}

/**
 * @brief Checks whether an item is finished.
 * @sa CL_CampaignRun
 */
void PR_ProductionRun (void)
{
	int i;
	objDef_t *od = NULL;
	aircraft_t *aircraft = NULL;
	production_t *prod;
	aircraft_t *ufocraft;

	/* Loop through all founded bases. Then check productions
	 * in global data array. Then increase prod->percentDone and check
	 * wheter an item is produced. Then add to base storage. */

	for (i = 0; i < MAX_BASES; i++) {
		if (!gd.bases[i].founded)
			continue;

		/* not actually any active productions */
		if (gd.productions[i].numItems <= 0)
			continue;

		/* Workshop is disabled because their dependences are disabled */
		if (!gd.bases[i].hasWorkshop)
			continue;

		prod = &gd.productions[i].items[0];
		assert(prod->objID >= 0);
		if (!prod->aircraft)
			od = &csi.ods[prod->objID];
		else
			aircraft = &aircraft_samples[prod->objID];

		if (prod->production) {	/* This is production, not disassembling. */
			if (!prod->aircraft) {
				/* Not enough money to produce more items in this base. */
				if (od->price * PRODUCE_FACTOR/PRODUCE_DIVISOR > ccs.credits) {
					if (!prod->creditmessage) {
						Com_sprintf(messageBuffer, sizeof(messageBuffer), _("Not enough credits to finish production in base %s.\n"), gd.bases[i].name);
						MN_AddNewMessage(_("Notice"), messageBuffer, qfalse, MSG_STANDARD, NULL);
						prod->creditmessage = qtrue;
					}
					continue;
				}
				/* Not enough free space in base storage for this item. */
				if (gd.bases[i].capacities[CAP_ITEMS].max - gd.bases[i].capacities[CAP_ITEMS].cur < od->size) {
					if (!prod->spacemessage) {
						Com_sprintf(messageBuffer, sizeof(messageBuffer), _("Not enough free storage space in base %s. Production paused.\n"), gd.bases[i].name);
						MN_AddNewMessage(_("Notice"), messageBuffer, qfalse, MSG_STANDARD, NULL);
						prod->spacemessage = qtrue;
					}
					continue;
				}
			} else {
				/* Not enough money to produce more items in this base. */
				if (aircraft->price * PRODUCE_FACTOR/PRODUCE_DIVISOR > ccs.credits) {
					if (!prod->creditmessage) {
						Com_sprintf(messageBuffer, sizeof(messageBuffer), _("Not enough credits to finish production in base %s.\n"), gd.bases[i].name);
						MN_AddNewMessage(_("Notice"), messageBuffer, qfalse, MSG_STANDARD, NULL);
						prod->creditmessage = qtrue;
					}
					continue;
				}
				/* Not enough free space in hangars for this aircraft. */
				if (AIR_CalculateHangarStorage(prod->objID, &gd.bases[i], 0) <= 0) {
					if (!prod->spacemessage) {
						Com_sprintf(messageBuffer, sizeof(messageBuffer), _("Not enough free hangar space in base %s. Production paused.\n"), gd.bases[i].name);
						MN_AddNewMessage(_("Notice"), messageBuffer, qfalse, MSG_STANDARD, NULL);
						prod->spacemessage = qtrue;
					}
					continue;
				}
			}
		} else {		/* This is disassembling. */
			if (gd.bases[i].capacities[CAP_ITEMS].max - gd.bases[i].capacities[CAP_ITEMS].cur <
			INV_DisassemblyItem(NULL, INV_GetComponentsByItemIdx(prod->objID), qtrue)) {
				if (!prod->spacemessage) {
					Com_sprintf(messageBuffer, sizeof(messageBuffer), _("Not enough free storage space in base %s. Disassembling paused.\n"), gd.bases[i].name);
					MN_AddNewMessage(_("Notice"), messageBuffer, qfalse, MSG_STANDARD, NULL);
					prod->spacemessage = qtrue;
				}
				continue;
			}
		}

#ifdef DEBUG
		if (!prod->aircraft && !od->tech)
			Sys_Error("PR_ProductionRun: No tech pointer for object id %i ('%s')\n", prod->objID, od->id);
#endif
		if (prod->production) {	/* This is production, not disassembling. */
			if (!prod->aircraft)
				prod->percentDone += PR_CalculateProductionPercentDone(&gd.bases[i], od->tech, NULL, qfalse);
			else
				prod->percentDone += PR_CalculateProductionPercentDone(&gd.bases[i], aircraft->tech, NULL, qfalse);
		} else /* This is disassembling. */
			prod->percentDone += PR_CalculateProductionPercentDone(&gd.bases[i], od->tech, INV_GetComponentsByItemIdx(prod->objID), qtrue);

		if (prod->percentDone >= 1.0f) {
			if (prod->production) {	/* This is production, not disassembling. */
				if (!prod->aircraft) {
					CL_UpdateCredits(ccs.credits - (od->price * PRODUCE_FACTOR / PRODUCE_DIVISOR));
					prod->percentDone = 0.0f;
					prod->amount--;
					/* Now add it to equipment and update capacity. */
					B_UpdateStorageAndCapacity(&gd.bases[i], prod->objID, 1, qfalse, qfalse);

					/* queue the next production */
					if (prod->amount <= 0) {
						Com_sprintf(messageBuffer, sizeof(messageBuffer), _("The production of %s has finished."), od->name);
						MN_AddNewMessage(_("Production finished"), messageBuffer, qfalse, MSG_PRODUCTION, od->tech);
						PR_QueueNext(&gd.bases[i]);
					}
				} else {
					CL_UpdateCredits(ccs.credits - (aircraft->price * PRODUCE_FACTOR / PRODUCE_DIVISOR));
					prod->percentDone = 0.0f;
					prod->amount--;
					/* Now add new aircraft. */
					AIR_NewAircraft(&gd.bases[i], aircraft->id);
					/* queue the next production */
					if (prod->amount <= 0) {
						Com_sprintf(messageBuffer, sizeof(messageBuffer), _("The production of %s has finished."), _(aircraft->name));
						MN_AddNewMessage(_("Production finished"), messageBuffer, qfalse, MSG_PRODUCTION, NULL);
						PR_QueueNext(&gd.bases[i]);
					}
				}
			} else {	/* This is disassembling. */
				gd.bases[i].capacities[CAP_ITEMS].cur += INV_DisassemblyItem(&gd.bases[i], INV_GetComponentsByItemIdx(prod->objID), qfalse);
				prod->percentDone = 0.0f;
				prod->amount--;
				/* If this is aircraft dummy item, update UFO hangars capacity. */
				if (od->aircraft) {
					ufocraft = AIR_GetAircraft(od->id);
					assert(ufocraft);
					gd.bases[i].capacities[CAP_UFOHANGARS].cur -= ufocraft->weight;
				}
				if (prod->amount <= 0) {
					Com_sprintf(messageBuffer, sizeof(messageBuffer), _("The disassembling of %s has finished."),od->name);
					MN_AddNewMessage(_("Production finished"), messageBuffer, qfalse, MSG_PRODUCTION, od->tech);
					PR_QueueNext(&gd.bases[i]);
				}
			}
		}
	}
}

/**
 * @brief Prints information about the selected item in production.
 * @param[in] disassembly True, if we are trying to display disassembly info.
 * @note -1 for objID means that there is no production in this base.
 */
static void PR_ProductionInfo (const base_t* base, qboolean disassembly)
{
	static char productionInfo[512];
	objDef_t *od, *compod;
	int objID;
	int time, i, j;
	components_t *comp = NULL;
	float prodPerHour;

	assert(base);

	if (selectedQueueItem) {
		assert(selectedIndex != NONE);
		objID = gd.productions[base->idx].items[selectedIndex].objID;
	} else {
		objID = selectedIndex;
	}

	if (!disassembly) {
		if (objID >= 0) {
			od = &csi.ods[objID];
			assert(od->tech);
			/* Don't try to display the item which is not producible. */
			if (od->tech->produceTime < 0) {
				Com_sprintf(productionInfo, sizeof(productionInfo), _("No item selected"));
				Cvar_Set("mn_item", "");
			} else {
				/* If item is first in queue, take percentDone into account. */
				prodPerHour = PR_CalculateProductionPercentDone(base, od->tech, NULL, qfalse);
				/* If you entered production menu, that means that prodPerHour > 0 (must not divide by 0) */
				assert(prodPerHour > 0);
				if (objID == gd.productions[base->idx].items[0].objID)
					time = ceil((1.0f - gd.productions[base->idx].items[0].percentDone) / prodPerHour);
				else
					time = ceil(1.0f / prodPerHour);
				Com_sprintf(productionInfo, sizeof(productionInfo), "%s\n", od->name);
				Q_strcat(productionInfo, va(_("Costs per item\t%i c\n"), (od->price * PRODUCE_FACTOR / PRODUCE_DIVISOR)),
					sizeof(productionInfo));
				Q_strcat(productionInfo, va(_("Productiontime\t%ih\n"), time), sizeof(productionInfo));
				Q_strcat(productionInfo, va(_("Item size\t%i\n"), od->size), sizeof(productionInfo));
				UP_ItemDescription(objID);
			}
		} else {
			Com_sprintf(productionInfo, sizeof(productionInfo), _("No item selected"));
			Cvar_Set("mn_item", "");
		}
	} else {	/* Disassembling. */
		/* Find related components array. */
		for (i = 0; i < gd.numComponents; i++) {
			comp = &gd.components[i];
			Com_DPrintf(DEBUG_CLIENT, "components definition: %s item: %s\n", comp->assembly_id, csi.ods[comp->assembly_idx].id);
			if (comp->assembly_idx == objID)
				break;
		}
		assert(comp);
		if (objID >= 0) {
			od = &csi.ods[objID];
			assert(od->tech);

			/* If item is first in queue, take percentDone into account. */
			prodPerHour = PR_CalculateProductionPercentDone(base, od->tech, comp, qtrue);
			/* If you entered production menu, that means that prodPerHour > 0 (must not divide by 0) */
			assert(prodPerHour > 0);
			if (objID == gd.productions[base->idx].items[0].objID)
				time = ceil((1.0f - gd.productions[base->idx].items[0].percentDone) / prodPerHour);
			else
				time = ceil(1.0f / prodPerHour);
			Com_sprintf(productionInfo, sizeof(productionInfo), _("%s - disassembly\n"), od->name);
			Q_strcat(productionInfo, _("Components: "), sizeof(productionInfo));
			/* Print components. */
			for (i = 0; i < comp->numItemtypes; i++) {
				for (j = 0, compod = csi.ods; j < csi.numODs; j++, compod++) {
					if (!Q_strncmp(compod->id, comp->item_id[i], MAX_VAR))
						break;
				}
				Q_strcat(productionInfo, va(_("%s (%i) "), compod->name, comp->item_amount[i]),
					sizeof(productionInfo));
			}
			Q_strcat(productionInfo, "\n", sizeof(productionInfo));
			Q_strcat(productionInfo, va(_("Disassembly time\t%ih\n"), time),
				sizeof(productionInfo));
			UP_ItemDescription(objID);
		}
	}
	menuText[TEXT_PRODUCTION_INFO] = productionInfo;
}

/**
 * @brief Prints information about the selected aircraft in production.
 */
static void PR_AircraftInfo (void)
{
	aircraft_t *aircraft;
	static char productionInfo[512];

	if (selectedIndex != NONE) {
		aircraft = &aircraft_samples[selectedIndex];
		Com_sprintf(productionInfo, sizeof(productionInfo), "%s\n", _(aircraft->name));
		Q_strcat(productionInfo, va(_("Production costs\t%i c\n"), (aircraft->price * PRODUCE_FACTOR / PRODUCE_DIVISOR)),
			sizeof(productionInfo));
		Q_strcat(productionInfo, va(_("Productiontime\t%ih\n"), aircraft->tech->produceTime), sizeof(productionInfo));
	} else {
		Com_sprintf(productionInfo, sizeof(productionInfo), _("No aircraft selected."));
	}
	menuText[TEXT_PRODUCTION_INFO] = productionInfo;
}

/**
 * @brief Click function for production list
 * @note Opens the ufopedia - by right clicking an item
 */
static void PR_ProductionListRightClick_f (void)
{
	int i, j, num, idx;
	objDef_t *od = NULL;
	production_queue_t *queue = NULL;

	/* can be called from everywhere without a started game */
	if (!baseCurrent ||!curCampaign)
		return;
	queue = &gd.productions[baseCurrent->idx];

	/* not enough parameters */
	if (Cmd_Argc() < 2) {
		Com_Printf("Usage: %s <arg>\n", Cmd_Argv(0));
		return;
	}

	/* clicked which item? */
	num = atoi(Cmd_Argv(1));

	/* Clicked the production queue or the item list? */
	if (num < queue->numItems && num >= 0) {
		od = &csi.ods[queue->items[num].objID];
		assert(od->tech);
		UP_OpenWith(od->tech->id);
	} else if (num >= queue->numItems + QUEUE_SPACERS) {
		/* Clicked in the item list. */
		idx = num - queue->numItems - QUEUE_SPACERS;
		for (j = 0, i = 0, od = csi.ods; i < csi.numODs; i++, od++) {
#ifdef DEBUG
			if (!od->tech)
				Sys_Error("PR_ProductionListRightClick_f: No tech pointer for object id %i ('%s')\n", i, od->id);
#endif
			/* Open up ufopedia for this entry. */
			if (BUYTYPE_MATCH(od->buytype, produceCategory) && RS_IsResearched_ptr(od->tech)) {
				if (j == idx) {
					UP_OpenWith(od->tech->id);
					return;
				}
				j++;
			}
		}
	}
#ifdef DEBUG
	else
		Com_DPrintf(DEBUG_CLIENT, "PR_ProductionListRightClick_f: Click on spacer %i\n", num);
#endif
}

/**
 * @brief Click function for production list.
 */
static void PR_ProductionListClick_f (void)
{
	int i, j, num, idx;
	objDef_t *od = NULL;
	aircraft_t *aircraft = NULL;
	components_t *comp = NULL;
	production_queue_t *queue = NULL;
	production_t *prod = NULL;
	base_t* base;

	/* can be called from everywhere without a started game */
	if (!baseCurrent || !curCampaign)
		return;

	base = baseCurrent;
	queue = &gd.productions[base->idx];

#if 0 /* FIXME: not a concern any more ... */
	/* there is already a running production - stop it first */
	if (gd.productions[base->idx].amount > 0 ) {
		PR_ProductionInfo(base, qfalse);
		return;
	}
#endif

	/* not enough parameters */
	if (Cmd_Argc() < 2) {
		Com_Printf("Usage: %s <arg>\n", Cmd_Argv(0));
		return;
	}

	/* clicked which item? */
	num = atoi(Cmd_Argv(1));

	/* Clicked the production queue or the item list? */
	if (num < queue->numItems && num >= 0) {
		prod = &queue->items[num];
		selectedQueueItem = qtrue;
		selectedIndex = num;
		if (prod->production) {
			if (prod->aircraft)
				PR_AircraftInfo();
			else
				PR_ProductionInfo(base, qfalse);
		} else
			PR_ProductionInfo(base, qtrue);
	} else if (num >= queue->numItems + QUEUE_SPACERS) {
		/* Clicked in the item list. */
		idx = num - queue->numItems - QUEUE_SPACERS;
		if (!production_disassembling) {
			if (produceCategory != BUY_AIRCRAFT) {	/* Everything except aircrafts. */
				for (j = 0, i = 0, od = csi.ods; i < csi.numODs; i++, od++) {
#ifdef DEBUG
					if (!od->tech)
						Sys_Error("PR_ProductionListClick_f: No tech pointer for object id %i ('%s')\n", i, od->id);
#endif
					/* We can only produce items that fulfill the following conditions... */
					if (BUYTYPE_MATCH(od->buytype, produceCategory)	/* Item is in the current inventory-category */
						&& RS_IsResearched_ptr(od->tech)		/* Tech is researched */
						&& od->tech->produceTime >= 0		/* Item is producible */
					) {
						assert(*od->name);
						if (j == idx) {
#if 0
							/* FIXME: don't start production yet .. */
							gd.productions[base->idx].objID = i;
							gd.productions[base->idx].amount = 0;
							gd.productions[base->idx].timeLeft = t->produceTime;
#endif
							selectedQueueItem = qfalse;
							selectedIndex = i;
							PR_ProductionInfo(base, qfalse);
							return;
						}
						j++;
					}
				}
			} else {	/* Aircraft. */
				for (j = 0, i = 0; i < numAircraft_samples; i++) {
					aircraft = &aircraft_samples[j];
					if ((aircraft->tech->produceTime >= 0) && RS_IsResearched_ptr(aircraft->tech)) {
						if (j == idx) {
							selectedQueueItem = qfalse;
							selectedIndex = i;
							PR_AircraftInfo();
							return;
						}
						j++;
					}
				}
			}
		} else {	/* Disassembling. */
			for (j = 0, i = 0; i < gd.numComponents; i++) {
				comp = &gd.components[i];
				if (!PR_ConditionsDisassembly(base, comp))
					continue;
				if (j == idx) {
					selectedQueueItem = qfalse;
					selectedIndex = gd.components[i].assembly_idx;
					PR_ProductionInfo(base, qtrue);
					return;
				}
				j++;
			}
		}
	}
#ifdef DEBUG
	else
		Com_DPrintf(DEBUG_CLIENT, "PR_ProductionListClick_f: Click on spacer %i\n", num);
#endif
}

/** @brief update the list of queued and available items */
static void PR_UpdateProductionList (base_t* base)
{
	int i, j, counter;
	static char productionList[1024];
	static char productionQueued[256];
	static char productionAmount[256];
	objDef_t *od;
	production_queue_t *queue;
	production_t *prod;
	aircraft_t *aircraft, *aircraftbase;
	technology_t *tech = NULL;

	assert(base);

	production_disassembling = qfalse;

	productionAmount[0] = productionList[0] = productionQueued[0] = '\0';
	queue = &gd.productions[base->idx];

	/* first add all the queue items */
	for (i = 0; i < queue->numItems; i++) {
		prod = &queue->items[i];
		if (!prod->aircraft) {
			od = &csi.ods[prod->objID];
			Q_strcat(productionList, va("%s\n", od->name), sizeof(productionList));
			Q_strcat(productionAmount, va("%i\n", base->storage.num[prod->objID]), sizeof(productionAmount));
			Q_strcat(productionQueued, va("%i\n", prod->amount), sizeof(productionQueued));
		} else {
			aircraft = &aircraft_samples[prod->objID];
			Q_strcat(productionList, va("%s\n", _(aircraft->name)), sizeof(productionList));
			for (j = 0, counter = 0; j < gd.numAircraft; j++) {
				aircraftbase = AIR_AircraftGetFromIdx(j);
				assert(aircraftbase);
				if ((aircraftbase->homebase == base) && (aircraftbase->idx_sample == i))
					counter++;
			}
			Q_strcat(productionAmount, va("%i\n", counter), sizeof(productionAmount));
			Q_strcat(productionQueued, va("%i\n", prod->amount), sizeof(productionQueued));
		}
	}

	/* then spacers */
	for (i = 0; i < QUEUE_SPACERS; i++) {
		Q_strcat(productionList, "\n", sizeof(productionList));
		Q_strcat(productionAmount, "\n", sizeof(productionAmount));
		Q_strcat(productionQueued, "\n", sizeof(productionQueued));
	}

	/* then go through all object definitions */
	if (produceCategory != BUY_AIRCRAFT) {	/* Everything except aircrafts. */
		for (i = 0, od = csi.ods; i < csi.numODs; i++, od++) {
			/* we will not show items with producetime = -1 - these are not producible */
			if (*od->id)
				tech = RS_GetTechByProvided(od->id);

			/* we can produce what was researched before */
			if (BUYTYPE_MATCH(od->buytype, produceCategory) && RS_IsResearched_ptr(od->tech)
			&& *od->name && tech && (tech->produceTime > 0)) {
				Q_strcat(productionList, va("%s\n", od->name), sizeof(productionList));
				Q_strcat(productionAmount, va("%i\n", base->storage.num[i]), sizeof(productionAmount));
				Q_strcat(productionQueued, "\n", sizeof(productionQueued));
			}
		}
	} else {
		for (i = 0; i < numAircraft_samples; i++) {
			aircraft = &aircraft_samples[i];
			/* don't allow producing ufos */
			if (aircraft->ufotype != UFO_MAX)
				continue;
			if (!aircraft->tech) {
				Com_Printf("PR_UpdateProductionList()... no technology for craft %s!\n", aircraft->id);
				continue;
			}
			Com_DPrintf(DEBUG_CLIENT, "air: %s ufotype: %i tech: %s time: %i\n", aircraft->id, aircraft->ufotype, aircraft->tech->id, aircraft->tech->produceTime);
			if (aircraft->tech->produceTime > 0 && RS_IsResearched_ptr(aircraft->tech)) {
				Q_strcat(productionList, va("%s\n", _(aircraft->name)), sizeof(productionList));
				for (j = 0, counter = 0; j < gd.numAircraft; j++) {
					aircraftbase = AIR_AircraftGetFromIdx(j);
					assert(aircraftbase);
					if ((aircraftbase->homebase == base) && (aircraftbase->idx_sample == i))
						counter++;
				}
				Q_strcat(productionAmount, va("%i\n", counter), sizeof(productionAmount));
				Q_strcat(productionQueued, "\n", sizeof(productionQueued));
			}
		}
	}
	/* bind the menu text to our static char array */
	menuText[TEXT_PRODUCTION_LIST] = productionList;
	/* bind the amount of available items */
	menuText[TEXT_PRODUCTION_AMOUNT] = productionAmount;
	/* bind the amount of queued items */
	menuText[TEXT_PRODUCTION_QUEUED] = productionQueued;

#if 0 /* FIXME: needed now? */
	/* now print the information about the current item in production */
	PR_ProductionInfo(base, qfalse);
#endif
}

/**
 * @brief update the list of items ready for disassembling
 */
static void PR_UpdateDisassemblingList_f (void)
{
	int i;
	static char productionList[1024];
	static char productionQueued[256];
	static char productionAmount[256];
	objDef_t *od;
	production_queue_t *queue;
	production_t *prod;
	components_t *comp;
	base_t* base;

	if (!baseCurrent)
		return;

	base = baseCurrent;
	production_disassembling = qtrue;

	productionAmount[0] = productionList[0] = productionQueued[0] = '\0';
	queue = &gd.productions[base->idx];

	/* first add all the queue items */
	for (i = 0; i < queue->numItems; i++) {
		prod = &queue->items[i];
		od = &csi.ods[prod->objID];

		Q_strcat(productionList, va("%s\n", od->name), sizeof(productionList));
		Q_strcat(productionAmount, va("%i\n", base->storage.num[prod->objID]), sizeof(productionAmount));
		Q_strcat(productionQueued, va("%i\n", prod->amount), sizeof(productionQueued));
	}

	/* then spacers */
	for (i = 0; i < QUEUE_SPACERS; i++) {
		Q_strcat(productionList, "\n", sizeof(productionList));
		Q_strcat(productionAmount, "\n", sizeof(productionAmount));
		Q_strcat(productionQueued, "\n", sizeof(productionQueued));
	}

	for (i = 0; i < gd.numComponents; i++) {
		if (gd.components[i].assembly_idx <= 0)
			continue;
		comp = &gd.components[i];
		if (PR_ConditionsDisassembly(base, comp)) {
			Q_strcat(productionList, va("%s\n", csi.ods[gd.components[i].assembly_idx].name), sizeof(productionList));
			Q_strcat(productionAmount, va("%i\n", base->storage.num[gd.components[i].assembly_idx]), sizeof(productionAmount));
			Q_strcat(productionQueued, "\n", sizeof(productionQueued));
		}
	}

	/* Enable disassembly cvar. */
	production_disassembling = qtrue;
	/* bind the menu text to our static char array */
	menuText[TEXT_PRODUCTION_LIST] = productionList;
	/* bind the amount of available items */
	menuText[TEXT_PRODUCTION_AMOUNT] = productionAmount;
	/* bind the amount of queued items */
	menuText[TEXT_PRODUCTION_QUEUED] = productionQueued;
}

/**
 * @brief will select a new tab on the produciton list
 */
static void PR_ProductionSelect_f (void)
{
	int cat;

	/* can be called from everywhere without a started game */
	if (!baseCurrent ||!curCampaign)
		return;

	if (Cmd_Argc() < 2) {
		Com_Printf("Usage: %s <category>\n", Cmd_Argv(0));
		return;
	}
	cat = atoi(Cmd_Argv(1));
	if (cat < MAX_BUYTYPES && cat >= BUY_WEAP_PRI)
		produceCategory = cat;
	else
		return;

	/* Enable disassembly cvar. */
	production_disassembling = qfalse;

	/* reset scroll values */
	node1->textScroll = node2->textScroll = prodlist->textScroll = 0;

	PR_UpdateProductionList(baseCurrent);
}

/**
 * @brief Will fill the list of producible items.
 * @note Some of Production Menu related cvars are being set here.
 */
static void PR_ProductionList_f (void)
{
	char tmpbuf[64];
	int numWorkshops = 0;

	if (Cmd_Argc() < 2) {
		Com_Printf("Usage: %s <type>\n", Cmd_Argv(0));
		return;
	}

	/* can be called from everywhere without a started game */
	if (!baseCurrent ||!curCampaign)
		return;

	if (atoi(Cmd_Argv(1)) == 0)
		Cmd_ExecuteString(va("prod_select %i", BUY_WEAP_PRI));
	else if (atoi(Cmd_Argv(1)) == 1)
		Cmd_ExecuteString(va("prod_select %i", BUY_AIRCRAFT));
	else
		return;

	PR_ProductionInfo(baseCurrent, qfalse);

	numWorkshops = B_GetNumberOfBuildingsInBaseByType(baseCurrent->idx, B_WORKSHOP);
	numWorkshops = (numWorkshops >= 0) ? numWorkshops : 0;

	Cvar_SetValue("mn_production_limit", MAX_PRODUCTIONS_PER_WORKSHOP * numWorkshops);
	Cvar_SetValue("mn_production_basecap", baseCurrent->capacities[CAP_WORKSPACE].max);

	/* Set amount of workers - all/ready to work (determined by base capacity. */
	if (baseCurrent->capacities[CAP_WORKSPACE].max >= E_CountHired(baseCurrent, EMPL_WORKER))
		baseCurrent->capacities[CAP_WORKSPACE].cur = E_CountHired(baseCurrent, EMPL_WORKER);
	else
		baseCurrent->capacities[CAP_WORKSPACE].cur = baseCurrent->capacities[CAP_WORKSPACE].max;

	Com_sprintf(tmpbuf, sizeof(tmpbuf), "%i/%i",
		baseCurrent->capacities[CAP_WORKSPACE].cur,
		E_CountHired(baseCurrent, EMPL_WORKER));
	Cvar_Set("mn_production_workers", tmpbuf);

	Com_sprintf(tmpbuf, sizeof(tmpbuf), "%i/%i",
		baseCurrent->capacities[CAP_ITEMS].cur,
		baseCurrent->capacities[CAP_ITEMS].max);
	Cvar_Set("mn_production_storage", tmpbuf);
}

/**
 * @brief Returns true if the current base is able to produce items
 * @sa B_BaseInit_f
 */
qboolean PR_ProductionAllowed (const base_t* base)
{
	assert(base);
	if (base->baseStatus != BASE_UNDER_ATTACK && base->hasWorkshop && E_CountHired(base, EMPL_WORKER) > 0) {
		Cbuf_AddText("set_prod_enabled;");
		return qtrue;
	} else {
		Cbuf_AddText("set_prod_disabled;");
		return qfalse;
	}
}

/**
 * @brief Function binding for prod_scroll that scrolls other text nodes, too
 */
static void PR_ProductionListScroll_f (void)
{
	node1->textScroll = node2->textScroll = prodlist->textScroll;
}

/**
 * @brief Sets the production array to 0
 */
void PR_ProductionInit (void)
{
	Com_DPrintf(DEBUG_CLIENT, "Reset all productions\n");
	memset(gd.productions, 0, sizeof(production_queue_t)*MAX_BASES);
	mn_production_limit = Cvar_Get("mn_production_limit", "0", 0, NULL);
	mn_production_workers = Cvar_Get("mn_production_workers", "0", 0, NULL);
}

/**
 * @sa CL_InitAfter
 */
void PR_Init (void)
{
	menu_t* menu = MN_GetMenu("production");
	if (!menu)
		Sys_Error("Could not find the production menu\n");

	prodlist = MN_GetNode(menu, "prodlist");
	node1 = MN_GetNode(menu, "prodlist_amount");
	node2 = MN_GetNode(menu, "prodlist_queued");

	if (!prodlist || !node1 || !node2)
		Sys_Error("Could not find the needed menu nodes in production menu\n");
}

/**
 * @brief Increases the production amount by given parameter.
 */
static void PR_ProductionIncrease_f (void)
{
	int amount = 1, amount_temp = 0;
	int producible_amount;
	production_queue_t *queue = NULL;
	objDef_t *od = NULL;
	aircraft_t *aircraft = NULL;
	production_t *prod = NULL;
	base_t* base;

	if (!baseCurrent)
		return;

	base = baseCurrent;

	if (Cmd_Argc() == 2)
		amount = atoi(Cmd_Argv(1));

	queue = &gd.productions[base->idx];

	if (selectedQueueItem) {
		assert(selectedIndex >= 0 && selectedIndex < queue->numItems);
		prod = &queue->items[selectedIndex];
		if (prod->production) {		/* Production. */
			if (prod->aircraft) {
				/* Don't allow to queue more aircrafts if there is no free space. */
				if (AIR_CalculateHangarStorage(prod->objID, base, 0) <= 0) {
					MN_Popup(_("Hangars not ready"), _("You cannot queue aircraft.\nNo free space in hangars.\n"));
					return;
				}
			}
			prod->amount += amount;
		} else {			/* Disassembling. */
			/* We can disassembly only as many items as we have in base storage. */
			if (base->storage.num[prod->objID] > amount)
				amount_temp = amount;
			else
				amount_temp = base->storage.num[prod->objID];
			Com_DPrintf(DEBUG_CLIENT, "PR_ProductionIncrease_f()... amounts: storage: %i, param: %i, temp: %i\n", base->storage.num[prod->objID], amount, amount_temp);
			/* Now remove the amount we just added to queue from base storage. */
			base->storage.num[prod->objID] -= amount_temp;
			prod->amount += amount_temp;
		}
	} else {
		if (selectedIndex == NONE)
			return;
		if (!production_disassembling) {
			prod = PR_QueueNew(base, queue, selectedIndex, amount, qfalse);	/* Production. */
		} else {
			/* We can disassembly only as many items as we have in base storage. */
			if (base->storage.num[selectedIndex] > amount)
				amount_temp = amount;
			else
				amount_temp = base->storage.num[selectedIndex];
			prod = PR_QueueNew(base, queue, selectedIndex, amount_temp, qtrue);	/* Disassembling. */
		}
		/* prod is NULL when queue limit is reached */
		if (!prod) {
			/* Oops! Too many items! */
			MN_Popup(_("Queue full!"), _("You cannot queue any more items!"));
			return;
		}

		if (produceCategory != BUY_AIRCRAFT) {
			/* Get technology of the item in the selected queue-entry. */
			od = &csi.ods[prod->objID];

			if (od->tech)
				producible_amount = PR_RequirementsMet(amount, &od->tech->require_for_production, base);
			else
				producible_amount = amount;

			if (producible_amount > 0) {	/* Check if production requirements have been (even partially) met. */
				if (od->tech) {
					/* Remove the additionally required items (multiplied by 'producible_amount') from base-storage.*/
					PR_UpdateRequiredItemsInBasestorage(base, -amount, &od->tech->require_for_production);
					prod->items_cached = qtrue;
				}

				if (producible_amount < amount) {
					/* @todo: make the numbers work here. */
					MN_Popup(_("Not enough material!"), va(_("You don't have enough material to produce all (%i) items. Production will continue with a reduced (%i) number."), amount, producible_amount));
				}

				if (!production_disassembling) {
					Com_sprintf(messageBuffer, sizeof(messageBuffer), _("Production of %s started"), csi.ods[selectedIndex].name);
					MN_AddNewMessage(_("Production started"), messageBuffer, qfalse, MSG_PRODUCTION, csi.ods[selectedIndex].tech);
				} else {
					Com_sprintf(messageBuffer, sizeof(messageBuffer), _("Disassembling of %s started"), csi.ods[selectedIndex].name);
					MN_AddNewMessage(_("Production started"), messageBuffer, qfalse, MSG_PRODUCTION, csi.ods[selectedIndex].tech);
				}

				/* Now we select the item we just created. */
				selectedQueueItem = qtrue;
				selectedIndex = queue->numItems - 1;
			} else { /* requirements are not met => producible_amount <= 0 */
 				/* @todo: better messages needed */
				MN_Popup(_("Not enough material!"), _("You don't have enough of the needed material to produce this item."));
				/* @todo:
				 *  -) need to popup something like: "You need the following items in order to produce more of ITEM:   x of ITEM, x of ITEM, etc..."
				 *     This info should also be displayed in the item-info.
				 *  -) can can (if possible) change the 'amount' to a vlalue that _can_ be produced (i.e. the maximum amount possible).*/
			}
		} else {
			prod->aircraft = qtrue;
			aircraft = &aircraft_samples[prod->objID];
			Com_DPrintf(DEBUG_CLIENT, "Increasing %s\n", aircraft->name);
			Com_sprintf(messageBuffer, sizeof(messageBuffer), _("Production of %s started"), _(aircraft->name));
			MN_AddNewMessage(_("Production started"), messageBuffer, qfalse, MSG_PRODUCTION, NULL);
			/* Now we select the item we just created. */
			selectedQueueItem = qtrue;
			selectedIndex = queue->numItems - 1;
		}
	}

	if (!production_disassembling) {	/* Production. */
		if (produceCategory != BUY_AIRCRAFT)
			PR_ProductionInfo(base, qfalse);
		else
			PR_AircraftInfo();
		PR_UpdateProductionList(base);
	} else {							/* Disassembling. */
		PR_ProductionInfo(base, qtrue);
		PR_UpdateDisassemblingList_f();
	}
}

/**
 * @brief Stops the current running production
 */
static void PR_ProductionStop_f (void)
{
	production_queue_t *queue;
	qboolean disassembling = qfalse;
	base_t* base;

	if (!baseCurrent || !selectedQueueItem)
		return;

	base = baseCurrent;

	queue = &gd.productions[base->idx];
	if (!queue->items[selectedIndex].production)
		disassembling = qtrue;

	PR_QueueDelete(base, queue, selectedIndex);

	if (queue->numItems == 0) {
		selectedQueueItem = qfalse;
		selectedIndex = NONE;
		PR_ProductionInfo(base, qfalse);
	} else if (selectedIndex >= queue->numItems) {
		selectedIndex = queue->numItems - 1;
		if (!queue->items[selectedIndex].production)
			PR_ProductionInfo(base, qtrue);
		else
			PR_ProductionInfo(base, qfalse);
	}

	PR_UpdateDisassemblingList_f();
	PR_UpdateProductionList(base);
}

/**
 * @brief Decrease the production amount by given parameter
 */
static void PR_ProductionDecrease_f (void)
{
	int amount = 1, amount_temp = 0;
	production_queue_t *queue;
	production_t *prod;
	base_t* base;

	if (Cmd_Argc() == 2)
		amount = atoi(Cmd_Argv(1));

	if (!baseCurrent || !selectedQueueItem)
		return;

	base = baseCurrent;

	queue = &gd.productions[base->idx];
	assert(selectedIndex >= 0 && selectedIndex < queue->numItems);
	prod = &queue->items[selectedIndex];
	if (prod->amount >= amount)
		amount_temp = amount;
	else
		amount_temp = prod->amount;

	prod->amount -= amount_temp;
	/* We need to readd items being disassembled to base storage. */
	if (!prod->production)
		base->storage.num[prod->objID] += amount_temp;

	if (prod->amount <= 0) {
		PR_ProductionStop_f();
	} else {
		if (prod->production) {
			PR_ProductionInfo(base, qfalse);
			PR_UpdateProductionList(base);
		} else {
			PR_ProductionInfo(base, qtrue);
			PR_UpdateDisassemblingList_f();
		}
 	}
}

/**
 * @brief shift the current production up the list
 */
static void PR_ProductionUp_f (void)
{
	production_queue_t *queue;

	if (!baseCurrent || !selectedQueueItem)
		return;

	/* first position already */
	if (selectedIndex == 0)
		return;

	queue = &gd.productions[baseCurrent->idx];
	PR_QueueMove(queue, selectedIndex, -1);

	selectedIndex--;
	PR_UpdateProductionList(baseCurrent);
}

/**
 * @brief shift the current production down the list
 */
static void PR_ProductionDown_f (void)
{
	production_queue_t *queue;

	if (!baseCurrent || !selectedQueueItem)
		return;

	queue = &gd.productions[baseCurrent->idx];

	if (selectedIndex >= queue->numItems - 1)
		return;

	PR_QueueMove(queue, selectedIndex, 1);

	selectedIndex++;
	PR_UpdateProductionList(baseCurrent);
}

/**
 * @brief This is more or less the initial
 * Bind some of the functions in this file to console-commands that you can call ingame.
 * Called from MN_ResetMenus resp. CL_InitLocal
 */
void PR_ResetProduction (void)
{
	/* add commands */
	Cmd_AddCommand("prod_init", PR_ProductionList_f, NULL);
	Cmd_AddCommand("prod_scroll", PR_ProductionListScroll_f, "Scrolls the production lists");
	Cmd_AddCommand("prod_select", PR_ProductionSelect_f, NULL);
	Cmd_AddCommand("prod_disassemble", PR_UpdateDisassemblingList_f, "List of items ready for disassembling");
	Cmd_AddCommand("prodlist_rclick", PR_ProductionListRightClick_f, NULL);
	Cmd_AddCommand("prodlist_click", PR_ProductionListClick_f, NULL);
	Cmd_AddCommand("prod_inc", PR_ProductionIncrease_f, NULL);
	Cmd_AddCommand("prod_dec", PR_ProductionDecrease_f, NULL);
	Cmd_AddCommand("prod_stop", PR_ProductionStop_f, NULL);
	Cmd_AddCommand("prod_up", PR_ProductionUp_f, NULL);
	Cmd_AddCommand("prod_down", PR_ProductionDown_f, NULL);
}

/**
 * @brief Save callback for savegames
 * @sa PR_Load
 * @sa SAV_GameSave
 */
qboolean PR_Save (sizebuf_t* sb, void* data)
{
	int i, j;
	production_queue_t *pq;

	for (i = 0; i < presaveArray[PRE_MAXBAS]; i++) {
		pq = &gd.productions[i];
		MSG_WriteByte(sb, pq->numItems);
		for (j = 0; j < pq->numItems; j++) {
			MSG_WriteString(sb, csi.ods[pq->items[j].objID].id);
			MSG_WriteLong(sb, pq->items[j].amount);
			MSG_WriteFloat(sb, pq->items[j].percentDone);
			MSG_WriteByte(sb, pq->items[j].production);
			MSG_WriteByte(sb, pq->items[j].aircraft);
			MSG_WriteByte(sb, pq->items[j].items_cached);
		}
	}
	return qtrue;
}

/**
 * @brief Load callback for savegames
 * @sa PR_Save
 * @sa SAV_GameLoad
 */
qboolean PR_Load (sizebuf_t* sb, void* data)
{
	int i, j, k;
	const char *s;
	production_queue_t *pq;

	for (i = 0; i < presaveArray[PRE_MAXBAS]; i++) {
		pq = &gd.productions[i];
		pq->numItems = MSG_ReadByte(sb);
		for (j = 0; j < pq->numItems; j++) {
			s = MSG_ReadString(sb);
			k = INVSH_GetItemByID(s);
			if (k == NONE || k >= MAX_OBJDEFS) {
				Com_Printf("PR_Load: Could not find item '%s'\n", s);
				MSG_ReadLong(sb); /* amount */
				MSG_ReadFloat(sb); /* percentDone */
				MSG_ReadByte(sb); /* production */
				MSG_ReadByte(sb); /* aircraft */
				MSG_ReadByte(sb); /* items_cached */
			} else {
				pq->items[j].objID = k;
				pq->items[j].amount = MSG_ReadLong(sb);
				pq->items[j].percentDone = MSG_ReadFloat(sb);
				pq->items[j].production = MSG_ReadByte(sb);
				pq->items[j].aircraft = MSG_ReadByte(sb);
				pq->items[j].items_cached = MSG_ReadByte(sb);
			}
		}
	}
	return qtrue;
}
