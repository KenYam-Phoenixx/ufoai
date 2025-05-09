/**
 * @file
 * @brief Base building related stuff.
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
#include "cp_building.h"
#include "../../cl_shared.h"
#include "../../../shared/parse.h"
#include "cp_campaign.h"
#include "cp_time.h"

/**
 * @brief Returns if a building is fully buildt up
 * @param[in] building Pointer to the building to check
 * @note it always return @c true for buildings with {0, 0} timeStart
 */
bool B_IsBuildingBuiltUp (const building_t* building)
{
	if (!building)
		return false;
	if (building->timeStart == DateTime(0, 0))
		return true;
	return (building->timeStart + DateTime(building->buildTime, 0)) <= ccs.date;
}

/**
 * @brief Returns the time remaining time of a building construction
 * @param[in] building Pointer to the building to check
 */
float B_GetConstructionTimeRemain (const building_t* building)
{
	DateTime diff = building->timeStart + DateTime(building->buildTime, 0) - ccs.date;
	return diff.getDateAsDays() + (float)diff.getTimeAsSeconds() / DateTime::SECONDS_PER_DAY;
}

static const struct buildingTypeMapping_s {
	const char* id;
	buildingType_t type;
} buildingTypeMapping[] = {
	{ "lab", B_LAB },
	{ "hospital", B_HOSPITAL },
	{ "aliencont", B_ALIEN_CONTAINMENT },
	{ "workshop",B_WORKSHOP },
	{ "storage", B_STORAGE },
	{ "hangar", B_HANGAR },
	{ "smallhangar",B_SMALL_HANGAR },
	{ "quarters", B_QUARTERS },
	{ "power", B_POWER },
	{ "command", B_COMMAND },
	{ "amstorage", B_ANTIMATTER },
	{ "entrance", B_ENTRANCE },
	{ "missile", B_DEFENCE_MISSILE },
	{ "laser", B_DEFENCE_LASER },
	{ "radar", B_RADAR },
	{ NULL, MAX_BUILDING_TYPE }
};

/**
 * @brief Returns the building type for a given building identified by its building id
 * from the ufo script files
 * @sa B_ParseBuildings
 * @param[in] buildingID The script building id that should get converted into the enum value
 */
buildingType_t B_GetBuildingTypeByBuildingID (const char* buildingID)
{
	for (const struct buildingTypeMapping_s* v = buildingTypeMapping; v->id; v++)
		if (Q_streq(buildingID, v->id))
			return v->type;
	return MAX_BUILDING_TYPE;
}

/**
 * @brief Holds the names of valid entries in the basemanagement.ufo file.
 *
 * The valid definition names for BUILDINGS (building_t) in the basemanagement.ufo file.
 * to the appropriate values in the corresponding struct
 */
static const value_t valid_building_vars[] = {
	{"map_name", V_HUNK_STRING, offsetof(building_t, mapPart), 0},	/**< Name of the map file for generating basemap. */
	{"max_count", V_INT, offsetof(building_t, maxCount), MEMBER_SIZEOF(building_t, maxCount)},	/**< How many building of the same type allowed? */
	{"level", V_FLOAT, offsetof(building_t, level), MEMBER_SIZEOF(building_t, level)},	/**< building level */
	{"name", V_TRANSLATION_STRING, offsetof(building_t, name), 0},	/**< The displayed building name. */
	{"tech", V_HUNK_STRING, offsetof(building_t, pedia), 0},	/**< The pedia-id string for the associated pedia entry. */
	{"status", V_INT, offsetof(building_t, buildingStatus), MEMBER_SIZEOF(building_t, buildingStatus)},	/**< The current status of the building. */
	{"image", V_HUNK_STRING, offsetof(building_t, image), 0},	/**< Identifies the image for the building. */
	{"size", V_POS, offsetof(building_t, size), MEMBER_SIZEOF(building_t, size)},	/**< Building size. */
	{"fixcosts", V_INT, offsetof(building_t, fixCosts), MEMBER_SIZEOF(building_t, fixCosts)},	/**< Cost to build. */
	{"varcosts", V_INT, offsetof(building_t, varCosts), MEMBER_SIZEOF(building_t, varCosts)},	/**< Costs that will come up by using the building. */
	{"build_time", V_INT, offsetof(building_t, buildTime), MEMBER_SIZEOF(building_t, buildTime)},	/**< How many days it takes to construct the building. */
	{"starting_employees", V_INT, offsetof(building_t, maxEmployees), MEMBER_SIZEOF(building_t, maxEmployees)},	/**< How many employees to hire on construction in the first base. */
	{"capacity", V_INT, offsetof(building_t, capacity), MEMBER_SIZEOF(building_t, capacity)},	/**< A size value that is used by many buildings in a different way. */

	/*event handler functions */
	{"onconstruct", V_HUNK_STRING, offsetof(building_t, onConstruct), 0},	/**< Event handler. */
	{"ondestroy", V_HUNK_STRING, offsetof(building_t, onDestroy), 0},		/**< Event handler. */
	{"onenable", V_HUNK_STRING, offsetof(building_t, onEnable), 0},			/**< Event handler. */
	{"ondisable", V_HUNK_STRING, offsetof(building_t, onDisable), 0},		/**< Event handler. */
	{"mandatory", V_BOOL, offsetof(building_t, mandatory), MEMBER_SIZEOF(building_t, mandatory)}, /**< Automatically construct this building when a base is set up. Must also set the pos-flag. */
	{nullptr, V_NULL, 0, 0}
};

