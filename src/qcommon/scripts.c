/**
 * @file scripts.c
 * @brief Ufo scripts used in client and server
 * @note interpreters for: object, inventory, equipment, name and team, damage
 */

/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/


#include "qcommon.h"
#include "../client/cl_research.h"

/*
==============================================================================
OBJECT DEFINITION INTERPRETER
==============================================================================
*/

static const char *skillNames[SKILL_NUM_TYPES - ABILITY_NUM_TYPES] = {
	"close",
	"heavy",
	"assault",
	"sniper",
	"explosive"
};

typedef enum objdefs {
	OD_WEAPON,
	OD_PROTECTION,
	OD_HARDNESS
} objdef_t;

static const value_t od_vals[] = {
	{"weapon_mod", V_NULL, 0},
	{"protection", V_NULL, 0},
	{"hardness", V_NULL, 0},

	{"name", V_TRANSLATION_STRING, offsetof(objDef_t, name)},
	{"model", V_STRING, offsetof(objDef_t, model)},
	{"image", V_STRING, offsetof(objDef_t, image)},
	{"type", V_STRING, offsetof(objDef_t, type)},
	{"category", V_CHAR, offsetof(objDef_t, category)},
	{"shape", V_SHAPE_SMALL, offsetof(objDef_t, shape)},
	{"scale", V_FLOAT, offsetof(objDef_t, scale)},
	{"center", V_VECTOR, offsetof(objDef_t, center)},
	{"weapon", V_BOOL, offsetof(objDef_t, weapon)},
	{"holdtwohanded", V_BOOL, offsetof(objDef_t, holdtwohanded)},
	{"firetwohanded", V_BOOL, offsetof(objDef_t, firetwohanded)},
	{"extends_item", V_STRING, offsetof(objDef_t, extends_item)},
	{"extension", V_BOOL, offsetof(objDef_t, extension)},
	{"headgear", V_BOOL, offsetof(objDef_t, headgear)},
	{"thrown", V_BOOL, offsetof(objDef_t, thrown)},
	{"ammo", V_INT, offsetof(objDef_t, ammo)},
	{"oneshot", V_BOOL, offsetof(objDef_t, oneshot)},
	{"deplete", V_BOOL, offsetof(objDef_t, deplete)},
	{"reload", V_INT, offsetof(objDef_t, reload)},
	{"price", V_INT, offsetof(objDef_t, price)},
	{"buytype", V_INT, offsetof(objDef_t, buytype)},
	{NULL, V_NULL, 0}
};

/* =========================================================== */

static const value_t fdps[] = {
	{"name", V_TRANSLATION_STRING, offsetof(fireDef_t, name)},
	{"shotorg", V_POS, offsetof(fireDef_t, shotOrg)},
	{"projtl", V_STRING, offsetof(fireDef_t, projectile)},
	{"impact", V_STRING, offsetof(fireDef_t, impact)},
	{"hitbody", V_STRING, offsetof(fireDef_t, hitBody)},
	{"firesnd", V_STRING, offsetof(fireDef_t, fireSound)},
	{"impsnd", V_STRING, offsetof(fireDef_t, impactSound)},
	{"bodysnd", V_STRING, offsetof(fireDef_t, hitBodySound)},
	{"bncsnd", V_STRING, offsetof(fireDef_t, bounceSound)},
	{"sndonce", V_BOOL, offsetof(fireDef_t, soundOnce)},
	{"gravity", V_BOOL, offsetof(fireDef_t, gravity)},
	{"launched", V_BOOL, offsetof(fireDef_t, launched)},
	{"rolled", V_BOOL, offsetof(fireDef_t, rolled)},
	{"reaction", V_BOOL, offsetof(fireDef_t, reaction)},
	{"delay", V_INT, offsetof(fireDef_t, delay)},
	{"bounce", V_INT, offsetof(fireDef_t, bounce)},
	{"bncfac", V_FLOAT, offsetof(fireDef_t, bounceFac)},
	{"speed", V_FLOAT, offsetof(fireDef_t, speed)},
	{"spread", V_POS, offsetof(fireDef_t, spread)},
	{"modif", V_FLOAT, offsetof(fireDef_t, modif)},
	{"crouch", V_FLOAT, offsetof(fireDef_t, crouch)},
/*	{"range", V_FLOAT, offsetof(fireDef_t, range)},*/
	{"shots", V_INT, offsetof(fireDef_t, shots)},
	{"ammo", V_INT, offsetof(fireDef_t, ammo)},
	{"rof", V_FLOAT, offsetof(fireDef_t, rof)},
	{"time", V_INT, offsetof(fireDef_t, time)},
	{"damage", V_POS, offsetof(fireDef_t, damage)},
	{"spldmg", V_POS, offsetof(fireDef_t, spldmg)},
/*	{"splrad", V_FLOAT, offsetof(fireDef_t, splrad)},*/
	{"dmgtype", V_DMGTYPE, offsetof(fireDef_t, dmgtype)},
	{"irgoggles", V_BOOL, offsetof(fireDef_t, irgoggles)},
	{NULL, 0, 0}
};


/**
 * @brief
 */
