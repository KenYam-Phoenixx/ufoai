/**
 * @file cl_campaign.h
 * @brief Header file for single player campaign control.
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

#ifndef CLIENT_CL_CAMPAIGN_H
#define CLIENT_CL_CAMPAIGN_H

/* Time Constants */
#define DAYS_PER_YEAR 365
#define DAYS_PER_YEAR_AVG 365.25
#define MONTHS_PER_YEAR 12

#define BID_FACTOR 0.9

/* Mission Constants */
#define MAX_MISSIONS	255
#define MAX_REQMISSIONS	4
#define MAX_ACTMISSIONS	16
#define MAX_SETMISSIONS	16

/* Stage Constants */
#define MAX_STAGESETS	256
#define MAX_STAGES		64

/* check for water */
/* blue value is 64 */
#define MapIsWater(color)        (color[0] ==   0 && color[1] ==   0 && color[2] ==  64)

/* terrain types */
#define MapIsArctic(color)       (color[0] == 128 && color[1] == 255 && color[2] == 255)
#define MapIsDesert(color)       (color[0] == 255 && color[1] == 128 && color[2] ==   0)
#define MapIsMountain(color)     (color[0] == 255 && color[1] ==   0 && color[2] ==   0)
#define MapIsTropical(color)     (color[0] == 128 && color[1] == 128 && color[2] == 255)
#define MapIsGrass(color)        (color[0] == 128 && color[1] == 255 && color[2] ==   0)
#define MapIsWasted(color)       (color[0] == 128 && color[1] ==   0 && color[2] == 128)
#define MapIsCold(color)         (color[0] ==   0 && color[1] ==   0 && color[2] == 255)

/* culture types */
#define MapIsWestern(color)      (color[0] == 128 && color[1] == 255 && color[2] == 255)
#define MapIsEastern(color)      (color[0] == 255 && color[1] == 128 && color[2] ==   0)
#define MapIsOriental(color)     (color[0] == 255 && color[1] ==   0 && color[2] ==   0)
#define MapIsAfrican(color)      (color[0] == 128 && color[1] == 128 && color[2] == 255)

/* population types */
#define MapIsUrban(color)        (color[0] == 128 && color[1] == 255 && color[2] == 255)
#define MapIsSuburban(color)     (color[0] == 255 && color[1] == 128 && color[2] ==   0)
#define MapIsVillage(color)      (color[0] == 255 && color[1] ==   0 && color[2] ==   0)
#define MapIsRural(color)        (color[0] == 128 && color[1] == 128 && color[2] == 255)
#define MapIsNopopulation(color) (color[0] == 128 && color[1] == 255 && color[2] ==   0)

/* RASTER enables a better performance for CP_GetRandomPosOnGeoscape set it to 1-6
 * the higher the value the better the performance, but the smaller the coverage */
#define RASTER 2

/** possible map types */
typedef enum mapType_s {
	MAPTYPE_TERRAIN,
	MAPTYPE_CULTURE,
	MAPTYPE_POPULATION,
	MAPTYPE_NATIONS,

	MAPTYPE_MAX
} mapType_t;

/** possible mission types */
typedef enum missionType_s {
	MIS_TERRORATTACK,	/* default: for every mission parsed in missions.ufo (not linked with type entry in missions.ufo) */
	MIS_BASEATTACK,		/* for base attack missions */
	MIS_CRASHSITE,		/* for dynamical crash sites (not for crash sites in missions.ufo) */

	MIS_MAX
} missionType_t;

/** mission definition */
typedef struct mission_s {
	mapDef_t* mapDef;
	char name[MAX_VAR];	/**< script id */
	char *missionText;	/**< translateable - but untranslated in memory */
	char *missionTextAlternate;	/**< translateable - but untranslated in memory */
	char *triggerText;	/**< translateable - but untranslated in memory */
	char location[MAX_VAR];
	char nation[MAX_VAR];
	char type[MAX_VAR];
	teamDef_t* alienTeams[MAX_TEAMS_PER_MISSION];
	int numAlienTeams;
	char alienEquipment[MAX_VAR];
	char civTeam[MAX_VAR];
	char cmds[MAX_VAR];
	const char *zoneType;
	/** @note Don't end with ; - the trigger commands get the base index as
	 * parameter - see CP_ExecuteMissionTrigger - If you don't need the base index
	 * in your trigger command, you can seperate more commands with ; of course */
	char onwin[MAX_VAR];		/**< trigger command after you've won a battle */
	char onlose[MAX_VAR];		/**< trigger command after you've lost a battle */
	int ugv;					/**< uncontrolled ground units (entity: info_ugv_start) */
	qboolean active;			/**< aircraft at place? */
	qboolean onGeoscape;		/**< is or was already on geoscape - don't add it twice */
	date_t dateOnGeoscape;		/**< last time the mission was on geoscape */
	qboolean storyRelated;		/**< auto mission play disabled when true */
	missionType_t missionType;	/**< type of mission */
	void* data;					/**< may be related to mission type */
	qboolean keepAfterFail;		/**< keep the mission on geoscape after failing */
	qboolean noExpire;			/**< the mission won't expire */
	qboolean played;			/**< a mission could have onGeoscape true but played false - because it expired */
	vec2_t pos;
	byte mask[4];
	int aliens, civilians;
} mission_t;

