/**
 * @file
 * @brief Campaign parsing code
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

#include "../../DateTime.h"
#include "../../cl_shared.h"
#include "../../../shared/parse.h"
#include "cp_campaign.h"
#include "cp_rank.h"
#include "cp_parse.h"
#include "../../cl_inventory.h" /* INV_GetEquipmentDefinitionByID */
#include "cp_component.h"

/**
 * @return Alien mission category
 * @sa interestCategory_t
 */
static interestCategory_t CP_GetAlienMissionTypeByID (const char* type)
{
	if (Q_streq(type, "recon"))
		return INTERESTCATEGORY_RECON;
	else if (Q_streq(type, "terror"))
		return INTERESTCATEGORY_TERROR_ATTACK;
	else if (Q_streq(type, "baseattack"))
		return INTERESTCATEGORY_BASE_ATTACK;
	else if (Q_streq(type, "building"))
		return INTERESTCATEGORY_BUILDING;
	else if (Q_streq(type, "supply"))
		return INTERESTCATEGORY_SUPPLY;
	else if (Q_streq(type, "xvi"))
		return INTERESTCATEGORY_XVI;
	else if (Q_streq(type, "intercept"))
		return INTERESTCATEGORY_INTERCEPT;
	else if (Q_streq(type, "harvest"))
		return INTERESTCATEGORY_HARVEST;
	else if (Q_streq(type, "alienbase"))
		return INTERESTCATEGORY_ALIENBASE;
	else if (Q_streq(type, "ufocarrier"))
		return INTERESTCATEGORY_UFOCARRIER;
	else if (Q_streq(type, "rescue"))
		return INTERESTCATEGORY_RESCUE;
	else {
		cgi->Com_Printf("CP_GetAlienMissionTypeByID: unknown alien mission category '%s'\n", type);
		return INTERESTCATEGORY_NONE;
	}
}

static const value_t alien_group_vals[] = {
	{"mininterest", V_INT, offsetof(alienTeamGroup_t, minInterest), 0},
	{"maxinterest", V_INT, offsetof(alienTeamGroup_t, maxInterest), 0},
	{"minaliencount", V_INT, offsetof(alienTeamGroup_t, minAlienCount), 0},
	{"maxaliencount", V_INT, offsetof(alienTeamGroup_t, maxAlienCount), 0},
	{nullptr, V_NULL, 0, 0}
};

/**
 * @sa CL_ParseScriptFirst
 */
