/**
 * @file cl_campaign.c
 * @brief Single player campaign control.
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

/* Constants */
#define DAYS_PER_YEAR 365
#define DAYS_PER_YEAR_AVG 365.25
#define MONTHS_PER_YEAR 12
#define BID_FACTOR 0.9

/* public vars */
static mission_t missions[MAX_MISSIONS];	/**< Document me. */
static int numMissions;				/**< Document me. */
actMis_t *selMis;				/**< Document me. */

static campaign_t campaigns[MAX_CAMPAIGNS];	/**< Document me. */
static int numCampaigns = 0;			/**< Document me. */

static stageSet_t stageSets[MAX_STAGESETS];	/**< Document me. */
static stage_t stages[MAX_STAGES];		/**< Document me. */
static int numStageSets = 0;			/**< Document me. */
static int numStages = 0;			/**< Document me. */

campaign_t *curCampaign;			/**< Document me. */
ccs_t ccs;					/**< Document me. */
base_t *baseCurrent;				/**< Pointer to current base. */

static byte *maskPic;				/**< Document me. */
static int maskWidth, maskHeight;		/**< Document me. */

static byte *nationsPic;			/**< Document me. */
static int nationsWidth, nationsHeight;		/**< Document me. */

#if 0
static int ever4hours;
#endif

/* extern in client.h */
stats_t stats;					/**< Document me. */

extern qboolean CL_SendAircraftToMission(aircraft_t * aircraft, actMis_t * mission);
extern void CL_AircraftsNotifyMissionRemoved(const actMis_t * mission);

/*
============================================================================
Boolean expression parser
============================================================================
*/

enum {
	BEPERR_NONE,
	BEPERR_KLAMMER,
	BEPERR_NOEND,
	BEPERR_NOTFOUND
} BEPerror;

static char varName[MAX_VAR];			/**< Document me. */

static qboolean(*varFunc)(char *var);
static qboolean CheckAND(char **s);

/**
 * @brief
 */
static void SkipWhiteSpaces (char **s)
{
	while (**s == ' ')
		(*s)++;
}

/**
 * @brief
 */
static void NextChar (char **s)
{
	(*s)++;
	/* skip white-spaces too */
	SkipWhiteSpaces(s);
}

/**
 * @brief
 */
static char *GetSwitchName (char **s)
{
	int pos = 0;

	while (**s > 32 && **s != '^' && **s != '|' && **s != '&' && **s != '!' && **s != '(' && **s != ')') {
		varName[pos++] = **s;
		(*s)++;
	}
	varName[pos] = 0;

	return varName;
}

/**
 * @brief
 */
static qboolean CheckOR (char **s)
{
	qboolean result = qfalse;
	int goon = 0;

	SkipWhiteSpaces(s);
	do {
		if (goon == 2)
			result ^= CheckAND(s);
		else
			result |= CheckAND(s);

		if (**s == '|') {
			goon = 1;
			NextChar(s);
		} else if (**s == '^') {
			goon = 2;
			NextChar(s);
		} else {
			goon = 0;
		}
	} while (goon && !BEPerror);

	return result;
}

/**
 * @brief
 */
static qboolean CheckAND (char **s)
{
	qboolean result = qtrue;
	qboolean negate = qfalse;
	qboolean goon = qfalse;
	int value;

	do {
		while (**s == '!') {
			negate ^= qtrue;
			NextChar(s);
		}
		if (**s == '(') {
			NextChar(s);
			result &= CheckOR(s) ^ negate;
			if (**s != ')')
				BEPerror = BEPERR_KLAMMER;
			NextChar(s);
		} else {
			/* get the variable state */
			value = varFunc(GetSwitchName(s));
			if (value == -1)
				BEPerror = BEPERR_NOTFOUND;
			else
				result &= value ^ negate;
			SkipWhiteSpaces(s);
		}

		if (**s == '&') {
			goon = qtrue;
			NextChar(s);
		} else {
			goon = qfalse;
		}
		negate = qfalse;
	} while (goon && !BEPerror);

	return result;
}

/**
 * @brief Boolean expression parser
 *
 * @param[in] expr
 * @param[in] varFuncParam Function pointer
 * @return qboolean
 * @sa CheckOR
 * @sa CheckAND
 */
static qboolean CheckBEP (char *expr, qboolean(*varFuncParam) (char *var))
{
	qboolean result;
	char *str;

	BEPerror = BEPERR_NONE;
	varFunc = varFuncParam;
	str = expr;
	result = CheckOR(&str);

	/* check for no end error */
	if (*str && !BEPerror)
		BEPerror = BEPERR_NOEND;

	switch (BEPerror) {
	case BEPERR_NONE:
		/* do nothing */
		return result;
	case BEPERR_KLAMMER:
		Com_Printf("')' expected in BEP (%s).\n", expr);
		return qtrue;
	case BEPERR_NOEND:
		Com_Printf("Unexpected end of condition in BEP (%s).\n", expr);
		return result;
	case BEPERR_NOTFOUND:
		Com_Printf("Variable '%s' not found in BEP (%s).\n", varName, expr);
		return qfalse;
	default:
		/* shouldn't happen */
		Com_Printf("Unknown CheckBEP error in BEP (%s).\n", expr);
		return qtrue;
	}
}


/* =========================================================== */

/**
 * @brief Calculate distance on the geoscape.
 * @param[in] pos1 Position at the start.
 * @param[in] pos2 Position at the end.
 * @return Distance from pos1 to pos2.
 */
extern float CP_GetDistance (const vec2_t pos1, const vec2_t pos2)
{
	float a, b, c;

	a = fabs(pos1[0] - pos2[0]);
	b = fabs(pos1[1] - pos2[1]);
	c = (a * a) + (b * b);
	return sqrt(c);
}


/**
 * @brief Check whether given position is Day or Night.
 * @param[in] pos Given position.
 * @return True if given position is Night.
 */
extern qboolean CL_MapIsNight (vec2_t pos)
{
	float p, q, a, root, x;

	p = (float) ccs.date.sec / (3600 * 24);
	q = (ccs.date.day + p) * 2 * M_PI / DAYS_PER_YEAR_AVG - M_PI;
	p = (0.5 + pos[0] / 360 - p) * 2 * M_PI - q;
	a = sin(pos[1] * torad);
	root = sqrt(1.0 - a * a);
	x = sin(p) * root * sin(q) - (a * SIN_ALPHA + cos(p) * root * COS_ALPHA) * cos(q);
	return (x > 0);
}


/**
 * @brief Check wheter given date and time is later than current date.
 * @param[in] now Current date.
 * @param[in] compare Date to compare.
 * @return True if current date is later than given one.
 */
static qboolean Date_LaterThan (date_t now, date_t compare)
{
	if (now.day > compare.day)
		return qtrue;
	if (now.day < compare.day)
		return qfalse;
	if (now.sec > compare.sec)
		return qtrue;
	return qfalse;
}


/**
 * @brief Add two dates and return the result.
 * @param[in] a First date.
 * @param[in] b Second date.
 * @return The result of adding date_ b to date_t a.
 */
static date_t Date_Add (date_t a, date_t b)
{
	a.sec += b.sec;
	a.day += (a.sec / (3600 * 24)) + b.day;
	a.sec %= 3600 * 24;
	return a;
}

#if (0)
/**
 * @brief
 */
static date_t Date_Random (date_t frame)
{
	frame.sec = (frame.day * 3600 * 24 + frame.sec) * frand();
	frame.day = frame.sec / (3600 * 24);
	frame.sec = frame.sec % (3600 * 24);
	return frame;
}
#endif

/**
 * @brief
 * @param[in] frame
 * @return
 */
static date_t Date_Random_Begin (date_t frame)
{
	int sec = frame.day * 3600 * 24 + frame.sec;

	/* first 1/3 of the frame */
	frame.sec = sec * frand() / 3;
	frame.day = frame.sec / (3600 * 24);
	frame.sec = frame.sec % (3600 * 24);
	return frame;
}


/**
 * @brief
 * @param[in] frame
 * @return
 */
static date_t Date_Random_Middle (date_t frame)
{
	int sec = frame.day * 3600 * 24 + frame.sec;

	/* middle 1/3 of the frame */
	frame.sec = sec / 3 + sec * frand() / 3 ;
	frame.day = frame.sec / (3600 * 24);
	frame.sec = frame.sec % (3600 * 24);
	return frame;
}


/* =========================================================== */


/**
 * @brief
 * @param[in] *color
 * @param[in] polar
 * @return
 */
static qboolean CL_MapMaskFind (byte * color, vec2_t polar)
{
	byte *c;
	int res, i, num;

	/* check color */
	if (!color[0] && !color[1] && !color[2])
		return qfalse;

	/* find possible positions */
	res = maskWidth * maskHeight;
	num = 0;
	for (i = 0, c = maskPic; i < res; i++, c += 4)
		if (c[0] == color[0] && c[1] == color[1] && c[2] == color[2])
			num++;

	/* nothing found? */
	if (!num)
		return qfalse;

	/* get position */
	num = rand() % num;
	for (i = 0, c = maskPic; i <= num; c += 4)
		if (c[0] == color[0] && c[1] == color[1] && c[2] == color[2])
			i++;

	/* transform to polar coords */
	res = (c - maskPic) / 4;
	polar[0] = 180.0 - 360.0 * ((float) (res % maskWidth) + 0.5) / maskWidth;
	polar[1] = 90.0 - 180.0 * ((float) (res / maskWidth) + 0.5) / maskHeight;
	Com_DPrintf("Set new coords for mission to %.0f:%.0f\n", polar[0], polar[1]);
	return qtrue;
}

/**
 * @brief Returns the color value from geoscape of maskPic at a given position.
 * @param[in] pos vec2_t Value of position on map to get the color value from.
 * @param[in] type
 * @return Returns the color value at given position.
 */
extern byte *CL_GetMapColor (const vec2_t pos, mapType_t type)
{
	int x, y;
	int width, height;
	byte *mask;

	switch (type) {
	case MAPTYPE_CLIMAZONE:
		mask = maskPic;
		width = maskWidth;
		height = maskHeight;
		break;
	case MAPTYPE_NATIONS:
		mask = nationsPic;
		width = nationsWidth;
		height = nationsHeight;
		break;
	default:
		Sys_Error("Unknown maptype %i\n", type);
	}

	/* get coordinates */
	x = (180 - pos[0]) / 360 * maskWidth;
	y = (90 - pos[1]) / 180 * maskHeight;
	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;

	return maskPic + 4 * (x + y * maskWidth);
}

/**
 * @brief Check conditions for new base and build it.
 * @param[in] pos Position on the geoscape.
 * @return True if the base has been build.
 * @sa B_BuildBase
 */
extern qboolean CL_NewBase (vec2_t pos)
{
	byte *color;
	const char *zoneType = NULL;

	assert(baseCurrent);

	if (baseCurrent->founded) {
		Com_DPrintf("CL_NewBase: base already founded: %i\n", baseCurrent->idx);
		return qfalse;
	} else if (gd.numBases == MAX_BASES) {
		Com_DPrintf("CL_NewBase: max base limit hit\n");
		return qfalse;
	}

	color = CL_GetMapColor(pos, MAPTYPE_CLIMAZONE);

	if (MapIsWater(color)) {
		/* This should already have been catched in MAP_MapClick (cl_menu.c), but just in case. */
		MN_AddNewMessage(_("Notice"), _("Could not set up your base at this location"), qfalse, MSG_INFO, NULL);
		return qfalse;
	} else {
		zoneType = MAP_GetZoneType(color);
		Com_DPrintf("CL_NewBase: zoneType: '%s'\n", zoneType);
		baseCurrent->mapChar = zoneType[0];
	}

	Com_DPrintf("Colorvalues for base: R:%i G:%i B:%i\n", color[0], color[1], color[2]);

	/* build base */
	Vector2Copy(pos, baseCurrent->pos);

	gd.numBases++;

	/* set up the base with buildings that have the autobuild flag set */
	B_SetUpBase();

	return qtrue;
}


static stage_t *testStage;			/**< Document me. */

/**
 * @brief Checks wheter a stage set exceeded the quota
 * @param[in] *name Stage set name from script file
 * @return qboolean
 */
static qboolean CL_StageSetDone (char *name)
{
	setState_t *set;
	int i;

	for (i = 0, set = &ccs.set[testStage->first]; i < testStage->num; i++, set++)
		if (!Q_strncmp(name, set->def->name, MAX_VAR)) {
			if (set->def->number && set->num >= set->def->number)
				return qtrue;
			if (set->def->quota && set->done >= set->def->quota)
				return qtrue;
		}
	return qfalse;
}


/**
 * @brief
 * @param[in] *stage
 * @sa CL_CampaignExecute
 */
static void CL_CampaignActivateStageSets (stage_t *stage)
{
	setState_t *set;
	int i;

	testStage = stage;
#ifdef PARANOID
	if (stage->first + stage->num >= MAX_STAGESETS)
		Sys_Error("CL_CampaignActivateStageSets '%s' (first: %i, num: %i)\n", stage->name, stage->first, stage->num);
#endif
	for (i = 0, set = &ccs.set[stage->first]; i < stage->num; i++, set++)
		if (!set->active && !set->num) {
			Com_DPrintf("CL_CampaignActivateStageSets: i = %i, stage->first = %i, stage->num = %i, stage->name = %s\n", i, stage->first, stage->num, stage->name);
			assert(!set->done); /* if not started, not done, as well */
			assert(set->stage);
			assert(set->def);

			/* check needed sets */
			if (set->def->needed[0] && !CheckBEP(set->def->needed, CL_StageSetDone))
				continue;

			Com_DPrintf("Activated: set->def->name = %s.\n", set->def->name);

			/* activate it */
			set->active = qtrue;
			set->start = Date_Add(ccs.date, set->def->delay);
			set->event = Date_Add(set->start, Date_Random_Begin(set->def->frame));
			if (*(set->def->sequence))
				Cbuf_ExecuteText(EXEC_APPEND, va("seq_start %s;\n", set->def->sequence));
		}
}


/**
 * @brief
 * @sa CL_CampaignActivateStageSets
 * @sa CL_CampaignExecute
 */
static stageState_t *CL_CampaignActivateStage (const char *name, qboolean setsToo)
{
	stage_t *stage;
	stageState_t *state;
	int i, j;

	for (i = 0, stage = stages; i < numStages; i++, stage++) {
		if (!Q_strncmp(stage->name, name, MAX_VAR)) {
			Com_Printf("Activate stage %s\n", stage->name);
			/* add it to the list */
			state = &ccs.stage[i];
			state->active = qtrue;
			state->def = stage;
			state->start = ccs.date;

			/* add stage sets */
			for (j = stage->first; j < stage->first + stage->num; j++) {
				memset(&ccs.set[j], 0, sizeof(setState_t));
				ccs.set[j].stage = stage;
				ccs.set[j].def = &stageSets[j];
			}
			/* activate stage sets */
			if (setsToo)
				CL_CampaignActivateStageSets(stage);

			return state;
		}
	}

	Com_Printf("CL_CampaignActivateStage: stage '%s' not found.\n", name);
	return NULL;
}


/**
 * @brief
 * @sa CL_CampaignExecute
 */
static void CL_CampaignEndStage (const char *name)
{
	stageState_t *state;
	int i;
	for (i = 0, state = ccs.stage; i < numStages; i++, state++) {
		if (state->def->name != NULL) {
			if (!Q_strncmp(state->def->name, name, MAX_VAR)) {
				state->active = qfalse;
				return;
			}
		}
	}

	Com_Printf("CL_CampaignEndStage: stage '%s' not found.\n", name);
}


/**
 * @brief
 * @sa CL_CampaignActivateStage
 * @sa CL_CampaignEndStage
 * @sa CL_CampaignActivateStageSets
 */
static void CL_CampaignExecute (setState_t * set)
{
	/* handle stages, execute commands */
	if (*set->def->nextstage)
		CL_CampaignActivateStage(set->def->nextstage, qtrue);

	if (*set->def->endstage)
		CL_CampaignEndStage(set->def->endstage);

	if (*set->def->cmds)
		Cbuf_AddText(set->def->cmds);

	/* activate new sets in old stage */
	CL_CampaignActivateStageSets(set->stage);
}

#define DETAILSWIDTH 14
/**
 * @brief Console command to list all available missions
 */
static void CP_MissionList_f (void)
{
	int i;
	qboolean details = qfalse;
	char tmp[DETAILSWIDTH+1];

	if (Cmd_Argc() > 1)
		details = qtrue;
	else
		Com_Printf("Use defails as parameter to get a more detailed list\n");

	/* detail header */
	if (details) {
		Com_Printf("| %-14s | %-14s | %-14s | %-14s | #  | %-14s | %-14s | #  |\n|", "id", "map", "param", "alienteam", "alienequip", "civteam");
		for (i = 0; i < 4; i++)
			Com_Printf("----------------|");
		Com_Printf("----|----------------|----------------|----|");
		Com_Printf("\n");
	}

	for (i = 0; i < numMissions; i++) {
		if (details) {
			Q_strncpyz(tmp, missions[i].name, sizeof(tmp));
			Com_Printf("| %-*s | ", DETAILSWIDTH, tmp);
			Q_strncpyz(tmp, missions[i].map, sizeof(tmp));
			Com_Printf("%-*s | ", DETAILSWIDTH, tmp);
			Q_strncpyz(tmp, missions[i].param, sizeof(tmp));
			Com_Printf("%-*s | ", DETAILSWIDTH, tmp);
			Q_strncpyz(tmp, missions[i].alienTeam, sizeof(tmp));
			Com_Printf("%-*s | ", DETAILSWIDTH, tmp);
			Q_strncpyz(tmp, missions[i].alienTeam, sizeof(tmp));
			Com_Printf("%02i | ", missions[i].aliens);
			Q_strncpyz(tmp, missions[i].alienEquipment, sizeof(tmp));
			Com_Printf("%-*s | ", DETAILSWIDTH, tmp);
			Q_strncpyz(tmp, missions[i].civTeam, sizeof(tmp));
			Com_Printf("%-*s | ", DETAILSWIDTH, tmp);
			Com_Printf("%02i |", missions[i].civilians);
			Com_Printf("\n");
		} else
			Com_Printf("%s\n", missions[i].name);
	}
}

