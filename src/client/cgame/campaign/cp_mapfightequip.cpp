/**
 * @file
 * @brief contains everything related to equiping slots of aircraft or base
 * @note Base defence functions prefix: BDEF_
 * @note Aircraft items slots functions prefix: AII_
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
#include "cp_missions.h"
#include "cp_mapfightequip.h"
#include "cp_ufo.h"
#include "save/save_fightequip.h"

#define UFO_RELOAD_DELAY_MULTIPLIER 2
#define AIRCRAFT_RELOAD_DELAY_MULTIPLIER 2
#define BASE_RELOAD_DELAY_MULTIPLIER 2
#define INSTALLATION_RELOAD_DELAY_MULTIPLIER 2

/**
 * @brief Returns a list of craftitem technologies for the given type.
 * @note This list is terminated by a nullptr pointer.
 * @param[in] type Type of the craft-items to return.
 */
technology_t** AII_GetCraftitemTechsByType (aircraftItemType_t type)
{
	static technology_t* techList[MAX_TECHNOLOGIES];
	int j = 0;

	for (int i = 0; i < cgi->csi->numODs; i++) {
		const objDef_t* aircraftitem = INVSH_GetItemByIDX(i);
		if (aircraftitem->craftitem.type == type) {
			technology_t* tech = RS_GetTechForItem(aircraftitem);
			assert(j < MAX_TECHNOLOGIES);
			techList[j] = tech;
			j++;
		}
		/* j+1 because last item has to be nullptr */
		if (j + 1 >= MAX_TECHNOLOGIES) {
			cgi->Com_Printf("AII_GetCraftitemTechsByType: MAX_TECHNOLOGIES limit hit.\n");
			break;
		}
	}
	/* terminate the list */
	techList[j] = nullptr;
	return techList;
}

/**
 * @brief Returns craftitem weight based on size.
 * @param[in] od Pointer to objDef_t object being craftitem.
 * @return itemWeight_t
 * @sa AII_WeightToName
 */
itemWeight_t AII_GetItemWeightBySize (const objDef_t* od)
{
	assert(od);

	if (od->size < 50)
		return ITEM_LIGHT;
	else if (od->size < 100)
		return ITEM_MEDIUM;
	else
		return ITEM_HEAVY;
}

/**
 * @brief Check if an aircraft item should or should not be displayed in airequip menu
 * @param[in] slot Pointer to an aircraft slot (can be base/installation too)
 * @param[in] tech Pointer to the technology to test
 * @return true if the craft item should be displayed, false else
 */
bool AIM_SelectableCraftItem (const aircraftSlot_t* slot, const technology_t* tech)
{
	const objDef_t* item;

	if (!slot)
		return false;

	if (!RS_IsResearched_ptr(tech))
		return false;

	item = INVSH_GetItemByID(tech->provides);
	if (!item)
		return false;

	if (item->craftitem.type >= AC_ITEM_AMMO) {
		const objDef_t* weapon = slot->item;
		int k;
		if (slot->nextItem != nullptr)
			weapon = slot->nextItem;

		if (weapon == nullptr)
			return false;

		/* Is the ammo is usable with the slot */
		for (k = 0; k < weapon->numAmmos; k++) {
			const objDef_t* usable = weapon->ammos[k];
			if (usable && item->idx == usable->idx)
				break;
		}
		if (k >= weapon->numAmmos)
			return false;
	}

	/** @todo maybe this isn't working, aircraft slot type can't be an AMMO */
	if (slot->type >= AC_ITEM_AMMO) {
		/** @todo This only works for ammo that is usable in exactly one weapon
		 * check the weap_idx array and not only the first value */
		if (!slot->nextItem && item->weapons[0] != slot->item)
			return false;

		/* are we trying to change ammos for nextItem? */
		if (slot->nextItem && item->weapons[0] != slot->nextItem)
			return false;
	}

	/* you can install an item only if its weight is small enough for the slot */
	if (AII_GetItemWeightBySize(item) > slot->size)
		return false;

	/* you can't install an item that you don't possess
	 * virtual items don't need to be possessed
	 * installations always have weapon and ammo */
	if (slot->aircraft) {
		if (!B_BaseHasItem(slot->aircraft->homebase, item))
			return false;
	} else if (slot->base) {
		if (!B_BaseHasItem(slot->base, item))
			return false;
	}

	/* you can't install an item that does not have an installation time (alien item)
	 * except for ammo which does not have installation time */
	if (item->craftitem.installationTime == -1 && slot->type < AC_ITEM_AMMO)
		return false;

	return true;
}

/**
 * @brief Checks to see if the pilot is in any aircraft at this base.
 * @param[in] base Which base has the aircraft to search for the employee in.
 * @param[in] pilot Which employee to search for.
 * @return true or false depending on if the employee was found on the base aircraft.
 */
bool AIM_PilotAssignedAircraft (const base_t* base, const Employee* pilot)
{
	bool found = false;

	AIR_ForeachFromBase(aircraft, base) {
		if (AIR_GetPilot(aircraft) == pilot) {
			found = true;
			break;
		}
	}

	return found;
}

/**
 * @brief Adds a defence system to base.
 * @param[in] basedefType Base defence type (see basedefenceType_t)
 * @param[in] base Pointer to the base in which the battery will be added
 * @sa BDEF_RemoveBattery
 */
void BDEF_AddBattery (basedefenceType_t basedefType, base_t* base)
{
	switch (basedefType) {
	case BASEDEF_MISSILE:
		if (base->numBatteries >= MAX_BASE_SLOT) {
			cgi->Com_Printf("BDEF_AddBattery: too many missile batteries in base\n");
			return;
		}
		if (base->numBatteries)
			base->batteries[base->numBatteries].autofire = base->batteries[0].autofire;
		else if (base->numLasers)
			base->batteries[base->numBatteries].autofire = base->lasers[0].autofire;
		else
			base->batteries[base->numBatteries].autofire = true;
		base->numBatteries++;
		break;
	case BASEDEF_LASER:
		if (base->numLasers >= MAX_BASE_SLOT) {
			cgi->Com_Printf("BDEF_AddBattery: too many laser batteries in base\n");
			return;
		}
		base->lasers[base->numLasers].slot.ammoLeft = AMMO_STATUS_NOT_SET;
		if (base->numBatteries)
			base->lasers[base->numLasers].autofire = base->batteries[0].autofire;
		else if (base->numLasers)
			base->lasers[base->numLasers].autofire = base->lasers[0].autofire;
		else
			base->lasers[base->numLasers].autofire = true;
		base->numLasers++;
		break;
	default:
		cgi->Com_Printf("BDEF_AddBattery: unknown type of air defence system.\n");
	}
}

