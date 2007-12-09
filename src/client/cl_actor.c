/**
 * @file cl_actor.c
 * @brief Actor related routines.
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
#include "cl_sound.h"

/* public */
le_t *selActor;
static fireDef_t *selFD;
static character_t *selChr;
static int selToHit;
static pos3_t mousePos;
/**
 * @brief If you want to change the z level of targetting and shooting,
 * use this value. Negative and positiv offsets are possible
 * @sa CL_ActorTargetAlign_f
 * @sa G_ClientShoot
 * @sa G_ShootGrenade
 * @sa G_ShootSingle
 */
static int mousePosTargettingAlign = 0;

int actorMoveLength;
invList_t invList[MAX_INVLIST];
static qboolean visible_firemode_list_left = qfalse;
static qboolean visible_firemode_list_right = qfalse;
static qboolean firemodes_change_display = qtrue; /* If this set to qfalse so CL_DisplayFiremodes_f will not attempt to hide the list */

typedef enum {
	RF_HAND,	/**< Stores the used hand (0=right, 1=left, -1=undef) */
	RF_FM,		/**< Stores the used firemode index. Max. number is MAX_FIREDEFS_PER_WEAPON -1=undef*/
	RF_WPIDX, 	/**< Stores the weapon idx in ods. (for faster access and checks) -1=undef */

	RF_MAX
} cl_reaction_firemode_type_t;

static int reactionFiremode[MAX_EDICTS][RF_MAX]; /** < Per actor: Stores the firemode to be used for reaction fire (if the fireDef allows that) See also reaction_firemode_type_t */

#define THIS_REACTION(actoridx, hand, fd_idx)	(reactionFiremode[actoridx][RF_HAND] == hand && reactionFiremode[actoridx][RF_FM] == fd_idx)
#define SANE_REACTION(actoridx)	(((reactionFiremode[actoridx][RF_HAND] >= 0) && (reactionFiremode[actoridx][RF_FM] >=0 && reactionFiremode[actoridx][RF_FM] < MAX_FIREDEFS_PER_WEAPON) && (reactionFiremode[actoridx][RF_FM] >= 0)))

static le_t *mouseActor;
static pos3_t mouseLastPos;
pos3_t mousePendPos; /* for double-click movement and confirmations ... */
static reactionmode_t selActorReactionState; /* keep track of reaction toggle */
static reactionmode_t selActorOldReactionState = R_FIRE_OFF; /* and another to help set buttons! */

/* to optimise painting of HUD in move-mode, keep track of some globals: */
static le_t *lastHUDActor; /* keeps track of selActor */
static int lastMoveLength; /* keeps track of actorMoveLength */
static int lastTU; /* keeps track of selActor->TU */

/* a cbuf string for each button_types_t */
static const char *shoot_type_strings[BT_NUM_TYPES] = {
	"pr\n",
	"reaction\n",
	"pl\n",
	"rr\n",
	"rl\n",
	"stand\n",
	"crouch\n",
	"headgear\n"
};

/**
 * @brief Defines the various states of a button.
 * @note Not all buttons do have all of these states (e.g. "unusable" is not very common).
 * @todo What does -1 mean here? it is used quite a bit
 */
typedef enum {
	BT_STATE_DISABLE,		/* 'Disabled' display (grey) */
	BT_STATE_DESELECT,	/* Normal display (blue) */
	BT_STATE_HIGHLIGHT,	/* Normal + highlight (blue + glow)*/
	BT_STATE_UNUSABLE	/* Normal + red (activated but unuseable aka "impossible") */
} weaponButtonState_t;

static weaponButtonState_t weaponButtonState[BT_NUM_TYPES];

/**
 * @brief Writes player action with its data
 */
void MSG_Write_PA (player_action_t player_action, int num, ...)
{
	va_list ap;
	struct dbuffer *msg = new_dbuffer();
	va_start(ap, num);
	NET_WriteFormat(msg, "bbs", clc_action, player_action, num);
	NET_V_WriteFormat(msg, pa_format[player_action], ap);
	va_end(ap);
	NET_WriteMsg(cls.stream, msg);
}

/*
==============================================================
ACTOR MENU UPDATING
==============================================================
*/

/**
 * @brief Return the skill string for the given skill level
 * @return skill string
 * @param[in] skill a skill value between 0 and MAX_SKILL (@todo: 0? never reached?)
 */
static const char *CL_GetSkillString (const int skill)
{
#ifdef DEBUG
	if (skill > MAX_SKILL) {
		Com_Printf("CL_GetSkillString: Skill is bigger than max allowed skill value (%i/%i)\n", skill, MAX_SKILL);
	}
#endif
	switch (skill*10/MAX_SKILL) {
	case 0:
		return _("Poor");
	case 1:
		return _("Mediocre");
	case 2:
		return _("Average");
	case 3:
		return _("Competent");
	case 4:
		return _("Proficient");
	case 5:
		return _("Very Good");
	case 6:
		return _("Highly Proficient");
	case 7:
		return _("Excellent");
	case 8:
		return _("Outstanding");
	case 9:
	case 10:
		return _("Superhuman");
	default:
		Com_Printf("CL_GetSkillString: Unknown skill: %i (index: %i)\n", skill, skill*10/MAX_SKILL);
		return "";
	}
}

/**
 * @brief Updates the character cvars for the given character.
 *
 * The models and stats that are displayed in the menu are stored in cvars.
 * These cvars are updated here when you select another character.
 *
 * @param chr Pointer to character_t (may not be null)
 * @sa CL_UGVCvars
 * @sa CL_ActorSelect
 */
void CL_CharacterCvars (character_t *chr)
{
	assert(chr);
	assert(chr->inv);	/** needed for CHRSH_CharGetBody and CHRSH_CharGetHead */

	Cvar_ForceSet("mn_name", chr->name);
	Cvar_ForceSet("mn_body", CHRSH_CharGetBody(chr));
	Cvar_ForceSet("mn_head", CHRSH_CharGetHead(chr));
	Cvar_ForceSet("mn_skin", va("%i", chr->skin));
	Cvar_ForceSet("mn_skinname", CL_GetTeamSkinName(chr->skin));

	Cvar_Set("mn_chrmis", va("%i", chr->assigned_missions));
	Cvar_Set("mn_chrkillalien", va("%i", chr->kills[KILLED_ALIENS]));
	Cvar_Set("mn_chrkillcivilian", va("%i", chr->kills[KILLED_CIVILIANS]));
	Cvar_Set("mn_chrkillteam", va("%i", chr->kills[KILLED_TEAM]));
	/* These two will be needed in Hire menu. */
	Cvar_Set("mn_employee_idx", va("%i", chr->empl_idx));
	Cvar_Set("mn_employee_type", va("%i", chr->empl_type));

	/* Display rank if not in multiplayer (numRanks==0) and the character has one. */
	if (chr->rank >= 0 && gd.numRanks) {
		Com_sprintf(messageBuffer, sizeof(messageBuffer), _("Rank: %s"), gd.ranks[chr->rank].name);
		Cvar_Set("mn_chrrank", messageBuffer);
		Cvar_Set("mn_chrrank_img", gd.ranks[chr->rank].image);
	} else {
		Cvar_Set("mn_chrrank", "");
		Cvar_Set("mn_chrrank_img", "");
	}

	Cvar_Set("mn_vpwr", va("%i", chr->skills[ABILITY_POWER]));
	Cvar_Set("mn_vspd", va("%i", chr->skills[ABILITY_SPEED]));
	Cvar_Set("mn_vacc", va("%i", chr->skills[ABILITY_ACCURACY]));
	Cvar_Set("mn_vmnd", va("%i", chr->skills[ABILITY_MIND]));
	Cvar_Set("mn_vcls", va("%i", chr->skills[SKILL_CLOSE]));
	Cvar_Set("mn_vhvy", va("%i", chr->skills[SKILL_HEAVY]));
	Cvar_Set("mn_vass", va("%i", chr->skills[SKILL_ASSAULT]));
	Cvar_Set("mn_vsnp", va("%i", chr->skills[SKILL_SNIPER]));
	Cvar_Set("mn_vexp", va("%i", chr->skills[SKILL_EXPLOSIVE]));
	Cvar_Set("mn_vhp", va("%i", chr->HP));
	Cvar_Set("mn_vhpmax", va("%i", chr->maxHP));

	Cvar_Set("mn_tpwr", va("%s (%i)", CL_GetSkillString(chr->skills[ABILITY_POWER]), chr->skills[ABILITY_POWER]));
	Cvar_Set("mn_tspd", va("%s (%i)", CL_GetSkillString(chr->skills[ABILITY_SPEED]), chr->skills[ABILITY_SPEED]));
	Cvar_Set("mn_tacc", va("%s (%i)", CL_GetSkillString(chr->skills[ABILITY_ACCURACY]), chr->skills[ABILITY_ACCURACY]));
	Cvar_Set("mn_tmnd", va("%s (%i)", CL_GetSkillString(chr->skills[ABILITY_MIND]), chr->skills[ABILITY_MIND]));
	Cvar_Set("mn_tcls", va("%s (%i)", CL_GetSkillString(chr->skills[SKILL_CLOSE]), chr->skills[SKILL_CLOSE]));
	Cvar_Set("mn_thvy", va("%s (%i)", CL_GetSkillString(chr->skills[SKILL_HEAVY]), chr->skills[SKILL_HEAVY]));
	Cvar_Set("mn_tass", va("%s (%i)", CL_GetSkillString(chr->skills[SKILL_ASSAULT]), chr->skills[SKILL_ASSAULT]));
	Cvar_Set("mn_tsnp", va("%s (%i)", CL_GetSkillString(chr->skills[SKILL_SNIPER]), chr->skills[SKILL_SNIPER]));
	Cvar_Set("mn_texp", va("%s (%i)", CL_GetSkillString(chr->skills[SKILL_EXPLOSIVE]), chr->skills[SKILL_EXPLOSIVE]));
	Cvar_Set("mn_thp", va("%i (%i)", chr->HP, chr->maxHP));
}

/**
 * @brief Updates the UGV cvars for the given "character".
 *
 * The models and stats that are displayed in the menu are stored in cvars.
 * These cvars are updated here when you select another character.
 *
 * @param chr Pointer to character_t (may not be null)
 * @sa CL_CharacterCvars
 * @sa CL_ActorSelect
 */
void CL_UGVCvars (character_t *chr)
{
	assert(chr);
	assert(chr->inv);	/** needed for CHRSH_CharGetBody and CHRSH_CharGetHead */

	Cvar_ForceSet("mn_name", chr->name);
	Cvar_ForceSet("mn_body", CHRSH_CharGetBody(chr));
	Cvar_ForceSet("mn_head", CHRSH_CharGetHead(chr));
	Cvar_ForceSet("mn_skin", va("%i", chr->skin));
	Cvar_ForceSet("mn_skinname", CL_GetTeamSkinName(chr->skin));

	Cvar_Set("mn_chrmis", va("%i", chr->assigned_missions));
	Cvar_Set("mn_chrkillalien", va("%i", chr->kills[KILLED_ALIENS]));
	Cvar_Set("mn_chrkillcivilian", va("%i", chr->kills[KILLED_CIVILIANS]));
	Cvar_Set("mn_chrkillteam", va("%i", chr->kills[KILLED_TEAM]));
	Cvar_Set("mn_chrrank_img", "");
	Cvar_Set("mn_chrrank", "");
	Cvar_Set("mn_chrrank_img", "");

	Cvar_Set("mn_vpwr", va("%i", chr->skills[ABILITY_POWER]));
	Cvar_Set("mn_vspd", va("%i", chr->skills[ABILITY_SPEED]));
	Cvar_Set("mn_vacc", va("%i", chr->skills[ABILITY_ACCURACY]));
	Cvar_Set("mn_vmnd", "0");
	Cvar_Set("mn_vcls", va("%i", chr->skills[SKILL_CLOSE]));
	Cvar_Set("mn_vhvy", va("%i", chr->skills[SKILL_HEAVY]));
	Cvar_Set("mn_vass", va("%i", chr->skills[SKILL_ASSAULT]));
	Cvar_Set("mn_vsnp", va("%i", chr->skills[SKILL_SNIPER]));
	Cvar_Set("mn_vexp", va("%i", chr->skills[SKILL_EXPLOSIVE]));

	Cvar_Set("mn_tpwr", va("%s (%i)", CL_GetSkillString(chr->skills[ABILITY_POWER]), chr->skills[ABILITY_POWER]));
	Cvar_Set("mn_tspd", va("%s (%i)", CL_GetSkillString(chr->skills[ABILITY_SPEED]), chr->skills[ABILITY_SPEED]));
	Cvar_Set("mn_tacc", va("%s (%i)", CL_GetSkillString(chr->skills[ABILITY_ACCURACY]), chr->skills[ABILITY_ACCURACY]));
	Cvar_Set("mn_tmnd", va("%s (0)", CL_GetSkillString(chr->skills[ABILITY_MIND])));
	Cvar_Set("mn_tcls", va("%s (%i)", CL_GetSkillString(chr->skills[SKILL_CLOSE]), chr->skills[SKILL_CLOSE]));
	Cvar_Set("mn_thvy", va("%s (%i)", CL_GetSkillString(chr->skills[SKILL_HEAVY]), chr->skills[SKILL_HEAVY]));
	Cvar_Set("mn_tass", va("%s (%i)", CL_GetSkillString(chr->skills[SKILL_ASSAULT]), chr->skills[SKILL_ASSAULT]));
	Cvar_Set("mn_tsnp", va("%s (%i)", CL_GetSkillString(chr->skills[SKILL_SNIPER]), chr->skills[SKILL_SNIPER]));
	Cvar_Set("mn_texp", va("%s (%i)", CL_GetSkillString(chr->skills[SKILL_EXPLOSIVE]), chr->skills[SKILL_EXPLOSIVE]));
}

/**
 * @brief Updates the global character cvars.
 */
static void CL_ActorGlobalCVars (void)
{
	le_t *le;
	char str[MAX_VAR];
	int i;

	Cvar_SetValue("mn_numaliensspotted", cl.numAliensSpotted);
	for (i = 0; i < MAX_TEAMLIST; i++) {
		le = cl.teamList[i];
		if (le && !(le->state & STATE_DEAD)) {
			/* the model name is the first entry in model_t */
			Cvar_Set(va("mn_head%i", i), (char *) le->model2);
			Com_sprintf(str, MAX_VAR, "%i", le->HP);
			Cvar_Set(va("mn_hp%i", i), str);
			Com_sprintf(str, MAX_VAR, "%i", le->maxHP);
			Cvar_Set(va("mn_hpmax%i", i), str);
			Com_sprintf(str, MAX_VAR, "%i", le->TU);
			Cvar_Set(va("mn_tu%i", i), str);
			Com_sprintf(str, MAX_VAR, "%i", le->maxTU);
			Cvar_Set(va("mn_tumax%i", i), str);
			Com_sprintf(str, MAX_VAR, "%i", le->morale);
			Cvar_Set(va("mn_morale%i",i), str);
			Com_sprintf(str, MAX_VAR, "%i", le->maxMorale);
			Cvar_Set(va("mn_moralemax%i",i), str);
			Com_sprintf(str, MAX_VAR, "%i", le->STUN);
			Cvar_Set(va("mn_stun%i", i), str);
		} else {
			Cvar_Set(va("mn_head%i", i), "");
			Cvar_Set(va("mn_hp%i", i), "0");
			Cvar_Set(va("mn_hpmax%i", i), "1");
			Cvar_Set(va("mn_tu%i", i), "0");
			Cvar_Set(va("mn_tumax%i", i), "1");
			Cvar_Set(va("mn_morale%i",i), "0");
			Cvar_Set(va("mn_moralemax%i",i), "1");
			Cvar_Set(va("mn_stun%i", i), "0");
		}
	}
}

/**
 * @brief Get state of the reaction-fire button.
 * @param[in] le Pointer to local entity structure, a soldier.
 * @return R_FIRE_MANY when STATE_REACTION_MANY.
 * @return R_FIRE_ONCE when STATE_REACTION_ONCE.
 * @return R_FIRE_OFF when no reaction fiR_
 * @sa CL_RefreshWeaponButtons
 * @sa CL_ActorUpdateCVars
 * @sa CL_ActorSelect
 */
static int CL_GetReactionState (le_t *le)
{
	if (le->state & STATE_REACTION_MANY)
		return R_FIRE_MANY;
	else if (le->state & STATE_REACTION_ONCE)
		return R_FIRE_ONCE;
	else
		return R_FIRE_OFF;
}

/**
 * @brief Calculate total reload time for selected actor.
 * @param[in] weapon_id Item in (currently only right) hand.
 * @return Time needed to reload or >= 999 if no suitable ammo found.
 * @note This routine assumes the time to reload a weapon
 * @note in the right hand is the same as the left hand.
 * @todo Distinguish between LEFT(selActor) and RIGHT(selActor).
 * @sa CL_RefreshWeaponButtons
 * @sa CL_CheckMenuAction
 */
static int CL_CalcReloadTime (int weapon_id)
{
	invList_t *ic;
	int container;
	int tu = 999;

	if (!selActor)
		return tu;

	for (container = 0; container < csi.numIDs; container++) {
		if (csi.ids[container].out < tu) {
			for (ic = selActor->i.c[container]; ic; ic = ic->next)
				if (INVSH_LoadableInWeapon(&csi.ods[ic->item.t], weapon_id)) {
					tu = csi.ids[container].out;
					break;
				}
		}
	}

	/* total TU cost is the sum of 3 numbers:
	 * TU for weapon reload + TU to get ammo out + TU to put ammo in hands */
	tu += csi.ods[weapon_id].reload + csi.ids[csi.idRight].in;
	return tu;
}

/**
 * @sa HighlightWeaponButton
 */
static void ClearHighlights (void)
{
#if 0
	int i;

	for (i = 0; i < BT_NUM_TYPES; i++)
		if (weaponButtonState[i] == BT_STATE_HIGHLIGHT) {
			weaponButtonState[i] = -1;
			return;
		}
#endif
}

#if 0
/**
 * @sa ClearHighlights
 */
static void HighlightWeaponButton (int button)
{
	char cbufText[MAX_VAR];

	/* only one button can be highlighted at once, so reset other buttons */
	ClearHighlights();

	Q_strncpyz(cbufText, "sel", MAX_VAR);
	Q_strcat(cbufText, shoot_type_strings[button], MAX_VAR);
	Cbuf_AddText(cbufText);
	weaponButtonState[button] = BT_STATE_HIGHLIGHT;
}
#endif

void CL_ResetWeaponButtons (void)
{
	memset(weaponButtonState, -1, sizeof(weaponButtonState));
}

/**
 * @brief Sets the display for a single weapon/reload HUD button.
 */
static void SetWeaponButton (int button, cl_reaction_firemode_type_t state)
{
	char cbufText[MAX_VAR];
	cl_reaction_firemode_type_t currentState = weaponButtonState[button];

	assert(button < BT_NUM_TYPES);

	/* No "switch" statement possible here for some non-obvious reason (gcc doesn't like constant ints here) :-/ */
	if ((state == currentState)
	||  (state == BT_STATE_HIGHLIGHT)) {
		/* Don't reset if it already is in current state or if highlighted,
		 * as HighlightWeaponButton deals with the highlighted state. */
		return;
	} else if (state == BT_STATE_DESELECT) {
		Q_strncpyz(cbufText, "desel", sizeof(cbufText));
	} else if (state == BT_STATE_DISABLE) {
		Q_strncpyz(cbufText, "dis", sizeof(cbufText));
	} else {
		Q_strncpyz(cbufText, "dis", sizeof(cbufText));
	}

	/* Connect confunc strings to the ones as defined in "menu nohud". */
	Q_strcat(cbufText, shoot_type_strings[button], sizeof(cbufText));

	Cbuf_AddText(cbufText);
	weaponButtonState[button] = state;
}