static void CP_ParseAlienTeam (const char* name, const char** text)
{
	const char* errhead = "CP_ParseAlienTeam: unexpected end of file (alienteam ";
	const char* token;
	int i;
	alienTeamCategory_t* alienCategory;

	/* get it's body */
	token = Com_Parse(text);

	if (!*text || *token != '{') {
		cgi->Com_Printf("CP_ParseAlienTeam: alien team category \"%s\" without body ignored\n", name);
		return;
	}

	if (ccs.numAlienCategories >= ALIENCATEGORY_MAX) {
		cgi->Com_Printf("CP_ParseAlienTeam: maximum number of alien team category reached (%i)\n", ALIENCATEGORY_MAX);
		return;
	}

	/* search for category with same name */
	for (i = 0; i < ccs.numAlienCategories; i++)
		if (Q_streq(name, ccs.alienCategories[i].id))
			break;
	if (i < ccs.numAlienCategories) {
		cgi->Com_Printf("CP_ParseAlienTeam: alien category def \"%s\" with same name found, second ignored\n", name);
		return;
	}

	alienCategory = &ccs.alienCategories[ccs.numAlienCategories++];
	Q_strncpyz(alienCategory->id, name, sizeof(alienCategory->id));

	do {
		token = cgi->Com_EParse(text, errhead, name);
		if (!*text)
			break;
		if (*token == '}')
			break;

		if (Q_streq(token, "equipment")) {
			linkedList_t** list = &alienCategory->equipment;
			if (!cgi->Com_ParseList(text, list)) {
				cgi->Com_Error(ERR_DROP, "CL_ParseAlienTeam: \"%s\" Error while parsing equipment list", name);
			}
		} else if (Q_streq(token, "category")) {
			linkedList_t* list;
			if (!cgi->Com_ParseList(text, &list)) {
				cgi->Com_Error(ERR_DROP, "CL_ParseAlienTeam: \"%s\" Error while parsing category list", name);
			}
			for (linkedList_t* element = list; element != nullptr; element = element->next) {
				alienCategory->missionCategories[alienCategory->numMissionCategories] = CP_GetAlienMissionTypeByID((const char*)element->data);
				if (alienCategory->missionCategories[alienCategory->numMissionCategories] == INTERESTCATEGORY_NONE)
					cgi->Com_Printf("CP_ParseAlienTeam: alien team category \"%s\" is used with no mission category. It won't be used in game.\n", name);
				alienCategory->numMissionCategories++;
			}
			cgi->LIST_Delete(&list);
		} else if (Q_streq(token, "teaminterest")) {
			alienTeamGroup_t* group;

			token = cgi->Com_EParse(text, errhead, name);
			if (!*text || *token != '{') {
				cgi->Com_Printf("CP_ParseAlienTeam: alien team \"%s\" has team with no opening brace\n", name);
				break;
			}

			if (alienCategory->numAlienTeamGroups >= MAX_ALIEN_GROUP_PER_CATEGORY) {
				cgi->Com_Printf("CP_ParseAlienTeam: maximum number of alien team reached (%i) in category \"%s\"\n", MAX_ALIEN_GROUP_PER_CATEGORY, name);
				break;
			}

			group = &alienCategory->alienTeamGroups[alienCategory->numAlienTeamGroups];
			group->idx = alienCategory->numAlienTeamGroups;
			group->categoryIdx = alienCategory - ccs.alienCategories;
			alienCategory->numAlienTeamGroups++;

			do {
				token = cgi->Com_EParse(text, errhead, name);

				if (!cgi->Com_ParseBlockToken(name, text, group, alien_group_vals, cp_campaignPool, token)) {
					if (!*text || *token == '}')
						break;

					if (Q_streq(token, "team")) {
						linkedList_t* list;
						if (!cgi->Com_ParseList(text, &list)) {
							cgi->Com_Error(ERR_DROP, "CL_ParseAlienTeam: \"%s\" Error while parsing team list", name);
						}
						for (linkedList_t* element = list; element != nullptr; element = element->next) {
							if (group->numAlienTeams >= MAX_TEAMS_PER_MISSION)
								cgi->Com_Error(ERR_DROP, "CL_ParseAlienTeam: MAX_TEAMS_PER_MISSION hit");
							const teamDef_t* teamDef = cgi->Com_GetTeamDefinitionByID(strtok((char*)element->data, "/"));
							if (teamDef) {
								group->alienTeams[group->numAlienTeams] = teamDef;
								const chrTemplate_t* chrTemplate = CHRSH_GetTemplateByID(teamDef, strtok(nullptr, ""));
								group->alienChrTemplates[group->numAlienTeams] = chrTemplate;
								++group->numAlienTeams;
							}
						}
						cgi->LIST_Delete(&list);
					} else {
						cgi->Com_Error(ERR_DROP, "CL_ParseAlienTeam: Unknown token \"%s\"\n", token);
					}
				}
			} while (*text);

			if (group->minAlienCount > group->maxAlienCount) {
				cgi->Com_Printf("CP_ParseAlienTeam: Minimum number of aliens is greater than maximum value! Swapped.\n");
				const int swap = group->minAlienCount;
				group->minAlienCount = group->maxAlienCount;
				group->maxAlienCount = swap;
			}
		} else {
			cgi->Com_Printf("CP_ParseAlienTeam: unknown token \"%s\" ignored (category %s)\n", token, name);
			continue;
		}
	} while (*text);

	if (cgi->LIST_IsEmpty(alienCategory->equipment))
		Sys_Error("alien category equipment list is empty");
}

/**
 * @brief This function parses a list of items that should be set to researched = true after campaign start
 */