#define DIST_MIN_BASE_MISSION 4
/**
 * @brief
 * @sa CL_CampaignRemoveMission
 * @sa CL_CampaignCheckEvents
 */
static void CL_CampaignAddMission (setState_t * set)
{
	actMis_t *mis;

	mission_t *misTemp;
	int i;
	float f;

	/* add mission */
	if (ccs.numMissions >= MAX_ACTMISSIONS) {
		Com_Printf("Too many active missions!\n");
		return;
	}

	while (1) {
		misTemp = &missions[set->def->missions[rand() % set->def->numMissions]];

		if ((set->def->numMissions < 2
			 || Q_strncmp(misTemp->name, gd.oldMis1, MAX_VAR))
			&& (set->def->numMissions < 3
				|| Q_strncmp(misTemp->name, gd.oldMis2, MAX_VAR))
			&& (set->def->numMissions < 4
				|| Q_strncmp(misTemp->name, gd.oldMis3, MAX_VAR)))
			break;
	}

	/* maybe the mission is already on geoscape --- e.g. one-mission sets */
	if (misTemp->onGeoscape) {
		Com_DPrintf("Mission is already on geoscape\n");
		return;
	} else {
		misTemp->onGeoscape = qtrue;
	}
	mis = &ccs.mission[ccs.numMissions++];
	memset(mis, 0, sizeof(actMis_t));

	/* set relevant info */
	mis->def = misTemp;
	mis->cause = set;
	Q_strncpyz(gd.oldMis3, gd.oldMis2, sizeof(gd.oldMis3));
	Q_strncpyz(gd.oldMis2, gd.oldMis1, sizeof(gd.oldMis2));
	Q_strncpyz(gd.oldMis1, misTemp->name, sizeof(gd.oldMis1));

	/* execute mission commands */
	if (*mis->def->cmds)
		Cbuf_ExecuteText(EXEC_NOW, mis->def->cmds);

	if (set->def->expire.day)
		mis->expire = Date_Add(ccs.date, Date_Random_Middle(set->def->expire));

	/* there can be more than one baseattack mission */
	/* just use baseattack1, baseattack2 and so on as mission names */
	if (!Q_strncmp(mis->def->name, "baseattack", 10)) {
		i = rand() % gd.numBases;
		/* the first founded base is more likely to be attacked */
		if (!gd.bases[i].founded) {
			for (i = 0; i < MAX_BASES; i++) {
				if (gd.bases[i].founded)
					break;
			}
			/* at this point there should be at least one base */
			if (i == MAX_BASES)
				Sys_Error("No bases found\n");
		}

		B_BaseAttack(&gd.bases[i]);
		mis->realPos[0] = gd.bases[i].pos[0];
		mis->realPos[1] = gd.bases[i].pos[1];
		Q_strncpyz(mis->def->location, gd.bases[i].name, sizeof(mis->def->location));
		/* set the mission type to base attack and store the base in data pointer */
		/* this is useful if the mission expires and we want to know which base it was */
		mis->def->missionType = MIS_BASEATTACK;
		mis->def->data = (void*)&gd.bases[i];

		/* Add message to message-system. */
		Com_sprintf(messageBuffer, sizeof(messageBuffer), _("Your base %s is under attack."), gd.bases[i].name);
		MN_AddNewMessage(_("Base Attack"), messageBuffer, qfalse, MSG_BASEATTACK, NULL);
		gd.mapAction = MA_BASEATTACK;
	} else {
		/* A mission must not be very near a base */
		for (i = 0; i < gd.numBases; i++) {
			if (CP_GetDistance(mis->def->pos, gd.bases[i].pos) < DIST_MIN_BASE_MISSION) {
				f = frand();
				mis->def->pos[0] = gd.bases[i].pos[0] + (gd.bases[i].pos[0] < 0 ? f * DIST_MIN_BASE_MISSION : -f * DIST_MIN_BASE_MISSION);
				f = sin(acos(f));
				mis->def->pos[1] = gd.bases[i].pos[1] + (gd.bases[i].pos[1] < 0 ? f * DIST_MIN_BASE_MISSION : -f * DIST_MIN_BASE_MISSION);
				break;
			}
		}
		/* get default position first, then try to find a corresponding mask color */
		mis->realPos[0] = mis->def->pos[0];
		mis->realPos[1] = mis->def->pos[1];
		CL_MapMaskFind(mis->def->mask, mis->realPos);

		/* Add message to message-system. */
		Com_sprintf(messageBuffer, sizeof(messageBuffer), _("Alien activity has been reported: '%s'"), mis->def->location);
		MN_AddNewMessage(_("Alien activity"), messageBuffer, qfalse, MSG_TERRORSITE, NULL);
		Com_DPrintf("Alien activity at long: %.0f lat: %0.f\n", mis->realPos[0], mis->realPos[1]);
	}

	/* prepare next event (if any) */
	set->num++;
	if (set->def->number && set->num >= set->def->number)
		set->active = qfalse;
	else
		set->event = Date_Add(ccs.date, Date_Random_Middle(set->def->frame));

	/* stop time */
	CL_GameTimeStop();
}

/**
 * @brief
 * @sa CL_CampaignAddMission
 */
static void CL_CampaignRemoveMission (actMis_t * mis)
{
	int i, num;
	base_t *base;

	num = mis - ccs.mission;
	if (num >= ccs.numMissions) {
		Com_Printf("CL_CampaignRemoveMission: Can't remove mission.\n");
		return;
	}

	/* allow respawn on geoscape */
	mis->def->onGeoscape = qfalse;

	/* Clear base-attack status if required */
	if (mis->def->missionType == MIS_BASEATTACK) {
		base = (base_t*)mis->def->data;
		B_BaseResetStatus(base);
	}

	/* Notifications */
	MAP_NotifyMissionRemoved(mis);
	CL_AircraftsNotifyMissionRemoved(mis);
	CL_PopupNotifyMIssionRemoved(mis);

	ccs.numMissions--;
	Com_DPrintf("%i missions left\n", ccs.numMissions);
	for (i = num; i < ccs.numMissions; i++)
		ccs.mission[i] = ccs.mission[i + 1];
}

/**
 * @brief Builds the aircraft list for textfield with id
 */
static void CL_AircraftList_f (void)
{
	char *s;
	int i, j;
	aircraft_t *aircraft;
	static char aircraftListText[1024];

	memset(aircraftListText, 0, sizeof(aircraftListText));
	for (j = 0; j < gd.numBases; j++) {
		if (!gd.bases[j].founded)
			continue;

		for (i = 0; i < gd.bases[j].numAircraftInBase; i++) {
			aircraft = &gd.bases[j].aircraft[i];
			s = va("%s (%i/%i)\t%s\t%s\n", aircraft->name, *aircraft->teamSize, aircraft->size, CL_AircraftStatusToName(aircraft), gd.bases[j].name);
			Q_strcat(aircraftListText, s, sizeof(aircraftListText));
		}
	}

	menuText[TEXT_AIRCRAFT_LIST] = aircraftListText;
}

/**
 * @brief Updates each nation's happiness and mission win/loss stats.
 * Should be called at the completion or expiration of every mission.
 * The nation where the mission took place will be most affected,
 * surrounding nations will be less affected.
 * TODO: nations react way too much; independently, an asymptotic reaction near 0.0 and 1.0 would be nice; high alienFriendly factor seems to increase happiness, instead of decreasing it.
 */
static void CL_HandleNationData (qboolean lost, int civiliansSurvived, int civiliansKilled, int aliensSurvived, int aliensKilled, actMis_t * mis)
{
	int i, is_on_Earth = 0;
	int civilianSum = civiliansKilled + civiliansSurvived;
	float civilianRatio = civilianSum ? (float)civiliansSurvived / (float)civilianSum : 0.;
	int alienSum = aliensKilled + aliensSurvived;
	float alienRatio = alienSum ? (float)aliensKilled / (float)alienSum : 0.;
	float performance = 0.5 + civilianRatio * 0.25 + alienRatio * 0.25;

	if (lost)
		stats.missionsLost++;
	else
		stats.missionsWon++;

	for (i = 0; i < gd.numNations; i++) {
		nation_t *nation = &gd.nations[i];
		float alienHostile = 1.0 - (float) nation->alienFriendly / 100.0;
		float happiness = nation->happiness;

		if (lost) {
			if (!Q_strcmp(nation->id, mis->def->nation)) {
				/* Strong negative reaction */
				happiness *= performance * alienHostile;
				is_on_Earth++;
			} else {
				/* Minor negative reaction */
				happiness *= 1.0 - pow(1.0 - performance * alienHostile, 5.0);
			}
		} else {
			if (!Q_strcmp(nation->id, mis->def->nation)) {
				/* Strong positive reaction */
				happiness += performance * alienHostile / 5.0;
				is_on_Earth++;
			} else {
				/* Minor positive reaction */
				happiness += performance * alienHostile / 50.0;
			}
			if (nation->happiness > 1.0) {
				/* Can't be more than 100% happy with you. */
				happiness = 1.0;
			}
		}
		nation->happiness = nation->happiness * 0.40 + happiness * 0.60;
	}
	if (!is_on_Earth)
		Com_DPrintf("CL_HandleNationData: Warning, mission '%s' located in an unknown country '%s'.\n", mis->def->name, mis->def->nation);
	else if (is_on_Earth > 1)
		Com_DPrintf("CL_HandleNationData: Error, mission '%s' located in many countries '%s'.\n", mis->def->name, mis->def->nation);
}

/**
 * @brief Deletes employees from a base along with all base equipment.
 * Called when invading forces overrun a base after a base-attack mission
 * @param[in] *base base_t base to be ransacked
 */
void CL_BaseRansacked (base_t *base)
{
	int item, ac;

	if (!base)
		return;

	/* Delete all employees from the base & the global list. */
	E_DeleteAllEmployees(base);

	/* Destroy all items in storage */
	for (item = 0; item < csi.numODs; item++) {
		base->storage.num[item] = 0;
		base->storage.num_loose[item] = 0;
	}

	/* Remove all aircrafts from the base. */
	for (ac = base->numAircraftInBase-1; ac >= 0; ac--)
		CL_DeleteAircraft(&base->aircraft[ac]);

	/* TODO: Maybe reset research in progress. ... needs playbalance
	 *       need another value in technology_t to remember researched
	 *       time from other bases?
	 * TODO: Destroy (or better: just damage) some random buildings. */

	Com_sprintf(messageBuffer, MAX_MESSAGE_TEXT, _("Your base: %s has been ransacked! All employees killed and all equipment destroyed."), base->name);
	MN_AddNewMessage(_("Notice"), messageBuffer, qfalse, MSG_STANDARD, NULL);
	CL_GameTimeStop();
}


/**
 * @brief
 * @sa CL_CampaignAddMission
 * @sa CL_CampaignExecute
 * @sa UFO_CampaignCheckEvents
 * @sa CL_CampaignRemoveMission
 * @sa CL_HandleNationData
 */
static void CL_CampaignCheckEvents (void)
{
	stageState_t *stage = NULL;
	setState_t *set = NULL;
	actMis_t *mis = NULL;
	base_t *base = NULL;

	int i, j;

	/* check campaign events */
	for (i = 0, stage = ccs.stage; i < numStages; i++, stage++)
		if (stage->active)
			for (j = 0, set = &ccs.set[stage->def->first]; j < stage->def->num; j++, set++)
				if (set->active && set->event.day && Date_LaterThan(ccs.date, set->event)) {
					if (set->def->numMissions) {
						CL_CampaignAddMission(set);
						if (gd.mapAction == MA_NONE) {
							gd.mapAction = MA_INTERCEPT;
							CL_AircraftList_f();
						}
					} else
						CL_CampaignExecute(set);
				}

	/* Check UFOs events. */
	UFO_CampaignCheckEvents();

	/* Let missions expire. */
	for (i = 0, mis = ccs.mission; i < ccs.numMissions; i++, mis++)
		if (mis->expire.day && Date_LaterThan(ccs.date, mis->expire)) {
			/* Mission is expired. Calculating penalties for the various mission types. */
			switch (mis->def->missionType) {
			case MIS_BASEATTACK:
				/* Base attack mission never attended to, so
				 * invaders had plenty of time to ransack it */
				base = (base_t*)mis->def->data;
				CL_HandleNationData(1, 0, mis->def->civilians, mis->def->aliens, 0, mis);
				CL_BaseRansacked(base);
				break;
			case MIS_INTERCEPT:
				/* Normal ground mission. */
				CL_HandleNationData(1, 0, mis->def->civilians, mis->def->aliens, 0, mis);
				Q_strncpyz(messageBuffer, va(ngettext("The mission expired and %i civilian died.", "The mission expired and %i civilians died.", mis->def->civilians), mis->def->civilians), MAX_MESSAGE_TEXT);
				MN_AddNewMessage(_("Notice"), messageBuffer, qfalse, MSG_STANDARD, NULL);
				break;
			default:
				Sys_Error("Unknown missionType for '%s'\n", mis->def->name);
				break;
			}
			/* Remove mission from the map. */
			if (mis->cause->def->number && mis->cause->num >= mis->cause->def->number)
				CL_CampaignExecute(mis->cause);
			CL_CampaignRemoveMission(mis);
		}
}

static const int monthLength[MONTHS_PER_YEAR] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

/**
 * @brief Converts a number of days into the number of the current month and the current day in this month.
 * @note The seconds from "date" are ignored here.
 * @note The function always starts calculation from Jan. and also catches new years.
 * @param[in] date Contains the number of days to be converted.
 * @param[out] month The month.
 * @param[out] day The day in the month above.
 */
extern void CL_DateConvert (date_t * date, int *day, int *month)
{
	int i, d;

	/* Get the day in the year. Other years are ignored. */
	d = date->day % DAYS_PER_YEAR;

	/* Subtract days until no full month is left. */
	for (i = 0; d >= monthLength[i]; i++)
		d -= monthLength[i];

	/* Prepare return values. */
	*day = d + 1;
	*month = i;
}

/**
 * @brief Returns the monatname to the given month index
 * @param[in] month The month index - starts at 0 - ends at 11
 * @return month name as char*
 */
extern const char *CL_DateGetMonthName (int month)
{
	switch (month) {
	case 0:
		return _("Jan");
	case 1:
		return _("Feb");
	case 2:
		return _("Mar");
	case 3:
		return _("Apr");
	case 4:
		return _("May");
	case 5:
		return _("Jun");
	case 6:
		return _("Jul");
	case 7:
		return _("Aug");
	case 8:
		return _("Sep");
	case 9:
		return _("Oct");
	case 10:
		return _("Nov");
	case 11:
		return _("Dec");
	default:
		return "Error";
	}
}

/**
 * @brief Translates the nation happiness float value to a string
 * @param[in] nation
 * @return Translated happiness string
 */
static const char* CL_GetNationHappinessString (nation_t* nation)
{
	if (nation->happiness < 0.015)
		return _("Giving up");
	else if (nation->happiness < 0.025)
		return _("Furious");
	else if (nation->happiness < 0.04)
		return _("Angry");
	else if (nation->happiness < 0.06)
		return _("Mad");
	else if (nation->happiness < 0.10)
		return _("Upset");
	else if (nation->happiness < 0.20)
		return _("Tolerant");
	else if (nation->happiness < 0.30)
		return _("Neutral");
	else if (nation->happiness < 0.50)
		return _("Content");
	else if (nation->happiness < 0.70)
		return _("Pleased");
	else if (nation->happiness < 1.0)
		return _("Happy");
	else
		return _("Exuberant");
}

/**
 * @brief Update the nation data from all parsed nation each month
 * @note give us nation support by:
 * * credits
 * * new soldiers
 * * new scientists
 * @note Called from CL_CampaignRun
 * @sa CL_CampaignRun
 * @sa B_CreateEmployee
 */