/**
 * @brief Remove a base defence sytem from base.
 * @param[in] base The base that is affected
 * @param[in] basedefType (see basedefenceType_t)
 * @param[in] idx index of the battery to destroy
 * @note if idx is negative the function looks for an empty battery to remove,
 *       it removes the last one if every one equipped
 * @sa BDEF_AddBattery
 */
void BDEF_RemoveBattery (base_t* base, basedefenceType_t basedefType, int idx)
{
	assert(base);

	/* Select the type of base defence system to destroy */
	switch (basedefType) {
	case BASEDEF_MISSILE: /* this is a missile battery */
		/* we must have at least one missile battery to remove it */
		assert(base->numBatteries > 0);
		/* look for an unequipped battery */
		if (idx < 0) {
			for (int i = 0; i < base->numBatteries; i++) {
				if (!base->batteries[i].slot.item) {
					idx = i;
					break;
				}
			}
		}
		/* if none found remove the last one */
		if (idx < 0)
			idx = base->numBatteries - 1;
		REMOVE_ELEM(base->batteries, idx, base->numBatteries);
		/* just for security */
		AII_InitialiseSlot(&base->batteries[base->numBatteries].slot, nullptr, base, nullptr, AC_ITEM_BASE_MISSILE);
		break;
	case BASEDEF_LASER: /* this is a laser battery */
		/* we must have at least one laser battery to remove it */
		assert(base->numLasers > 0);
		/* look for an unequipped battery */
		if (idx < 0) {
			for (int i = 0; i < base->numLasers; i++) {
				if (!base->lasers[i].slot.item) {
					idx = i;
					break;
				}
			}
		}
		/* if none found remove the last one */
		if (idx < 0)
			idx = base->numLasers - 1;
		REMOVE_ELEM(base->lasers, idx, base->numLasers);
		/* just for security */
		AII_InitialiseSlot(&base->lasers[base->numLasers].slot, nullptr, base, nullptr, AC_ITEM_BASE_LASER);
		break;
	default:
		cgi->Com_Printf("BDEF_RemoveBattery_f: unknown type of air defence system.\n");
	}
}

/**
 * @brief Initialise all values of base slot defence.
 * @param[in] base Pointer to the base which needs initalisation of its slots.
 */
void BDEF_InitialiseBaseSlots (base_t* base)
{
	for (int i = 0; i < MAX_BASE_SLOT; i++) {
		baseWeapon_t* battery = &base->batteries[i];
		baseWeapon_t* laser = &base->lasers[i];
		AII_InitialiseSlot(&battery->slot, nullptr, base, nullptr, AC_ITEM_BASE_MISSILE);
		AII_InitialiseSlot(&laser->slot, nullptr, base, nullptr, AC_ITEM_BASE_LASER);
		battery->autofire = true;
		battery->target = nullptr;
		laser->autofire = true;
		laser->target = nullptr;
	}
}

/**
 * @brief Initialise all values of installation slot defence.
 * @param[in] installation Pointer to the installation which needs initialisation of its slots.
 */
void BDEF_InitialiseInstallationSlots (installation_t* installation)
{
	for (int i = 0; i < installation->installationTemplate->maxBatteries; i++) {
		baseWeapon_t* battery = &installation->batteries[i];
		AII_InitialiseSlot(&battery->slot, nullptr, nullptr, installation, AC_ITEM_BASE_MISSILE);
		battery->target = nullptr;
		battery->autofire = true;
	}
}


/**
 * @brief Update the installation delay of one slot.
 * @param[in] base Pointer to the base to update the storage and capacity for
 * @param[in] installation Pointer to the installation being installed.
 * @param[in] aircraft Pointer to the aircraft (nullptr if a base is updated)
 * @param[in] slot Pointer to the slot to update
 * @sa AII_AddItemToSlot
 */
static void AII_UpdateOneInstallationDelay (base_t* base, installation_t* installation, aircraft_t* aircraft, aircraftSlot_t* slot)
{
	assert(base || installation);

	/* if the item is already installed, nothing to do */
	if (slot->installationTime == 0)
		return;
	else if (slot->installationTime > 0) {
		/* the item is being installed */
		slot->installationTime--;
		/* check if installation is over */
		if (slot->installationTime <= 0) {
			/* Update stats values */
			if (aircraft) {
				AII_UpdateAircraftStats(aircraft);
				Com_sprintf(cp_messageBuffer, lengthof(cp_messageBuffer),
						_("%s was successfully installed into aircraft %s at %s."),
						_(slot->item->name), aircraft->name, aircraft->homebase->name);
			} else if (installation) {
				Com_sprintf(cp_messageBuffer, lengthof(cp_messageBuffer), _("%s was successfully installed at installation %s."),
						_(slot->item->name), installation->name);
			} else {
				Com_sprintf(cp_messageBuffer, lengthof(cp_messageBuffer), _("%s was successfully installed at %s."),
						_(slot->item->name), base->name);
			}
			MSO_CheckAddNewMessage(NT_INSTALLATION_INSTALLED, _("Notice"), cp_messageBuffer);
		}
	} else if (slot->installationTime < 0) {
		/* the item is being removed */
		slot->installationTime++;
		if (slot->installationTime >= 0) {
#ifdef DEBUG
			if (aircraft && aircraft->homebase != base)
				Sys_Error("AII_UpdateOneInstallationDelay: aircraft->homebase and base pointers are out of sync\n");
#endif
			const objDef_t* olditem = slot->item;
			AII_RemoveItemFromSlot(base, slot, false);
			if (aircraft) {
				AII_UpdateAircraftStats(aircraft);
				/* Only stop time and post a notice, if no new item to install is assigned */
				if (!slot->item) {
					Com_sprintf(cp_messageBuffer, lengthof(cp_messageBuffer),
						_("%s was successfully removed from aircraft %s at %s."),
						_(olditem->name), aircraft->name, base->name);
					MSO_CheckAddNewMessage(NT_INSTALLATION_REMOVED, _("Notice"), cp_messageBuffer);
				} else {
					Com_sprintf(cp_messageBuffer, lengthof(cp_messageBuffer),
						_ ("%s was successfully removed, starting installation of %s into aircraft %s at %s"),
						_(olditem->name), _(slot->item->name), aircraft->name, base->name);
					MSO_CheckAddNewMessage(NT_INSTALLATION_REPLACE, _("Notice"), cp_messageBuffer);
				}
			} else if (!slot->item) {
				if (installation) {
					Com_sprintf(cp_messageBuffer, lengthof(cp_messageBuffer),
						_("%s was successfully removed from installation %s."),
						_(olditem->name), installation->name);
				} else {
					Com_sprintf(cp_messageBuffer, lengthof(cp_messageBuffer), _("%s was successfully removed from %s."),
						_(olditem->name), base->name);
				}
				MSO_CheckAddNewMessage(NT_INSTALLATION_REMOVED, _("Notice"), cp_messageBuffer);
			}
		}
	}
}