static void Com_ParseFire (char *name, char **text, fireDef_t * fd)
{
	const value_t *fdp;
	const char *errhead = "Com_ParseFire: unexptected end of file";
	char *token;

	/* get its body */
	token = COM_Parse(text);

	if (!*text || *token != '{') {
		Com_Printf("Com_ParseFire: fire definition \"%s\" without body ignored\n", name);
		return;
	}

	do {
		token = COM_EParse(text, errhead, name);
		if (!*text)
			return;
		if (*token == '}')
			return;

		for (fdp = fdps; fdp->string; fdp++)
			if (!Q_stricmp(token, fdp->string)) {
				/* found a definition */
				token = COM_EParse(text, errhead, name);
				if (!*text)
					return;

				Com_ParseValue(fd, token, fdp->type, fdp->ofs);
				break;
			}

		if (!fdp->string) {
			if (!Q_strncmp(token, "skill", 5)) {
				int skill;

				token = COM_EParse(text, errhead, name);
				if (!*text)
					return;

				for (skill = ABILITY_NUM_TYPES; skill < SKILL_NUM_TYPES; skill++)
					if (!Q_stricmp(skillNames[skill - ABILITY_NUM_TYPES], token)) {
						fd->weaponSkill = skill;
						break;
					}
				if (skill >= SKILL_NUM_TYPES)
					Com_Printf("Com_ParseFire: unknown weapon skill \"%s\" ignored (weapon %s)\n", token, name);
			} else if (!Q_strncmp(token, "range", 5)) {
				token = COM_EParse(text, errhead, name);
				if (!*text)
					return;
				fd->range = atof(token) * UNIT_SIZE;
			} else if (!Q_strncmp(token, "splrad", 6)) {
				token = COM_EParse(text, errhead, name);
				if (!*text)
					return;
				fd->splrad = atof(token) * UNIT_SIZE;
			} else
				Com_Printf("Com_ParseFire: unknown token \"%s\" ignored (weapon %s)\n", token, name);
		}
	} while (*text);
}


/**
 * @brief
 */
static void Com_ParseArmor (char *name, char **text, short *ad)
{
	const char *errhead = "Com_ParseFire: unexptected end of file";
	char *token;
	int i;

	/* get its body */
	token = COM_Parse(text);

	if (!*text || *token != '{') {
		Com_Printf("Com_ParseArmor: armor definition \"%s\" without body ignored\n", name);
		return;
	}

	do {
		token = COM_EParse(text, errhead, name);
		if (!*text)
			return;
		if (*token == '}')
			return;

		for (i = 0; i < csi.numDTs; i++)
			if (!Q_strncmp(token, csi.dts[i], MAX_VAR)) {
				token = COM_EParse(text, errhead, name);
				if (!*text)
					return;

				ad[i] = atoi(token);
				break;
			}

		if (i >= csi.numDTs)
			Com_Printf("Com_ParseArmor: unknown damage type \"%s\" ignored (in %s)\n", token, name);
	} while (*text);
}


/**
 * @brief
 */
static void Com_ParseItem (char *name, char **text)
{
	const char *errhead = "Com_ParseItem: unexptected end of file (weapon ";
	const value_t *val;
	objDef_t *od;
	char *token;
	int i;
	int weap_fds_idx, fd_idx;

	/* search for menus with same name */
	for (i = 0; i < csi.numODs; i++)
		if (!Q_strncmp(name, csi.ods[i].name, MAX_VAR))
			break;

	if (i < csi.numODs) {
		Com_Printf("Com_ParseItem: weapon def \"%s\" with same name found, second ignored\n", name);
		return;
	}

	/* initialize the object definition */
	od = &csi.ods[csi.numODs++];
	memset(od, 0, sizeof(objDef_t));

	/* default value is no ammo */
	memset(od->weap_idx, -1, sizeof(od->weap_idx));

	Q_strncpyz(od->id, name, MAX_VAR);

	/* get it's body */
	token = COM_Parse(text);

	if (!*text || *token != '{') {
		Com_Printf("Com_ParseItem: weapon def \"%s\" without body ignored\n", name);
		csi.numODs--;
		return;
	}

	do {
		token = COM_EParse(text, errhead, name);
		if (!*text)
			break;
		if (*token == '}')
			break;

		for (val = od_vals, i = 0; val->string; val++, i++)
			if (!Q_stricmp(token, val->string)) {
				/* found a definition */
				if (val->type != V_NULL) {
					/* parse a value */
					token = COM_EParse(text, errhead, name);
					if (!*text)
						break;

					Com_ParseValue(od, token, val->type, val->ofs);
				} else {
					/* parse fire definitions */
					switch (i) {
					case OD_WEAPON:
						/* Save the weapon id. */
						token = COM_Parse(text);
						Q_strncpyz(od->weap_id[od->numWeapons], token, MAX_VAR);

						/* get it's body */
						token = COM_Parse(text);

						if (!*text || *token != '{') {
							Com_Printf("Com_ParseItem: weapon_mod \"%s\" without body ignored\n", name);
							break;
						}

						if (od->numWeapons < MAX_WEAPONS_PER_OBJDEF) {
							weap_fds_idx = od->numWeapons;
							/* For parse each firedef entry for this weapon.  */
							do {
								token = COM_EParse(text, errhead, name);
								if (!*text)
									return;
								if (*token == '}')
									break;

								if (!Q_strncmp(token, "firedef", MAX_VAR)) {
									if (od->numFiredefs[weap_fds_idx] < MAX_FIREDEFS_PER_WEAPON) {
										fd_idx = od->numFiredefs[weap_fds_idx] ;
										/* Parse firemode into fd[IDXweapon][IDXfiremode] */
										Com_ParseFire(name, text, &od->fd[weap_fds_idx][fd_idx]);
										/* Self-link fd */
										od->fd[weap_fds_idx][fd_idx].fd_idx = fd_idx;
										/* Self-link weapn_mod */
										od->fd[weap_fds_idx][fd_idx].weap_fds_idx = weap_fds_idx;
										od->numFiredefs[od->numWeapons]++;
									} else {
										Com_Printf("Com_ParseItem: Too many firedefs at \"%s\". Max is %i\n", name, MAX_FIREDEFS_PER_WEAPON);
									}
								}
								/*
								else {
									Bad syntax.
								}
								*/
							} while (*text);
							od->numWeapons++;
						} else {
							Com_Printf("Com_ParseItem: Too many weapon_mod definitions at \"%s\". Max is %i\n", name, MAX_WEAPONS_PER_OBJDEF);
						}
						break;
					case OD_PROTECTION:
						Com_ParseArmor(name, text, od->protection);
						break;
					case OD_HARDNESS:
						Com_ParseArmor(name, text, od->hardness);
						break;
					default:
						break;
					}
				}
				break;
			}
		if (!val->string)
			Com_Printf("Com_ParseItem: unknown token \"%s\" ignored (weapon %s)\n", token, name);

	} while (*text);

	/* get size */
	for (i = 7; i >= 0; i--)
		if (od->shape & (0x01010101 << i))
			break;
	od->sx = i + 1;

	for (i = 3; i >= 0; i--)
		if (od->shape & (0xFF << (i * 8)))
			break;
	od->sy = i + 1;
}


