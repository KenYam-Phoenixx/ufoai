/**
 * @file sv_init.c
 * @brief Server initialisation stuff.
 */

/*
All original materal Copyright (C) 2002-2007 UFO: Alien Invasion team.

26/06/06, Eddy Cullen (ScreamingWithNoSound):
	Reformatted to agreed style.
	Added doxygen file comment.
	Updated copyright notice.

Original file from Quake 2 v3.21: quake2-2.31/server/sv_init.c

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

#include "server.h"

server_static_t svs;			/* persistant server info */
server_t sv;					/* local server */

/**
 * @brief
 */
static int SV_FindIndex (const char *name, int start, int max, qboolean create)
{
	int i;

	if (!name || !name[0])
		return 0;

	for (i = 1; i < max && sv.configstrings[start + i][0]; i++)
		if (!strcmp(sv.configstrings[start + i], name))
			return i;

	if (!create)
		return 0;

	if (i == max)
		Com_Error(ERR_DROP, "*Index: overflow");

	Q_strncpyz(sv.configstrings[start + i], name, sizeof(sv.configstrings[i]));

	if (sv.state != ss_loading) {	/* send the update to everyone */
		SZ_Clear(&sv.multicast);
		MSG_WriteChar(&sv.multicast, svc_configstring);
		MSG_WriteShort(&sv.multicast, start + i);
		MSG_WriteString(&sv.multicast, name);
		SV_Multicast(~0);
	}

	return i;
}


/**
 * @brief
 */
int SV_ModelIndex (const char *name)
{
	return SV_FindIndex(name, CS_MODELS, MAX_MODELS, qtrue);
}

/**
 * @brief
 */
int SV_SoundIndex (const char *name)
{
	return SV_FindIndex(name, CS_SOUNDS, MAX_SOUNDS, qtrue);
}

/**
 * @brief
 */
int SV_ImageIndex (const char *name)
{
	return SV_FindIndex(name, CS_IMAGES, MAX_IMAGES, qtrue);
}


/*
 * ================
 * MAP ASSEMBLY
 * ================
 * More info on map-assembly can be found at:
 * http://ufoai.ninex.info/wiki/index.php/Mapping/Random_map_assembly
 */
#define MAX_MAPASSEMBLIES 32
#define MAX_TILETYPES 64
#define MAX_TILESIZE 10
#define MAX_TILEALTS 8
#define MAX_FIXEDTILES 32
#define MAX_REGIONSIZE 32

#define MAX_ASSEMBLYRETRIES 8000
#define MAX_REGIONRETRIES 1

/** @brief Stores the parsed data fo a map tile. (See *.ump files) */
typedef struct mTile_s {
	char id[MAX_VAR];	/**< The id (string) of the tile as defined in the ump file (next to "tile"). */
	byte spec[MAX_TILESIZE][MAX_TILESIZE][MAX_TILEALTS];	/** @todo  document me */
	int w, h;	/**< The width and height of the tile. */
} mTile_t;

/**
 * @brief Stores the parsed data of an assembly definition.  (See *.ump files) 
 * @todo: Please have a look if the comments are correct.
 */
typedef struct mAssembly_s {
	char id[MAX_VAR];		/**< The id (string) of the assembly as defined in the ump file (next to "assembly"). */
	char title[MAX_VAR];		/**< The full title fo this assembly. */
	byte min[MAX_TILETYPES];	/**< Minimum tile number for each used tile-type. */
	byte max[MAX_TILETYPES];	/**< Maximum tile number for each used tile-type. */
	byte fT[MAX_FIXEDTILES];	/** Index of used (fix) tile in mTile[] array */
	byte fX[MAX_FIXEDTILES];	/** x position of the used  (fix) tile in fT */
	byte fY[MAX_FIXEDTILES];	/** y position of the used  (fix) tile in fT */
	int numFixed;	/** Number of fixed tiles. Counts entries of fX, fY and fT */
	int w, h;	/**< The width and height of the assembly. (size "<w> <h>") */
} mAssembly_t;

/**
 * @brief Defines a placed tile
 * @sa mTile_t
 */
typedef struct mPlaced_s {
	mTile_t *tile;	/**< The tile that was/is placed. */
	int x, y;		/**< The position in the map the tile was/is placed in. */
} mPlaced_t;

static mTile_t mTile[MAX_TILETYPES];					 /**< @todo A list of parsed map-tiles. */
static mAssembly_t mAssembly[MAX_MAPASSEMBLIES];		 /**< @todo A list of parsed assembly definitions. */