/**
 * @brief Update the installation delay of all slots of a given aircraft.
 * @note hourly called
 * @sa CP_CampaignRun
 * @sa AII_UpdateOneInstallationDelay
 */
void AII_UpdateInstallationDelay (void)
{
	base_t* base;
	int k;

	INS_Foreach(installation) {
		for (k = 0; k < installation->installationTemplate->maxBatteries; k++)
			AII_UpdateOneInstallationDelay(nullptr, installation, nullptr, &installation->batteries[k].slot);
	}

	base = nullptr;
	while ((base = B_GetNext(base)) != nullptr) {
		/* Update base */
		for (k = 0; k < base->numBatteries; k++)
			AII_UpdateOneInstallationDelay(base, nullptr, nullptr, &base->batteries[k].slot);
		for (k = 0; k < base->numLasers; k++)
			AII_UpdateOneInstallationDelay(base, nullptr, nullptr, &base->lasers[k].slot);
	}

	/* Update each aircraft */
	AIR_Foreach(aircraft) {
		if (AIR_IsAircraftInBase(aircraft)) {
			/* Update electronics delay */
			for (k = 0; k < aircraft->maxElectronics; k++)
				AII_UpdateOneInstallationDelay(aircraft->homebase, nullptr, aircraft, aircraft->electronics + k);
				/* Update weapons delay */
			for (k = 0; k < aircraft->maxWeapons; k++)
				AII_UpdateOneInstallationDelay(aircraft->homebase, nullptr, aircraft, aircraft->weapons + k);

			/* Update shield delay */
			AII_UpdateOneInstallationDelay(aircraft->homebase, nullptr, aircraft, &aircraft->shield);
		}
	}
}

/**
 * @brief Auto add ammo corresponding to weapon, if there is enough in storage.
 * @param[in] slot Pointer to the slot where you want to add ammo
 * @sa AIM_AircraftEquipAddItem_f
 * @sa AII_RemoveItemFromSlot
 */
void AII_AutoAddAmmo (aircraftSlot_t* slot)
{
	assert(slot);

	if (slot->aircraft && !AIR_IsUFO(slot->aircraft) && !AIR_IsAircraftInBase(slot->aircraft))
		return;

	/* Get the weapon (either current weapon or weapon to install after this one is removed) */
	const objDef_t* item = slot->nextItem ? slot->nextItem : slot->item;
	/* no items assigned */
	if (!item)
		return;
	/* item not a weapon */
	if (item->craftitem.type > AC_ITEM_WEAPON)
		return;
	/* don't try to add ammo to a slot that already has ammo */
	if (slot->nextItem ? slot->nextAmmo : slot->ammo)
		return;
	/* Try every ammo usable with this weapon until we find one we have in storage */
	for (int k = 0; k < item->numAmmos; k++) {
		const objDef_t* ammo = item->ammos[k];
		if (!ammo)
			continue;
		const technology_t* ammoTech = RS_GetTechForItem(ammo);
		if (!AIM_SelectableCraftItem(slot, ammoTech))
			continue;
		base_t* base;
		if (ammo->isVirtual)
			base = nullptr;
		else if (slot->aircraft)
			base = slot->aircraft->homebase;
		else
			base = slot->base;
		AII_AddAmmoToSlot(base, ammoTech, slot);
		break;
	}
}

/**
 * @brief Remove the item from the slot (or optionally its ammo only) and put it the base storage.
 * @note if there is another item to install after removal, begin this installation.
 * @note virtual items cannot be removed!
 * @param[in] base The base to add the item to (may be nullptr if item shouldn't be removed from any base).
 * @param[in] slot The slot to remove the item from.
 * @param[in] ammo true if we want to remove only ammo. false if the whole item should be removed.
 * @sa AII_AddItemToSlot
 * @sa AII_AddAmmoToSlot
 * @sa AII_RemoveNextItemFromSlot
 */
void AII_RemoveItemFromSlot (base_t* base, aircraftSlot_t* slot, bool ammo)
{
	assert(slot);

	if (ammo) {
		/* only remove the ammo */
		if (slot->ammo) {
			/* Can only remove non-virtual ammo */
			if (!slot->ammo->isVirtual) {
				if (base)
					B_AddToStorage(base, slot->ammo, 1);
				slot->ammo = nullptr;
				slot->ammoLeft = 0;
			}
		}
	} else if (slot->item) {
		/* remove ammo */
		AII_RemoveItemFromSlot(base, slot, true);
		if (!slot->item->isVirtual) {
			if (base)
				B_AddToStorage(base, slot->item, 1);
			/* the removal is over */
			if (slot->nextItem) {
				/* there is anoter item to install after this one */
				slot->item = slot->nextItem;
				/* we already removed nextItem from storage when it has been added to slot: don't use B_AddToStorage */
				slot->ammo = slot->nextAmmo;
				if (slot->ammo)
					slot->ammoLeft = slot->ammo->ammo;
				slot->installationTime = slot->item->craftitem.installationTime;
				slot->nextItem = nullptr;
				slot->nextAmmo = nullptr;
			} else {
				slot->item = nullptr;
				/* make sure nextAmmo is removed too
				 * virtual ammos cannot be removed without the weapon itself */
				slot->ammo = nullptr;
				slot->ammoLeft = 0;
				slot->installationTime = 0;
			}
		}
	}
}

/**
 * @brief Cancel replacing item, move nextItem (or optionally its ammo only) back to the base storage.
 * @param[in] base The base to add the item to (may be nullptr if item shouldn't be removed from any base).
 * @param[in] slot The slot to remove the item from.
 * @param[in] ammo true if we want to remove only ammo. false if the whole item should be removed.
 * @sa AII_AddItemToSlot
 * @sa AII_AddAmmoToSlot
 * @sa AII_RemoveItemFromSlot
 */