/**
 * @brief Returns the number of the actor in the teamlist.
 * @param[in] le The actor to search.
 * @return The number of the actor in the teamlist.
 */
static int CL_GetActorNumber (le_t * le)
{
	int actor_idx;

	assert(le);

	for (actor_idx = 0; actor_idx < cl.numTeamList; actor_idx++) {
		if (cl.teamList[actor_idx] == le)
			return actor_idx;
	}
	return -1;
}

/**
 * @brief Returns the character information for an actor in the teamlist.
 * @param[in] le The actor to search.
 * @return A pointer to a character struct.
 * @todo We really needs a better way to syn this up.
 */
static character_t *CL_GetActorChr (le_t * le)
{
	int actor_idx;
	aircraft_t *aircraft = cls.missionaircraft;
	int i, p;

	if (!aircraft) {
		Com_DPrintf(DEBUG_CLIENT, "CL_GetActorChr: No mission-aircraft found!\n");
		return NULL;
	}

	assert(le);

	actor_idx = CL_GetActorNumber(le);

	if (actor_idx < 0) {
		Com_DPrintf(DEBUG_CLIENT, "CL_GetActorChr: BAD ACTOR IDX!\n");
		return NULL;
	}

	/* Search in the aircraft team (we skip unused entries) for this actor. */
	for (i = 0, p = 0; i < aircraft->maxTeamSize; i++) {
		if (aircraft->teamIdxs[i] != -1) {
			if (actor_idx == p) {
				return &gd.employees[aircraft->teamTypes[actor_idx]][aircraft->teamIdxs[actor_idx]].chr;
			}
			p++;
		}
	}
	Com_DPrintf(DEBUG_CLIENT, "CL_GetActorChr: no character info found!\n");
	return NULL;
}

/**
 * @brief Makes all entries of the firemode lists invisible.
 */
static void HideFiremodes (void)
{
	int i;

	visible_firemode_list_left = qfalse;
	visible_firemode_list_right = qfalse;
	for (i = 0; i < MAX_FIREDEFS_PER_WEAPON; i++) {
		Cbuf_AddText(va("set_left_inv%i\n", i));
		Cbuf_AddText(va("set_right_inv%i\n", i));
	}

}


/**
 * @brief Stores the given firedef index and object index for reaction fire and sends in over the network as well.
 * @param[in] actor_idx Index of an actor.
 * @param[in] handidx Index of hand with item, which will be used for reactionfiR_ Possible hand indices: 0=right, 1=right, -1=undef
 * @param[in] fd_idx Index of firedefinition for an item in given hand.
 */
static void CL_SetReactionFiremode (int actor_idx, int handidx, int obj_idx, int fd_idx)
{
	le_t *le;

	if (cls.team != cl.actTeam) {	/**< Not our turn */
		/* This check is just here (additional to the one in CL_DisplayFiremodes_f) in case a possible situation was missed. */
		Com_DPrintf(DEBUG_CLIENT, "CL_SetReactionFiremode: Function called on enemy/other turn.\n");
		return;
	}

	if (actor_idx < 0 || actor_idx >= MAX_EDICTS) {
		Com_DPrintf(DEBUG_CLIENT, "CL_SetReactionFiremode: Actor index is negative or out of bounds. Abort.\n");
		return;
	}

	if (handidx < -1 || handidx > 1) {
		Com_DPrintf(DEBUG_CLIENT, "CL_SetReactionFiremode: Bad hand index given. Abort.\n");
		return;
	}

	assert(actor_idx < MAX_TEAMLIST);

	le = cl.teamList[actor_idx];
	Com_DPrintf(DEBUG_CLIENT, "CL_SetReactionFiremode: actor:%i entnum:%i hand:%i fd:%i\n", actor_idx,le->entnum, handidx, fd_idx);

	reactionFiremode[actor_idx][RF_HAND] = handidx;	/* Store the given hand. */
	reactionFiremode[actor_idx][RF_FM] = fd_idx;	/* Store the given firemode for this hand. */
	MSG_Write_PA(PA_REACT_SELECT, le->entnum, handidx, fd_idx); /* Send hand and firemode to server-side storage as well. See g_local.h for moR_ */
	reactionFiremode[actor_idx][RF_WPIDX] = obj_idx;	/* Store the weapon-idx of the object in the hand (for faster access). */
}

/**
 * @brief Sets the display for a single weapon/reload HUD button.
 * @param[in] fd Pointer to the firedefinition/firemode to be displayed.
 * @param[in] hand Which list to display: 'l' for left hand list, 'r' for right hand list.
 * @param[in] status Display the firemode clickable/active (1) or inactive (0).
 */
static void CL_DisplayFiremodeEntry (fireDef_t *fd, char hand, qboolean status)
{
	/* char cbufText[MAX_VAR]; */
	if (!fd)
		return;

	if (!selActor)
		return;

	if (hand == 'r') {
		Cbuf_AddText(va("set_right_vis%i\n", fd->fd_idx)); /* Make this entry visible (in case it wasn't). */

		if (status) {
			Cbuf_AddText(va("set_right_a%i\n", fd->fd_idx));
		} else {
			Cbuf_AddText(va("set_right_ina%i\n", fd->fd_idx));
		}

		if (selActor->TU > fd->time)
			Cvar_Set(va("mn_r_fm_tt_tu%i", fd->fd_idx),va(_("Remaining TUs: %i"),selActor->TU - fd->time));
		else
			Cvar_Set(va("mn_r_fm_tt_tu%i", fd->fd_idx),_("No remaining TUs left after shot."));

		Cvar_Set(va("mn_r_fm_name%i", fd->fd_idx),  va("%s", fd->name));
		Cvar_Set(va("mn_r_fm_tu%i", fd->fd_idx),va(_("TU: %i"),  fd->time));
		Cvar_Set(va("mn_r_fm_shot%i", fd->fd_idx), va(_("Shots:%i"), fd->ammo));

	} else if (hand == 'l') {
		Cbuf_AddText(va("set_left_vis%i\n", fd->fd_idx)); /* Make this entry visible (in case it wasn't). */

		if (status) {
			Cbuf_AddText(va("set_left_a%i\n", fd->fd_idx));
		} else {
			Cbuf_AddText(va("set_left_ina%i\n", fd->fd_idx));
		}

		if (selActor->TU > fd->time)
			Cvar_Set(va("mn_l_fm_tt_tu%i", fd->fd_idx),va(_("Remaining TUs: %i"),selActor->TU - fd->time));
		else
			Cvar_Set(va("mn_l_fm_tt_tu%i", fd->fd_idx),_("No remaining TUs left after shot."));

		Cvar_Set(va("mn_l_fm_name%i", fd->fd_idx),  va("%s", fd->name));
		Cvar_Set(va("mn_l_fm_tu%i", fd->fd_idx),va(_("TU: %i"),  fd->time));
		Cvar_Set(va("mn_l_fm_shot%i", fd->fd_idx), va(_("Shots:%i"), fd->ammo));
	} else {
		Com_Printf("CL_DisplayFiremodeEntry: Bad hand [l|r] defined: '%c'\n", hand);
		return;
	}
}

/**
 * @brief Returns the weapon its ammo and the firemodes-index inside the ammo for a given hand.
 * @param[in] actor The pointer to the actor we want to get the data from.
 * @param[in] hand Which weapon(-hand) to use [l|r].
 * @param[out] weapon The weapon in the hand.
 * @param[out] ammo The ammo used in the weapon (is the same as weapon for grenades and similar).
 * @param[out] weap_fd_idx weapon_mod index in the ammo for the weapon (objDef.fd[x][])
 */
static void CL_GetWeaponAndAmmo (le_t *actor, char hand, objDef_t **weapon, objDef_t **ammo, int *weap_fd_idx)
{
	invList_t *invlist_weapon = NULL;

	if (!actor)
		return;

	if (hand == 'r')
		invlist_weapon = RIGHT(actor);
	else
		invlist_weapon = LEFT(actor);

	if (!invlist_weapon || invlist_weapon->item.t == NONE)
		return;

	*weapon = &csi.ods[invlist_weapon->item.t];

	if (!weapon)
		return;

	if ((*weapon)->numWeapons) /* @todo: "|| invlist_weapon->item.m == NONE" ... actually what does a negative number for ammo mean? */
		*ammo = *weapon; /* This weapon doesn't need ammo it already has firedefs */
	else
		*ammo = &csi.ods[invlist_weapon->item.m];

	*weap_fd_idx = FIRESH_FiredefsIDXForWeapon(*ammo, invlist_weapon->item.t);
}

/**
 * @brief Checks if a there is a weapon in the hand that can be used for reaction fiR_
 * @param[in] actor What actor to check.
 * @param[in] hand Which hand to check: 'l' for left hand, 'r' for right hand.
 */
static qboolean CL_WeaponWithReaction (le_t *actor, char hand)
{
	objDef_t *ammo = NULL;
	objDef_t *weapon = NULL;
	int weap_fd_idx = -1;
	int i;

	/* Get ammo and weapon-index in ammo. */
	CL_GetWeaponAndAmmo(actor, hand, &weapon, &ammo, &weap_fd_idx);

	if (weap_fd_idx == -1) {
		Com_DPrintf(DEBUG_CLIENT, "CL_WeaponWithReaction: No weapondefinition in ammo found\n");
		return qfalse;
	}

	if (!weapon || !ammo)
		return qfalse;

	/* check ammo for reaction-enabled firemodes */
	for (i = 0; i < ammo->numFiredefs[weap_fd_idx]; i++) {
		if (ammo->fd[weap_fd_idx][i].reaction) {
			return qtrue;
		}
	}

	return qfalse;
}

/**
 * @brief Display 'useable" (blue) reaction buttons.
 * @param[in] actor the actor to check for his reaction state.
 */
static void CL_DisplayPossibleReaction (le_t *actor)
{
	if (!actor)
		return;

	if (actor != selActor) {
		Com_DPrintf(DEBUG_CLIENT, "CL_DisplayPossibleReaction: Something went wront, given actor does not equal the currently selectd actor!\n");
		return;
	}

	/* Display 'useable" (blue) reaction buttons
	 * Code also used in CL_ActorToggleReaction_f */
	switch (CL_GetReactionState(actor)) {
	case R_FIRE_ONCE:
		Cbuf_AddText("startreactiononce\n");
		break;
	case R_FIRE_MANY:
		Cbuf_AddText("startreactionmany\n");
		break;
	default:
		break;
	}
}

/**
 * @brief Display 'impossible" (red) reaction buttons.
 * @param[in] actor the actor to check for his reaction state.
 * @return qtrue if "turn rf off" message was sent otherwise qfalse.
 * @todo I think the MSG_Write_PA and return stuff might be unneccesary now after the RPG-multiplayer fix. Nothing urgent though.
 */
static qboolean CL_DisplayImpossibleReaction (le_t *actor)
{
	if (!actor)
		return qfalse;

	if (actor != selActor) {
		Com_DPrintf(DEBUG_CLIENT, "CL_DisplayImpossibleReaction: Something went wront, given actor does not equal the currently selected actor!\n");
		return qfalse;
	}

	/* Display 'impossible" (red) reaction buttons */
	switch (CL_GetReactionState(actor)) {
	case R_FIRE_ONCE:
		weaponButtonState[BT_REACTION] = BT_STATE_UNUSABLE;	/* Set but not used anywhere (yet) */
		Cbuf_AddText("startreactiononce_impos\n");
		break;
	case R_FIRE_MANY:
		weaponButtonState[BT_REACTION] = BT_STATE_UNUSABLE;	/* Set but not used anywhere (yet) */
		Cbuf_AddText("startreactionmany_impos\n");
		break;
	default:
		/* Send the "turn rf off" message just in case. */
		MSG_Write_PA(PA_STATE, actor->entnum,  ~STATE_REACTION);
		return qtrue;
	}

	return qfalse;
}

/**
 * @brief Updates the information in reactionFiremode for the selected actor with the given data from the parameters.
 * @param[in] hand Which weapon(-hand) to use (l|r).
 * @param[in] active_firemode Set this to the firemode index you want to activate or set it to -1 if the default one (currently the first one found) should be used.
 */
static void CL_UpdateReactionFiremodes (char hand, int actor_idx, int active_firemode)
{
	objDef_t *weapon = NULL;
	objDef_t *ammo = NULL;
	int weap_fd_idx = -1;
	int i = -1;

	int handidx = (hand == 'r') ? 0 : 1;

	if (actor_idx < 0) {
		Com_DPrintf(DEBUG_CLIENT, "CL_UpdateReactionFiremodes: Invalid (negative) actor index given!\n");
		return;
	}

	CL_GetWeaponAndAmmo(cl.teamList[actor_idx], hand, &weapon, &ammo, &weap_fd_idx);

	if (weap_fd_idx == -1) {
		CL_DisplayImpossibleReaction(cl.teamList[actor_idx]);
		Com_DPrintf(DEBUG_CLIENT, "CL_UpdateReactionFiremodes: No weap_fd_idx found for %c hand.\n", hand);
		return;
	}

	if (!weapon) {
		CL_DisplayImpossibleReaction(cl.teamList[actor_idx]);
		Com_DPrintf(DEBUG_CLIENT, "CL_UpdateReactionFiremodes: No weapon found for %c hand.\n", hand);
		return;
	}

	/* ammo is definitly set here - otherwise the both checks above would have
	 * left this function already */
	if (!RS_ItemIsResearched(csi.ods[ammo->weap_idx[weap_fd_idx]].id)) {
		Com_DPrintf(DEBUG_CLIENT, "CL_UpdateReactionFiremodes: Weapon '%s' not researched, can't use for reaction fiR_\n", csi.ods[ammo->weap_idx[weap_fd_idx]].id);
		return;
	}

	if (active_firemode >= MAX_FIREDEFS_PER_WEAPON) {
		Com_Printf("CL_UpdateReactionFiremodes: Firemode index to big (%i). Highest possible number is %i.\n", active_firemode, MAX_FIREDEFS_PER_WEAPON-1);
		return;
	}

	if (active_firemode < 0) {
		/* Set default reaction firemode for this hand (active_firemode=-1) */
		i = FIRESH_GetDefaultReactionFire(ammo, weap_fd_idx);

		if (i >= 0) {
			Com_DPrintf(DEBUG_CLIENT, "CL_UpdateReactionFiremodes: DEBUG i>=0\n");
			/* Found useable firemode for the weapon in this hand */
			CL_SetReactionFiremode(actor_idx, handidx, ammo->weap_idx[weap_fd_idx], i);

			/* Display 'useable" (blue) reaction buttons */
			CL_DisplayPossibleReaction(cl.teamList[actor_idx]);
		} else {
			Com_DPrintf(DEBUG_CLIENT, "CL_UpdateReactionFiremodes: DEBUG else (i>=0)\n");
			/* weapon in hand not RF-capable. */
			if (CL_WeaponWithReaction(cl.teamList[actor_idx], (hand == 'r') ? 'l' : 'r')) {
				/* The other hand has useable firemodes for RF, use it instead. */
				CL_UpdateReactionFiremodes((hand == 'r') ? 'l' : 'r', actor_idx, -1);

				Com_DPrintf(DEBUG_CLIENT, "CL_UpdateReactionFiremodes: DEBUG inverse hand\n");
				/* Display 'useable" (blue) reaction buttons */
				CL_DisplayPossibleReaction(cl.teamList[actor_idx]);
			} else {
				Com_DPrintf(DEBUG_CLIENT, "CL_UpdateReactionFiremodes: DEBUG no rf-items in hands\n");
				/* No RF-capable item in either hand. */

				/* Display "impossible" (red) reaction button or disable button. */
				CL_DisplayImpossibleReaction(cl.teamList[actor_idx]);
				/* Set RF-mode info to undef. */
				CL_SetReactionFiremode(actor_idx, -1, NONE, -1);
			}
		}
		/* The rest of this function assumes that active_firemode is bigger than -1 ->  finish. */
		return;
	}

	Com_DPrintf(DEBUG_CLIENT, "CL_UpdateReactionFiremodes: act%i handidx%i weapfdidx%i\n", actor_idx, handidx, weap_fd_idx);
	if (reactionFiremode[actor_idx][RF_WPIDX] == ammo->weap_idx[weap_fd_idx]
	 && reactionFiremode[actor_idx][RF_HAND] == handidx) {
		if (ammo->fd[weap_fd_idx][active_firemode].reaction) {
			if (reactionFiremode[actor_idx][RF_FM] == active_firemode)
				/* Weapon is the same, firemode is already selected and reaction-usable. Nothing to do. */
				return;
		} else {
			/* Weapon is the same and firemode is not reaction-usable*/
			return;
		}
	}

	/* Search for a (reaction) firemode with the given index and store/send it. */
	CL_SetReactionFiremode(actor_idx, -1, NONE, -1);
	for (i = 0; i < ammo->numFiredefs[weap_fd_idx]; i++) {
		if (ammo->fd[weap_fd_idx][i].reaction) {
			if (i == active_firemode) {
				CL_SetReactionFiremode(actor_idx, handidx, ammo->weap_idx[weap_fd_idx], i);
				return;
			}
		}
	}
}

/**
 * @brief Sets the reaction-firemode of an actor/soldier to it's default value on client- and server-side.
 * @param[in] actor_idx Index of the actor to set the firemode for.
 * @param[in] hand Which weapon(-hand) to try first for reaction-firemode (r|l).
 */
void CL_SetDefaultReactionFiremode (int actor_idx, char hand)
{
	if (actor_idx < 0 || actor_idx >= MAX_EDICTS) {
		Com_DPrintf(DEBUG_CLIENT, "CL_SetDefaultReactionFiremode: Actor index is negative or out of bounds. Abort.\n");
		return;
	}

	if (hand == 'r') {
		/* Set default firemode (try right hand first, then left hand). */
		/* Try to set right hand */
		CL_UpdateReactionFiremodes('r', actor_idx, -1);
		if (!SANE_REACTION(actor_idx)) {
			/* If that failed try to set left hand. */
			CL_UpdateReactionFiremodes('l', actor_idx, -1);
#if 0
			if (!SANE_REACTION(actor_idx)) {
				/* If that fails as well set firemode to undef. */
				CL_SetReactionFiremode (actor_idx, -1, NONE, -1);
			}
#endif
		}
	} else {
		/* Set default firemode (try left hand first, then right hand). */
		/* Try to set left hand. */
		CL_UpdateReactionFiremodes('l', actor_idx, -1);
		if (!SANE_REACTION(actor_idx)) {
			/* If that failed try to set right hand. */
			CL_UpdateReactionFiremodes('r', actor_idx, -1);
#if 0
			if (!SANE_REACTION(actor_idx)) {
				/* If that fails as well set firemode to undef. */
				CL_SetReactionFiremode (actor_idx, -1, NONE, -1);
			}
#endif
		}
	}
}

/**
 * @brief Displays the firemodes for the given hand.
 * @note Console command: list_firemodes
 */