static void CL_HandleBudget (void)
{
	int i, j;
	char message[1024];
	int funding;
	int cost;
	nation_t *nation;
	int initial_credits = ccs.credits;
	int new_scientists, new_medics, new_soldiers, new_workers;

	for (i = 0; i < gd.numNations; i++) {
		nation = &gd.nations[i];
		funding = nation->funding * nation->happiness;
		CL_UpdateCredits(ccs.credits + funding);

		new_scientists = new_medics = new_soldiers = new_workers = 0;

		for (j = 0; 0.25 + j < (float) nation->scientists * nation->happiness * nation->happiness; j++) {
			/* Create a scientist, but don't auto-hire her. */
			E_CreateEmployee(EMPL_SCIENTIST);
			new_scientists++;
		}

		for (j = 0; 0.25 + j * 3 < (float) nation->scientists * nation->happiness; j++) {
			/* Create a medic. */
			E_CreateEmployee(EMPL_MEDIC);
			new_medics++;
		}

		for (j = 0; 0.25 + j < (float) nation->soldiers * nation->happiness * nation->happiness * nation->happiness; j++) {
			/* Create a soldier. */
			E_CreateEmployee(EMPL_SOLDIER);
			new_soldiers++;
		}

		for (j = 0; 0.25 + j * 2 < (float) nation->soldiers * nation->happiness; j++) {
			/* Create a worker. */
			E_CreateEmployee(EMPL_WORKER);
			new_workers++;
		}

		Com_sprintf(message, sizeof(message), _("Gained %i %s, %i %s, %i %s, %i %s, and %i %s from nation %s (%s)"),
					funding, ngettext("credit", "credits", funding),
					new_scientists, ngettext("scientist", "scientists", new_scientists),
					new_medics, ngettext("medic", "medics", new_medics),
					new_soldiers, ngettext("soldier", "soldiers", new_soldiers),
					new_workers, ngettext("worker", "workers", new_workers),
					_(nation->name), CL_GetNationHappinessString(nation));
		MN_AddNewMessage(_("Notice"), message, qfalse, MSG_STANDARD, NULL);
	}

	cost = 0;
	for (i = 0; i < gd.numEmployees[EMPL_SOLDIER]; i++) {
		if (gd.employees[EMPL_SOLDIER][i].hired)
			cost += SALARY_SOLDIER_BASE + gd.employees[EMPL_SOLDIER][i].chr.rank * SALARY_SOLDIER_RANKBONUS;
	}

	Com_sprintf(message, sizeof(message), _("Paid %i credits to soldiers"), cost);
	CL_UpdateCredits(ccs.credits - cost);
	MN_AddNewMessage(_("Notice"), message, qfalse, MSG_STANDARD, NULL);

	cost = 0;
	for (i = 0; i < gd.numEmployees[EMPL_WORKER]; i++) {
		if (gd.employees[EMPL_WORKER][i].hired)
			cost += SALARY_WORKER_BASE + gd.employees[EMPL_WORKER][i].chr.rank * SALARY_WORKER_RANKBONUS;
	}

	Com_sprintf(message, sizeof(message), _("Paid %i credits to workers"), cost);
	CL_UpdateCredits(ccs.credits - cost);
	MN_AddNewMessage(_("Notice"), message, qfalse, MSG_STANDARD, NULL);

	cost = 0;
	for (i = 0; i < gd.numEmployees[EMPL_SCIENTIST]; i++) {
		if (gd.employees[EMPL_SCIENTIST][i].hired)
			cost += SALARY_SCIENTIST_BASE + gd.employees[EMPL_SCIENTIST][i].chr.rank * SALARY_SCIENTIST_RANKBONUS;
	}

	Com_sprintf(message, sizeof(message), _("Paid %i credits to scientists"), cost);
	CL_UpdateCredits(ccs.credits - cost);
	MN_AddNewMessage(_("Notice"), message, qfalse, MSG_STANDARD, NULL);

	cost = 0;
	for (i = 0; i < gd.numEmployees[EMPL_MEDIC]; i++) {
		if (gd.employees[EMPL_MEDIC][i].hired)
			cost += SALARY_MEDIC_BASE + gd.employees[EMPL_MEDIC][i].chr.rank * SALARY_MEDIC_RANKBONUS;
	}

	Com_sprintf(message, sizeof(message), _("Paid %i credits to medics"), cost);
	CL_UpdateCredits(ccs.credits - cost);
	MN_AddNewMessage(_("Notice"), message, qfalse, MSG_STANDARD, NULL);

	cost = 0;
	for (i = 0; i < gd.numEmployees[EMPL_ROBOT]; i++) {
		if (gd.employees[EMPL_ROBOT][i].hired)
			cost += SALARY_ROBOT_BASE + gd.employees[EMPL_ROBOT][i].chr.rank * SALARY_ROBOT_RANKBONUS;
	}

	if (cost != 0) {
		Com_sprintf(message, sizeof(message), _("Paid %i credits for robots"), cost);
		CL_UpdateCredits(ccs.credits - cost);
		MN_AddNewMessage(_("Notice"), message, qfalse, MSG_STANDARD, NULL);
	}

	cost = 0;
	for (i = 0; i < gd.numBases; i++) {
		for (j = 0; j < gd.bases[i].numAircraftInBase; j++) {
			cost += gd.bases[i].aircraft[j].price * SALARY_AIRCRAFT_FACTOR / SALARY_AIRCRAFT_DIVISOR;
		}
	}

	if (cost != 0) {
		Com_sprintf(message, sizeof(message), _("Paid %i credits for aircraft"), cost);
		CL_UpdateCredits(ccs.credits - cost);
		MN_AddNewMessage(_("Notice"), message, qfalse, MSG_STANDARD, NULL);
	}

	for (i = 0; i < gd.numBases; i++) {
		cost = SALARY_BASE_UPKEEP;	/* base cost */
		for (j = 0; j < gd.numBuildings[i]; j++) {
			cost += gd.buildings[i][j].varCosts;
		}

		Com_sprintf(message, sizeof(message), _("Paid %i credits for upkeep of base %s"), cost, gd.bases[i].name);
		MN_AddNewMessage(_("Notice"), message, qfalse, MSG_STANDARD, NULL);
		CL_UpdateCredits(ccs.credits - cost);
	}

	cost = SALARY_ADMIN_INITIAL + gd.numEmployees[EMPL_SOLDIER] * SALARY_ADMIN_SOLDIER + gd.numEmployees[EMPL_WORKER] * SALARY_ADMIN_WORKER + gd.numEmployees[EMPL_SCIENTIST] * SALARY_ADMIN_SCIENTIST + gd.numEmployees[EMPL_MEDIC] * SALARY_ADMIN_MEDIC + gd.numEmployees[EMPL_ROBOT] * SALARY_ADMIN_ROBOT;
	Com_sprintf(message, sizeof(message), _("Paid %i credits for administrative overhead."), cost);
	CL_UpdateCredits(ccs.credits - cost);
	MN_AddNewMessage(_("Notice"), message, qfalse, MSG_STANDARD, NULL);

	if (initial_credits < 0) {
		float interest = initial_credits * SALARY_DEBT_INTEREST;

		cost = (int)ceil(interest);
		Com_sprintf(message, sizeof(message), _("Paid %i credits in interest on your debt."), cost);
		CL_UpdateCredits(ccs.credits - cost);
		MN_AddNewMessage(_("Notice"), message, qfalse, MSG_STANDARD, NULL);
	}
	CL_GameTimeStop();
}

/**
 * @brief sets market prices at start of the game
 * @sa CL_GameInit
 */
static void CL_CampaignInitMarket (void)
{
	int i;
	for (i = 0; i < csi.numODs; i++) {
		if(ccs.eMarket.ask[i] == 0)
		{
			ccs.eMarket.ask[i] = csi.ods[i].price;
			ccs.eMarket.bid[i] = floor(ccs.eMarket.ask[i]*BID_FACTOR);
		}
	}
}

/**
 * @brief simulates one hour of supply and demand on the market (adds items and sets prices)
 * @sa CL_CampaignRun
 * @sa CL_GameNew
 */
static void CL_CampaignRunMarket (void)
{
	int i, market_action;
	equipDef_t *ed;
	double research_factor, price_factor, curr_supp_diff;
	/* supply and demand */
	const double mrs1 = 0.1, mpr1 = 400, mrg1 = 0.002, mrg2 = 0.03;

	/* find the relevant market */
	for (i = 0, ed = csi.eds; i < csi.numEDs; i++, ed++)
		if (!Q_strncmp(curCampaign->market, ed->name, MAX_VAR))
			break;
	assert (i != csi.numEDs);


	/* TODO: Save eMarket into savefiles */
	/* TODO: Take starting date from compaign description instead of using fixed '760439' */
	/* TODO: Find out why there is a 58 days discrepancy in reasearched_date*/

	for (i = 0; i < csi.numODs; i++) {
		if (RS_ItemIsResearched(csi.ods[i].id)) {
			/* supply balance */
			technology_t *tech = RS_GetTechByProvided(csi.ods[i].id);
			int reasearched_date = tech->researchedDateDay + tech->researchedDateMonth*30 +  tech->researchedDateYear*DAYS_PER_YEAR - 2;
			if (reasearched_date <= curCampaign->date.sec/86400 + curCampaign->date.day)
				reasearched_date -= 100;
			research_factor = mrs1 * sqrt(ccs.date.day - reasearched_date);
			price_factor = mpr1/sqrt(csi.ods[i].price+1);
			curr_supp_diff = floor(research_factor*price_factor - ccs.eMarket.num[i]);
			if(curr_supp_diff != 0)
				ccs.eMarket.cumm_supp_diff[i] += curr_supp_diff;
			else
				ccs.eMarket.cumm_supp_diff[i] *= 0.9;
			market_action = floor(mrg1*ccs.eMarket.cumm_supp_diff[i] + mrg2*curr_supp_diff);
			ccs.eMarket.num[i] += market_action;
			if (ccs.eMarket.num[i]<0)
				ccs.eMarket.num[i]=0;

			/* set item price based on supply imbalance */
			if(research_factor*price_factor >= 1)
				ccs.eMarket.ask[i] = floor( csi.ods[i].price*(1-(1-BID_FACTOR)/2*(1/(1+exp(curr_supp_diff/(research_factor*price_factor)))*2-1)) );
			else
				ccs.eMarket.ask[i] = csi.ods[i].price;
			ccs.eMarket.bid[i] = floor(ccs.eMarket.ask[i]*BID_FACTOR);
		}
	}
}

/**
 * @brief Called every frame when we are in geoscape view
 * @note Called for node types MN_MAP and MN_3DMAP
 * @sa MN_DrawMenus
 * @sa CL_HandleBudget
 * @sa B_UpdateBaseData
 * @sa CL_CampaignRunAircraft
 * @sa CL_CampaignCheckEvents
 */
extern void CL_CampaignRun (void)
{
	/* advance time */
	ccs.timer += cls.frametime * gd.gameTimeScale;
	if (ccs.timer >= 1.0) {
		/* calculate new date */
		int dt, day, month, currenthour;

		dt = (int)floor(ccs.timer);
		currenthour = (int)floor(ccs.date.sec / 3600);
		ccs.date.sec += dt;
		ccs.timer -= dt;

		/* compute hourly events  */
		/* (this may run multiple times if the time stepping is > 1 hour at a time) */
		while (currenthour < (int)floor(ccs.date.sec / 3600)) {
			currenthour++;
			CL_CheckResearchStatus();
			PR_ProductionRun();
			CL_CampaignRunMarket();
		}

		while (ccs.date.sec > 3600 * 24) {
			ccs.date.sec -= 3600 * 24;
			ccs.date.day++;
			/* every day */
			B_UpdateBaseData();
			HOS_HospitalRun();
		}

		/* check for campaign events */
		CL_CampaignRunAircraft(dt);
		UFO_CampaignRunUfos(dt);
		CL_CampaignCheckEvents();

		/* set time cvars */
		CL_DateConvert(&ccs.date, &day, &month);
		/* every first day of a month */
		if (day == 1 && gd.fund != qfalse && gd.numBases) {
			CL_HandleBudget();
			gd.fund = qfalse;
		} else if (day > 1)
			gd.fund = qtrue;

		Cvar_SetValue("mn_unreadmails", UP_GetUnreadMails());
		Cvar_Set("mn_mapdate", va("%i %s %i", ccs.date.day / DAYS_PER_YEAR, CL_DateGetMonthName(month), day));	/* CL_DateGetMonthName is already "gettexted" */
		Com_sprintf(messageBuffer, sizeof(messageBuffer), _("%02i:%02i"), ccs.date.sec / 3600, ((ccs.date.sec % 3600) / 60));
		Cvar_Set("mn_maptime", messageBuffer);
	}
}


/* =========================================================== */

typedef struct gameLapse_s {
	const char name[16];
	int scale;
} gameLapse_t;

#define NUM_TIMELAPSE 6

static const gameLapse_t lapse[NUM_TIMELAPSE] = {
	{"5 sec", 5},
	{"5 mins", 5 * 60},
	{"1 hour", 60 * 60},
	{"12 hour", 12 * 3600},
	{"1 day", 24 * 3600},
	{"5 days", 5 * 24 * 3600}
};

static int gameLapse;

/**
 * @brief Stop game time speed
 */
extern void CL_GameTimeStop (void)
{
	/* don't allow time scale in tactical mode */
	if (!CL_OnBattlescape()) {
		gameLapse = 0;
		Cvar_Set("mn_timelapse", _(lapse[gameLapse].name));
		gd.gameTimeScale = lapse[gameLapse].scale;
	}
}


/**
 * @brief Decrease game time speed
 *
 * Decrease game time speed - only works when there is already a base available
 */
extern void CL_GameTimeSlow (void)
{
	/* don't allow time scale in tactical mode */
	if (!CL_OnBattlescape()) {
		/*first we have to set up a home base */
		if (!gd.numBases)
			CL_GameTimeStop();
		else {
			if (gameLapse > 0)
				gameLapse--;
			Cvar_Set("mn_timelapse", _(lapse[gameLapse].name));
			gd.gameTimeScale = lapse[gameLapse].scale;
		}
	}
}

/**
 * @brief Increase game time speed
 *
 * Increase game time speed - only works when there is already a base available
 */
extern void CL_GameTimeFast (void)
{
	/* don't allow time scale in tactical mode */
	if (!CL_OnBattlescape()) {
		/*first we have to set up a home base */
		if (!gd.numBases)
			CL_GameTimeStop();
		else {
			if (gameLapse < NUM_TIMELAPSE - 1)
				gameLapse++;
			Cvar_Set("mn_timelapse", _(lapse[gameLapse].name));
			gd.gameTimeScale = lapse[gameLapse].scale;
		}
	}
}

#define MAX_CREDITS 10000000
/**
 * @brief Sets credits and update mn_credits cvar
 *
 * Checks whether credits are bigger than MAX_CREDITS
 */
extern void CL_UpdateCredits (int credits)
{
	/* credits */
	if (credits > MAX_CREDITS)
		credits = MAX_CREDITS;
	ccs.credits = credits;
	Cvar_Set("mn_credits", va(_("%i c"), ccs.credits));
}


#define MAX_STATS_BUFFER 2048
/**
 * @brief Shows the current stats from stats_t stats
 */
static void CL_StatsUpdate_f (void)
{
	char *pos;
	static char statsBuffer[MAX_STATS_BUFFER];
	int hired[MAX_EMPL];
	int i = 0, j, costs = 0, sum = 0;

	/* delete buffer */
	memset(statsBuffer, 0, sizeof(statsBuffer));
	memset(hired, 0, sizeof(hired));

	pos = statsBuffer;

	/* missions */
	menuText[TEXT_STATS_1] = pos;
	Com_sprintf(pos, MAX_STATS_BUFFER, _("Won:\t%i\nLost:\t%i\n\n"), stats.missionsWon, stats.missionsLost);

	/* bases */
	pos += (strlen(pos) + 1);
	menuText[TEXT_STATS_2] = pos;
	Com_sprintf(pos, (ptrdiff_t)(&statsBuffer[MAX_STATS_BUFFER] - pos), _("Built:\t%i\nActive:\t%i\nAttacked:\t%i\n"),
		stats.basesBuild, gd.numBases, stats.basesAttacked),

	/* nations */
	pos += (strlen(pos) + 1);
	menuText[TEXT_STATS_3] = pos;
	for (i = 0; i < gd.numNations; i++) {
		Q_strcat(pos, va(_("%s\t%s\n"), gd.nations[i].name, CL_GetNationHappinessString(&gd.nations[i])), (ptrdiff_t)(&statsBuffer[MAX_STATS_BUFFER] - pos));
	}

	/* costs */
	for (i = 0; i < gd.numEmployees[EMPL_SCIENTIST]; i++) {
		if (gd.employees[EMPL_SCIENTIST][i].hired) {
			costs += SALARY_SCIENTIST_BASE + gd.employees[EMPL_SCIENTIST][i].chr.rank * SALARY_SCIENTIST_RANKBONUS;
			hired[EMPL_SCIENTIST]++;
		}
	}
	for (i = 0; i < gd.numEmployees[EMPL_SOLDIER]; i++) {
		if (gd.employees[EMPL_SOLDIER][i].hired) {
			costs += SALARY_SOLDIER_BASE + gd.employees[EMPL_SOLDIER][i].chr.rank * SALARY_SOLDIER_RANKBONUS;
			hired[EMPL_SOLDIER]++;
		}
	}
	for (i = 0; i < gd.numEmployees[EMPL_WORKER]; i++) {
		if (gd.employees[EMPL_WORKER][i].hired) {
			costs += SALARY_WORKER_BASE + gd.employees[EMPL_WORKER][i].chr.rank * SALARY_WORKER_RANKBONUS;
			hired[EMPL_WORKER]++;
		}
	}
	for (i = 0; i < gd.numEmployees[EMPL_MEDIC]; i++) {
		if (gd.employees[EMPL_MEDIC][i].hired) {
			costs += SALARY_MEDIC_BASE + gd.employees[EMPL_MEDIC][i].chr.rank * SALARY_MEDIC_RANKBONUS;
			hired[EMPL_MEDIC]++;
		}
	}
	for (i = 0; i < gd.numEmployees[EMPL_ROBOT]; i++) {
		if (gd.employees[EMPL_ROBOT][i].hired) {
			costs += SALARY_ROBOT_BASE + gd.employees[EMPL_ROBOT][i].chr.rank * SALARY_ROBOT_RANKBONUS;
			hired[EMPL_ROBOT]++;
		}
	}

	/* employees - this is between the two costs parts to count the hired employees */
	pos += (strlen(pos) + 1);
	menuText[TEXT_STATS_4] = pos;
	for (i = 0; i < MAX_EMPL; i++) {
		Q_strcat(pos, va(_("%s\t%i\n"), E_GetEmployeeString(i), hired[i]), (ptrdiff_t)(&statsBuffer[MAX_STATS_BUFFER] - pos));
	}

	/* costs - second part */
	pos += (strlen(pos) + 1);
	menuText[TEXT_STATS_5] = pos;
	Q_strcat(pos, va(_("Employees:\t%i c\n"), costs), (ptrdiff_t)(&statsBuffer[MAX_STATS_BUFFER] - pos));
	sum += costs;

	costs = 0;
	for (i = 0; i < gd.numBases; i++) {
		for (j = 0; j < gd.bases[i].numAircraftInBase; j++) {
			costs += gd.bases[i].aircraft[j].price * SALARY_AIRCRAFT_FACTOR / SALARY_AIRCRAFT_DIVISOR;
		}
	}
	Q_strcat(pos, va(_("Aircrafts:\t%i c\n"), costs), (ptrdiff_t)(&statsBuffer[MAX_STATS_BUFFER] - pos));
	sum += costs;

	for (i = 0; i < gd.numBases; i++) {
		costs = SALARY_BASE_UPKEEP;	/* base cost */
		for (j = 0; j < gd.numBuildings[i]; j++) {
			costs += gd.buildings[i][j].varCosts;
		}
		Q_strcat(pos, va(_("Base (%s):\t%i c\n"), gd.bases[i].name, costs), (ptrdiff_t)(&statsBuffer[MAX_STATS_BUFFER] - pos));
		sum += costs;
	}

	costs = SALARY_ADMIN_INITIAL + gd.numEmployees[EMPL_SOLDIER] * SALARY_ADMIN_SOLDIER + gd.numEmployees[EMPL_WORKER] * SALARY_ADMIN_WORKER + gd.numEmployees[EMPL_SCIENTIST] * SALARY_ADMIN_SCIENTIST + gd.numEmployees[EMPL_MEDIC] * SALARY_ADMIN_MEDIC + gd.numEmployees[EMPL_ROBOT] * SALARY_ADMIN_ROBOT;
	Q_strcat(pos, va(_("Administrative costs:\t%i c\n"), costs), (ptrdiff_t)(&statsBuffer[MAX_STATS_BUFFER] - pos));
	sum += costs;

	if (ccs.credits < 0) {
		float interest = ccs.credits * SALARY_DEBT_INTEREST;

		costs = (int)ceil(interest);
		Q_strcat(pos, va(_("Debt:\t%i c\n"), costs), (ptrdiff_t)(&statsBuffer[MAX_STATS_BUFFER] - pos));
		sum += costs;
	}
	Q_strcat(pos, va(_("\n\t-------\nSum:\t%i c\n"), sum), (ptrdiff_t)(&statsBuffer[MAX_STATS_BUFFER] - pos));
}