void AII_RemoveNextItemFromSlot (base_t* base, aircraftSlot_t* slot, bool ammo)
{
	assert(slot);

	if (ammo) {
		/* only remove the ammo */
		if (slot->nextAmmo) {
			if (!slot->nextAmmo->isVirtual) {
				if (base)
					B_AddToStorage(base, slot->nextAmmo, 1);
				slot->nextAmmo = nullptr;
			}
		}
	} else if (slot->nextItem) {
		/* Remove nextItem */
		if (base)
			B_AddToStorage(base, slot->nextItem, 1);
		slot->nextItem = nullptr;
		/* also remove ammo if any */
		if (slot->nextAmmo)
			AII_RemoveNextItemFromSlot(base, slot, true);
		/* make sure nextAmmo is removed too
		 * virtual ammos cannot be removed without the weapon itself */
		slot->nextAmmo = nullptr;
	}
}

/**
 * @brief Reloads an aircraft/defence-system weapon
 * @param[in,out] slot Pointer to the aircraftSlot where weapon is attached to
 * @returns true on success - if the weapon was reloaded OR it didn't need reload
 */
bool AII_ReloadWeapon (aircraftSlot_t* slot)
{
	assert(slot);

	/* no weapon assigned - failure */
	if (!slot->item)
		return false;
	/* no ammo assigned - try adding one */
	if (!slot->ammo)
		AII_AutoAddAmmo(slot);
	/* still no ammo assigned - failure */
	if (!slot->ammo)
		return false;
	/* no reload needed - success */
	if (slot->ammoLeft >= slot->ammo->ammo)
		return true;

	if (slot->aircraft) {
		/* PHALANX aircraft and UFO crafts */
		if (AIR_IsUFO(slot->aircraft)) {
			/* UFO - can always be reloaded */
			slot->ammoLeft = slot->ammo->ammo;
			slot->delayNextShot = slot->ammo->craftitem.weaponDelay * UFO_RELOAD_DELAY_MULTIPLIER;
		} else {
			/* PHALANX aircraft */
			/* not at home */
			if (!AIR_IsAircraftInBase(slot->aircraft))
				return false;
			/* no more ammo available */
			if (!B_BaseHasItem(slot->aircraft->homebase, slot->ammo)) {
				slot->ammo = nullptr;
				AII_UpdateAircraftStats(slot->aircraft);
				return false;
			}

			B_AddToStorage(slot->aircraft->homebase, slot->ammo, -1);
			slot->ammoLeft = slot->ammo->ammo;
			slot->delayNextShot = slot->ammo->craftitem.weaponDelay * AIRCRAFT_RELOAD_DELAY_MULTIPLIER;
			AII_UpdateAircraftStats(slot->aircraft);
		}
	} else if (slot->base) {
		/* Base Defence weapons */
		/* no more ammo available */
		if (!B_BaseHasItem(slot->base, slot->ammo))
			return false;

		B_AddToStorage(slot->base, slot->ammo, -1);
		slot->ammoLeft = slot->ammo->ammo;
		slot->delayNextShot = slot->ammo->craftitem.weaponDelay * BASE_RELOAD_DELAY_MULTIPLIER;
	} else if (slot->installation) {
		/* Installations (SAM-Sites) - can always be reloaded. */
		slot->ammoLeft = slot->ammo->ammo;
		slot->delayNextShot = slot->ammo->craftitem.weaponDelay * INSTALLATION_RELOAD_DELAY_MULTIPLIER;
	} else {
		cgi->Com_Error(ERR_DROP, "AII_ReloadWeapon: AircraftSlot not linked anywhere (aircraft/base/installation)!\n");
	}
	return true;
}

/**
 * @brief Reload the weapons of an aircraft
 * @param[in,out] aircraft Pointer to the aircraft to reload
 */
void AII_ReloadAircraftWeapons (aircraft_t* aircraft)
{
	assert(aircraft);
	/* Reload all ammos of aircraft */
	for (int i = 0; i < aircraft->maxWeapons; i++) {
		AII_ReloadWeapon(&aircraft->weapons[i]);
	}
}

/**
 * @brief Add an ammo to an aircraft weapon slot
 * @note No check for the _type_ of item is done here, so it must be done before.
 * @param[in] base Pointer to the base which provides items (nullptr if items shouldn't be removed of storage)
 * @param[in] tech Pointer to the tech to add to slot
 * @param[in] slot Pointer to the slot where you want to add ammos
 * @sa AII_AddItemToSlot
 */
bool AII_AddAmmoToSlot (base_t* base, const technology_t* tech, aircraftSlot_t* slot)
{
	if (slot == nullptr || slot->item == nullptr)
		return false;

	assert(tech);

	const objDef_t* ammo = INVSH_GetItemByID(tech->provides);
	if (!ammo) {
		cgi->Com_Printf("AII_AddAmmoToSlot: Could not add item (%s) to slot\n", tech->provides);
		return false;
	}

	const objDef_t* item = (slot->nextItem) ? slot->nextItem : slot->item;

	/* Is the ammo is usable with the slot */
	int k;
	for (k = 0; k < item->numAmmos; k++) {
		const objDef_t* usable = item->ammos[k];
		if (usable && ammo->idx == usable->idx)
			break;
	}
	if (k >= item->numAmmos)
		return false;

	/* the base pointer can be null here - e.g. in case you are equipping a UFO
	 * and base ammo defence are not stored in storage */
	if (base && ammo->craftitem.type <= AC_ITEM_AMMO) {
		if (!B_BaseHasItem(base, ammo)) {
			cgi->Com_Printf("AII_AddAmmoToSlot: No more ammo of this type to equip (%s)\n", ammo->id);
			return false;
		}
	}

	/* remove any applied ammo in the current slot */
	if (slot->nextItem) {
		if (slot->nextAmmo)
			AII_RemoveNextItemFromSlot(base, slot, true);
		/* ammo couldn't be removed (maybe it's virtual) */
		if (slot->nextAmmo)
			return false;
		slot->nextAmmo = ammo;
	} else {
		/* you shouldn't be able to have nextAmmo set if you don't have nextItem set */
		assert(!slot->nextAmmo);
		AII_RemoveItemFromSlot(base, slot, true);
		/* ammo couldn't be removed (maybe it's virtual) */
		if (slot->ammo)
			return false;
		slot->ammo = ammo;
	}

	/* proceed only if we are changing ammo of current weapon */
	if (slot->nextItem) {
		/* the base pointer can be null here - e.g. in case you are equipping a UFO */
		if (base)
			B_AddToStorage(base, ammo, -1);
		return true;
	}
	AII_ReloadWeapon(slot);

	return true;
}

/**
 * @brief Add an item to an aircraft slot
 * @param[in] base Pointer to the base where item will be removed (nullptr for ufos, virtual ammos or while loading game)
 * @param[in] tech Pointer to the tech that will be added in this slot.
 * @param[in] slot Pointer to the aircraft, base, or installation slot.
 * @param[in] nextItem False if we are changing current item in slot, true if this is the item to install
 * after current removal is over.
 * @note No check for the _type_ of item is done here.
 * @sa AII_UpdateOneInstallationDelay
 * @sa AII_AddAmmoToSlot
 */