void CL_DisplayFiremodes_f (void)
{
	int actor_idx;
	objDef_t *weapon = NULL;
	objDef_t *ammo = NULL;
	int weap_fd_idx = -1;
	int i;
	const char *hand;

	if (!selActor)
		return;

	if (cls.team != cl.actTeam) {	/**< Not our turn */
		HideFiremodes();
		visible_firemode_list_right = qfalse;
		visible_firemode_list_left = qfalse;
		return;
	}

	if (Cmd_Argc() < 2) { /* no argument given */
		hand = "r";
	} else {
		hand = Cmd_Argv(1);
	}

	if (hand[0] != 'r' && hand[0] != 'l') {
		Com_Printf("Usage: %s [l|r]\n", Cmd_Argv(0));
		return;
	}

	CL_GetWeaponAndAmmo(selActor, hand[0], &weapon, &ammo, &weap_fd_idx);
	if (weap_fd_idx == -1)
		return;

	Com_DPrintf(DEBUG_CLIENT, "CL_DisplayFiremodes_f: %s | %s | %i\n", weapon->name, ammo->name, weap_fd_idx);

	if (!weapon || !ammo) {
		Com_DPrintf(DEBUG_CLIENT, "CL_DisplayFiremodes_f: no weapon or ammo found.\n");
		return;
	}

	Com_DPrintf(DEBUG_CLIENT, "CL_DisplayFiremodes_f: displaying %s firemodes.\n", hand);

	if (firemodes_change_display) {
		/* Toggle firemode lists if needed. Mind you that HideFiremodes modifies visible_firemode_list_xxx to qfalse */
		if (hand[0] == 'r') {
			if (visible_firemode_list_right == qtrue) {
				HideFiremodes(); /* Modifies visible_firemode_list_xxxx */
				return;
			} else {
				HideFiremodes();
				visible_firemode_list_left = qfalse;
				visible_firemode_list_right = qtrue;
			}
		} else  { /* 'l' */
			if (visible_firemode_list_left == qtrue) {
				HideFiremodes(); /* Modifies visible_firemode_list_xxxx */
				return;
			} else {
				HideFiremodes(); /* Modifies visible_firemode_list_xxxx */
				visible_firemode_list_left = qtrue;
				visible_firemode_list_right = qfalse;
			}
		}
	}
	firemodes_change_display = qtrue;

	actor_idx = CL_GetActorNumber(selActor);
	Com_DPrintf(DEBUG_CLIENT, "CL_DisplayFiremodes_f: actor index: %i\n", actor_idx);
	if (actor_idx == -1)
		Com_Error(ERR_DROP, "Could not get current selected actor's id");

	/* Set default firemode if the currenttly seleced one is not sane or for another weapon. */
	if (!SANE_REACTION(actor_idx)) {	/* No sane firemode selected. */
		/* Set default firemode (try right hand first, then left hand). */
		CL_SetDefaultReactionFiremode(actor_idx, 'r');
	} else {
		if (reactionFiremode[actor_idx][RF_WPIDX] != ammo->weap_idx[weap_fd_idx]) {
			if ((hand[0] == 'r') && (reactionFiremode[actor_idx][RF_HAND] == 0)) {
				/* Set default firemode (try right hand first, then left hand). */
				CL_SetDefaultReactionFiremode(actor_idx, 'r');
			}
			if ((hand[0] == 'l') && (reactionFiremode[actor_idx][RF_HAND] == 1)) {
				/* Set default firemode (try left hand first, then right hand). */
				CL_SetDefaultReactionFiremode(actor_idx, 'l');
			}
		}
	}

	for (i = 0; i < MAX_FIREDEFS_PER_WEAPON; i++) {
		if (i < ammo->numFiredefs[weap_fd_idx]) { /* We have a defined fd ... */
			/* Display the firemode information (image + text). */
			if (ammo->fd[weap_fd_idx][i].time <= selActor->TU) {  /* Enough timeunits for this firemode?*/
				CL_DisplayFiremodeEntry(&ammo->fd[weap_fd_idx][i], hand[0], qtrue);
			} else{
				CL_DisplayFiremodeEntry(&ammo->fd[weap_fd_idx][i], hand[0], qfalse);
			}

			/* Display checkbox for reaction firemode (this needs a sane reactionFiremode array) */
			if (ammo->fd[weap_fd_idx][i].reaction) {
				Com_DPrintf(DEBUG_CLIENT, "CL_DisplayFiremodes_f: This is a reaction firemode: %i\n", i);
				if (hand[0] == 'r') {
					if (THIS_REACTION(actor_idx,0,i)) {
						/* Set this checkbox visible+active. */
						Cbuf_AddText(va("set_right_cb_a%i\n", i));
					} else {
						/* Set this checkbox visible+inactive. */
						Cbuf_AddText(va("set_right_cb_ina%i\n", i));
					}
				} else { /* hand[0] == 'l' */
					if (THIS_REACTION(actor_idx,1,i)) {
						/* Set this checkbox visible+active. */
						Cbuf_AddText(va("set_left_cb_a%i\n", i));
					} else {
						/* Set this checkbox visible+active. */
						Cbuf_AddText(va("set_left_cb_ina%i\n", i));
					}
				}
			}

		} else { /* No more fd left in the list or weapon not researched. */
			if (hand[0] == 'r')
				Cbuf_AddText(va("set_right_inv%i\n", i)); /* Hide this entry */
			else
				Cbuf_AddText(va("set_left_inv%i\n", i)); /* Hide this entry */
		}
	}
}

/**
 * @brief Changes the display of the firemode-list to a given hand, but only if the list is visible already.
 * @note Console command: switch_firemode_list
 */
void CL_SwitchFiremodeList_f (void)
{
	/* Cmd_Argv(1) ... = hand */
	if (Cmd_Argc() < 2 || (Cmd_Argv(1)[0] != 'r' && Cmd_Argv(1)[0] != 'l')) { /* no argument given */
		Com_Printf("Usage: %s [l|r]\n", Cmd_Argv(0));
		return;
	}

	if (visible_firemode_list_right || visible_firemode_list_left) {
		Cbuf_AddText(va("list_firemodes %s\n", Cmd_Argv(1)));
	}
}

/**
 * @brief Checks if the selected firemode checkbox is ok as a reaction firemode and updates data+display.
 */
void CL_SelectReactionFiremode_f (void)
{
	const char *hand;
	int firemode;
	int actor_idx = -1;

	if (Cmd_Argc() < 3) { /* no argument given */
		Com_Printf("Usage: %s [l|r] <num>   num=firemode number\n", Cmd_Argv(0));
		return;
	}

	hand = Cmd_Argv(1);

	if (hand[0] != 'r' && hand[0] != 'l') {
		Com_Printf("Usage: %s [l|r] <num>   num=firemode number\n", Cmd_Argv(0));
		return;
	}

	if (!selActor)
		return;

	actor_idx = CL_GetActorNumber(selActor);
	if (actor_idx == -1)
		Com_Error(ERR_DROP, "Could not get current selected actor's id");

	firemode = atoi(Cmd_Argv(2));

	if (firemode >= MAX_FIREDEFS_PER_WEAPON) {
		Com_Printf("CL_SelectReactionFiremode_f: Firemode index to big (%i). Highest possible number is %i.\n", firemode, MAX_FIREDEFS_PER_WEAPON-1);
		return;
	}

	CL_UpdateReactionFiremodes(hand[0], actor_idx, firemode);

	/* Update display of firemode checkbuttons. */
	firemodes_change_display = qfalse; /* So CL_DisplayFiremodes_f doesn't hide the list. */
	CL_DisplayFiremodes_f();
}


/**
 * @brief Starts aiming/target mode for selected left/right firemode.
 * @note Previously know as a combination of CL_FireRightPrimary, CL_FireRightSecondary,
 * @note CL_FireLeftPrimary and CL_FireLeftSecondary.
 */
void CL_FireWeapon_f (void)
{
	const char *hand;
	int firemode;

	objDef_t *weapon = NULL;
	objDef_t *ammo = NULL;
	int weap_fd_idx = -1;

	if (Cmd_Argc() < 3) { /* no argument given */
		Com_Printf("Usage: %s [l|r] <num>   num=firemode number\n", Cmd_Argv(0));
		return;
	}

	hand = Cmd_Argv(1);

	if (hand[0] != 'r' && hand[0] != 'l') {
		Com_Printf("Usage: %s [l|r] <num>   num=firemode number\n", Cmd_Argv(0));
		return;
	}

	if (!selActor)
		return;

	firemode = atoi(Cmd_Argv(2));

	if (firemode >= MAX_FIREDEFS_PER_WEAPON) {
		Com_Printf("CL_FireWeapon_f: Firemode index to big (%i). Highest possible number is %i.\n", firemode, MAX_FIREDEFS_PER_WEAPON-1);
		return;
	}

	CL_GetWeaponAndAmmo(selActor, hand[0], &weapon, &ammo, &weap_fd_idx);
	if (weap_fd_idx == -1)
		return;

	/* Let's check if shooting is possible.
	 * Don't let the selActor->TU parameter irritate you, it is not checked/used heR_ */
	if (hand[0] == 'r') {
		if (!CL_CheckMenuAction(selActor->TU, RIGHT(selActor), EV_INV_AMMO))
			return;
	} else {
		if (!CL_CheckMenuAction(selActor->TU, LEFT(selActor), EV_INV_AMMO))
			return;
	}

	if (ammo->fd[weap_fd_idx][firemode].time <= selActor->TU) {
		/* Actually start aiming. This is done by changing the current mode of display. */
		if (hand[0] == 'r')
			cl.cmode = M_FIRE_R;
		else
			cl.cmode = M_FIRE_L;
		cl.cfiremode = firemode;	/* Store firemode. */
		HideFiremodes();
	} else {
		/* Cannot shoot because of not enough TUs - every other
		   case should be checked previously in this function. */
		CL_DisplayHudMessage(_("Can't perform action:\nnot enough TUs.\n"), 2000);
		Com_DPrintf(DEBUG_CLIENT, "CL_FireWeapon_f: Firemode not available (%s, %s).\n", hand, ammo->fd[weap_fd_idx][firemode].name);
		return;
	}
}

/**
 * @brief Refreshes the weapon/reload buttons on the HUD.
 * @param[in] time The amount of TU (of an actor) left in case of action.
 * @sa CL_ActorUpdateCVars
 */
static void CL_RefreshWeaponButtons (int time)
{
	invList_t *weaponr = NULL;
	invList_t *weaponl = NULL;
	invList_t *headgear = NULL;
	int minweaponrtime = 100, minweaponltime = 100;
	int minheadgeartime = 100;
	int weaponr_fds_idx = -1, weaponl_fds_idx = -1;
	int headgear_fds_idx = -1;
	qboolean isammo = qfalse;
	int i;

	if (!selActor)
		return;

	weaponr = RIGHT(selActor);
	headgear = HEADGEAR(selActor);

	/* check for two-handed weapon - if not, also define weaponl */
	if (!weaponr || !csi.ods[weaponr->item.t].holdTwoHanded)
		weaponl = LEFT(selActor);

	/* crouch/stand button */
	if (selActor->state & STATE_CROUCHED) {
		weaponButtonState[BT_STAND] = -1;
		if (time < TU_CROUCH)
			SetWeaponButton(BT_CROUCH, BT_STATE_DISABLE);
		else
			SetWeaponButton(BT_CROUCH, BT_STATE_DESELECT);
	} else {
		weaponButtonState[BT_CROUCH] = -1;
		if (time < TU_CROUCH)
			SetWeaponButton(BT_STAND, BT_STATE_DISABLE);
		else
			SetWeaponButton(BT_STAND, BT_STATE_DESELECT);
	}

	/* headgear button (nearly the same code as for weapon firing buttons below). */
	/** @todo Make a generic function out of this? */
	if (headgear) {
		assert(headgear->item.t != NONE);
		/* Check whether this item use ammo. */
		if (headgear->item.m == NONE) {
			/* This item does not use ammo, check for existing firedefs in this item. */
			if (csi.ods[headgear->item.t].numWeapons > 0) {
				/* Get firedef from the weapon entry instead. */
				headgear_fds_idx = FIRESH_FiredefsIDXForWeapon(&csi.ods[headgear->item.t], headgear->item.t);
			} else {
				headgear_fds_idx = -1;
			}
			isammo = qfalse;
		} else {
			/* This item uses ammo, get the firedefs from ammo. */
			headgear_fds_idx = FIRESH_FiredefsIDXForWeapon(&csi.ods[headgear->item.m], headgear->item.t);
			isammo = qtrue;
		}
		if (isammo) {
			/* Search for the smallest TU needed to shoot. */
			if (headgear_fds_idx != -1)
				for (i = 0; i < MAX_FIREDEFS_PER_WEAPON; i++) {
					if (!csi.ods[headgear->item.m].fd[headgear_fds_idx][i].time)
						continue;
					if (csi.ods[headgear->item.m].fd[headgear_fds_idx][i].time < minheadgeartime)
					minheadgeartime = csi.ods[headgear->item.m].fd[headgear_fds_idx][i].time;
				}
		} else {
			if (headgear_fds_idx != -1)
				for (i = 0; i < MAX_FIREDEFS_PER_WEAPON; i++) {
					if (!csi.ods[headgear->item.t].fd[headgear_fds_idx][i].time)
						continue;
					if (csi.ods[headgear->item.t].fd[headgear_fds_idx][i].time < minheadgeartime)
						minheadgeartime = csi.ods[headgear->item.t].fd[headgear_fds_idx][i].time;
				}
		}
		if (time < minheadgeartime) {
			SetWeaponButton(BT_HEADGEAR, BT_STATE_DISABLE);
		} else {
			SetWeaponButton(BT_HEADGEAR, BT_STATE_DESELECT);
		}
	} else {
		SetWeaponButton(BT_HEADGEAR, BT_STATE_DISABLE);
	}

	/* reaction-fire button */
	if (CL_GetReactionState(selActor) == R_FIRE_OFF) {
		if ((time >= TU_REACTION_SINGLE)
		&& (CL_WeaponWithReaction(selActor, 'r') || CL_WeaponWithReaction(selActor, 'l')))
			SetWeaponButton(BT_REACTION, BT_STATE_DESELECT);
		else
			SetWeaponButton(BT_REACTION, BT_STATE_DISABLE);

	} else {
		if ((CL_WeaponWithReaction(selActor, 'r') || CL_WeaponWithReaction(selActor, 'l'))) {
			CL_DisplayPossibleReaction(selActor);
		} else {
			CL_DisplayImpossibleReaction(selActor);
		}
	}

	/* reload buttons */
	if (!weaponr || weaponr->item.m == NONE
		 || !csi.ods[weaponr->item.t].reload
		 || time < CL_CalcReloadTime(weaponr->item.t))
		SetWeaponButton(BT_RIGHT_RELOAD, BT_STATE_DISABLE);
	else
		SetWeaponButton(BT_RIGHT_RELOAD, BT_STATE_DESELECT);

	if (!weaponl || weaponl->item.m == NONE
		 || !csi.ods[weaponl->item.t].reload
		 || time < CL_CalcReloadTime(weaponl->item.t))
		SetWeaponButton(BT_LEFT_RELOAD, BT_STATE_DISABLE);
	else
		SetWeaponButton(BT_LEFT_RELOAD, BT_STATE_DESELECT);

	/* Weapon firing buttons. (nearly the same code as for headgear buttons above).*/
	/** @todo Make a generic function out of this? */
	if (weaponr) {
		assert(weaponr->item.t != NONE);
		/* Check whether this item use ammo. */
		if (weaponr->item.m == NONE) {
			/* This item does not use ammo, check for existing firedefs in this item. */
			if (csi.ods[weaponr->item.t].numWeapons > 0) {
				/* Get firedef from the weapon entry instead. */
				weaponr_fds_idx = FIRESH_FiredefsIDXForWeapon(&csi.ods[weaponr->item.t], weaponr->item.t);
			} else {
				weaponr_fds_idx = -1;
			}
			isammo = qfalse;
		} else {
			/* This item uses ammo, get the firedefs from ammo. */
			weaponr_fds_idx = FIRESH_FiredefsIDXForWeapon(&csi.ods[weaponr->item.m], weaponr->item.t);
			isammo = qtrue;
		}
		if (isammo) {
			/* Search for the smallest TU needed to shoot. */
			if (weaponr_fds_idx != -1)
				for (i = 0; i < MAX_FIREDEFS_PER_WEAPON; i++) {
					if (!csi.ods[weaponr->item.m].fd[weaponr_fds_idx][i].time)
						continue;
					if (csi.ods[weaponr->item.m].fd[weaponr_fds_idx][i].time < minweaponrtime)
					minweaponrtime = csi.ods[weaponr->item.m].fd[weaponr_fds_idx][i].time;
				}
		} else {
			if (weaponr_fds_idx != -1)
				for (i = 0; i < MAX_FIREDEFS_PER_WEAPON; i++) {
					if (!csi.ods[weaponr->item.t].fd[weaponr_fds_idx][i].time)
						continue;
					if (csi.ods[weaponr->item.t].fd[weaponr_fds_idx][i].time < minweaponrtime)
						minweaponrtime = csi.ods[weaponr->item.t].fd[weaponr_fds_idx][i].time;
				}
		}
		if (time < minweaponrtime)
			SetWeaponButton(BT_RIGHT_FIRE, BT_STATE_DISABLE);
		else
			SetWeaponButton(BT_RIGHT_FIRE, BT_STATE_DESELECT);
	} else {
		SetWeaponButton(BT_RIGHT_FIRE, BT_STATE_DISABLE);
	}

	if (weaponl) {
		assert(weaponl->item.t != NONE);
		/* Check whether this item use ammo. */
		if (weaponl->item.m == NONE) {
			/* This item does not use ammo, check for existing firedefs in this item. */
			if (csi.ods[weaponl->item.t].numWeapons > 0) {
				/* Get firedef from the weapon entry instead. */
				weaponl_fds_idx = FIRESH_FiredefsIDXForWeapon(&csi.ods[weaponl->item.t], weaponl->item.t);
			} else {
				weaponl_fds_idx = -1;
			}
			isammo = qfalse;
		} else {
			/* This item uses ammo, get the firedefs from ammo. */
			weaponl_fds_idx = FIRESH_FiredefsIDXForWeapon(&csi.ods[weaponl->item.m], weaponl->item.t);
			isammo = qtrue;
		}
		if (isammo) {
			/* Search for the smallest TU needed to shoot. */
			if (weaponl_fds_idx != -1)
				for (i = 0; i < MAX_FIREDEFS_PER_WEAPON; i++) {
					if (!csi.ods[weaponl->item.m].fd[weaponl_fds_idx][i].time)
						continue;
					if (csi.ods[weaponl->item.m].fd[weaponl_fds_idx][i].time < minweaponltime)
						minweaponltime = csi.ods[weaponl->item.m].fd[weaponl_fds_idx][i].time;
				}
		} else {
			if (weaponl_fds_idx != -1)
				for (i = 0; i < MAX_FIREDEFS_PER_WEAPON; i++) {
					if (!csi.ods[weaponl->item.t].fd[weaponl_fds_idx][i].time)
						continue;
					if (csi.ods[weaponl->item.t].fd[weaponl_fds_idx][i].time < minweaponltime)
						minweaponltime = csi.ods[weaponl->item.t].fd[weaponl_fds_idx][i].time;
				}
		}
		if (time < minweaponltime)
			SetWeaponButton(BT_LEFT_FIRE, BT_STATE_DISABLE);
		else
			SetWeaponButton(BT_LEFT_FIRE, BT_STATE_DESELECT);
	} else {
		SetWeaponButton(BT_LEFT_FIRE, BT_STATE_DISABLE);
	}
}