/**
 * @brief Load callback for campaign data
 * @sa CP_Save
 * @sa SAV_GameSave
 */
extern qboolean CP_Load (sizebuf_t *sb, void *data)
{
	actMis_t *mis;
	stageState_t *state;
	setState_t *set;
	setState_t dummy;
	const char *name;
	int i, j, num;
	char val[32];
	base_t *base;
	int selectedMission;

	/* read campaign name */
	name = MSG_ReadString(sb);

	for (i = 0, curCampaign = campaigns; i < numCampaigns; i++, curCampaign++)
		if (!Q_strncmp(name, curCampaign->id, MAX_VAR))
			break;

	if (i == numCampaigns) {
		Com_Printf("SAV_GameLoad: Campaign \"%s\" doesn't exist.\n", name);
		curCampaign = NULL;
		return qfalse;
	}

	Com_sprintf(val, sizeof(val), "%i", curCampaign->difficulty);
	Cvar_ForceSet("difficulty", val);

	re.LoadTGA(va("pics/menu/%s_mask.tga", curCampaign->map), &maskPic, &maskWidth, &maskHeight);
	if (!maskPic)
		Sys_Error("Couldn't load map mask %s_mask.tga in pics/menu\n", curCampaign->map);

	re.LoadTGA(va("pics/menu/%s_nations.tga", curCampaign->map), &nationsPic, &nationsWidth, &nationsHeight);
	if (!nationsPic)
		Sys_Error("Couldn't load map mask %s_nations.tga in pics/menu\n", curCampaign->map);

	/* reset */
	MAP_ResetAction();

	memset(&ccs, 0, sizeof(ccs_t));

	ccs.angles[YAW] = GLOBE_ROTATE;

	/* read date */
	ccs.date.day = MSG_ReadLong(sb);
	ccs.date.sec = MSG_ReadLong(sb);

	/* read map view */
	ccs.center[0] = MSG_ReadFloat(sb);
	ccs.center[1] = MSG_ReadFloat(sb);
	ccs.zoom = MSG_ReadFloat(sb);

	/* read credits */
	CL_UpdateCredits(MSG_ReadLong(sb));

	/* read campaign data */
	name = MSG_ReadString(sb);
	while (*name) {
		state = CL_CampaignActivateStage(name, qfalse);
		if (!state) {
			Com_Printf("CP_Load: Unable to load campaign, unknown stage '%s'\n", name);
			curCampaign = NULL;
			Cbuf_AddText("mn_pop\n");
			return qfalse;
		}

		state->start.day = MSG_ReadLong(sb);
		state->start.sec = MSG_ReadLong(sb);
		num = MSG_ReadByte(sb);
		assert (num == state->def->num);
		for (i = 0; i < num; i++) {
			name = MSG_ReadString(sb);
			for (j = 0, set = &ccs.set[state->def->first]; j < state->def->num; j++, set++)
				if (!Q_strncmp(name, set->def->name, MAX_VAR))
					break;
			/* write on dummy set, if it's unknown */
			if (j >= state->def->num) {
				Com_Printf("CP_Load: Warning: Set '%s' not found\n", name);
				set = &dummy;
			}

			set->active = MSG_ReadByte(sb);
			set->num = MSG_ReadShort(sb);
			set->done = MSG_ReadShort(sb);
			set->start.day = MSG_ReadLong(sb);
			set->start.sec = MSG_ReadLong(sb);
			set->event.day = MSG_ReadLong(sb);
			set->event.sec = MSG_ReadLong(sb);
		}

		/* read next stage name */
		name = MSG_ReadString(sb);
	}

	/* reset team */
	Cvar_Set("team", curCampaign->team);

	/* store active missions */
	ccs.numMissions = MSG_ReadByte(sb);
	for (i = 0, mis = ccs.mission; i < ccs.numMissions; i++, mis++) {
		/* get mission definition */
		name = MSG_ReadString(sb);
		for (j = 0; j < numMissions; j++)
			if (!Q_strncmp(name, missions[j].name, MAX_VAR)) {
				mis->def = &missions[j];
				break;
			}
		if (j >= numMissions)
			Com_Printf("CP_Load: Warning: Mission '%s' not found\n", name);

		/* get mission type and location */
		mis->def->missionType = MSG_ReadByte(sb);
		name = MSG_ReadString(sb);
		Q_strncpyz(mis->def->location, name, MAX_VAR);

		/* get mission cause */
		name = MSG_ReadString(sb);

		for (j = 0; j < numStageSets; j++)
			if (!Q_strncmp(name, stageSets[j].name, MAX_VAR)) {
				mis->cause = &ccs.set[j];
				break;
			}
		if (j >= numStageSets)
			Com_Printf("CP_Load: Warning: Stage set '%s' not found\n", name);

		/* read position and time */
		mis->realPos[0] = MSG_ReadFloat(sb);
		mis->realPos[1] = MSG_ReadFloat(sb);
		mis->expire.day = MSG_ReadLong(sb);
		mis->expire.sec = MSG_ReadLong(sb);
		mis->def->onGeoscape = qtrue;

		/* ignore incomplete info */
		if (!mis->def || !mis->cause) {
			memset(mis, 0, sizeof(actMis_t));
			mis--;
			i--;
			ccs.numMissions--;
		}
		/* manually set mission data for a base-attack */
		if (mis->def->missionType == MIS_BASEATTACK) {
			/* Load IDX of base under attack */
			int baseidx = (int)MSG_ReadByte(sb);
			base = &gd.bases[baseidx];
			if (base->baseStatus == BASE_UNDER_ATTACK && !Q_strncmp(mis->def->location, base->name, MAX_VAR))
				Com_DPrintf("CP_Load: Base %i (%s) is under attack\n", j, base->name);
			else
				Com_Printf("CP_Load: Warning: base %i (%s) is supposedly under attack but base status or mission location (%s) doesn't match!\n", j, base->name, selMis->def->location);
			mis->def->data = (void*)base;
		}
	}

	/* stores the select mission on geoscape */
	selectedMission = MSG_ReadLong(sb);
	if (selectedMission >= 0)
		selMis = ccs.mission + selectedMission;
	else
		selMis = NULL;

	/* and now fix the mission pointers or let the aircraft return to base */
	for (i = 0; i < gd.numBases; i++) {
		base = &gd.bases[i];
		for (j = 0; j < base->numAircraftInBase; j++) {
			if (base->aircraft[j].status == AIR_MISSION) {
				if (selMis)
					base->aircraft[j].mission = selMis;
				else
					CL_AircraftReturnToBase(&(base->aircraft[j]));
			}
		}
	}

	return qtrue;
}

/**
 * @brief Save callback for campaign data
 * @sa CP_Load
 * @sa SAV_GameSave
 */
extern qboolean CP_Save (sizebuf_t *sb, void *data)
{
	stageState_t *state;
	actMis_t *mis;
	int i, j;
	base_t *base;

	/* store campaign name */
	MSG_WriteString(sb, curCampaign->id);

	/* store date */
	MSG_WriteLong(sb, ccs.date.day);
	MSG_WriteLong(sb, ccs.date.sec);

	/* store map view */
	MSG_WriteFloat(sb, ccs.center[0]);
	MSG_WriteFloat(sb, ccs.center[1]);
	MSG_WriteFloat(sb, ccs.zoom);

	/* store credits */
	MSG_WriteLong(sb, ccs.credits);

	/* store campaign data */
	for (i = 0, state = ccs.stage; i < numStages; i++, state++)
		if (state->active) {
			/* write head */
			setState_t *set;

			MSG_WriteString(sb, state->def->name);
			MSG_WriteLong(sb, state->start.day);
			MSG_WriteLong(sb, state->start.sec);
			MSG_WriteByte(sb, state->def->num);

			/* write all sets */
			for (j = 0, set = &ccs.set[state->def->first]; j < state->def->num; j++, set++) {
				MSG_WriteString(sb, set->def->name);
				MSG_WriteByte(sb, set->active);
				MSG_WriteShort(sb, set->num);
				MSG_WriteShort(sb, set->done);
				MSG_WriteLong(sb, set->start.day);
				MSG_WriteLong(sb, set->start.sec);
				MSG_WriteLong(sb, set->event.day);
				MSG_WriteLong(sb, set->event.sec);
			}
		}
	/* terminate list */
	MSG_WriteByte(sb, 0);

	/* store active missions */
	MSG_WriteByte(sb, ccs.numMissions);
	for (i = 0, mis = ccs.mission; i < ccs.numMissions; i++, mis++) {
		MSG_WriteString(sb, mis->def->name);
		MSG_WriteByte(sb, mis->def->missionType);
		MSG_WriteString(sb, mis->def->location);
		MSG_WriteString(sb, mis->cause->def->name);

		MSG_WriteFloat(sb, mis->realPos[0]);
		MSG_WriteFloat(sb, mis->realPos[1]);
		MSG_WriteLong(sb, mis->expire.day);
		MSG_WriteLong(sb, mis->expire.sec);
		/* save IDX of base under attack if required */
		if (mis->def->missionType == MIS_BASEATTACK) {
			base = (base_t*)mis->def->data;
			MSG_WriteByte(sb, base->idx);
		}
	}

	/* stores the select mission on geoscape */
	if (selMis)
		MSG_WriteLong(sb, selMis - ccs.mission);
	else
		MSG_WriteLong(sb, -1);

	return qtrue;
}


/**
 * @brief
 */
extern qboolean STATS_Save (sizebuf_t* sb, void* data)
{
	MSG_WriteShort(sb, stats.missionsWon);
	MSG_WriteShort(sb, stats.missionsLost);
	MSG_WriteShort(sb, stats.basesBuild);
	MSG_WriteShort(sb, stats.basesAttacked);
	MSG_WriteShort(sb, stats.interceptions);
	MSG_WriteShort(sb, stats.soldiersLost);
	MSG_WriteShort(sb, stats.soldiersNew);
	MSG_WriteShort(sb, stats.killedAliens);
	MSG_WriteShort(sb, stats.rescuedCivilians);
	MSG_WriteShort(sb, stats.researchedTechnologies);
	MSG_WriteShort(sb, stats.moneyInterceptions);
	MSG_WriteShort(sb, stats.moneyBases);
	MSG_WriteShort(sb, stats.moneyResearch);
	MSG_WriteShort(sb, stats.moneyWeapons);
	return qtrue;
}

/**
 * @brief
 */
extern qboolean STATS_Load (sizebuf_t* sb, void* data)
{
	stats.missionsWon = MSG_ReadShort(sb);
	stats.missionsLost = MSG_ReadShort(sb);
	stats.basesBuild = MSG_ReadShort(sb);
	stats.basesAttacked = MSG_ReadShort(sb);
	stats.interceptions = MSG_ReadShort(sb);
	stats.soldiersLost = MSG_ReadShort(sb);
	stats.soldiersNew = MSG_ReadShort(sb);
	stats.killedAliens = MSG_ReadShort(sb);
	stats.rescuedCivilians = MSG_ReadShort(sb);
	stats.researchedTechnologies = MSG_ReadShort(sb);
	stats.moneyInterceptions = MSG_ReadShort(sb);
	stats.moneyBases = MSG_ReadShort(sb);
	stats.moneyResearch = MSG_ReadShort(sb);
	stats.moneyWeapons = MSG_ReadShort(sb);
	return qtrue;
}

/**
 * @brief Nation saving callback
 */
extern qboolean NA_Save (sizebuf_t* sb, void* data)
{
	int i;
	for (i = 0; i < gd.numNations; i++) {
		MSG_WriteFloat(sb, gd.nations[i].happiness);
	}
	return qtrue;
}

/**
 * @brief Nation loading callback
 */
extern qboolean NA_Load (sizebuf_t* sb, void* data)
{
	int i;

	for (i = 0; i < gd.numNations; i++) {
		gd.nations[i].happiness = MSG_ReadFloat(sb);
	}
	return qtrue;
}

/**
 * @brief Set some needed cvars from mission definition
 * @param[in] mission mission definition pointer with the needed data to set the cvars to
 * @sa CL_StartMission_f
 * @sa CL_GameGo
 */
void CL_SetMissionCvars (mission_t* mission)
{
	/* start the map */
	Cvar_SetValue("ai_numaliens", (float) mission->aliens);
	Cvar_SetValue("ai_numcivilians", (float) mission->civilians);
	Cvar_Set("ai_alien", mission->alienTeam);
	Cvar_Set("ai_civilian", mission->civTeam);
	Cvar_Set("ai_equipment", mission->alienEquipment);
	Cvar_Set("music", mission->music);
	Com_DPrintf("CL_SetMissionCvars:\n");

	Com_DPrintf("..numAliens: %i\n..numCivilians: %i\n..alienTeam: '%s'\n..civTeam: '%s'\n..alienEquip: '%s'\n..music: '%s'\n",
		mission->aliens,
		mission->civilians,
		mission->alienTeam,
		mission->civTeam,
		mission->alienEquipment,
		mission->music);
}

/**
 * @brief Select the mission type and start the map from mission definition
 * @param[in] mission Mission definition to start the map from
 * @sa CL_GameGo
 * @sa CL_StartMission_f
 */
void CL_StartMissionMap (mission_t* mission)
{
	char expanded[MAX_QPATH];
	char timeChar;
	base_t *bAttack;

	/* prepare */
	MN_PopMenu(qtrue);
	Cvar_Set("mn_main", "singleplayermission");

	/* get appropriate map */
	if (CL_MapIsNight(mission->pos))
		timeChar = 'n';
	else
		timeChar = 'd';

	switch (mission->map[0]) {
	case '+':
		Com_sprintf(expanded, sizeof(expanded), "maps/%s%c.ump", mission->map + 1, timeChar);
		break;
	/* base attack maps starts with a dot */
	case '.':
		bAttack = (base_t*)mission->data;
		if (!bAttack) {
			/* assemble a random base and set the base status to BASE_UNDER_ATTACK */
			Cbuf_AddText("base_assemble_rand 1;");
			return;
		} else if (bAttack->baseStatus != BASE_UNDER_ATTACK) {
			Com_Printf("Base is not under attack\n");
			return;
		}
		/* check whether are there founded bases */
		if (B_GetCount() > 0)
			Cbuf_AddText(va("base_assemble %i 1;", bAttack->idx));
		return;
	default:
		Com_sprintf(expanded, sizeof(expanded), "maps/%s%c.bsp", mission->map, timeChar);
		break;
	}

	if (FS_LoadFile(expanded, NULL) != -1)
		Cbuf_AddText(va("map %s%c %s\n", mission->map, timeChar, mission->param));
	else
		Cbuf_AddText(va("map %s %s\n", mission->map, mission->param));
}

/**
 * @brief Starts a selected mission
 *
 * @note Checks whether a dropship is near the landing zone and whether it has a team on board
 * @sa CL_SetMissionCvars
 * @sa CL_StartMission_f
 */
static void CL_GameGo (void)
{
	mission_t *mis;
	aircraft_t *aircraft;
	int i, p;

	if (!curCampaign || gd.interceptAircraft < 0 || gd.interceptAircraft >= gd.numAircraft) {
		Com_DPrintf("curCampaign: %p, selMis: %p, interceptAircraft: %i\n", curCampaign, selMis, gd.interceptAircraft);
		return;
	}

	aircraft = CL_AircraftGetFromIdx(gd.interceptAircraft);
	/* update mission-status (active?) for the selected aircraft */
	/* Check what this was supposed to do ? */
	/* CL_CheckAircraft(aircraft); */

	if (!selMis)
		selMis = aircraft->mission;

	if (!selMis) {
		Com_DPrintf("No selMis\n");
		return;
	}

	mis = selMis->def;
	map_maxlevel_base = 0;
	baseCurrent = aircraft->homebase;
	assert(baseCurrent && mis && aircraft);

	/* set current aircraft of current base */
	baseCurrent->aircraftCurrent = aircraft->idxInBase;

	if (!ccs.singleplayer && B_GetNumOnTeam() == 0) {
		/* multiplayer; but we never reach this far! */
		MN_Popup(_("Note"), _("Assemble or load a team"));
		return;
	} else if (ccs.singleplayer && (!mis->active || !*(aircraft->teamSize))) {
		/* dropship not near landing zone */
		Com_DPrintf("Dropship not near landingzone: mis->active: %i\n", mis->active);
		return;
	}

	CL_SetMissionCvars(mis);

	/* TODO: Map assembling to get the current used dropship in the map is not fully implemented */
	/* but can be done via the map assembling part of the random map assembly */
	Cvar_Set("map_dropship", aircraft->id);

	/* retrieve the correct team */
	for (i = 0, p = 0; i < cl_numnames->integer; i++)
		if (CL_SoldierInAircraft(i, aircraft->idx)) {
			assert(p < MAX_ACTIVETEAM);
			baseCurrent->curTeam[p] = E_GetHiredCharacter(baseCurrent, EMPL_SOLDIER, i);
			p++;
		}

	/* manage inventory */
	ccs.eMission = baseCurrent->storage; /* copied, including arrays inside! */
	CL_CleanTempInventory();
	CL_ReloadAndRemoveCarried(&ccs.eMission);
	/* remove inventory of any old temporary LEs */
	LE_Cleanup();

	/* Zero out kill counters */
	ccs.civiliansKilled = 0;
	ccs.aliensKilled = 0;

	CL_StartMissionMap(mis);
}

