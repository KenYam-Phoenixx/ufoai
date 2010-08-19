/**
 * @file cl_game.h
 * @brief Shared game type headers
 */

/*
Copyright (C) 2002-2010 UFO: Alien Invasion.

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

#ifndef CL_GAME_H
#define CL_GAME_H

#include "client.h"
#include "ui/ui_nodes.h"

#define GAME_NONE			0
#define GAME_SINGLEPLAYER	(1 << 0)
#define GAME_MULTIPLAYER	(1 << 1)
#define GAME_CAMPAIGN		(GAME_SINGLEPLAYER | (1 << 2))
#define GAME_SKIRMISH		(GAME_SINGLEPLAYER | (1 << 3))

#define GAME_MAX			GAME_SKIRMISH

#define GAME_IsSingleplayer()	(cls.gametype & GAME_SINGLEPLAYER)
#define GAME_IsMultiplayer()	(cls.gametype == GAME_MULTIPLAYER)

void GAME_InitStartup(void);
void GAME_SetMode(int gametype);
void GAME_ReloadMode(void);
void GAME_Init(qboolean load);
void GAME_DisplayItemInfo(uiNode_t *node, const char *string);
qboolean GAME_ItemIsUseable(const objDef_t *od);
void GAME_HandleResults(struct dbuffer *msg, int winner, int *numSpawned, int *numAlive, int numKilled[][MAX_TEAMS], int numStunned[][MAX_TEAMS]);
void GAME_SpawnSoldiers(void);
int GAME_GetCurrentTeam(void);
equipDef_t *GAME_GetEquipmentDefinition(void);
void GAME_CharacterCvars(const character_t *chr);
character_t* GAME_GetCharacter(int index);
size_t GAME_GetCharacterArraySize(void);
void GAME_ResetCharacters(void);
void GAME_GenerateTeam(const char *teamDefID, const equipDef_t *ed, int teamMembers);
void GAME_AppendTeamMember(int memberIndex, const char *teamDefID, const equipDef_t *ed);
void GAME_StartBattlescape(qboolean isTeamPlay);
void GAME_EndBattlescape(void);
void GAME_EndRoundAnnounce(int playerNum, int team);
qboolean GAME_TeamIsKnown(const teamDef_t *teamDef);
const char* GAME_GetTeamDef(void);
void GAME_Drop(void);
void GAME_Frame(void);
const char* GAME_GetModelForItem(const objDef_t *od, struct uiModel_s** menuModel);

#include "cl_game_campaign.h"
#include "cl_game_skirmish.h"
#include "cl_game_multiplayer.h"

#endif
