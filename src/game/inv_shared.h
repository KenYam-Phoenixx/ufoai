/**
 * @file inv_shared.h
 * @brief common object-, inventory-, container- and firemode-related functions headers.
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

#ifndef GAME_INV_SHARED_H
#define GAME_INV_SHARED_H

#include "q_shared.h"

/* this is the absolute max for now */
#define MAX_OBJDEFS		128		/* Remember to adapt the "NONE" define (and similar) if this gets changed. */
#define MAX_MAPDEFS		128
#define MAX_WEAPONS_PER_OBJDEF 4
#define MAX_AMMOS_PER_OBJDEF 4
#define MAX_FIREDEFS_PER_WEAPON 8
#define MAX_DAMAGETYPES 64

/** @brief Possible inventory actions for moving items between containers */
typedef enum {
	IA_NONE,			/**< no move possible */

	IA_MOVE,			/**< normal inventory item move */
	IA_ARMOUR,			/**< move or swap armour */
	IA_RELOAD,			/**< reload weapon */
	IA_RELOAD_SWAP,		/**< switch loaded ammo */

	IA_NOTIME,			/**< not enough TUs to make this inv move */
	IA_NORELOAD			/**< not loadable or already fully loaded */
} inventory_action_t;

/** @brief this is a fire definition for our weapons/ammo */
typedef struct fireDef_s {
	char name[MAX_VAR];			/**< script id */
	char projectile[MAX_VAR];	/**< projectile particle */
	char impact[MAX_VAR];		/**< impact particle */
	char hitBody[MAX_VAR];		/**< hit the body particles */
	char fireSound[MAX_VAR];	/**< the sound when a recruits fires */
	char impactSound[MAX_VAR];	/**< the sound that is played on impact */
	char hitBodySound[MAX_VAR];	/**< the sound that is played on hitting a body */
	float relFireVolume;
	float relImpactVolume;
	char bounceSound[MAX_VAR];	/**< bouncing sound */

	/* These values are created in Com_ParseItem and Com_AddObjectLinks.
	 * They are used for self-referencing the firedef. */
	int obj_idx;		/**< The weapon/ammo (csi.ods[obj_idx]) this fd is located in.
						 ** So you can get the containing object by acceessing e.g. csi.ods[obj_idx]. */
	int weap_fds_idx;	/**< The index of the "weapon_mod" entry (objDef_t->fd[weap_fds_idx]) this fd is located in.
						 ** Depending on this value you can find out via objDef_t->weap_idx[weap_fds_idx] what weapon this firemode is used for.
						 ** This does _NOT_ equal the index of the weapon object in ods.
						 */
	int fd_idx;		/**< Self link of the fd in the objDef_t->fd[][fd_idx] array. */

	qboolean soundOnce;
	qboolean gravity;			/**< Does gravity has any influence on this item? */
	qboolean launched;
	qboolean rolled;			/**< Can it be rolled - e.g. grenades */
	qboolean reaction;			/**< This firemode can be used/selected for reaction fire.*/
	int throughWall;		/**< allow the shooting through a wall */
	byte dmgweight;
	float speed;
	vec2_t shotOrg;
	vec2_t spread;
	int delay;
	int bounce;				/**< Is this item bouncing? e.g. grenades */
	float bounceFac;
	float crouch;
	float range;			/**< range of the weapon ammunition */
	int shots;
	int ammo;
	/** the delay that the weapon needs to play sounds and particles
	 * The higher the value, the less the delay (1000/delay) */
	float delayBetweenShots;
	int time;
	vec2_t damage, spldmg;
	float splrad;			/**< splash damage radius */
	int weaponSkill;		/**< What weapon skill is needed to fire this weapon. */
	int irgoggles;			/**< Is this an irgoogle? */
} fireDef_t;

/**
 * @brief The max width and height of an item-shape
 * @note these values depend on the the usage of an uint32_t that has 32 bits and a width of 8bit => 4 rows
 * @sa SHAPE_BIG_MAX_HEIGHT
 * @sa SHAPE_BIG_MAX_WIDTH
 * @note This is also used for bit shifting, so please don't change this until
 * you REALLY know what you are doing
 */