static int numTiles;		 /**< @todo The number of tiles in mTile. */
static int numAssemblies;	 /**< @todo The number of assemblies in mAssembly. */

static mPlaced_t mPlaced[MAX_MAPTILES];	 /**< @todo Holds all tiles that have been placed ont he current map. */
static int numPlaced;				/**< @todo The number of tiles in mPlaced. */

static short prList[32 * 32];			/**< @todo document me */
static short trList[MAX_TILETYPES];	/**< @todo document me */

static mAssembly_t *mAsm;	/**< @todo document me (current map?) */
static int mapSize;			/**< @todo document me */
static int mapX, mapY;		/**< @todo document me (current position in the map?) */
static int mapW, mapH;		/**< @todo document me (width and heigth of the curent map?) */

/**
 * @brief
 */
static void RandomList (int n, short *list)
{
	short i, r, t;

	for (i = 0; i < n; i++)
		list[i] = i;

	for (i = 0; i < n; i++) {
		r = rand() % (i + (n - i));
		t = list[r];
		list[r] = list[i];
		list[i] = t;
	}
}


#define SOLID 255

/**
 * @brief Convert to tile spec - normalize the characters
 * @sa SV_ParseMapTile
 * @note a tile definition looks like this:
 * @code tile +s02
 * {
 * 3 3
 *
 * 0      a      0
 * b      +b     b
 * 0      a      0
 * }
 * @endcode
 * tile +s02 defines the name of the tile which can be refered to from the assembly
 * the first two numbers defines the tile size - if you have a tile with the 'real'
 * size of 1x1 (256x256 in radiant) the definition is 3x3 because you have to
 * define the surroundings, too
 * The field marked with the + is the 'real' mapparts all the others are the
 * surroundings - the letters of the surroundings must have a tile definition with
 * a + and the letter, too - otherwise the placing of the tile may fail
 *
 * @note If you marked a tile with + the mTile_t->spec at that position will be SOLID
 * @note valid tile characters are 0-9 and a-z
 */
static byte tileChar (const char chr)
{
	if (chr == '+')
		return SOLID;
	else if (chr >= '0' && chr <= '9')
		return chr - '0';
	/* allow 0-9 - that's the +10 */
	else if (chr >= 'a' && chr <= 'z')
		return chr - ('a' + 10);
	else if (chr >= 'A' && chr <= 'Z')
		return chr - ('A' + 10);
	else
		Com_Error(ERR_DROP, "SV_ParseMapTile: Invalid tile char '%c'\n", chr);
	/* never reached */
	return 0;
}

/**
 * @brief Parsed a tile definition out of the ump-files
 * @sa SV_ParseAssembly
 * @sa SV_AssembleMap
 */
static void SV_ParseMapTile (const char *filename, char **text)
{
	const char *errhead = "SV_ParseMapTile: Unexpected end of file (";
	char *token, *chr;
	mTile_t *t;
	int x, y, i;

	/* get tile name */
	token = COM_EParse(text, errhead, filename);
	if (!*text)
		return;
	if (numTiles >= MAX_TILETYPES) {
		Com_Printf("SV_ParseMapTile: Too many map tile types (%s)\n", filename);
		return;
	}
	t = &mTile[numTiles];
	memset(t, 0, sizeof(mTile_t));
	Q_strncpyz(t->id, token, sizeof(t->id));

	/* start parsing the block */
	token = COM_EParse(text, errhead, filename);
	if (!*text)
		return;
	if (*token != '{') {
		Com_Printf("SV_ParseMapTile: Expected '{' for tile '%s' (%s)\n", t->id, filename);
		return;
	}

	/* get width and height */
	token = COM_EParse(text, errhead, filename);
	if (!*text)
		return;
	t->w = atoi(token);

	token = COM_EParse(text, errhead, filename);
	if (!*text)
		return;
	t->h = atoi(token);

	if (t->w > MAX_TILESIZE || t->h > MAX_TILESIZE) {
		Com_Printf("SV_ParseMapTile: Bad tile size [%i %i] (%s) (max. [%i %i])\n", t->w, t->h, filename, MAX_TILESIZE, MAX_TILESIZE);
		*text = strchr(*text, '}');
		return;
	}

	/* get tile specs */
	for (y = t->h - 1; y >= 0; y--)
		for (x = 0; x < t->w; x++) {
			token = COM_EParse(text, errhead, filename);
			if (!*text || *token == '}') {
				Com_Printf("SV_ParseMapTile: Bad tile desc in '%s' - not enough entries for size\n", t->id);
				*text = strchr(*text, '}') + 1;
				return;
			}
			chr = (char *) &t->spec[y][x][0];
			for (i = 0; token[i] && i < MAX_TILEALTS; i++, chr++) {
				*chr = tileChar(token[i]);
			}
		}

	token = COM_EParse(text, errhead, filename);

	/* get connections */
	if (*token != '}')
		Com_Printf("SV_ParseMapTile: Bad tile desc in '%s' - too many entries for size\n", t->id);

	/* successfully parsed - this tile counts */
	numTiles++;
}


