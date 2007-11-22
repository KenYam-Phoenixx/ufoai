/**
 * @file cl_mapfightequip.h
 * @brief Header for slot management related stuff.
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

#ifndef CLIENT_CL_MAPFIGHTEQUIP_H
#define CLIENT_CL_MAPFIGHTEQUIP_H

/**
 * @brief Zone number in airequip menu or base defense menu.
 * @note A zone is the rectangular box in the upper right of the screen.
 * @note A zone is different from a slot or the type of an item.
 */
typedef enum {
	ZONE_NONE,	/**< This is not a zone (used when no zone is selected). */
	ZONE_MAIN,	/**< This is the upper (1st) zone containing the current item installed in the slot. */
	ZONE_NEXT,	/**< This is the zone in the middle (2nd) containing the item to install when the item
				 ** in ZONE_MAIN will be removed (if we ask for removing it). */
	ZONE_AMMO,	/**< This is the lowest (3rd) zone containing the ammo fitting the weapon in ZONE_MAIN
				 ** Therefore, this zone should only be used when 1st zone contain an WEAPON. */

	ZONE_MAX
} zoneaircraftParams_t;

/**
 * @brief The different possible types of base defense systems.
 * @sa BDEF_RemoveBattery_f: BASEDEF_LASER must be just after BASEDEF_MISSILE
 */
typedef enum {
	BASEDEF_RANDOM,		/**< The base defense system should be randomly selected. */
	BASEDEF_MISSILE,	/**< The base defense system is a missile battery. */
	BASEDEF_LASER,		/**< The base defense system is a laser battery. */

	BASEDEF_MAX
} basedefenseType_t;

/** Base defense functions. */
void BDEF_AddBattery_f(void);
void BDEF_RemoveBattery(base_t *base, basedefenseType_t basedefType, int idx);
void BDEF_RemoveBattery_f(void);
void BDEF_InitialiseBaseSlots(base_t *base);
void BDEF_MenuInit_f(void);
void BDEF_BaseDefenseMenuUpdate_f(void);
void BDEF_ListClick_f(void);
void BDEF_ReloadBattery(void);

void AII_UpdateInstallationDelay(void);
void AIM_AircraftEquipMenuUpdate_f(void);
void AIM_AircraftEquipSlotSelect_f(void);
void AIM_AircraftEquipZoneSelect_f(void);
qboolean AII_AddItemToSlot(base_t* base, technology_t *tech, aircraftSlot_t *slot);
qboolean AII_AddAmmoToSlot(base_t* base, technology_t *tech, aircraftSlot_t *slot);
void AII_RemoveItemFromSlot(base_t* base, aircraftSlot_t *slot, qboolean ammo);
void AIM_AircraftEquipAddItem_f(void);
void AIM_AircraftEquipDeleteItem_f(void);
void AIM_AircraftEquipMenuClick_f(void);
void AII_InitialiseSlot(aircraftSlot_t *slot, int aircraftIdx, aircraftItemType_t type);
void AII_UpdateAircraftStats(aircraft_t *aircraft);
int AII_GetSlotItems(aircraftItemType_t type, aircraft_t *aircraft);
int AII_AircraftCanShoot(aircraft_t *aircraft);
int AII_BaseCanShoot(const base_t *base);

itemWeight_t AII_GetItemWeightBySize(objDef_t *od);

const char* AII_WeightToName(itemWeight_t weight);

#endif /* CLIENT_CL_MAPFIGHTEQUIP_H */