#define SHAPE_SMALL_MAX_WIDTH 8
#define SHAPE_SMALL_MAX_HEIGHT 4

/**
 * @brief defines the max height of an inventory container
 * @note the max width is 32 - because uint32_t has 32 bits and we are
 * using a bitmask for the x values
 * @sa SHAPE_SMALL_MAX_WIDTH
 * @sa SHAPE_SMALL_MAX_HEIGHT
 * @note This is also used for bit shifting, so please don't change this until
 * you REALLY know what you are doing
 */
#define SHAPE_BIG_MAX_HEIGHT 16
/** @brief 32 bit mask */
#define SHAPE_BIG_MAX_WIDTH 32

/**
 * @brief All different types of craft items.
 * @note must begin with weapons and end with ammos
 */
typedef enum {
	/* weapons */
	AC_ITEM_BASE_MISSILE,
	AC_ITEM_BASE_LASER,
	AC_ITEM_WEAPON,

	/* misc */
	AC_ITEM_SHIELD,
	AC_ITEM_ELECTRONICS,

	/* ammos */
	AC_ITEM_AMMO,			/* aircraft ammos */
	AC_ITEM_AMMO_MISSILE,	/* base ammos */
	AC_ITEM_AMMO_LASER,		/* base ammos */

	MAX_ACITEMS
} aircraftItemType_t;

/**
 * @brief Aircraft parameters.
 * @note This is a list of all aircraft parameters that depends on aircraft items.
 * those values doesn't change with shield or weapon assigned to aircraft
 * @note AIR_STATS_WRANGE must be the last parameter (see AII_UpdateAircraftStats)
 */
typedef enum {
	AIR_STATS_SPEED,	/**< Aircraft speed. */
	AIR_STATS_SHIELD,	/**< Aircraft shield. */
	AIR_STATS_ECM,		/**< Aircraft electronic warfare level. */
	AIR_STATS_DAMAGE,	/**< Aircraft damage points (= hit points of the aircraft). */
	AIR_STATS_ACCURACY,	/**< Aircraft accuracy - base accuracy (without weapon). */
	AIR_STATS_FUELSIZE,	/**< Aircraft fuel capacity. */
	AIR_STATS_WRANGE,	/**< Aircraft weapon range - the maximum distance aircraft can open fire. @note: Should be the last one */

	AIR_STATS_MAX
} aircraftParams_t;

typedef struct craftitem_s {
	aircraftItemType_t type;		/**< The type of the aircraft item. */
	float stats[AIR_STATS_MAX];	/**< All coefficient that can affect aircraft->stats */
	float weaponDamage;			/**< The base damage inflicted by an ammo */
	float weaponSpeed;			/**< The speed of the projectile on geoscape */
	float weaponDelay;			/**< The minimum delay between 2 shots */
	int installationTime;		/**< The time needed to install/remove the item on an aircraft */
	qboolean bullets;			/**< create bullets for the projectiles */
} craftitem_t;

/** @brief Buytype categories in the various equipment screens (buy/sell, equip, etc...)
 ** Do not mess with the order (especially BUY_AIRCRAFT and BUY_MULTI_AMMO is/will be used for max-check in normal equipment screens)
 ** @sa scripts.c:buytypeNames
 ** @note Be sure to also update all usages of the buy_type" console function (defined in cl_market.c and mostly used there and in menu_buy.ufo) when changing this.
 **/
typedef enum {
	BUY_WEAP_PRI,	/**< All 'Primary' weapons and their ammo for soldiers. */
	BUY_WEAP_SEC,	/**< All 'Secondary' weapons and their ammo for soldiers. */
	BUY_MISC,		/**< Misc soldier equipment. */
	BUY_ARMOUR,		/**< Armour for soldiers. */
	BUY_MULTI_AMMO, /**< Ammo (and other stuff) that is used in both Pri/Sec weapons. */
	/* MAX_SOLDIER_EQU_BUYTYPES */
	BUY_AIRCRAFT,	/**< Aircraft and craft-equipment. */
	BUY_DUMMY,		/**< Everything that is not equipment for soldiers except craftitems. */
	BUY_CRAFTITEM,	/**< Craftitem. */
	MAX_BUYTYPES,

	ENSURE_32BIT = 0xFFFFFF
} equipmentBuytypes_t;