static void CP_ParseResearchedCampaignItems (const campaign_t* campaign, const char* name, const char** text)
{
	const char* errhead = "CP_ParseResearchedCampaignItems: unexpected end of file (equipment ";
	const char* token;
	int i;

	/* Don't parse if it is not definition for current type of campaign. */
	if (!Q_streq(campaign->researched, name))
		return;

	/* get it's body */
	token = Com_Parse(text);

	if (!*text || *token != '{') {
		cgi->Com_Printf("CP_ParseResearchedCampaignItems: equipment def \"%s\" without body ignored (%s)\n",
				name, token);
		return;
	}

	cgi->Com_DPrintf(DEBUG_CLIENT, "..campaign research list '%s'\n", name);
	do {
		token = cgi->Com_EParse(text, errhead, name);
		if (!*text || *token == '}')
			return;

		for (i = 0; i < ccs.numTechnologies; i++) {
			technology_t* tech = RS_GetTechByIDX(i);
			assert(tech);
			if (Q_streq(token, tech->id)) {
				tech->mailSent = MAILSENT_FINISHED;
				tech->markResearched.markOnly[tech->markResearched.numDefinitions] = true;
				tech->markResearched.campaign[tech->markResearched.numDefinitions] = cgi->PoolStrDup(name, cp_campaignPool, 0);
				tech->markResearched.numDefinitions++;
				cgi->Com_DPrintf(DEBUG_CLIENT, "...tech %s\n", tech->id);
				break;
			}
		}

		if (i == ccs.numTechnologies)
			cgi->Com_Printf("CP_ParseResearchedCampaignItems: unknown token \"%s\" ignored (tech %s)\n", token, name);

	} while (*text);
}

/**
 * @brief This function parses a list of items that should be set to researchable = true after campaign start
 * @param[in] campaign The campaign data structure
 * @param[in] name Name of the techlist
 * @param[in,out] text Script to parse
 * @param[in] researchable Mark them researchable or not researchable
 * @sa CP_ParseScriptFirst
 */
static void CP_ParseResearchableCampaignStates (const campaign_t* campaign, const char* name, const char** text, bool researchable)
{
	const char* errhead = "CP_ParseResearchableCampaignStates: unexpected end of file (equipment ";
	const char* token;
	int i;

	/* get it's body */
	token = Com_Parse(text);

	if (!*text || *token != '{') {
		cgi->Com_Printf("CP_ParseResearchableCampaignStates: equipment def \"%s\" without body ignored\n", name);
		return;
	}

	if (!Q_streq(campaign->researched, name)) {
		cgi->Com_DPrintf(DEBUG_CLIENT, "..don't use '%s' as researchable list\n", name);
		return;
	}

	cgi->Com_DPrintf(DEBUG_CLIENT, "..campaign researchable list '%s'\n", name);
	do {
		token = cgi->Com_EParse(text, errhead, name);
		if (!*text || *token == '}')
			return;

		for (i = 0; i < ccs.numTechnologies; i++) {
			technology_t* tech = RS_GetTechByIDX(i);
			if (Q_streq(token, tech->id)) {
				if (researchable) {
					tech->mailSent = MAILSENT_PROPOSAL;
					RS_MarkOneResearchable(tech);
				} else {
					/** @todo Mark unresearchable */
				}
				cgi->Com_DPrintf(DEBUG_CLIENT, "...tech %s\n", tech->id);
				break;
			}
		}

		if (i == ccs.numTechnologies)
			cgi->Com_Printf("CP_ParseResearchableCampaignStates: unknown token \"%s\" ignored (tech %s)\n", token, name);

	} while (*text);
}

/* =========================================================== */