/*
==============================================================================
INVENTORY DEFINITION INTERPRETER
==============================================================================
*/

static const value_t idps[] = {
	{"shape", V_SHAPE_BIG, offsetof(invDef_t, shape)}
	,
	/* only a single item */
	{"single", V_BOOL, offsetof(invDef_t, single)}
	,
	/* only a single item as weapon extension - single should be set, too */
	{"extension", V_BOOL, offsetof(invDef_t, extension)}
	,
	/* this is the armor container */
	{"armor", V_BOOL, offsetof(invDef_t, armor)}
	,
	/* this is the headgear container */
	{"headgear", V_BOOL, offsetof(invDef_t, headgear)}
	,
	/* allow everything to be stored in this container (e.g armor and weapons) */
	{"all", V_BOOL, offsetof(invDef_t, all)}
	,
	{"temp", V_BOOL, offsetof(invDef_t, temp)}
	,
	/* time units for moving something in */
	{"in", V_INT, offsetof(invDef_t, in)}
	,
	/* time units for moving something out */
	{"out", V_INT, offsetof(invDef_t, out)}
	,

	{NULL, 0, 0}
};

/**
 * @brief
 */
static void Com_ParseInventory (char *name, char **text)
{
	const char *errhead = "Com_ParseInventory: unexptected end of file (inventory ";
	invDef_t *id;
	const value_t *idp;
	char *token;
	int i;

	/* search for containers with same name */
	for (i = 0; i < csi.numIDs; i++)
		if (!Q_strncmp(name, csi.ids[i].name, MAX_VAR))
			break;

	if (i < csi.numIDs) {
		Com_Printf("Com_ParseInventory: inventory def \"%s\" with same name found, second ignored\n", name);
		return;
	}

	if (i >= MAX_INVDEFS) {
		Sys_Error("Too many inventory definitions - max allowed: %i\n", MAX_INVDEFS);
		return; /* never reached */
	}

	/* initialize the inventory definition */
	id = &csi.ids[csi.numIDs++];
	memset(id, 0, sizeof(invDef_t));

	Q_strncpyz(id->name, name, MAX_VAR);

	/* get it's body */
	token = COM_Parse(text);

	if (!*text || *token != '{') {
		Com_Printf("Com_ParseInventory: inventory def \"%s\" without body ignored\n", name);
		csi.numIDs--;
		return;
	}

	/* special IDs */
	if (!Q_strncmp(name, "right", 5))
		csi.idRight = id - csi.ids;
	else if (!Q_strncmp(name, "left", 4))
		csi.idLeft = id - csi.ids;
	else if (!Q_strncmp(name, "extension", 4))
		csi.idExtension = id - csi.ids;
	else if (!Q_strncmp(name, "belt", 4))
		csi.idBelt = id - csi.ids;
	else if (!Q_strncmp(name, "holster", 7))
		csi.idHolster = id - csi.ids;
	else if (!Q_strncmp(name, "backpack", 8))
		csi.idBackpack = id - csi.ids;
	else if (!Q_strncmp(name, "armor", 5))
		csi.idArmor = id - csi.ids;
	else if (!Q_strncmp(name, "floor", 5))
		csi.idFloor = id - csi.ids;
	else if (!Q_strncmp(name, "equip", 5))
		csi.idEquip = id - csi.ids;
	else if (!Q_strncmp(name, "headgear", 8))
		csi.idHeadgear = id - csi.ids;

	do {
		token = COM_EParse(text, errhead, name);
		if (!*text)
			return;
		if (*token == '}')
			return;

		for (idp = idps; idp->string; idp++)
			if (!Q_stricmp(token, idp->string)) {
				/* found a definition */
				token = COM_EParse(text, errhead, name);
				if (!*text)
					return;

				Com_ParseValue(id, token, idp->type, idp->ofs);
				break;
			}

		if (!idp->string)
			Com_Printf("Com_ParseInventory: unknown token \"%s\" ignored (inventory %s)\n", token, name);

	} while (*text);
}


/*
==============================================================================
EQUIPMENT DEFINITION INTERPRETER
==============================================================================
*/

#define MAX_NAMECATS	64
#define MAX_INFOSTRING	65536

typedef enum model_script_s {
	MODEL_PATH,
	MODEL_BODY,
	MODEL_HEAD,
	MODEL_SKIN,

	MODEL_NUM_TYPES
} model_script_t;

typedef struct nameCategory_s {
	char title[MAX_VAR];
	char *names[NAME_NUM_TYPES];
	int numNames[NAME_NUM_TYPES];
	char *models[NAME_LAST];
	int numModels[NAME_LAST];
} nameCategory_t;

typedef struct teamDef_s {
	char title[MAX_VAR];
	char *cats;
	int num;
} teamDef_t;