#define MAX_SOLDIER_EQU_BUYTYPES BUY_MULTI_AMMO

#define BUY_PRI(type)	( (((type) == BUY_WEAP_PRI) || ((type) == BUY_MULTI_AMMO)) ) /** < Checks if "type" is displayable/usable in the primary category. */
#define BUY_SEC(type)	( (((type) == BUY_WEAP_SEC) || ((type) == BUY_MULTI_AMMO)) ) /** < Checks if "type" is displayable/usable in the secondary category. */
#define BUYTYPE_MATCH(type1,type2) (\
	(  ((((type1) == BUY_WEAP_PRI) || ((type1) == BUY_WEAP_SEC)) && ((type2) == BUY_MULTI_AMMO)) \
	|| ((((type2) == BUY_WEAP_PRI) || ((type2) == BUY_WEAP_SEC)) && ((type1) == BUY_MULTI_AMMO)) \
	|| ((type1) == (type2)) ) \
	) /**< Check if the 2 buytypes (type1 and type2) are compatible) */

/**
 * @brief Defines all attributes of obejcts used in the inventory.
 * @todo Document the various (and mostly not obvious) varables here. The documentation in the .ufo file(s) might be a good starting point.
 * @note See also http://ufoai.ninex.info/wiki/index.php/UFO-Scripts/weapon_%2A.ufo
 */
typedef struct objDef_s {
	/* Common */
	char name[MAX_VAR];		/**< Item name taken from scriptfile. */
	char id[MAX_VAR];		/**< Identifier of the item being item definition in scriptfile. */
	char model[MAX_VAR];		/**< Model name - relative to game dir. */
	char image[MAX_VAR];		/**< Object image file - relative to game dir. */
	char type[MAX_VAR];		/**< melee, rifle, ammo, armour @todo: clarify how this is being used */
	char extends_item[MAX_VAR];	/**< @todo: document me */
	uint32_t shape;			/**< The shape in inventory. */

	byte sx, sy;			/**< Size in x and y direction. */
	float scale;			/**< scale value for images? and models @todo: fixme - array of scales. */
	vec3_t center;			/**< origin for models @todo: fixme - array of scales. */
	char animationIndex;	/**< The animation index for the character with the weapon. */
	qboolean weapon;		/**< This item is a weapon or ammo. */
	qboolean holdTwoHanded;		/**< The soldier needs both hands to hold this object. */
	qboolean fireTwoHanded;		/**< The soldier needs both hands to fire using object. */
	qboolean extension;		/**< This is an extension. (may not be headgear, too). */
	qboolean headgear;		/**< This is a headgear. (may not be extension, too). */
	qboolean thrown;		/**< This item can be thrown. */

	int price;			/**< Price for this item. */
	int size;			/**< Size of an item, used in storage capacities. */
	equipmentBuytypes_t buytype;			/**< Category of the item - used in menus (see equipment_buytypes_t). */
	qboolean notOnMarket;		/**< True if this item should not be available on market. */

	/* Weapon specific. */
	int ammo;			/**< How much can we load into this weapon at once. @todo: what is this? isn't it ammo-only specific which defines amount of bullets in clip? */
	int reload;			/**< Time units (TUs) for reloading the weapon. */
	qboolean oneshot;		/**< This weapon contains its own ammo (it is loaded in the base).
					     int ammo of objDef_s defines the amount of ammo used in oneshoot weapons. */
	qboolean deplete;		/**< This weapon useless after all ("oneshot") ammo is used up.
				  	     If true this item will not be collected on mission-end. (see INV_CollectinItems). */

	int useable;			/**< Defines which team can use this item: 0 - human, 1 - alien.
					     Used in checking the right team when filling the containers with available armours. */

	int ammo_idx[MAX_AMMOS_PER_OBJDEF];		/**< List of ammo-object indices. The index of the ammo in csi.ods[xxx]. */
	int numAmmos;			/**< Number of ammos this weapon can be used with, which is <= MAX_AMMOS_PER_OBJDEF. */

	/* Firemodes (per weapon). */
	char weap_id[MAX_WEAPONS_PER_OBJDEF][MAX_VAR];	/**< List of weapon ids as parsed from the ufo file "weapon_mod <id>". */
	int weap_idx[MAX_WEAPONS_PER_OBJDEF];		/**< List of weapon-object indices. The index of the weapon in csi.ods[xxx].
								Correct index for this array can be get from fireDef_t.weap_fds_idx. or
								FIRESH_FiredefsIDXForWeapon. */
	fireDef_t fd[MAX_WEAPONS_PER_OBJDEF][MAX_FIREDEFS_PER_WEAPON];	/**< List of firemodes per weapon (the ammo can be used in). */
	int numFiredefs[MAX_WEAPONS_PER_OBJDEF];	/**< Number of firemodes per weapon.
							     Maximum value for fireDef_t.fd_idx <= MAX_FIREDEFS_PER_WEAPON. */
	int numWeapons;					/**< Number of weapons this ammo can be used in.
							     Maximum value for fireDef_t.weap_fds_idx <= MAX_WEAPONS_PER_OBJDEF. */

	struct technology_s *tech;	/**< Technology link to item. */
	struct technology_s *extension_tech;	/**< Technology link to item to use this extension for (if this is an extension). @todo: is this used anywhere? */

	/* Armour specific */
	short protection[MAX_DAMAGETYPES];	/**< Protection values for each armour and every damage type. */
	short ratings[MAX_DAMAGETYPES];		/**< Rating values for each armour and every damage type to display in the menus. */

	/* Aircraft specific */
	qboolean aircraft;			/**< True if this item is dummy aircraft - used in disassembling. */
	byte dmgtype;
	craftitem_t craftitem;
} objDef_t;