/**
 * @brief For things like craft_ufo_scout that are no real items this function will
 * increase the collected counter by one
 * @note Mission trigger function
 * @sa CP_MissionTriggerFunctions
 * @sa CP_ExecuteMissionTrigger
 */
static void CP_AddItemAsCollected (void)
{
	int i, baseID;
	objDef_t *item = NULL;
	const char* id = NULL;

	/* baseid is appened in mission trigger function */
	if (Cmd_Argc() < 3) {
		Com_Printf("Usage: %s <item>\n", Cmd_Argv(0));
		return;
	}

	id = Cmd_Argv(1);
	baseID = atoi(Cmd_Argv(2));

	/* i = item index */
	for (i = 0; i < csi.numODs; i++) {
		item = &csi.ods[i];
		if (!Q_strncmp(id, item->id, MAX_VAR)) {
			gd.bases[baseID].storage.num[i]++;
			Com_DPrintf("add item: '%s'\n", item->id);
			assert(item->tech);
			RS_MarkCollected((technology_t*)(item->tech));
		}
	}
}

/** @brief mission trigger functions */
static const cmdList_t cp_commands[] = {
	{"cp_add_item", CP_AddItemAsCollected, "Add an item as collected"},

	{NULL, NULL, NULL}
};

/**
 * @brief Add/Remove temporary mission trigger functions
 * @note These function can be defined via onwin/onlose parameters in missions.ufo
 * @param[in] add If true, add the trigger functions, otherwise delete them
 */
static void CP_MissionTriggerFunctions (qboolean add)
{
	const cmdList_t *commands;

	for (commands = cp_commands; commands->name; commands++)
		if (add)
			Cmd_AddCommand(commands->name, commands->function, commands->description);
		else
			Cmd_RemoveCommand(commands->name);
}

/**
 * @brief Executes console commands after a mission
 *
 * @param m Pointer to mission_t
 * @param won Int value that is one when you've won the game, and zero when the game was lost
 * Can execute console commands (triggers) on win and lose
 * This can be used for story dependent missions
 */
static void CP_ExecuteMissionTrigger (mission_t * m, int won, base_t* base)
{
	/* we add them only here - and remove them afterwards to prevent cheating */
	CP_MissionTriggerFunctions(qtrue);
	Com_DPrintf("Execute mission triggers\n");
	if (won && *m->onwin) {
		assert(base);
		Com_DPrintf("...won - executing '%s'\n", m->onwin);
		Cbuf_ExecuteText(EXEC_NOW, va("%s %i", m->onwin, base->idx));
	} else if (!won && *m->onlose) {
		assert(base);
		Com_DPrintf("...lost - executing '%s'\n", m->onlose);
		Cbuf_ExecuteText(EXEC_NOW, va("%s %i", m->onlose, base->idx));
	}
	CP_MissionTriggerFunctions(qfalse);
}

/**
 * @brief Checks whether you have to play this mission
 *
 * You can mark a mission as story related.
 * If a mission is story related the cvar game_autogo is set to 0
 * If this cvar is 1 - the mission dialog will have a auto mission button
 * @sa CL_GameAutoGo_f
 */
static void CL_GameAutoCheck_f (void)
{
	if (!curCampaign || !selMis || gd.interceptAircraft < 0 || gd.interceptAircraft >= gd.numAircraft) {
		Com_DPrintf("No update after automission\n");
		return;
	}

	switch (selMis->def->storyRelated) {
	case qtrue:
		Com_DPrintf("story related - auto mission is disabled\n");
		Cvar_SetValue("game_autogo", 0.0f);
		break;
	default:
		Com_DPrintf("auto mission is enabled\n");
		Cvar_SetValue("game_autogo", 1.0f);
		break;
	}
}

/**
 * @brief
 *
 * @sa CL_GameAutoCheck_f
 * @sa CL_GameGo
 */
static void CL_GameAutoGo_f (void)
{
	mission_t *mis;
	int won;
	aircraft_t *aircraft;

	if (!curCampaign || !selMis || gd.interceptAircraft < 0 || gd.interceptAircraft >= gd.numAircraft) {
		Com_DPrintf("No update after automission\n");
		return;
	}

	aircraft = CL_AircraftGetFromIdx(gd.interceptAircraft);

	/* start the map */
	mis = selMis->def;
	baseCurrent = aircraft->homebase;
	assert(baseCurrent && mis && aircraft);
	baseCurrent->aircraftCurrent = aircraft->idxInBase; /* Might not be needed, but it's used later on in CL_AircraftReturnToBase_f */

	if (!mis->active) {
		MN_AddNewMessage(_("Notice"), _("Your dropship is not near the landing zone"), qfalse, MSG_STANDARD, NULL);
		return;
	} else if (mis->storyRelated) {
		Com_DPrintf("You have to play this mission, because it's story related\n");
		return;
	}

	MN_PopMenu(qfalse);

	/* FIXME: This needs work */
	won = mis->aliens * difficulty->integer > *(aircraft->teamSize) ? 0 : 1;

	Com_DPrintf("Aliens: %i (count as %i) - Soldiers: %i\n", mis->aliens, mis->aliens * difficulty->integer, *(aircraft->teamSize));

	/* update nation opinions */
	if (won) {
		CL_HandleNationData(0, mis->civilians, 0, 0, mis->aliens, selMis);
	} else {
		CL_HandleNationData(1, 0, mis->civilians, mis->aliens, 0, selMis);
	}

	/* campaign effects */
	selMis->cause->done++;
	if ((selMis->cause->def->quota && selMis->cause->done >= selMis->cause->def->quota)
		 || (selMis->cause->def->number
			 && selMis->cause->num >= selMis->cause->def->number)) {
		selMis->cause->active = qfalse;
		CL_CampaignExecute(selMis->cause);
	}

	/* onwin and onlose triggers */
	CP_ExecuteMissionTrigger(selMis->def, won, aircraft->homebase);

	CL_CampaignRemoveMission(selMis);

	if (won)
		MN_AddNewMessage(_("Notice"), _("You've won the battle"), qfalse, MSG_STANDARD, NULL);
	else
		MN_AddNewMessage(_("Notice"), _("You've lost the battle"), qfalse, MSG_STANDARD, NULL);

	MAP_ResetAction();

	CL_AircraftReturnToBase_f();
}

/**
 * @brief Let the aliens win the match
 */
static void CL_GameAbort_f (void)
{
	/* aborting means letting the aliens win */
	Cbuf_AddText(va("sv win %i\n", TEAM_ALIEN));
}

/**
 * Collecting items functions.
 */

static equipDef_t eTempEq;	/**< Used to count ammo in magazines. */
static int eTempCredits;	/**< Used to count temporary credits for item selling. */

/**
 * @brief Count and collect ammo from gun magazine.
 * @param[in] *magazine Pointer to invList_t being magazine.
 * @param[in] *aircraft Pointer to aircraft used in this mission.
 * @sa CL_CollectingItems
 */
static void CL_CollectingAmmo (invList_t *magazine, aircraft_t *aircraft)
{
	int i;
	itemsTmp_t *cargo = NULL;

	if (!aircraft) {
#ifdef DEBUG
		/* should never happen */
		Com_Printf("CL_CollectingAmmo()... no aircraft!\n");
#endif
		return;
	} else {
		cargo = aircraft->itemcargo;
	}

	/* Let's add remaining ammo to market. */
	eTempEq.num_loose[magazine->item.m] += magazine->item.a;
	if (eTempEq.num_loose[magazine->item.m] >= csi.ods[magazine->item.t].ammo) {
		/* There are more or equal ammo on the market than magazine needs - collect magazine. */
		eTempEq.num_loose[magazine->item.m] -= csi.ods[magazine->item.t].ammo;
		for (i = 0; i < aircraft->itemtypes; i++) {
			if (cargo[i].idx == magazine->item.m) {
				cargo[i].amount++;
				Com_DPrintf("Collecting item in CL_CollectingAmmo(): %i name: %s amount: %i\n", cargo[i].idx, csi.ods[magazine->item.m].name, cargo[i].amount);
				break;
			}
		}
		if (i == aircraft->itemtypes) {
			cargo[i].idx = magazine->item.m;
			cargo[i].amount++;
			Com_DPrintf("Adding item in CL_CollectingAmmo(): %i, name: %s\n", cargo[i].idx, csi.ods[magazine->item.m].name);
			aircraft->itemtypes++;
		}
	}
}

/**
 * @brief Process items carried by soldiers.
 * @param[in] *soldier Pointer to le_t being a soldier from our team.
 * @sa CL_CollectingItems
 */
static void CL_CarriedItems (le_t *soldier)
{
	int container;
	invList_t *item = NULL;
	technology_t *tech = NULL;

	for (container = 0; container < csi.numIDs; container++) {
		if (csi.ids[container].temp) /* Items collected as ET_ITEM in CL_CollectingItems(). */
			break;
		for (item = soldier->i.c[container]; item; item = item->next) {
			/* Fake item. */
			assert(item->item.t != NONE);
			/* Twohanded weapons and container is left hand container. */
			/* FIXME: */
			/* assert(container == csi.idLeft && csi.ods[item->item.t].holdtwohanded); */

			ccs.eMission.num[item->item.t]++;
			tech = csi.ods[item->item.t].tech;
			if (!tech)
				Sys_Error("CL_CarriedItems: No tech for %s / %s\n", csi.ods[item->item.t].id, csi.ods[item->item.t].name);
			RS_MarkCollected(tech);

			if (!csi.ods[item->item.t].reload || item->item.a == 0)
				continue;
			ccs.eMission.num_loose[item->item.m] += item->item.a;
			if (ccs.eMission.num_loose[item->item.m] >= csi.ods[item->item.t].ammo) {
				ccs.eMission.num_loose[item->item.m] -= csi.ods[item->item.t].ammo;
				ccs.eMission.num[item->item.m]++;
			}
			/* The guys keep their weapons (half-)loaded. Auto-reload
			   will happen at equip screen or at the start of next mission,
			   but fully loaded weapons will not be reloaded even then. */
		}
	}
}

/**
 * @brief Collect items from the battlefield.
 * @note The way we are collecting items:
 * @note 1. Grab everything from the floor and add it to the aircraft cargo here.
 * @note 2. When collecting gun, collect it's remaining ammo as well in CL_CollectingAmmo
 * @note 3. Sell everything or add to base storage in CL_SellingAmmo, when dropship lands in base.
 * @note 4. Items carried by soldiers are processed in CL_CarriedItems, nothing is being sold.
 * @sa CL_ParseResults
 * @sa CL_CollectingAmmo
 * @sa CL_SellorAddAmmo
 * @sa CL_CarriedItems
 */
extern void CL_CollectingItems (int won)
{
	int i, j;
	le_t *le;
	invList_t *item;
	itemsTmp_t *cargo;
	aircraft_t *aircraft;

	if (!baseCurrent) {
#ifdef DEBUG
		/* should never happen */
		Com_Printf("CL_CollectingItems()... no base selected!\n");
#endif
		return;
	}
	if ((baseCurrent->aircraftCurrent >= 0) && (baseCurrent->aircraftCurrent < baseCurrent->numAircraftInBase)) {
		aircraft = &baseCurrent->aircraft[baseCurrent->aircraftCurrent];
	} else {
#ifdef DEBUG
		/* should never happen */
		Com_Printf("CL_CollectingItems()... no aircraft selected!\n");
#endif
		return;
	}

	/* Make sure itemcargo is empty. */
	memset(aircraft->itemcargo, 0, sizeof(aircraft->itemcargo));

	/* Make sure eTempEq is empty as well. */
	memset(&eTempEq, 0, sizeof(eTempEq));

	cargo = aircraft->itemcargo;
	aircraft->itemtypes = 0;
	eTempCredits = ccs.credits;

	for (i = 0, le = LEs; i < numLEs; i++, le++) {
		/* Winner collects everything on the floor, and everything carried
		   by surviving actors. Loser only gets what their living team
		   members carry. */
		if (!le->inuse)
			continue;
		switch (le->type) {
		case ET_ITEM:
			if (won) {
				for (item = FLOOR(le); item; item = item->next) {
					if ((item->item.a <= 0) /* No ammo left, and... */
					 && csi.ods[item->item.t].oneshot /* ... oneshot weapon, and... */
					 && csi.ods[item->item.t].deplete) { /* ... useless after ammo is gone. */
						Com_DPrintf("CL_CollectItems: depletable item not collected: %s\n", csi.ods[item->item.t].name);
						continue;
					}
					for (j = 0; j < aircraft->itemtypes; j++) {
						if (cargo[j].idx == item->item.t) {
							cargo[j].amount++;
							Com_DPrintf("Collecting item: %i name: %s amount: %i\n", cargo[j].idx, csi.ods[item->item.t].name, cargo[j].amount);
							/* If this is not reloadable item, or no ammo left, break... */
							if (!csi.ods[item->item.t].reload || item->item.a == 0)
								break;
							/* ...otherwise collect ammo as well. */
							CL_CollectingAmmo(item, aircraft);
							break;
						}
					}
					if (j == aircraft->itemtypes) {
						cargo[j].idx = item->item.t;
						cargo[j].amount++;
						Com_DPrintf("Adding item: %i name: %s\n", cargo[j].idx, csi.ods[item->item.t].name);
						aircraft->itemtypes++;
						/* If this is not reloadable item, or no ammo left, break... */
						if (!csi.ods[item->item.t].reload || item->item.a == 0)
							continue;
						/* ...otherwise collect ammo as well. */
						CL_CollectingAmmo(item, aircraft);
					}
				}
			}
			break;
		case ET_ACTOR:
		case ET_UGV:
			/* First of all collect armours of dead or stunned actors (if won). */
			if (won) {
				/* (le->state & STATE_DEAD) includes STATE_STUN */
				if (le->state & STATE_DEAD) {
					if (le->i.c[csi.idArmor]) {
						item = le->i.c[csi.idArmor];
						for (j = 0; j < aircraft->itemtypes; j++) {
							if (cargo[j].idx == item->item.t) {
								cargo[j].amount++;
								Com_DPrintf("Collecting armour: %i name: %s amount: %i\n", cargo[j].idx, csi.ods[item->item.t].name, cargo[j].amount);
								break;
							}
						}
						if (j == aircraft->itemtypes) {
							cargo[j].idx = item->item.t;
							cargo[j].amount++;
							Com_DPrintf("Adding item: %i name: %s\n", cargo[j].idx, csi.ods[item->item.t].name);
							aircraft->itemtypes++;
						}
					}
					break;
				}
			}
			/* Now, if this is dead or stunned actor, additional check. */
			if (le->state & STATE_DEAD || le->team != cls.team) {
				/* The items are already dropped to floor and are available
				   as ET_ITEM; or the actor is not ours. */
				break;
			}
			/* Finally, the living actor from our team. */
			CL_CarriedItems(le);
			break;
		default:
			break;
		}
	}
#ifdef DEBUG
	/* Print all of collected items. */
	for (i = 0; i < aircraft->itemtypes; i++) {
		if (cargo[i].amount > 0)
			Com_DPrintf("Collected items: idx: %i name: %s amount: %i\n", cargo[i].idx, csi.ods[cargo[i].idx].name, cargo[i].amount);
	}
#endif
}

/**
 * @brief Sell items to the market or add them to base storage.
 * @sa CL_DropshipReturned
 */
void CL_SellOrAddItems (void)
{
	int i, numitems = 0, gained = 0;
	char str[128];
	itemsTmp_t *cargo = NULL;
	aircraft_t *aircraft = NULL;
	technology_t *tech = NULL;

	if (!baseCurrent) {
#ifdef DEBUG
		/* should never happen */
		Com_Printf("CL_SellingItems()... no base selected!\n");
#endif
		return;
	}
	if ((baseCurrent->aircraftCurrent >= 0) && (baseCurrent->aircraftCurrent < baseCurrent->numAircraftInBase)) {
		aircraft = &baseCurrent->aircraft[baseCurrent->aircraftCurrent];
	} else {
#ifdef DEBUG
		/* should never happen */
		Com_Printf("CL_SellingItems()... no aircraft selected!\n");
#endif
		return;
	}

	eTempCredits = ccs.credits;
	cargo = aircraft->itemcargo;

	for (i = 0; i < aircraft->itemtypes; i++) {
		tech = csi.ods[cargo[i].idx].tech;
		if (!tech)
			Sys_Error("CL_SellOrAddItems: No tech for %s / %s\n", csi.ods[cargo[i].idx].id, csi.ods[cargo[i].idx].name);
		/* If the related technology is NOT researched, don't sell items. */
		if (!RS_IsResearched_ptr(tech)) {
			baseCurrent->storage.num[cargo[i].idx] += cargo[i].amount;
			if (cargo[i].amount > 0)
				RS_MarkCollected(tech);
			continue;
		}
		/* If the related technology is researched, check the autosell option. */
		if (RS_IsResearched_ptr(tech)) {
			if (gd.autosell[cargo[i].idx]) { /* Sell items if autosell is enabled. */
				ccs.eMarket.num[cargo[i].idx] += cargo[i].amount;
				eTempCredits += (csi.ods[cargo[i].idx].price * cargo[i].amount);
				numitems += cargo[i].amount;
			} else { /* Store items if autosell is disabled. */
				baseCurrent->storage.num[cargo[i].idx] += cargo[i].amount;
			}
			continue;
		}
	}

	gained = eTempCredits - ccs.credits;
	if (gained > 0) {
		Com_sprintf(str, sizeof(str), _("By selling %s %s"),
		va(ngettext("%i collected item", "%i collected items", numitems), numitems),
		va(_("you gathered %i credits."), gained));
		MN_AddNewMessage(_("Notice"), str, qfalse, MSG_STANDARD, NULL);
	}
	CL_UpdateCredits(ccs.credits + gained);
}


