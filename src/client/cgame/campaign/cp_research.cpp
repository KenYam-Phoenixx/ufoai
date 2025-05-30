/**
 * @file
 * @brief Technology research.
 *
 * Handles everything related to the research-tree.
 * Provides information if items/buildings/etc.. can be researched/used/displayed etc...
 * Implements the research-system (research new items/etc...)
 * See base/ufos/research.ufo and base/ufos/ui/research.ufo for the underlying content.
 * @todo comment on used global variables.
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
#include "cp_capacity.h"
#include "cp_research.h"
#include "cp_popup.h"
#include "cp_time.h"
#include "save/save_research.h"
#include "aliencontainment.h"

#define TECH_HASH_SIZE 64
static technology_t* techHash[TECH_HASH_SIZE];
static technology_t* techHashProvided[TECH_HASH_SIZE];

static linkedList_t* redirectedTechs;

/**
 * @brief Sets a technology status to researched and updates the date.
 * @param[in] tech The technology that was researched.
 */
void RS_ResearchFinish (technology_t* tech)
{
	/* Remove all scientists from the technology. */
	RS_StopResearch(tech);

	/** At this point we define what research-report description is used when displayed. (i.e. "usedDescription" is set here).
	 * That's because this is the first the player will see the research result
	 * and any later changes may make the content inconsistent for the player.
	 * @sa RS_MarkOneResearchable */
	RS_GetDescription(&tech->description);
	if (tech->preDescription.usedDescription < 0) {
		/* For some reason the research proposal description was not set at this point - we just make sure it _is_ set. */
		RS_GetDescription(&tech->preDescription);
	}

	/* execute the trigger only if the tech is not yet researched */
	if (tech->finishedResearchEvent && tech->statusResearch != RS_FINISH)
		cgi->Cmd_ExecuteString("%s", tech->finishedResearchEvent);

	tech->statusResearch = RS_FINISH;
	tech->researchedDate = DateTime(ccs.date);
	if (!tech->statusResearchable) {
		tech->statusResearchable = true;
		tech->preResearchedDate = DateTime(ccs.date);
	}

	/* send a new message and add it to the mailclient */
	if (tech->mailSent < MAILSENT_FINISHED && tech->type != RS_LOGIC) {
		Com_sprintf(cp_messageBuffer, sizeof(cp_messageBuffer), _("A research project has been completed: %s\n"), _(tech->name));
		MSO_CheckAddNewMessage(NT_RESEARCH_COMPLETED, _("Research finished"), cp_messageBuffer, MSG_RESEARCH_FINISHED, tech);
		tech->mailSent = MAILSENT_FINISHED;

		if (tech->announce) {
			UP_OpenWith(tech->id);
		}
	}
}

/**
 * @brief Stops a research (Removes scientists from it)
 * @param[in] tech The technology that is being researched.
 */
void RS_StopResearch (technology_t* tech)
{
	assert(tech);
	while (tech->scientists > 0)
		RS_RemoveScientist(tech, nullptr);
}

/**
 * @brief Marks one tech as researchable.
 * @param tech The technology to be marked.
 * @sa RS_MarkCollected
 * @sa RS_MarkResearchable
 */
void RS_MarkOneResearchable (technology_t* tech)
{
	if (!tech)
		return;

	cgi->Com_DPrintf(DEBUG_CLIENT, "RS_MarkOneResearchable: \"%s\" marked as researchable.\n", tech->id);

	/* Don't do anything for not researchable techs. */
	if (tech->time == -1)
		return;

	/* Don't send mail for automatically completed techs. */
	if (tech->time == 0)
		tech->mailSent = MAILSENT_FINISHED;

	/** At this point we define what research proposal description is used when displayed. (i.e. "usedDescription" is set here).
	 * That's because this is the first the player will see anything from
	 * the tech and any later changes may make the content (proposal) of the tech inconsistent for the player.
	 * @sa RS_ResearchFinish */
	RS_GetDescription(&tech->preDescription);
	/* tech->description is checked before a research is finished */

	if (tech->mailSent < MAILSENT_PROPOSAL) {
		Com_sprintf(cp_messageBuffer, sizeof(cp_messageBuffer), _("New research proposal: %s\n"), _(tech->name));
		MSO_CheckAddNewMessage(NT_RESEARCH_PROPOSED, _("Unknown Technology researchable"), cp_messageBuffer, MSG_RESEARCH_PROPOSAL, tech);
		tech->mailSent = MAILSENT_PROPOSAL;
	}

	tech->statusResearchable = true;

	/* only change the date if it wasn't set before */
	if (tech->preResearchedDate.getDateAsDays() == 0) {
		tech->preResearchedDate = DateTime(ccs.date);
	}
}

/**
 * @brief Checks if all requirements of a tech have been met so that it becomes researchable.
 * @note If there are NO requirements defined at all it will always return true.
 * @param[in] tech The technology we want to research
 * @param[in] base In what base to check the "collected" items etc..
 * @return @c true if all requirements are satisfied otherwise @c false.
 * @todo Add support for the "delay" value.
 */
bool RS_RequirementsMet (const technology_t* tech, const base_t* base)
{
	int i;
	bool metAND = false;
	bool metOR = false;
	const requirements_t* requiredAND = &tech->requireAND;	/* a list of AND-related requirements */
	const requirements_t* requiredOR = &tech->requireOR;	/* a list of OR-related requirements */

	if (!requiredAND && !requiredOR) {
		cgi->Com_Printf("RS_RequirementsMet: No requirement list(s) given as parameter.\n");
		return false;
	}

	/* If there are no requirements defined at all we have 'met' them by default. */
	if (requiredAND->numLinks == 0 && requiredOR->numLinks == 0) {
		cgi->Com_DPrintf(DEBUG_CLIENT, "RS_RequirementsMet: No requirements set for this tech. They are 'met'.\n");
		return true;
	}

	if (requiredAND->numLinks) {
		metAND = true;
		for (i = 0; i < requiredAND->numLinks; i++) {
			const requirement_t* req = &requiredAND->links[i];
			switch (req->type) {
			case RS_LINK_TECH:
				/* if a tech that links itself is already marked researchable, we can research it */
				if (!(Q_streq(req->id, tech->id) && tech->statusResearchable) && !RS_IsResearched_ptr(req->link.tech))
					metAND = false;
				break;
			case RS_LINK_TECH_NOT:
				if (RS_IsResearched_ptr(req->link.tech))
					metAND = false;
				break;
			case RS_LINK_ITEM:
				/* The same code is used in "PR_RequirementsMet" */
				if (!base || B_ItemInBase(req->link.od, base) < req->amount)
					metAND = false;
				break;
			case RS_LINK_ALIEN_DEAD:
				if (!base || !base->alienContainment || base->alienContainment->getDead(req->link.td) < req->amount)
					metAND = false;
				break;
			case RS_LINK_ALIEN:
				if (!base || !base->alienContainment || base->alienContainment->getAlive(req->link.td) < req->amount)
					metAND = false;
				break;
			case RS_LINK_ALIEN_GLOBAL:
				if (AL_CountAll() < req->amount)
					metAND = false;
				break;
			case RS_LINK_UFO:
				if (US_UFOsInStorage(req->link.aircraft, nullptr) < req->amount)
					metAND = false;
				break;
			case RS_LINK_ANTIMATTER:
				if (!base || B_AntimatterInBase(base) < req->amount)
					metAND = false;
				break;
			default:
				break;
			}

			if (!metAND)
				break;
		}
	}

	if (requiredOR->numLinks)
		for (i = 0; i < requiredOR->numLinks; i++) {
			const requirement_t* req = &requiredOR->links[i];
			switch (req->type) {
			case RS_LINK_TECH:
				if (RS_IsResearched_ptr(req->link.tech))
					metOR = true;
				break;
			case RS_LINK_TECH_NOT:
				if (!RS_IsResearched_ptr(req->link.tech))
					metOR = true;
				break;
			case RS_LINK_ITEM:
				/* The same code is used in "PR_RequirementsMet" */
				if (base && B_ItemInBase(req->link.od, base) >= req->amount)
					metOR = true;
				break;
			case RS_LINK_ALIEN:
				if (base && base->alienContainment && base->alienContainment->getAlive(req->link.td) >= req->amount)
					metOR = true;
				break;
			case RS_LINK_ALIEN_DEAD:
				if (base && base->alienContainment && base->alienContainment->getDead(req->link.td) >= req->amount)
					metOR = true;
				break;
			case RS_LINK_ALIEN_GLOBAL:
				if (AL_CountAll() >= req->amount)
					metOR = true;
				break;
			case RS_LINK_UFO:
				if (US_UFOsInStorage(req->link.aircraft, nullptr) >= req->amount)
					metOR = true;
				break;
			case RS_LINK_ANTIMATTER:
				if (base && B_AntimatterInBase(base) >= req->amount)
					metOR = true;
				break;
			default:
				break;
			}

			if (metOR)
				break;
		}
	cgi->Com_DPrintf(DEBUG_CLIENT, "met_AND is %i, met_OR is %i\n", metAND, metOR);

	return (metAND || metOR);
}

/**
 * @brief returns the currently used description for a technology.
 * @param[in,out] desc A list of possible descriptions (with tech-links that decide which one is the correct one)
 */