enum {
	INV_DOES_NOT_FIT,
	INV_FITS,
	INV_FITS_ONLY_ROTATED
};

#define MAX_INVDEFS     16

/** @brief inventory definition for our menus */
typedef struct invDef_s {
	char name[MAX_VAR];	/**< id from script files */
	/** type of this container or inventory */
	qboolean single;	/**< just a single item can be stored in this container */
	qboolean armour;	/**< only armour can be stored in this container */
	qboolean extension;	/**< only extension items can be stored in this container */
	qboolean headgear;	/**< only headgear items can be stored in this container */
	qboolean all;	/**< every item type can be stored in this container */
	qboolean temp;	/**< this is only a pointer to another inventory definitions */
	uint32_t shape[SHAPE_BIG_MAX_HEIGHT];	/**< the inventory form/shape */
	int in, out;	/**< TU costs */
} invDef_t;

#define MAX_CONTAINERS  MAX_INVDEFS
#define MAX_INVLIST     1024

/**
 * @brief item definition
 * @note m and t are transfered as shorts over the net - a value of NONE means
 * that there is no item - e.g. a value of NONE for m means, that there is no
 * ammo loaded or assigned to this weapon
 */
typedef struct item_s {
	int a;	/**< number of ammo rounds left - see NONE_AMMO */
	int m;	/**< unique index of ammo type on csi->ods - see NONE */
	int t;	/**< unique index of weapon in csi.ods array - see NONE */
	int amount;	/**< the amount of items of this type on the same x and y location in the container */
	int rotated; /**< If the item is currently displayed rotated (1) or not (0) */
} item_t;

/** @brief container/inventory list (linked list) with items */
typedef struct invList_s {
	item_t item;	/**< which item */
	int x, y;		/**< position of the item */
	struct invList_s *next;		/**< next entry in this list */
} invList_t;

/** @brief inventory defintion with all its containers */
typedef struct inventory_s {
	invList_t *c[MAX_CONTAINERS];
} inventory_t;

#define MAX_EQUIPDEFS   64

typedef struct equipDef_s {
	char name[MAX_VAR];
	int num[MAX_OBJDEFS];
	byte num_loose[MAX_OBJDEFS];
} equipDef_t;