/**
 * @brief Checks whether an action on hud menu is valid and displays proper message.
 * @param[in] time The amount of TU (of an actor) left.
 * @param[in] weapon An item in hands.
 * @param[in] mode EV_INV_AMMO in case of fire button, EV_INV_RELOAD in case of reload button
 * @return qfalse when action is not possible, otherwise qtrue
 * @sa CL_FireWeapon_f
 * @sa CL_ReloadLeft_f
 * @sa CL_ReloadRight_f
 * @todo Check for ammo in hand and give correct feedback in all cases.
 */
qboolean CL_CheckMenuAction (int time, invList_t *weapon, int mode)
{
	/* No item in hand. */
	/* @todo: ignore this condition when ammo in hand */
	if (!weapon || weapon->item.t == NONE) {
		CL_DisplayHudMessage(_("No item in hand.\n"), 2000);
		return qfalse;
	}

	switch (mode) {
	case EV_INV_AMMO:
		/* Check if shooting is possible.
		 * i.e. The weapon has ammo and can be fired with the 'available' hands.
		 * TUs (i.e. "time") are are _not_ checked here, this needs to be done
		 * elsewhere for the correct firemode. */

		/* Cannot shoot because of lack of ammo. */
		if (weapon->item.a <= 0 && csi.ods[weapon->item.t].reload) {
			CL_DisplayHudMessage(_("Can't perform action:\nout of ammo.\n"), 2000);
			return qfalse;
		}
		/* Cannot shoot because weapon is fireTwoHanded, yet both hands handle items. */
		if (csi.ods[weapon->item.t].fireTwoHanded && LEFT(selActor)) {
			CL_DisplayHudMessage(_("This weapon cannot be fired\none handed.\n"), 2000);
			return qfalse;
		}
		break;
	case EV_INV_RELOAD:
		/* Check if reload is possible. Also checks for the correct amount of TUs. */

		/* Cannot reload because this item is not reloadable. */
		if (!csi.ods[weapon->item.t].reload) {
			CL_DisplayHudMessage(_("Can't perform action:\nthis item is not reloadable.\n"), 2000);
			return qfalse;
		}
		/* Cannot reload because of no ammo in inventory. */
		if (CL_CalcReloadTime(weapon->item.t) >= 999) {
			CL_DisplayHudMessage(_("Can't perform action:\nammo not available.\n"), 2000);
			return qfalse;
		}
		/* Cannot reload because of not enough TUs. */
		if (time < CL_CalcReloadTime(weapon->item.t)) {
			CL_DisplayHudMessage(_("Can't perform action:\nnot enough TUs.\n"), 2000);
			return qfalse;
		}
		break;
	default:
		break;
	}

	return qtrue;
}


/**
 * @brief Updates console vars for an actor.
 *
 * This function updates the cvars for the hud (battlefield)
 * unlike CL_CharacterCvars and CL_UGVCvars which updates them for
 * diplaying the data in the menu system
 *
 * @sa CL_CharacterCvars
 * @sa CL_UGVCvars
 */
void CL_ActorUpdateCVars (void)
{
	static char infoText[MAX_SMALLMENUTEXTLEN];
	static char mouseText[MAX_SMALLMENUTEXTLEN];
	qboolean refresh;
	char *name;
	int time;
	fireDef_t *old;

	if (cls.state != ca_active)
		return;

	refresh = Cvar_VariableInteger("hud_refresh");
	if (refresh) {
		Cvar_Set("hud_refresh", "0");
		Cvar_Set("cl_worldlevel", cl_worldlevel->string);
		CL_ResetWeaponButtons();
	}

	/* set Cvars for all actors */
	CL_ActorGlobalCVars();

	/* force them empty first */
	Cvar_Set("mn_anim", "stand0");
	Cvar_Set("mn_rweapon", "");
	Cvar_Set("mn_lweapon", "");

	if (selActor) {
		invList_t *selWeapon;

		/* set generic cvars */
		Cvar_Set("mn_tu", va("%i", selActor->TU));
		Cvar_Set("mn_tumax", va("%i", selActor->maxTU));
		Cvar_Set("mn_morale", va("%i", selActor->morale));
		Cvar_Set("mn_moralemax", va("%i", selActor->maxMorale));
		Cvar_Set("mn_hp", va("%i", selActor->HP));
		Cvar_Set("mn_hpmax", va("%i", selActor->maxHP));
		Cvar_Set("mn_stun", va("%i", selActor->STUN));

		/* animation and weapons */
		name = R_AnimGetName(&selActor->as, selActor->model1);
		if (name)
			Cvar_Set("mn_anim", name);
		if (RIGHT(selActor))
			Cvar_Set("mn_rweapon", csi.ods[RIGHT(selActor)->item.t].model);
		if (LEFT(selActor))
			Cvar_Set("mn_lweapon", csi.ods[LEFT(selActor)->item.t].model);

		/* get weapon */
		if (IS_MODE_FIRE_HEADGEAR(cl.cmode)) {
			selWeapon = HEADGEAR(selActor);
		} else if (IS_MODE_FIRE_LEFT(cl.cmode)) {
			selWeapon = LEFT(selActor);
		} else {
			selWeapon = RIGHT(selActor);
		}

		if (!selWeapon && RIGHT(selActor) && csi.ods[RIGHT(selActor)->item.t].holdTwoHanded)
			selWeapon = RIGHT(selActor);

		if (selWeapon) {
			if (selWeapon->item.t == NONE) {
				/* No valid weapon in the hand. */
				selFD = NULL;
			} else {
				/* Check whether this item uses/has ammo. */
				if (selWeapon->item.m == NONE) {
					/* This item does not use ammo, check for existing firedefs in this item. */
					/* This is supposed to be a weapon or other useable item. */
					if (csi.ods[selWeapon->item.t].numWeapons > 0) {
						if (csi.ods[selWeapon->item.t].weapon || (csi.ods[selWeapon->item.t].weap_idx[0] == selWeapon->item.t)) {
							/* Get firedef from the weapon (or other useable item) entry instead. */
							selFD = FIRESH_GetFiredef(
								selWeapon->item.t,
								FIRESH_FiredefsIDXForWeapon(
									&csi.ods[selWeapon->item.t],
									selWeapon->item.t),
								cl.cfiremode);
						} else {
							/* This is ammo */
							selFD = NULL;
						}
					} else {
						/* No firedefinitions found in this presumed 'weapon with no ammo'. */
						selFD = NULL;
					}
				} else {
					/* This item uses ammo, get the firedefs from ammo. */
					old = FIRESH_GetFiredef(
						selWeapon->item.m,
						FIRESH_FiredefsIDXForWeapon(
							&csi.ods[selWeapon->item.m],
							selWeapon->item.t),
						cl.cfiremode);
					/* reset the align if we switched the firemode */
					if (old != selFD)
						mousePosTargettingAlign = 0;
					selFD = old;
				}
			}
		}

		/* write info */
		time = 0;

		/* display special message */
		if (cl.time < cl.msgTime)
			Com_sprintf(infoText, sizeof(infoText), cl.msgText);

		/* update HUD stats etc in more or shoot modes */
		if (cl.cmode == M_MOVE || cl.cmode == M_PEND_MOVE) {
			/* If the mouse is outside the world, blank move */
			/* or the movelength is 255 - not reachable e.g. */
			if ((mouseSpace != MS_WORLD && cl.cmode < M_PEND_MOVE) || actorMoveLength == ROUTING_NOT_REACHABLE) {
				/* @todo: CHECKME Why do we check for (cl.cmode < M_PEND_MOVE) here? */
				actorMoveLength = ROUTING_NOT_REACHABLE;
				Com_sprintf(infoText, sizeof(infoText), _("Morale  %i\n"), selActor->morale);
				MN_MenuTextReset(TEXT_MOUSECURSOR_RIGHT);
			}
			if (cl.cmode != cl.oldcmode || refresh || lastHUDActor != selActor
						|| lastMoveLength != actorMoveLength || lastTU != selActor->TU) {
				if (actorMoveLength != ROUTING_NOT_REACHABLE) {
					CL_RefreshWeaponButtons(selActor->TU - actorMoveLength);
					Com_sprintf(infoText, sizeof(infoText), _("Morale  %i\nMove %i (%i TU left)\n"), selActor->morale, actorMoveLength, selActor->TU - actorMoveLength);
					if (actorMoveLength <= selActor->TU)
						Com_sprintf(mouseText, sizeof(mouseText), "%i (%i)\n", actorMoveLength, selActor->TU);
					else
						Com_sprintf(mouseText, sizeof(mouseText), "- (-)\n");
				} else {
					CL_RefreshWeaponButtons(selActor->TU);
				}
				lastHUDActor = selActor;
				lastMoveLength = actorMoveLength;
				lastTU = selActor->TU;
				menuText[TEXT_MOUSECURSOR_RIGHT] = mouseText;
			}
			time = actorMoveLength;
		} else {
			MN_MenuTextReset(TEXT_MOUSECURSOR_RIGHT);
			/* in multiplayer RS_ItemIsResearched always returns true,
			 * so we are able to use the aliens weapons */
			if (selWeapon && !RS_ItemIsResearched(csi.ods[selWeapon->item.t].id)) {
				CL_DisplayHudMessage(_("You cannot use this unknown item.\nYou need to research it first.\n"), 2000);
				cl.cmode = M_MOVE;
			} else if (selWeapon && selFD) {
				Com_sprintf(infoText, sizeof(infoText),
							"%s\n%s (%i) [%i%%] %i\n", csi.ods[selWeapon->item.t].name, selFD->name, selFD->ammo, selToHit, selFD->time);
				Com_sprintf(mouseText, sizeof(mouseText),
							"%s: %s (%i) [%i%%] %i\n", csi.ods[selWeapon->item.t].name, selFD->name, selFD->ammo, selToHit, selFD->time);

				menuText[TEXT_MOUSECURSOR_RIGHT] = mouseText;	/* Save the text for later display next to the cursor. */

				time = selFD->time;
				/* if no TUs left for this firing action of if the weapon is reloadable and out of ammo, then change to move mode */
				if (selActor->TU < time || (csi.ods[selWeapon->item.t].reload && selWeapon->item.a <= 0)) {
					cl.cmode = M_MOVE;
					CL_RefreshWeaponButtons(selActor->TU - actorMoveLength);
				}
			} else if (selWeapon) {
				Com_sprintf(infoText, sizeof(infoText), _("%s\n(empty)\n"), csi.ods[selWeapon->item.t].name);
			} else {
				cl.cmode = M_MOVE;
				CL_RefreshWeaponButtons(selActor->TU - actorMoveLength);
			}
		}

		/* handle actor in a panic */
		if (selActor->state & STATE_PANIC) {
			Com_sprintf(infoText, sizeof(infoText), _("Currently panics!\n"));
			MN_MenuTextReset(TEXT_MOUSECURSOR_RIGHT);
		}

		/* Calculate remaining TUs. */
		time = max(0, selActor->TU - time);

		Cvar_Set("mn_turemain", va("%i", time));

		/* print ammo */
		if (RIGHT(selActor)) {
			Cvar_SetValue("mn_ammoright", RIGHT(selActor)->item.a);
		} else {
			Cvar_Set("mn_ammoright", "");
		}
		if (LEFT(selActor)) {
			Cvar_SetValue("mn_ammoleft", LEFT(selActor)->item.a);
		} else {
			Cvar_Set("mn_ammoleft", "");
		}

		if (!LEFT(selActor) && RIGHT(selActor)
			&& csi.ods[RIGHT(selActor)->item.t].holdTwoHanded)
			Cvar_Set("mn_ammoleft", Cvar_VariableString("mn_ammoright"));

		/* change stand-crouch & reaction button state */
		if (cl.oldstate != selActor->state || refresh) {
			selActorReactionState = CL_GetReactionState(selActor);
			if (selActorOldReactionState != selActorReactionState) {
				selActorOldReactionState = selActorReactionState;
				switch (selActorReactionState) {
				case R_FIRE_MANY:
					Cbuf_AddText("startreactionmany\n");
					break;
				case R_FIRE_ONCE:
					Cbuf_AddText("startreactiononce\n");
					break;
				case R_FIRE_OFF: /* let RefreshWeaponButtons work it out */
					weaponButtonState[BT_REACTION] = -1;
					break;
				}
			}

			cl.oldstate = selActor->state;
			if (actorMoveLength < ROUTING_NOT_REACHABLE
				&& (cl.cmode == M_MOVE || cl.cmode == M_PEND_MOVE))
				CL_RefreshWeaponButtons(time);
			else
				CL_RefreshWeaponButtons(selActor->TU);
		} else {
			/* no actor selected, reset cvars */
			/* @todo: this overwrites the correct values a bit to often.
			Cvar_Set("mn_tu", "0");
			Cvar_Set("mn_turemain", "0");
			Cvar_Set("mn_tumax", "30");
			Cvar_Set("mn_morale", "0");
			Cvar_Set("mn_moralemax", "1");
			Cvar_Set("mn_hp", "0");
			Cvar_Set("mn_hpmax", "1");
			Cvar_Set("mn_ammoright", "");
			Cvar_Set("mn_ammoleft", "");
			Cvar_Set("mn_stun", "0");
			*/
			if (refresh)
				Cbuf_AddText("deselstand\n");

			/* this allows us to display messages even with no actor selected */
			if (cl.time < cl.msgTime) {
				/* special message */
				Com_sprintf(infoText, sizeof(infoText), cl.msgText);
			}
		}
		menuText[TEXT_STANDARD] = infoText;
	/* this will stop the drawing of the bars over the hole screen when we test maps */
	} else if (!cl.numTeamList) {
		Cvar_SetValue("mn_tu", 0);
		Cvar_SetValue("mn_tumax", 100);
		Cvar_SetValue("mn_morale", 0);
		Cvar_SetValue("mn_moralemax", 100);
		Cvar_SetValue("mn_hp", 0);
		Cvar_SetValue("mn_hpmax", 100);
		Cvar_SetValue("mn_stun", 0);
	}

	/* mode */
	if (cl.oldcmode != cl.cmode || refresh) {
		switch (cl.cmode) {
		/* @todo: Better highlight for active firemode (from the list, not the buttons) needed ... */
		case M_FIRE_L:
		case M_PEND_FIRE_L:
			/* @todo: Display current firemode better*/
			break;
		case M_FIRE_R:
		case M_PEND_FIRE_R:
			/* @todo: Display current firemode better*/
			break;
		default:
			ClearHighlights();
		}
		cl.oldcmode = cl.cmode;
		if (selActor)
			CL_RefreshWeaponButtons(selActor->TU);
	}

	/* player bar */
	if (cl_selected->modified || refresh) {
		int i;

		for (i = 0; i < MAX_TEAMLIST; i++) {
			if (!cl.teamList[i] || cl.teamList[i]->state & STATE_DEAD) {
				Cbuf_AddText(va("huddisable%i\n", i));
			} else if (i == cl_selected->integer) {
				/* stored in menu_nohud.ufo - confunc that calls all the different
				 * hud select confuncs */
				Cbuf_AddText(va("hudselect%i\n", i));
			} else {
				Cbuf_AddText(va("huddeselect%i\n", i));
			}
		}
		cl_selected->modified = qfalse;
	}
}

/*
==============================================================
ACTOR SELECTION AND TEAM LIST
==============================================================
*/

/**
 * @brief Adds the actor the the team list.
 * @sa CL_ActorAppear
 * @sa CL_RemoveActorFromTeamList
 * @param le Pointer to local entity struct
 */
void CL_AddActorToTeamList (le_t * le)
{
	int i;

	/* test team */
	if (!le || le->team != cls.team || le->pnum != cl.pnum || (le->state & STATE_DEAD))
		return;

	/* check list length */
	if (cl.numTeamList >= MAX_TEAMLIST)
		return;

	/* check list for that actor */
	i = CL_GetActorNumber(le);

	/* add it */
	if (i == -1) {
		i = cl.numTeamList;
		cl.teamList[cl.numTeamList++] = le;
		Cbuf_AddText(va("numonteam%i\n", cl.numTeamList)); /* althud */
		Cbuf_AddText(va("huddeselect%i\n", i));
		if (cl.numTeamList == 1)
			CL_ActorSelectList(0);

		CL_SetDefaultReactionFiremode(i, 'r');	/**< Set default reaction firemode for this soldier. */
#if 0
		/* @todo: remove this if the above works (this should fix the wrong setting of the default firemode on re-try or next mission) */
		/* Initialize reactionmode (with undefined firemode) ... this will be checked for in CL_DoEndRound. */
		CL_SetReactionFiremode(i, -1, NONE, -1);
#endif
	}
}


/**
 * @brief Removes an actor from the team list.
 * @sa CL_ActorStateChange
 * @sa CL_AddActorToTeamList
 * @param le Pointer to local entity struct
 */
void CL_RemoveActorFromTeamList (le_t * le)
{
	int i;

	if (!le)
		return;

	for (i = 0; i < cl.numTeamList; i++) {
		if (cl.teamList[i] == le) {
			/* disable hud button */
			Cbuf_AddText(va("huddisable%i\n", i));

			/* remove from list */
			cl.teamList[i] = NULL;
			break;
		}
	}

	/* check selection */
	if (selActor == le) {
		/* @todo: This should probably be a while loop */
		for (i = 0; i < cl.numTeamList; i++) {
			if (CL_ActorSelect(cl.teamList[i]))
				break;
		}

		if (i == cl.numTeamList) {
			selActor->selected = qfalse;
			selActor = NULL;
		}
	}
}

/**
 * @brief Selects an actor.
 *
 * @param le Pointer to local entity struct
 *
 * @sa CL_UGVCvars
 * @sa CL_CharacterCvars
 */
qboolean CL_ActorSelect (le_t * le)
{
	int actor_idx;
	qboolean same_actor = qfalse;

	/* test team */
	if (!le || le->team != cls.team ||
		(le->state & STATE_DEAD) || !le->inuse)
		return qfalse;

	if (blockEvents)
		return qfalse;

	/* select him */
	if (selActor)
		selActor->selected = qfalse;
	le->selected = qtrue;

	/* reset the align if we switched the actor */
	if (selActor != le)
		mousePosTargettingAlign = 0;
	else
		same_actor = qtrue;

	selActor = le;
	menuInventory = &selActor->i;
	selActorReactionState = CL_GetReactionState(selActor);

	actor_idx = CL_GetActorNumber(le);
	if (actor_idx < 0)
		return qfalse;

	/* console commands, update cvars */
	Cvar_ForceSet("cl_selected", va("%i", actor_idx));

	selChr = CL_GetActorChr(le);
	assert(selChr);
	if (selChr->inv) {
		switch (le->fieldSize) {
		case ACTOR_SIZE_NORMAL:
			CL_CharacterCvars(selChr);
			break;
		case ACTOR_SIZE_2x2:
			CL_UGVCvars(selChr);
			break;
		default:
			Com_Error(ERR_DROP, "CL_ActorSelect: Unknown fieldsize");
		}
	}

	/* Forcing the hud-display to refresh it's displayed stuff */
	Cvar_SetValue("hud_refresh", 1);
	CL_ActorUpdateCVars();

	CL_ConditionalMoveCalc(&clMap, le, MAX_ROUTE);

	/* move first person camera to new actor */
	if (camera_mode == CAMERA_MODE_FIRSTPERSON)
		CL_CameraModeChange(CAMERA_MODE_FIRSTPERSON);

	/* Change to move-mode and hide firemodes.
	 * Only if it's a different actor - if it's the same we keep the current mode etc... */
	if (!same_actor) {
		HideFiremodes();
		cl.cmode = M_MOVE;
	}

	return qtrue;
}