static const value_t salary_vals[] = {
	{"soldier_base", V_INT, offsetof(salary_t, base[EMPL_SOLDIER]), MEMBER_SIZEOF(salary_t, base[EMPL_SOLDIER])},
	{"soldier_rankbonus", V_INT, offsetof(salary_t, rankBonus[EMPL_SOLDIER]), MEMBER_SIZEOF(salary_t, rankBonus[EMPL_SOLDIER])},
	{"worker_base", V_INT, offsetof(salary_t, base[EMPL_WORKER]), MEMBER_SIZEOF(salary_t, base[EMPL_WORKER])},
	{"worker_rankbonus", V_INT, offsetof(salary_t, rankBonus[EMPL_WORKER]), MEMBER_SIZEOF(salary_t, rankBonus[EMPL_WORKER])},
	{"scientist_base", V_INT, offsetof(salary_t, base[EMPL_SCIENTIST]), MEMBER_SIZEOF(salary_t, base[EMPL_SCIENTIST])},
	{"scientist_rankbonus", V_INT, offsetof(salary_t, rankBonus[EMPL_SCIENTIST]), MEMBER_SIZEOF(salary_t, rankBonus[EMPL_SCIENTIST])},
	{"pilot_base", V_INT, offsetof(salary_t, base[EMPL_PILOT]), MEMBER_SIZEOF(salary_t, base[EMPL_PILOT])},
	{"pilot_rankbonus", V_INT, offsetof(salary_t, rankBonus[EMPL_PILOT]), MEMBER_SIZEOF(salary_t, rankBonus[EMPL_PILOT])},
	{"robot_base", V_INT, offsetof(salary_t, base[EMPL_ROBOT]), MEMBER_SIZEOF(salary_t, base[EMPL_ROBOT])},
	{"robot_rankbonus", V_INT, offsetof(salary_t, rankBonus[EMPL_ROBOT]), MEMBER_SIZEOF(salary_t, rankBonus[EMPL_ROBOT])},
	{"aircraft_factor", V_INT, offsetof(salary_t, aircraftFactor), MEMBER_SIZEOF(salary_t, aircraftFactor)},
	{"aircraft_divisor", V_INT, offsetof(salary_t, aircraftDivisor), MEMBER_SIZEOF(salary_t, aircraftDivisor)},
	{"base_upkeep", V_INT, offsetof(salary_t, baseUpkeep), MEMBER_SIZEOF(salary_t, baseUpkeep)},
	{"debt_interest", V_FLOAT, offsetof(salary_t, debtInterest), MEMBER_SIZEOF(salary_t, debtInterest)},
	{nullptr, V_NULL, 0, 0}
};

/**
 * @brief Parse the salaries from campaign definition
 * @param[in] name Name or ID of the found character skill and ability definition
 * @param[in] text The text of the nation node
 * @param[out] s Pointer to the campaign salaries data structure to parse into
 * @note Example:
 * <code>salary {
 *  soldier_base 3000
 * }</code>
 */
static void CP_ParseSalary (const char* name, const char** text, salary_t* s)
{
	cgi->Com_ParseBlock(name, text, s, salary_vals, cp_campaignPool);
}

/* =========================================================== */