#define MAX_TEAMS_PER_MISSION 4
#define MAX_TERRAINS 8
#define MAX_CULTURES 8
#define MAX_POPULATIONS 8

typedef struct mapDef_s {
	/* general */
	char *id;				/**< script file id */
	char *map;				/**< bsp or ump base filename (without extension and day or night char) */
	char *param;			/**< in case of ump file, the assembly to use */
	char *description;		/**< the description to show in the menus */
	char *loadingscreen;	/**< the loading screen */
	char *size;				/**< small, medium, big */
	char *music;			/**< music that should be played during this mission */

	/* multiplayer */
	qboolean multiplayer;	/**< is this map multiplayer ready at all */
	int teams;				/**< multiplayer teams */
	linkedList_t *gameTypes;	/**< gametype strings this map is useable for */

	/* singleplayer */
	/* @todo: Make use of these values */
	linkedList_t *terrains;		/**< terrain strings this map is useable for */
	linkedList_t *populations;	/**< population strings this map is useable for */
	linkedList_t *cultures;		/**< culture strings this map is useable for */
} mapDef_t;

typedef struct damageType_s {
	char id[MAX_VAR];
	qboolean showInMenu;
} damageType_t;

/**
 * @brief The csi structure is the client-server-information structure
 * which contains all the UFO info needed by the server and the client.
 * @sa ccs_t
 * @note Most of this comes from the script files
 */
typedef struct csi_s {
	/** Object definitions */
	objDef_t ods[MAX_OBJDEFS];
	int numODs;

	/** Inventory definitions */
	invDef_t ids[MAX_INVDEFS];
	int numIDs;

	/** Map definitions */
	mapDef_t mds[MAX_MAPDEFS];
	int numMDs;
	mapDef_t *currentMD;	/**< currently selected mapdef */

	/** Special container ids */
	int idRight, idLeft, idExtension;
	int idHeadgear, idBackpack, idBelt, idHolster;
	int idArmour, idFloor, idEquip;

	/** Damage type ids */
	int damNormal, damBlast, damFire, damShock;
	/** Damage type ids */
	int damLaser, damPlasma, damParticle, damStun;

	/** Equipment definitions */
	equipDef_t eds[MAX_EQUIPDEFS];
	int numEDs;

	/** Damage types */
	damageType_t dts[MAX_DAMAGETYPES];
	int numDTs;

	/** team definitions */
	teamDef_t teamDef[MAX_TEAMDEFS];
	int numTeamDefs;

	/** the current assigned teams for this mission */
	teamDef_t* alienTeams[MAX_TEAMS_PER_MISSION];
	int numAlienTeams;
} csi_t;


/** @todo Medals. Still subject to (major) changes. */

#define MAX_SKILL           100

#define GET_HP_HEALING( ab ) (1 + (ab) * 10/MAX_SKILL)
#define GET_HP( ab )        (min((80 + (ab) * 90/MAX_SKILL), 255))
#define GET_ACC( ab, sk )   ((1 - ((float)(ab)/MAX_SKILL + (float)(sk)/MAX_SKILL) / 2)) /**@todo Skill-influence needs some balancing. */
#define GET_TU( ab )        (min((27 + (ab) * 20/MAX_SKILL), 255))
#define GET_MORALE( ab )        (min((100 + (ab) * 150/MAX_SKILL), 255))

typedef enum {
	KILLED_ALIENS,
	KILLED_CIVILIANS,
	KILLED_TEAM,

	KILLED_NUM_TYPES
} killtypes_t;

typedef enum {
	ABILITY_POWER,
	ABILITY_SPEED,
	ABILITY_ACCURACY,
	ABILITY_MIND,

	SKILL_CLOSE,
	SKILL_HEAVY,
	SKILL_ASSAULT,
	SKILL_SNIPER,
	SKILL_EXPLOSIVE,
	SKILL_NUM_TYPES
} abilityskills_t;

#define ABILITY_NUM_TYPES SKILL_CLOSE


#define MAX_UGV         8
typedef struct ugv_s {
	char id[MAX_VAR];
	char weapon[MAX_VAR];
	char armour[MAX_VAR];
	int size;
	int tu;
	char actors[MAX_VAR];
} ugv_t;