/**
 * @brief Selects an actor from a list.
 *
 * This function is used to select an actor from the lists that are
 * used in equipment and team assemble screens
 *
 * @param num The index value from the list of actors
 *
 * @sa CL_ActorSelect
 * @return qtrue if selection was possible otherwise qfalse
 */
qboolean CL_ActorSelectList (int num)
{
	le_t *le;

	/* check if actor exists */
	if (num >= cl.numTeamList)
		return qfalse;

	/* select actor */
	le = cl.teamList[num];
	if (!CL_ActorSelect(le))
		return qfalse;

	/* center view (if wanted) */
	if (cl_centerview->integer)
		VectorCopy(le->origin, cl.cam.reforg);
	/* change to worldlevel were actor is right now */
	Cvar_SetValue("cl_worldlevel", le->pos[2]);

	return qtrue;
}

/**
 * @brief selects the next actor
 */
qboolean CL_ActorSelectNext (void)
{
	le_t* le;
	int selIndex = -1;
	int num = cl.numTeamList;
	int i;

	/* find index of currently selected actor */
	for (i = 0; i < num; i++) {
		le = cl.teamList[i];
		if (le && le->selected && le->inuse && !(le->state & STATE_DEAD)) {
			selIndex = i;
			break;
		}
	}
	if (selIndex < 0)
		return qfalse;			/* no one selected? */

	/* cycle round */
	i = selIndex;
	while (qtrue) {
		i = (i + 1) % num;
		if (i == selIndex)
			break;
		if (CL_ActorSelectList(i))
			return qtrue;
	}
	return qfalse;
}


/*
==============================================================
ACTOR MOVEMENT AND SHOOTING
==============================================================
*/

/**
 * @brief A list of locations that cannot be moved to.
 * @note Pointer to le->pos or edict->pos followed by le->fieldSize or edict->fieldSize
 * @see CL_BuildForbiddenList
 */
pos_t *fb_list[MAX_FORBIDDENLIST];
/**
 * @brief Current length of fb_list.
 * @note all byte pointers in the fb_list list (pos + fieldSize)
 * @see fb_list
 */
int fb_length;

/**
 * @brief Builds a list of locations that cannot be moved to (client side).
 * @sa G_MoveCalc
 * @sa G_BuildForbiddenList <- server side
 * @sa Grid_CheckForbidden
 * @note This is used for pathfinding.
 * It is a list of where the selected unit can not move to because others are standing there already.
 */
static void CL_BuildForbiddenList (void)
{
	le_t *le;
	int i;

	fb_length = 0;

	for (i = 0, le = LEs; i < numLEs; i++, le++) {
		if (!le->inuse || le->invis)
			continue;
		/* Dead ugv will stop walking, too. */
		/**
		 * @note Just a note for the futuR_
		 * If we get any ugv that does not block the map when dead this is the place to look.
		 */
		if ((!(le->state & STATE_DEAD) && le->type == ET_ACTOR) || le->type == ET_ACTOR2x2) {
			fb_list[fb_length++] = le->pos;
			fb_list[fb_length++] = (byte*)&le->fieldSize;
		}
	}

#ifdef PARANOID
	if (fb_length > MAX_FORBIDDENLIST)
		Com_Error(ERR_DROP, "CL_BuildForbiddenList: list too long");
#endif
}

#ifdef DEBUG
/**
 * @brief Draws a marker for all blocked map-positions.
 * @note currently uses basically the same code as CL_BuildForbiddenList
 * @note usage in console: "debug_drawblocked"
 * @todo currently the particles stay a _very_ long time ... so everybody has to stand still in order for the display to be correct.
 * @sa CL_BuildForbiddenList
 */
void CL_DisplayBlockedPaths_f (void)
{
	le_t *le = NULL;
	int i;
	ptl_t *ptl = NULL;
	vec3_t s;

	for (i = 0, le = LEs; i < numLEs; i++, le++) {
		if (!le->inuse)
			continue;

		if (!(le->state & STATE_DEAD)
		&& (le->type == ET_ACTOR || le->type == ET_ACTOR2x2)) {
			/* draw blocking cursor at le->pos */
			Grid_PosToVec(&clMap, le->pos, s);
			ptl = CL_ParticleSpawn("cross", 0, s, NULL, NULL);
			ptl->rounds = 2;
			ptl->roundsCnt = 2;
			ptl->life = 10000;
			ptl->t = 0;
			if (le->fieldSize == ACTOR_SIZE_2x2) {
				/* If this actor blocks 4 fields draw them as well. */
				pos3_t temp;
				ptl_t *ptl2 = NULL;
				ptl_t *ptl3 = NULL;
				ptl_t *ptl4 = NULL;

				VectorSet(temp,  le->pos[0]+1,  le->pos[1],  le->pos[2]);
				Grid_PosToVec(&clMap,temp, s);
				ptl2 = CL_ParticleSpawn("cross", 0, s, NULL, NULL);
				ptl2->rounds = ptl->rounds;
				ptl2->roundsCnt = ptl->roundsCnt;
				ptl2->life = ptl->life;
				ptl2->t = ptl->t;

				VectorSet(temp,  le->pos[0],  le->pos[1]+1,  le->pos[2]);
				Grid_PosToVec(&clMap,temp, s);
				ptl3 = CL_ParticleSpawn("cross", 0, s, NULL, NULL);
				ptl3->rounds = ptl->rounds;
				ptl3->roundsCnt = ptl->roundsCnt;
				ptl3->life = ptl->life;
				ptl3->t = ptl->t;

				VectorSet(temp,  le->pos[0]+1,  le->pos[1]+1,  le->pos[2]);
				Grid_PosToVec(&clMap,temp, s);
				ptl4 = CL_ParticleSpawn("cross", 0, s, NULL, NULL);
				ptl4->rounds = ptl->rounds;
				ptl4->roundsCnt = ptl->roundsCnt;
				ptl4->life = ptl->life;
				ptl4->t = ptl->t;
			}
		}
	}
}
#endif

/**
 * @brief Recalculate forbidden list, available moves and actor's move length
 * if given le is the selected Actor.
 * @param[in] map Routing data
 * @param[in] le Calculate the move for this actor (if he's the selected actor)
 * @param[in] distance
 */
void CL_ConditionalMoveCalc (struct routing_s *map, le_t *le, int distance)
{
	if (selActor && selActor == le) {
		CL_BuildForbiddenList();
		Grid_MoveCalc(map, le->pos, le->fieldSize, distance, fb_list, fb_length);
		CL_ResetActorMoveLength();
	}
}

/**
 * @brief Checks that an action is valid.
 */
static int CL_CheckAction (void)
{
	static char infoText[MAX_SMALLMENUTEXTLEN];

	if (!selActor) {
		Com_Printf("Nobody selected.\n");
		Com_sprintf(infoText, sizeof(infoText), _("Nobody selected\n"));
		return qfalse;
	}

/*	if (blockEvents) {
		Com_Printf("Can't do that right now.\n");
		return qfalse;
	}
*/
	if (cls.team != cl.actTeam) {
		CL_DisplayHudMessage(_("This isn't your round\n"), 2000);
		return qfalse;
	}

	return qtrue;
}

/**
 * @brief Draws the way to walk when confirm actions is activated.
 * @param[in] to
 */
static qboolean CL_TraceMove (pos3_t to)
{
	int length;
	vec3_t vec, oldVec;
	pos3_t pos;
	int dv;

	length = Grid_MoveLength(&clMap, to, qfalse);

	if (!selActor || !length || length >= 0x3F)
		return qfalse;

	Grid_PosToVec(&clMap, to, oldVec);
	VectorCopy(to, pos);

	while ((dv = Grid_MoveNext(&clMap, pos)) < ROUTING_NOT_REACHABLE) {
		length = Grid_MoveLength(&clMap, pos, qfalse);
		PosAddDV(pos, dv);
		Grid_PosToVec(&clMap, pos, vec);
		if (length > selActor->TU)
			CL_ParticleSpawn("longRangeTracer", 0, vec, oldVec, NULL);
		else if (selActor->state & STATE_CROUCHED)
			CL_ParticleSpawn("crawlTracer", 0, vec, oldVec, NULL);
		else
			CL_ParticleSpawn("moveTracer", 0, vec, oldVec, NULL);
		VectorCopy(vec, oldVec);
	}
	return qtrue;
}

/**
 * @brief Starts moving actor.
 * @param[in] le
 * @param[in] to
 * @sa CL_ActorActionMouse
 * @sa CL_ActorSelectMouse
 */
void CL_ActorStartMove (le_t * le, pos3_t to)
{
	int length;

	if (mouseSpace != MS_WORLD)
		return;

	if (!CL_CheckAction())
		return;

	/* the actor is still moving */
	if (blockEvents)
		return;

	length = Grid_MoveLength(&clMap, to, qfalse);

	if (!length || length >= ROUTING_NOT_REACHABLE) {
		/* move not valid, don't even care to send */
		return;
	}

	/* change mode to move now */
	cl.cmode = M_MOVE;

	/* move seems to be possible; send request to server */
	MSG_Write_PA(PA_MOVE, le->entnum, to);
}


/**
 * @brief Shoot with actor.
 * @param[in] le Who is shooting
 * @param[in] at Position you are targetting to
 */
void CL_ActorShoot (le_t * le, pos3_t at)
{
	int type;

	if (mouseSpace != MS_WORLD)
		return;

	if (!CL_CheckAction())
		return;

	Com_DPrintf(DEBUG_CLIENT, "CL_ActorShoot: cl.firemode %i.\n",  cl.cfiremode);

	/* @todo: Is there a better way to do this?
	 * This type value will travel until it is checked in at least g_combat.c:G_GetShotFromType.
	 */
	if (IS_MODE_FIRE_RIGHT(cl.cmode)) {
		type = ST_RIGHT;
	} else if (IS_MODE_FIRE_LEFT(cl.cmode)) {
		type = ST_LEFT;
	} else if (IS_MODE_FIRE_HEADGEAR(cl.cmode)) {
		type = ST_HEADGEAR;
	} else
		return;

	MSG_Write_PA(PA_SHOOT, le->entnum, at, type, cl.cfiremode, mousePosTargettingAlign);
}


/**
 * @brief Reload weapon with actor.
 * @param[in] hand
 * @sa CL_CheckAction
 */
void CL_ActorReload (int hand)
{
	inventory_t *inv;
	invList_t *ic;
	int weapon, x, y, tu;
	int container, bestContainer;

	if (!CL_CheckAction())
		return;

	/* check weapon */
	inv = &selActor->i;

	/* search for clips and select the one that is available easily */
	x = 0;
	y = 0;
	tu = 100;
	bestContainer = NONE;

	if (inv->c[hand]) {
		weapon = inv->c[hand]->item.t;
	} else if (hand == csi.idLeft
		&& csi.ods[inv->c[csi.idRight]->item.t].holdTwoHanded) {
		/* Check for two-handed weapon */
		hand = csi.idRight;
		weapon = inv->c[hand]->item.t;
	} else
		/* otherwise we could use weapon uninitialized */
		return;

	if (weapon == NONE)
		return;

	/* return if the weapon is not reloadable */
	if (!csi.ods[weapon].reload)
		return;

	if (!RS_ItemIsResearched(csi.ods[weapon].id)) {
		CL_DisplayHudMessage(_("You cannot reload this unknown item.\nYou need to research it and its ammunition first.\n"), 2000);
		return;
	}

	for (container = 0; container < csi.numIDs; container++) {
		if (csi.ids[container].out < tu) {
			/* Once we've found at least one clip, there's no point */
			/* searching other containers if it would take longer */
			/* to retrieve the ammo from them than the one */
			/* we've already found. */
			for (ic = inv->c[container]; ic; ic = ic->next)
				if (INVSH_LoadableInWeapon(&csi.ods[ic->item.t], weapon)
				 && RS_ItemIsResearched(csi.ods[ic->item.t].id)) {
					x = ic->x;
					y = ic->y;
					tu = csi.ids[container].out;
					bestContainer = container;
					break;
				}
		}
	}

	/* send request */
	if (bestContainer != NONE)
		MSG_Write_PA(PA_INVMOVE, selActor->entnum, bestContainer, x, y, hand, 0, 0);
	else
		Com_Printf("No (researched) clip left.\n");
}

/**
 * @brief The client changed something in his hand-containers. This function updates the reactionfire info.
 * @param[in] msg
 */
void CL_InvCheckHands (struct dbuffer *msg)
{
	int entnum;
	le_t *le;
	int actor_idx = -1;
	int hand = -1;		/**< 0=right, 1=left -1=undef*/
	int firemode_idx = -1;

	objDef_t *weapon = NULL;
	objDef_t *ammo = NULL;
	int weap_fd_idx = NONE;

	NET_ReadFormat(msg, ev_format[EV_INV_HANDS_CHANGED], &entnum, &hand);

	if ((entnum < 0) || (hand < 0)) {
		Com_Printf("CL_InvCheckHands: entnum or hand not sent/received correctly. (number: %i)\n", entnum);
	}

	le = LE_Get(entnum);
	if (!le) {
		Com_Printf("CL_InvCheckHands: LE doesn't exist. (number: %i)\n", entnum);
		return;
	}

	actor_idx = CL_GetActorNumber(le);
	if (actor_idx == -1) {
		Com_DPrintf(DEBUG_CLIENT, "CL_InvCheckHands: Could not get local entity actor id via CL_GetActorNumber\n");
		Com_DPrintf(DEBUG_CLIENT, "CL_InvCheckHands: DEBUG actor info: team=%i(%s) type=%i inuse=%i\n", le->team, le->teamDef ? le->teamDef->name : "No team", le->type, le->inuse);
		return;
	}

	/* Check if current RF-selection is sane (and in the other hand) ... */
	if (SANE_REACTION(actor_idx) && (reactionFiremode[actor_idx][RF_HAND] != hand)) {
		CL_GetWeaponAndAmmo(le, reactionFiremode[actor_idx][RF_HAND], &weapon, &ammo, &weap_fd_idx); /* get info about other hand */

		/* Break if the currently selected RF mode is ok. */
		if (weapon && (weap_fd_idx != NONE)) {
			firemode_idx = reactionFiremode[actor_idx][RF_FM];
			if (ammo->fd[weap_fd_idx][firemode_idx].reaction)
				return;
		}
	}

	/* ... ELSE  (not sane and/or not useable) */
	/* Update the changed hand with default firemode. */
	if (hand == 0) {
		Com_DPrintf(DEBUG_CLIENT, "CL_InvCheckHands: DEBUG right\n");
		CL_UpdateReactionFiremodes('r', actor_idx, -1);
	} else {
		Com_DPrintf(DEBUG_CLIENT, "CL_InvCheckHands: DEBUG left\n");
		CL_UpdateReactionFiremodes('l', actor_idx, -1);
	}
	HideFiremodes();
}

/**
 * @brief Moves actor.
 * @param[in] msg
 * @sa LET_PathMove
 * @note EV_ACTOR_MOVE
 */
void CL_ActorDoMove (struct dbuffer *msg)
{
	le_t *le;
	int number, i;

	number = NET_ReadShort(msg);
	/* get le */
	le = LE_Get(number);
	if (!le) {
		Com_Printf("CL_ActorDoMove: Could not get LE with id %i\n", number);
		return;
	}

	switch (le->type) {
	case ET_ACTORHIDDEN:
	case ET_ACTOR:
	case ET_ACTOR2x2:
		break;
	default:
		Com_Printf("Can't move, LE doesn't exist or is not an actor (number: %i, type: %i)\n",
			number, le ? le->type : -1);
		return;
	}

	if (le->state & STATE_DEAD) {
		Com_Printf("Can't move, actor dead\n");
		return;
	}

#ifdef DEBUG
	/* get length/steps */
	if (le->pathLength || le->pathPos)
		Com_DPrintf(DEBUG_CLIENT, "CL_ActorDoMove: Looks like the previous movement wasn't finished but a new one was received already\n");
#endif

	le->pathLength = NET_ReadByte(msg);
	assert(le->pathLength <= MAX_LE_PATHLENGTH);
	for (i = 0; i < le->pathLength; i++) {
		le->path[i] = NET_ReadByte(msg);
		le->pathContents[i] = NET_ReadShort(msg);
	}

	/* activate PathMove function */
	FLOOR(le) = NULL;
	le->think = LET_StartPathMove;
	le->pathPos = 0;
	le->startTime = cl.time;
	le->endTime = cl.time;

	/* FIXME: speed should somehow depend on strength of character */
	if (le->state & STATE_CROUCHED)
		le->speed = 50;
	else
		le->speed = 100;
	CL_BlockEvents();
}


/**
 * @brief Turns the actor around without moving
 */
void CL_ActorTurnMouse (void)
{
	vec3_t div;
	byte dv;

	if (mouseSpace != MS_WORLD)
		return;

	if (!CL_CheckAction())
		return;

	/* check for fire-modes, and cancel them */
	switch (cl.cmode) {
	case M_FIRE_R:
	case M_FIRE_L:
	case M_PEND_FIRE_R:
	case M_PEND_FIRE_L:
		cl.cmode = M_MOVE;
		return; /* and return without turning */
	default:
		break;
	}

	/* calculate dv */
	VectorSubtract(mousePos, selActor->pos, div);
	dv = AngleToDV((int) (atan2(div[1], div[0]) * todeg));

	/* send message to server */
	MSG_Write_PA(PA_TURN, selActor->entnum, dv);
}


/**
 * @brief Turns actor.
 * @param[in] msg
 */
void CL_ActorDoTurn (struct dbuffer *msg)
{
	le_t *le;
	int entnum, dir;

	NET_ReadFormat(msg, ev_format[EV_ACTOR_TURN], &entnum, &dir);

	/* get le */
	le = LE_Get(entnum);
	if (!le) {
		Com_Printf("CL_ActorDoTurn: Could not get LE with id %i\n", entnum);
		return;
	}

	switch (le->type) {
	case ET_ACTORHIDDEN:
	case ET_ACTOR:
	case ET_ACTOR2x2:
		break;
	default:
		Com_Printf("Can't turn, LE doesn't exist or is not an actor (number: %i, type: %i)\n", entnum, le ? le->type : -1);
		return;
	}

	if (le->state & STATE_DEAD) {
		Com_Printf("Can't turn, actor dead\n");
		return;
	}

	le->dir = dir;
	le->angles[YAW] = dangle[le->dir];
}


/**
 * @brief Stands or crouches actor.
 */
void CL_ActorStandCrouch_f (void)
{
	if (!CL_CheckAction())
		return;

	if (selActor->fieldSize == ACTOR_SIZE_2x2)
		/** @todo future thoughs: maybe define this in team_*.ufo files instead? */
		return;
	/* send a request to toggle crouch to the server */
	MSG_Write_PA(PA_STATE, selActor->entnum, STATE_CROUCHED);
}

/**
 * @brief Toggles the headgear for the current selected player
 */