const char* RS_GetDescription (technologyDescriptions_t* desc)
{
	/* Return (unparsed) default description (0) if nothing is defined.
	 * it is _always_ set, even if numDescriptions is zero. See RS_ParseTechnologies (standard values). */
	if (desc->numDescriptions == 0)
		return desc->text[0];

	/* Return already used description if it's defined. */
	if (desc->usedDescription >= 0)
		return desc->text[desc->usedDescription];

	/* Search for useable description text (first match is returned => order is important)
	 * The default (0) entry is skipped here. */
	for (int i = 1; i < desc->numDescriptions; i++) {
		const technology_t* tech = RS_GetTechByID(desc->tech[i]);
		if (!tech)
			continue;

		if (RS_IsResearched_ptr(tech)) {
			desc->usedDescription = i;	/**< Stored used description */
			return desc->text[i];
		}
	}

	return desc->text[0];
}

/**
 * @brief Marks a give technology as collected
 * @sa CP_AddItemAsCollected_f
 * @sa MS_AddNewMessage
 * @sa RS_MarkOneResearchable
 * @param[in] tech The technology pointer to mark collected
 */
void RS_MarkCollected (technology_t* tech)
{
	assert(tech);

	if (tech->time == 0) /* Don't send mail for automatically completed techs. */
		tech->mailSent = MAILSENT_FINISHED;

	if (tech->mailSent < MAILSENT_PROPOSAL) {
		if (tech->statusResearch < RS_FINISH) {
			Com_sprintf(cp_messageBuffer, sizeof(cp_messageBuffer), _("New research proposal: %s\n"), _(tech->name));
			MSO_CheckAddNewMessage(NT_RESEARCH_PROPOSED, _("Unknown Technology found"), cp_messageBuffer, MSG_RESEARCH_PROPOSAL, tech);
		}
		tech->mailSent = MAILSENT_PROPOSAL;
	}

	/* only change the date if it wasn't set before */
	if (tech->preResearchedDate.getDateAsDays() == 0) {
		tech->preResearchedDate = DateTime(ccs.date);
	}

	tech->statusCollected = true;
}

/**
 * @brief Marks all the techs that can be researched.
 * Automatically researches 'free' techs such as ammo for a weapon. Not "researchable"-related.
 * Should be called when a new item is researched (RS_MarkResearched) and after
 * the tree-initialisation (RS_InitTree)
 * @sa RS_MarkResearched
 */
void RS_MarkResearchable (const base_t* base, bool init)
{
	const base_t* thisBase = base;

	for (int i = 0; i < ccs.numTechnologies; i++) {
		technology_t* tech = RS_GetTechByIDX(i);
		/* In case we loopback we need to check for already marked techs. */
		if (tech->statusResearchable)
			continue;
		/* Check for collected items/aliens/etc... */
		if (tech->statusResearch == RS_FINISH)
			continue;

		cgi->Com_DPrintf(DEBUG_CLIENT, "RS_MarkResearchable: handling \"%s\".\n", tech->id);

		if (tech->base)
			base = tech->base;
		else
			base = thisBase;

		/* If required techs are all researched and all other requirements are met, mark this as researchable. */
		/* All requirements are met. */
		if (RS_RequirementsMet(tech, base)) {
			cgi->Com_DPrintf(DEBUG_CLIENT, "RS_MarkResearchable: \"%s\" marked researchable. reason:requirements.\n", tech->id);
			if (init && tech->time == 0)
				tech->mailSent = MAILSENT_PROPOSAL;
			RS_MarkOneResearchable(tech);
		}

		/* If the tech is a 'free' one (such as ammo for a weapon),
		 * mark it as researched and loop back to see if it unlocks
		 * any other techs */
		if (tech->statusResearchable && tech->time == 0) {
			if (init)
				tech->mailSent = MAILSENT_FINISHED;
			RS_ResearchFinish(tech);
			cgi->Com_DPrintf(DEBUG_CLIENT, "RS_MarkResearchable: automatically researched \"%s\"\n", tech->id);
			/* Restart the loop as this may have unlocked new possibilities. */
			i = -1;
		}
	}
	cgi->Com_DPrintf(DEBUG_CLIENT, "RS_MarkResearchable: Done.\n");
}

/**
 * @brief Assign required tech/item/etc... pointers for a single requirements list.
 * @note A function with the same behaviour was formerly also known as RS_InitRequirementList
 */
static void RS_AssignTechLinks (requirements_t* reqs)
{
	for (int i = 0; i < reqs->numLinks; i++) {
		requirement_t* req = &reqs->links[i];
		switch (req->type) {
		case RS_LINK_TECH:
		case RS_LINK_TECH_NOT:
			/* Get the index in the techtree. */
			req->link.tech = RS_GetTechByID(req->id);
			if (!req->link.tech)
				cgi->Com_Error(ERR_DROP, "RS_AssignTechLinks: Could not get tech definition for '%s'", req->id);
			break;
		case RS_LINK_ITEM:
			/* Get index in item-list. */
			req->link.od = INVSH_GetItemByID(req->id);
			if (!req->link.od)
				cgi->Com_Error(ERR_DROP, "RS_AssignTechLinks: Could not get item definition for '%s'", req->id);
			break;
		case RS_LINK_ALIEN:
		case RS_LINK_ALIEN_DEAD:
			req->link.td = cgi->Com_GetTeamDefinitionByID(req->id);
			if (!req->link.td)
				cgi->Com_Error(ERR_DROP, "RS_AssignTechLinks: Could not get alien type (alien or alien_dead) definition for '%s'", req->id);
			break;
		case RS_LINK_UFO:
			req->link.aircraft = AIR_GetAircraft(req->id);
			break;
		default:
			break;
		}
	}
}

/**
 * @brief Assign Link pointers to all required techs/items/etc...
 * @note This replaces the RS_InitRequirementList function (since the switch to the _OR and _AND list)
 */
void RS_RequiredLinksAssign (void)
{
	linkedList_t* ll = redirectedTechs;	/**< Use this so we do not change the original redirectedTechs pointer. */

	for (int i = 0; i < ccs.numTechnologies; i++) {
		technology_t* tech = RS_GetTechByIDX(i);
		if (tech->requireAND.numLinks)
			RS_AssignTechLinks(&tech->requireAND);
		if (tech->requireOR.numLinks)
			RS_AssignTechLinks(&tech->requireOR);
		if (tech->requireForProduction.numLinks)
			RS_AssignTechLinks(&tech->requireForProduction);
	}

	/* Link the redirected technologies to their correct "parents" */
	while (ll) {
		/* Get the data stored in the linked list. */
		assert(ll);
		technology_t* redirectedTech = (technology_t*) ll->data;
		ll = ll->next;

		assert(redirectedTech);

		assert(ll);
		redirectedTech->redirect = RS_GetTechByID((char*)ll->data);
		ll = ll->next;
	}

	/* clean up redirected techs list as it is no longer needed */
	cgi->LIST_Delete(&redirectedTechs);
}

/**
 * @brief Returns technology entry for an item
 * @param[in] item Pointer to the item (object) definition
 */
technology_t* RS_GetTechForItem (const objDef_t* item)
{
	if (item == nullptr)
		cgi->Com_Error(ERR_DROP, "RS_GetTechForItem: No item given");
	if (item->idx < 0 || item->idx > lengthof(ccs.objDefTechs))
		cgi->Com_Error(ERR_DROP, "RS_GetTechForItem: Buffer overflow");
	if (ccs.objDefTechs[item->idx] == nullptr)
		cgi->Com_Error(ERR_DROP, "RS_GetTechForItem: No technology for item %s", item->id);
	return ccs.objDefTechs[item->idx];
}

/**
 * @brief Returns technology entry for a team
 * @param[in] team Pointer to the team definition
 */
technology_t* RS_GetTechForTeam (const teamDef_t* team)
{
	if (team == nullptr)
		cgi->Com_Error(ERR_DROP, "RS_GetTechForTeam: No team given");
	if (team->idx < 0 || team->idx > lengthof(ccs.teamDefTechs))
		cgi->Com_Error(ERR_DROP, "RS_GetTechForTeam: Buffer overflow");
	if (ccs.teamDefTechs[team->idx] == nullptr)
		cgi->Com_Error(ERR_DROP, "RS_GetTechForTeam: No technology for team %s", team->id);
	return ccs.teamDefTechs[team->idx];
}

/**
 * @brief Gets all needed names/file-paths/etc... for each technology entry.
 * Should be executed after the parsing of _all_ the ufo files and e.g. the
 * research tree/inventory/etc... are initialised.
 * @param[in] campaign The campaign data structure
 * @param[in] load true if we are loading a game, false otherwise
 * @todo Add a function to reset ALL research-stati to RS_NONE; -> to be called after start of a new game.
 * @sa CP_CampaignInit
 */
