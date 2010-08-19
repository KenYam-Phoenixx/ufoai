/**
 * @file cp_market.c
 * @brief Single player market stuff.
 * @note Buy/Sell menu functions prefix: BS_
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
#include "../ui/ui_main.h"
#include "../ui/ui_popup.h"
#include "../cl_inventory.h" /* INV_GetEquipmentDefinitionByID */
#include "cp_campaign.h"
#include "cp_market.h"
#include "cp_market_callbacks.h"
#include "save/save_market.h"

void BS_AddItemToMarket (const objDef_t *od, int amount)
{
	assert(amount >= 0);
	ccs.eMarket.numItems[od->idx] += amount;
}

void BS_RemoveItemFromMarket (const objDef_t *od, int amount)
{
	assert(amount >= 0);
	ccs.eMarket.numItems[od->idx] -= amount;
	if (ccs.eMarket.numItems[od->idx] < 0)
		ccs.eMarket.numItems[od->idx] = 0;
}

void BS_AddAircraftToMarket (const aircraft_t *aircraft, int amount)
{
	const humanAircraftType_t type = Com_DropShipShortNameToID(aircraft->id);
	assert(amount >= 0);
	assert(aircraft->type != AIRCRAFT_UFO);
	ccs.eMarket.numAircraft[type] += amount;
}

void BS_RemoveAircraftFromMarket (const aircraft_t *aircraft, int amount)
{
	const humanAircraftType_t type = Com_DropShipShortNameToID(aircraft->id);
	assert(amount >= 0);
	assert(aircraft->type != AIRCRAFT_UFO);
	ccs.eMarket.numAircraft[type] -= amount;
	if (ccs.eMarket.numAircraft[type] < 0)
		ccs.eMarket.numAircraft[type] = 0;
}

/**
 * @brief Get the number of aircraft of the given type on the market
 * @param[in] aircraft The aircraft to search the market for
 * @return The amount of aircraft for the given type
 */
int BS_GetAircraftOnMarket (const aircraft_t *aircraft)
{
	const humanAircraftType_t type = Com_DropShipShortNameToID(aircraft->id);
	assert(aircraft->type != AIRCRAFT_UFO);
	return ccs.eMarket.numAircraft[type];
}

/**
 * @brief Get the price for an aircraft that you want to sell on the market
 * @param[in] aircraft The aircraft to sell
 * @return The price of the aircraft
 */
int BS_GetAircraftSellingPrice (const aircraft_t *aircraft)
{
	const humanAircraftType_t type = Com_DropShipShortNameToID(aircraft->id);
	assert(aircraft->type != AIRCRAFT_UFO);
	return ccs.eMarket.bidAircraft[type];
}

/**
 * @brief Get the price for an aircraft that you want to buy on the market
 * @param[in] aircraft The aircraft to buy
 * @return The price of the aircraft
 */
int BS_GetAircraftBuyingPrice (const aircraft_t *aircraft)
{
	const humanAircraftType_t type = Com_DropShipShortNameToID(aircraft->id);
	assert(aircraft->type != AIRCRAFT_UFO);
	return ccs.eMarket.askAircraft[type];
}

/**
 * @brief Get the price for an item that you want to sell on the market
 * @param[in] od The item to sell
 * @return The price of the item
 */
int BS_GetItemSellingPrice (const objDef_t *od)
{
	return ccs.eMarket.bidItems[od->idx];
}

/**
 * @brief Get the price for an item that you want to buy on the market
 * @param[in] od The item to buy
 * @return The price of the item
 */
int BS_GetItemBuyingPrice (const objDef_t *od)
{
	return ccs.eMarket.askItems[od->idx];
}

/**
 * @brief Buy items.
 * @param[in] base Pointer to the base where items are bought.
 * @param[in] item Pointer to the item to buy.
 * @param[in] number Number of items to buy.
 */