teamDesc_t teamDesc[MAX_TEAMDEFS];

static nameCategory_t nameCat[MAX_NAMECATS];
static teamDef_t teamDef[MAX_TEAMDEFS];
static char infoStr[MAX_INFOSTRING];
static char *infoPos;
static int numNameCats = 0;
static int numTeamDefs = 0;
int numTeamDesc = 0;

const char *name_strings[NAME_NUM_TYPES] = {
	"neutral",
	"female",
	"male",
	"lastname",
	"female_lastname",
	"male_lastname"
};


/**
 * @brief
 */
static void Com_ParseEquipment (char *name, char **text)
{
	const char *errhead = "Com_ParseEquipment: unexptected end of file (equipment ";
	equipDef_t *ed;
	char *token;
	int i, n;

	/* search for equipments with same name */
	for (i = 0; i < csi.numEDs; i++)
		if (!Q_strncmp(name, csi.eds[i].name, MAX_VAR))
			break;

	if (i < csi.numEDs) {
		Com_Printf("Com_ParseEquipment: equipment def \"%s\" with same name found, second ignored\n", name);
		return;
	}

	/* initialize the equipment definition */
	ed = &csi.eds[csi.numEDs++];
	memset(ed, 0, sizeof(equipDef_t));

	Q_strncpyz(ed->name, name, MAX_VAR);

	/* get it's body */
	token = COM_Parse(text);

	if (!*text || *token != '{') {
		Com_Printf("Com_ParseEquipment: equipment def \"%s\" without body ignored\n", name);
		csi.numEDs--;
		return;
	}

	do {
		token = COM_EParse(text, errhead, name);
		if (!*text || *token == '}')
			return;

		for (i = 0; i < csi.numODs; i++)
			if (!Q_strncmp(token, csi.ods[i].id, MAX_VAR)) {
				token = COM_EParse(text, errhead, name);
				if (!*text || *token == '}') {
					Com_Printf("Com_ParseEquipment: unexpected end of equipment def \"%s\"\n", name);
					return;
				}
				n = atoi(token);
				if (n)
					ed->num[i] = n;
				break;
			}

		if (i == csi.numODs)
			Com_Printf("Com_ParseEquipment: unknown token \"%s\" ignored (equipment %s)\n", token, name);

	} while (*text);
}


/*
==============================================================================
NAME AND TEAM DEFINITION INTERPRETER
==============================================================================
*/

/**
 * @brief
 * @param[in] gender 1 (female) or 2 (male)
 * @param[in] category country strings like: spanish_italian, german, russian and so on
 * @sa Com_GetModelAndName
 */
char *Com_GiveName (int gender, char *category)
{
	static char returnName[MAX_VAR];
	nameCategory_t *nc;
	char *pos;
	int i, j, name = 0;

	/* search the name */
	for (i = 0, nc = nameCat; i < numNameCats; i++, nc++)
		if (!Q_strncmp(category, nc->title, MAX_VAR)) {
#ifdef DEBUG
			for (j = 0; j < NAME_NUM_TYPES; j++)
				name += nc->numNames[j];
			if (!name)
				Sys_Error("Could not find any valid name definitions for category '%s'\n", category);
#endif
			/* found category */
			if (!nc->numNames[gender]) {
#ifdef DEBUG
				Com_DPrintf("No valid name definitions for gender %i in category '%s'\n", gender, category);
#endif
				return NULL;
			}
			name = rand() % nc->numNames[gender];

			/* skip names */
			pos = nc->names[gender];
			for (j = 0; j < name; j++)
				pos += strlen(pos) + 1;

			/* store the name */
			Q_strncpyz(returnName, pos, MAX_VAR);
			return returnName;
		}

	/* nothing found */
	return NULL;
}

/**
 * @brief
 * @param[in] type MODEL_PATH, MODEL_BODY, MODEL_HEAD, MODEL_SKIN (path, body, head, skin - see team_*.ufo)
 * @param[in] gender 1 (female) or 2 (male)
 * @param[in] category country strings like: spanish_italian, german, russian and so on
 * @sa Com_GetModelAndName
 */
char *Com_GiveModel (int type, int gender, char *category)
{
	nameCategory_t *nc;
	char *str;
	int i, j, num;

	/* search the name */
	for (i = 0, nc = nameCat; i < numNameCats; i++, nc++)
		if (!Q_strncmp(category, nc->title, MAX_VAR)) {
			/* found category */
			if (!nc->numModels[gender]) {
				Com_Printf("Com_GiveModel: no models defined for gender %i and category '%s'\n", gender, category);
				return NULL;
			}
			/* search one of the model definitions */
			num = (rand() % nc->numModels[gender]) * MODEL_NUM_TYPES;
			/* now go to the type entry from team_*.ufo */
			num += type;

			/* skip models and unwanted info */
			str = nc->models[gender];
			for (j = 0; j < num; j++)
				str += strlen(str) + 1;

			/* return the value */
			return str;
		}

	Com_Printf("Com_GiveModel: no models for gender %i and category '%s'\n", gender, category);
	/* nothing found */
	return NULL;
}

/**
 * @brief Assign 3D models and names to a character.
 * @param[in] team What team the character is on.
 * @param[in,out] chr The character that should get the paths to the differenbt models/skins.
 * @sa Com_GiveName
 * @sa Com_GiveModel
 */
