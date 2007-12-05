/**
 * @file cl_basemanagement.h
 * @brief Header for base management related stuff.
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

#ifndef CLIENT_CL_BASEMANGEMENT_H
#define CLIENT_CL_BASEMANGEMENT_H

#define MAX_LIST_CHAR		1024
#define MAX_BUILDINGS		256
#define MAX_BASES		8

#define MAX_BATTERY_DAMAGE	50
#define MAX_BASE_DAMAGE		100
#define MAX_BASE_SLOT		4

/** @todo take the values from scriptfile */
#define BASEMAP_SIZE_X		778.0
#define BASEMAP_SIZE_Y		672.0

/* see MAX_TILESTRINGS */
#define BASE_SIZE		5

#define MAX_EMPLOYEES_IN_BUILDING 64

#define BASE_FREESLOT		-1
/* you can't place buildings here */
#define BASE_INVALID_SPACE	-2

/**
 * @brief Possible base states
 * @note: Don't change the order or you have to change the basemenu scriptfiles, too
 */
typedef enum {
	BASE_NOT_USED,
	BASE_UNDER_ATTACK,	/**< base is under attack */
	BASE_WORKING		/**< nothing special */
} baseStatus_t;

/** @brief All possible base actions */
typedef enum {
	BA_NONE,
	BA_NEWBUILDING,		/**< hovering the needed base tiles for this building */

	BA_MAX
} baseAction_t;

/** @brief All possible building status. */
typedef enum {
	B_STATUS_NOT_SET,			/**< not build yet */
	B_STATUS_UNDER_CONSTRUCTION,	/**< right now under construction */
	B_STATUS_CONSTRUCTION_FINISHED,	/**< construction finished - no workers assigned */
	/* and building needs workers */
	B_STATUS_WORKING,			/**< working normal (or workers assigned when needed) */
	B_STATUS_DOWN				/**< totally damaged */
} buildingStatus_t;

/** @brief All different building types.
 * @note if you change the order, you'll load values of hasBuilding in wrong indice */
typedef enum {
	B_MISC,			/**< this building is nothing with a special function (used when a building appears twice in .ufo file) */
	B_LAB,			/**< this building is a lab */
	B_QUARTERS,		/**< this building is a quarter */
	B_STORAGE,		/**< this building is a storage */
	B_WORKSHOP,		/**< this building is a workshop */
	B_HOSPITAL,		/**< this building is a hospital */
	B_HANGAR,		/**< this building is a hangar */
	B_ALIEN_CONTAINMENT,	/**< this building is an alien containment */
	B_SMALL_HANGAR,		/**< this building is a small hangar */
	B_UFO_HANGAR,		/**< this building is a UFO hangar */
	B_UFO_SMALL_HANGAR,	/**< this building is a small UFO hangar */
	B_POWER,		/**< this building is power plant */
	B_COMMAND,		/**< this building is command centre */
	B_ANTIMATTER,		/**< this building is antimatter storage */
	B_ENTRANCE,		/**< this building is an entrance */
	B_DEFENSE_MISSILE,		/**< this building is a missile rack */
	B_DEFENSE_LASER,		/**< this building is a laser battery */
	B_RADAR,			/**< this building is a radar */
	B_TEAMROOM,			/**< this building is a Team Room */

	MAX_BUILDING_TYPE
} buildingType_t;

/** @brief All possible capacities in base. */
typedef enum {
	CAP_ALIENS,		/**< Alive aliens stored in base. */
	CAP_AIRCRAFTS_SMALL,	/**< Small aircraft in base. */
	CAP_AIRCRAFTS_BIG,	/**< Big aircraft in base. */
	CAP_EMPLOYEES,		/**< Personel in base. */
	CAP_ITEMS,		/**< Items in base. */
	CAP_LABSPACE,		/**< Space for scientists in laboratory. */
	CAP_WORKSPACE,		/**< Space for workers in workshop. */
	CAP_HOSPSPACE,		/**< Space for hurted people in hospital. */
	CAP_UFOHANGARS,		/**< Space for recovered UFOs. */
	CAP_ANTIMATTER,		/**< Space for Antimatter Storage. */

	MAX_CAP
} baseCapacities_t;

/** @brief Store capacities in base. */
typedef struct cap_maxcur_s {
	int max;		/**< Maximum capacity. */
	int cur;		/**< Currently used capacity. */
} capacities_t;