/**
 * @brief Update employeer stats after mission.
 * @param[in] won Determines whether we won the mission or not.
 * @note Soldier promotion is being done here.
 * @note Soldier skill upgrade is being done here.
 * @sa CL_GameResults_f
 *
 * FIXME: See TODO and FIXME included
 */
static void CL_UpdateCharacterStats (int won)
{
	character_t *chr = NULL;
	rank_t *rank = NULL;
	aircraft_t *aircraft;
	int i, j, idx = 0;

	Com_DPrintf("CL_UpdateCharacterStats: numTeamList: %i\n", cl.numTeamList);

	/* aircraft = &baseCurrent->aircraft[gd.interceptAircraft]; remove this TODO: check if baseCurrent has the currect aircraftCurrent.  */
	aircraft = CL_AircraftGetFromIdx(gd.interceptAircraft);

	Com_DPrintf("CL_UpdateCharacterStats: baseCurrent: %s\n", baseCurrent->name);

	for (i = 0; i < gd.numEmployees[EMPL_SOLDIER]; i++)
		if (CL_SoldierInAircraft(i, gd.interceptAircraft) ) {
			Com_DPrintf("CL_UpdateCharacterStats: searching for soldier: %i\n", i);
			chr = E_GetHiredCharacter(baseCurrent, EMPL_SOLDIER, -(idx+1));
			assert(chr);
			/* count every hired soldier in aircraft */
			idx++;
			chr->assigned_missions++;

			CL_UpdateCharacterSkills(chr);

			/* TODO: use chrScore_t to determine negative influence on soldier here,
			   like killing too many civilians and teammates can lead to unhire and disband
			   such soldier, or maybe rank degradation. */

			/* Check if the soldier meets the requirements for a higher rank
			   and do a promotion. */
			/* TODO: use param[in] in some way. */
			if (gd.numRanks >= 2) {
				for (j = gd.numRanks - 1; j > chr->rank; j--) {
					rank = &gd.ranks[j];
					/* FIXME: (Zenerka 20080301) extend ranks and change calculations here. */
					if (rank->type == EMPL_SOLDIER && (chr->skills[ABILITY_MIND] >= rank->mind)
						&& (chr->kills[KILLED_ALIENS] >= rank->killed_enemies)
						&& ((chr->kills[KILLED_CIVILIANS] + chr->kills[KILLED_TEAM]) <= rank->killed_others)) {
						chr->rank = j;
						if (chr->HP > 0)
							Com_sprintf(messageBuffer, sizeof(messageBuffer), _("%s has been promoted to %s.\n"), chr->name, rank->name);
						else
							Com_sprintf(messageBuffer, sizeof(messageBuffer), _("%s has been awarded the posthumous rank of %s\\for inspirational gallantry in the face of overwhelming odds.\n"), chr->name, rank->name);
						MN_AddNewMessage(_("Soldier promoted"), messageBuffer, qfalse, MSG_PROMOTION, NULL);
						break;
					}
				}
			}
		/* also count the employees that are only hired but not in the aircraft */
		} else if (E_GetHiredEmployee(baseCurrent, EMPL_SOLDIER, i))
			idx++;
	Com_DPrintf("CL_UpdateCharacterStats: Done\n");
}

#ifdef DEBUG
/**
 * @brief Debug function to set the credits to max
 */
static void CL_DebugFullCredits_f (void)
{
	CL_UpdateCredits(MAX_CREDITS);
}

/**
 * @brief Debug function to increase the kills and test the ranks
 */
static void CL_DebugChangeCharacterStats_f (void)
{
	int i, j;
	character_t *chr;

	for (i = 0; i < gd.numEmployees[EMPL_SOLDIER]; i++) {
		chr = E_GetHiredCharacter(baseCurrent, EMPL_SOLDIER, i);
		if (chr) {
			for (j = 0; j < KILLED_NUM_TYPES; j++)
				chr->kills[j]++;
		}
	}
	CL_UpdateCharacterStats(1);
}
#endif

/**
 * @brief
 * @sa CL_ParseResults
 * @sa CL_ParseCharacterData
 * @sa CL_GameAbort_f
 */
static void CL_GameResults_f (void)
{
	int won;
	int civilians_killed;
	int aliens_killed;
	int i;
	employee_t* employee;
	int numberofsoldiers = 0; /* DEBUG */
	base_t *attackedbase = NULL;
	character_t *chr = NULL;

	Com_DPrintf("CL_GameResults_f\n");

	/* multiplayer? */
	if (!curCampaign || !selMis || !baseCurrent)
		return;

	/* check for replay */
	if (Cvar_VariableInteger("game_tryagain")) {
		/* don't collect things and update stats --- we retry the mission */
		CL_GameGo();
		return;
	}
	/* check for win */
	if (Cmd_Argc() < 2) {
		Com_Printf("Usage: game_results <won>\n");
		return;
	}
	won = atoi(Cmd_Argv(1));

	assert(gd.interceptAircraft >= 0);

	baseCurrent = CL_AircraftGetFromIdx(gd.interceptAircraft)->homebase;
	baseCurrent->aircraftCurrent =  CL_AircraftGetFromIdx(gd.interceptAircraft)->idxInBase;

	/* add the looted goods to base storage and market */
	baseCurrent->storage = ccs.eMission; /* copied, including the arrays! */

	civilians_killed = ccs.civiliansKilled;
	aliens_killed = ccs.aliensKilled;
	/* fprintf(stderr, "Won: %d   Civilians: %d/%d   Aliens: %d/%d\n", won, selMis->def->civilians - civilians_killed, civilians_killed, selMis->def->aliens - aliens_killed, aliens_killed); */
	CL_HandleNationData(!won, selMis->def->civilians - civilians_killed, civilians_killed, selMis->def->aliens - aliens_killed, aliens_killed, selMis);

	/* update the character stats */
	CL_ParseCharacterData(NULL, qtrue);

	/* update stats */
	CL_UpdateCharacterStats(won);

	/* Backward loop because gd.numEmployees[EMPL_SOLDIER] is decremented by E_DeleteEmployee */
	for (i = gd.numEmployees[EMPL_SOLDIER]-1; i >= 0 ; i-- ) {
		/* if employee is marked as dead */
		if (CL_SoldierInAircraft(i, gd.interceptAircraft))	/* DEBUG? */
			numberofsoldiers++;

		Com_DPrintf("CL_GameResults_f - try to get player %i \n", i);
		employee = E_GetHiredEmployee(baseCurrent, EMPL_SOLDIER, i);

		if (employee != NULL) {
			chr = E_GetHiredCharacter(baseCurrent, EMPL_SOLDIER, i);
			assert(chr);
			Com_DPrintf("CL_GameResults_f - idx %d hp %d\n",chr->ucn, chr->HP);
			if (chr->HP <= 0) { /* TODO: <= -50, etc. */
				/* Delete the employee. */
				/* sideeffect: gd.numEmployees[EMPL_SOLDIER] and teamNum[] are decremented by one here. */
				Com_DPrintf("CL_GameResults_f: Delete this dead employee: %i (%s)\n", i, gd.employees[EMPL_SOLDIER][i].chr.name);
				E_DeleteEmployee(employee, EMPL_SOLDIER);
			} /* if dead */
		} /* if employee != NULL */
	} /* for */
	Com_DPrintf("CL_GameResults_f - num %i\n", numberofsoldiers); /* DEBUG */

	Com_DPrintf("CL_GameResults_f - done removing dead players\n");

	/* onwin and onlose triggers */
	CP_ExecuteMissionTrigger(selMis->def, won, baseCurrent);

	/* Check for alien containment in aircraft homebase. */
	if (baseCurrent->aircraft[baseCurrent->aircraftCurrent].alientypes && !baseCurrent->hasAlienCont) {
		/* We have captured/killed aliens, but the homebase of this aircraft does not have alien containment. Popup aircraft transer dialog. */
		TR_TransferAircraftMenu(&(baseCurrent->aircraft[baseCurrent->aircraftCurrent]));
	} else {
		/* The aircraft can be savely sent to its homebase without losing aliens */

		/* TODO: Is this really needed? At the beginning of CL_GameResults_f we already have this status (if I read this correctly). */
		baseCurrent->aircraft[baseCurrent->aircraftCurrent].homebase = baseCurrent;
		baseCurrent->aircraft[baseCurrent->aircraftCurrent].idxBase = baseCurrent->idx;

		CL_AircraftReturnToBase_f();
	}

	/* campaign effects */
	selMis->cause->done++;
	if ((selMis->cause->def->quota
		  && selMis->cause->done >= selMis->cause->def->quota)
		 || (selMis->cause->def->number
			 && selMis->cause->num >= selMis->cause->def->number)) {
		selMis->cause->active = qfalse;
		CL_CampaignExecute(selMis->cause);
	}

	/* handle base attack mission */
	if (selMis->def->missionType == MIS_BASEATTACK) {
		attackedbase = (base_t*)selMis->def->data;
		if (won) {
			Com_sprintf(messageBuffer, MAX_MESSAGE_TEXT, _("Defense of base: %s successful!"), attackedbase->name);
			MN_AddNewMessage(_("Notice"), messageBuffer, qfalse, MSG_STANDARD, NULL);
		} else
			CL_BaseRansacked(attackedbase);
	}

	/* remove mission from list */
	CL_CampaignRemoveMission(selMis);
}

/* =========================================================== */

/** @brief valid mission descriptors */
static const value_t mission_vals[] = {
	{"location", V_TRANSLATION_STRING, offsetof(mission_t, location)}
	,
	{"type", V_TRANSLATION_STRING, offsetof(mission_t, type)}
	,
	{"text", V_TRANSLATION_STRING, 0}	/* max length is 128 */
	,
	{"nation", V_STRING, offsetof(mission_t, nation)}
	,
	{"map", V_STRING, offsetof(mission_t, map)}
	,
	{"param", V_STRING, offsetof(mission_t, param)}
	,
	{"music", V_STRING, offsetof(mission_t, music)}
	,
	{"pos", V_POS, offsetof(mission_t, pos)}
	,
	{"mask", V_RGBA, offsetof(mission_t, mask)}	/* color values from map mask this mission needs */
	,
	{"aliens", V_INT, offsetof(mission_t, aliens)}
	,
	{"maxugv", V_INT, offsetof(mission_t, ugv)}
	,
	{"commands", V_STRING, offsetof(mission_t, cmds)}	/* commands that are excuted when this mission gets active */
	,
	{"onwin", V_STRING, offsetof(mission_t, onwin)}
	,
	{"onlose", V_STRING, offsetof(mission_t, onlose)}
	,
	{"alienteam", V_STRING, offsetof(mission_t, alienTeam)}
	,
	{"alienequip", V_STRING, offsetof(mission_t, alienEquipment)}
	,
	{"civilians", V_INT, offsetof(mission_t, civilians)}
	,
	{"civteam", V_STRING, offsetof(mission_t, civTeam)}
	,
	{"storyrelated", V_BOOL, offsetof(mission_t, storyRelated)}
	,
	{"loadingscreen", V_STRING, offsetof(mission_t, loadingscreen)}
	,
	{NULL, 0, 0}
	,
};

#define		MAX_MISSIONTEXTS	MAX_MISSIONS*128
static char missionTexts[MAX_MISSIONTEXTS];
static char *mtp = missionTexts;

/**
 * @brief Adds a mission to current stageSet
 * @note the returned mission_t pointer has to be filled - this function only fills the name
 * @param[in] name valid mission name
 * @return ms is the mission_t pointer of the mission to add
 */
extern mission_t *CL_AddMission (const char *name)
{
	int i;
	mission_t *ms;

	/* just to let us know: search for missions with same name */
	for (i = 0; i < numMissions; i++)
		if (!Q_strncmp(name, missions[i].name, MAX_VAR))
			break;
	if (i < numMissions)
		Com_DPrintf("CL_AddMission: mission def \"%s\" with same name found\n", name);

	if (numMissions >= MAX_MISSIONS) {
		Com_Printf("CL_AddMission: Max missions reached\n");
		return NULL;
	}
	/* initialize the mission */
	ms = &missions[numMissions++];
	memset(ms, 0, sizeof(mission_t));
	Q_strncpyz(ms->name, name, sizeof(ms->name));
	Com_DPrintf("CL_AddMission: mission name: '%s'\n", name);

	return ms;
}

/**
 * @brief
 */
extern void CL_ParseMission (const char *name, char **text)
{
	const char *errhead = "CL_ParseMission: unexpected end of file (mission ";
	mission_t *ms;
	const value_t *vp;
	char *token;
	int i;

	/* search for missions with same name */
	for (i = 0; i < numMissions; i++)
		if (!Q_strncmp(name, missions[i].name, MAX_VAR))
			break;

	if (i < numMissions) {
		Com_Printf("CL_ParseMission: mission def \"%s\" with same name found, second ignored\n", name);
		return;
	}

	if (numMissions >= MAX_MISSIONS) {
		Com_Printf("CL_ParseMission: Max missions reached\n");
		return;
	}

	/* initialize the mission */
	ms = &missions[numMissions++];
	memset(ms, 0, sizeof(mission_t));

	Q_strncpyz(ms->name, name, sizeof(ms->name));

	/* get it's body */
	token = COM_Parse(text);

	if (!*text || *token != '{') {
		Com_Printf("CL_ParseMission: mission def \"%s\" without body ignored\n", name);
		numMissions--;
		return;
	}

	do {
		token = COM_EParse(text, errhead, name);
		if (!*text)
			break;
		if (*token == '}')
			break;

		for (vp = mission_vals; vp->string; vp++)
			if (!Q_strcmp(token, vp->string)) {
				/* found a definition */
				token = COM_EParse(text, errhead, name);
				if (!*text)
					return;

				if (vp->ofs)
					Com_ParseValue(ms, token, vp->type, vp->ofs);
				else {
					if (*token == '_')
						token++;
					Q_strncpyz(mtp, _(token), 128);
					ms->text = mtp;
					do {
						mtp = strchr(mtp, '\\');
						if (mtp)
							*mtp = '\n';
					} while (mtp);
					mtp = ms->text + strlen(ms->text) + 1;
				}
				break;
			}

		if (!vp->string)
			Com_Printf("CL_ParseMission: unknown token \"%s\" ignored (mission %s)\n", token, name);

	} while (*text);
#ifdef DEBUG
	if (abs(ms->pos[0]) > 180.0f)
		Com_Printf("Longitude for mission '%s' is bigger than 180 EW (%.0f)\n", ms->name, ms->pos[0]);
	if (abs(ms->pos[1]) > 90.0f)
		Com_Printf("Latitude for mission '%s' is bigger than 90 NS (%.0f)\n", ms->name, ms->pos[1]);
#endif
	if (!*ms->loadingscreen)
		Q_strncpyz(ms->loadingscreen, "default.jpg", MAX_VAR);
}


/* =========================================================== */

/**
 * @brief This function parses a list of items that should be set to researched = true after campaign start
 * @TODO: Implement the use of this function
 */
extern void CL_ParseResearchedCampaignItems (const char *name, char **text)
{
	const char *errhead = "CL_ParseResearchedCampaignItems: unexpected end of file (equipment ";
	char *token;
	int i;
	campaign_t* campaign = NULL;

	campaign = CL_GetCampaign(Cvar_VariableString("campaign"));
	if (!campaign) {
		Com_Printf("CL_ParseResearchedCampaignItems: failed\n");
		return;
	}

	/* get it's body */
	token = COM_Parse(text);

	if (!*text || *token != '{') {
		Com_Printf("CL_ParseResearchedCampaignItems: equipment def \"%s\" without body ignored (%s)\n", name, token);
		return;
	}

	Com_DPrintf("..campaign research list '%s'\n", name);
	do {
		token = COM_EParse(text, errhead, name);
		if (!*text || *token == '}')
			return;

		for (i = 0; i < gd.numTechnologies; i++)
			if (!Q_strncmp(token, gd.technologies[i].id, MAX_VAR)) {
				gd.technologies[i].mailSent = MAILSENT_FINISHED;
				gd.technologies[i].markResearched.markOnly[gd.technologies[i].markResearched.numDefinitions] = qtrue;
				Q_strncpyz(gd.technologies[i].markResearched.campaign[gd.technologies[i].markResearched.numDefinitions],
					name, MAX_VAR);
				gd.technologies[i].markResearched.numDefinitions++;
				Com_DPrintf("...tech %s\n", gd.technologies[i].id);
				break;
			}

		if (i == gd.numTechnologies)
			Com_Printf("CL_ParseResearchedCampaignItems: unknown token \"%s\" ignored (tech %s)\n", token, name);

	} while (*text);
}

/**
 * @brief This function parses a list of items that should be set to researchable = true after campaign start
 * @param researchable Mark them researchable or not researchable
 */
extern void CL_ParseResearchableCampaignStates (const char *name, char **text, qboolean researchable)
{
	const char *errhead = "CL_ParseResearchableCampaignStates: unexpected end of file (equipment ";
	char *token;
	int i;
	campaign_t* campaign = NULL;

	campaign = CL_GetCampaign(Cvar_VariableString("campaign"));
	if (!campaign) {
		Com_Printf("CL_ParseResearchableCampaignStates: failed\n");
		return;
	}

	/* get it's body */
	token = COM_Parse(text);

	if (!*text || *token != '{') {
		Com_Printf("CL_ParseResearchableCampaignStates: equipment def \"%s\" without body ignored\n", name);
		return;
	}

	if (Q_strncmp(campaign->researched, name, MAX_VAR)) {
		Com_DPrintf("..don't use '%s' as researchable list\n", name);
		return;
	}

	Com_DPrintf("..campaign researchable list '%s'\n", name);
	do {
		token = COM_EParse(text, errhead, name);
		if (!*text || *token == '}')
			return;

		for (i = 0; i < gd.numTechnologies; i++)
			if (!Q_strncmp(token, gd.technologies[i].id, MAX_VAR)) {
				if (researchable) {
					gd.technologies[i].mailSent = MAILSENT_PROPOSAL;
					RS_MarkOneResearchable(&gd.technologies[i]);
				} else
					Com_Printf("TODO: Mark unresearchable");
				Com_DPrintf("...tech %s\n", gd.technologies[i].id);
				break;
			}

		if (i == gd.numTechnologies)
			Com_Printf("CL_ParseResearchableCampaignStates: unknown token \"%s\" ignored (tech %s)\n", token, name);

	} while (*text);
}