int Com_GetModelAndName (char *team, character_t * chr)
{
	teamDef_t *td;
	char *str;
	int i, gender, category = 0;
	int retry = 1000;

	if (!numTeamDefs) {
		/*Sys_Error("Com_GetModelAndName: No team definitions found\n");*/
		return 0;
	}

	/* get team definition */
	for (i = 0; i < numTeamDefs; i++)
		if (!Q_strncmp(team, teamDef[i].title, MAX_VAR))
			break;

	if (i < numTeamDefs)
		td = &teamDef[i];
	else {
		Com_DPrintf("Com_GetModelAndName: could not find team '%s' in team definitions - searching name definitions now\n", team);
		/* search in name categories, if it isn't a team definition */
		td = NULL;
		for (i = 0; i < numNameCats; i++)
			if (!Q_strncmp(team, nameCat[i].title, MAX_VAR))
				break;
		if (i == numNameCats) {
			/* use default team */
			td = &teamDef[0];
			assert(td);
			Com_DPrintf("Com_GetModelAndName: could not find team '%s' in name definitions - using the first team definition now: '%s'\n", team, td->title);
		} else
			category = i;
	}

#ifdef DEBUG
	if (!td)
		Com_DPrintf("Com_GetModelAndName: Warning - this team (%s) could not be handled via aliencont code - no tech pointer will be available\n", team);
#endif

	/* get the models */
	while (retry--) {
		gender = rand() % NAME_LAST;
		if (td)
			category = (int) td->cats[rand() % td->num];

		for (i = 0; i < numTeamDesc; i++)
			if (!Q_strcmp(teamDesc[i].id, nameCat[category].title)) {
				/* transfered as byte - 0 means, not found - no -1 possible */
				chr->teamDesc = i + 1;
				/* set some team definition values to character, too */
				/* make these values available to the game lib */
				chr->weapons = teamDesc[i].weapons;
				chr->armor = teamDesc[i].armor;
			}

#ifdef DEBUG
		if (i == numTeamDesc)
			Com_DPrintf("Com_GetModelAndName: Warning - could not find a valid teamdesc for team '%s'\n", nameCat[category].title);
#endif

		/* get name */
		str = Com_GiveName(gender, nameCat[category].title);
		if (!str)
			continue;
		Q_strncpyz(chr->name, str, MAX_VAR);
		Q_strcat(chr->name, " ", MAX_VAR);
		str = Com_GiveName(gender + LASTNAME, nameCat[category].title);
		if (!str)
			continue;
		Q_strcat(chr->name, str, MAX_VAR);

		/* get model */
		str = Com_GiveModel(MODEL_PATH, gender, nameCat[category].title);
		if (!str)
			continue;
		Q_strncpyz(chr->path, str, MAX_VAR);

		str = Com_GiveModel(MODEL_BODY, gender, nameCat[category].title);
		if (!str)
			continue;
		Q_strncpyz(chr->body, str, MAX_VAR);

		str = Com_GiveModel(MODEL_HEAD, gender, nameCat[category].title);
		if (!str)
			continue;
		Q_strncpyz(chr->head, str, MAX_VAR);

		str = Com_GiveModel(MODEL_SKIN, gender, nameCat[category].title);
		if (!str)
			continue;
		return atoi(str);
	}
	return 0;
}

/**
 * @brief Parses "name" definition from team_* ufo script files
 * @sa Com_ParseActors
 * @sa Com_ParseScripts
 * @note fills the nameCat array
 * @note searches whether the actor id is already somewhere in the nameCat array
 * if not, it generates a new nameCat entry
 */
static void Com_ParseNames (char *title, char **text)
{
	nameCategory_t *nc;
	const char *errhead = "Com_ParseNames: unexptected end of file (names ";
	char *token;
	int i;

	/* check for additions to existing name categories */
	for (i = 0, nc = nameCat; i < numNameCats; i++, nc++)
		if (!Q_strncmp(nc->title, title, MAX_VAR))
			break;

	/* reset new category */
	if (i == numNameCats) {
		memset(nc, 0, sizeof(nameCategory_t));
		numNameCats++;
	}
	Q_strncpyz(nc->title, title, MAX_VAR);

	/* get name list body body */
	token = COM_Parse(text);

	if (!*text || *token != '{') {
		Com_Printf("Com_ParseNames: name def \"%s\" without body ignored\n", title);
		if (numNameCats - 1 == nc - nameCat)
			numNameCats--;
		return;
	}

	do {
		/* get the name type */
		token = COM_EParse(text, errhead, title);
		if (!*text)
			break;
		if (*token == '}')
			break;

		for (i = 0; i < NAME_NUM_TYPES; i++)
			if (!Q_strcmp(token, name_strings[i])) {
				/* initialize list */
				nc->names[i] = infoPos;
				nc->numNames[i] = 0;

				token = COM_EParse(text, errhead, title);
				if (!*text)
					break;
				if (*token != '{')
					break;

				do {
					/* get a name */
					token = COM_EParse(text, errhead, title);
					if (!*text)
						break;
					if (*token == '}')
						break;

					/* some names can be translateable */
					if (*token == '_')
						token++;
					strcpy(infoPos, token);
					infoPos += strlen(token) + 1;
					nc->numNames[i]++;
				} while (*text);

				/* lastname is different */
				/* fill female and male lastnames from neutral lastnames */
				if (i == NAME_LAST)
					for (i = NAME_NUM_TYPES - 1; i > NAME_LAST; i--) {
						nc->names[i] = nc->names[NAME_LAST];
						nc->numNames[i] = nc->numNames[NAME_LAST];
					}
				break;
			}

		if (i == NAME_NUM_TYPES)
			Com_Printf("Com_ParseNames: unknown token \"%s\" ignored (names %s)\n", token, title);

	} while (*text);

	if (nc->numNames[NAME_FEMALE] && !nc->numNames[NAME_FEMALE_LAST])
		Sys_Error("Com_ParseNames: '%s' has no female lastname category\n", nc->title);
	if (nc->numNames[NAME_MALE] && !nc->numNames[NAME_MALE_LAST])
		Sys_Error("Com_ParseNames: '%s' has no male lastname category\n", nc->title);
	if (nc->numNames[NAME_NEUTRAL] && !nc->numNames[NAME_LAST])
		Sys_Error("Com_ParseNames: '%s' has no neutral lastname category\n", nc->title);
}