void RS_InitTree (const campaign_t* campaign, bool load)
{
	int i, j;
	technology_t* tech;
	byte found;
	const objDef_t* od;

	/* Add links to technologies. */
	for (i = 0, od = cgi->csi->ods; i < cgi->csi->numODs; i++, od++) {
		ccs.objDefTechs[od->idx] = RS_GetTechByProvided(od->id);
		if (!ccs.objDefTechs[od->idx])
			cgi->Com_Error(ERR_DROP, "RS_InitTree: Could not find a valid tech for item %s", od->id);
	}

	for (i = 0, tech = ccs.technologies; i < ccs.numTechnologies; i++, tech++) {
		for (j = 0; j < tech->markResearched.numDefinitions; j++) {
			if (tech->markResearched.markOnly[j] && Q_streq(tech->markResearched.campaign[j], campaign->researched)) {
				cgi->Com_DPrintf(DEBUG_CLIENT, "...mark %s as researched\n", tech->id);
				RS_ResearchFinish(tech);
				break;
			}
		}

		/* Save the idx to the id-names of the different requirement-types for quicker access.
		 * The id-strings themself are not really needed afterwards :-/ */
		RS_AssignTechLinks(&tech->requireAND);
		RS_AssignTechLinks(&tech->requireOR);

		/* Search in correct data/.ufo */
		switch (tech->type) {
		case RS_CRAFTITEM:
			if (!tech->name)
				cgi->Com_DPrintf(DEBUG_CLIENT, "RS_InitTree: \"%s\" A type craftitem needs to have a 'name\txxx' defined.", tech->id);
			break;
		case RS_NEWS:
			if (!tech->name)
				cgi->Com_DPrintf(DEBUG_CLIENT, "RS_InitTree: \"%s\" A 'type news' item needs to have a 'name\txxx' defined.", tech->id);
			break;
		case RS_TECH:
			if (!tech->name)
				cgi->Com_DPrintf(DEBUG_CLIENT, "RS_InitTree: \"%s\" A 'type tech' item needs to have a 'name\txxx' defined.", tech->id);
			break;
		case RS_WEAPON:
		case RS_ARMOUR:
			found = false;
			for (j = 0; j < cgi->csi->numODs; j++) {	/* j = item index */
				const objDef_t* item = INVSH_GetItemByIDX(j);

				/* This item has been 'provided' -> get the correct data. */
				if (Q_streq(tech->provides, item->id)) {
					found = true;
					if (!tech->name)
						tech->name = cgi->PoolStrDup(item->name, cp_campaignPool, 0);
					if (!tech->mdl)
						tech->mdl = cgi->PoolStrDup(item->model, cp_campaignPool, 0);
					if (!tech->image)
						tech->image = cgi->PoolStrDup(item->image, cp_campaignPool, 0);
					break;
				}
			}
			/* No id found in cgi->csi->ods */
			if (!found) {
				tech->name = cgi->PoolStrDup(tech->id, cp_campaignPool, 0);
				cgi->Com_Printf("RS_InitTree: \"%s\" - Linked weapon or armour (provided=\"%s\") not found. Tech-id used as name.\n",
						tech->id, tech->provides);
			}
			break;
		case RS_BUILDING:
			found = false;
			for (j = 0; j < ccs.numBuildingTemplates; j++) {
				building_t* building = &ccs.buildingTemplates[j];
				/* This building has been 'provided'  -> get the correct data. */
				if (Q_streq(tech->provides, building->id)) {
					found = true;
					if (!tech->name)
						tech->name = cgi->PoolStrDup(building->name, cp_campaignPool, 0);
					if (!tech->image)
						tech->image = cgi->PoolStrDup(building->image, cp_campaignPool, 0);
					break;
				}
			}
			if (!found) {
				tech->name = cgi->PoolStrDup(tech->id, cp_campaignPool, 0);
				cgi->Com_DPrintf(DEBUG_CLIENT, "RS_InitTree: \"%s\" - Linked building (provided=\"%s\") not found. Tech-id used as name.\n",
						tech->id, tech->provides);
			}
			break;
		case RS_CRAFT:
			found = false;
			for (j = 0; j < ccs.numAircraftTemplates; j++) {
				aircraft_t* aircraftTemplate = &ccs.aircraftTemplates[j];
				/* This aircraft has been 'provided'  -> get the correct data. */
				if (!tech->provides)
					cgi->Com_Error(ERR_FATAL, "RS_InitTree: \"%s\" - No linked aircraft or craft-upgrade.\n", tech->id);
				if (Q_streq(tech->provides, aircraftTemplate->id)) {
					found = true;
					if (!tech->name)
						tech->name = cgi->PoolStrDup(aircraftTemplate->name, cp_campaignPool, 0);
					if (!tech->mdl) {	/* DEBUG testing */
						tech->mdl = cgi->PoolStrDup(aircraftTemplate->model, cp_campaignPool, 0);
						cgi->Com_DPrintf(DEBUG_CLIENT, "RS_InitTree: aircraft model \"%s\" \n", aircraftTemplate->model);
					}
					aircraftTemplate->tech = tech;
					break;
				}
			}
			if (!found)
				cgi->Com_Printf("RS_InitTree: \"%s\" - Linked aircraft or craft-upgrade (provided=\"%s\") not found.\n", tech->id, tech->provides);
			break;
		case RS_ALIEN:
			/* does nothing right now */
			break;
		case RS_UGV:
			/** @todo Implement me */
			break;
		case RS_LOGIC:
			/* Does not need any additional data. */
			break;
		}

		/* Check if we finally have a name for the tech. */
		if (!tech->name) {
			if (tech->type != RS_LOGIC)
				cgi->Com_Error(ERR_DROP, "RS_InitTree: \"%s\" - no name found!", tech->id);
		} else {
			/* Fill in subject lines of tech-mails.
			 * The tech-name is copied if nothing is defined. */
			for (j = 0; j < TECHMAIL_MAX; j++) {
				/* Check if no subject was defined (but it is supposed to be sent) */
				if (!tech->mail[j].subject && tech->mail[j].to) {
					tech->mail[j].subject = tech->name;
				}
			}
		}

		if (!tech->image && !tech->mdl)
			cgi->Com_DPrintf(DEBUG_CLIENT, "Tech %s of type %i has no image (%p) and no model (%p) assigned.\n",
					tech->id, tech->type, tech->image, tech->mdl);
	}

	if (load) {
		/* when you load a savegame right after starting UFO, the aircraft in bases
		 * and installations don't have any tech assigned */
		AIR_Foreach(aircraft) {
			/* if you already played before loading the game, tech are already defined for templates */
			if (!aircraft->tech)
				aircraft->tech = RS_GetTechByProvided(aircraft->id);
		}
	}

	cgi->Com_DPrintf(DEBUG_CLIENT, "RS_InitTree: Technology tree initialised. %i entries found.\n", i);
}

/**
 * @brief Assigns scientist to the selected research-project.
 * @note The lab will be automatically selected (the first one that has still free space).
 * @param[in] tech What technology you want to assign the scientist to.
 * @param[in] base Pointer to base where the research is ongoing.
 * @param[in] employee Pointer to the scientist to assign. It can be nullptr! That means "any".
 * @note if employee is nullptr, te system selects an unassigned scientist on the selected (or tech-) base
 * @sa RS_AssignScientist_f
 * @sa RS_RemoveScientist
 */
void RS_AssignScientist (technology_t* tech, base_t* base, Employee* employee)
{
	assert(tech);
	cgi->Com_DPrintf(DEBUG_CLIENT, "RS_AssignScientist: %i | %s \n", tech->idx, tech->name);

	/* if the tech is already assigned to a base, use that one */
	if (tech->base)
		base = tech->base;

	assert(base);

	if (!employee)
		employee = E_GetUnassignedEmployee(base, EMPL_SCIENTIST);
	if (!employee) {
		/* No scientists are free in this base. */
		cgi->Com_DPrintf(DEBUG_CLIENT, "No free scientists in this base (%s) to assign to tech '%s'\n", base->name, tech->id);
		return;
	}

	if (!tech->statusResearchable)
		return;

	if (CAP_GetFreeCapacity(base, CAP_LABSPACE) <= 0) {
		CP_Popup(_("Not enough laboratories"), _("No free space in laboratories left.\nBuild more laboratories.\n"));
		return;
	}

	tech->scientists++;
	tech->base = base;
	CAP_AddCurrent(base, CAP_LABSPACE, 1);
	employee->setAssigned(true);
	tech->statusResearch = RS_RUNNING;
}

/**
 * @brief Remove a scientist from a technology.
 * @param[in] tech The technology you want to remove the scientist from.
 * @param[in] employee Employee you want to remove (nullptr if you don't care which one should be removed).
 * @sa RS_RemoveScientist_f
 * @sa RS_AssignScientist
 */
void RS_RemoveScientist (technology_t* tech, Employee* employee)
{
	assert(tech);

	/* no need to remove anything, but we can do some check */
	if (tech->scientists == 0) {
		assert(tech->base == nullptr);
		assert(tech->statusResearch == RS_PAUSED);
		return;
	}

	if (!employee)
		employee = E_GetAssignedEmployee(tech->base, EMPL_SCIENTIST);
	if (!employee)
		cgi->Com_Printf("RS_RemoveScientist: No assigned scientists found - serious inconsistency.\n");
	else
		employee->setAssigned(false);

	tech->scientists--;
	CAP_AddCurrent(tech->base, CAP_LABSPACE, -1);

	assert(tech->scientists >= 0);
	if (tech->scientists == 0) {
		/* Remove the tech from the base if no scientists are left to research it. */
		tech->base = nullptr;
		tech->statusResearch = RS_PAUSED;
	}
}

/**
 * @brief Remove one scientist from research project if needed.
 * @param[in] base Pointer to base where a scientist should be removed.
 * @param[in] employee Pointer to the employee that is fired.
 * @note used when a scientist is fired.
 * @note This function is called before the employee is actually fired.
 */