typedef struct stageSet_s {
	char name[MAX_VAR];
	char needed[MAX_VAR];
	char nextstage[MAX_VAR];
	char endstage[MAX_VAR];
	char cmds[MAX_VAR];			/**< script commands to execute when stageset gets activated */

	/** @note set only one of the two - cutscene should have higher priority */
	char sequence[MAX_VAR];		/**< play a sequence when entering a new stage? */
	char cutscene[MAX_VAR];		/**< play a cutscent when entering a new stage? */

	date_t delay;
	date_t frame;
	date_t expire;				/**< date when this mission will expire and will be removed from geoscape */
	int number;					/**< number of missions until set is deactivated (they only need to appear on geoscape) */
	int quota;					/**< number of successfully ended missions until set gets deactivated */
	qboolean activateXVI;		/**< if this is true the stage will active the global XVI spreading */
	qboolean humanAttack;		/**< true when humans start to attack player */
	int numMissions;			/**< number of missions in this set */
	int missions[MAX_SETMISSIONS];	/**< mission names in this set */
	int ufos;					/**< how many ufos should appear in this stage */
} stageSet_t;

typedef struct stage_s {
	char name[MAX_VAR];			/**< stage name */
	int first;					/**< stageSet id in stageSets array */
	int num;					/**< how many stageSets in this stage */
} stage_t;

typedef struct setState_s {
	stageSet_t *def;
	stage_t *stage;
	byte active;				/**< is this set active? */
	date_t start;				/**< date when the set was activated */
	date_t event;
	int num;
	int done;					/**< how many mission out of the mission pool are already done */
} setState_t;

typedef struct stageState_s {
	stage_t *def;
	byte active;
	date_t start;
} stageState_t;

typedef struct actMis_s {
	mission_t *def;
	setState_t *cause;
	date_t expire;
	vec2_t realPos;
} actMis_t;

/* UFO Recoveries stuff. */
#define MAX_RECOVERIES 32

/** @brief Structure of UFO recoveries (all of them). */
typedef struct ufoRecoveries_s {
	qboolean active;		/**< True if the recovery is under processing. */
	int baseID;			/**< Base idx where the recovery will be processing. */
	int ufotype;			/**< Index of UFO in aircraft_samples array. */
	date_t event;			/**< When the process will start (UFO got transported to base). */
} ufoRecoveries_t;

/** @brief Structure with mission info needed to create results summary at Menu Won. */
typedef struct missionResults_s {
	int itemtypes;		/**< Types of items gathered from a mission. */
	int itemamount;		/**< Amount of items (all) gathered from a mission. */
	qboolean recovery;	/**< Qtrue if player secured a UFO (landed or crashed). */
	ufoType_t ufotype;	/**< Type of UFO secured during the mission. */
	qboolean crashsite;	/**< Qtrue if secured UFO was crashed one. */
} missionResults_t;

/** campaign definition */
typedef struct campaign_s {
	int idx;					/**< own index in global campaign array */
	char id[MAX_VAR];
	char name[MAX_VAR];
	char team[MAX_VAR];
	char researched[MAX_VAR];
	char equipment[MAX_VAR];
	char market[MAX_VAR];
	equipDef_t *marketDef;
	char text[MAX_VAR];			/**< placeholder for gettext stuff */
	char map[MAX_VAR];			/**< geoscape map */
	char firststage[MAX_VAR];
	int soldiers;				/**< start with x soldiers */
	int scientists;				/**< start with x scientists */
	int workers;				/**< start with x workers */
	int medics;					/**< start with x medics */
	int ugvs;					/**< start with x ugvs (robots) */
	int credits;				/**< start with x credits */
	int num;
	signed int difficulty;		/**< difficulty level -4 - 4 */
	int civiliansKilledUntilLost;	/**< how many civilians may be killed until you've lost the game */
	int negativeCreditsUntilLost;	/**< bankrupt - negative credits until you've lost the game */
	int maxAllowedXVIRateUntilLost;	/**< 0 - 100 - the average rate of XVI over all nations before you've lost the game */
	qboolean visible;			/**< visible in campaign menu? */
	date_t date;				/**< starting date for this campaign */
	qboolean finished;
} campaign_t;