void CL_ActorUseHeadgear_f (void)
{
	invList_t* headgear;
	int tmp_mouseSpace= mouseSpace;

	/* this can be executed by a click on a hud button
	 * but we need MS_WORLD mouse space to let the shooting
	 * function work */
	mouseSpace = MS_WORLD;

	if (!CL_CheckAction())
		return;

	headgear = HEADGEAR(selActor);
	if (!headgear)
		return;

	cl.cmode = M_FIRE_HEADGEAR;
	cl.cfiremode = 0; /** @todo make this a variable somewhere? */
	CL_ActorShoot(selActor, selActor->pos);
	cl.cmode = M_MOVE;

	/* restore old mouse space */
	mouseSpace = tmp_mouseSpace;
}

/**
 * @brief Toggles reaction fire between the states off/single/multi.
 * @note RF mode will change as follows (Current mode -> resulting mode after click)
 * 	off	-> single
 * 	single	-> multi
 * 	multi	-> off
 * 	single	-> off (if no TUs for multi available)
 */
void CL_ActorToggleReaction_f (void)
{
	int state = 0;
	int actor_idx = -1;

	if (!CL_CheckAction())
		return;

	actor_idx = CL_GetActorNumber(selActor);
	if (actor_idx == -1)
		Com_Error(ERR_DROP, "Could not get current selected actor's id");

	selActorReactionState++;
	if (selActorReactionState > R_FIRE_MANY)
		selActorReactionState = R_FIRE_OFF;

	/* Check all hands for reaction-enabled ammo-firemodes. */
	if (CL_WeaponWithReaction(selActor, 'r') || CL_WeaponWithReaction(selActor, 'l')) {
		/* At least one weapon is RF capable. */

		switch (selActorReactionState) {
		case R_FIRE_OFF:
			state = ~STATE_REACTION;
			break;
		case R_FIRE_ONCE:
			state = STATE_REACTION_ONCE;
			/* @todo: Check if stored info for RF is up-to-date and set it to default if not. */
			/* Set default rf-mode. */
			if (!SANE_REACTION(actor_idx)) {
				CL_SetDefaultReactionFiremode(actor_idx, 'r');
			}
			break;
		case R_FIRE_MANY:
			state = STATE_REACTION_MANY;
			/* @todo: Check if stored info for RF is up-to-date and set it to default if not. */
			/* Set default rf-mode. */
			if (!SANE_REACTION(actor_idx)) {
				CL_SetDefaultReactionFiremode(actor_idx, 'r');
			}
			break;
		default:
			return;
		}

		/* Update RF list if it is visible. */
		if (visible_firemode_list_left || visible_firemode_list_right)
			CL_DisplayFiremodes_f();

		/* Send request to update actor's reaction state to the server. */
		MSG_Write_PA(PA_STATE, selActor->entnum, state);
	} else {
		/* No useable RF weapon. */
		switch (selActorReactionState) {
		case R_FIRE_OFF:
			state = ~STATE_REACTION;
			break;
		case R_FIRE_ONCE:
			state = STATE_REACTION_ONCE;
			break;
		case R_FIRE_MANY:
			state = STATE_REACTION_MANY;
			break;
		default:
			/* Display "impossible" reaction button or disable button. */
			CL_DisplayImpossibleReaction(selActor);
			break;
		}

		/* Send request to update actor's reaction state to the server. */
		MSG_Write_PA(PA_STATE, selActor->entnum, state);

		/* Set RF-mode info to undef. */
		CL_SetReactionFiremode(actor_idx, -1, NONE, -1);
	}
}

/**
 * @brief Spawns particle effects for a hit actor.
 * @param[in] le The actor to spawn teh particles for.
 * @param[in] impact The impact location (where the particles are spawned).
 * @param[in] normal The index of the normal vector of the particles (think: impact angle).
 * @todo Get real impact location and direction?
 */
static void CL_ActorHit (le_t *le, vec3_t impact, int normal)
{
	if (!le) {
		Com_DPrintf(DEBUG_CLIENT, "CL_ActorHit: Can't spawn particles, LE doesn't exist\n");
		return;
	}

	switch (le->type) {
	case ET_ACTORHIDDEN:
	case ET_ACTOR:
	case ET_ACTOR2x2:
		break;
	default:
		Com_Printf("CL_ActorHit: Can't spawn particles, LE is not an actor (type: %i)\n", le->type);
		return;
	}
#if 0
	else if (le->state & STATE_DEAD) {
		Com_Printf("CL_ActorHit: Can't spawn particles, actor already dead\n");
		return;
	}
#endif

	if (le->teamDef) {
		/* Spawn "hit_particle" if defined in teamDef. */
		if (le->teamDef->hitParticle[0])
			CL_ParticleSpawn(le->teamDef->hitParticle, 0, impact, bytedirs[normal], NULL);
	}
}

/**
 * @brief Records if shot is first shot.
 */
static qboolean firstShot = qfalse;

/**
 * @brief Shoot with weapon.
 * @sa CL_ActorShoot
 * @sa CL_ActorShootHidden
 * @todo Improve detection of left- or right animation.
 * @sa EV_ACTOR_SHOOT
 */
void CL_ActorDoShoot (struct dbuffer *msg)
{
	fireDef_t *fd;
	le_t *le;
	vec3_t muzzle, impact;
	int flags, normal, number;
	int obj_idx;
	int weap_fds_idx, fd_idx, surfaceFlags;

	/* read data */
	NET_ReadFormat(msg, ev_format[EV_ACTOR_SHOOT], &number, &obj_idx, &weap_fds_idx, &fd_idx, &flags, &surfaceFlags, &muzzle, &impact, &normal);

	/* get le */
	le = LE_Get(number);

	/* get the fire def */
	fd = FIRESH_GetFiredef(obj_idx, weap_fds_idx, fd_idx);

	/* add effect le */
	LE_AddProjectile(fd, flags, muzzle, impact, normal, qtrue);

	/* start the sound */
	if ((!fd->soundOnce || firstShot) && fd->fireSound[0] && !(flags & SF_BOUNCED)) {
		S_StartLocalSound(fd->fireSound);
	}

	firstShot = qfalse;

	if (fd->irgoggles)
		refdef.rdflags |= RDF_IRGOGGLES;

	/* do actor related stuff */
	if (!le)
		return; /* maybe hidden or inuse is false? */

	switch (le->type) {
	case ET_ACTORHIDDEN:
	case ET_ACTOR:
	case ET_ACTOR2x2:
		break;
	default:
		Com_Printf("Can't shoot, LE not an actor (type: %i)\n", le->type);
		return;
	}

	/* Spawn blood particles (if defined) if actor was hit. Even if actor is dead :) */
	if (flags & SF_BODY) { /**< @todo && !(flags & SF_BOUNCED) ? */
		CL_ActorHit(le, impact, normal);
	}

	if (le->state & STATE_DEAD) {
		Com_Printf("Can't shoot, actor dead\n");
		return;
	}

	/* if actor on our team, set this le as the last moving */
	if (le->team == cls.team)
		CL_SetLastMoving(le);

	/* no animations for hidden actors */
	if (le->type == ET_ACTORHIDDEN)
		return;

	/* Animate - we have to check if it is right or left weapon usage. */
	/* @todo: FIXME the left/right info for actors in the enemy team/turn has to come from somewheR_ */
	if (HEADGEAR(le) && IS_MODE_FIRE_HEADGEAR(cl.cmode)) {
		/* No animation for this */
	} else if (RIGHT(le) && IS_MODE_FIRE_RIGHT(cl.cmode)) {
		R_AnimChange(&le->as, le->model1, LE_GetAnim("shoot", le->right, le->left, le->state));
		R_AnimAppend(&le->as, le->model1, LE_GetAnim("stand", le->right, le->left, le->state));
	} else if (LEFT(le) && IS_MODE_FIRE_LEFT(cl.cmode)) {
		R_AnimChange(&le->as, le->model1, LE_GetAnim("shoot", le->left, le->right, le->state));
		R_AnimAppend(&le->as, le->model1, LE_GetAnim("stand", le->left, le->right, le->state));
	} else {
		Com_DPrintf(DEBUG_CLIENT, "CL_ActorDoShoot: No information about weapon hand found or left/right info out of sync somehow.\n");
		/* We use the default (right) animation now. */
		R_AnimChange(&le->as, le->model1, LE_GetAnim("shoot", le->right, le->left, le->state));
		R_AnimAppend(&le->as, le->model1, LE_GetAnim("stand", le->right, le->left, le->state));
	}
}


/**
 * @brief Shoot with weapon but don't bother with animations - actor is hidden.
 * @sa CL_ActorShoot
 */
void CL_ActorShootHidden (struct dbuffer *msg)
{
	fireDef_t	*fd;
	int first;
	int obj_idx;
	int weap_fds_idx, fd_idx;

	NET_ReadFormat(msg, ev_format[EV_ACTOR_SHOOT_HIDDEN], &first, &obj_idx, &weap_fds_idx, &fd_idx);

	/* get the fire def */
	fd = FIRESH_GetFiredef(obj_idx, weap_fds_idx, fd_idx);

	/* start the sound; @todo: is check for SF_BOUNCED needed? */
	if (((first && fd->soundOnce) || (!first && !fd->soundOnce)) && fd->fireSound[0]) {
		S_StartLocalSound(fd->fireSound);
	}

	/* if the shooting becomes visible, don't repeat sounds! */
	firstShot = qfalse;
}


/**
 * @brief Throw item with actor.
 * @param[in] msg
 */
void CL_ActorDoThrow (struct dbuffer *msg)
{
	fireDef_t *fd;
	vec3_t muzzle, v0;
	int flags;
	int dtime;
	int obj_idx;
	int weap_fds_idx, fd_idx;

	/* read data */
	NET_ReadFormat(msg, ev_format[EV_ACTOR_THROW], &dtime, &obj_idx, &weap_fds_idx, &fd_idx, &flags, &muzzle, &v0);

	/* get the fire def */
	fd = FIRESH_GetFiredef(obj_idx, weap_fds_idx, fd_idx);

	/* add effect le (local entity) */
	LE_AddGrenade(fd, flags, muzzle, v0, dtime);

	/* start the sound */
	if ((!fd->soundOnce || firstShot) && fd->fireSound[0] && !(flags & SF_BOUNCED)) {
		sfx_t *sfx = S_RegisterSound(fd->fireSound);
		S_StartSound(muzzle, sfx, DEFAULT_SOUND_PACKET_VOLUME, DEFAULT_SOUND_PACKET_ATTENUATION);
	}

	firstShot = qfalse;
}


/**
 * @brief Starts shooting with actor.
 * @param[in] msg
 * @sa CL_ActorShootHidden
 * @sa CL_ActorShoot
 * @sa CL_ActorDoShoot
 * @todo Improve detection of left- or right animation.
 * @sa EV_ACTOR_START_SHOOT
 */
void CL_ActorStartShoot (struct dbuffer *msg)
{
	fireDef_t *fd;
	le_t *le;
	pos3_t from, target;
	int number;
	int obj_idx;
	int weap_fds_idx, fd_idx;

	NET_ReadFormat(msg, ev_format[EV_ACTOR_START_SHOOT], &number, &obj_idx, &weap_fds_idx, &fd_idx, &from, &target);

	fd = FIRESH_GetFiredef(obj_idx, weap_fds_idx, fd_idx);

	le = LE_Get(number);

	/* center view (if wanted) */
	if (cl_centerview->integer && cl.actTeam != cls.team)
		CL_CameraRoute(from, target);

	/* first shot */
	firstShot = qtrue;

	/* actor dependant stuff following */
	if (!le)
		/* it's OK, the actor not visible */
		return;

	switch (le->type) {
	case ET_ACTORHIDDEN:
	case ET_ACTOR:
	case ET_ACTOR2x2:
		break;
	default:
		Com_Printf("CL_ActorStartShoot: LE (%i) not an actor (type: %i)\n", number, le->type);
		return;
	}

	if (le->state & STATE_DEAD) {
		Com_Printf("CL_ActorStartShoot: Can't start shoot, actor (%i) dead\n", number);
		return;
	}

	/* erase one-time weapons from storage --- which ones?
	if (curCampaign && le->team == cls.team && !csi.ods[type].ammo) {
		if (ccs.eMission.num[type])
			ccs.eMission.num[type]--;
	} */

	/* Animate - we have to check if it is right or left weapon usage. */
	/* @todo: FIXME the left/right info for actors in the enemy team/turn has to come from somewheR_ */
	if (RIGHT(le) && IS_MODE_FIRE_RIGHT(cl.cmode)) {
		R_AnimChange(&le->as, le->model1, LE_GetAnim("move", le->right, le->left, le->state));
	} else if (LEFT(le) && IS_MODE_FIRE_LEFT(cl.cmode)) {
		R_AnimChange(&le->as, le->model1, LE_GetAnim("move", le->left, le->right, le->state));
	} else {
		Com_DPrintf(DEBUG_CLIENT, "CL_ActorStartShoot: No information about weapon hand found or left/right info out of sync somehow.\n");
		/* We use the default (right) animation now. */
		R_AnimChange(&le->as, le->model1, LE_GetAnim("move", le->right, le->left, le->state));
	}
}

/**
 * @brief Kills actor.
 * @param[in] msg
 */
void CL_ActorDie (struct dbuffer *msg)
{
	le_t *le;
	int number, state;
	int i;
	char tmpbuf[128];
	character_t *chr;

	NET_ReadFormat(msg, ev_format[EV_ACTOR_DIE], &number, &state);

	/* get le */
	for (i = 0, le = LEs; i < numLEs; i++, le++)
		if (le->entnum == number)
			break;

	if (le->entnum != number) {
		Com_DPrintf(DEBUG_CLIENT, "CL_ActorDie: Can't kill, LE doesn't exist\n");
		return;
	}

	switch (le->type) {
	case ET_ACTORHIDDEN:
	case ET_ACTOR:
	case ET_ACTOR2x2:
		break;
	default:
		Com_Printf("CL_ActorDie: Can't kill, LE is not an actor (type: %i)\n", le->type);
		return;
	}

	if (le->state & STATE_DEAD) {
		Com_Printf("CL_ActorDie: Can't kill, actor already dead\n");
		return;
	}
	/* else if (!le->inuse) {
		* LE not in use condition normally arises when CL_EntPerish has been
		* called on the le to hide it from the client's sight.
		* Probably can return without killing unused LEs, but testing reveals
		* killing an unused LE here doesn't cause any problems and there is
		* an outside chance it fixes some subtle bugs.
	}*/

	/* count spotted aliens */
	if (le->team != cls.team && le->team != TEAM_CIVILIAN && le->inuse)
		cl.numAliensSpotted--;

	/* set relevant vars */
	FLOOR(le) = NULL;
	le->STUN = 0;
	le->state = state;

	/* play animation */
	le->think = NULL;
	R_AnimChange(&le->as, le->model1, va("death%i", le->state & STATE_DEAD));
	R_AnimAppend(&le->as, le->model1, va("dead%i", le->state & STATE_DEAD));

	/* Print some info about the death or stun. */
	if (le->team == cls.team) {
		chr = CL_GetActorChr(le);
		if (chr && ((le->state & STATE_STUN) & ~STATE_DEAD)) {
			Com_sprintf(tmpbuf, sizeof(tmpbuf), _("%s %s was stunned\n"),
			chr->rank >= 0 ? gd.ranks[chr->rank].shortname : "", chr->name);
			CL_DisplayHudMessage(tmpbuf, 2000);
		} else if (chr) {
			Com_sprintf(tmpbuf, sizeof(tmpbuf), _("%s %s was killed\n"),
			chr->rank >= 0 ? gd.ranks[chr->rank].shortname : "", chr->name);
			CL_DisplayHudMessage(tmpbuf, 2000);
		}
	} else {
		switch (le->team) {
		case (TEAM_CIVILIAN):
			if ((le->state & STATE_STUN) & ~STATE_DEAD)
				CL_DisplayHudMessage(_("A civilian was stunned.\n"), 2000);
			else
				CL_DisplayHudMessage(_("A civilian was killed.\n"), 2000);
			break;
		case (TEAM_ALIEN):
			if (le->teamDef) {
				if (RS_IsResearched_idx(RS_GetTechIdxByName(le->teamDef->tech))) {
					if ((le->state & STATE_STUN) & ~STATE_DEAD) {
						Com_sprintf(tmpbuf, sizeof(tmpbuf), _("An alien was stunned: %s\n"), _(le->teamDef->name));
						CL_DisplayHudMessage(tmpbuf, 2000);
					} else {
						Com_sprintf(tmpbuf, sizeof(tmpbuf), _("An alien was killed: %s\n"), _(le->teamDef->name));
						CL_DisplayHudMessage(tmpbuf, 2000);
					}
				} else {
					if ((le->state & STATE_STUN) & ~STATE_DEAD)
						CL_DisplayHudMessage(_("An alien was stunned.\n"), 2000);
					else
						CL_DisplayHudMessage(_("An alien was killed.\n"), 2000);
				}
			} else {
				if ((le->state & STATE_STUN) & ~STATE_DEAD)
					CL_DisplayHudMessage(_("An alien was stunned.\n"), 2000);
				else
					CL_DisplayHudMessage(_("An alien was killed.\n"), 2000);
			}
			break;
		case (TEAM_PHALANX):
			if ((le->state & STATE_STUN) & ~STATE_DEAD)
				CL_DisplayHudMessage(_("A soldier was stunned.\n"), 2000);
			else
				CL_DisplayHudMessage(_("A soldier was killed.\n"), 2000);
			break;
		default:
			if ((le->state & STATE_STUN) & ~STATE_DEAD)
				CL_DisplayHudMessage(va(_("A member of team %i was stunned.\n"), le->team), 2000);
			else
				CL_DisplayHudMessage(va(_("A member of team %i was killed.\n"), le->team), 2000);
			break;
		}
	}

	CL_PlayActorSound(le, SND_DEATH);

	VectorCopy(player_dead_maxs, le->maxs);
	CL_RemoveActorFromTeamList(le);

	CL_ConditionalMoveCalc(&clMap, selActor, MAX_ROUTE);
}


/*
==============================================================
MOUSE INPUT
==============================================================
*/

/**
 * @brief handle select or action clicking in either move mode
 * @sa CL_ActorSelectMouse
 * @sa CL_ActorActionMouse
 */
static void CL_ActorMoveMouse (void)
{
	if (cl.cmode == M_PEND_MOVE) {
		if (VectorCompare(mousePos, mousePendPos)) {
			/* Pending move and clicked the same spot (i.e. 2 clicks on the same place) */
			CL_ActorStartMove(selActor, mousePos);
		} else {
			/* Clicked different spot. */
			VectorCopy(mousePos, mousePendPos);
		}
	} else {
		if (confirm_actions->integer) {
			/* Set our mode to pending move. */
			VectorCopy(mousePos, mousePendPos);

			cl.cmode = M_PEND_MOVE;
		} else {
			/* Just move theR_ */
			CL_ActorStartMove(selActor, mousePos);
		}
	}
}

/**
 * @brief Selects an actor using the mouse.
 * @todo Comment on the cl.cmode stuff.
 */