bool AII_AddItemToSlot (base_t* base, const technology_t* tech, aircraftSlot_t* slot, bool nextItem)
{
	const objDef_t* item;

	assert(slot);
	assert(tech);

	item = INVSH_GetItemByID(tech->provides);
	if (!item) {
		cgi->Com_Printf("AII_AddItemToSlot: Could not add item (%s) to slot\n", tech->provides);
		return false;
	}

#ifdef DEBUG
	/* Sanity check : the type of the item cannot be an ammo */
	/* note that this should never be reached because a slot type should never be an ammo
	 * , so the test just before should be wrong */
	if (item->craftitem.type >= AC_ITEM_AMMO) {
		cgi->Com_Printf("AII_AddItemToSlot: Type of the item to install (%s) should be a weapon, a shield, or electronics (no ammo)\n", item->id);
		return false;
	}
#endif

	/* Sanity check : the type of the item should be the same than the slot type */
	if (slot->type != item->craftitem.type) {
		cgi->Com_Printf("AII_AddItemToSlot: Type of the item to install (%s -- %i) doesn't match type of the slot (%i)\n", item->id, item->craftitem.type, slot->type);
		return false;
	}

	/* the base pointer can be null here - e.g. in case you are equipping a UFO */
	if (base && !B_BaseHasItem(base, item)) {
		cgi->Com_Printf("AII_AddItemToSlot: No more item of this type to equip (%s)\n", item->id);
		return false;
	}

	if (slot->size >= AII_GetItemWeightBySize(item)) {
		if (nextItem)
			slot->nextItem = item;
		else {
			slot->item = item;
			slot->installationTime = item->craftitem.installationTime;
		}
		/* the base pointer can be null here - e.g. in case you are equipping a UFO
		 * Remove item even for nextItem, this way we are sure we won't use the same item
		 * for another aircraft. */
		if (base)
			B_AddToStorage(base, item, -1);
	} else {
		cgi->Com_Printf("AII_AddItemToSlot: Could not add item '%s' to slot %i (slot-size: %i - item-weight: %i)\n",
			item->id, slot->idx, slot->size, item->size);
		return false;
	}

	return true;
}

/**
 * @brief Auto Add weapon and ammo to an aircraft.
 * @param[in] aircraft Pointer to the aircraft
 * @note This is used to auto equip interceptor of first base.
 * @sa B_SetUpBase
 */
void AIM_AutoEquipAircraft (aircraft_t* aircraft)
{
	int i;
	const objDef_t* item;
	/** @todo Eliminate hardcoded techs here. */
	const technology_t* tech = RS_GetTechByID("rs_craft_weapon_sparrowhawk");

	if (!tech)
		cgi->Com_Error(ERR_DROP, "Could not get tech rs_craft_weapon_sparrowhawk");

	assert(aircraft);
	assert(aircraft->homebase);

	item = INVSH_GetItemByID(tech->provides);
	if (!item)
		return;

	for (i = 0; i < aircraft->maxWeapons; i++) {
		aircraftSlot_t* slot = &aircraft->weapons[i];
		if (slot->size < AII_GetItemWeightBySize(item))
			continue;
		if (!B_BaseHasItem(aircraft->homebase, item))
			continue;
		AII_AddItemToSlot(aircraft->homebase, tech, slot, false);
		AII_AutoAddAmmo(slot);
		slot->installationTime = 0;
	}

	/* Fill slots too small for sparrowhawk with shiva */
	tech = RS_GetTechByID("rs_craft_weapon_shiva");

	if (!tech)
		cgi->Com_Error(ERR_DROP, "Could not get tech rs_craft_weapon_shiva");

	item = INVSH_GetItemByID(tech->provides);

	if (!item)
		return;

	for (i = 0; i < aircraft->maxWeapons; i++) {
		aircraftSlot_t* slot = &aircraft->weapons[i];
		if (slot->size < AII_GetItemWeightBySize(item))
			continue;
		if (!B_BaseHasItem(aircraft->homebase, item))
			continue;
		if (slot->item)
			continue;
		AII_AddItemToSlot(aircraft->homebase, tech, slot, false);
		AII_AutoAddAmmo(slot);
		slot->installationTime = 0;
	}

	AII_UpdateAircraftStats(aircraft);
}

/**
 * @brief Initialise values of one slot of an aircraft or basedefence common to all types of items.
 * @param[in] slot	Pointer to the slot to initialize.
 * @param[in] aircraftTemplate Template	Pointer to aircraft template.
 * @param[in] base	Pointer to base.
 * @param[in] installation Pointer to the thing being installed.
 * @param[in] type The type of item that can fit in this slot.
 */
void AII_InitialiseSlot (aircraftSlot_t* slot, aircraft_t* aircraftTemplate, base_t* base, installation_t* installation, aircraftItemType_t type)
{
	/* Only one of them is allowed. */
	assert((!base && aircraftTemplate) || (base && !aircraftTemplate) || (installation && !aircraftTemplate));
	/* Only one of them is allowed or neither. */
	assert((!base && installation) || (base && !installation) || (!base && !installation));

	OBJZERO(*slot);
	slot->aircraft = aircraftTemplate;
	slot->base = base;
	slot->installation = installation;
	slot->item = nullptr;
	slot->ammo = nullptr;
	slot->nextAmmo = nullptr;
	slot->size = ITEM_HEAVY;
	slot->nextItem = nullptr;
	slot->type = type;
	/** sa BDEF_AddBattery: it needs to be AMMO_STATUS_NOT_SET and not 0 @sa B_SaveBaseSlots */
	slot->ammoLeft = AMMO_STATUS_NOT_SET;
	slot->installationTime = 0;
}

/**
 * @brief Check if item in given slot should change one aircraft stat.
 * @param[in] slot Pointer to the slot containing the item
 * @param[in] stat the stat that should be checked
 * @return true if the item should change the stat.
 */
static bool AII_CheckUpdateAircraftStats (const aircraftSlot_t* slot, int stat)
{
	assert(slot);

	/* there's no item */
	if (!slot->item)
		return false;

	/* you can not have advantages from items if it is being installed or removed, but only disavantages */
	if (slot->installationTime != 0) {
		const objDef_t* item = slot->item;
		if (item->craftitem.stats[stat] > 1.0f) /* advantages for relative and absolute values */
			return false;
	}

	return true;
}

