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

#define MAX_MISSIONS	255
#define MAX_REQMISSIONS	4
#define MAX_ACTMISSIONS	16
#define MAX_SETMISSIONS	16

#define MAX_STAGESETS	256
#define MAX_STAGES		64

/* check for water */
/* blue value is 64 */
#define MapIsWater(color) (color[0] == 0 && color[1] == 0 && color[2] == 64)
#define MapIsArctic(color) (color[0] == 128 && color[1] == 255 && color[2] == 255)
#define MapIsDesert(color) (color[0] == 255 && color[1] == 128 && color[2] == 0)
/* others: */
/* red 255, 0, 0 */
/* yellow 255, 255, 0 */
/* green 128, 255, 0 */
/* violet 128, 0, 128 */
/* blue (not water) 128, 128, 255 */
/* blue (not water, too) 0, 0, 255 */

/** possible map types */
typedef enum mapType_s {
	MAPTYPE_CLIMAZONE,
	MAPTYPE_NATIONS,

	MAPTYPE_MAX
} mapType_t;

/** possible mission types */
typedef enum missionType_s {
	MIS_INTERCEPT, /* default */
	MIS_BASEATTACK,

	MIS_MAX
} missionType_t;

/** mission definition */
typedef struct mission_s {
	char *text;
	char name[MAX_VAR];
	char map[MAX_VAR];
	char loadingscreen[MAX_VAR];
	char param[MAX_VAR];
	char location[MAX_VAR];
	char nation[MAX_VAR];
	char type[MAX_VAR];
	char music[MAX_VAR];
	char alienTeam[MAX_VAR];
	char alienEquipment[MAX_VAR];
	char civTeam[MAX_VAR];
	char cmds[MAX_VAR];
	char onwin[MAX_VAR];
	char onlose[MAX_VAR];
	int ugv;					/* uncontrolled ground units (entity: info_ugv_start) */
	qboolean active;			/* aircraft at place? */
	qboolean onGeoscape;		/* already on geoscape - don't add it twice */
	date_t dateOnGeoscape;		/* last time the mission was on geoscape */
	qboolean storyRelated;		/* auto mission play disabled when true */
	missionType_t missionType;	/* type of mission */
	void* data;					/* may be related to mission type */
	vec2_t pos;
	byte mask[4];
	int aliens, civilians;
} mission_t;

typedef struct stageSet_s {
	char name[MAX_VAR];
	char needed[MAX_VAR];
	char nextstage[MAX_VAR];
	char endstage[MAX_VAR];
	char cmds[MAX_VAR];			/* script commands to execute when stageset gets activated */
	char sequence[MAX_VAR];		/* play a sequence when entering a new stage? */
	date_t delay;
	date_t frame;
	date_t expire;
	int number;					/* number of missions until set is deactivated (they only need to appear on geoscape) */
	int quota;					/* number of successfully ended missions until set gets deactivated */
	byte numMissions;			/* number of missions in this set */
	int missions[MAX_SETMISSIONS];	/* mission names in this set */
} stageSet_t;

typedef struct stage_s {
	char name[MAX_VAR];
	int first, num;
} stage_t;