/**
 * @brief Parses an assembly block
 * @sa SV_AssembleMap
 * @sa SV_ParseMapTile
 * @note: format of size: "size x y"
 * @note: format of fix: "fix [tilename] x y"
 * @note: format of tile: "[tilename] min max"
 */
static void SV_ParseAssembly (const char *filename, char **text)
{
	const char *errhead = "SV_ParseAssembly: Unexpected end of file (";
	char *token;
	mAssembly_t *a;
	int i, x, y;

	/* get assembly name */
	token = COM_EParse(text, errhead, filename);
	if (!*text)
		return;
	if (numAssemblies >= MAX_MAPASSEMBLIES) {
		Com_Printf("SV_ParseAssembly: Too many map assemblies (%s)\n", filename);
		return;
	}

	/* init */
	a = &mAssembly[numAssemblies++];
	memset(a, 0, sizeof(mAssembly_t));
	Q_strncpyz(a->id, token, sizeof(a->id));
	a->w = 8;
	a->h = 8;

	do {
		/* get tile name */
		token = COM_EParse(text, errhead, filename);
		if (!text || *token == '}')
			break;

		if (!Q_strncmp(token, "title", 5)) {
			/* get map title */
			token = COM_EParse(text, errhead, filename);
			if (!text)
				break;

			Q_strncpyz(a->title, token, sizeof(a->title));
			continue;
			/* fix tilename x y */
		} else if (!Q_strncmp(token, "size", 4)) {
			/* get map size */
			token = COM_EParse(text, errhead, filename);
			if (!text)
				break;

			sscanf(token, "%i %i", &a->w, &a->h);
			continue;
		/* fix tilename x y */
		} else if (!Q_strncmp(token, "fix", 3)) {
			/* get tile */
			token = COM_EParse(text, errhead, filename);
			if (!text)
				break;

			for (i = 0; i < numTiles; i++)
				if (!Q_strncmp(token, mTile[i].id, MAX_VAR)) {
					if (a->numFixed >= MAX_FIXEDTILES) {
						Com_Printf("SV_ParseAssembly: Too many fixed tiles\n");
						break;
					}
					/* get coordinates */
					token = COM_EParse(text, errhead, filename);
					if (!text)
						break;

					sscanf(token, "%i %i", &x, &y);
					a->fX[a->numFixed] = x;
					a->fY[a->numFixed] = y;
					a->fT[a->numFixed] = i;
					a->numFixed++;
				}
			continue;
		}

		for (i = 0; i < numTiles; i++)
			if (!Q_strncmp(token, mTile[i].id, MAX_VAR)) {
				/* get min and max tile number */
				token = COM_EParse(text, errhead, filename);
				if (!text)
					break;

				sscanf(token, "%i %i", &x, &y);
				a->min[i] = x;
				a->max[i] = y;
				break;
			}
	}
	while (text);
}


/**
 * @brief Adds a new map-tile to an assembled map. Also adds the tile to the placed-tiles list.
 * @param[in] map The map array the tile should be added to.
 * @param[in] tile The tile to add to the map.
 * @param[in] x The x position in the map where the tile should be placed.
 * @param[in] y The y position in the map where the tile should be placed.
 * @param[in|out] toFill This is the amount of tiles (tile-positions) in the map that are still to fill. It is updated on success here.
 * @return qtrue if the tile could be added.
 * @return qtrue if the tile does not fit or an error was encountered.
 * @sa SV_AssembleMap
 * @sa SV_AddRegion
 * @sa SV_FitTile
 */