void CL_ActorSelectMouse (void)
{
	if (mouseSpace != MS_WORLD)
		return;

	switch (cl.cmode) {
	case M_MOVE:
	case M_PEND_MOVE:
		/* Try and select another team member */
		if ((mouseActor != selActor) && CL_ActorSelect(mouseActor)) {
			/* Succeeded so go back into move mode. */
			cl.cmode = M_MOVE;
		} else {
			CL_ActorMoveMouse();
		}
		break;
	case M_PEND_FIRE_R:
	case M_PEND_FIRE_L:
		if (VectorCompare(mousePos, mousePendPos)) {
			/* Pending shot and clicked the same spot (i.e. 2 clicks on the same place) */
			CL_ActorShoot(selActor, mousePos);

			/* We switch back to aiming mode. */
			if (cl.cmode == M_PEND_FIRE_R)
				cl.cmode = M_FIRE_R;
			else
				cl.cmode = M_FIRE_L;
		} else {
			/* Clicked different spot. */
			VectorCopy(mousePos, mousePendPos);
		}
		break;
	case M_FIRE_R:
		if (mouseActor == selActor)
			break;

		/* We either switch to "pending" fire-mode or fire the gun. */
		if (confirm_actions->integer == 1) {
			cl.cmode = M_PEND_FIRE_R;
			VectorCopy(mousePos, mousePendPos);
		} else {
			CL_ActorShoot(selActor, mousePos);
		}
		break;
	case M_FIRE_L:
		if (mouseActor == selActor)
			break;

		/* We either switch to "pending" fire-mode or fire the gun. */
		if (confirm_actions->integer == 1) {
			cl.cmode = M_PEND_FIRE_L;
			VectorCopy(mousePos, mousePendPos);
		} else {
			CL_ActorShoot(selActor, mousePos);
		}
		break;
	default:
		break;
	}
}


/**
 * @brief initiates action with mouse.
 * @sa CL_ActionDown
 * @sa CL_ActorStartMove
 */
void CL_ActorActionMouse (void)
{
	if (!selActor || mouseSpace != MS_WORLD)
		return;

	if (cl.cmode == M_MOVE || cl.cmode == M_PEND_MOVE) {
		CL_ActorMoveMouse();
	} else {
		cl.cmode = M_MOVE;
	}
}


/*==============================================================
ROUND MANAGEMENT
==============================================================*/

/**
 * @brief Finishes the current round of the player in battlescape and starts the round for the next team.
 */
void CL_NextRound_f (void)
{
	struct dbuffer *msg;
	/* can't end round if we are not in battlescape */
	if (!CL_OnBattlescape())
		return;

	/* can't end round if we're not active */
	if (cls.team != cl.actTeam)
		return;

	/* send endround */
	msg = new_dbuffer();
	NET_WriteByte(msg, clc_endround);
	NET_WriteMsg(cls.stream, msg);

	/* change back to remote view */
	if (camera_mode == CAMERA_MODE_FIRSTPERSON)
		CL_CameraModeChange(CAMERA_MODE_REMOTE);
}

/**
 * @brief Performs end-of-turn processing.
 * @param[in] msg
 * @sa CL_EndRoundAnnounce
 */
void CL_DoEndRound (struct dbuffer *msg)
{
	int actor_idx;

	/* hud changes */
	if (cls.team == cl.actTeam)
		Cbuf_AddText("endround\n");

	refdef.rdflags &= ~RDF_IRGOGGLES;

	/* change active player */
	Com_Printf("Team %i ended round", cl.actTeam);
	cl.actTeam = NET_ReadByte(msg);
	Com_Printf(", team %i's round started!\n", cl.actTeam);

	/* hud changes */
	if (cls.team == cl.actTeam) {
		/* check whether a particle has to go */
		CL_ParticleCheckRounds();
		Cbuf_AddText("startround\n");
		CL_DisplayHudMessage(_("Your round started!\n"), 2000);
		S_StartLocalSound("misc/roundstart");
		CL_ConditionalMoveCalc(&clMap, selActor, MAX_ROUTE);

		for (actor_idx = 0; actor_idx < cl.numTeamList; actor_idx++) {
			if (cl.teamList[actor_idx]) {
				/* Check if any default firemode is defined and search for one if not. */
				if (!SANE_REACTION(actor_idx)) {
					CL_SetDefaultReactionFiremode(actor_idx, 'r');
				}
			}
		}
	}
}


/*
==============================================================
MOUSE SCANNING
==============================================================
*/

/**
 * @brief Recalculates the currently selected Actor's move length.
 * */
void CL_ResetActorMoveLength (void)
{
	actorMoveLength = Grid_MoveLength(&clMap, mousePos, qfalse);
	if (selActor->state & STATE_CROUCHED)
		actorMoveLength *= 1.5;
}

/**
 * @brief Battlescape cursor positioning.
 * @note Sets global var mouseActor to current selected le
 * @sa IN_Parse
 */
void CL_ActorMouseTrace (void)
{
	int i, restingLevel, intersectionLevel;
	float cur[2], frustumslope[2], projectiondistance = 2048;
	float nDotP2minusP1, u;
	vec3_t forward, right, up, stop;
	vec3_t from, end, dir;
	vec3_t mapNormal, P3, P2minusP1, P3minusP1;
	vec3_t pA, pB, pC;
	pos3_t testPos;
	pos3_t actor2x2[3];

	int fieldSize = selActor /**< Get size of selected actor or fall back to 1x1. */
		? selActor->fieldSize
		: ACTOR_SIZE_NORMAL;

	le_t *le;

	/* get cursor position as a -1 to +1 range for projection */
	cur[0] = (mousePosX * viddef.rx - scr_vrect.width * 0.5 - scr_vrect.x) / (scr_vrect.width * 0.5);
	cur[1] = (mousePosY * viddef.ry - scr_vrect.height * 0.5 - scr_vrect.y) / (scr_vrect.height * 0.5);

	/* get trace vectors */
	if (camera_mode == CAMERA_MODE_FIRSTPERSON) {
		VectorCopy(selActor->origin, from);
		/* trace from eye-height */
		if (selActor->state & STATE_CROUCHED)
			from[2] += EYE_HT_CROUCH;
		else
			from[2] += EYE_HT_STAND;
		AngleVectors(cl.cam.angles, forward, right, up);
		/* set the intersection level to that of the selected actor */
		VecToPos(from, testPos);
		intersectionLevel = Grid_Fall(&clMap, testPos, fieldSize);

 		/* if looking up, raise the intersection level */
		if (cur[1] < 0.0f)
			intersectionLevel++;
	} else {
		VectorCopy(cl.cam.camorg, from);
		VectorCopy(cl.cam.axis[0], forward);
		VectorCopy(cl.cam.axis[1], right);
		VectorCopy(cl.cam.axis[2], up);
		intersectionLevel = cl_worldlevel->integer;
	}

	if (cl_isometric->integer)
		frustumslope[0] = 10.0 * refdef.fov_x;
	else
		frustumslope[0] = tan(refdef.fov_x * M_PI / 360) * projectiondistance;
	frustumslope[1] = frustumslope[0] * ((float)scr_vrect.height / scr_vrect.width);

	/* transform cursor position into perspective space */
	VectorMA(from, projectiondistance, forward, stop);
	VectorMA(stop, cur[0] * frustumslope[0], right, stop);
	VectorMA(stop, cur[1] * -frustumslope[1], up, stop);

	/* in isometric mode the camera position has to be calculated from the cursor position so that the trace goes in the right direction */
	if (cl_isometric->integer)
		VectorMA(stop, -projectiondistance * 2, forward, from);

	/* set stop point to the intersection of the trace line with the desired plane */
	/* description of maths used:
	 *   The equation for the plane can be written:
	 *     mapNormal dot (end - P3) = 0
	 *     where mapNormal is the vector normal to the plane,
	 *         P3 is any point on the plane and
	 *         end is the point where the line interesects the plane
	 *   All points on the line can be calculated using:
	 *     P1 + u*(P2 - P1)
	 *     where P1 and P2 are points that define the line and
	 *           u is some scalar
	 *   The intersection of the line and plane occurs when:
	 *     mapNormal dot (P1 + u*(P2 - P1)) == mapNormal dot P3
	 *   The intersection therefore occurs when:
	 *     u = (mapNormal dot (P3 - P1))/(mapNormal dot (P2 - P1))
	 * Note: in the code below from & stop represent P1 and P2 respectively
	 */
	VectorSet(P3, 0., 0., intersectionLevel * UNIT_HEIGHT + CURSOR_OFFSET);
	VectorSet(mapNormal, 0., 0., 1.);
	VectorSubtract(stop, from, P2minusP1);
	nDotP2minusP1 = DotProduct(mapNormal, P2minusP1);

	/* calculate intersection directly if angle is not parallel to the map plane */
	if (nDotP2minusP1 > 0.01 || nDotP2minusP1 < -0.01) {
		VectorSubtract(P3, from,  P3minusP1);
		u = DotProduct(mapNormal, P3minusP1)/nDotP2minusP1;
		VectorScale(P2minusP1, (vec_t)u, dir);
		VectorAdd(from, dir, end);
	} else { /* otherwise do a full trace */
		CM_TestLineDM(from, stop, end);
	}

	VecToPos(end, testPos);
	restingLevel = Grid_Fall(&clMap, testPos, fieldSize);

	/* hack to prevent cursor from getting stuck on the top of an invisible
	 * playerclip surface (in most cases anyway) */
	PosToVec(testPos, pA);
	VectorCopy(pA, pB);
	pA[2] += UNIT_HEIGHT;
	pB[2] -= UNIT_HEIGHT;
	/* FIXME: The next three lines are not endian safe - the min call returns
	 * 0 (restingLevel is correct here after the first Grid_Fall call) and thus
	 * the cursor is invisible on every other level than 1
	 * This bug is most likly related to the see-through-walls bug - so maybe
	 * some of the trace functions are the not endian safe and the result is
	 * seen here */
	/* FIXME: Shouldn't we check the return value of CM_TestLineDM here - maybe
	 * we don't have to do the second Grid_Fall call at all and can safe a lot
	 * of traces */
	CM_TestLineDM(pA, pB, pC);
	VecToPos(pC, testPos);
	restingLevel = min(restingLevel, Grid_Fall(&clMap, testPos, fieldSize));

	/* if grid below intersection level, start a trace from the intersection */
	if (restingLevel < intersectionLevel) {
		VectorCopy(end, from);
		from[2] -= CURSOR_OFFSET;
		CM_TestLineDM(from, stop, end);
		VecToPos(end, testPos);
		restingLevel = Grid_Fall(&clMap, testPos, fieldSize);
	}

	/* test if the selected grid is out of the world */
	if (restingLevel < 0 || restingLevel >= HEIGHT)
		return;

	testPos[2] = restingLevel;
	VectorCopy(testPos, mousePos);

	/* search for an actor on this field */
	mouseActor = NULL;
	for (i = 0, le = LEs; i < numLEs; i++, le++)

		if (le->inuse && !(le->state & STATE_DEAD) && (le->type == ET_ACTOR || le->type == ET_ACTOR2x2))
			switch (le->fieldSize) {
			case ACTOR_SIZE_NORMAL:
				if (VectorCompare(le->pos, mousePos)) {
					mouseActor = le;
				}
				break;
			case ACTOR_SIZE_2x2:
				VectorSet(actor2x2[0], le->pos[0]+1, le->pos[1], le->pos[2]);
				VectorSet(actor2x2[1], le->pos[0], le->pos[1]+1, le->pos[2]);
				VectorSet(actor2x2[2], le->pos[0]+1, le->pos[1]+1, le->pos[2]);
				if (VectorCompare(le->pos, mousePos)
				|| VectorCompare(actor2x2[0], mousePos)
				|| VectorCompare(actor2x2[1], mousePos)
				|| VectorCompare(actor2x2[2], mousePos)) {
					mouseActor = le;
				}
				break;
			default:
				Com_Error(ERR_DROP, "Grid_MoveCalc: unknown actor-size: %i", le->fieldSize);
				break;
		}

	/* calculate move length */
	if (selActor && !VectorCompare(mousePos, mouseLastPos)) {
		VectorCopy(mousePos, mouseLastPos);
		CL_ResetActorMoveLength();
	}

	/* mouse is in the world */
	mouseSpace = MS_WORLD;
}


/*
==============================================================
ACTOR GRAPHICS
==============================================================
*/

/**
 * @brief Adds an actor.
 * @param[in] le The local entity to get the values from
 * @param[in] ent The body entity used in the renderer
 * @sa CL_AddUGV
 * @sa LE_AddToScene
 */
qboolean CL_AddActor (le_t * le, entity_t * ent)
{
	entity_t add;

	if (!(le->state & STATE_DEAD)) {
		/* add weapon */
		if (le->left != NONE) {
			memset(&add, 0, sizeof(entity_t));

			add.lightparam = &le->sunfrac;
			add.model = cls.model_weapons[le->left];

			/* +2 (resp. +3) because the body and the head are already
			 * (and maybe the right weapon will be)
			 * at the previous location */
			add.tagent = V_GetEntity() + 2 + (le->right != NONE);
			add.tagname = "tag_lweapon";

			V_AddEntity(&add);
		}

		/* add weapon */
		if (le->right != NONE) {
			memset(&add, 0, sizeof(entity_t));

			add.lightparam = &le->sunfrac;
			add.alpha = le->alpha;
			add.model = cls.model_weapons[le->right];

			/* +2 because the body and the head are already
			 * at the previous location */
			add.tagent = V_GetEntity() + 2;
			add.tagname = "tag_rweapon";

			V_AddEntity(&add);
		}
	}

	/* add head */
	memset(&add, 0, sizeof(entity_t));

	/* the actor is hearing a sound */
	if (le->hearTime) {
		if (cls.realtime - le->hearTime > 3000) {
			le->hearTime = 0;
		} else {
			add.flags |= RF_HIGHLIGHT;
		}
	}

	add.lightparam = &le->sunfrac;
	add.alpha = le->alpha;
	add.model = le->model2;
	add.skinnum = le->skinnum;

	/* +1 because the body is already at the previous location */
	add.tagent = V_GetEntity() + 1;
	add.tagname = "tag_head";

	V_AddEntity(&add);

	/* add actor special effects */
	if (le->state & STATE_DEAD)
		ent->flags |= RF_BLOOD;
	else
		ent->flags |= RF_SHADOW;

	if (!(le->state & STATE_DEAD)) {
		if (le->selected)
			ent->flags |= RF_SELECTED;
		if (le->team == cls.team) {
			if (le->pnum == cl.pnum)
				ent->flags |= RF_MEMBER;
			if (le->pnum != cl.pnum)
				ent->flags |= RF_ALLIED;
		}
	}

	return qtrue;
}

/**
 * @brief Adds an UGV.
 * @param[in] le
 * @param[in] ent
 * @sa CL_AddActor
 */
qboolean CL_AddUGV (le_t * le, entity_t * ent)
{
	entity_t add;

	if (!(le->state & STATE_DEAD)) {
		/* add weapon */
		if (le->left != NONE) {
			memset(&add, 0, sizeof(entity_t));

			add.lightparam = &le->sunfrac;
			add.model = cls.model_weapons[le->left];

			add.tagent = V_GetEntity() + 2 + (le->right != NONE);
			add.tagname = "tag_lweapon";

			V_AddEntity(&add);
		}

		/* add weapon */
		if (le->right != NONE) {
			memset(&add, 0, sizeof(entity_t));

			add.lightparam = &le->sunfrac;
			add.alpha = le->alpha;
			add.model = cls.model_weapons[le->right];

			add.tagent = V_GetEntity() + 2;
			add.tagname = "tag_rweapon";

			V_AddEntity(&add);
		}
	}

	/* add head */
	memset(&add, 0, sizeof(entity_t));

	add.lightparam = &le->sunfrac;
	add.alpha = le->alpha;
	add.model = le->model2;
	add.skinnum = le->skinnum;

	/* FIXME */
	add.tagent = V_GetEntity() + 1;
	add.tagname = "tag_head";

	V_AddEntity(&add);

	/* add actor special effects */
	ent->flags |= RF_SHADOW;

	if (!(le->state & STATE_DEAD)) {
		if (le->selected)
			ent->flags |= RF_SELECTED;
		if (le->team == cls.team) {
			if (le->pnum == cl.pnum)
				ent->flags |= RF_MEMBER;
			if (le->pnum != cl.pnum)
				ent->flags |= RF_ALLIED;
		}
	}

	return qtrue;
}

/*
==============================================================
TARGETING GRAPHICS
==============================================================
*/

#define LOOKUP_EPSILON 0.0001f

/**
 * @brief table for lookup_erf
 * lookup[]= {erf(0), erf(0.1), ...}
 */
static const float lookup[30]= {
	0.0f,    0.1125f, 0.2227f, 0.3286f, 0.4284f, 0.5205f, 0.6039f,
	0.6778f, 0.7421f, 0.7969f, 0.8427f, 0.8802f, 0.9103f, 0.9340f,
	0.9523f, 0.9661f, 0.9763f, 0.9838f, 0.9891f, 0.9928f, 0.9953f,
	0.9970f, 0.9981f, 0.9989f, 0.9993f, 0.9996f, 0.9998f, 0.9999f,
	0.9999f, 1.0000f
};

/**
 * @brief table for lookup_erf
 * lookup[]= {10*(erf(0.1)-erf(0.0)), 10*(erf(0.2)-erf(0.1)), ...}
 */
static const float lookupdiff[30]= {
	1.1246f, 1.1024f, 1.0592f, 0.9977f, 0.9211f, 0.8336f, 0.7395f,
	0.6430f, 0.5481f, 0.4579f, 0.3750f, 0.3011f, 0.2369f, 0.1828f,
	0.1382f, 0.1024f, 0.0744f, 0.0530f, 0.0370f, 0.0253f, 0.0170f,
	0.0112f, 0.0072f, 0.0045f, 0.0028f, 0.0017f, 0.0010f, 0.0006f,
	0.0003f, 0.0002f
};

/**
 * @brief calculate approximate erf, the error function
 * http://en.wikipedia.org/wiki/Error_function
 * uses lookup table and linear interpolation
 * approximation good to around 0.001.
 * easily good enough for the job.
 * @param[in] the number to calculate the erf of.
 * @return for posotive arg, returns approximate erf. for -ve arg returns 0.0f.
 */
static float lookup_erf (float z)
{
	float ifloat;
	int iint;

	/* erf(-z)=-erf(z), but erf of -ve number should never be used here
	 * so return 0 here */
	if (z < LOOKUP_EPSILON)
		return 0.0f;
	if (z > 2.8f)
		return 1.0f;
	ifloat = floor(z * 10.0f);
	iint = (int)ifloat;
	assert(iint < 30);
	return lookup[iint] + (z - ifloat / 10.0f) * lookupdiff[iint];
}

/**
 * @brief Calculates chance to hit.
 * @param[in] toPos
 */