static const value_t campaign_vals[] = {
	{"default", V_BOOL, offsetof(campaign_t, defaultCampaign), MEMBER_SIZEOF(campaign_t, defaultCampaign)},
	{"team", V_TEAM, offsetof(campaign_t, team), MEMBER_SIZEOF(campaign_t, team)},
	{"soldiers", V_INT, offsetof(campaign_t, soldiers), MEMBER_SIZEOF(campaign_t, soldiers)},
	{"workers", V_INT, offsetof(campaign_t, workers), MEMBER_SIZEOF(campaign_t, workers)},
	{"xvirate", V_INT, offsetof(campaign_t, maxAllowedXVIRateUntilLost), MEMBER_SIZEOF(campaign_t, maxAllowedXVIRateUntilLost)},
	{"maxdebts", V_INT, offsetof(campaign_t, negativeCreditsUntilLost), MEMBER_SIZEOF(campaign_t, negativeCreditsUntilLost)},
	{"minhappiness", V_FLOAT, offsetof(campaign_t, minhappiness), MEMBER_SIZEOF(campaign_t, minhappiness)},
	{"scientists", V_INT, offsetof(campaign_t, scientists), MEMBER_SIZEOF(campaign_t, scientists)},
	{"pilots", V_INT, offsetof(campaign_t, pilots), MEMBER_SIZEOF(campaign_t, pilots)},
	{"ugvs", V_INT, offsetof(campaign_t, ugvs), MEMBER_SIZEOF(campaign_t, ugvs)},
	{"equipment", V_STRING, offsetof(campaign_t, equipment), 0},
	{"soldierequipment", V_STRING, offsetof(campaign_t, soldierEquipment), 0},
	{"market", V_STRING, offsetof(campaign_t, market), 0},
	{"asymptotic_market", V_STRING, offsetof(campaign_t, asymptoticMarket), 0},
	{"researched", V_STRING, offsetof(campaign_t, researched), 0},
	{"difficulty", V_INT, offsetof(campaign_t, difficulty), MEMBER_SIZEOF(campaign_t, difficulty)},
	{"map", V_STRING, offsetof(campaign_t, map), 0},
	{"credits", V_INT, offsetof(campaign_t, credits), MEMBER_SIZEOF(campaign_t, credits)},
	{"visible", V_BOOL, offsetof(campaign_t, visible), MEMBER_SIZEOF(campaign_t, visible)},
	{"text", V_TRANSLATION_STRING, offsetof(campaign_t, text), 0}, /* just a gettext placeholder */
	{"name", V_TRANSLATION_STRING, offsetof(campaign_t, name), 0},
	{"basecost", V_INT, offsetof(campaign_t, basecost), MEMBER_SIZEOF(campaign_t, basecost)},
	{"firstbase", V_STRING, offsetof(campaign_t, firstBaseTemplate), 0},
	{"researchrate", V_FLOAT, offsetof(campaign_t, researchRate), MEMBER_SIZEOF(campaign_t, researchRate)},
	{"producerate", V_FLOAT, offsetof(campaign_t, produceRate), MEMBER_SIZEOF(campaign_t, produceRate)},
	{"healingrate", V_FLOAT, offsetof(campaign_t, healingRate), MEMBER_SIZEOF(campaign_t, healingRate)},
	{"liquidationrate", V_FLOAT, offsetof(campaign_t, liquidationRate), MEMBER_SIZEOF(campaign_t, liquidationRate)},
	{"componentrate", V_FLOAT, offsetof(campaign_t, componentRate), MEMBER_SIZEOF(campaign_t, componentRate)},
	{"minmissions", V_INT, offsetof(campaign_t, minMissions), MEMBER_SIZEOF(campaign_t, minMissions)},
	{"maxmissions", V_INT, offsetof(campaign_t, maxMissions), MEMBER_SIZEOF(campaign_t, maxMissions)},
	{"uforeductionrate", V_FLOAT, offsetof(campaign_t, ufoReductionRate), MEMBER_SIZEOF(campaign_t, ufoReductionRate)},
	{"initialinterest", V_INT, offsetof(campaign_t, initialInterest), MEMBER_SIZEOF(campaign_t, initialInterest)},
	{"employeerate", V_FLOAT, offsetof(campaign_t, employeeRate), MEMBER_SIZEOF(campaign_t, employeeRate)},
	{"alienbaseinterest", V_INT, offsetof(campaign_t, alienBaseInterest), MEMBER_SIZEOF(campaign_t, alienBaseInterest)},
	{nullptr, V_NULL, 0, 0}
};

/**
 * @sa CL_ParseClientData
 */