/* =========================================================== */

static const value_t stageset_vals[] = {
	{"needed", V_STRING, offsetof(stageSet_t, needed)}
	,
	{"delay", V_DATE, offsetof(stageSet_t, delay)}
	,
	{"frame", V_DATE, offsetof(stageSet_t, frame)}
	,
	{"expire", V_DATE, offsetof(stageSet_t, expire)}
	,
	{"number", V_INT, offsetof(stageSet_t, number)}
	,
	{"quota", V_INT, offsetof(stageSet_t, quota)}
	,
	{"seq", V_STRING, offsetof(stageSet_t, sequence)}
	,
	{"nextstage", V_STRING, offsetof(stageSet_t, nextstage)}
	,
	{"endstage", V_STRING, offsetof(stageSet_t, endstage)}
	,
	{"commands", V_STRING, offsetof(stageSet_t, cmds)}
	,
	{NULL, 0, 0}
	,
};

/**
 * @brief
 */
static void CL_ParseStageSet (const char *name, char **text)
{
	const char *errhead = "CL_ParseStageSet: unexpected end of file (stageset ";
	stageSet_t *sp;
	const value_t *vp;
	char missionstr[256];
	char *token, *misp;
	int j;

	if (numStageSets >= MAX_STAGESETS) {
		Com_Printf("CL_ParseStageSet: Max stagesets reached\n");
		return;
	}

	/* initialize the stage */
	sp = &stageSets[numStageSets++];
	memset(sp, 0, sizeof(stageSet_t));
	Q_strncpyz(sp->name, name, sizeof(sp->name));

	/* get it's body */
	token = COM_Parse(text);
	if (!*text || *token != '{') {
		Com_Printf("Com_ParseStageSets: stageset def \"%s\" without body ignored\n", name);
		numStageSets--;
		return;
	}

	do {
		token = COM_EParse(text, errhead, name);
		if (!*text)
			break;
		if (*token == '}')
			break;

		/* check for some standard values */
		for (vp = stageset_vals; vp->string; vp++)
			if (!Q_strcmp(token, vp->string)) {
				/* found a definition */
				token = COM_EParse(text, errhead, name);
				if (!*text)
					return;

				Com_ParseValue(sp, token, vp->type, vp->ofs);
				break;
			}
		if (vp->string)
			continue;

		/* get mission set */
		if (!Q_strncmp(token, "missions", 8)) {
			token = COM_EParse(text, errhead, name);
			if (!*text)
				return;
			Q_strncpyz(missionstr, token, sizeof(missionstr));
			misp = missionstr;

			/* add mission options */
			if (sp->numMissions) {
				Sys_Error("CL_ParseStageSet: Double mission tag in set '%s'\n", sp->name);
				return; /* code analyst */
			}
			sp->numMissions = 0;
			do {
				token = COM_Parse(&misp);
				if (!misp)
					break;

				for (j = 0; j < numMissions; j++)
					if (!Q_strncmp(token, missions[j].name, MAX_VAR)) {
						sp->missions[sp->numMissions++] = j;
						break;
					}

				if (j == numMissions)
					Com_Printf("Com_ParseStageSet: unknown mission \"%s\" ignored (stageset %s)\n", token, sp->name);
			} while (sp->numMissions < MAX_SETMISSIONS);
			continue;
		}
		Com_Printf("Com_ParseStageSet: unknown token \"%s\" ignored (stageset %s)\n", token, name);
	} while (*text);
	if (sp->numMissions && !sp->number && !sp->quota) {
		sp->quota = (int)(sp->numMissions / 2)+1;
		Com_Printf("Com_ParseStageSet: Set quota to %i for stage set '%s' with %i missions\n", sp->quota, sp->name, sp->numMissions);
	}
}


/**
 * @brief
 * @sa CL_ParseStageSet
 */
extern void CL_ParseStage (const char *name, char **text)
{
	const char *errhead = "CL_ParseStage: unexpected end of file (stage ";
	stage_t *sp;
	char *token;
	int i;

	/* search for campaigns with same name */
	for (i = 0; i < numStages; i++)
		if (!Q_strncmp(name, stages[i].name, MAX_VAR))
			break;

	if (i < numStages) {
		Com_Printf("Com_ParseStage: stage def \"%s\" with same name found, second ignored\n", name);
		return;
	} else if (numStages >= MAX_STAGES) {
		Com_Printf("CL_ParseStages: Max stages reached\n");
		return;
	}

	/* get it's body */
	token = COM_Parse(text);
	if (!*text || *token != '{') {
		Com_Printf("Com_ParseStages: stage def \"%s\" without body ignored\n", name);
		return;
	}

	/* initialize the stage */
	sp = &stages[numStages++];
	memset(sp, 0, sizeof(stage_t));
	Q_strncpyz(sp->name, name, sizeof(sp->name));
	sp->first = numStageSets;

	Com_DPrintf("stage: %s\n", name);

	do {
		token = COM_EParse(text, errhead, name);
		if (!*text)
			break;
		if (*token == '}')
			break;

		if (!Q_strncmp(token, "set", 3)) {
			token = COM_EParse(text, errhead, name);
			CL_ParseStageSet(token, text);
		} else
			Com_Printf("Com_ParseStage: unknown token \"%s\" ignored (stage %s)\n", token, name);
	} while (*text);

	sp->num = numStageSets - sp->first;
}

/* =========================================================== */

salary_t salaries[MAX_CAMPAIGNS];

static const value_t salary_vals[] = {
	{"soldier_base", V_INT, offsetof(salary_t, soldier_base)}
	,
	{"soldier_rankbonus", V_INT, offsetof(salary_t, soldier_rankbonus)}
	,
	{"worker_base", V_INT, offsetof(salary_t, worker_base)}
	,
	{"worker_rankbonus", V_INT, offsetof(salary_t, worker_rankbonus)}
	,
	{"scientist_base", V_INT, offsetof(salary_t, scientist_base)}
	,
	{"scientist_rankbonus", V_INT, offsetof(salary_t, scientist_rankbonus)}
	,
	{"medic_base", V_INT, offsetof(salary_t, medic_base)}
	,
	{"medic_rankbonus", V_INT, offsetof(salary_t, medic_rankbonus)}
	,
	{"robot_base", V_INT, offsetof(salary_t, robot_base)}
	,
	{"robot_rankbonus", V_INT, offsetof(salary_t, robot_rankbonus)}
	,
	{"aircraft_factor", V_INT, offsetof(salary_t, aircraft_factor)}
	,
	{"aircraft_divisor", V_INT, offsetof(salary_t, aircraft_divisor)}
	,
	{"base_upkeep", V_INT, offsetof(salary_t, base_upkeep)}
	,
	{"admin_initial", V_INT, offsetof(salary_t, admin_initial)}
	,
	{"admin_soldier", V_INT, offsetof(salary_t, admin_soldier)}
	,
	{"admin_worker", V_INT, offsetof(salary_t, admin_worker)}
	,
	{"admin_scientist", V_INT, offsetof(salary_t, admin_scientist)}
	,
	{"admin_medic", V_INT, offsetof(salary_t, admin_medic)}
	,
	{"admin_robot", V_INT, offsetof(salary_t, admin_robot)}
	,
	{"debt_interest", V_FLOAT, offsetof(salary_t, debt_interest)}
	,
	{NULL, 0, 0}
};

/**
 * @brief Parse the salaries from campaign definition
 * @param[in] name Name or ID of the found character skill and ability definition
 * @param[in] text The text of the nation node
 * @param[in] campaignID Current campaign id (idx)
 * @note Example:
 * <code>salary {
 *  soldier_base 3000
 * }</code>
 */
extern void CL_ParseSalary (const char *name, char **text, int campaignID)
{
	const char *errhead = "CL_ParseSalary: unexpected end of file ";
	salary_t *s;
	const value_t *vp;
	char *token;

	/* initialize the campaign */
	s = &salaries[campaignID];

	/* get it's body */
	token = COM_Parse(text);

	if (!*text || *token != '{') {
		Com_Printf("CL_ParseSalary: salary def without body ignored\n");
		return;
	}

	do {
		token = COM_EParse(text, errhead, name);
		if (!*text)
			break;
		if (*token == '}')
			break;

		/* check for some standard values */
		for (vp = salary_vals; vp->string; vp++)
			if (!Q_strcmp(token, vp->string)) {
				/* found a definition */
				token = COM_EParse(text, errhead, name);
				if (!*text)
					return;

				Com_ParseValue(s, token, vp->type, vp->ofs);
				break;
			}
		if (!vp->string) {
			Com_Printf("CL_ParseSalary: unknown token \"%s\" ignored (campaignID %i)\n", token, campaignID);
			COM_EParse(text, errhead, name);
		}
	} while (*text);
}

/* =========================================================== */

/**
 * @brief Parse the character ability and skill values from script
 * @param[in] name Name or ID of the found character skill and ability definition
 * @param[in] text The text of the nation node
 * @param[in] campaignID Current campaign id (idx)
 * @note Example:
 * <code>character_data {
 *  TEAM_ALIEN skill 15 75
 *  TEAM_ALIEN ability 15 95
 * }</code>
 */
extern void CL_ParseCharacterValues (const char *name, char **text, int campaignID)
{
	const char *errhead = "CL_ParseCharacterValues: unexpected end of file (character_data ";
	char *token;
	int team = 0, i, empl_type = 0;

	Com_DPrintf("...found character value %s\n", name);

	/* get it's body */
	token = COM_Parse(text);

	if (!*text || *token != '{') {
		Com_Printf("CL_ParseCharacterValues: character_data def \"%s\" without body ignored\n", name);
		return;
	}

	do {
		token = COM_EParse(text, errhead, name);
		if (!*text)
			break;
		if (*token == '}')
			break;

		team = Com_StringToTeamNum(token);
		/* >= MAX_TEAMS is impossible atm - but who knows */
		if (team < 0 || team >= MAX_TEAMS)
			Sys_Error("CL_ParseCharacterValues: Unknown teamString\n");
		/* now let's check whether we want parse skill or abilitie values */
		token = COM_EParse(text, errhead, name);
		if (!*text)
			Sys_Error("CL_ParseCharacterValues: invalid character_data entry for team %i\n", team);
		empl_type = E_GetEmployeeType(token);
		token = COM_EParse(text, errhead, name);
		if (!*text)
			Sys_Error("CL_ParseCharacterValues: invalid character_data entry for team %i\n", team);

		/* found a definition */
		if (!Q_strcmp(token, "skill")) {
			for (i = 0; i < 2; i++) {
				token = COM_EParse(text, errhead, name);
				if (!*text)
					Sys_Error("CL_ParseCharacterValues: invalid skill entry for team %i\n", team);
				skillValues[campaignID][team][empl_type][i] = atoi(token);
			}
		} else if (!Q_strcmp(token, "ability")) {
			for (i = 0; i < 2; i++) {
				token = COM_EParse(text, errhead, name);
				if (!*text)
					Sys_Error("CL_ParseCharacterValues: invalid ability entry for team %i\n", team);
				abilityValues[campaignID][team][empl_type][i] = atoi(token);
			}
		}
	} while (*text);
}

/* =========================================================== */

static const value_t campaign_vals[] = {
	{"team", V_STRING, offsetof(campaign_t, team)}
	,
	{"soldiers", V_INT, offsetof(campaign_t, soldiers)}
	,
	{"workers", V_INT, offsetof(campaign_t, workers)}
	,
	{"medics", V_INT, offsetof(campaign_t, medics)}
	,
	{"scientists", V_INT, offsetof(campaign_t, scientists)}
	,
	{"ugvs", V_INT, offsetof(campaign_t, ugvs)}
	,
	{"equipment", V_STRING, offsetof(campaign_t, equipment)}
	,
	{"market", V_STRING, offsetof(campaign_t, market)}
	,
	{"researched", V_STRING, offsetof(campaign_t, researched)}
	,
	{"difficulty", V_INT, offsetof(campaign_t, difficulty)}
	,
	{"firststage", V_STRING, offsetof(campaign_t, firststage)}
	,
	{"map", V_STRING, offsetof(campaign_t, map)}
	,
	{"credits", V_INT, offsetof(campaign_t, credits)}
	,
	{"visible", V_BOOL, offsetof(campaign_t, visible)}
	,
	{"text", V_TRANSLATION2_STRING, offsetof(campaign_t, text)}
	,							/* just a gettext placeholder */
	{"name", V_TRANSLATION_STRING, offsetof(campaign_t, name)}
	,
	{"date", V_DATE, offsetof(campaign_t, date)}
	,
	{NULL, 0, 0}
	,
};

/**
 * @brief
 */
extern void CL_ParseCampaign (const char *name, char **text)
{
	const char *errhead = "CL_ParseCampaign: unexpected end of file (campaign ";
	campaign_t *cp;
	const value_t *vp;
	char *token;
	int i;
	salary_t *s;

	/* search for campaigns with same name */
	for (i = 0; i < numCampaigns; i++)
		if (!Q_strncmp(name, campaigns[i].id, sizeof(campaigns[i].id)))
			break;

	if (i < numCampaigns) {
		Com_Printf("CL_ParseCampaign: campaign def \"%s\" with same name found, second ignored\n", name);
		return;
	}

	if (numCampaigns >= MAX_CAMPAIGNS) {
		Com_Printf("CL_ParseCampaign: Max campaigns reached (%i)\n", MAX_CAMPAIGNS);
		return;
	}

	/* initialize the campaign */
	cp = &campaigns[numCampaigns++];
	memset(cp, 0, sizeof(campaign_t));

	cp->idx = numCampaigns-1;
	Q_strncpyz(cp->id, name, sizeof(cp->id));

	memset(skillValues[cp->idx], -1, sizeof(skillValues[cp->idx]));
	memset(abilityValues[cp->idx], -1, sizeof(abilityValues[cp->idx]));

	/* some default values */
	Q_strncpyz(cp->team, "human", sizeof(cp->team));
	Q_strncpyz(cp->researched, "researched_human", sizeof(cp->researched));

	/* get it's body */
	token = COM_Parse(text);

	if (!*text || *token != '{') {
		Com_Printf("CL_ParseCampaign: campaign def \"%s\" without body ignored\n", name);
		numCampaigns--;
		return;
	}

	/* some default values */
	s = &salaries[numCampaigns-1];
	memset(s, 0, sizeof(salary_t));
	s->soldier_base = 3000;
	s->soldier_rankbonus = 500;
	s->worker_base = 3000;
	s->worker_rankbonus = 500;
	s->scientist_base = 3000;
	s->scientist_rankbonus = 500;
	s->medic_base = 3000;
	s->medic_rankbonus = 500;
	s->robot_base = 7500;
	s->robot_rankbonus = 1500;
	s->aircraft_factor = 1;
	s->aircraft_divisor = 25;
	s->base_upkeep = 20000;
	s->admin_initial = 1000;
	s->admin_soldier = 75;
	s->admin_worker = 75;
	s->admin_scientist = 75;
	s->admin_medic = 75;
	s->admin_robot = 150;
	s->debt_interest = 0.005;

	do {
		token = COM_EParse(text, errhead, name);
		if (!*text)
			break;
		if (*token == '}')
			break;

		/* check for some standard values */
		for (vp = campaign_vals; vp->string; vp++)
			if (!Q_strcmp(token, vp->string)) {
				/* found a definition */
				token = COM_EParse(text, errhead, name);
				if (!*text)
					return;

				Com_ParseValue(cp, token, vp->type, vp->ofs);
				break;
			}
		if (!Q_strncmp(token, "character_data", MAX_VAR)) {
			CL_ParseCharacterValues(token, text, numCampaigns-1);
		} else if (!Q_strncmp(token, "salary", MAX_VAR)) {
			CL_ParseSalary(token, text, numCampaigns-1);
		} else if (!vp->string) {
			Com_Printf("CL_ParseCampaign: unknown token \"%s\" ignored (campaign %s)\n", token, name);
			COM_EParse(text, errhead, name);
		}
	} while (*text);
}

/* =========================================================== */

static const value_t nation_vals[] = {
	{"name", V_TRANSLATION_STRING, offsetof(nation_t, name)}
	,
	{"pos", V_POS, offsetof(nation_t, pos)}
	,
	{"color", V_COLOR, offsetof(nation_t, color)}
	,
	{"funding", V_INT, offsetof(nation_t, funding)}
	,
	{"happiness", V_FLOAT, offsetof(nation_t, happiness)}
	,
	{"alien_friendly", V_FLOAT, offsetof(nation_t, alienFriendly)}
	,
	{"soldiers", V_INT, offsetof(nation_t, soldiers)}
	,
	{"scientists", V_INT, offsetof(nation_t, scientists)}
	,
	{"names", V_INT, offsetof(nation_t, names)}
	,

	{NULL, 0, 0}
};

/**
 * @brief Parse the nation data from script file
 * @param[in] name Name or ID of the found nation
 * @param[in] text The text of the nation node
 * @sa nation_vals
 */