static float CL_TargetingToHit (pos3_t toPos)
{
	vec3_t shooter, target;
	float distance, pseudosin, width, height, acc, perpX, perpY, hitchance,
		stdevupdown, stdevleftright, crouch, commonfactor;
	int distx, disty, i, n;
	le_t *le;

	if (!selActor || !selFD)
		return 0.0;

	for (i = 0, le = LEs; i < numLEs; i++, le++)
		if (le->inuse && VectorCompare(le->pos, toPos))
			break;

	if (i >= numLEs)
		/* no target there */
		return 0.0;
	/* or suicide attempted */
	if (le == selActor && selFD->damage[0] > 0)
		return 0.0;

	VectorCopy(selActor->origin, shooter);
	VectorCopy(le->origin, target);

	/* Calculate HitZone: */
	distx = fabs(shooter[0] - target[0]);
	disty = fabs(shooter[1] - target[1]);
	distance = sqrt(distx * distx + disty * disty);
	if (distx * distx > disty * disty)
		pseudosin = distance / distx;
	else
		pseudosin = distance / disty;
	width = 2 * PLAYER_WIDTH * pseudosin;
	height = ((le->state & STATE_CROUCHED) ? PLAYER_CROUCH : PLAYER_STAND) - PLAYER_MIN;

	acc = GET_ACC(selChr->skills[ABILITY_ACCURACY],
			selFD->weaponSkill ? selChr->skills[selFD->weaponSkill] : 0);

	crouch = ((selActor->state & STATE_CROUCHED) && selFD->crouch) ? selFD->crouch : 1;

	commonfactor = 0.5f * crouch * torad * distance;
	stdevupdown = (selFD->spread[0] + acc * (1 + selFD->modif)) * commonfactor;
	stdevleftright = (selFD->spread[1] + acc * (1 + selFD->modif)) * commonfactor;

	hitchance = (stdevupdown > LOOKUP_EPSILON ? lookup_erf(height * 0.3536f / stdevupdown) : 1.0f)
			  * (stdevleftright > LOOKUP_EPSILON ? lookup_erf(width * 0.3536f / stdevleftright) : 1.0f);
	/* 0.3536=sqrt(2)/4 */

	/* Calculate cover: */
	n = 0;
	height = height / 18;
	width = width / 18;
	target[2] -= UNIT_HEIGHT / 2;
	target[2] += height * 9;
	perpX = disty / distance * width;
	perpY = 0 - distx / distance * width;

	target[0] += perpX;
	perpX *= 2;
	target[1] += perpY;
	perpY *= 2;
	target[2] += 6 * height;
/*	CL_ParticleSpawn("inRangeTracer", 0, shooter, target, NULL); */
	if (!CM_TestLine(shooter, target))
		n++;
	target[0] += perpX;
	target[1] += perpY;
	target[2] -= 6 * height;
/*	CL_ParticleSpawn("inRangeTracer", 0, shooter, target, NULL); */
	if (!CM_TestLine(shooter, target))
		n++;
	target[0] += perpX;
	target[1] += perpY;
	target[2] += 4 * height;
/*	CL_ParticleSpawn("inRangeTracer", 0, shooter, target, NULL); */
	if (!CM_TestLine(shooter, target))
		n++;
	target[2] += 4 * height;
/*	CL_ParticleSpawn("inRangeTracer", 0, shooter, target, NULL); */
	if (!CM_TestLine(shooter, target))
		n++;
	target[0] -= perpX * 3;
	target[1] -= perpY * 3;
	target[2] -= 12 * height;
/*	CL_ParticleSpawn("inRangeTracer", 0, shooter, target, NULL); */
	if (!CM_TestLine(shooter, target))
		n++;
	target[0] -= perpX;
	target[1] -= perpY;
	target[2] += 6 * height;
/*	CL_ParticleSpawn("inRangeTracer", 0, shooter, target, NULL); */
	if (!CM_TestLine(shooter, target))
		n++;
	target[0] -= perpX;
	target[1] -= perpY;
	target[2] -= 4 * height;
/*	CL_ParticleSpawn("inRangeTracer", 0, shooter, target, NULL); */
	if (!CM_TestLine(shooter, target))
		n++;
	target[0] -= perpX;
	target[1] -= perpY;
	target[2] += 10 * height;
/*	CL_ParticleSpawn("inRangeTracer", 0, shooter, target, NULL); */
	if (!CM_TestLine(shooter, target))
		n++;

	return (hitchance * (0.125) * n);
}

/**
 * @brief Show weapon radius
 * @param[in] center The center of the circle
 */
static void CL_Targeting_Radius (vec3_t center)
{
	const vec4_t color = {0, 1, 0, 0.3};
	ptl_t *particle = NULL;

	assert(selFD);

	particle = CL_ParticleSpawn("*circle", 0, center, NULL, NULL);
	particle->size[0] = selFD->splrad; /* misuse size vector as radius */
	particle->size[1] = 1; /* thickness */
	particle->style = STYLE_CIRCLE;
	particle->blend = BLEND_BLEND;
	/* free the particle every frame */
	particle->life = 0.0001;
	Vector4Copy(color, particle->color);
}


/**
 * @brief Draws line to target.
 * @param[in] fromPos The (grid-) position of the aiming actor.
 * @param[in] toPos The (grid-) position of the target.
 * @sa CL_TargetingGrenade
 * @sa CL_AddTargeting
 * @sa CL_Trace
 */
static void CL_TargetingStraight (pos3_t fromPos, pos3_t toPos)
{
	vec3_t start, end;
	vec3_t dir, mid;
	trace_t tr;
	int oldLevel, i;
	float d;
	qboolean crossNo;
	le_t *le;
	le_t *target = NULL;

	if (!selActor || !selFD)
		return;

	Grid_PosToVec(&clMap, fromPos, start);
	Grid_PosToVec(&clMap, toPos, end);
	if (mousePosTargettingAlign)
		end[2] -= mousePosTargettingAlign;

	/* calculate direction */
	VectorSubtract(end, start, dir);
	VectorNormalize(dir);

	/* calculate 'out of range point' if there is one */
	if (VectorDistSqr(start, end) > selFD->range * selFD->range) {
		VectorMA(start, selFD->range, dir, mid);
		crossNo = qtrue;
	} else {
		VectorCopy(end, mid);
		crossNo = qfalse;
	}

	/* switch up to top level, this is a bit of a hack to make sure our trace doesn't go through ceilings ... */
	oldLevel = cl_worldlevel->integer;
	cl_worldlevel->integer = map_maxlevel-1;

	/* search for an actor at target */
	for (i = 0, le = LEs; i < numLEs; i++, le++)
		if (le->inuse && !(le->state & STATE_DEAD) && (le->type == ET_ACTOR || le->type == ET_ACTOR2x2) && VectorCompare(le->pos, toPos)) {
			target = le;
			break;
		}

	tr = CL_Trace(start, mid, vec3_origin, vec3_origin, selActor, target, MASK_SHOT);

	if (tr.fraction < 1.0) {
		d = VectorDist(start, mid);
		VectorMA(start, tr.fraction * d, dir, mid);
		crossNo = qtrue;
	}

	/* switch level back to where it was again */
	cl_worldlevel->integer = oldLevel;

	/* spawn particles */
	CL_ParticleSpawn("inRangeTracer", 0, start, mid, NULL);
	if (crossNo) {
		CL_ParticleSpawn("longRangeTracer", 0, mid, end, NULL);
		CL_ParticleSpawn("cross_no", 0, end, NULL, NULL);
	} else {
		CL_ParticleSpawn("cross", 0, end, NULL, NULL);
	}

	selToHit = 100 * CL_TargetingToHit(toPos);
}


#define GRENADE_PARTITIONS	20

/**
 * @brief Shows targetting for a grenade.
 * @param[in] fromPos The (grid-) position of the aiming actor.
 * @param[in] toPos The (grid-) position of the target (mousePos or mousePendPos).
 * @todo Find out why the ceiling is not checked against the parabola when calculating obstacles.
 * i.e. the line is drawn green even if a ceiling prevents the shot.
 * https://sourceforge.net/tracker/index.php?func=detail&aid=1701263&group_id=157793&atid=805242
 * @sa CL_TargetingStraight
 */
static void CL_TargetingGrenade (pos3_t fromPos, pos3_t toPos)
{
	vec3_t from, at, cross;
	float vz, dt;
	vec3_t v0, ds, next;
	trace_t tr;
	int oldLevel;
	qboolean obstructed = qfalse;
	int i;
	le_t *le;
	le_t *target = NULL;

	if (!selActor || (fromPos[0] == toPos[0] && fromPos[1] == toPos[1]))
		return;

	/* get vectors, paint cross */
	Grid_PosToVec(&clMap, fromPos, from);
	Grid_PosToVec(&clMap, toPos, at);
	from[2] += selFD->shotOrg[1];

	/* prefer to aim grenades at the ground */
	at[2] -= GROUND_DELTA;
	if (mousePosTargettingAlign)
		at[2] -= mousePosTargettingAlign;
	VectorCopy(at, cross);

	/* search for an actor at target */
	for (i = 0, le = LEs; i < numLEs; i++, le++)
		if (le->inuse && !(le->state & STATE_DEAD) && (le->type == ET_ACTOR || le->type == ET_ACTOR2x2) && VectorCompare(le->pos, toPos)) {
			target = le;
			break;
		}

	/* calculate parabola */
	dt = Com_GrenadeTarget(from, at, selFD->range, selFD->launched, selFD->rolled, v0);
	if (!dt) {
		CL_ParticleSpawn("cross_no", 0, cross, NULL, NULL);
		return;
	}

	dt /= GRENADE_PARTITIONS;
	VectorSubtract(at, from, ds);
	VectorScale(ds, 1.0 / GRENADE_PARTITIONS, ds);
	ds[2] = 0;

	/* switch up to top level, this is a bit of a hack to make sure our trace doesn't go through ceilings ... */
	oldLevel = cl_worldlevel->integer;
	cl_worldlevel->integer = map_maxlevel-1;

	/* paint */
	vz = v0[2];

	for (i = 0; i < GRENADE_PARTITIONS; i++) {
		VectorAdd(from, ds, next);
		next[2] += dt * (vz - 0.5 * GRAVITY * dt);
		vz -= GRAVITY * dt;
		VectorScale(v0, (i + 1.0) / GRENADE_PARTITIONS, at);

		/* trace for obstacles */
		tr = CL_Trace(from, next, vec3_origin, vec3_origin, selActor, target, MASK_SHOT);

		/* something was hit */
		if (tr.fraction < 1.0) {
			obstructed = qtrue;
		}

		/* draw particles */
		/* @todo: character strength should be used here, too
		 * the stronger the character, the further the throw */
		if (obstructed || VectorLength(at) > selFD->range)
			CL_ParticleSpawn("longRangeTracer", 0, from, next, NULL);
		else
			CL_ParticleSpawn("inRangeTracer", 0, from, next, NULL);
		VectorCopy(next, from);
	}
	/* draw targetting cross */
	if (obstructed || VectorLength(at) > selFD->range)
		CL_ParticleSpawn("cross_no", 0, cross, NULL, NULL);
	else
		CL_ParticleSpawn("cross", 0, cross, NULL, NULL);

	if (selFD->splrad) {
		Grid_PosToVec(&clMap, toPos, at);
		CL_Targeting_Radius(at);
	}

	selToHit = 100 * CL_TargetingToHit(toPos);

	/* switch level back to where it was again */
	cl_worldlevel->integer = oldLevel;
}


/**
 * @brief field marker box
 * @sa ModelOffset in cl_le.c
 */
static const vec3_t boxSize = { BOX_DELTA_WIDTH, BOX_DELTA_LENGTH, BOX_DELTA_HEIGHT };
#define BoxSize(i,source,target) (target[0]=i*source[0]+((i-1)*UNIT_SIZE),target[1]=i*source[1]+((i-1)*UNIT_SIZE),target[2]=source[2])
#define BoxOffset(i, target) (target[0]=(i-1)*(UNIT_SIZE+BOX_DELTA_WIDTH), target[1]=(i-1)*(UNIT_SIZE+BOX_DELTA_LENGTH), target[2]=0)

/**
 * @brief create a targeting box at the given position
 * @sa CL_ParseClientinfo
 */
static void CL_AddTargetingBox (pos3_t pos, qboolean pendBox)
{
	entity_t ent;
	vec3_t realBoxSize;
	vec3_t cursorOffset;

	memset(&ent, 0, sizeof(entity_t));
	ent.flags = RF_BOX;

	Grid_PosToVec(&clMap, pos, ent.origin);

	/* ok, paint the green box if move is possible */
	if (selActor && actorMoveLength < ROUTING_NOT_REACHABLE && actorMoveLength <= selActor->TU)
		VectorSet(ent.angles, 0, 1, 0);
	/* and paint a dark blue one if move is impossible or the soldier */
	/* does not have enough TimeUnits left */
	else
		VectorSet(ent.angles, 0, 0, 0.6);

	VectorAdd(ent.origin, boxSize, ent.oldorigin);

	/* color */
	if (mouseActor && (mouseActor != selActor)) {
		ent.alpha = 0.4 + 0.2 * sin((float) cl.time / 80);
		/* paint the box red if the soldiers under the cursor is
		 * not in our team and no civilian, too */
		if (mouseActor->team != cls.team) {
			switch (mouseActor->team) {
			case TEAM_CIVILIAN:
				/* civilians are yellow */
				VectorSet(ent.angles, 1, 1, 0);
				break;
			default:
				if (mouseActor->team == TEAM_ALIEN) {
					if (mouseActor->teamDef) {
						if (RS_IsResearched_idx(RS_GetTechIdxByName(mouseActor->teamDef->tech)))
							menuText[TEXT_MOUSECURSOR_PLAYERNAMES] = _(mouseActor->teamDef->name);
						else
							menuText[TEXT_MOUSECURSOR_PLAYERNAMES] = _("Unknown alien race");
					}
				} else {
					/* multiplayer names */
					/* see CL_ParseClientinfo */
					menuText[TEXT_MOUSECURSOR_PLAYERNAMES] = cl.configstrings[CS_PLAYERNAMES + mouseActor->pnum];
				}
				/* aliens (and players not in our team [multiplayer]) are red */
				VectorSet(ent.angles, 1, 0, 0);
				break;
			}
		} else {
			/* coop multiplayer games */
			if (mouseActor->pnum != cl.pnum) {
				menuText[TEXT_MOUSECURSOR_PLAYERNAMES] = cl.configstrings[CS_PLAYERNAMES + mouseActor->pnum];
			} else {
				/* we know the names of our own actors */
				character_t* chr = CL_GetActorChr(mouseActor);
				assert(chr);
				menuText[TEXT_MOUSECURSOR_PLAYERNAMES] = chr->name;
			}
			/* paint a light blue box if on our team */
			VectorSet(ent.angles, 0.2, 0.3, 1);
		}

		if (selActor) {
			BoxOffset(selActor->fieldSize, cursorOffset);
			VectorAdd(ent.oldorigin, cursorOffset, ent.oldorigin);
			VectorAdd(ent.origin, cursorOffset, ent.origin);
			BoxSize(selActor->fieldSize, boxSize, realBoxSize);
			VectorSubtract(ent.origin, realBoxSize, ent.origin);
		}
	} else {
		if (selActor) {
			BoxOffset(selActor->fieldSize, cursorOffset);
			VectorAdd(ent.oldorigin, cursorOffset, ent.oldorigin);
			VectorAdd(ent.origin, cursorOffset, ent.origin);

			BoxSize(selActor->fieldSize, boxSize, realBoxSize);
			VectorSubtract(ent.origin, realBoxSize, ent.origin);
		} else {
			VectorSubtract(ent.origin, boxSize, ent.origin);
		}
		ent.alpha = 0.3;
	}
#if 0
	{
		vec3_t color = {1.0, 0, 0};
		V_AddLight(ent.origin, 256, color);
	}
#endif
	/* if pendBox is true then ignore all the previous color considerations and use cyan */
	if (pendBox) {
		VectorSet(ent.angles, 0, 1, 1);
		ent.alpha = 0.15;
	}

	/* add it */
	V_AddEntity(&ent);
}

/**
 * @brief Key binding for fast inventory access
 */
void CL_ActorInventoryOpen_f (void)
{
	if (CL_OnBattlescape()) {
		menu_t* menu = MN_ActiveMenu();
		if (!strstr(menu->name, "hudinv")) {
			if (!Q_strcmp(mn_hud->string, "althud"))
				MN_PushMenu("ahudinv");
			else
				MN_PushMenu("hudinv");
		} else
			MN_PopMenu(qfalse);
	}
}

/**
 * @brief Targets to the ground when holding the assigned button
 * @sa mousePosTargettingAlign
 */
void CL_ActorTargetAlign_f (void)
{
	int align = GROUND_DELTA;
	static int currentPos = 0;

	/* no firedef selected */
	if (!selFD || !selActor)
		return;
	if (cl.cmode != M_FIRE_R && cl.cmode != M_FIRE_L
	 && cl.cmode != M_PEND_FIRE_R && cl.cmode != M_PEND_FIRE_L)
		return;

	/* user defined height align */
	if (Cmd_Argc() == 2) {
		align = atoi(Cmd_Argv(1));
	} else {
		switch (currentPos) {
		case 0:
			if (selFD->gravity)
				align = -align;
			currentPos = 1; /* next level */
			break;
		case 1:
			/* only allow to align to lower z level if the actor is
			 * standing at a higher z-level */
			if (selFD->gravity)
				align = -(2 * align);
			else
				align = -align;
			currentPos = 2;
			break;
		case 2:
			/* the static var is not reseted on weaponswitch or actorswitch */
			if (selFD->gravity) {
				align = 0;
				currentPos = 0; /* next level */
			} else {
				align = -(2 * align);
				currentPos = 3; /* next level */
			}
			break;
		case 3:
			align = 0;
			currentPos = 0; /* back to start */
			break;
		}
	}
	mousePosTargettingAlign = align;
}

/**
 * @brief Adds a target cursor.
 * @sa CL_TargetingStraight
 * @sa CL_TargetingGrenade
 * @sa CL_AddTargetingBox
 * @sa CL_TraceMove
 * Draws the tracer (red, yellow, green box) on the grid
 */
void CL_AddTargeting (void)
{
	if (mouseSpace != MS_WORLD)
		return;

	switch (cl.cmode) {
	case M_MOVE:
	case M_PEND_MOVE:
		/* Display Move-cursor. */
		CL_AddTargetingBox(mousePos, qfalse);

		if (cl.cmode == M_PEND_MOVE) {
			/* Also display a box for the pending move if we have one. */
			CL_AddTargetingBox(mousePendPos, qtrue);
			if (!CL_TraceMove(mousePendPos))
				cl.cmode = M_MOVE;
		}
		break;
	case M_FIRE_R:
	case M_FIRE_L:
		if (!selActor || !selFD)
			return;

		if (!selFD->gravity)
			CL_TargetingStraight(selActor->pos, mousePos);
		else
			CL_TargetingGrenade(selActor->pos, mousePos);
		break;
	case M_PEND_FIRE_R:
	case M_PEND_FIRE_L:
		if (!selActor || !selFD)
			return;

		/* Draw cursor at mousepointer */
		CL_AddTargetingBox(mousePos, qfalse);

		/* Draw (pending) Cursor at target */
		CL_AddTargetingBox(mousePendPos, qtrue);

		if (!selFD->gravity)
			CL_TargetingStraight(selActor->pos, mousePendPos);
		else
			CL_TargetingGrenade(selActor->pos, mousePendPos);
		break;
	default:
		break;
	}
}

/**
 * @brief Plays various sounds on actor action.
 * @param[in] category
 * @param[in] gender
 * @param[in] soundType Type of action (among actorSound_t) for which we need a sound.
 */
void CL_PlayActorSound (le_t* le, actorSound_t soundType)
{
	sfx_t* sfx;
	const char *actorSound = Com_GetActorSound(le->teamDef, le->gender, soundType);
	if (actorSound) {
		sfx = S_RegisterSound(actorSound);
		if (sfx) {
			Com_DPrintf(DEBUG_SOUND|DEBUG_CLIENT, "CL_PlayActorSound: ActorSound: '%s'\n", actorSound);
			S_StartSound(le->origin, sfx, DEFAULT_SOUND_PACKET_VOLUME, DEFAULT_SOUND_PACKET_ATTENUATION);
		}
	}
}