/**
 * @brief Parses "actor" definition from team_* ufo script files
 * @sa Com_ParseNames
 * @sa Com_ParseScripts
 * @note fills the nameCat array
 * @note searches whether the actor id is already somewhere in the nameCat array
 * if not, it generates a new nameCat entry
 */
static void Com_ParseActors (char *title, char **text)
{
	nameCategory_t *nc;
	const char *errhead = "Com_ParseActors: unexptected end of file (actors ";
	char *token;
	int i, j;

	/* check for additions to existing name categories */
	for (i = 0, nc = nameCat; i < numNameCats; i++, nc++)
		if (!Q_strncmp(nc->title, title, MAX_VAR))
			break;

	/* reset new category */
	if (i == numNameCats) {
		if (numNameCats < MAX_NAMECATS) {
			memset(nc, 0, sizeof(nameCategory_t));
			numNameCats++;
		} else {
			Com_Printf("Too many name categories, '%s' ignored.\n", title);
			return;
		}
	}
	Q_strncpyz(nc->title, title, MAX_VAR);

	/* get name list body body */
	token = COM_Parse(text);

	if (!*text || *token != '{') {
		Com_Printf("Com_ParseActors: actor def \"%s\" without body ignored\n", title);
		if (numNameCats - 1 == nc - nameCat)
			numNameCats--;
		return;
	}

	do {
		/* get the name type */
		token = COM_EParse(text, errhead, title);
		if (!*text)
			break;
		if (*token == '}')
			break;

		for (i = 0; i < NAME_NUM_TYPES; i++)
			if (!Q_strcmp(token, name_strings[i])) {
				/* initialize list */
				nc->models[i] = infoPos;
				nc->numModels[i] = 0;
				token = COM_EParse(text, errhead, title);
				if (!*text)
					break;
				if (*token != '{')
					break;

				do {
					/* get the path, body, head and skin */
					for (j = 0; j < 4; j++) {
						token = COM_EParse(text, errhead, title);
						if (!*text)
							break;
						if (*token == '}')
							break;

						if (j == 3 && *token == '*')
							*infoPos++ = 0;
						else {
							strcpy(infoPos, token);
							infoPos += strlen(token) + 1;
						}
					}

					/* only add complete actor info */
					if (j == 4)
						nc->numModels[i]++;
					else
						break;
				}
				while (*text);
				break;
			}

		if (i == NAME_NUM_TYPES)
			Com_Printf("Com_ParseNames: unknown token \"%s\" ignored (actors %s)\n", token, title);

	} while (*text);
}

/** @brief possible teamdesc values (ufo-scriptfiles) */
static const value_t teamDescValues[] = {
	{"tech", V_STRING, offsetof(teamDesc_t, tech)} /**< tech id from research.ufo */
	,
	{"name", V_TRANSLATION2_STRING, offsetof(teamDesc_t, name)} /**< internal team name */
	,
	{"alien", V_BOOL, offsetof(teamDesc_t, alien)} /**< is this an alien? */
	,
	{"armor", V_BOOL, offsetof(teamDesc_t, armor)} /**< are these team members able to wear armor? */
	,
	{"weapons", V_BOOL, offsetof(teamDesc_t, weapons)} /**< are these team members able to use weapons? */
	,
	{NULL, 0, 0}
};


/**
 * @brief Parse the team descriptions (teamdesc) in the teams*.ufo files.
 */
static void Com_ParseTeamDesc (char *title, char **text)
{
	teamDesc_t *td;
	const char *errhead = "Com_ParseTeamDesc: unexptected end of file (teamdesc ";
	char *token;
	int i;
	const value_t *v;

	/* check whether team description already exists */
	for (i = 0, td = teamDesc; i < numTeamDesc; i++, td++)
		if (!Q_strncmp(td->id, title, MAX_VAR))
			break;

	/* reset new category */
	if (i >= MAX_TEAMDEFS) {
		Com_Printf("Too many team descriptions, '%s' ignored.\n", title);
		return;
	}

	if (i < numTeamDesc) {
		Com_Printf("Com_ParseTeamDesc: found defition with same name - second ignored\n");
		return;
	}

	td = &teamDesc[numTeamDesc++];
	memset(td, 0, sizeof(teamDesc_t));
	Q_strncpyz(td->id, title, MAX_VAR);
	td->armor = td->weapons = qtrue; /* default values */

	/* get name list body body */
	token = COM_Parse(text);

	if (!*text || *token != '{') {
		Com_Printf("Com_ParseTeamDesc: team desc \"%s\" without body ignored\n", title);
		if (numTeamDesc - 1 == td - teamDesc)
			numTeamDesc--;
		return;
	}

	do {
		/* get the name type */
		token = COM_EParse(text, errhead, title);
		if (!*text)
			break;
		if (*token == '}')
			break;

		for (v = teamDescValues; v->string; v++)
			if (!Q_strncmp(token, v->string, sizeof(v->string))) {
				/* found a definition */
				token = COM_EParse(text, errhead, title);
				if (!*text)
					return;

				Com_ParseValue(td, token, v->type, v->ofs);
				break;
			}

		if (!v->string)
			Com_Printf("Com_ParseTeamDesc: unknown token \"%s\" ignored (team %s)\n", token, title);
	} while (*text);
}

/**
 * @brief
 */