void RS_RemoveFiredScientist (base_t* base, Employee* employee)
{
	technology_t* tech;
	Employee* freeScientist = E_GetUnassignedEmployee(base, EMPL_SCIENTIST);

	assert(base);
	assert(employee);

	/* Get a tech where there is at least one scientist working on (unless no scientist working in this base) */
	tech = RS_GetTechWithMostScientists(base);

	/* tech should never be nullptr, as there is at least 1 scientist working in base */
	if (tech == nullptr) {
		cgi->Com_Printf("RS_RemoveFiredScientist: Cannot unassign scientist %d no tech is being researched in base %d\n", employee->chr.ucn, base->idx);
		employee->setAssigned(false);
	} else {
		RS_RemoveScientist(tech, employee);
	}

	/* if there is at least one scientist not working on a project, make this one replace removed employee */
	if (freeScientist)
		RS_AssignScientist(tech, base, freeScientist);
}

/**
 * @brief Mark technologies as researched. This includes techs that depends on "tech" and have time=0
 * @param[in] tech Pointer to a technology_t struct.
 * @param[in] base Pointer to base where we did research.
 * @todo Base shouldn't be needed here - check RS_MarkResearchable() for that.
 * @sa RS_ResearchRun
 */
static void RS_MarkResearched (technology_t* tech, const base_t* base)
{
	RS_ResearchFinish(tech);
	cgi->Com_DPrintf(DEBUG_CLIENT, "Research of \"%s\" finished.\n", tech->id);
	RS_MarkResearchable(base);
}

/**
 * Pick a random base to research a story line event tech
 * @param techID The event technology script id to research
 * @note If there is no base available the tech is not marked as researched, too
 */
bool RS_MarkStoryLineEventResearched (const char* techID)
{
	technology_t* tech = RS_GetTechByID(techID);
	if (!RS_IsResearched_ptr(tech)) {
		const base_t* base = B_GetNext(nullptr);
		if (base != nullptr) {
			RS_MarkResearched(tech, base);
			return true;
		}
	}
	return false;
}

/**
 * @brief Checks if running researches still meet their requirements
 */
void RS_CheckRequirements (void)
{
	for (int i = 0; i < ccs.numTechnologies; i++) {
		technology_t* tech = RS_GetTechByIDX(i);

		if (tech->statusResearch != RS_RUNNING)
			continue;

		if (RS_RequirementsMet(tech, tech->base))
			continue;

		Com_sprintf(cp_messageBuffer, sizeof(cp_messageBuffer), _("Research prerequisites of %s are not met at %s. Research halted!"), _(tech->name), tech->base->name);
		MSO_CheckAddNewMessage(NT_RESEARCH_HALTED, _("Research halted"), cp_messageBuffer, MSG_RESEARCH_HALTED);

		RS_StopResearch(tech);
	}
}

/**
 * @brief Checks the research status
 * @todo Needs to check on the exact time that elapsed since the last check of the status.
 * @sa RS_MarkResearched
 * @return The amout of new researched technologies
 */
int RS_ResearchRun (void)
{
	int newResearch = 0;

	for (int i = 0; i < ccs.numTechnologies; i++) {
		technology_t* tech = RS_GetTechByIDX(i);

		if (tech->statusResearch != RS_RUNNING)
			continue;

		if (!RS_RequirementsMet(tech, tech->base)) {
			Com_sprintf(cp_messageBuffer, sizeof(cp_messageBuffer), _("Research prerequisites of %s are not met at %s. Research halted!"), _(tech->name), tech->base->name);
			MSO_CheckAddNewMessage(NT_RESEARCH_HALTED, _("Research halted"), cp_messageBuffer, MSG_RESEARCH_HALTED);

			RS_StopResearch(tech);
			continue;
		}

		if (tech->time > 0 && tech->scientists > 0) {
			/* If there are scientists there _has_ to be a base. */
			const base_t* base = tech->base;
			assert(tech->base);
			if (RS_ResearchAllowed(base)) {
				tech->time -= tech->scientists * ccs.curCampaign->researchRate;
				/* Will be a good thing (think of percentage-calculation) once non-integer values are used. */
				if (tech->time <= 0) {
					RS_MarkResearched(tech, base);

					newResearch++;
					tech->time = 0;
				}
			}
		}
	}

	return newResearch;
}

#ifdef DEBUG
/** @todo use cgi->Com_RegisterConstInt(); */
static const char* RS_TechTypeToName (researchType_t type)
{
	switch(type) {
	case RS_TECH:
		return "tech";
	case RS_WEAPON:
		return "weapon";
	case RS_ARMOUR:
		return "armour";
	case RS_CRAFT:
		return "craft";
	case RS_CRAFTITEM:
		return "craftitem";
	case RS_BUILDING:
		return "building";
	case RS_ALIEN:
		return "alien";
	case RS_UGV:
		return "ugv";
	case RS_NEWS:
		return "news";
	case RS_LOGIC:
		return "logic";
	default:
		return "unknown";
	}
}

static const char* RS_TechReqToName (requirement_t* req)
{
	switch(req->type) {
	case RS_LINK_TECH:
		return req->link.tech->id;
	case RS_LINK_TECH_NOT:
		return va("not %s", req->link.tech->id);
	case RS_LINK_ITEM:
		return req->link.od->id;
	case RS_LINK_ALIEN:
		return req->link.td->id;
	case RS_LINK_ALIEN_DEAD:
		return req->link.td->id;
	case RS_LINK_ALIEN_GLOBAL:
		return "global alien count";
	case RS_LINK_UFO:
		return req->link.aircraft->id;
	case RS_LINK_ANTIMATTER:
		return "antimatter";
	default:
		return "unknown";
	}
}

/** @todo use cgi->Com_RegisterConstInt(); */
static const char* RS_TechLinkTypeToName (requirementType_t type)
{
	switch(type) {
	case RS_LINK_TECH:
		return "tech";
	case RS_LINK_TECH_NOT:
		return "tech (not)";
	case RS_LINK_ITEM:
		return "item";
	case RS_LINK_ALIEN:
		return "alien";
	case RS_LINK_ALIEN_DEAD:
		return "alien_dead";
	case RS_LINK_ALIEN_GLOBAL:
		return "alienglobal";
	case RS_LINK_UFO:
		return "ufo";
	case RS_LINK_ANTIMATTER:
		return "antimatter";
	default:
		return "unknown";
	}
}

/**
 * @brief List all parsed technologies and their attributes in commandline/console.
 * @note called with debug_listtech
 */
static void RS_TechnologyList_f (void)
{
	cgi->Com_Printf("#techs: %i\n", ccs.numTechnologies);
	for (int i = 0; i < ccs.numTechnologies; i++) {
		int j;
		technology_t* tech;
		requirements_t* reqs;
		dateLong_t date;

		tech = RS_GetTechByIDX(i);
		cgi->Com_Printf("Tech: %s\n", tech->id);
		cgi->Com_Printf("... time      -> %.2f\n", tech->time);
		cgi->Com_Printf("... name      -> %s\n", tech->name);
		reqs = &tech->requireAND;
		cgi->Com_Printf("... requires ALL  ->");
		for (j = 0; j < reqs->numLinks; j++)
			cgi->Com_Printf(" %s (%s) %s", reqs->links[j].id, RS_TechLinkTypeToName(reqs->links[j].type), RS_TechReqToName(&reqs->links[j]));
		reqs = &tech->requireOR;
		cgi->Com_Printf("\n");
		cgi->Com_Printf("... requires ANY  ->");
		for (j = 0; j < reqs->numLinks; j++)
			cgi->Com_Printf(" %s (%s) %s", reqs->links[j].id, RS_TechLinkTypeToName(reqs->links[j].type), RS_TechReqToName(&reqs->links[j]));
		cgi->Com_Printf("\n");
		cgi->Com_Printf("... provides  -> %s", tech->provides);
		cgi->Com_Printf("\n");

		cgi->Com_Printf("... type      -> ");
		cgi->Com_Printf("%s\n", RS_TechTypeToName(tech->type));

		cgi->Com_Printf("... researchable -> %i\n", tech->statusResearchable);

		if (tech->statusResearchable) {
			CP_DateConvertLong(tech->preResearchedDate, &date);
			cgi->Com_Printf("... researchable date: %02i %02i %i\n", date.day, date.month, date.year);
		}

		cgi->Com_Printf("... research  -> ");
		switch (tech->statusResearch) {
		case RS_NONE:
			cgi->Com_Printf("nothing\n");
			break;
		case RS_RUNNING:
			cgi->Com_Printf("running\n");
			break;
		case RS_PAUSED:
			cgi->Com_Printf("paused\n");
			break;
		case RS_FINISH:
			cgi->Com_Printf("done\n");
			CP_DateConvertLong(tech->researchedDate, &date);
			cgi->Com_Printf("... research date: %02i %02i %i\n", date.day, date.month, date.year);
			break;
		default:
			cgi->Com_Printf("unknown\n");
			break;
		}
	}
}

/**
 * @brief Mark everything as researched
 * @sa UI_StartServer
 */
static void RS_DebugMarkResearchedAll (void)
{
	for (int i = 0; i < ccs.numTechnologies; i++) {
		technology_t* tech = RS_GetTechByIDX(i);
		cgi->Com_DPrintf(DEBUG_CLIENT, "...mark %s as researched\n", tech->id);
		RS_MarkOneResearchable(tech);
		RS_ResearchFinish(tech);
		/** @todo Set all "collected" entries in the requirements to the "amount" value. */
	}
}