/**
 * @brief returns the aircraftSlot of a base at an index or the first free slot
 * @param[in] base Pointer to base
 * @param[in] type defence type, see aircraftItemType_t
 * @param[in] idx index of aircraftslot
 * @returns the aircraftSlot at the index idx if idx non-negative the first free slot otherwise
 */
aircraftSlot_t* BDEF_GetBaseSlotByIDX (base_t* base, aircraftItemType_t type, int idx)
{
	switch (type) {
	case AC_ITEM_BASE_MISSILE:
		if (idx < 0) {			/* returns the first free slot on negative */
			int i;
			for (i = 0; i < base->numBatteries; i++) {
				aircraftSlot_t* slot = &base->batteries[i].slot;
				if (!slot->item && !slot->nextItem)
					return slot;
			}
		} else if (idx < base->numBatteries)
			return &base->batteries[idx].slot;
		break;
	case AC_ITEM_BASE_LASER:
		if (idx < 0) {			/* returns the first free slot on negative */
			int i;
			for (i = 0; i < base->numLasers; i++) {
				aircraftSlot_t* slot = &base->lasers[i].slot;
				if (!slot->item && !slot->nextItem)
					return slot;
			}
		} else if (idx < base->numLasers)
			return &base->lasers[idx].slot;
		break;
	default:
		break;
	}
	return nullptr;
}

/**
 * @brief returns the aircraftSlot of an installaion at an index or the first free slot
 * @param[in] installation Pointer to the installation
 * @param[in] type defence type, see aircraftItemType_t
 * @param[in] idx index of aircraftslot
 * @returns the aircraftSlot at the index idx if idx non-negative the first free slot otherwise
 */
aircraftSlot_t* BDEF_GetInstallationSlotByIDX (installation_t* installation, aircraftItemType_t type, int idx)
{
	switch (type) {
	case AC_ITEM_BASE_MISSILE:
		if (idx < 0) {			/* returns the first free slot on negative */
			for (int i = 0; i < installation->numBatteries; i++) {
				aircraftSlot_t* slot = &installation->batteries[i].slot;
				if (!slot->item && !slot->nextItem)
					return slot;
			}
		} else if (idx < installation->numBatteries)
			return &installation->batteries[idx].slot;
		break;
	default:
		break;
	}
	return nullptr;
}

/**
 * @brief returns the aircraftSlot of an aircraft at an index or the first free slot
 * @param[in] aircraft Pointer to aircraft
 * @param[in] type base defence type, see aircraftItemType_t
 * @param[in] idx index of aircraftslot
 * @returns the aircraftSlot at the index idx if idx non-negative the first free slot otherwise
 */
aircraftSlot_t* AII_GetAircraftSlotByIDX (aircraft_t* aircraft, aircraftItemType_t type, int idx)
{
	switch (type) {
	case AC_ITEM_WEAPON:
		if (idx < 0) {			/* returns the first free slot on negative */
			for (int i = 0; i < aircraft->maxWeapons; i++) {
				aircraftSlot_t* slot = &aircraft->weapons[i];
				if (!slot->item && !slot->nextItem)
					return slot;
			}
		} else if (idx < aircraft->maxWeapons)
			return &aircraft->weapons[idx];
		break;
	case AC_ITEM_SHIELD:
		if (idx == 0 || ((idx < 0) && !aircraft->shield.item && !aircraft->shield.nextItem))	/* returns the first free slot on negative */
			return &aircraft->shield;
		break;
	case AC_ITEM_ELECTRONICS:
		if (idx < 0) {			/* returns the first free slot on negative */
			for (int i = 0; i < aircraft->maxElectronics; i++) {
				aircraftSlot_t* slot = &aircraft->electronics[i];
				if (!slot->item && !slot->nextItem)
					return slot;
			}
		} else if (idx < aircraft->maxElectronics)
			return &aircraft->electronics[idx];
		break;
	default:
		break;
	}
	return nullptr;
}


/**
 * @brief Get the maximum weapon range of aircraft.
 * @param[in] slot Pointer to the aircrafts weapon slot list.
 * @param[in] maxSlot maximum number of weapon slots in aircraft.
 * @return Maximum weapon range for this aircaft as an angle.
 */
float AIR_GetMaxAircraftWeaponRange (const aircraftSlot_t* slot, int maxSlot)
{
	float range = 0.0f;

	assert(slot);

	/* We choose the usable weapon with the biggest range */
	for (int i = 0; i < maxSlot; i++) {
		const aircraftSlot_t* weapon = slot + i;
		const objDef_t* ammo = weapon->ammo;

		if (!ammo)
			continue;

		/* make sure this item is useable */
		if (!AII_CheckUpdateAircraftStats(weapon, AIR_STATS_WRANGE))
			continue;

		/* select this weapon if this is the one with the longest range */
		if (ammo->craftitem.stats[AIR_STATS_WRANGE] > range) {
			range = ammo->craftitem.stats[AIR_STATS_WRANGE];
		}
	}
	return range;
}

/**
 * @brief Repair aircraft.
 * @note Hourly called.
 * @note Repairs 1% of a craft's max damage capacity.
 */
void AII_RepairAircraft (void)
{
	base_t* base = nullptr;

	while ((base = B_GetNext(base)) != nullptr) {
		AIR_ForeachFromBase(aircraft, base) {
			if (!AIR_IsAircraftInBase(aircraft))
				continue;
			const int REPAIR_PER_HOUR = std::max(1, int(round(aircraft->stats[AIR_STATS_DAMAGE] / 100))); /**< Number of damage points repaired per hour: 1% of aircraft max damage value */
			cgi->Com_DPrintf(DEBUG_CLIENT, "Repair per hour: %s %d, %d / %d\n",
				aircraft->name, REPAIR_PER_HOUR, aircraft->damage, aircraft->stats[AIR_STATS_DAMAGE]);
			aircraft->damage = std::min(aircraft->damage + REPAIR_PER_HOUR, aircraft->stats[AIR_STATS_DAMAGE]);
		}
	}
}

/**
 * @brief Update the value of stats array of an aircraft.
 * @param[in] aircraft Pointer to the aircraft
 * @note This should be called when an item starts to be added/removed and when addition/removal is over.
 */