extern void CL_ParseNations (const char *name, char **text)
{
	const char *errhead = "CL_ParseNations: unexpected end of file (nation ";
	nation_t *nation;
	const value_t *vp;
	char *token;

	if (gd.numNations >= MAX_NATIONS) {
		Com_Printf("CL_ParseNations: nation def \"%s\" with same name found, second ignored\n", name);
		return;
	}

	/* initialize the nation */
	nation = &gd.nations[gd.numNations++];
	memset(nation, 0, sizeof(nation_t));

	Com_DPrintf("...found nation %s\n", name);
	Q_strncpyz(nation->id, name, sizeof(nation->id));

	/* get it's body */
	token = COM_Parse(text);

	if (!*text || *token != '{') {
		Com_Printf("CL_ParseNations: nation def \"%s\" without body ignored\n", name);
		gd.numNations--;
		return;
	}

	do {
		token = COM_EParse(text, errhead, name);
		if (!*text)
			break;
		if (*token == '}')
			break;

		/**
		 * <code>
		 * borders {
		 *	"13.5 27.4"
		 *	"-13.5 87.4"
		 *	"[...]"
		 * }
		 * </code>
		 */
		if (!Q_strcmp(token, "borders")) {
			/* found border definitions */
			token = COM_EParse(text, errhead, name);
			if (!*text)
				return;
			if (*token != '{') {
				Com_Printf("CL_ParseNations: empty nation borders - skip it (%s)\n", name);
				continue;
			}
			do {
				token = COM_EParse(text, errhead, name);
				if (!*text)
					return;
				if (*token != '}') {
					if (nation->numBorders >= MAX_NATION_BORDERS)
						Sys_Error("CL_ParseNations: too many nation borders for nation '%s'\n", name);
					Com_ParseValue(nation, token, V_POS, offsetof(nation_t, borders[nation->numBorders++]));
				}
			} while (*token != '}');
		} else {
			/* check for some standard values */
			for (vp = nation_vals; vp->string; vp++)
				if (!Q_strcmp(token, vp->string)) {
					/* found a definition */
					token = COM_EParse(text, errhead, name);
					if (!*text)
						return;

					Com_ParseValue(nation, token, vp->type, vp->ofs);
					break;
				}

			if (!vp->string) {
				Com_Printf("CL_ParseNations: unknown token \"%s\" ignored (nation %s)\n", token, name);
				COM_EParse(text, errhead, name);
			}
		}
	} while (*text);
}

/**
 * @brief Check whether we are in a tactical mission as server or as client
 * @note handles multiplayer and singleplayer
 *
 * @return true when we are not in battlefield
 * TODO: Check cvar mn_main for value
 */
extern qboolean CL_OnBattlescape (void)
{
	/* sv.state is set to zero on every battlefield shutdown */
	if (Com_ServerState() > 0)
		return qtrue; /* server */

	/* client */
	if (cls.state >= ca_connected)
		return qtrue;

	return qfalse;
}

/**
 * @brief Starts a given mission
 * @note Mainly for testing
 * @sa CL_SetMissionCvars
 * @sa CL_GameGo
 */
static void CL_StartMission_f (void)
{
	int i;
	const char *missionID;
	mission_t* mission = NULL;

	if (Cmd_Argc() < 2) {
		Com_Printf("Usage: mission <missionID>\n");
		return;
	}

	missionID = Cmd_Argv(1);

	for (i = 0; i < numMissions; i++) {
		mission = &missions[i];
		if (!Q_strncmp(missions[i].name, missionID, sizeof(missions[i].name))) {
			break;
		}
	}
	if (i == numMissions) {
		Com_Printf("Mission with id '%s' was not found\n", missionID);
		return;
	}

	CL_SetMissionCvars(mission);
	CL_StartMissionMap(mission);
}

/**
 * @brief Scriptfunction to list all parsed nations with their current values
 */
static void CL_NationList_f (void)
{
	int i;
	for (i = 0; i < gd.numNations; i++) {
		Com_Printf("Nation ID: %s\n", gd.nations[i].id);
		Com_Printf("...funding %i c\n", gd.nations[i].funding);
		Com_Printf("...alienFriendly %0.2f\n", gd.nations[i].alienFriendly);
		Com_Printf("...happiness %0.2f\n", gd.nations[i].happiness);
		Com_Printf("...soldiers %i\n", gd.nations[i].soldiers);
		Com_Printf("...scientists %i\n", gd.nations[i].scientists);
		Com_Printf("...color r:%.2f g:%.2f b:%.2f a:%.2f\n", gd.nations[i].color[0], gd.nations[i].color[1], gd.nations[i].color[2], gd.nations[i].color[3]);
		Com_Printf("...pos x:%.0f y:%.0f\n", gd.nations[i].pos[0], gd.nations[i].pos[1]);
	}
}

/* ===================================================================== */

/* these commands are only available in singleplayer */
static const cmdList_t game_commands[] = {
	{"aircraft_start", CL_AircraftStart_f, NULL}
#ifdef DEBUG
	,
	{"aircraftlist", CL_ListAircraft_f, NULL}
#endif
	,
	{"aircraft_select", CL_AircraftSelect_f, NULL}
	,
	{"aircraft_init", CL_AircraftInit, NULL}
	,
	{"airequip_init", CL_AircraftEquipmenuMenuInit_f, NULL}
	,
	{"airequip_list_click", CL_AircraftEquipmenuMenuClick_f, NULL}
	,
	{"mn_next_aircraft", MN_NextAircraft_f, NULL}
	,
	{"mn_prev_aircraft", MN_PrevAircraft_f, NULL}
	,
	{"aircraft_new", CL_NewAircraft_f, NULL}
	,
	{"mn_reset_air", CL_ResetAircraftCvars_f, NULL}
	,
	{"aircraft_return", CL_AircraftReturnToBase_f, NULL}
	,
	{"aircraft_list", CL_AircraftList_f, "Generate the aircraft (interception) list"}
	,
	{"stats_update", CL_StatsUpdate_f, NULL}
	,
	{"game_go", CL_GameGo, NULL}
	,
	{"game_auto_check", CL_GameAutoCheck_f, NULL}
	,
	{"game_auto_go", CL_GameAutoGo_f, NULL}
	,
	{"game_abort", CL_GameAbort_f, NULL}
	,
	{"game_results", CL_GameResults_f, "Parses and shows the game results"}
	,
	{"game_timestop", CL_GameTimeStop, NULL}
	,
	{"game_timeslow", CL_GameTimeSlow, NULL}
	,
	{"game_timefast", CL_GameTimeFast, NULL}
	,
	{"inc_sensor", B_SetSensor_f, "Increase the radar range"}
	,
	{"dec_sensor", B_SetSensor_f, "Decrease the radar range"}
	,
	{"mn_mapaction_reset", MAP_ResetAction, NULL}
	,
	{"nationlist", CL_NationList_f, "List all nations on the game console"}
	,
	{"mission", CL_StartMission_f, NULL}
	,
#ifdef DEBUG
	{"debug_fullcredits", CL_DebugFullCredits_f, "Debug function to give the player full credits"}
	,
#endif
	{NULL, NULL, NULL}
};

/**
 * @brief
 * @sa CL_GameNew
 * @sa SAV_GameLoad
 */
extern void CL_GameExit (void)
{
	const cmdList_t *commands;

	Cbuf_AddText("disconnect\n");
	Cvar_Set("mn_main", "main");
	Cvar_Set("mn_active", "");
	MN_ShutdownMessageSystem();
	CL_InitMessageSystem();
	/* singleplayer commands are no longer available */
	if (curCampaign) {
		Com_DPrintf("Remove game commands\n");
		for (commands = game_commands; commands->name; commands++) {
			Com_DPrintf("...%s\n", commands->name);
			Cmd_RemoveCommand(commands->name);
		}
	}
	curCampaign = NULL;
}

/**
 * @brief Called at new game and load game
 * @sa SAV_GameLoad
 * @sa CL_GameNew
 */
void CL_GameInit (void)
{
	const cmdList_t *commands;

	assert(curCampaign);

	Com_DPrintf("Init game commands\n");
	for (commands = game_commands; commands->name; commands++) {
		Com_DPrintf("...%s\n", commands->name);
		Cmd_AddCommand(commands->name, commands->function, commands->description);
	}

	CL_GameTimeStop();

	Com_AddObjectLinks();	/* Add tech links + ammo<->weapon links to items.*/
	RS_InitTree();		/* Initialise all data in the research tree. */

	/* After inited the techtree we can assign the weapons
	 * and shields to aircrafts. */
	CL_AircraftInit();

	CL_CampaignInitMarket();

	/* Init popup and map/geoscape */
	CL_PopupInit();
	MAP_GameInit();
	Com_SetGlobalCampaignID(curCampaign->idx);
}

/**
 * @brief Returns the campaign pointer from global campaign array
 * @param name Name of the campaign
 * @return campaign_t pointer to campaign with name or NULL if not found
 */
campaign_t* CL_GetCampaign (const char* name)
{
	campaign_t* campaign;
	int i;

	for (i = 0, campaign = campaigns; i < numCampaigns; i++, campaign++)
		if (!Q_strncmp(name, campaign->id, MAX_VAR))
			break;

	if (i == numCampaigns) {
		Com_Printf("CL_GetCampaign: Campaign \"%s\" doesn't exist.\n", name);
		return NULL;
	}
	return campaign;
}

/* called by CL_GameNew */
static void CL_CampaignRunMarket(void);

/**
 * @brief Starts a new single-player game
 * @sa CL_GameInit
 */
static void CL_GameNew (void)
{
	int i;
	char val[8];

	Cvar_Set("mn_main", "singleplayerInGame");
	Cvar_Set("mn_active", "map");
	Cvar_SetValue("maxclients", 1.0f);

	/* exit running game */
	if (curCampaign)
		CL_GameExit();

	/* clear any old pending messages */
	CL_InitMessageSystem();

	/* get campaign - they are already parsed here */
	curCampaign = CL_GetCampaign(Cvar_VariableString("campaign"));
	if (!curCampaign)
		return;

	memset(&ccs, 0, sizeof(ccs_t));
	CL_StartSingleplayer(qtrue);

	ccs.angles[YAW] = GLOBE_ROTATE;
	ccs.date = curCampaign->date;

	memset(&gd, 0, sizeof(gd));
	memset(&stats, 0, sizeof(stats));
	CL_ReadSinglePlayerData();

	Cvar_Set("team", curCampaign->team);

	/* difficulty */
	Com_sprintf(val, sizeof(val), "%i", curCampaign->difficulty);
	Cvar_ForceSet("difficulty", val);

	if (difficulty->integer < -4)
		Cvar_ForceSet("difficulty", "-4");
	else if (difficulty->integer > 4)
		Cvar_ForceSet("difficulty", "4");

	re.LoadTGA(va("pics/menu/%s_mask.tga", curCampaign->map), &maskPic, &maskWidth, &maskHeight);
	if (!maskPic)
		Sys_Error("Couldn't load map mask %s_mask.tga in pics/menu\n", curCampaign->map);

	/* base setup */
	gd.numBases = 0;
	gd.numAircraft = 0;
	B_NewBases();
	PR_ProductionInit();

	/* reset, set time */
	selMis = NULL;

	/* ensure ccs.singleplayer is set to true */
	CL_StartSingleplayer(qtrue);

	/* get day */
	while (ccs.date.sec > 3600 * 24) {
		ccs.date.sec -= 3600 * 24;
		ccs.date.day++;
	}

	/* set map view */
	ccs.center[0] = ccs.center[1] = 0.5;
	ccs.zoom = 1.0;

	CL_UpdateCredits(curCampaign->credits);

	/* stage setup */
	CL_CampaignActivateStage(curCampaign->firststage, qtrue);

	MN_PopMenu(qtrue);
	MN_PushMenu("map");

	/* create a base as first step */
	Cbuf_ExecuteText(EXEC_NOW, "mn_select_base -1");

	/* add some items to the market so it does not start empty */
	for (i = 0; i < 7; i++)
		CL_CampaignRunMarket();

	CL_GameInit();
}

#define MAXCAMPAIGNTEXT 4096
static char campaignText[MAXCAMPAIGNTEXT];
static char campaignDesc[MAXCAMPAIGNTEXT];
/**
 * @brief fill a list with available campaigns
 */
static void CP_GetCampaigns_f (void)
{
	int i;

	*campaignText = *campaignDesc = '\0';
	for (i = 0; i < numCampaigns; i++) {
		if (campaigns[i].visible)
			Q_strcat(campaignText, va("%s\n", campaigns[i].name), MAXCAMPAIGNTEXT);
	}
	/* default campaign */
	menuText[TEXT_STANDARD] = campaignDesc;

	/* select main as default */
	for (i = 0; i < numCampaigns; i++)
		if (!Q_strncmp("main", campaigns[i].id, MAX_VAR)) {
			Cbuf_ExecuteText(EXEC_NOW, va("campaignlist_click %i;", i));
			break;
		}

}

/**
 * @brief Script function to select a campaign from campaign list
 */
static void CP_CampaignsClick_f (void)
{
	int num;
	const char *racetype;

	if (Cmd_Argc() < 2) {
		Com_Printf("Usage: %s <arg>\n", Cmd_Argv(0));
		return;
	}

	/*which building? */
	num = atoi(Cmd_Argv(1));

	/* jump over all invisible campaigns */
	while (!campaigns[num].visible) {
		num++;
		if (num >= numCampaigns)
			return;
	}

	if (num >= numCampaigns || num < 0)
		return;

	Cvar_Set("campaign", campaigns[num].id);
	racetype = campaigns[num].team;
	(!Q_strncmp(racetype, "human", 5)) ? (racetype = _("Human")) : (racetype = _("Aliens"));

	Com_sprintf(campaignDesc, sizeof(campaignDesc), _("Race: %s\nRecruits: %i %s, %i %s, %i %s, %i %s\nCredits: %ic\nDifficulty: %s\n%s\n"),
			racetype,
			campaigns[num].soldiers, ngettext("soldier", "soldiers", campaigns[num].soldiers),
			campaigns[num].scientists, ngettext("scientist", "scientists", campaigns[num].scientists),
			campaigns[num].workers, ngettext("worker", "workers", campaigns[num].workers),
			campaigns[num].medics, ngettext("medic", "medics", campaigns[num].medics),
			campaigns[num].credits, CL_ToDifficultyName(campaigns[num].difficulty), _(campaigns[num].text));
	menuText[TEXT_STANDARD] = campaignDesc;
}

/**
 * @brief Will clear most of the parsed singleplayer data
 * @sa Com_InitInventory
 */
extern void CL_ResetSinglePlayerData (void)
{
	numStageSets = numStages = numMissions = 0;
	memset(missions, 0, sizeof(mission_t) * MAX_MISSIONS);
	memset(stageSets, 0, sizeof(stageSet_t) * MAX_STAGESETS);
	memset(stages, 0, sizeof(stage_t) * MAX_STAGES);
	memset(&invList, 0, sizeof(invList));
	/* called to flood the hash list - because the parse tech function
	 * was maybe already called */
	RS_ResetHash();
	E_ResetEmployees();
	Com_InitInventory(invList);
}

/**
 * @brief Show campaign stats in console
 *
 * call this function via campaign_stats
 */
static void CP_CampaignStats (void)
{
	setState_t *set;
	int i;

	if (!curCampaign) {
		Com_Printf("No campaign active\n");
		return;
	}

	Com_Printf("Campaign id: %s\n", curCampaign->id);
	Com_Printf("..research list: %s\n", curCampaign->researched);
	Com_Printf("..equipment: %s\n", curCampaign->equipment);
	Com_Printf("..team: %s\n", curCampaign->team);

	Com_Printf("..active stage: %s\n", testStage->name);
	for (i = 0, set = &ccs.set[testStage->first]; i < testStage->num; i++, set++) {
		Com_Printf("....name: %s\n", set->def->name);
		Com_Printf("......needed: %s\n", set->def->needed);
		Com_Printf("......quota: %i\n", set->def->quota);
		Com_Printf("......number: %i\n", set->def->number);
		Com_Printf("......done: %i\n", set->done);
	}
	Com_Printf("..salaries:\n");
	Com_Printf("...soldier_base: %i\n", SALARY_SOLDIER_BASE);
	Com_Printf("...soldier_rankbonus: %i\n", SALARY_SOLDIER_RANKBONUS);
	Com_Printf("...worker_base: %i\n", SALARY_WORKER_BASE);
	Com_Printf("...worker_rankbonus: %i\n", SALARY_WORKER_RANKBONUS);
	Com_Printf("...scientist_base: %i\n", SALARY_SCIENTIST_BASE);
	Com_Printf("...scientist_rankbonus: %i\n", SALARY_SCIENTIST_RANKBONUS);
	Com_Printf("...medic_base: %i\n", SALARY_MEDIC_BASE);
	Com_Printf("...medic_rankbonus: %i\n", SALARY_MEDIC_RANKBONUS);
	Com_Printf("...robot_base: %i\n", SALARY_ROBOT_BASE);
	Com_Printf("...robot_rankbonus: %i\n", SALARY_ROBOT_RANKBONUS);
	Com_Printf("...aircraft_factor: %i\n", SALARY_AIRCRAFT_FACTOR);
	Com_Printf("...aircraft_divisor: %i\n", SALARY_AIRCRAFT_DIVISOR);
	Com_Printf("...base_upkeep: %i\n", SALARY_BASE_UPKEEP);
	Com_Printf("...admin_initial: %i\n", SALARY_ADMIN_INITIAL);
	Com_Printf("...admin_soldier: %i\n", SALARY_ADMIN_SOLDIER);
	Com_Printf("...admin_worker: %i\n", SALARY_ADMIN_WORKER);
	Com_Printf("...admin_scientist: %i\n", SALARY_ADMIN_SCIENTIST);
	Com_Printf("...admin_medic: %i\n", SALARY_ADMIN_MEDIC);
	Com_Printf("...admin_robot: %i\n", SALARY_ADMIN_ROBOT);
	Com_Printf("...debt_interest: %.5f\n", SALARY_DEBT_INTEREST);
}

/**
 * @brief
 */
extern void CL_ResetCampaign (void)
{
	/* reset some vars */
	curCampaign = NULL;
	baseCurrent = NULL;
	menuText[TEXT_CAMPAIGN_LIST] = campaignText;

	/* commands */
	Cmd_AddCommand("campaign_stats", CP_CampaignStats, NULL);
	Cmd_AddCommand("campaignlist_click", CP_CampaignsClick_f, NULL);
	Cmd_AddCommand("getcampaigns", CP_GetCampaigns_f, NULL);
	Cmd_AddCommand("missionlist", CP_MissionList_f, "Shows all missions from the script files");
	Cmd_AddCommand("game_new", CL_GameNew, NULL);	Cmd_AddCommand("game_exit", CL_GameExit, NULL);
#ifdef DEBUG
	Cmd_AddCommand("debug_statsupdate", CL_DebugChangeCharacterStats_f, NULL);
#endif
}