/**
 * @brief Set all items to researched
 * @note Just for debugging purposes
 */
static void RS_DebugResearchAll_f (void)
{
	if (cgi->Cmd_Argc() != 2) {
		RS_DebugMarkResearchedAll();
	} else {
		technology_t* tech = RS_GetTechByID(cgi->Cmd_Argv(1));
		if (!tech)
			return;
		cgi->Com_DPrintf(DEBUG_CLIENT, "...mark %s as researched\n", tech->id);
		RS_MarkOneResearchable(tech);
		RS_ResearchFinish(tech);
	}
}

/**
 * @brief Set all item to researched
 * @note Just for debugging purposes
 */
static void RS_DebugResearchableAll_f (void)
{
	if (cgi->Cmd_Argc() != 2) {
		for (int i = 0; i < ccs.numTechnologies; i++) {
			technology_t* tech = RS_GetTechByIDX(i);
			cgi->Com_Printf("...mark %s as researchable\n", tech->id);
			RS_MarkOneResearchable(tech);
			RS_MarkCollected(tech);
		}
	} else {
		technology_t* tech = RS_GetTechByID(cgi->Cmd_Argv(1));
		if (tech) {
			cgi->Com_Printf("...mark %s as researchable\n", tech->id);
			RS_MarkOneResearchable(tech);
			RS_MarkCollected(tech);
		}
	}
}

static void RS_DebugFinishResearches_f (void)
{
	for (int i = 0; i < ccs.numTechnologies; i++) {
		technology_t* tech = RS_GetTechByIDX(i);
		if (tech->statusResearch == RS_RUNNING) {
			assert(tech->base);
			cgi->Com_DPrintf(DEBUG_CLIENT, "...mark %s as researched\n", tech->id);
			RS_MarkResearched(tech, tech->base);
		}
	}
}
#endif


/**
 * @brief This is more or less the initial
 * Bind some of the functions in this file to console-commands that you can call ingame.
 * Called from UI_InitStartup resp. CL_InitLocal
 */
void RS_InitStartup (void)
{
	/* add commands and cvars */
#ifdef DEBUG
	cgi->Cmd_AddCommand("debug_listtech", RS_TechnologyList_f, "Print the current parsed technologies to the game console");
	cgi->Cmd_AddCommand("debug_researchall", RS_DebugResearchAll_f, "Mark all techs as researched");
	cgi->Cmd_AddCommand("debug_researchableall", RS_DebugResearchableAll_f, "Mark all techs as researchable");
	cgi->Cmd_AddCommand("debug_finishresearches", RS_DebugFinishResearches_f, "Mark all running researches as finished");
#endif
}

/**
 * @brief This is called everytime RS_ParseTechnologies is called - to prevent cyclic hash tables
 */
void RS_ResetTechs (void)
{
	/* they are static - but i'm paranoid - this is called before the techs were parsed */
	OBJZERO(techHash);
	OBJZERO(techHashProvided);

	/* delete redirectedTechs, will be filled during parse */
	cgi->LIST_Delete(&redirectedTechs);
}

/**
 * @brief The valid definition names in the research.ufo file.
 * @note Handled in parser below.
 * description, preDescription, requireAND, requireOR, up_chapter
 */
static const value_t valid_tech_vars[] = {
	{"name", V_TRANSLATION_STRING, offsetof(technology_t, name), 0},
	{"provides", V_HUNK_STRING, offsetof(technology_t, provides), 0},
	{"event", V_HUNK_STRING, offsetof(technology_t, finishedResearchEvent), 0},
	{"delay", V_INT, offsetof(technology_t, delay), MEMBER_SIZEOF(technology_t, delay)},
	{"producetime", V_INT, offsetof(technology_t, produceTime), MEMBER_SIZEOF(technology_t, produceTime)},
	{"time", V_FLOAT, offsetof(technology_t, time), MEMBER_SIZEOF(technology_t, time)},
	{"announce", V_BOOL, offsetof(technology_t, announce), MEMBER_SIZEOF(technology_t, announce)},
	{"image", V_HUNK_STRING, offsetof(technology_t, image), 0},
	{"model", V_HUNK_STRING, offsetof(technology_t, mdl), 0},

	{nullptr, V_NULL, 0, 0}
};

/**
 * @brief The valid definition names in the research.ufo file for tech mails
 */
static const value_t valid_techmail_vars[] = {
	{"from", V_TRANSLATION_STRING, offsetof(techMail_t, from), 0},
	{"to", V_TRANSLATION_STRING, offsetof(techMail_t, to), 0},
	{"subject", V_TRANSLATION_STRING, offsetof(techMail_t, subject), 0},
	{"date", V_TRANSLATION_STRING, offsetof(techMail_t, date), 0},
	{"icon", V_HUNK_STRING, offsetof(techMail_t, icon), 0},
	{"model", V_HUNK_STRING, offsetof(techMail_t, model), 0},

	{nullptr, V_NULL, 0, 0}
};

/**
 * @brief Parses one "tech" entry in the research.ufo file and writes it into the next free entry in technologies (technology_t).
 * @param[in] name Unique id of a technology_t. This is parsed from "tech xxx" -> id=xxx
 * @param[in] text the whole following text that is part of the "tech" item definition in research.ufo.
 * @sa CL_ParseScriptFirst
 * @sa GAME_SetMode
 * @note write into cp_campaignPool - free on every game restart and reparse
 */