void AII_UpdateAircraftStats (aircraft_t* aircraft)
{
	assert(aircraft);

	const aircraft_t* source = aircraft->tpl;

	for (int currentStat = 0; currentStat < AIR_STATS_MAX; currentStat++) {
		/* we scan all the stats except AIR_STATS_WRANGE (see below) */
		if (currentStat == AIR_STATS_WRANGE)
			continue;

		/* initialise value */
		aircraft->stats[currentStat] = source->stats[currentStat];

		/* modify by electronics (do nothing if the value of stat is 0) */
		for (int i = 0; i < aircraft->maxElectronics; i++) {
			const aircraftSlot_t* slot = &aircraft->electronics[i];
			const objDef_t* item;
			if (!AII_CheckUpdateAircraftStats(slot, currentStat))
				continue;
			item = slot->item;
			if (fabs(item->craftitem.stats[currentStat]) > 2.0f)
				aircraft->stats[currentStat] += (int) item->craftitem.stats[currentStat];
			else if (!EQUAL(item->craftitem.stats[currentStat], 0))
				aircraft->stats[currentStat] *= item->craftitem.stats[currentStat];
		}

		/* modify by weapons (do nothing if the value of stat is 0)
		 * note that stats are not modified by ammos */
		for (int i = 0; i < aircraft->maxWeapons; i++) {
			const aircraftSlot_t* slot = &aircraft->weapons[i];
			const objDef_t* item;
			if (!AII_CheckUpdateAircraftStats(slot, currentStat))
				continue;
			item = slot->item;
			if (fabs(item->craftitem.stats[currentStat]) > 2.0f)
				aircraft->stats[currentStat] += item->craftitem.stats[currentStat];
			else if (!EQUAL(item->craftitem.stats[currentStat], 0))
				aircraft->stats[currentStat] *= item->craftitem.stats[currentStat];
		}

		/* modify by shield (do nothing if the value of stat is 0) */
		if (AII_CheckUpdateAircraftStats(&aircraft->shield, currentStat)) {
			const objDef_t* item = aircraft->shield.item;
			if (fabs(item->craftitem.stats[currentStat]) > 2.0f)
				aircraft->stats[currentStat] += item->craftitem.stats[currentStat];
			else if (!EQUAL(item->craftitem.stats[currentStat], 0))
				aircraft->stats[currentStat] *= item->craftitem.stats[currentStat];
		}
	}

	/* now we update AIR_STATS_WRANGE (this one is the biggest range of every ammo) */
	aircraft->stats[AIR_STATS_WRANGE] = 1000.0f * AIR_GetMaxAircraftWeaponRange(aircraft->weapons, aircraft->maxWeapons);

	/* check that aircraft hasn't too much fuel (caused by removal of fuel pod) */
	if (aircraft->fuel > aircraft->stats[AIR_STATS_FUELSIZE])
		aircraft->fuel = aircraft->stats[AIR_STATS_FUELSIZE];

	/* check that aircraft hasn't too much HP (caused by removal of armour) */
	if (aircraft->damage > aircraft->stats[AIR_STATS_DAMAGE])
		aircraft->damage = aircraft->stats[AIR_STATS_DAMAGE];

	/* check that speed of the aircraft is positive */
	if (aircraft->stats[AIR_STATS_SPEED] < 1)
		aircraft->stats[AIR_STATS_SPEED] = 1;

	/* Update aircraft state if needed */
	if (aircraft->status == AIR_HOME && aircraft->fuel < aircraft->stats[AIR_STATS_FUELSIZE])
		aircraft->status = AIR_REFUEL;
}

/**
 * @brief Check if base or installation weapon can shoot
 * @param[in] weapons Pointer to the weapon array of the base.
 * @param[in] numWeapons Pointer to the number of weapon in this base.
 * @return true if the base can fight, false else
 * @sa AII_BaseCanShoot
 */
static bool AII_WeaponsCanShoot (const baseWeapon_t* weapons, int numWeapons)
{
	for (int i = 0; i < numWeapons; i++) {
		if (AIRFIGHT_CheckWeapon(&weapons[i].slot, 0) != AIRFIGHT_WEAPON_CAN_NEVER_SHOOT)
			return true;
	}

	return false;
}

/**
 * @brief Check if the base has weapon and ammo
 * @param[in] base Pointer to the base you want to check (may not be nullptr)
 * @return true if the base can shoot, qflase else
 * @sa AII_AircraftCanShoot
 */
int AII_BaseCanShoot (const base_t* base)
{
	assert(base);

	/* If we can shoot with missile defences */
	if (B_GetBuildingStatus(base, B_DEFENCE_MISSILE)
	 && AII_WeaponsCanShoot(base->batteries, base->numBatteries))
		return true;

	/* If we can shoot with beam defences */
	if (B_GetBuildingStatus(base, B_DEFENCE_LASER)
	 && AII_WeaponsCanShoot(base->lasers, base->numLasers))
		return true;

	return false;
}

/**
 * @brief Check if the installation has a weapon and ammo
 * @param[in] installation Pointer to the installation you want to check (may not be nullptr)
 * @return true if the installation can shoot, qflase else
 * @sa AII_AircraftCanShoot
 */
bool AII_InstallationCanShoot (const installation_t* installation)
{
	assert(installation);

	if (installation->installationStatus == INSTALLATION_WORKING
	 && installation->installationTemplate->maxBatteries > 0) {
		/* installation is working and has battery */
		return AII_WeaponsCanShoot(installation->batteries, installation->installationTemplate->maxBatteries);
	}

	return false;
}

/**
 * @brief Chooses a target for surface to air defences automatically
 * @param[in,out] weapons Weapons array
 * @param[in] maxWeapons Number of weapons
 */