/** salary values for a campaign */
typedef struct salary_s {
	int soldier_base;
	int soldier_rankbonus;
	int worker_base;
	int worker_rankbonus;
	int scientist_base;
	int scientist_rankbonus;
	int medic_base;
	int medic_rankbonus;
	int robot_base;
	int robot_rankbonus;
	int aircraft_factor;
	int aircraft_divisor;
	int base_upkeep;
	int admin_initial;
	int admin_soldier;
	int admin_worker;
	int admin_scientist;
	int admin_medic;
	int admin_robot;
	float debt_interest;
} salary_t;

#define SALARY_SOLDIER_BASE salaries[curCampaign->idx].soldier_base
#define SALARY_SOLDIER_RANKBONUS salaries[curCampaign->idx].soldier_rankbonus
#define SALARY_WORKER_BASE salaries[curCampaign->idx].worker_base
#define SALARY_WORKER_RANKBONUS salaries[curCampaign->idx].worker_rankbonus
#define SALARY_SCIENTIST_BASE salaries[curCampaign->idx].scientist_base
#define SALARY_SCIENTIST_RANKBONUS salaries[curCampaign->idx].scientist_rankbonus
#define SALARY_MEDIC_BASE salaries[curCampaign->idx].medic_base
#define SALARY_MEDIC_RANKBONUS salaries[curCampaign->idx].medic_rankbonus
#define SALARY_ROBOT_BASE salaries[curCampaign->idx].robot_base
#define SALARY_ROBOT_RANKBONUS salaries[curCampaign->idx].robot_rankbonus
#define SALARY_AIRCRAFT_FACTOR salaries[curCampaign->idx].aircraft_factor
#define SALARY_AIRCRAFT_DIVISOR salaries[curCampaign->idx].aircraft_divisor
#define SALARY_BASE_UPKEEP salaries[curCampaign->idx].base_upkeep
#define SALARY_ADMIN_INITIAL salaries[curCampaign->idx].admin_initial
#define SALARY_ADMIN_SOLDIER salaries[curCampaign->idx].admin_soldier
#define SALARY_ADMIN_WORKER salaries[curCampaign->idx].admin_worker
#define SALARY_ADMIN_SCIENTIST salaries[curCampaign->idx].admin_scientist
#define SALARY_ADMIN_MEDIC salaries[curCampaign->idx].admin_medic
#define SALARY_ADMIN_ROBOT salaries[curCampaign->idx].admin_robot
#define SALARY_DEBT_INTEREST salaries[curCampaign->idx].debt_interest

/** market structure */
typedef struct market_s {
	int num[MAX_OBJDEFS];
	int bid[MAX_OBJDEFS];
	int ask[MAX_OBJDEFS];
	double cumm_supp_diff[MAX_OBJDEFS];
} market_t;

/**
 * @brief Detailed information about the nation relationship (currently per month, but could be used elsewhere).
 * @todo Maybe we should also move the "funding" stuff (see nation_t) in here? It is static right now though so i see no reason to do that yet.
 */
typedef struct nationInfo_s {
	qboolean	inuse;	/**< Is this entry used? */

	/* Relationship */
	float happiness;
/**	float xvi_infection;	@todo How much (percentage 0-100) of the population in this nation is infected. */
	float alienFriendly;	/**< How friendly is this nation towards the aliens. (percentage 0-100)
				 * @todo Check if this is still needed after XVI factors are in.
				 * Pro: People can be alien-frienldy without being affected after all.
				 * Con: ?
				 */
} nationInfo_t;

#define MAX_NATION_BORDERS 64
/**
 * @brief Nation definition
 */
typedef struct nation_s {
	char *id;		/**< Unique ID of this nation. */
	char *name;		/**< Full name of the nation. */

	vec4_t color;		/**< The color this nation uses int he color-coded earth-map */
	vec2_t pos;		/**< Nation name position on geoscape. */
	int XVIRate;	/**< 0 - 100 - the rate of infection for this nation - starts at 0 */

	nationInfo_t stats[MONTHS_PER_YEAR];	/**< Detailed information about teh history of this nations relationship toward PHALANX and the aliens.
									 * The first entry [0] is the current month - all following entries are stored older months.
									 * Combined with the funding info below we can generate an overview over time.
									 */

	/* Funding */
	int maxFunding;		/**< How many (monthly) credits. */
	int maxSoldiers;	/**< How many (monthly) soldiers. */
	int maxScientists;	/**< How many (monthly) scientists. */
	int maxWorkers;		/**< How many (monthly) workers. */
	int maxMedics;		/**< How many (monthly) medics. */
	int maxUgvs;		/**< How many (monthly) ugvs (robots).
				 * @todo this needs to be removed here and added into the buy&produce menues.
				 */

	/** A list if points where the border of this nation is located
	@todo not used right now? */
	vec2_t borders[MAX_NATION_BORDERS];	/**< GL_LINE_LOOP coordinates */
	int numBorders;		/**< coordinate count */
} nation_t;

