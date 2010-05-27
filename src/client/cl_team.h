/**
 * @file cl_team.h
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

#ifndef CLIENT_CL_TEAM_H
#define CLIENT_CL_TEAM_H

#include "mxml/mxml_ufoai.h"
#include "../common/msg.h"

#define MAX_WHOLETEAM	32

#define MAX_TEAMDATASIZE	32768

#define NUM_TEAMSKINS	6
#define NUM_TEAMSKINS_SINGLEPLAYER 4

void CL_GenerateCharacter(character_t *chr, const char *teamDefName);
void CL_CharacterSkillAndScoreCvars(const character_t *chr, const char* cvarPrefix);

qboolean CL_SaveCharacterXML(mxml_node_t *p, const character_t *chr);
qboolean CL_LoadCharacterXML(mxml_node_t *p, character_t *chr);

void CL_SaveInventoryXML(mxml_node_t *p, const inventory_t * i);
void CL_LoadInventoryXML(mxml_node_t *p, inventory_t * i);
void TEAM_InitStartup(void);

extern chrList_t chrDisplayList;

#endif