#define MAX_RANKS           32

/** @brief Describes a rank that a recruit can gain */
typedef struct rank_s {
	char *id;		/**< Index in gd.ranks */
	char name[MAX_VAR];	/**< Rank name (Captain, Squad Leader) */
	char shortname[8];		/**< Rank shortname (Cpt, Sqd Ldr) */
	char *image;		/**< Image to show in menu */
	int type;			/**< employeeType_t */
	int mind;			/**< character mind attribute needed */
	int killed_enemies;		/**< needed amount of enemies killed */
	int killed_others;		/**< needed amount of other actors killed */
} rank_t;

extern rank_t ranks[MAX_RANKS]; /**< Global list of all ranks defined in medals.ufo. */
extern int numRanks;            /**< The number of entries in the list above. */

/** @brief Structure of scores being counted after soldier kill or stun. */
typedef struct chrScore_s {
	int alienskilled;	/**< Killed aliens. */
	int aliensstunned;	/**< Stunned aliens. */
	int civilianskilled;	/**< Killed civilians. @todo: use me. */
	int civiliansstunned;	/**< Stunned civilians. @todo: use me. */
	int teamkilled;		/**< Killed teammates. @todo: use me. */
	int teamstunned;	/**< Stunned teammates. @todo: use me. */
	int closekills;		/**< Aliens killed by CLOSE. */
	int heavykills;		/**< Aliens killed by HEAVY. */
	int assaultkills;	/**< Aliens killed by ASSAULT. */
	int sniperkills;	/**< Aliens killed by SNIPER. */
	int explosivekills;	/**< Aliens killed by EXPLOSIVE. */
	int accuracystat;	/**< Aliens kills or stuns counted to ACCURACY. */
	int powerstat;		/**< Aliens kills or stuns counted to POWER. */
	int survivedmissions;	/**< Missions survived. @todo: use me. */
} chrScore_t;

/** @brief Describes a character with all its attributes */
typedef struct character_s {
	int ucn;
	char name[MAX_VAR];			/**< Character name (as in: soldier name). */
	char path[MAX_VAR];			/**< @todo: document me. */
	char body[MAX_VAR];			/**< @todo: document me. */
	char head[MAX_VAR];			/**< @todo: document me. */
	int skin;				/**< Index of skin. */

	int skills[SKILL_NUM_TYPES];		/**< Array of skills and abilities. */

	int HP;						/**< Health points (current ones). */
	int maxHP;					/**< Maximum health points (as in: 100% == fully healed). */
	int STUN, morale;			/**< @todo: document me. */

	chrScore_t chrscore;		/**< Array of scores. */

	int kills[KILLED_NUM_TYPES];/**< Array of kills (@todo: integrate me with chrScore_t chrscore). */
	int assigned_missions;		/**< Assigned missions (@todo: integrate me with chrScore_t chrscore). */

	int rank;					/**< Index of rank (in gd.ranks). */

	/** @sa memcpy in Grid_CheckForbidden */
	int fieldSize;				/**< ACTOR_SIZE_* */

	inventory_t *inv;			/**< Inventory definition. */

	int empl_idx;				/**< Backlink to employee-struct - global employee index. */
	int empl_type;				/**< Employee type. */

	qboolean armour;			/**< Able to use armour. */
	qboolean weapons;			/**< Able to use weapons. */

	int teamDefIndex;			/**< teamDef array index. */
	int gender;					/**< Gender index. */
} character_t;

/** @brief The types of employees. */
/** @note If you will change order, make sure personel transfering still works. */
typedef enum {
	EMPL_SOLDIER,
	EMPL_SCIENTIST,
	EMPL_WORKER,
	EMPL_MEDIC,
	EMPL_ROBOT,
	MAX_EMPL					/**< for counting over all available enums */
} employeeType_t;

#define MAX_CAMPAIGNS	16

/* ================================ */
/*  CHARACTER GENERATING FUNCTIONS  */
/* ================================ */