static void CP_ParseCampaign (const char* name, const char** text)
{
	const char* errhead = "CP_ParseCampaign: unexpected end of file (campaign ";
	campaign_t* cp;
	const char* token;
	int i;
	salary_t* s;
	bool drop = false;

	/* search for campaigns with same name */
	if (CP_GetCampaign(name) != nullptr) {
		cgi->Com_Printf("CP_ParseCampaign: campaign def \"%s\" with same name found, second ignored\n", name);
		return;
	}

	if (ccs.numCampaigns >= MAX_CAMPAIGNS) {
		cgi->Com_Printf("CP_ParseCampaign: Max campaigns reached (%i)\n", MAX_CAMPAIGNS);
		return;
	}

	/* initialize the campaign */
	cp = &ccs.campaigns[ccs.numCampaigns++];
	OBJZERO(*cp);
	cp->idx = ccs.numCampaigns - 1;
	Q_strncpyz(cp->id, name, sizeof(cp->id));
	cp->team = TEAM_PHALANX;
	Q_strncpyz(cp->researched, "researched_human", sizeof(cp->researched));
	cp->researchRate = 0.8f;
	cp->produceRate = 1.0f;
	cp->healingRate = 1.0f;
	cp->liquidationRate = 0.0f;
	cp->componentRate = 1.0f;
	cp->maxMissions = 17;
	cp->minMissions = 5;
	cp->ufoReductionRate = NON_OCCURRENCE_PROBABILITY;
	cp->initialInterest = INITIAL_OVERALL_INTEREST;
	cp->employeeRate = 1.0f;
	cp->alienBaseInterest = 200;

	/* get it's body */
	token = Com_Parse(text);

	if (!*text || *token != '{') {
		cgi->Com_Printf("CP_ParseCampaign: campaign def \"%s\" without body ignored\n", name);
		ccs.numCampaigns--;
		return;
	}

	/* set undefined markers */
	s = &cp->salaries;
	for (i = 0; i < MAX_EMPL; i++) {
		s->base[i] = -1;
		s->rankBonus[i] = -1;
	}
	s->aircraftFactor = -1;
	s->aircraftDivisor = -1;
	s->baseUpkeep = -1;
	s->debtInterest = -1;

	do {
		token = cgi->Com_EParse(text, errhead, name);
		if (!*text)
			break;
		if (*token == '}')
			break;

		/* check for some standard values */
		if (cgi->Com_ParseBlockToken(name, text, cp, campaign_vals, nullptr, token)) {
			continue;
		} else if (Q_streq(token, "salary")) {
			CP_ParseSalary(token, text, s);
		} else if (Q_streq(token, "events")) {
			token = cgi->Com_EParse(text, errhead, name);
			if (!*text)
				return;
			cp->events = CP_GetEventsByID(token);
		} else if (Q_streq(token, "aircraft")) {
			cgi->Com_ParseList(text, &cp->initialCraft);
		} else if (Q_streq(token, "date")) {
			token = cgi->Com_EParse(text, errhead, name);
			if (!*text)
				return;
			int year;
			int day;
			int hour;
			if (sscanf(token, "%i %i %i", &year, &day, &hour) != 3) {
				Com_Error(ERR_DROP, "Illegal campaign start date for campaign %s", cp->id);
			}
			cp->date = DateTime(DateTime::DAYS_PER_YEAR * year + day, DateTime::SECONDS_PER_HOUR * hour);
		} else {
			cgi->Com_Printf("CP_ParseCampaign: unknown token \"%s\" ignored (campaign %s)\n", token, name);
			cgi->Com_EParse(text, errhead, name);
		}
	} while (*text);

	if (cp->difficulty < -4)
		cp->difficulty = -4;
	else if (cp->difficulty > 4)
		cp->difficulty = 4;

	/* checking for undefined values */
	for (i = 0; i < MAX_EMPL; i++) {
		if (s->base[i] == -1 || s->rankBonus[i] == -1) {
			drop = true;
			break;
		}
	}
	if (drop || s->aircraftFactor == -1 || s->aircraftDivisor == -1 || s->baseUpkeep == -1
	 || s->debtInterest == -1) {
		cgi->Com_Printf("CP_ParseCampaign: check salary definition. Campaign def \"%s\" ignored\n", name);
		ccs.numCampaigns--;
		return;
	}
}

/**
 * @brief Parsing campaign data
 *
 * first stage parses all the main data into their struct
 * see CP_ParseScriptSecond for more details about parsing stages
 * @sa CP_ParseCampaignData
 * @sa CP_ParseScriptSecond
 */
