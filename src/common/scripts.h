/**
 * @file scripts.h
 * @brief Header for script parsing functions
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

/**
 * @brief conditions for V_IF
 */
typedef enum menuIfCondition_s {
	/** float compares */
	IF_EQ = 0, /**< == */
	IF_LE, /**< <= */
	IF_GE, /**< >= */
	IF_GT, /**< > */
	IF_LT, /**< < */
	IF_NE = 5, /**< != */
	IF_EXISTS, /**< only cvar given - check for existence */

	/** string compares */
	IF_STR_EQ,	/**< eq */
	IF_STR_NE,	/**< ne */

	IF_SIZE
} menuIfCondition_t;

/** @sa menuIfCondition_t */
typedef struct menuDepends_s {
	char *var;
	char *value;
	cvar_t *cvar;
	int cond;
} menuDepends_t;

/* end of V_IF */

#ifndef ALIGNBYTES
#define ALIGNBYTES 1
#endif
/* darwin defines this in /usr/include/ppc/param.h */
#ifndef ALIGN
#define ALIGN(size)  size
/*((size) + ((ALIGN_BYTES - ((size) % ALIGN_BYTES)) % ALIGN_BYTES))*/
#endif

#define MEMBER_SIZEOF(TYPE, MEMBER) sizeof(((TYPE *)0)->MEMBER)

/**
 * @brief possible values for parsing functions
 * @sa vt_names
 * @sa vt_sizes
 */
typedef enum {
	V_NULL,
	V_BOOL,
	V_CHAR,
	V_INT,
	V_INT2,
	V_FLOAT = 5,
	V_POS,
	V_VECTOR,
	V_COLOR,
	V_RGBA,
	V_STRING = 10,
	V_TRANSLATION_STRING,	/**< translate via gettext and store already translated in target buffer */
	V_TRANSLATION_MANUAL_STRING,	/**< remove _ but don't translate */
	V_LONGSTRING,
	V_ALIGN,
	V_BLEND = 15,
	V_STYLE,
	V_FADE,
	V_SHAPE_SMALL,				/**< space a weapon allocates in the inventory shapes, w, h */
	V_SHAPE_BIG,				/**< inventory shape, x, y, w, h */
	V_DMGTYPE = 20,
	V_DMGWEIGHT,
	V_DATE,
	V_IF,
	V_RELABS,					/**< relative (e.g. 1.50) and absolute (e.g. +15) values */
	V_CLIENT_HUNK = 25,			/**< only for client side data - not handled in Com_ParseValue */
	V_CLIENT_HUNK_STRING,		/**< same as for V_CLIENT_HUNK */

	V_NUM_TYPES
} valueTypes_t;

extern const char *vt_names[V_NUM_TYPES];

/** possible alien values - see also align_names */
typedef enum {
	ALIGN_UL,
	ALIGN_UC,
	ALIGN_UR,
	ALIGN_CL,
	ALIGN_CC,
	ALIGN_CR,
	ALIGN_LL,
	ALIGN_LC,
	ALIGN_LR,

	ALIGN_LAST
} align_t;

/** possible blend modes - see also blend_names */
typedef enum {
	BLEND_REPLACE,
	BLEND_BLEND,
	BLEND_ADD,
	BLEND_FILTER,
	BLEND_INVFILTER,

	BLEND_LAST
} blend_t;

typedef enum {
	STYLE_FACING,
	STYLE_ROTATED,
	STYLE_BEAM,
	STYLE_LINE,
	STYLE_AXIS,
	STYLE_CIRCLE,

	STYLE_LAST
} style_t;

typedef enum {
	FADE_NONE,
	FADE_IN,
	FADE_OUT,
	FADE_SIN,
	FADE_SAW,
	FADE_BLEND,

	FADE_LAST
} fade_t;

extern const char *align_names[ALIGN_LAST];
extern const char *blend_names[BLEND_LAST];
extern const char *style_names[STYLE_LAST];
extern const char *fade_names[FADE_LAST];
extern const char *air_slot_type_strings[MAX_ACITEMS];

/** used e.g. in our parsers */
typedef struct value_s {
	const char *string;
	const int type;
	const size_t ofs;
	const size_t size;
} value_t;

#ifdef DEBUG
int Com_ParseValueDebug(void *base, const char *token, valueTypes_t type, int ofs, size_t size, const char* file, int line);
int Com_SetValueDebug(void *base, const void *set, valueTypes_t type, int ofs, size_t size, const char* file, int line);
#else
int Com_ParseValue(void *base, const char *token, valueTypes_t type, int ofs, size_t size);
int Com_SetValue(void *base, const void *set, valueTypes_t type, int ofs, size_t size);
#endif
const char *Com_ValueToStr(void *base, valueTypes_t type, int ofs);

/*==============================================================
SCRIPT PARSING
==============================================================*/

#ifndef SCRIPTS_H
#define SCRIPTS_H

extern const char *name_strings[NAME_NUM_TYPES];

/** @brief Different terrain definitions for footsteps and particles */
typedef struct terrainType_s {
	const char *texture;			/**< script id is the texture name/path */
	const char *footStepSound;		/**< sound to play when walking on this terrain type */
	const char *particle;			/**< particle to spawn when walking on this type of terrain */
	float footStepVolume;			/**< footstep sound volume */
	float footStepAttenuation;		/**< reduction in amplitude and intensity of a sound */
	struct terrainType_s *hash_next;	/**< next entry in the hash list */
} terrainType_t;

const terrainType_t* Com_GetTerrainType(const char *textureName);

const char *Com_GiveName(int gender, const char *category);
const char *Com_GiveModel(int type, int gender, const char *teamID);
int Com_GetCharacterValues(const char *team, character_t * chr);
const char* Com_GetActorSound(teamDef_t* td, int gender, actorSound_t soundType);
teamDef_t* Com_GetTeamDefinitionByID(const char *team);
mapDef_t* Com_GetMapDefinitionByID(const char *mapDefID);
void Com_AddObjectLinks(void);
void Com_ParseScripts(void);
void Com_PrecacheCharacterModels(void);

#endif /* SCRIPTS_H */