/**
 * @brief Copies an entry from the building description file into the list of building types.
 * @note Parses one "building" entry in the basemanagement.ufo file and writes
 * it into the next free entry in bmBuildings[0], which is the list of buildings
 * in the first base (building_t).
 * @param[in] name Unique script id of a building. This is parsed from "building xxx" -> id=xxx.
 * @param[in] text the whole following text that is part of the "building" item definition in .ufo.
 * @param[in] link Bool value that decides whether to link the tech pointer in or not
 * @sa CL_ParseScriptFirst (link is false here)
 * @sa CL_ParseScriptSecond (link it true here)
 */
void B_ParseBuildings (const char* name, const char** text, bool link)
{
	building_t* building;
	technology_t* techLink;
	const char* errhead = "B_ParseBuildings: unexpected end of file (names ";
	const char* token;

	/* get id list body */
	token = Com_Parse(text);
	if (!*text || *token != '{') {
		cgi->Com_Printf("B_ParseBuildings: building \"%s\" without body ignored\n", name);
		return;
	}

	if (ccs.numBuildingTemplates >= MAX_BUILDINGS)
		cgi->Com_Error(ERR_DROP, "B_ParseBuildings: too many buildings");

	if (!link) {
		for (int i = 0; i < ccs.numBuildingTemplates; i++) {
			if (Q_streq(ccs.buildingTemplates[i].id, name)) {
				cgi->Com_Printf("B_ParseBuildings: Second building with same name found (%s) - second ignored\n", name);
				return;
			}
		}

		/* new entry */
		building = &ccs.buildingTemplates[ccs.numBuildingTemplates];
		OBJZERO(*building);
		building->id = cgi->PoolStrDup(name, cp_campaignPool, 0);

		cgi->Com_DPrintf(DEBUG_CLIENT, "...found building %s\n", building->id);

		/* set standard values */
		building->tpl = building;	/* Self-link just in case ... this way we can check if it is a template or not. */
		building->idx = -1;			/* No entry in buildings list (yet). */
		building->base = nullptr;
		building->buildingType = MAX_BUILDING_TYPE;
		building->dependsBuilding = nullptr;
		building->maxCount = -1;	/* Default: no limit */
		building->size[0] = 1;
		building->size[1] = 1;

		ccs.numBuildingTemplates++;
		do {
			/* get the name type */
			token = cgi->Com_EParse(text, errhead, name);
			if (!*text)
				break;
			if (*token == '}')
				break;

			/* get values */
			if (Q_streq(token, "type")) {
				token = cgi->Com_EParse(text, errhead, name);
				if (!*text)
					return;

				building->buildingType = B_GetBuildingTypeByBuildingID(token);
				if (building->buildingType >= MAX_BUILDING_TYPE)
					cgi->Com_Printf("didn't find buildingType '%s'\n", token);
			} else {
				/* no linking yet */
				if (Q_streq(token, "depends")) {
					cgi->Com_EParse(text, errhead, name);
					if (!*text)
						return;
				} else {
					if (!cgi->Com_ParseBlockToken(name, text, building, valid_building_vars, cp_campaignPool, token))
						cgi->Com_Printf("B_ParseBuildings: unknown token \"%s\" ignored (building %s)\n", token, name);
				}
			}
		} while (*text);
		if (building->size[0] < 1 || building->size[1] < 1 || building->size[0] >= BASE_SIZE || building->size[1] >= BASE_SIZE) {
			cgi->Com_Printf("B_ParseBuildings: Invalid size for building %s (%i, %i)\n", building->id, (int)building->size[0], (int)building->size[1]);
			ccs.numBuildingTemplates--;
		}
	} else {
		building = B_GetBuildingTemplate(name);
		if (!building)
			cgi->Com_Error(ERR_DROP, "B_ParseBuildings: Could not find building with id %s\n", name);

		techLink = RS_GetTechByProvided(name);
		if (techLink)
			building->tech = techLink;

		do {
			/* get the name type */
			token = cgi->Com_EParse(text, errhead, name);
			if (!*text)
				break;
			if (*token == '}')
				break;
			/* get values */
			if (Q_streq(token, "depends")) {
				const building_t* dependsBuilding = B_GetBuildingTemplate(cgi->Com_EParse(text, errhead, name));
				if (!dependsBuilding)
					cgi->Com_Error(ERR_DROP, "Could not find building depend of %s\n", building->id);
				building->dependsBuilding = dependsBuilding;
				if (!*text)
					return;
			}
		} while (*text);
	}
}