qboolean BS_CheckAndDoBuyItem (base_t* base, const objDef_t *item, int number)
{
	int numItems;
	const int price = BS_GetItemBuyingPrice(item);

	assert(base);

	/* you can't buy more items than there are on market */
	numItems = min(number, ccs.eMarket.numItems[item->idx]);

	/* you can't buy more items than you have credits for */
	/** @todo Handle items with price 0 better */
	if (price)
		numItems = min(numItems, ccs.credits / price);
	if (numItems <= 0)
		return qfalse;

	/* you can't buy more items than you have room for */
	/** @todo Handle items with size 0 better */
	if (item->size)
		numItems = min(numItems, (base->capacities[CAP_ITEMS].max - base->capacities[CAP_ITEMS].cur) / item->size);
	/* make sure that numItems is > 0 (can be negative because capacities.cur may be greater than
	 * capacities.max if storage is disabled or if alien items have been collected on mission */
	if (numItems <= 0) {
		UI_Popup(_("Not enough storage space"), _("You cannot buy this item.\nNot enough space in storage.\nBuild more storage facilities."));
		return qfalse;
	}

	B_UpdateStorageAndCapacity(base, item, numItems, qfalse, qfalse);
	BS_RemoveItemFromMarket(item, numItems);
	CL_UpdateCredits(ccs.credits - price * numItems);
	return qtrue;
}


/**
 * @brief Update storage, the market, and the player's credits
 * @note Don't update capacity here because we can sell items directly from aircraft (already removed from storage).
 */
void BS_ProcessCraftItemSale (const objDef_t *craftitem, const int numItems)
{
	if (craftitem) {
		BS_AddItemToMarket(craftitem, numItems);
		CL_UpdateCredits(ccs.credits + BS_GetItemSellingPrice(craftitem) * numItems);
	}
}

/**
 * @brief Save callback for savegames
 * @param[out] parent XML Node structure, where we write the information to
 * @sa BS_LoadXML
 * @sa SAV_GameSaveXML
 */
qboolean BS_SaveXML (mxml_node_t *parent)
{
	int i;
	mxml_node_t *node;
	/* store market */
	node = mxml_AddNode(parent, SAVE_MARKET_MARKET);
	for (i = 0; i < csi.numODs; i++) {
		const objDef_t *od = INVSH_GetItemByIDX(i);
		if (BS_IsOnMarket(od)) {
			mxml_node_t * snode = mxml_AddNode(node, SAVE_MARKET_ITEM);
			mxml_AddString(snode, SAVE_MARKET_ID, od->id);
			mxml_AddIntValue(snode, SAVE_MARKET_NUM, ccs.eMarket.numItems[i]);
			mxml_AddIntValue(snode, SAVE_MARKET_BID, ccs.eMarket.bidItems[i]);
			mxml_AddIntValue(snode, SAVE_MARKET_ASK, ccs.eMarket.askItems[i]);
			mxml_AddDoubleValue(snode, SAVE_MARKET_EVO, ccs.eMarket.currentEvolutionItems[i]);
			mxml_AddBoolValue(snode, SAVE_MARKET_AUTOSELL, ccs.eMarket.autosell[i]);
		}
	}
	for (i = 0; i < AIRCRAFTTYPE_MAX; i++) {
		market_t *market = &ccs.eMarket;
		if (market->bidAircraft[i] > 0 || market->askAircraft[i] > 0) {
			mxml_node_t * snode = mxml_AddNode(node, SAVE_MARKET_AIRCRAFT);
			mxml_AddString(snode, SAVE_MARKET_ID, Com_DropShipTypeToShortName(i));
			mxml_AddIntValue(snode, SAVE_MARKET_NUM, market->numAircraft[i]);
			mxml_AddIntValue(snode, SAVE_MARKET_BID, market->bidAircraft[i]);
			mxml_AddIntValue(snode, SAVE_MARKET_ASK, market->askAircraft[i]);
			mxml_AddDoubleValue(snode, SAVE_MARKET_EVO, market->currentEvolutionAircraft[i]);
		}
	}
	return qtrue;
}

/**
 * @brief Load callback for savegames
 * @param[in] parent XML Node structure, where we get the information from
 * @sa BS_Save
 * @sa SAV_GameLoad
 */