static void SV_AddTile (byte map[32][32][MAX_TILEALTS], mTile_t * tile, int x, int y, int *toFill)
{
	qboolean bad;
	int tx, ty;
	int a, b, c;

	/* add the new tile */
	for (ty = 0; ty < tile->h; ty++)
		for (tx = 0; tx < tile->w; tx++) {
			assert(y+ty < 32);
			assert(x+tx < 32);
			if (tile->spec[ty][tx][0] == SOLID || !map[y + ty][x + tx][0]) {
				/* copy the solid info */
				if (tile->spec[ty][tx][0] == SOLID && toFill)
					*toFill -= 1;
				for (a = 0; a < MAX_TILEALTS; a++)
					map[y + ty][x + tx][a] = tile->spec[ty][tx][a];
			} else if (tile->spec[ty][tx][0] && map[y + ty][x + tx][0] != SOLID) {
				/* calc remaining connection options */
				for (a = 0; map[y + ty][x + tx][a] && a < MAX_TILEALTS; a++) {
					bad = qtrue;
					for (b = 0; bad && tile->spec[ty][tx][b] && b < MAX_TILEALTS; b++)
						if (tile->spec[ty][tx][b] == map[y + ty][x + tx][a])
							bad = qfalse;

					if (bad) {
						/* not an option anymore */
						for (c = a + 1; c < MAX_TILEALTS; c++)
							map[y + ty][x + tx][c - 1] = map[y + ty][x + tx][c];
						map[y + ty][x + tx][MAX_TILEALTS - 1] = 0;
						a--;
					}
				}
			}
		}

	/* add the tile */
	if (numPlaced >= MAX_MAPTILES)
		Com_Error(ERR_DROP, "SV_AddTile: Too many map tiles\n");

	mPlaced[numPlaced].tile = tile;
	mPlaced[numPlaced].x = x;
	mPlaced[numPlaced].y = y;
	numPlaced++;
}


/**
 * @brief Checks if a given map-tile fits into the empty space (in a given location) of a map.
 * @param[in] map The map to check agains the tile.
 * @param[in] tile The tile definition that should be fitted into the map.
 * @param[in] x The x position in the map where the tile is supposed to be placed/checked.
 * @param[in] y The y position in the map where the tile is supposed to be placed/checked.
 * @param[in] force
 * @return qtrue if the tile fits.
 * @return qfalse if the tile does not fit or an error was encountered.
 * @sa SV_AddMandatoryParts
 * @sa SV_AddRegion
 */
static qboolean SV_FitTile (byte map[32][32][MAX_TILEALTS], mTile_t * tile, int x, int y, qboolean force)
{
	qboolean touch;
	int tx, ty;
	int a, b;
	byte *spec = NULL;
	byte *m = NULL;

	if (x < 0 || y < 0)
		return qfalse;

	if (!tile)
		return qfalse;
	
	/* check for map border */
	if (x + tile->w > mapX + mapW + 2 || y + tile->h > mapY + mapH + 2)
		return qfalse;

	/* require touching tiles */
	if (x == mapX)
		touch = qtrue;
	else
		touch = qfalse;

	/* test for fit */
	spec = &tile->spec[0][0][0];
	m = &map[y][x][0];
	for (ty = 0; ty < tile->h; ty++) {
		for (tx = 0; tx < tile->w; tx++, spec += MAX_TILEALTS, m += MAX_TILEALTS) {
			if (*spec == SOLID && *m == SOLID) {
				/* already something there */
				return qfalse;
			} else if (*spec && *m) {
				/* check connection, avoid contradictory connections */
				if (*m == SOLID)
					touch = qtrue;

				/* check whether maptile and surrounding specs are suitable */
				for (a = 0; spec[a] && a < MAX_TILEALTS; a++)
					for (b = 0; m[b] && b < MAX_TILEALTS; b++)
						if (spec[a] == m[b])
							goto fine;

				/* not jumped => impossible */
				return qfalse;
			}
		  fine:;
		}
		spec += MAX_TILEALTS * (MAX_TILESIZE - tile->w);
		m += MAX_TILEALTS * (32 - tile->w);
	}

	/* it fits, check for touch */
	if (touch || !force)
		return qtrue;
	else
		return qfalse;
}


/**
 * @brief Adds a new region-part to an assembled map.
 * @param[in] map The map array the region should be added to.
 * @param[in] num TODO: writeme
 * @return qtrue if the region could be added.
 * @return qtrue if the region does not fit or an error was encountered.
 * @sa SV_FitTile
 * @sa SV_AddTile
 */
