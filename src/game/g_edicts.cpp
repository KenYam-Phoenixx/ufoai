/**
 * @file
 * @brief functions to handle the storage of all edicts in the game module.
 */

/*
All original material Copyright (C) 2002-2013 UFO: Alien Invasion.

Original file from Quake 2 v3.21: quake2-2.31/game/g_utils.c
Copyright (C) 1997-2001 Id Software, Inc.

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

#include "g_edicts.h"
#include "g_actor.h"

/** This is where we store the edicts */
static Edict* g_edicts;

/**
 * @brief Allocate space for the entity pointers.
 * @note No need to set it to zero, G_TagMalloc will do that for us
 */
Edict* G_EdictsConstruct (void)
{
	g_edicts = (Edict*)G_TagMalloc(game.sv_maxentities * sizeof(g_edicts[0]), TAG_GAME);
	return g_edicts;
}

/**
 * @brief Reset the entity pointers for eg. a new game.
 */
void G_EdictsInit (void)
{
	for (int i = 0; i < game.sv_maxentities; i++)
		g_edicts[i].init();
}

/**
 * @brief Get an entity's ID number
 * @param ent The entity
 * @return the entity's ID number, currently the index in the array
 * @note DO NOT use this number as anything other than an identifier !
 */
int G_EdictsGetNumber (const Edict* ent)
{
	int idx = ent - g_edicts;
	assert(idx >= 0 && idx < globals.num_edicts);
	return idx;
}

/**
 * @brief Check if the given number could point to an existing entity
 * @note entity must also be 'in use' to be really valid
 * @param[in] num The entity's index in the array of entities
 */
bool G_EdictsIsValidNum (const int num)
{
	if (num >= 0 && num < globals.num_edicts)
		return true;
	return false;
}

/**
 * @brief Get an entity by it's number
 * @param[in] num The entity's index in the array of entities
 */
Edict* G_EdictsGetByNum (const int num)
{
	if (!G_EdictsIsValidNum(num)) {
		gi.DPrintf("Invalid edict num %i\n", num);
		return nullptr;
	}

	return g_edicts + num;
}

/**
 * @brief Returns the first entity
 * @note This is always a world edict - but in case of rma there might be several parts
 * of the world spread all over the array.
 */
Edict* G_EdictsGetFirst (void)
{
	return &g_edicts[0];
}

/**
 * @brief Iterate through the list of entities
 * @param lastEnt The entity found in the previous iteration; if nullptr, we start at the beginning
 */
Edict* G_EdictsGetNext (Edict* lastEnt)
{
	Edict* endOfEnts = &g_edicts[globals.num_edicts];
	Edict* ent;

	if (!globals.num_edicts)
		return nullptr;

	if (!lastEnt)
		return g_edicts;
	assert(lastEnt >= g_edicts);
	assert(lastEnt < endOfEnts);

	ent = lastEnt;

	ent++;
	if (ent >= endOfEnts)
		return nullptr;

	return ent;
}

Edict* G_EdictDuplicate (const Edict* edict)
{
	Edict* duplicate = G_EdictsGetNewEdict();
	if (duplicate == nullptr)
		return nullptr;
	memcpy(duplicate, edict, sizeof(*edict));
	duplicate->number = G_EdictsGetNumber(duplicate);
	return duplicate;
}

/**
 * @brief Find an entity that is not in use
 */
Edict* G_EdictsGetNewEdict (void)
{
	Edict* ent = nullptr;

	/* try to recycle an edict */
	while ((ent = G_EdictsGetNext(ent))) {
		if (!ent->inuse)
			return ent;
	}

	/* no unused edict found, create a new one */
	ent = &g_edicts[globals.num_edicts];
	globals.num_edicts++;
	if (globals.num_edicts > game.sv_maxentities)
		return nullptr;

	return ent;
}

/**
 * @brief Iterate through the entities that are in use
 * @note we can hopefully get rid of this function once we know when it makes sense
 * to iterate through entities that are NOT in use
 * @param lastEnt The entity found in the previous iteration; if nullptr, we start at the beginning
 */
Edict* G_EdictsGetNextInUse (Edict* lastEnt)
{
	Edict* ent = lastEnt;

	while ((ent = G_EdictsGetNext(ent))) {
		if (ent->inuse)
			break;
	}
	return ent;
}

/**
 * @brief Iterator through all the trigger_nextmap edicts
 * @param lastEnt The entity found in the previous iteration; if nullptr, we start at the beginning
 */