typedef struct setState_s {
	stageSet_t *def;
	stage_t *stage;
	byte active;
	date_t start;
	date_t event;
	int num;
	int done;
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

/** campaign definition */
typedef struct campaign_s {
	int idx;					/**< own index in global campaign array */
	char id[MAX_VAR];
	char name[MAX_VAR];
	char team[MAX_VAR];
	char researched[MAX_VAR];
	char equipment[MAX_VAR];
	char market[MAX_VAR];
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

extern salary_t salaries[MAX_CAMPAIGNS];

#define MAX_NATION_BORDERS 64

/** nation definition */
typedef struct nation_s {
	char id[MAX_VAR];
	char name[MAX_VAR];
	int funding;		/**< how many (monthly) credits */
	float happiness;
	vec4_t color;
	vec2_t pos;			/**< nation name position on geoscape */
	float alienFriendly;
	int soldiers;		/**< how many (monthly) soldiers */
	int scientists;		/**< how many (monthly) scientists */
	int workers;		/**< how many (monthly) workers */
	int medics;			/**< how many (monthly) medics */
	int ugvs;			/**< how many (monthly) ugvs (robots) */
	char names[MAX_VAR];
	vec2_t borders[MAX_NATION_BORDERS];	/**< GL_LINE_LOOP coordinates */
	int numBorders;		/**< coordinate count */
} nation_t;

#define MAX_NATIONS 8

/** client structure */
typedef struct ccs_s {
	equipDef_t eMission, eMarket;

	stageState_t stage[MAX_STAGES];
	setState_t set[MAX_STAGESETS];
	actMis_t mission[MAX_ACTMISSIONS];
	int numMissions;

	vec2_t mapPos;
	vec2_t mapSize;

	qboolean singleplayer;	/**< singleplayer or multiplayer */

	int credits;			/**< actual credits amount */
	int civiliansKilled;	/**< how many civilians were killed already */
	int aliensKilled;		/**< how many aliens were killed already */
	date_t date;			/**< current date */
	float timer;

	vec3_t angles;			/**< 3d geoscape rotation */
	vec2_t center;
	float zoom;
} ccs_t;

/** possible geoscape actions */
typedef enum mapAction_s {
	MA_NONE,
	MA_NEWBASE,					/**< build a new base */
	MA_INTERCEPT,				/**< intercept */
	MA_BASEATTACK,				/**< base attacking */
	MA_UFORADAR					/**< ufos are in our radar */
} mapAction_t;

/** possible aircraft states */
typedef enum aircraftStatus_s {
	AIR_NONE = 0,
	AIR_REFUEL,					/**< refill fuel */
	AIR_HOME,					/**< in homebase */
	AIR_IDLE,					/**< just sit there on geoscape */
	AIR_TRANSIT,				/**< moving */
	AIR_MISSION,				/**< moving to a mission */
	AIR_UFO,					/**< purchasing an ufo */
	AIR_DROP,					/**< ready to drop down */
	AIR_INTERCEPT,				/**< ready to intercept */
	AIR_TRANSPORT,				/**< transporting from one base to another */
	AIR_RETURNING				/**< returning to homebase */
} aircraftStatus_t;

extern actMis_t *selMis;

extern campaign_t *curCampaign;
extern ccs_t ccs;

extern int mapAction;

void AIR_SaveAircraft(sizebuf_t * sb, base_t * base);
void AIR_LoadAircraft(sizebuf_t * sb, base_t * base, int version);

extern qboolean CL_MapIsNight(vec2_t pos);
void CL_ResetCampaign(void);
void CL_ResetSinglePlayerData(void);
void CL_DateConvert(date_t * date, int *day, int *month);
const char *CL_DateGetMonthName(int month);
void CL_CampaignRun(void);
void CL_GameTimeStop(void);
void CL_GameTimeFast(void);
void CL_GameTimeSlow(void);
extern byte *CL_GetMapColor(const vec2_t pos, mapType_t type);
extern qboolean CL_NewBase(vec2_t pos);
void CL_ParseMission(const char *name, char **text);
mission_t* CL_AddMission(const char *name);
void CL_ParseStage(const char *name, char **text);
void CL_ParseCampaign(const char *name, char **text);
void CL_ParseNations(const char *name, char **text);
#if 0
/* 20070228 Zenerka: new solution for collecting items from the battlefield
   see CL_CollectingItems(), CL_SellorAddItems(), CL_CollectingAmmo(), CL_CarriedItems() */
void CL_CollectItems(int won, int *number, int *credits);
#endif
void CL_CollectingItems(int won);
void CL_SellOrAddItems(void);
void CL_UpdateCredits(int credits);
qboolean CL_OnBattlescape(void);
void CL_GameInit (void);
extern float CP_GetDistance(const vec2_t pos1, const vec2_t pos2);
void CL_NewAircraft(base_t * base, const char *name);
void CL_ParseResearchedCampaignItems(const char *name, char **text);
void CL_ParseResearchableCampaignStates(const char *name, char **text, qboolean researchable);

campaign_t* CL_GetCampaign(const char* name);

#endif /* CLIENT_CL_CAMPAIGN_H */