static qboolean SV_AddRegion (byte map[32][32][MAX_TILEALTS], byte * num)
{
	mTile_t *tile = NULL;
	int i, j, x, y;
	int toFill, oldToFill, oldNumPlaced;
	int pos, lastPos;

	if (!num)
		return qfalse;

	/* store old values */
	oldNumPlaced = numPlaced;

	/* count tiles to fill in */
	oldToFill = 0;
	for (y = mapY + 1; y < mapY + mapH + 1; y++)
		for (x = mapX + 1; x < mapX + mapW + 1; x++)
			if (map[y][x][0] != SOLID)
				oldToFill++;

	/* restore old values */
	toFill = oldToFill;
	numPlaced = oldNumPlaced;

	RandomList(mapSize, prList);
	lastPos = mapSize - 1;
	pos = 0;

	/* finishing condition */
	while (toFill > 0) {
		/* refresh random lists */
		RandomList(numTiles, trList);

		while (pos != lastPos) {
			x = prList[pos] % mapW + mapX;
			y = prList[pos] / mapW + mapY;

			for (i = 0; i < numTiles; i++) {
				j = trList[i];
				if (num[j] >= mAsm->max[j])
					continue;
				tile = &mTile[j];

				/* add the tile, if it fits */
				if (SV_FitTile(map, tile, x, y, qtrue)) {
					/* mark as used and add the tile */
					num[j]++;
					SV_AddTile(map, tile, x, y, &toFill);
					lastPos = pos;
					break;
				}
			}
			pos++;
			if (pos >= mapSize)
				pos = 0;

			if (i < numTiles)
				break;
		}
		if (pos == lastPos)
			break;
	}
	/* check for success */
	if (toFill <= 0)
		return qtrue;

	/* too many retries */
	return qfalse;
}


/**
 * @param[in] map The map parts should be added to.
 * @param[out] num TODO
 * @sa SV_FitTile
 * @sa SV_AddTile
 */
static qboolean SV_AddMandatoryParts (byte map[32][32][MAX_TILEALTS], byte * num)
{
	mTile_t *tile = NULL;
	int i, j, n, x, y;

	for (i = 0, tile = mTile; i < numTiles; i++, tile++) {
		for (j = 0; j < mAsm->min[i]; j++) {
			RandomList(mapSize, prList);
			for (n = 0; n < mapSize; n++) {
				x = prList[n] % mapW;
				y = prList[n] / mapH;
				if (SV_FitTile(map, tile, x, y, qfalse)) {
					/* add tile */
					SV_AddTile(map, tile, x, y, NULL);
					break;
				}
			}
			/* no tile fit */
			if (n >= mapSize) {
				Com_Printf("SV_AddMandatoryParts: Could not find a tile that fits for '%s'\n", tile->id);
				return qfalse;
			}
		}
		num[i] = mAsm->min[i];
	}
	/* success */
	return qtrue;
}


/**
 * @brief Assembles a "random" map
 * parses the *.ump files for assembling the "random" maps
 * @sa B_AssembleMap_f
 * @sa SV_AddTile
 * @sa SV_AddMandatoryParts
 * @sa SV_ParseAssembly
 * @sa SV_ParseMapTile
 */