/** @brief A building with all it's data */
typedef struct building_s {
	int idx;					/**< self link in "buildings" list. */
	int type_idx;				/**< self link in "buildingTypes" list. */
	int base_idx;				/**< Number/index of base this building is located in. */

	char *id;
	char *name;
	char *image, *mapPart, *pedia;
	/** needs determines the second building part */
	char *needs;		/**< if the buildign has a second part */
	int fixCosts, varCosts;

	int timeStart, buildTime;

	buildingStatus_t buildingStatus;	/**< [BASE_SIZE*BASE_SIZE]; */

	/** if we can build more than one building of the same type: */
	qboolean visible;	/**< is this building visible in the building list */
	/** needed for baseassemble
	 * when there are two tiles (like hangar) - we only load the first tile */
	int used;

	/** event handler functions */
	char onConstruct[MAX_VAR];
	char onAttack[MAX_VAR];
	char onDestroy[MAX_VAR];
	char onUpgrade[MAX_VAR];
	char onRepair[MAX_VAR];
	char onClick[MAX_VAR];

	/** more than one building of the same type allowed? */
	int moreThanOne;

	/** position of autobuild */
	vec2_t pos;

	/** autobuild when base is set up */
	qboolean autobuild;

	/** autobuild when base is set up */
	qboolean firstbase;

	/** How many employees to hire on construction in the first base */
	int maxEmployees;

	/** this way we can rename the buildings without loosing the control */
	buildingType_t buildingType;

	int tech;					/**< link to the building-technology */
	/** if the building needs another one to work (= to be buildable) .. links to gd.buildingTypes */
	int dependsBuilding;

	int capacity;		/**< Capacity of this building (used in calculate base capacities). */
} building_t;

/** @brief A base with all it's data */
typedef struct base_s {
	int idx;					/**< Self link. Index in the global base-list. */
	char name[MAX_VAR];			/**< Name of the base */
	int map[BASE_SIZE][BASE_SIZE];	/**< the base maps (holds building ids) */

	qboolean founded;	/**< already founded? */
	vec3_t pos;		/**< pos on geoscape */

	/**
	 * @note These qbooleans does not say whether there is such building in the
	 * base or there is not. They are true only if such buildings are operational
	 * (for example, in some cases, if they are provided with power).
	 */
	qboolean hasBuilding[MAX_BUILDING_TYPE];

	/** this is here to allocate the needed memory for the buildinglist */
	char allBuildingsList[MAX_LIST_CHAR];

	/** mapZone indicated which map to load (grass, desert, arctic,...) */
	const char *mapZone;

	/** all aircraft in this base
	  @todo: make me a linked list (see cl_market.c aircraft selling) */
	aircraft_t aircraft[MAX_AIRCRAFT];
	int numAircraftInBase;	/**< How many aircraft are in this base. */
	int aircraftCurrent;		/**< Index of the currently selected aircraft in this base (NOT a global one). Max is numAircraftInBase-1  */

	int posX[BASE_SIZE][BASE_SIZE];	/**< the x coordinates for the basemap (see map[BASE_SIZE][BASE_SIZE]) */
	int posY[BASE_SIZE][BASE_SIZE];	/**< the y coordinates for the basemap (see map[BASE_SIZE][BASE_SIZE]) */

	baseStatus_t baseStatus; /**< the current base status */

	qboolean isDiscovered;	/** qtrue if the base has been discovered by aliens */

	radar_t	radar;	/**< the onconstruct value of the buliding building_radar increases the sensor width */

	aliensCont_t alienscont[MAX_ALIENCONT_CAP];	/**< alien containment capacity */

	capacities_t capacities[MAX_CAP];		/**< Capacities. */

	equipDef_t storage;	/**< weapons, etc. stored in base */

	inventory_t equipByBuyType;	/**< idEquip sorted by buytype; needen't be saved;
		a hack based on assertion (MAX_CONTAINERS >= BUY_AIRCRAFT) ... see e.g. CL_GenerateEquipment_f */

	int equipType;	/**< the current selected category in equip menu */

	character_t *curChr;	/**< needn't be saved */

	int buyfactor;	/**< Factor for buying items in Buy/Sell menu for this base. */
	int sellfactor;	/**< Factor for selling items in Buy/Sell menu for this base. */

	int hospitalList[MAX_EMPL][MAX_EMPLOYEES];	/**< Hospital list of employees currently being healed. */
	int hospitalListCount[MAX_EMPL];				/**< Counter for hospitalList. */
	int hospitalMissionList[MAX_EMPLOYEES];		/**< Hospital list of soldiers being healed but moved to the mission. */
	int hospitalMissionListCount;			/**< Counter for hospitalMissionList. */

	int maxBatteries;
	aircraftSlot_t batteries[MAX_BASE_SLOT];	/**< Missile batteries assigned to base. */
	int targetMissileIdx[MAX_BASE_SLOT];		/**< aimed target for missile battery */
	int maxLasers;
	aircraftSlot_t lasers[MAX_BASE_SLOT];		/**< Laser batteries assigned to base. */
	int targetLaserIdx[MAX_BASE_SLOT];			/**< aimed target for laser battery */

	int batteryDamage;			/**< Hit points of defense system */
	int baseDamage;			/**< Hit points of base */

	int buildingToBuild;	/**< needed if there is another buildingpart to build (link to gd.buildingTypes) */

	struct building_s *buildingCurrent; /**< needn't be saved */
} base_t;