static void BDEF_AutoTarget (baseWeapon_t* weapons, int maxWeapons)
{
	if (maxWeapons <= 0)
		return;

	const installation_t* inst;
	const base_t* base;
	const aircraftSlot_t* slot = &weapons[0].slot;
	/* Check if it's a Base or an Installation */
	if (slot->installation) {
		inst = slot->installation;
		base = nullptr;
	} else if (slot->base) {
		base = slot->base;
		inst = nullptr;
	} else
		cgi->Com_Error(ERR_DROP, "BDEF_AutoSelectTarget: slot doesn't belong to any base or installation");

	/* Get closest UFO(s) */
	aircraft_t* closestCraft = nullptr;
	float minCraftDistance = -1;
	aircraft_t* closestAttacker = nullptr;
	float minAttackerDistance = -1;
	aircraft_t* ufo = nullptr;
	while ((ufo = UFO_GetNextOnGeoscape(ufo)) != nullptr) {
		const float distance = GetDistanceOnGlobe(inst ? inst->pos : base->pos, ufo->pos);
		if (minCraftDistance < 0 || minCraftDistance > distance) {
			minCraftDistance = distance;
			closestCraft = ufo;
		}
		if ((minAttackerDistance < 0 || minAttackerDistance > distance) && ufo->mission
		 && ((base && ufo->mission->category == INTERESTCATEGORY_BASE_ATTACK && ufo->mission->data.base == base)
		 || (inst && ufo->mission->category == INTERESTCATEGORY_INTERCEPT && ufo->mission->data.installation == inst))) {
			minAttackerDistance = distance;
			closestAttacker = ufo;
		}
	}

	/* Loop weaponslots */
	for (int i = 0; i < maxWeapons; i++) {
		baseWeapon_t* weapon  = &weapons[i];
		slot = &weapon->slot;
		/* skip if autofire is disabled */
		if (!weapon->autofire)
			continue;
		/* skip if no weapon or ammo assigned */
		if (!slot->item || !slot->ammo)
			continue;
		/* skip if weapon installation not yet finished */
		if (slot->installationTime > 0)
			continue;
		/* skip if no more ammo left */
		/** @note it's not really needed but it's cheaper not to check ufos in this case */
		if (slot->ammoLeft <= 0)
			continue;

		if (closestAttacker) {
			const int test = AIRFIGHT_CheckWeapon(slot, minAttackerDistance);
			if (test != AIRFIGHT_WEAPON_CAN_NEVER_SHOOT
			 && test != AIRFIGHT_WEAPON_CAN_NOT_SHOOT_AT_THE_MOMENT
			 && (minAttackerDistance <= slot->ammo->craftitem.stats[AIR_STATS_WRANGE]))
				weapon->target = closestAttacker;
		} else if (closestCraft) {
			const int test = AIRFIGHT_CheckWeapon(slot, minCraftDistance);
			if (test != AIRFIGHT_WEAPON_CAN_NEVER_SHOOT
			 && test != AIRFIGHT_WEAPON_CAN_NOT_SHOOT_AT_THE_MOMENT
			 && (minCraftDistance <= slot->ammo->craftitem.stats[AIR_STATS_WRANGE]))
				weapon->target = closestCraft;
		}
	}
}

/**
 * @brief Chooses target for all base defences and sam sites
 */
void BDEF_AutoSelectTarget (void)
{
	base_t* base;

	base = nullptr;
	while ((base = B_GetNext(base)) != nullptr) {
		BDEF_AutoTarget(base->batteries, base->numBatteries);
		BDEF_AutoTarget(base->lasers, base->numLasers);
	}

	INS_Foreach(inst)
		BDEF_AutoTarget(inst->batteries, inst->numBatteries);
}

/**
 * @brief Translate a weight int to a translated string
 * @sa itemWeight_t
 * @sa AII_GetItemWeightBySize
 */
const char* AII_WeightToName (itemWeight_t weight)
{
	switch (weight) {
	case ITEM_LIGHT:
		return _("Light weight");
		break;
	case ITEM_MEDIUM:
		return _("Medium weight");
		break;
	case ITEM_HEAVY:
		return _("Heavy weight");
		break;
	default:
		return _("Unknown weight");
		break;
	}
}

/**
 * @brief Save callback for savegames in XML Format
 * @param[out] p XML Node structure, where we write the information to
 * @param[in] slot The aircraftslot to save
 * @param[in] weapon True if this is a weapon slot
 */
void AII_SaveOneSlotXML (xmlNode_t* p, const aircraftSlot_t* slot, bool weapon)
{
	cgi->XML_AddStringValue(p, SAVE_SLOT_ITEMID, slot->item ? slot->item->id : "");
	cgi->XML_AddStringValue(p, SAVE_SLOT_NEXTITEMID, slot->nextItem ? slot->nextItem->id : "");
	cgi->XML_AddIntValue(p, SAVE_SLOT_INSTALLATIONTIME, slot->installationTime);

	/* everything below is only for weapon */
	if (!weapon)
		return;

	cgi->XML_AddIntValue(p, SAVE_SLOT_AMMOLEFT, slot->ammoLeft);
	cgi->XML_AddStringValue(p, SAVE_SLOT_AMMOID, slot->ammo ? slot->ammo->id : "");
	cgi->XML_AddStringValue(p, SAVE_SLOT_NEXTAMMOID, slot->nextAmmo ? slot->nextAmmo->id : "");
	cgi->XML_AddIntValue(p, SAVE_SLOT_DELAYNEXTSHOT, slot->delayNextShot);
}

/**
 * @brief Loads one slot (base, installation or aircraft)
 * @param[in] node XML Node structure, where we get the information from
 * @param[out] slot Pointer to the slot where item should be added.
 * @param[in] weapon True if the slot is a weapon slot.
 * @sa B_Load
 * @sa B_SaveAircraftSlots
 */
void AII_LoadOneSlotXML (xmlNode_t* node, aircraftSlot_t* slot, bool weapon)
{
	const char* name;
	name = cgi->XML_GetString(node, SAVE_SLOT_ITEMID);
	if (name[0] != '\0') {
		const technology_t* tech = RS_GetTechByProvided(name);
		/* base is nullptr here to not check against the storage amounts - they
		* are already loaded in the campaign load function and set to the value
		* after the craftitem was already removed from the initial game - thus
		* there might not be any of these items in the storage at this point.
		* Furthermore, they have already be taken from storage during game. */
		if (tech)
			AII_AddItemToSlot(nullptr, tech, slot, false);
	}

	/* item to install after current one is removed */
	name = cgi->XML_GetString(node, SAVE_SLOT_NEXTITEMID);
	if (name && name[0] != '\0') {
		const technology_t* tech = RS_GetTechByProvided(name);
		if (tech)
			AII_AddItemToSlot(nullptr, tech, slot, true);
	}

	slot->installationTime = cgi->XML_GetInt(node, SAVE_SLOT_INSTALLATIONTIME, 0);

	/* everything below is weapon specific */
	if (!weapon)
		return;

	/* current ammo */
	/* load ammoLeft before adding ammo to avoid unnecessary auto-reloading */
	slot->ammoLeft = cgi->XML_GetInt(node, SAVE_SLOT_AMMOLEFT, 0);
	name = cgi->XML_GetString(node, SAVE_SLOT_AMMOID);
	if (name && name[0] != '\0') {
		const technology_t* tech = RS_GetTechByProvided(name);
		/* next Item must not be loaded yet in order to install ammo properly */
		if (tech)
			AII_AddAmmoToSlot(nullptr, tech, slot);
	}
	/* ammo to install after current one is removed */
	name = cgi->XML_GetString(node, SAVE_SLOT_NEXTAMMOID);
	if (name && name[0] != '\0') {
		const technology_t* tech = RS_GetTechByProvided(name);
		if (tech)
			AII_AddAmmoToSlot(nullptr, tech, slot);
	}
	slot->delayNextShot = cgi->XML_GetInt(node, SAVE_SLOT_DELAYNEXTSHOT, 0);
}