static void SV_AssembleMap (const char *name, const char *assembly, char **map, char **pos)
{
	mPlaced_t *pl;
	byte curMap[32][32][MAX_TILEALTS];
	byte curNum[MAX_TILETYPES];
	char asmMap[MAX_TOKEN_CHARS * MAX_TILESTRINGS];
	char asmPos[MAX_TOKEN_CHARS * MAX_TILESTRINGS];
	char filename[MAX_QPATH];
	char basePath[MAX_QPATH];
	char *buf;
	char *text, *token;
	int i, tries;
	int regNumX, regNumY;
	float regFracX, regFracY;
	qboolean ok;

	/* load the map info */
	Com_sprintf(filename, sizeof(filename), "maps/%s.ump", name);
	FS_LoadFile(filename, (void **) &buf);
	if (!buf)
		Com_Error(ERR_DROP, "SV_AssembleMap: Map assembly info '%s' not found\n", filename);

	/* parse it */
	text = buf;
	numTiles = 0;
	numAssemblies = 0;
	basePath[0] = 0;
	do {
		token = COM_Parse(&text);
		if (!text)
			break;

		if (!Q_strcmp(token, "base")) {
			token = COM_Parse(&text);
			Q_strncpyz(basePath, token, sizeof(basePath));
		} else if (!Q_strcmp(token, "tile"))
			SV_ParseMapTile(filename, &text);
		else if (!Q_strcmp(token, "assembly"))
			SV_ParseAssembly(filename, &text);
		else if (!Q_strcmp(token, "{")) {
			Com_Printf("SV_AssembleMap: Skipping unknown block\n");
			/* ignore unknown block */
			text = strchr(text, '}') + 1;
			if (!text)
				break;
		} else
			Com_Printf("SV_AssembleMap: Unknown token '%s' (%s)\n", token, filename);
	} while (text);

	/* free the file */
	FS_FreeFile(buf);

	/* check for parsed tiles and assemblies */
	if (!numTiles)
		Com_Error(ERR_DROP, "No map tiles defined (%s)!\n", filename);
#ifdef DEBUG
	else
		Com_DPrintf("numTiles: %i\n", numTiles);
#endif

	if (!numAssemblies)
		Com_Error(ERR_DROP, "No map assemblies defined (%s)!\n", filename);
#ifdef DEBUG
	else
		Com_DPrintf("numAssemblies: %i\n", numAssemblies);
#endif

	/* get assembly */
	if (assembly && assembly[0]) {
		for (i = 0, mAsm = mAssembly; i < numAssemblies; i++, mAsm++)
			if (!Q_strcmp(assembly, mAsm->id))
				break;
		if (i >= numAssemblies) {
			Com_Printf("SV_AssembleMap: Map assembly '%s' not found\n", assembly);
			mAsm = NULL;
		}
	} else
		mAsm = NULL;

	/* use random assembly, if no valid one has been specified */
	if (!mAsm) {
		mAsm = &mAssembly[rand() % numAssemblies];
		Com_DPrintf("Use random assembly: '%s'\n", mAsm->id);
	}

	/* calculate regions */
	regNumX = mAsm->w / MAX_REGIONSIZE + 1;
	regNumY = mAsm->h / MAX_REGIONSIZE + 1;
	regFracX = (float) mAsm->w / regNumX;
	regFracY = (float) mAsm->h / regNumY;

	for (tries = 0; tries < MAX_ASSEMBLYRETRIES; tries++) {
		int x, y;

		/* assemble the map */
		numPlaced = 0;
		memset(&curMap[0][0][0], 0, sizeof(curMap));
		memset(&curNum[0], 0, sizeof(curNum));

		/* place fixed parts - defined in ump via fix parameter */
		for (i = 0; i < mAsm->numFixed; i++)
			SV_AddTile(curMap, &mTile[mAsm->fT[i]], mAsm->fX[i], mAsm->fY[i], NULL);

		/* place mandatory parts */
		mapX = 0;
		mapY = 0;
		mapW = mAsm->w;
		mapH = mAsm->h;
		mapSize = mAsm->w * mAsm->h;
		if (!SV_AddMandatoryParts(curMap, curNum))
			continue;

		/* start region assembly */
		ok = qtrue;
		for (y = 0; y < regNumY && ok; y++)
			for (x = 0; x < regNumX && ok; x++) {
				mapX = x * regFracX;
				mapY = y * regFracY;
				mapW = (int) ((x + 1) * regFracX + 0.1) - (int) (x * regFracX);
				mapH = (int) ((y + 1) * regFracY + 0.1) - (int) (y * regFracY);
				mapSize = mapW * mapH;
				if (!SV_AddRegion(curMap, curNum))
					ok = qfalse;
			}

		/* break if everything seems to be ok */
		if (ok)
			break;
	}

	if (tries >= MAX_ASSEMBLYRETRIES)
		Com_Error(ERR_DROP, "SV_AssembleMap: Failed to assemble map (%s) (max-tries reached)\n", filename);

	/* prepare map and pos strings */
	if (basePath[0]) {
		asmMap[0] = '-';
		Q_strncpyz(&asmMap[1], basePath, MAX_QPATH-1);
		*map = asmMap;
	} else {
		asmMap[0] = 0;
		*map = asmMap + 1;
	}
	asmPos[0] = 0;
	*pos = asmPos + 1;

	/* generate the strings */
	for (i = 0, pl = mPlaced; i < numPlaced; i++, pl++) {
		Q_strcat(asmMap, va(" %s", pl->tile->id), MAX_TOKEN_CHARS * MAX_TILESTRINGS);
		Q_strcat(asmPos, va(" %i %i", (pl->x - mAsm->w / 2) * 8, (pl->y - mAsm->h / 2) * 8), MAX_TOKEN_CHARS * MAX_TILESTRINGS);
	}

	Com_DPrintf("tiles: %s\n", *map);
	Com_DPrintf("pos: %s\n", *pos);
	Com_DPrintf("tiles: %i tries: %i\n", numPlaced, tries + 1);
}