qboolean BS_LoadXML (mxml_node_t *parent)
{
	mxml_node_t *node, *snode;
	node = mxml_GetNode(parent, SAVE_MARKET_MARKET);

	if (!node)
		return qfalse;

	for (snode = mxml_GetNode(node, SAVE_MARKET_ITEM); snode; snode = mxml_GetNextNode(snode, node, SAVE_MARKET_ITEM)) {
		const char *s = mxml_GetString(snode, SAVE_MARKET_ID);
		const objDef_t *od = INVSH_GetItemByID(s);

		if (!od) {
			Com_Printf("BS_LoadXML: Could not find item '%s'\n", s);
			continue;
		}

		ccs.eMarket.numItems[od->idx] = mxml_GetInt(snode, SAVE_MARKET_NUM, 0);
		ccs.eMarket.bidItems[od->idx] = mxml_GetInt(snode, SAVE_MARKET_BID, 0);
		ccs.eMarket.askItems[od->idx] = mxml_GetInt(snode, SAVE_MARKET_ASK, 0);
		ccs.eMarket.currentEvolutionItems[od->idx] = mxml_GetDouble(snode, SAVE_MARKET_EVO, 0.0);
		ccs.eMarket.autosell[od->idx] = mxml_GetBool(snode, SAVE_MARKET_AUTOSELL, qfalse);
	}
	for (snode = mxml_GetNode(node, SAVE_MARKET_AIRCRAFT); snode; snode = mxml_GetNextNode(snode, node, SAVE_MARKET_AIRCRAFT)) {
		const char *s = mxml_GetString(snode, SAVE_MARKET_ID);
		humanAircraftType_t type = Com_DropShipShortNameToID(s);
		
		ccs.eMarket.numAircraft[type] = mxml_GetInt(snode, SAVE_MARKET_NUM, 0);
		ccs.eMarket.bidAircraft[type] = mxml_GetInt(snode, SAVE_MARKET_BID, 0);
		ccs.eMarket.askAircraft[type] = mxml_GetInt(snode, SAVE_MARKET_ASK, 0);
		ccs.eMarket.currentEvolutionAircraft[type] = mxml_GetDouble(snode, SAVE_MARKET_EVO, 0.0);
	}

	return qtrue;
}

/**
 * @brief sets market prices at start of the game
 * @sa CP_CampaignInit
 * @sa B_SetUpFirstBase
 * @sa BS_Load (Market load function)
 * @param[in] load Is this an attempt to init the market for a savegame?
 */
void BS_InitMarket (void)
{
	int i;
	campaign_t *campaign = ccs.curCampaign;
	market_t *market = &ccs.eMarket;

	/* find the relevant markets */
	campaign->marketDef = INV_GetEquipmentDefinitionByID(campaign->market);
	campaign->asymptoticMarketDef = INV_GetEquipmentDefinitionByID(campaign->asymptoticMarket);

	for (i = 0; i < csi.numODs; i++) {
		const objDef_t *od = INVSH_GetItemByIDX(i);
		if (market->askItems[i] == 0) {
			market->askItems[i] = od->price;
			market->bidItems[i] = floor(market->askItems[i] * BID_FACTOR);
		}

		if (campaign->marketDef->numItems[i] <= 0)
			continue;

		if (RS_IsResearched_ptr(RS_GetTechForItem(od))) {
			/* the other relevant values were already set above */
			market->numItems[i] = campaign->marketDef->numItems[i];
		} else {
			Com_Printf("BS_InitMarket: Could not add item %s to the market - not marked as researched in campaign %s\n",
					od->id, campaign->id);
		}
	}

	for (i = 0; i < AIRCRAFTTYPE_MAX; i++) {
		const char* name = Com_DropShipTypeToShortName(i);
		const aircraft_t *aircraft = AIR_GetAircraft(name);
		if (market->askAircraft[i] == 0) {
			market->askAircraft[i] = aircraft->price;
			market->bidAircraft[i] = floor(market->askAircraft[i] * BID_FACTOR);
		}

		if (campaign->marketDef->numAircraft[i] <= 0)
			continue;

		if (RS_IsResearched_ptr(aircraft->tech)) {
			/* the other relevant values were already set above */
			market->numAircraft[i] = campaign->marketDef->numAircraft[i];
		} else {
			Com_Printf("BS_InitMarket: Could not add aircraft %s to the market - not marked as researched in campaign %s\n",
					aircraft->id, campaign->id);
		}
	}
}

/**
 * @brief make number of items change every day.
 * @sa CL_CampaignRun
 * @sa daily called
 * @note This function makes items number on market slowly reach the asymptotic number of items defined in equipment.ufo
 * If an item has just been researched, it's not available on market until RESEARCH_LIMIT_DELAY days is reached.
 */