static void CP_ParseScriptFirst (const char* type, const char* name, const char** text)
{
	/* check for client interpretable scripts */
	if (Q_streq(type, "up_chapter"))
		UP_ParseChapter(name, text);
	else if (Q_streq(type, "building"))
		B_ParseBuildings(name, text, false);
	else if (Q_streq(type, "installation"))
		INS_ParseInstallations(name, text);
	else if (Q_streq(type, "tech"))
		RS_ParseTechnologies(name, text);
	else if (Q_streq(type, "nation"))
		CL_ParseNations(name, text);
	else if (Q_streq(type, "city"))
		CITY_Parse(name, text);
	else if (Q_streq(type, "rank"))
		CL_ParseRanks(name, text);
	else if (Q_streq(type, "aircraft"))
		AIR_ParseAircraft(name, text, false);
	else if (Q_streq(type, "mail"))
		CL_ParseEventMails(name, text);
	else if (Q_streq(type, "events"))
		CL_ParseCampaignEvents(name, text);
	else if (Q_streq(type, "event"))
		CP_ParseEventTrigger(name, text);
	else if (Q_streq(type, "components"))
		COMP_ParseComponents(name, text);
	else if (Q_streq(type, "alienteam"))
		CP_ParseAlienTeam(name, text);
	else if (Q_streq(type, "msgoptions"))
		MSO_ParseMessageSettings(name, text);
}

/**
 * @brief Parsing only for singleplayer
 *
 * parsed if we are no dedicated server
 * second stage links all the parsed data from first stage
 * example: we need a techpointer in a building - in the second stage the buildings and the
 * techs are already parsed - so now we can link them
 * @sa CP_ParseCampaignData
 * @sa Com_ParseScripts
 * @sa CL_ParseScriptFirst
 */
static void CP_ParseScriptSecond (const char* type, const char* name, const char** text)
{
	/* check for client interpretable scripts */
	if (Q_streq(type, "building"))
		B_ParseBuildings(name, text, true);
	else if (Q_streq(type, "aircraft"))
		AIR_ParseAircraft(name, text, true);
	else if (Q_streq(type, "basetemplate"))
		B_ParseBaseTemplate(name, text);
	else if (Q_streq(type, "campaign"))
		CP_ParseCampaign(name, text);
}

/**
 * @brief Parses the campaign specific data - this data can only be parsed once the campaign started
 */
static void CP_ParseScriptCampaignRelated (const campaign_t* campaign, const char* type, const char* name, const char** text)
{
	if (Q_streq(type, "researched"))
		CP_ParseResearchedCampaignItems(campaign, name, text);
	else if (Q_streq(type, "researchable"))
		CP_ParseResearchableCampaignStates(campaign, name, text, true);
	else if (Q_streq(type, "notresearchable"))
		CP_ParseResearchableCampaignStates(campaign, name, text, false);
}

/**
 * @brief Make sure values of items after parsing are proper.
 */
static bool CP_ItemsSanityCheck (void)
{
	bool result = true;

	for (int i = 0; i < cgi->csi->numODs; i++) {
		const objDef_t* item = INVSH_GetItemByIDX(i);

		/* Warn if item has no size set. */
		if (item->size <= 0 && B_ItemIsStoredInBaseStorage(item)) {
			result = false;
			cgi->Com_Printf("CP_ItemsSanityCheck: Item %s has zero size set.\n", item->id);
		}

		/* Warn if no price is set. */
		if (item->price <= 0 && BS_IsOnMarket(item)) {
			result = false;
			cgi->Com_Printf("CP_ItemsSanityCheck: Item %s has zero price set.\n", item->id);
		}

		if (item->price > 0 && !BS_IsOnMarket(item) && !PR_ItemIsProduceable(item)) {
			result = false;
			cgi->Com_Printf("CP_ItemsSanityCheck: Item %s has a price set though it is neither available on the market and production.\n", item->id);
		}
	}

	return result;
}

/** @brief struct that holds the sanity check data */
typedef struct {
	bool (*check)(void);	/**< function pointer to check function */
	const char* name;			/**< name of the subsystem to check */
} sanity_functions_t;

