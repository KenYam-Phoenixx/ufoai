/**
 * @file cl_aliencont.h
 * @brief Header file for Alien Containment stuff.
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

#ifndef CLIENT_CL_ALIENCONT_H
#define CLIENT_CL_ALIENCONT_H

#define MAX_CARGO		32
#define MAX_ALIENCONT_CAP	32

/** specializations of aliens */
/*@todo: this is not used anywhere yet */
typedef enum {
	AS_PILOT,
	AS_GUNNER,
	AS_HARVESTER,
	AS_SOLDIER
} alienSpec_t;

/** cases of alien amount calculation */
typedef enum {
	AL_RESEARCH,			/**< When we remove alien(s) because of research topic. */
	AL_KILL,			/**< When we kill all aliens in base because of base defence mission. */
	AL_KILLONE,			/**< When we kill one alien of given type in AC menu. */
	AL_ADDALIVE,			/**< When we add one alien by hand. */
	AL_ADDDEAD			/**< When we kill one alien by hand. */
} alienCalcType_t;

/** structure of Alien Containment being a member of base_t */
typedef struct aliensCont_s {
	int idx;			/**< Index of alien race in global csi.teamDef array. */
	char alientype[MAX_VAR];	/**< type of alien */
	int amount_alive;		/**< Amount of live captured aliens. */
	int amount_dead;		/**< Amount of alien corpses. */
	int techIdx;			/**< Idx of related tech. */
} aliensCont_t;

/** alien cargo in aircraft_t, carrying aliens and bodies from a mission to base */
typedef struct aliensTmp_s {
	char alientype[MAX_VAR];	/**< type of alien */
	int amount_alive;		/**< Amount of live captured aliens. */
	int amount_dead;		/**< Amount of alien corpses. */
} aliensTmp_t;

/**
 * Collecting aliens functions.
 */

void AL_FillInContainment(struct base_s *base);
const char *AL_AlienTypeToName(int teamDescIdx);
void AL_CollectingAliens(struct aircraft_s *aircraft);
void AL_AddAliens(struct aircraft_s *aircraft);
void AL_RemoveAliens(struct base_s *base, const char *name, int amount, alienCalcType_t action);
int AL_GetAlienIdx(const char *id);
int AL_GetAlienGlobalIdx(int idx);
int AL_GetAlienAmount(int idx, requirementType_t reqtype, struct base_s *base);

/**
 * Menu functions
 */
int AL_CountAll(void);
int AL_CountInBase(void);

void AC_Reset(void);

void AC_KillAll(struct base_s *base);

extern qboolean AC_ContainmentAllowed(void);

#endif /* CLIENT_CL_ALIENCONT_H */