void RS_ParseTechnologies (const char* name, const char** text)
{
	for (int i = 0; i < ccs.numTechnologies; i++) {
		if (Q_streq(ccs.technologies[i].id, name)) {
			cgi->Com_Printf("RS_ParseTechnologies: Second tech with same name found (%s) - second ignored\n", name);
			return;
		}
	}

	if (ccs.numTechnologies >= MAX_TECHNOLOGIES) {
		cgi->Com_Printf("RS_ParseTechnologies: too many technology entries. limit is %i.\n", MAX_TECHNOLOGIES);
		return;
	}

	/* get body */
	const char* token = Com_Parse(text);
	if (!*text || *token != '{') {
		cgi->Com_Printf("RS_ParseTechnologies: \"%s\" technology def without body ignored.\n", name);
		return;
	}

	/* New technology (next free entry in global tech-list) */
	technology_t* tech = &ccs.technologies[ccs.numTechnologies];
	ccs.numTechnologies++;

	OBJZERO(*tech);

	/*
	 * Set standard values
	 */
	tech->idx = ccs.numTechnologies - 1;
	tech->id = cgi->PoolStrDup(name, cp_campaignPool, 0);
	unsigned hash = Com_HashKey(tech->id, TECH_HASH_SIZE);

	/* Set the default string for descriptions (available even if numDescriptions is 0) */
	tech->description.text[0] = _("No description available.");
	tech->preDescription.text[0] = _("No research proposal available.");
	/* Set desc-indices to undef. */
	tech->description.usedDescription = -1;
	tech->preDescription.usedDescription = -1;

	/* link the variable in */
	/* tech_hash should be null on the first run */
	tech->hashNext = techHash[hash];
	/* set the techHash pointer to the current tech */
	/* if there were already others in techHash at position hash, they are now
	 * accessible via tech->next - loop until tech->next is null (the first tech
	 * at that position)
	 */
	techHash[hash] = tech;

	tech->type = RS_TECH;
	tech->statusResearch = RS_NONE;
	tech->statusResearchable = false;

	const char* errhead = "RS_ParseTechnologies: unexpected end of file.";
	do {
		/* get the name type */
		token = cgi->Com_EParse(text, errhead, name);
		if (!*text)
			break;
		if (*token == '}')
			break;
		/* get values */
		if (Q_streq(token, "type")) {
			/* what type of tech this is */
			token = cgi->Com_EParse(text, errhead, name);
			if (!*text)
				return;
			/** @todo use cgi->Com_RegisterConstInt(); */
			/* redundant, but oh well. */
			if (Q_streq(token, "tech"))
				tech->type = RS_TECH;
			else if (Q_streq(token, "weapon"))
				tech->type = RS_WEAPON;
			else if (Q_streq(token, "news"))
				tech->type = RS_NEWS;
			else if (Q_streq(token, "armour"))
				tech->type = RS_ARMOUR;
			else if (Q_streq(token, "craft"))
				tech->type = RS_CRAFT;
			else if (Q_streq(token, "craftitem"))
				tech->type = RS_CRAFTITEM;
			else if (Q_streq(token, "building"))
				tech->type = RS_BUILDING;
			else if (Q_streq(token, "alien"))
				tech->type = RS_ALIEN;
			else if (Q_streq(token, "ugv"))
				tech->type = RS_UGV;
			else if (Q_streq(token, "logic"))
				tech->type = RS_LOGIC;
			else
				cgi->Com_Printf("RS_ParseTechnologies: \"%s\" unknown techtype: \"%s\" - ignored.\n", name, token);
		} else {
			if (Q_streq(token, "description") || Q_streq(token, "pre_description")) {
				/* Parse the available descriptions for this tech */
				technologyDescriptions_t* descTemp;

				/* Link to correct list. */
				if (Q_streq(token, "pre_description")) {
					descTemp = &tech->preDescription;
				} else {
					descTemp = &tech->description;
				}

				token = cgi->Com_EParse(text, errhead, name);
				if (!*text)
					break;
				if (*token != '{')
					break;

				do {	/* Loop through all descriptions in the list.*/
					token = cgi->Com_EParse(text, errhead, name);
					if (!*text)
						return;
					if (*token == '}')
						break;

					linkedList_t* list;

					if (Q_streq(token, "default")) {
						list = nullptr;
						cgi->LIST_AddString(&list, token);
						token = cgi->Com_EParse(text, errhead, name);
						cgi->LIST_AddString(&list, token);
					} else if (Q_streq(token, "extra")) {
						if (!cgi->Com_ParseList(text, &list)) {
							cgi->Com_Error(ERR_DROP, "RS_ParseTechnologies: error while reading extra description tuple");
						}
						if (cgi->LIST_Count(list) != 2) {
							cgi->LIST_Delete(&list);
							cgi->Com_Error(ERR_DROP, "RS_ParseTechnologies: extra description tuple must contains 2 elements (id string)");
						}
					} else {
						cgi->Com_Error(ERR_DROP, "RS_ParseTechnologies: error while reading description: token \"%s\" not expected", token);
					}

					if (descTemp->numDescriptions < MAX_DESCRIPTIONS) {
						const char* id = (char*)list->data;
						const char* description = (char*)list->next->data;

						/* Copy tech string into entry. */
						descTemp->tech[descTemp->numDescriptions] = cgi->PoolStrDup(id, cp_campaignPool, 0);

						/* skip translation marker */
						if (description[0] == '_')
							description++;

						descTemp->text[descTemp->numDescriptions] = cgi->PoolStrDup(description, cp_campaignPool, 0);
						descTemp->numDescriptions++;
					} else {
						cgi->Com_Printf("skipped description for tech '%s'\n", tech->id);
					}
					cgi->LIST_Delete(&list);
				} while (*text);

			} else if (Q_streq(token, "redirect")) {
				token = cgi->Com_EParse(text, errhead, name);
				/* Store this tech and the parsed tech-id of the target of the redirection for later linking. */
				cgi->LIST_AddPointer(&redirectedTechs, tech);
				cgi->LIST_AddString(&redirectedTechs, token);
			} else if (Q_streq(token, "require_AND") || Q_streq(token, "require_OR") || Q_streq(token, "require_for_production")) {
				requirements_t* requiredTemp;
				/* Link to correct list. */
				if (Q_streq(token, "require_AND")) {
					requiredTemp = &tech->requireAND;
				} else if (Q_streq(token, "require_OR")) {
					requiredTemp = &tech->requireOR;
				} else { /* It's "requireForProduction" */
					requiredTemp = &tech->requireForProduction;
				}

				token = cgi->Com_EParse(text, errhead, name);
				if (!*text)
					break;
				if (*token != '{')
					break;

				do {	/* Loop through all 'require' entries.*/
					token = cgi->Com_EParse(text, errhead, name);
					if (!*text)
						return;
					if (*token == '}')
						break;

					if (Q_streq(token, "tech") || Q_streq(token, "tech_not")) {
						if (requiredTemp->numLinks < MAX_TECHLINKS) {
							/* Set requirement-type. */
							if (Q_streq(token, "tech_not"))
								requiredTemp->links[requiredTemp->numLinks].type = RS_LINK_TECH_NOT;
							else
								requiredTemp->links[requiredTemp->numLinks].type = RS_LINK_TECH;

							/* Set requirement-name (id). */
							token = Com_Parse(text);
							requiredTemp->links[requiredTemp->numLinks].id = cgi->PoolStrDup(token, cp_campaignPool, 0);

							cgi->Com_DPrintf(DEBUG_CLIENT, "RS_ParseTechnologies: require-tech ('tech' or 'tech_not')- %s\n", requiredTemp->links[requiredTemp->numLinks].id);

							requiredTemp->numLinks++;
						} else {
							cgi->Com_Printf("RS_ParseTechnologies: \"%s\" Too many 'required' defined. Limit is %i - ignored.\n", name, MAX_TECHLINKS);
						}
					} else if (Q_streq(token, "item")) {
						/* Defines what items need to be collected for this item to be researchable. */
						if (requiredTemp->numLinks < MAX_TECHLINKS) {
							linkedList_t* list;
							if (!cgi->Com_ParseList(text, &list)) {
								cgi->Com_Error(ERR_DROP, "RS_ParseTechnologies: error while reading required item tuple");
							}

							if (cgi->LIST_Count(list) != 2) {
								cgi->Com_Error(ERR_DROP, "RS_ParseTechnologies: required item tuple must contains 2 elements (id pos)");
							}

							const char* idToken = (char*)list->data;
							const char* amountToken = (char*)list->next->data;

							/* Set requirement-type. */
							requiredTemp->links[requiredTemp->numLinks].type = RS_LINK_ITEM;
							/* Set requirement-name (id). */
							requiredTemp->links[requiredTemp->numLinks].id = cgi->PoolStrDup(idToken, cp_campaignPool, 0);
							/* Set requirement-amount of item. */
							requiredTemp->links[requiredTemp->numLinks].amount = atoi(amountToken);
							cgi->Com_DPrintf(DEBUG_CLIENT, "RS_ParseTechnologies: require-item - %s - %i\n", requiredTemp->links[requiredTemp->numLinks].id, requiredTemp->links[requiredTemp->numLinks].amount);
							requiredTemp->numLinks++;
							cgi->LIST_Delete(&list);
						} else {
							cgi->Com_Printf("RS_ParseTechnologies: \"%s\" Too many 'required' defined. Limit is %i - ignored.\n", name, MAX_TECHLINKS);
						}
					} else if (Q_streq(token, "alienglobal")) {
						if (requiredTemp->numLinks < MAX_TECHLINKS) {
							/* Set requirement-type. */
							requiredTemp->links[requiredTemp->numLinks].type = RS_LINK_ALIEN_GLOBAL;
							cgi->Com_DPrintf(DEBUG_CLIENT, "RS_ParseTechnologies:  require-alienglobal - %i\n", requiredTemp->links[requiredTemp->numLinks].amount);

							/* Set requirement-amount of item. */
							token = Com_Parse(text);
							requiredTemp->links[requiredTemp->numLinks].amount = atoi(token);
							requiredTemp->numLinks++;
						} else {
							cgi->Com_Printf("RS_ParseTechnologies: \"%s\" Too many 'required' defined. Limit is %i - ignored.\n", name, MAX_TECHLINKS);
						}
					} else if (Q_streq(token, "alien_dead") || Q_streq(token, "alien")) { /* Does this only check the beginning of the string? */
						/* Defines what live or dead aliens need to be collected for this item to be researchable. */
						if (requiredTemp->numLinks < MAX_TECHLINKS) {
							/* Set requirement-type. */
							if (Q_streq(token, "alien_dead")) {
								requiredTemp->links[requiredTemp->numLinks].type = RS_LINK_ALIEN_DEAD;
								cgi->Com_DPrintf(DEBUG_CLIENT, "RS_ParseTechnologies:  require-alien dead - %s - %i\n", requiredTemp->links[requiredTemp->numLinks].id, requiredTemp->links[requiredTemp->numLinks].amount);
							} else {
								requiredTemp->links[requiredTemp->numLinks].type = RS_LINK_ALIEN;
								cgi->Com_DPrintf(DEBUG_CLIENT, "RS_ParseTechnologies:  require-alien alive - %s - %i\n", requiredTemp->links[requiredTemp->numLinks].id, requiredTemp->links[requiredTemp->numLinks].amount);
							}

							linkedList_t* list;
							if (!cgi->Com_ParseList(text, &list)) {
								cgi->Com_Error(ERR_DROP, "RS_ParseTechnologies: error while reading required alien tuple");
							}

							if (cgi->LIST_Count(list) != 2) {
								cgi->Com_Error(ERR_DROP, "RS_ParseTechnologies: required alien tuple must contains 2 elements (id pos)");
							}

							const char* idToken = (char*)list->data;
							const char* amountToken = (char*)list->next->data;

							/* Set requirement-name (id). */
							requiredTemp->links[requiredTemp->numLinks].id = cgi->PoolStrDup(idToken, cp_campaignPool, 0);
							/* Set requirement-amount of item. */
							requiredTemp->links[requiredTemp->numLinks].amount = atoi(amountToken);
							requiredTemp->numLinks++;
							cgi->LIST_Delete(&list);
						} else {
							cgi->Com_Printf("RS_ParseTechnologies: \"%s\" Too many 'required' defined. Limit is %i - ignored.\n", name, MAX_TECHLINKS);
						}
					} else if (Q_streq(token, "ufo")) {
						/* Defines what ufos need to be collected for this item to be researchable. */
						if (requiredTemp->numLinks < MAX_TECHLINKS) {
							linkedList_t* list;
							if (!cgi->Com_ParseList(text, &list)) {
								cgi->Com_Error(ERR_DROP, "RS_ParseTechnologies: error while reading required item tuple");
							}

							if (cgi->LIST_Count(list) != 2) {
								cgi->Com_Error(ERR_DROP, "RS_ParseTechnologies: required item tuple must contains 2 elements (id pos)");
							}

							const char* idToken = (char*)list->data;
							const char* amountToken = (char*)list->next->data;

							/* Set requirement-type. */
							requiredTemp->links[requiredTemp->numLinks].type = RS_LINK_UFO;
							/* Set requirement-name (id). */
							requiredTemp->links[requiredTemp->numLinks].id = cgi->PoolStrDup(idToken, cp_campaignPool, 0);
							/* Set requirement-amount of item. */
							requiredTemp->links[requiredTemp->numLinks].amount = atoi(amountToken);
							cgi->Com_DPrintf(DEBUG_CLIENT, "RS_ParseTechnologies: require-ufo - %s - %i\n", requiredTemp->links[requiredTemp->numLinks].id, requiredTemp->links[requiredTemp->numLinks].amount);
							requiredTemp->numLinks++;
						}
					} else if (Q_streq(token, "antimatter")) {
						/* Defines what ufos need to be collected for this item to be researchable. */
						if (requiredTemp->numLinks < MAX_TECHLINKS) {
							/* Set requirement-type. */
							requiredTemp->links[requiredTemp->numLinks].type = RS_LINK_ANTIMATTER;
							/* Set requirement-amount of item. */
							token = Com_Parse(text);
							requiredTemp->links[requiredTemp->numLinks].amount = atoi(token);
							cgi->Com_DPrintf(DEBUG_CLIENT, "RS_ParseTechnologies: require-antimatter - %i\n", requiredTemp->links[requiredTemp->numLinks].amount);
							requiredTemp->numLinks++;
						}
					} else {
						cgi->Com_Printf("RS_ParseTechnologies: \"%s\" unknown requirement-type: \"%s\" - ignored.\n", name, token);
					}
				} while (*text);
			} else if (Q_streq(token, "up_chapter")) {
				/* UFOpaedia chapter */
				token = cgi->Com_EParse(text, errhead, name);
				if (!*text)
					return;

				if (*token) {
					/* find chapter */
					for (int i = 0; i < ccs.numChapters; i++) {
						if (Q_streq(token, ccs.upChapters[i].id)) {
							/* add entry to chapter */
							tech->upChapter = &ccs.upChapters[i];
							if (!ccs.upChapters[i].first) {
								ccs.upChapters[i].first = tech;
								ccs.upChapters[i].last = tech;
								tech->upPrev = nullptr;
								tech->upNext = nullptr;
							} else {
								/* get "last entry" in chapter */
								technology_t* techOld = ccs.upChapters[i].last;
								ccs.upChapters[i].last = tech;
								techOld->upNext = tech;
								ccs.upChapters[i].last->upPrev = techOld;
								ccs.upChapters[i].last->upNext = nullptr;
							}
							break;
						}
						if (i == ccs.numChapters)
							cgi->Com_Printf("RS_ParseTechnologies: \"%s\" - chapter \"%s\" not found.\n", name, token);
					}
				}
			} else if (Q_streq(token, "mail") || Q_streq(token, "mail_pre")) {
				techMail_t* mail;

				/* how many mails found for this technology
				 * used in UFOpaedia to check which article to display */
				tech->numTechMails++;

				if (tech->numTechMails > TECHMAIL_MAX)
					cgi->Com_Printf("RS_ParseTechnologies: more techmail-entries found than supported. \"%s\"\n",  name);

				if (Q_streq(token, "mail_pre")) {
					mail = &tech->mail[TECHMAIL_PRE];
				} else {
					mail = &tech->mail[TECHMAIL_RESEARCHED];
				}
				token = cgi->Com_EParse(text, errhead, name);
				if (!*text || *token != '{')
					return;

				/* grab the initial mail entry */
				token = cgi->Com_EParse(text, errhead, name);
				if (!*text || *token == '}')
					return;
				do {
					cgi->Com_ParseBlockToken(name, text, mail, valid_techmail_vars, cp_campaignPool, token);

					/* grab the next entry */
					token = cgi->Com_EParse(text, errhead, name);
					if (!*text)
						return;
				} while (*text && *token != '}');
				/* default model is navarre */
				if (mail->model == nullptr)
					mail->model = "characters/navarre";
			} else {
				if (!cgi->Com_ParseBlockToken(name, text, tech, valid_tech_vars, cp_campaignPool, token))
					cgi->Com_Printf("RS_ParseTechnologies: unknown token \"%s\" ignored (entry %s)\n", token, name);
			}
		}
	} while (*text);

	if (tech->provides) {
		hash = Com_HashKey(tech->provides, TECH_HASH_SIZE);
		/* link the variable in */
		/* techHashProvided should be null on the first run */
		tech->hashProvidedNext = techHashProvided[hash];
		/* set the techHashProvided pointer to the current tech */
		/* if there were already others in techHashProvided at position hash, they are now
		 * accessable via tech->next - loop until tech->next is null (the first tech
		 * at that position)
		 */
		techHashProvided[hash] = tech;
	} else {
		if (tech->type == RS_WEAPON || tech->type == RS_ARMOUR) {
			Sys_Error("RS_ParseTechnologies: weapon or armour tech without a provides property");
		}
		cgi->Com_DPrintf(DEBUG_CLIENT, "tech '%s' doesn't have a provides string\n", tech->id);
	}

	/* set the overall reseach time to the one given in the ufo-file. */
	tech->overallTime = tech->time;
}