/** @brief Data for sanity check of parsed script data */
static const sanity_functions_t sanity_functions[] = {
	{B_BuildingScriptSanityCheck, "buildings"},
	{RS_ScriptSanityCheck, "tech"},
	{AIR_ScriptSanityCheck, "aircraft"},
	{CP_ItemsSanityCheck, "items"},
	{NAT_ScriptSanityCheck, "nations"},

	{nullptr, nullptr}
};

/**
 * @brief Check the parsed script values for errors after parsing every script file
 * @sa CP_ParseCampaignData
 */
void CP_ScriptSanityCheck (void)
{
	const sanity_functions_t* s;

	cgi->Com_Printf("Sanity check for script data\n");
	s = sanity_functions;
	while (s->check) {
		bool status = s->check();
		cgi->Com_Printf("...%s %s\n", s->name, (status ? "ok" : "failed"));
		s++;
	}
}

/**
 * @brief Read the data for campaigns
 * @sa SAV_GameLoad
 * @sa CP_ResetCampaignData
 */
void CP_ParseCampaignData (void)
{
	const char* type, *name, *text;
	int i;
	campaign_t* campaign;

	/* pre-stage parsing */
	cgi->FS_BuildFileList("ufos/*.ufo");
	cgi->FS_NextScriptHeader(nullptr, nullptr, nullptr);
	text = nullptr;

	while ((type = cgi->FS_NextScriptHeader("ufos/*.ufo", &name, &text)) != nullptr)
		CP_ParseScriptFirst(type, name, &text);

	/* fill in IDXs for required research techs */
	RS_RequiredLinksAssign();

	/* stage two parsing */
	cgi->FS_NextScriptHeader(nullptr, nullptr, nullptr);
	text = nullptr;

	cgi->Com_DPrintf(DEBUG_CLIENT, "Second stage parsing started...\n");
	while ((type = cgi->FS_NextScriptHeader("ufos/*.ufo", &name, &text)) != nullptr)
		CP_ParseScriptSecond(type, name, &text);
	INS_LinkTechnologies();

	for (i = 0; i < cgi->csi->numTeamDefs; i++) {
		const teamDef_t* teamDef = &cgi->csi->teamDef[i];
		if (!CHRSH_IsTeamDefAlien(teamDef))
			continue;

		ccs.teamDefTechs[teamDef->idx] = RS_GetTechByID(teamDef->tech);
		if (ccs.teamDefTechs[teamDef->idx] == nullptr)
			cgi->Com_Error(ERR_DROP, "Could not find a tech for teamdef %s", teamDef->id);
	}

	for (i = 0, campaign = ccs.campaigns; i < ccs.numCampaigns; i++, campaign++) {
		/* find the relevant markets */
		campaign->marketDef = cgi->INV_GetEquipmentDefinitionByID(campaign->market);
		campaign->asymptoticMarketDef = cgi->INV_GetEquipmentDefinitionByID(campaign->asymptoticMarket);
	}

	cgi->Com_Printf("Campaign data loaded - size " UFO_SIZE_T " bytes\n", sizeof(ccs));
	cgi->Com_Printf("...techs: %i\n", ccs.numTechnologies);
	cgi->Com_Printf("...buildings: %i\n", ccs.numBuildingTemplates);
	cgi->Com_Printf("...ranks: %i\n", ccs.numRanks);
	cgi->Com_Printf("...nations: %i\n", ccs.numNations);
	cgi->Com_Printf("...cities: %i\n", ccs.numCities);
	cgi->Com_Printf("\n");
}

void CP_ReadCampaignData (const campaign_t* campaign)
{
	const char* type, *name, *text;

	/* stage two parsing */
	cgi->FS_NextScriptHeader(nullptr, nullptr, nullptr);
	text = nullptr;

	cgi->Com_DPrintf(DEBUG_CLIENT, "Second stage parsing started...\n");
	while ((type = cgi->FS_NextScriptHeader("ufos/*.ufo", &name, &text)) != nullptr)
		CP_ParseScriptCampaignRelated(campaign, type, name, &text);

	ccs.date = campaign->date;
}