Edict* G_EdictsGetTriggerNextMaps (Edict* lastEnt)
{
	Edict* ent = lastEnt;

	while ((ent = G_EdictsGetNextInUse(ent))) {
		if (G_IsTriggerNextMap(ent))
			break;
	}
	return ent;
}

/**
 * @brief Iterate through the living actor entities
 * @param lastEnt The entity found in the previous iteration; if nullptr, we start at the beginning
 */
Edict* G_EdictsGetNextLivingActor (Edict* lastEnt)
{
	Edict* ent = lastEnt;

	while ((ent = G_EdictsGetNextInUse(ent))) {
		if (G_IsLivingActor(ent))
			break;
	}
	return ent;
}

/**
 * @brief Iterate through the living actor entities of the given team
 * @param lastEnt The entity found in the previous iteration; if nullptr, we start at the beginning
 * @param team The team we are looking for
 */
Edict* G_EdictsGetNextLivingActorOfTeam (Edict* lastEnt, const int team)
{
	Edict* ent = lastEnt;

	while ((ent = G_EdictsGetNextLivingActor(ent))) {
		if (ent->team == team)
			break;
	}
	return ent;
}

/**
 * @brief Iterate through the actor entities (even the dead! - but skips the invisible)
 * @param lastEnt The entity found in the previous iteration; if nullptr, we start at the beginning
 */
Edict* G_EdictsGetNextActor (Edict* lastEnt)
{
	Edict* ent = lastEnt;

	assert(lastEnt < &g_edicts[globals.num_edicts]);

	while ((ent = G_EdictsGetNextInUse(ent))) {
		if (G_IsActor(ent))
			break;
	}
	return ent;
}

/**
 * @brief Searches an actor by a unique character number
 * @param[in] ucn The unique character number
 * @param[in] team The team to get the actor with the ucn from
 * @return The actor edict if found, otherwise @c nullptr
 */
Edict* G_EdictsGetActorByUCN (const int ucn, const int team)
{
	Edict* ent = nullptr;

	while ((ent = G_EdictsGetNextActor(ent)))
		if (team == ent->team && ent->chr.ucn == ucn)
			return ent;

	return nullptr;
}

/**
 * @brief Searches an actor at the given grid location.
 * @param pos The grid location to look for an edict.
 * @return @c nullptr if nothing was found, otherwise the actor located at the given grid position.
 */
Edict* G_EdictsGetLivingActorFromPos (const pos3_t pos)
{
	Edict* ent = nullptr;

	while ((ent = G_EdictsGetNextLivingActor(ent))) {
		if (!VectorCompare(pos, ent->pos))
			continue;

		return ent;
	}
	/* nothing found at this pos */
	return nullptr;
}

/**
 * @brief Searches the edict that has the given target as @c targetname set
 * @param target The target name of the edict that you are searching
 * @return @c nullptr if no edict with the given target name was found, otherwise
 * the edict that has the targetname set you were looking for.
 */
Edict* G_EdictsFindTargetEntity (const char* target)
{
	Edict* ent = nullptr;

	while ((ent = G_EdictsGetNextInUse(ent))) {
		const char* n = ent->targetname;
		if (n && Q_streq(n, target))
			return ent;
	}

	return nullptr;
}

/**
 * @brief Calculate the edict's origin vector from it's grid position
 * @param ent The entity
 */
void G_EdictCalcOrigin (Edict* ent)
{
	gi.GridPosToVec(ent->fieldSize, ent->pos, ent->origin);
}

/**
 * @brief Set the edict's pos and origin vector to the given grid position
 * @param ent The entity
 * @param newPos The new grid position
 */
void G_EdictSetOrigin (Edict* ent, const pos3_t newPos)
{
	VectorCopy(newPos, ent->pos);
	gi.GridPosToVec(ent->fieldSize, ent->pos, ent->origin);
}

/**
 * @brief Check whether the edict is on the given position
 * @param ent The entity
 * @param cmpPos The grid position to compare to
 * @return true if positions are equal
 */
bool G_EdictPosIsSameAs (const Edict* ent, const pos3_t cmpPos)
{
	return VectorCompare(cmpPos, ent->pos);
}

/**
 * @brief Called every frame to let the edicts tick
 */
void G_EdictsThink (void)
{
	/* treat each object in turn */
	/* even the world gets a chance to think */
	Edict* ent = nullptr;
	while ((ent = G_EdictsGetNextInUse(ent))) {
		if (!ent->think)
			continue;
		if (ent->nextthink <= 0)
			continue;
		if (ent->nextthink > level.time + 0.001f)
			continue;

		ent->nextthink = level.time + SERVER_FRAME_SECONDS;
		ent->think(ent);
	}
}