static inline bool RS_IsValidTechIndex (int techIdx)
{
	if (techIdx == TECH_INVALID)
		return false;
	if (techIdx < 0 || techIdx >= ccs.numTechnologies)
		return false;
	if (techIdx >= MAX_TECHNOLOGIES)
		return false;

	return true;
}

/**
 * @brief Checks if the technology (tech-index) has been researched.
 * @param[in] techIdx index of the technology.
 * @return @c true if the technology has been researched, otherwise (or on error) false;
 * @sa RS_IsResearched_ptr
 */
bool RS_IsResearched_idx (int techIdx)
{
	if (!RS_IsValidTechIndex(techIdx))
		return false;

	if (ccs.technologies[techIdx].statusResearch == RS_FINISH)
		return true;

	return false;
}

/**
 * @brief Checks whether an item is already researched
 * @sa RS_IsResearched_idx
 * Call this function if you already hold a tech pointer
 */
bool RS_IsResearched_ptr (const technology_t* tech)
{
	if (tech && tech->statusResearch == RS_FINISH)
		return true;
	return false;
}

/**
 * @brief Returns the technology pointer for a tech index.
 * You can use this instead of "&ccs.technologies[techIdx]" to avoid having to check valid indices.
 * @param[in] techIdx Index in the global ccs.technologies[] array.
 * @return technology_t pointer or nullptr if an error occurred.
 */
technology_t* RS_GetTechByIDX (int techIdx)
{
	if (!RS_IsValidTechIndex(techIdx))
		return nullptr;
	return &ccs.technologies[techIdx];
}


/**
 * @brief return a pointer to the technology identified by given id string
 * @param[in] id Unique identifier of the tech as defined in the research.ufo file (e.g. "tech xxxx").
 * @return technology_t pointer or nullptr if an error occured.
 */
technology_t* RS_GetTechByID (const char* id)
{
	if (Q_strnull(id))
		return nullptr;

	unsigned hash = Com_HashKey(id, TECH_HASH_SIZE);
	for (technology_t* tech = techHash[hash]; tech; tech = tech->hashNext)
		if (!Q_strcasecmp(id, tech->id))
			return tech;

	cgi->Com_Printf("RS_GetTechByID: Could not find a technology with id \"%s\"\n", id);
	return nullptr;
}

/**
 * @brief returns a pointer to the item tech (as listed in "provides")
 * @param[in] idProvided Unique identifier of the object the tech is providing
 * @return The tech for the given object id or nullptr if not found
 */
technology_t* RS_GetTechByProvided (const char* idProvided)
{
	if (!idProvided)
		return nullptr;
	/* catch empty strings */
	if (idProvided[0] == '\0')
		return nullptr;

	unsigned hash = Com_HashKey(idProvided, TECH_HASH_SIZE);
	for (technology_t* tech = techHashProvided[hash]; tech; tech = tech->hashProvidedNext)
		if (!Q_strcasecmp(idProvided, tech->provides))
			return tech;

	cgi->Com_DPrintf(DEBUG_CLIENT, "RS_GetTechByProvided: %s\n", idProvided);
	/* if a building, probably needs another building */
	/* if not a building, catch nullptr where function is called! */
	return nullptr;
}

/**
 * @brief Searches for the technology that has the most scientists assigned in a given base.
 * @param[in] base In what base the tech should be researched.
 */
technology_t* RS_GetTechWithMostScientists (const struct base_s* base)
{
	if (!base)
		return nullptr;

	technology_t* tech = nullptr;
	int max = 0;
	for (int i = 0; i < ccs.numTechnologies; i++) {
		technology_t* tech_temp = RS_GetTechByIDX(i);
		if (tech_temp->statusResearch == RS_RUNNING && tech_temp->base == base) {
			if (tech_temp->scientists > max) {
				tech = tech_temp;
				max = tech->scientists;
			}
		}
	}

	/* this tech has at least one assigned scientist or is a nullptr pointer */
	return tech;
}

/**
 * @brief Returns the index (idx) of a "tech" entry given it's name.
 * @param[in] name the name of the tech
 */
int RS_GetTechIdxByName (const char* name)
{
	const unsigned hash = Com_HashKey(name, TECH_HASH_SIZE);

	for (technology_t* tech = techHash[hash]; tech; tech = tech->hashNext)
		if (!Q_strcasecmp(name, tech->id))
			return tech->idx;

	cgi->Com_Printf("RS_GetTechIdxByName: Could not find tech '%s'\n", name);
	return TECH_INVALID;
}