#define MAX_NATIONS 8

/**
 * @brief client structure
 * @sa csi_t
 */
typedef struct ccs_s {
	equipDef_t eMission;
	market_t eMarket;

	stageState_t stage[MAX_STAGES];
	setState_t set[MAX_STAGESETS];
	actMis_t mission[MAX_ACTMISSIONS];
	int numMissions;

	vec2_t mapPos;
	vec2_t mapSize;

	qboolean singleplayer;	/**< singleplayer or multiplayer */
	int multiplayerMapDefinitionIndex;	/**< current selected multiplayer map */

	int credits;			/**< actual credits amount */
	int civiliansKilled;	/**< how many civilians were killed already */
	int aliensKilled;		/**< how many aliens were killed already */
	date_t date;			/**< current date */
	qboolean XVISpreadActivated;	/**< should the XVI spread over the globe already */
	qboolean humansAttackActivated;	/**< humans start to attack player */
	float timer;

	vec3_t angles;			/**< 3d geoscape rotation */
	vec2_t center;
	float zoom;
} ccs_t;

/** possible geoscape actions */
typedef enum mapAction_s {
	MA_NONE,
	MA_NEWBASE,				/**< build a new base */
	MA_INTERCEPT,			/**< intercept */
	MA_BASEATTACK,			/**< base attacking */
	MA_UFORADAR				/**< ufos are in our radar */
} mapAction_t;

/** possible aircraft states */
typedef enum aircraftStatus_s {
	AIR_NONE = 0,
	AIR_REFUEL,				/**< refill fuel */
	AIR_HOME,				/**< in homebase */
	AIR_IDLE,				/**< just sit there on geoscape */
	AIR_TRANSIT,			/**< moving */
	AIR_MISSION,			/**< moving to a mission */
	AIR_UFO,				/**< purchasing a UFO - also used for ufos that are purchasing an aircraft */
	AIR_DROP,				/**< ready to drop down */
	AIR_INTERCEPT,			/**< ready to intercept */
	AIR_TRANSFER,			/**< being transfered */
	AIR_RETURNING,			/**< returning to homebase */
	AIR_FLEEING				/**< fleeing other aircraft */
} aircraftStatus_t;

extern actMis_t *selMis;

extern campaign_t *curCampaign;
extern ccs_t ccs;

void AIR_SaveAircraft(sizebuf_t * sb, base_t * base);
void AIR_LoadAircraft(sizebuf_t * sb, base_t * base, int version);

void CL_ResetCampaign(void);
void CL_ResetSinglePlayerData(void);
void CL_DateConvert(const date_t * date, int *day, int *month);
const char *CL_DateGetMonthName(int month);
void CL_CampaignRun(void);
void CL_GameTimeStop(void);
void CL_GameTimeFast(void);
void CL_GameTimeSlow(void);
qboolean CL_NewBase(base_t* base, vec2_t pos);
void CL_ParseMission(const char *name, const char **text);
mission_t* CL_AddMission(const char *name);
void CP_RemoveLastMission(void);
void CL_ParseStage(const char *name, const char **text);
void CL_ParseCampaign(const char *name, const char **text);
void CL_ParseNations(const char *name, const char **text);
void CL_UpdateCredits(int credits);
qboolean CL_OnBattlescape(void);
void CL_GameInit(qboolean load);
void AIR_NewAircraft(base_t * base, const char *name);
void CL_ParseResearchedCampaignItems(const char *name, const char **text);
void CL_ParseResearchableCampaignStates(const char *name, const char **text, qboolean researchable);
void CP_ExecuteMissionTrigger(mission_t * m, qboolean won);
actMis_t* CL_CampaignAddGroundMission(mission_t* mis);

qboolean CP_GetRandomPosOnGeoscape(vec2_t pos, linkedList_t* terrainTypes, linkedList_t* cultureTypes, linkedList_t* populationTypes, linkedList_t* nations);

campaign_t* CL_GetCampaign(const char* name);
void CL_GameExit(void);
void CL_GameAutoGo(actMis_t *mission);

qboolean AIR_SendAircraftToMission(aircraft_t * aircraft, actMis_t * mission);
void AIR_AircraftsNotifyMissionRemoved(const actMis_t * mission);

base_t *CP_GetMissionBase(void);
qboolean CP_SpawnBaseAttackMission(base_t* base, mission_t* mis, setState_t *cause);
qboolean CP_SpawnCrashSiteMission(aircraft_t* aircraft);
void CP_UFOSendMail(aircraft_t *ufocraft, base_t *base);

technology_t *CP_IsXVIResearched(void);

#endif /* CLIENT_CL_CAMPAIGN_H */