static void Com_ParseTeam (char *title, char **text)
{
	nameCategory_t *nc;
	teamDef_t *td;
	const char *errhead = "Com_ParseTeam: unexptected end of file (team ";
	char *token;
	int i;

	/* check for additions to existing name categories */
	for (i = 0, td = teamDef; i < numTeamDefs; i++, td++)
		if (!Q_strncmp(td->title, title, MAX_VAR))
			break;

	/* reset new category */
	if (i == numTeamDefs) {
		if (numTeamDefs < MAX_TEAMDEFS) {
			memset(td, 0, sizeof(teamDef_t));
			numTeamDefs++;
		} else {
			Com_Printf("Too many team definitions, '%s' ignored.\n", title);
			return;
		}
	}
	Q_strncpyz(td->title, title, MAX_VAR);

	/* get name list body body */
	token = COM_Parse(text);

	if (!*text || *token != '{') {
		Com_Printf("Com_ParseTeam: team def \"%s\" without body ignored\n", title);
		if (numTeamDefs - 1 == td - teamDef)
			numTeamDefs--;
		return;
	}

	/* initialize list */
	td->cats = infoPos;
	td->num = 0;

	do {
		/* get the name type */
		token = COM_EParse(text, errhead, title);
		if (!*text)
			break;
		if (*token == '}')
			break;

		if (*token == '{') {
			for (;;) {
				token = COM_EParse(text, errhead, title);
				if (*token == '}') {
					token = COM_EParse(text, errhead, title);
					break;
				}
				for (i = 0, nc = nameCat; i < numNameCats; i++, nc++)
					if (!Q_strncmp(token, nc->title, MAX_VAR)) {
						*infoPos++ = (char) i;
						td->num++;
						break;
					}
				if (i == numNameCats)
					Com_Printf("Com_ParseTeam: unknown token \"%s\" ignored (team %s)\n", token, title);
			}
			if (*token == '}')
				break;
		}
	} while (*text);
}

/*
==============================================================================
GAMETYPE INTERPRETER
==============================================================================
*/

gametype_t gts[MAX_GAMETYPES];
int numGTs = 0;

/** @brief possible gametype values for the gameserver (ufo-scriptfiles) */
static const value_t gameTypeValues[] = {
	{"name", V_TRANSLATION2_STRING, offsetof(gametype_t, name)} /**< translated game-type name for menu displaying */
	,
	{NULL, 0, 0}
};

static void Com_ParseGameTypes (char *name, char **text)
{
	const char *errhead = "Com_ParseGameTypes: unexptected end of file (gametype ";
	char *token;
	int i;
	const value_t *v;
	gametype_t* gt;
	cvarlist_t* cvarlist;

	/* get it's body */
	token = COM_Parse(text);
	if (!*text || *token != '{') {
		Com_Printf("Com_ParseGameTypes: gametype \"%s\" without body ignored\n", name);
		return;
	}

	/* search for game types with same name */
	for (i = 0; i < numGTs; i++)
		if (!Q_strncmp(token, gts[i].id, MAX_VAR))
			break;

	if (i == numGTs) {
		gt = &gts[numGTs++];
		memset(gt, 0, sizeof(gametype_t));
		Q_strncpyz(gt->id, name, MAX_VAR);
		if (numGTs >= MAX_GAMETYPES)
			Sys_Error("Com_ParseGameTypes: Too many damage types.\n");

		do {
			token = COM_EParse(text, errhead, name);
			if (!*text)
				break;
			if (*token == '}')
				break;

			for (v = gameTypeValues; v->string; v++)
				if (!Q_strncmp(token, v->string, sizeof(v->string))) {
					/* found a definition */
					token = COM_EParse(text, errhead, name);
					if (!*text)
						return;

					Com_ParseValue(gt, token, v->type, v->ofs);
					break;
				}

			if (!v->string) {
				if (*token != '{')
					Sys_Error("Com_ParseGameTypes: gametype \"%s\" without cvarlist\n", name);

				do {
					token = COM_EParse(text, errhead, name);
					if (!*text || *token == '}') {
						if (!gt->num_cvars)
							Sys_Error("Com_ParseGameTypes: gametype \"%s\" with empty cvarlist\n", name);
						else
							break;
					}
					/* initial pointer */
					cvarlist = &gt->cvars[gt->num_cvars++];
					if (gt->num_cvars >= MAX_CVARLISTINGAMETYPE)
						Sys_Error("Com_ParseGameTypes: gametype \"%s\" max cvarlist hit\n", name);
					Q_strncpyz(cvarlist->name, token, sizeof(cvarlist->name));
					token = COM_EParse(text, errhead, name);
					if (!*text || *token == '}')
						Sys_Error("Com_ParseGameTypes: gametype \"%s\" cvar \"%s\" with no value\n", name, cvarlist->name);
					Q_strncpyz(cvarlist->value, token, sizeof(cvarlist->value));
				} while (*text && *token != '}');
			}
		} while (*text);
	} else {
		Com_Printf("Com_ParseGameTypes: gametype \"%s\" with same already exists - ignore the second one\n", name);
		FS_SkipBlock(text);
	}
}

/*
==============================================================================
DAMAGE TYPES INTERPRETER
==============================================================================
*/

/**
 * @brief
 */