/**
 * @brief Returns the number of employees searching in labs in given base.
 * @param[in] base Pointer to the base
 * @sa B_ResetAllStatusAndCapacities_f
 * @note must not return 0 if hasBuilding[B_LAB] is false: used to update capacity
 */
int RS_CountScientistsInBase (const base_t* base)
{
	int counter = 0;

	for (int i = 0; i < ccs.numTechnologies; i++) {
		const technology_t* tech = &ccs.technologies[i];
		if (tech->base == base) {
			/* Get a free lab from the base. */
			counter += tech->scientists;
		}
	}

	return counter;
}

/**
 * @brief Remove all exceeding scientist.
 * @param[in, out] base Pointer to base where a scientist should be removed.
 */
void RS_RemoveScientistsExceedingCapacity (base_t* base)
{
	assert(base);

	/* Make sure current CAP_LABSPACE capacity is set to proper value */
	CAP_SetCurrent(base, CAP_LABSPACE, RS_CountScientistsInBase(base));

	while (CAP_GetFreeCapacity(base, CAP_LABSPACE) < 0) {
		technology_t* tech = RS_GetTechWithMostScientists(base);
		RS_RemoveScientist(tech, nullptr);
	}
}

/**
 * @brief Save callback for research and technologies
 * @param[out] parent XML Node structure, where we write the information to
 * @sa RS_LoadXML
 */
bool RS_SaveXML (xmlNode_t* parent)
{
	cgi->Com_RegisterConstList(saveResearchConstants);
	xmlNode_t* node = cgi->XML_AddNode(parent, SAVE_RESEARCH_RESEARCH);
	for (int i = 0; i < ccs.numTechnologies; i++) {
		const technology_t* t = RS_GetTechByIDX(i);

		xmlNode_t* snode = cgi->XML_AddNode(node, SAVE_RESEARCH_TECH);
		cgi->XML_AddString(snode, SAVE_RESEARCH_ID, t->id);
		cgi->XML_AddBoolValue(snode, SAVE_RESEARCH_STATUSCOLLECTED, t->statusCollected);
		cgi->XML_AddFloatValue(snode, SAVE_RESEARCH_TIME, t->time);
		cgi->XML_AddString(snode, SAVE_RESEARCH_STATUSRESEARCH, cgi->Com_GetConstVariable(SAVE_RESEARCHSTATUS_NAMESPACE, t->statusResearch));
		if (t->base)
			cgi->XML_AddInt(snode, SAVE_RESEARCH_BASE, t->base->idx);
		cgi->XML_AddIntValue(snode, SAVE_RESEARCH_SCIENTISTS, t->scientists);
		cgi->XML_AddBool(snode, SAVE_RESEARCH_STATUSRESEARCHABLE, t->statusResearchable);
		cgi->XML_AddDate(snode, SAVE_RESEARCH_PREDATE, t->preResearchedDate.getDateAsDays(), t->preResearchedDate.getTimeAsSeconds());
		cgi->XML_AddDate(snode, SAVE_RESEARCH_DATE, t->researchedDate.getDateAsDays(), t->researchedDate.getTimeAsSeconds());
		cgi->XML_AddInt(snode, SAVE_RESEARCH_MAILSENT, t->mailSent);

		/* save which techMails were read */
		/** @todo this should be handled by the mail system */
		for (int j = 0; j < TECHMAIL_MAX; j++) {
			if (t->mail[j].read) {
				xmlNode_t* ssnode = cgi->XML_AddNode(snode, SAVE_RESEARCH_MAIL);
				cgi->XML_AddInt(ssnode, SAVE_RESEARCH_MAIL_ID, j);
			}
		}
	}
	cgi->Com_UnregisterConstList(saveResearchConstants);

	return true;
}

/**
 * @brief Load callback for research and technologies
 * @param[in] parent XML Node structure, where we get the information from
 * @sa RS_SaveXML
 */
bool RS_LoadXML (xmlNode_t* parent)
{
	xmlNode_t* topnode;
	xmlNode_t* snode;
	bool success = true;

	topnode = cgi->XML_GetNode(parent, SAVE_RESEARCH_RESEARCH);
	if (!topnode)
		return false;

	cgi->Com_RegisterConstList(saveResearchConstants);
	for (snode = cgi->XML_GetNode(topnode, SAVE_RESEARCH_TECH); snode; snode = cgi->XML_GetNextNode(snode, topnode, "tech")) {
		const char* techString = cgi->XML_GetString(snode, SAVE_RESEARCH_ID);
		xmlNode_t* ssnode;
		int baseIdx;
		technology_t* t = RS_GetTechByID(techString);
		const char* type = cgi->XML_GetString(snode, SAVE_RESEARCH_STATUSRESEARCH);

		if (!t) {
			cgi->Com_Printf("......your game doesn't know anything about tech '%s'\n", techString);
			continue;
		}

		if (!cgi->Com_GetConstIntFromNamespace(SAVE_RESEARCHSTATUS_NAMESPACE, type, (int*) &t->statusResearch)) {
			cgi->Com_Printf("Invalid research status '%s'\n", type);
			success = false;
			break;
		}

		t->statusCollected = cgi->XML_GetBool(snode, SAVE_RESEARCH_STATUSCOLLECTED, false);
		t->time = cgi->XML_GetFloat(snode, SAVE_RESEARCH_TIME, 0.0);
		/* Prepare base-index for later pointer-restoration in RS_PostLoadInit. */
		baseIdx = cgi->XML_GetInt(snode, SAVE_RESEARCH_BASE, -1);
		if (baseIdx >= 0)
			/* even if the base is not yet loaded we can set the pointer already */
			t->base = B_GetBaseByIDX(baseIdx);
		t->scientists = cgi->XML_GetInt(snode, SAVE_RESEARCH_SCIENTISTS, 0);
		t->statusResearchable = cgi->XML_GetBool(snode, SAVE_RESEARCH_STATUSRESEARCHABLE, false);
		int date;
		int time;
		cgi->XML_GetDate(snode, SAVE_RESEARCH_PREDATE, &date, &time);
		t->preResearchedDate = DateTime(date, time);
		cgi->XML_GetDate(snode, SAVE_RESEARCH_DATE, &date, &time);
		t->researchedDate = DateTime(date, time);
		t->mailSent = (mailSentType_t)cgi->XML_GetInt(snode, SAVE_RESEARCH_MAILSENT, 0);

		/* load which techMails were read */
		/** @todo this should be handled by the mail system */
		for (ssnode = cgi->XML_GetNode(snode, SAVE_RESEARCH_MAIL); ssnode; ssnode = cgi->XML_GetNextNode(ssnode, snode, SAVE_RESEARCH_MAIL)) {
			const int j= cgi->XML_GetInt(ssnode, SAVE_RESEARCH_MAIL_ID, TECHMAIL_MAX);
			if (j < TECHMAIL_MAX)
				t->mail[j].read = true;
			else
				cgi->Com_Printf("......your save game contains unknown techmail ids... \n");
		}

#ifdef DEBUG
		if (t->statusResearch == RS_RUNNING && t->scientists > 0) {
			if (!t->base) {
				cgi->Com_Printf("No base but research is running and scientists are assigned");
				success = false;
				break;
			}
		}
#endif
	}
	cgi->Com_UnregisterConstList(saveResearchConstants);

	return success;
}

/**
 * @brief Returns true if the current base is able to handle research
 * @sa B_BaseInit_f
 * probably menu function, but not for research gui
 */
bool RS_ResearchAllowed (const base_t* base)
{
	assert(base);
	return !B_IsUnderAttack(base) && B_GetBuildingStatus(base, B_LAB) && E_CountHired(base, EMPL_SCIENTIST) > 0;
}

/**
 * @brief Checks the parsed tech data for errors
 * @return false if there are errors - true otherwise
 */
bool RS_ScriptSanityCheck (void)
{
	int i, error = 0;
	technology_t* t;

	for (i = 0, t = ccs.technologies; i < ccs.numTechnologies; i++, t++) {
		if (!t->name) {
			error++;
			cgi->Com_Printf("...... technology '%s' has no name\n", t->id);
		}
		if (!t->provides) {
			switch (t->type) {
			case RS_TECH:
			case RS_NEWS:
			case RS_LOGIC:
			case RS_ALIEN:
				break;
			default:
				error++;
				cgi->Com_Printf("...... technology '%s' doesn't provide anything\n", t->id);
				break;
			}
		}

		if (t->produceTime == 0) {
			switch (t->type) {
			case RS_TECH:
			case RS_NEWS:
			case RS_LOGIC:
			case RS_BUILDING:
			case RS_ALIEN:
				break;
			default:
				/** @todo error++; Crafts still give errors - are there any definitions missing? */
				cgi->Com_Printf("...... technology '%s' has zero (0) produceTime, is this on purpose?\n", t->id);
				break;
			}
		}

		if (t->type != RS_LOGIC && (!t->description.text[0] || t->description.text[0][0] == '_')) {
			if (!t->description.text[0])
				cgi->Com_Printf("...... technology '%s' has a strange 'description' value '%s'.\n", t->id, t->description.text[0]);
			else
				cgi->Com_Printf("...... technology '%s' has no 'description' value.\n", t->id);
		}
	}

	return !error;
}