/**
 * @brief Get the map title for a given map
 * @note the title string must be translated client side
 * @return Never NULL - mapname or maptitle (if defined in assembly)
 */
static const char* SV_GetMapTitle (const char* const mapname)
{
	assert(mapname);

	if (mapname[0] == '+') {
		if (mAsm && mAsm->title[0]) {
			/* return the assembly title itself - must be translated client side */
			if (mAsm->title[0] == '_')
				return mAsm->title + 1;
			else {
				Com_Printf("The assembly title '%s' is not marked as translateable\n", mAsm->title);
				return mAsm->title;
			}
		}
	}
	return mapname;
}

/**
 * @brief Change the server to a new map, taking all connected clients along with it.
 * @sa SV_AssembleMap
 * @sa CM_LoadMap
 * @sa Com_SetServerState
 */
static void SV_SpawnServer (char *server, char *param, server_state_t serverstate, qboolean attractloop)
{
	int i;
	unsigned checksum = 0;
	char *map, *pos;

	if (attractloop)
		Cvar_Set("paused", "0");

	Com_Printf("------- Server Initialization -------\n");

	Com_DPrintf("SpawnServer: %s\n", server);
	if (sv.demofile.f)
		fclose(sv.demofile.f);

	svs.spawncount++;
	/* any partially connected client will be restarted */
	sv.state = ss_dead;
	Com_SetServerState(sv.state);

	/* wipe the entire per-level structure */
	memset(&sv, 0, sizeof(sv));
	svs.realtime = 0;
	sv.attractloop = attractloop;

	/* save name for levels that don't set message */
	Q_strncpyz(sv.configstrings[CS_NAME], server, MAX_TOKEN_CHARS);

	SZ_Init(&sv.multicast, sv.multicast_buf, sizeof(sv.multicast_buf));

	Q_strncpyz(sv.name, server, sizeof(sv.name));

	/* leave slots at start for clients only */
	for (i = 0; i < sv_maxclients->value; i++) {
		/* needs to reconnect */
		if (svs.clients[i].state > cs_connected)
			svs.clients[i].state = cs_connected;
		svs.clients[i].lastframe = -1;
	}

	sv.time = 1000;

	switch (serverstate) {
	case ss_game:
		/* assemble and load the map */
		if (server[0] == '+') {
			SV_AssembleMap(server + 1, param, &map, &pos);
			if (!map || !pos) {
				Com_Printf("Could not load assembly for map '%s'\n", server);
				return;
			}
		} else {
			map = server;
			pos = param;
		}

		Q_strncpyz(sv.configstrings[CS_TILES], map, MAX_TOKEN_CHARS * MAX_TILESTRINGS);
		if (pos)
			Q_strncpyz(sv.configstrings[CS_POSITIONS], pos, MAX_TOKEN_CHARS * MAX_TILESTRINGS);
		else
			sv.configstrings[CS_POSITIONS][0] = 0;

		CM_LoadMap(map, pos, &checksum);
		break;
	default:
		CM_LoadMap("", NULL, &checksum);
		break;
	}

	Com_DPrintf("checksum for this map: %u\n", checksum);
	Com_sprintf(sv.configstrings[CS_MAPCHECKSUM], sizeof(sv.configstrings[CS_MAPCHECKSUM]), "%i", checksum);

	checksum = 0;	/* TODO */
	Com_DPrintf("ufo script checksum %u\n", checksum);
	Com_sprintf(sv.configstrings[CS_UFOCHECKSUM], sizeof(sv.configstrings[CS_UFOCHECKSUM]), "%i", checksum);

	Com_sprintf(sv.configstrings[CS_VERSION], sizeof(sv.configstrings[CS_VERSION]), UFO_VERSION);

	Com_sprintf(sv.configstrings[CS_MAPTITLE], sizeof(sv.configstrings[CS_MAPTITLE]), "%s", SV_GetMapTitle(server));

	/* clear physics interaction links */
	SV_ClearWorld();

	/* fix this! */
	for (i = 1; i <= CM_NumInlineModels(); i++)
		sv.models[i] = CM_InlineModel(va("*%i", i));

	/* precache and static commands can be issued during */
	/* map initialization */
	sv.state = ss_loading;
	Com_SetServerState(sv.state);

	/* load and spawn all other entities */
	ge->SpawnEntities(sv.name, CM_EntityString());

	/* all precaches are complete */
	sv.state = serverstate;
	Com_SetServerState(sv.state);

	sv.active = qtrue;

	/* set serverinfo variable */
	Cvar_FullSet("mapname", sv.name, CVAR_SERVERINFO | CVAR_NOSET);

	Com_Printf("-------------------------------------\n");
}