static void Com_ParseDamageTypes (char *name, char **text)
{
	const char *errhead = "Com_ParseDamageTypes: unexptected end of file (damagetype ";
	char *token;
	int i;

	/* get it's body */
	token = COM_Parse(text);

	if (!*text || *token != '{') {
		Com_Printf("Com_ParseDamageTypes: damage type list \"%s\" without body ignored\n", name);
		return;
	}

	do {
		token = COM_EParse(text, errhead, name);
		if (!*text)
			break;
		if (*token == '}')
			break;

		/* search for damage types with same name */
		for (i = 0; i < csi.numDTs; i++)
			if (!Q_strncmp(token, csi.dts[i], MAX_VAR))
				break;

		/* not found in the for loop */
		if (i == csi.numDTs) {
			/* gettext marker */
			if (*token == '_')
				token++;
			Q_strncpyz(csi.dts[csi.numDTs], token, MAX_VAR);

			/* special IDs */
			if (!Q_strncmp(token, "normal", 6))
				csi.damNormal = csi.numDTs;
			else if (!Q_strncmp(token, "blast", 5))
				csi.damBlast = csi.numDTs;
			else if (!Q_strncmp(token, "fire", 4))
				csi.damFire = csi.numDTs;
			else if (!Q_strncmp(token, "shock", 5))
				csi.damShock = csi.numDTs;
			else if (!Q_strncmp(token, "laser", 5))
				csi.damLaser = csi.numDTs;
			else if (!Q_strncmp(token, "plasma", 6))
				csi.damPlasma = csi.numDTs;
			else if (!Q_strncmp(token, "particle", 7))
				csi.damParticle = csi.numDTs;
			else if (!Q_strncmp(token, "stun", 4))
				csi.damStun = csi.numDTs;

			csi.numDTs++;
			if (csi.numDTs >= MAX_DAMAGETYPES)
				Sys_Error("Com_ParseDamageTypes: Too many damage types.\n");
		} else {
			Com_Printf("Com_ParseDamageTypes: damage type list \"%s\" with same already exists - ignore the second one\n", name);
			FS_SkipBlock(text);
			return;
		}
	} while (*text);
}


/*
==============================================================================
MAIN SCRIPT PARSING FUNCTION
==============================================================================
*/

/**
 * @brief Creates links to the technology entries in the pedia and to other items (i.e. ammo<->weapons)
 */
extern void Com_AddObjectLinks (void)
{
	objDef_t *od = NULL;
	int i;
	byte j, k;
#ifndef DEDICATED_ONLY
	technology_t *tech = NULL;
#endif

	for (i = 0, od = csi.ods; i < csi.numODs; i++, od++) {

#ifndef DEDICATED_ONLY
		/* Add links to technologies. */
		tech = RS_GetTechByProvided(od->id);
		od->tech = tech;
		if (!od->tech) {
			Com_Printf("Com_AddObjectLinks: Could not find a valid tech for item %s\n", od->id);
		}
#endif

		/* Add links to weapons. */
		for (j = 0; j < od->numWeapons; j++ ) {
			od->weap_idx[j] = Com_GetItemByID(od->weap_id[j]);
			assert(od->weap_idx[j] != -1);
			/* Back-link the obj-idx inside the fds */
			for (k = 0; k < od->numFiredefs[j]; k++ ) {
				od->fd[j][k].obj_idx = i;
			}
		}
	}
}

/**
 * @brief
 * @sa CL_ParseClientData
 * @sa CL_ParseScriptFirst
 * @sa CL_ParseScriptSecond
 * @sa Qcommon_Init
 */
extern void Com_ParseScripts (void)
{
	char *type, *name, *text;

	/* reset csi basic info */
	Com_InitCSI(&csi);
	csi.idRight = csi.idLeft = csi.idExtension = csi.idBackpack = csi.idBelt = csi.idHolster = csi.idArmor = csi.idFloor = csi.idEquip = csi.idHeadgear = NONE;
	csi.damNormal = csi.damBlast = csi.damFire = csi.damShock = csi.damLaser = csi.damPlasma = csi.damParticle = csi.damStun = NONE;

	/* I guess this is needed, too, if not please remove */
	csi.numODs = 0;
	csi.numIDs = 0;
	csi.numEDs = 0;
	csi.numDTs = 0;

	/* reset name and team def counters */
	numNameCats = 0;
	numTeamDefs = 0;
	infoPos = infoStr;

	/* pre-stage parsing */
	FS_BuildFileList("ufos/*.ufo");
	text = NULL;

	while ((type = FS_NextScriptHeader("ufos/*.ufo", &name, &text)) != 0)
		if (!Q_strncmp(type, "damagetypes", 11))
			Com_ParseDamageTypes(name, &text);
		else if (!Q_strncmp(type, "gametype", 8))
			Com_ParseGameTypes(name, &text);

	/* stage one parsing */
	FS_NextScriptHeader(NULL, NULL, NULL);
	text = NULL;

	while ((type = FS_NextScriptHeader("ufos/*.ufo", &name, &text)) != 0) {
		/* server/client scripts */
		if (!Q_strncmp(type, "item", 4))
			Com_ParseItem(name, &text);
		else if (!Q_strncmp(type, "inventory", 9))
			Com_ParseInventory(name, &text);
		else if (!Q_strncmp(type, "names", 5))
			Com_ParseNames(name, &text);
		else if (!Q_strncmp(type, "actors", 6))
			Com_ParseActors(name, &text);
		else if (!dedicated->value)
			CL_ParseClientData(type, name, &text);
	}

	/* Stage two parsing (weapon/inventory dependant stuff). */
	FS_NextScriptHeader(NULL, NULL, NULL);
	text = NULL;

	while ((type = FS_NextScriptHeader("ufos/*.ufo", &name, &text)) != 0) {
		/* server/client scripts */
		if (!Q_strncmp(type, "equipment", 9))
			Com_ParseEquipment(name, &text);
		else if (!Q_strncmp(type, "teamdesc", 8))
			Com_ParseTeamDesc(name, &text);
		else if (!Q_strncmp(type, "team", 4))
			Com_ParseTeam(name, &text);
	}

	Com_Printf("Shared Client/Server Info loaded\n");
}