/** Currently displayed/accessed base. */
extern base_t *baseCurrent;

void B_UpdateBaseData(void);
int B_CheckBuildingConstruction(building_t *b, base_t* base);
int B_GetNumOnTeam(const aircraft_t *aircraft);
void B_ParseBuildings(const char *name, const char **text, qboolean link);
void B_ParseBases(const char *name, const char **text);
void B_BaseAttack(base_t* const base);
void B_BaseResetStatus(base_t* const base);
building_t *B_GetBuildingInBaseByType(const base_t* base, buildingType_t type, qboolean onlyWorking);
building_t *B_GetBuildingByIdx(base_t* base, int idx);
building_t *B_GetBuildingType(const char *buildingName);
buildingType_t B_GetBuildingTypeByBuildingID(const char *buildingID);

/** Coordinates to place the new base at (long, lat) */
extern vec3_t newBasePos;

int B_GetFoundedBaseCount(void);
void B_SetUpBase(base_t* base);

void B_SetBuildingByClick(int row, int col);
void B_ResetBaseManagement(void);
void B_ClearBase(base_t *const base);
void B_NewBases(void);
void B_BuildingStatus(base_t* base, building_t* building);

building_t *B_GetFreeBuildingType(buildingType_t type);
int B_GetNumberOfBuildingsInBaseByTypeIDX(int base_idx, int type_idx);
int B_GetNumberOfBuildingsInBaseByType(int base_idx, buildingType_t type);

int B_ItemInBase(int item_idx, base_t *base);

aircraft_t *B_GetAircraftFromBaseByIndex(base_t* base, int index);
void B_ReviveSoldiersInBase(base_t* base); /* @todo */

int B_GetAvailableQuarterSpace(const base_t* const base);
int B_GetEmployeeCount(const base_t* const base);

qboolean B_CheckBuildingTypeStatus(const base_t* const base, buildingType_t type, buildingStatus_t status, int *cnt);
qboolean B_GetBuildingStatus (const base_t* const base, buildingType_t type);
void B_SetBuildingStatus (base_t* const base, buildingType_t type, qboolean newStatus);
qboolean B_CheckBuildingDependencesStatus(const base_t* const base, building_t* building);

void B_MarkBuildingDestroy(base_t* base, building_t* building);
qboolean B_BuildingDestroy(base_t* base, building_t* building);
void CL_AircraftReturnedToHomeBase(aircraft_t* aircraft);

void B_UpdateBaseCapacities(baseCapacities_t cap, base_t *base);
qboolean B_UpdateStorageAndCapacity(base_t* base, int objIDX, int amount, qboolean reset, qboolean ignorecap);
baseCapacities_t B_GetCapacityFromBuildingType(buildingType_t type);

qboolean B_ScriptSanityCheck(void);

/* menu functions that checks whether the buttons in the base menu are useable */
qboolean BS_BuySellAllowed(const base_t* base);
qboolean AIR_AircraftAllowed(const base_t* base);
qboolean RS_ResearchAllowed(const base_t* base);
qboolean PR_ProductionAllowed(const base_t* base);
qboolean E_HireAllowed(const base_t* base);
qboolean AC_ContainmentAllowed(const base_t* base);
qboolean HOS_HospitalAllowed(const base_t* base);

#endif /* CLIENT_CL_BASEMANGEMENT_H */