/**
 * @brief A brand new game has been started
 */
static void SV_InitGame (void)
{
/*	edict_t	*ent; */
	char masterserver[32];

	if (svs.initialized) {
		/* cause any connected clients to reconnect */
		SV_Shutdown("Server restarted\n", qtrue);
	} else {
		/* make sure the client is down */
		CL_Drop();
		SCR_BeginLoadingPlaque();
	}

	/* allow next change after map change or restart */
	sv_maxclients->flags |= CVAR_LATCH;

	/* get any latched variable changes (maxclients, etc) */
	Cvar_GetLatchedVars();

	svs.initialized = qtrue;

/*	Cvar_FullSet ("maxclients", "8", CVAR_SERVERINFO | CVAR_LATCH); */

	svs.spawncount = rand();
	svs.clients = Mem_Alloc(sizeof(client_t) * sv_maxclients->value);
	svs.num_client_entities = sv_maxclients->value * UPDATE_BACKUP * 64;

	/* init network stuff */
	NET_Config((sv_maxclients->value > 1));

	/* heartbeats will always be sent to the ufo master */
	svs.last_heartbeat = -99999;	/* send immediately */
	Com_sprintf(masterserver, sizeof(masterserver), "%s:%s", Cvar_VariableString("masterserver_ip"), Cvar_VariableString("masterserver_port"));
	NET_StringToAdr(masterserver, &master_adr);

	/* init game */
	SV_InitGameProgs();

	if (sv_maxclients->integer != 1 && (dedicated->value || public_server->value))
		SV_SetMaster_f();
}


/**
 * @brief Loads the map
 * @note the full syntax is:
 * @note map [*]<map>$<startspot>+<nextserver>
 * command from the console or progs.
 * Map can also be a .pcx, or .dm2 file
 * Nextserver is used to allow a cinematic to play, then proceed to
 * another level:
 * map tram.pcx#jail_e3
 * @sa SV_SpawnServer
 * @sa SV_Map_f
 * @sa SV_Demo_f
 */
extern void SV_Map (qboolean attractloop, char *levelstring, char *assembly)
{
	char level[MAX_QPATH];
	char *ch;
	int l;

	sv.attractloop = attractloop;

	if (sv.state == ss_dead)
		SV_InitGame();			/* the game is just starting */

	Q_strncpyz(level, levelstring, sizeof(level));

	/* if there is a # in the map, set nextserver to the remainder */
	ch = strstr(level, "#");
	if (ch && !assembly) {
		*ch = 0;
		Cvar_Set("nextserver", va("gamemap \"%s\"", ch + 1));
	} else
		Cvar_Set("nextserver", "");

	/* skip the end-of-unit flag if necessary */
	if (level[0] == '*')
		Q_strncpyz(level, level + 1, sizeof(level));

	l = strlen(level);
	if (l > 4 && !Q_strcmp(level + l - 4, ".dm2")) {
		SCR_BeginLoadingPlaque();	/* for local system */
		SV_BroadcastCommand("changing\n");
		SV_SpawnServer(level, NULL, ss_demo, attractloop);
	} else if (l > 4 && !Q_strcmp(level + l - 4, ".pcx")) {
		SCR_BeginLoadingPlaque();	/* for local system */
		SV_BroadcastCommand("changing\n");
		SV_SpawnServer(level, NULL, ss_pic, attractloop);
	} else {
		SCR_BeginLoadingPlaque();	/* for local system */
		SV_BroadcastCommand("changing\n");
		SV_SendClientMessages();
		SV_SpawnServer(levelstring, assembly, ss_game, attractloop);
		Cbuf_CopyToDefer();
	}

	SV_BroadcastCommand("reconnect\n");
}