/**
 * @brief Checks the parsed buildings for errors
 * @return false if there are errors - true otherwise
 */
bool B_BuildingScriptSanityCheck (void)
{
	int i, error = 0;
	building_t* b;

	for (i = 0, b = ccs.buildingTemplates; i < ccs.numBuildingTemplates; i++, b++) {
		if (!b->name) {
			error++;
			cgi->Com_Printf("...... no name for building '%s' given\n", b->id);
		}
		if (!b->image) {
			error++;
			cgi->Com_Printf("...... no image for building '%s' given\n", b->id);
		}
		if (!b->pedia) {
			error++;
			cgi->Com_Printf("...... no pedia link for building '%s' given\n", b->id);
		} else if (!RS_GetTechByID(b->pedia)) {
			error++;
			cgi->Com_Printf("...... could not get pedia entry tech (%s) for building '%s'\n", b->pedia, b->id);
		}
	}

	return !error;
}

/**
 * @brief Returns the building in the global building-types list that has the unique name buildingID.
 * @param[in] buildingName The unique id of the building (building_t->id).
 * @return Building template pointer if found, nullptr otherwise
 * @todo make the returned pointer const
 */
building_t* B_GetBuildingTemplateSilent (const char* buildingName)
{
	if (!buildingName)
		return nullptr;
	for (int i = 0; i < ccs.numBuildingTemplates; i++) {
		building_t* buildingTemplate = &ccs.buildingTemplates[i];
		if (Q_streq(buildingTemplate->id, buildingName))
			return buildingTemplate;
	}
	return nullptr;
}

/**
 * @brief Returns the building in the global building-types list that has the unique name buildingID.
 * @param[in] buildingName The unique id of the building (building_t->id).
 * @return Building template pointer if found, nullptr otherwise
 * @todo make the returned pointer const
 */
building_t* B_GetBuildingTemplate (const char* buildingName)
{
	if (!buildingName || buildingName[0] == '\0') {
		cgi->Com_Printf("No, or empty building ID\n");
		return nullptr;
	}

	building_t* buildingTemplate = B_GetBuildingTemplateSilent(buildingName);
	if (!buildingTemplate)
		cgi->Com_Printf("Building %s not found\n", buildingName);
	return buildingTemplate;
}

/**
 * @brief Returns the building template in the global building-types list for a buildingType.
 * @param[in] type Building type.
 */
const building_t* B_GetBuildingTemplateByType(buildingType_t type)
{
	for (int i = 0; i < ccs.numBuildingTemplates; i++) {
		const building_t* buildingTemplate = &ccs.buildingTemplates[i];
		if (buildingTemplate->buildingType == type)
			return buildingTemplate;
	}
	return nullptr;
}

/**
 * @brief Check that the dependences of a building is operationnal
 * @param[in] building Pointer to the building to check
 * @return true if base contains needed dependence for entering building
 */
bool B_CheckBuildingDependencesStatus (const building_t* building)
{
	assert(building);

	if (!building->dependsBuilding)
		return true;

	/* Make sure the dependsBuilding pointer is really a template .. just in case. */
	assert(building->dependsBuilding == building->dependsBuilding->tpl);

	return B_GetBuildingStatus(building->base, building->dependsBuilding->buildingType);
}

/**
 * @brief Run eventhandler script for a building
 * @param[in] buildingTemplate Building type (template) to run event for
 * @param[in] base The base to run it at
 * @param[in] eventType Type of the event to run
 * @return @c true if an event was fired @c false otherwise (the building may not have one)
 */
bool B_FireEvent (const building_t* buildingTemplate, const base_t* base, buildingEvent_t eventType)
{
	const char* command = nullptr;

	assert(buildingTemplate);
	assert(base);

	switch (eventType) {
		case B_ONCONSTRUCT:
			command = buildingTemplate->onConstruct;
			break;
		case B_ONENABLE:
			command = buildingTemplate->onEnable;
			break;
		case B_ONDISABLE:
			command = buildingTemplate->onDisable;
			break;
		case B_ONDESTROY:
			command = buildingTemplate->onDestroy;
			break;
		default:
			cgi->Com_Error(ERR_DROP, "B_FireEvent: Invalid Event\n");
	}

	if (Q_strvalid(command)) {
		cgi->Cmd_ExecuteString("%s %i %i", command, base->idx, buildingTemplate->buildingType);
		return true;
	}

	return false;
}