/* These are min and max values for all teams can be defined via campaign script. */
extern int CHRSH_skillValues[MAX_CAMPAIGNS][MAX_TEAMS][MAX_EMPL][2];
extern int CHRSH_abilityValues[MAX_CAMPAIGNS][MAX_TEAMS][MAX_EMPL][2];

void CHRSH_SetGlobalCampaignID(int campaignID);
int Com_StringToTeamNum(const char* teamString) __attribute__((nonnull));
void CHRSH_CharGenAbilitySkills(character_t * chr, int team) __attribute__((nonnull));
char *CHRSH_CharGetBody(character_t* const chr) __attribute__((nonnull));
char *CHRSH_CharGetHead(character_t* const chr) __attribute__((nonnull));

/* ================================ */
/*  INVENTORY MANAGEMENT FUNCTIONS  */
/* ================================ */

void INVSH_InitCSI(csi_t * import) __attribute__((nonnull));
void INVSH_InitInventory(invList_t * invChain) __attribute__((nonnull));
int Com_CheckToInventory(const inventory_t* const i, const int item, const int container, int x, int y);
invList_t *Com_SearchInInventory(const inventory_t* const i, int container, int x, int y) __attribute__((nonnull(1)));
invList_t *Com_AddToInventory(inventory_t* const i, item_t item, int container, int x, int y, int amount) __attribute__((nonnull(1)));
qboolean Com_RemoveFromInventory(inventory_t* const i, int container, int x, int y) __attribute__((nonnull(1)));
qboolean Com_RemoveFromInventoryIgnore(inventory_t* const i, int container, int x, int y, qboolean ignore_type) __attribute__((nonnull(1)));
int Com_MoveInInventory(inventory_t* const i, int from, int fx, int fy, int to, int tx, int ty, int *TU, invList_t ** icp) __attribute__((nonnull(1)));
int Com_MoveInInventoryIgnore(inventory_t* const i, int from, int fx, int fy, int to, int tx, int ty, int *TU, invList_t ** icp, qboolean ignore_type) __attribute__((nonnull(1)));
void INVSH_EmptyContainer(inventory_t* const i, const int container) __attribute__((nonnull(1)));
void INVSH_DestroyInventory(inventory_t* const i) __attribute__((nonnull(1)));
void Com_FindSpace(const inventory_t* const inv, item_t *item, const int container, int * const px, int * const py) __attribute__((nonnull(1)));
int Com_TryAddToInventory(inventory_t* const inv, item_t item, int container) __attribute__((nonnull(1)));
int Com_TryAddToBuyType(inventory_t* const inv, item_t item, int container, int amount) __attribute__((nonnull(1)));
void INVSH_EquipActor(inventory_t* const inv, const int *equip, int anzEquip, const char *name, character_t* chr) __attribute__((nonnull(1)));
void INVSH_EquipActorMelee(inventory_t* const inv, character_t* chr) __attribute__((nonnull(1)));
void INVSH_PrintContainerToConsole(inventory_t* const i);

void INVSH_PrintItemDescription(int i);
int INVSH_GetItemByID(const char *id);
qboolean INVSH_LoadableInWeapon(objDef_t *od, int weapon_idx);

/* =============================== */
/*  FIREMODE MANAGEMENT FUNCTIONS  */
/* =============================== */

fireDef_t* FIRESH_GetFiredef(int objIdx, int weapFdsIdx, int fdIdx);
int FIRESH_FiredefsIDXForWeapon(objDef_t *od, int weapon_idx);
int FIRESH_GetDefaultReactionFire(objDef_t *ammo, int weapon_fds_idx);

void Com_MergeShapes(uint32_t *shape, uint32_t itemshape, int x, int y);
qboolean Com_CheckShape(const uint32_t *shape, int x, int y);
int Com_ShapeUsage(uint32_t shape);
uint32_t Com_ShapeRotate(uint32_t shape);
#ifdef DEBUG
void Com_ShapePrint(uint32_t shape);
#endif

/** @brief Number of bytes that is read and written via inventory transfer functions */
#define INV_INVENTORY_BYTES 9

#endif /* GAME_INV_SHARED_H */