void CL_CampaignRunMarket (void)
{
	int i;
	campaign_t *campaign = ccs.curCampaign;
	const float TYPICAL_TIME = 10.f;			/**< Number of days to reach the asymptotic number of items */
	const int RESEARCH_LIMIT_DELAY = 30;		/**< Numbers of days after end of research to wait in order to have
												 * items added on market */

	assert(campaign->marketDef);
	assert(campaign->asymptoticMarketDef);

	for (i = 0; i < csi.numODs; i++) {
		const objDef_t *od = INVSH_GetItemByIDX(i);
		const technology_t *tech = RS_GetTechForItem(od);
		int asymptoticNumber;

		if (RS_IsResearched_ptr(tech) && (campaign->marketDef->numItems[i] != 0 || ccs.date.day > tech->researchedDate.day + RESEARCH_LIMIT_DELAY)) {
			/* if items are researched for more than RESEARCH_LIMIT_DELAY or was on the initial market,
			 * there number tend to the value defined in equipment.ufo.
			 * This value is the asymptotic value if it is not 0, or initial value else */
			asymptoticNumber = campaign->asymptoticMarketDef->numItems[i] ? campaign->asymptoticMarketDef->numItems[i] : campaign->marketDef->numItems[i];
		} else {
			/* items that have just been researched don't appear on market, but they can disappear */
			asymptoticNumber = 0;
		}

		/* Store the evolution of the market in currentEvolution */
		ccs.eMarket.currentEvolutionItems[i] += (asymptoticNumber - ccs.eMarket.numItems[i]) / TYPICAL_TIME;

		/* Check if new items appeared or disappeared on market */
		if (fabs(ccs.eMarket.currentEvolutionItems[i]) >= 1.0f) {
			const int num = (int)(ccs.eMarket.currentEvolutionItems[i]);
			if (num >= 0)
				BS_AddItemToMarket(od, num);
			else
				BS_RemoveItemFromMarket(od, -num);
			ccs.eMarket.currentEvolutionItems[i] -= num;
		}
	}

	for (i = 0; i < AIRCRAFTTYPE_MAX; i++) {
		const humanAircraftType_t type = i;
		const char *aircraftID = Com_DropShipTypeToShortName(type);
		const aircraft_t* aircraft = AIR_GetAircraft(aircraftID);
		const technology_t *tech = aircraft->tech;
		int asymptoticNumber;

		if (RS_IsResearched_ptr(tech) && (campaign->marketDef->numAircraft[i] != 0 || ccs.date.day > tech->researchedDate.day + RESEARCH_LIMIT_DELAY)) {
			/* if aircraft is researched for more than RESEARCH_LIMIT_DELAY or was on the initial market,
			 * there number tend to the value defined in equipment.ufo.
			 * This value is the asymptotic value if it is not 0, or initial value else */
			asymptoticNumber = campaign->asymptoticMarketDef->numAircraft[i] ? campaign->asymptoticMarketDef->numAircraft[i] : campaign->marketDef->numAircraft[i];
		} else {
			/* items that have just been researched don't appear on market, but they can disappear */
			asymptoticNumber = 0;
		}
		/* Store the evolution of the market in currentEvolution */
		ccs.eMarket.currentEvolutionAircraft[i] += (asymptoticNumber - ccs.eMarket.numAircraft[i]) / TYPICAL_TIME;

		/* Check if new items appeared or disappeared on market */
		if (fabs(ccs.eMarket.currentEvolutionAircraft[i]) >= 1.0f) {
			const int num = (int)(ccs.eMarket.currentEvolutionAircraft[i]);
			if (num >= 0)
				BS_AddAircraftToMarket(aircraft, num);
			else
				BS_RemoveAircraftFromMarket(aircraft, -num);
			ccs.eMarket.currentEvolutionAircraft[i] -= num;
		}
	}
}

/**
 * @brief Check if an item is on market
 * @param[in] item Pointer to the item to check
 * @note this function doesn't check if the item is available on market (buyable > 0)
 */
qboolean BS_IsOnMarket (const objDef_t const* item)
{
	assert(item);
	return !(item->isVirtual || item->notOnMarket);
}

/**
 * @brief Returns true if you can buy or sell equipment
 * @param[in] base Pointer to base to check on
 * @sa B_BaseInit_f
 */
qboolean BS_BuySellAllowed (const base_t* base)
{
	if (base->baseStatus != BASE_UNDER_ATTACK
	 && B_GetBuildingStatus(base, B_STORAGE)) {
		return qtrue;
	} else {
		return qfalse;
	}
}

