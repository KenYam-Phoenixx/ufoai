/**
 * @file map.c
 */

/*
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


#include "qbsp.h"

extern qboolean onlyents;

int			nummapbrushes;
mapbrush_t	mapbrushes[MAX_MAP_BRUSHES];

int			nummapbrushsides;
side_t		brushsides[MAX_MAP_SIDES];
static brush_texture_t side_brushtextures[MAX_MAP_SIDES];

int			nummapplanes;
plane_t		mapplanes[MAX_MAP_PLANES];

#define	PLANE_HASHES	1024
static plane_t *planehash[PLANE_HASHES];

static vec3_t map_mins, map_maxs;

/* undefine to make plane finding use linear sort */
#define	USE_HASHING

void TestExpandBrushes(void);

static int c_boxbevels = 0;
static int c_edgebevels = 0;
static int c_areaportals = 0;
static int c_clipbrushes = 0;

/*
=============================================================================
PLANE FINDING
=============================================================================
*/


static int PlaneTypeForNormal (vec3_t normal)
{
	vec_t ax, ay, az;

	/* NOTE: should these have an epsilon around 1.0?		 */
	if (normal[0] == 1.0 || normal[0] == -1.0)
		return PLANE_X;
	if (normal[1] == 1.0 || normal[1] == -1.0)
		return PLANE_Y;
	if (normal[2] == 1.0 || normal[2] == -1.0)
		return PLANE_Z;

	ax = fabs(normal[0]);
	ay = fabs(normal[1]);
	az = fabs(normal[2]);

	if (ax >= ay && ax >= az)
		return PLANE_ANYX;
	if (ay >= ax && ay >= az)
		return PLANE_ANYY;
	return PLANE_ANYZ;
}

#define	NORMAL_EPSILON	0.00001
#define	DIST_EPSILON	0.01
static qboolean PlaneEqual (plane_t *p, vec3_t normal, vec_t dist)
{
	if (fabs(p->normal[0] - normal[0]) < NORMAL_EPSILON
	 && fabs(p->normal[1] - normal[1]) < NORMAL_EPSILON
	 && fabs(p->normal[2] - normal[2]) < NORMAL_EPSILON
	 && fabs(p->dist - dist) < DIST_EPSILON)
		return qtrue;
	return qfalse;
}

static void AddPlaneToHash (plane_t *p)
{
	int hash;

	hash = (int)fabs(p->dist) / 8;
	hash &= (PLANE_HASHES-1);

	p->hash_chain = planehash[hash];
	planehash[hash] = p;
}

static int CreateNewFloatPlane (vec3_t normal, vec_t dist)
{
	plane_t *p, temp;

	if (VectorLength(normal) < 0.5)
		Sys_Error("FloatPlane: bad normal");
	/* create a new plane */
	if (nummapplanes+2 > MAX_MAP_PLANES)
		Sys_Error("MAX_MAP_PLANES (%i)", nummapplanes+2);

	p = &mapplanes[nummapplanes];
	VectorCopy(normal, p->normal);
	p->dist = dist;
	p->type = (p + 1)->type = PlaneTypeForNormal (p->normal);

	VectorSubtract (vec3_origin, normal, (p + 1)->normal);
	(p + 1)->dist = -dist;

	nummapplanes += 2;

	/* always put axial planes facing positive first */
	if (p->type < 3) {
		if (p->normal[0] < 0 || p->normal[1] < 0 || p->normal[2] < 0) {
			/* flip order */
			temp = *p;
			*p = *(p + 1);
			*(p + 1) = temp;

			AddPlaneToHash(p);
			AddPlaneToHash(p + 1);
			return nummapplanes - 1;
		}
	}

	AddPlaneToHash(p);
	AddPlaneToHash(p + 1);
	return nummapplanes - 2;
}

/**
 * @brief Round the vector to int values
 * @note Can be used to save net bandwidth
 */
static void SnapVector (vec3_t normal)
{
	int i;

	for (i = 0; i < 3; i++) {
		if (fabs(normal[i] - 1) < NORMAL_EPSILON) {
			VectorClear(normal);
			normal[i] = 1;
			break;
		}
		if (fabs(normal[i] - -1) < NORMAL_EPSILON) {
			VectorClear(normal);
			normal[i] = -1;
			break;
		}
	}
}

static void SnapPlane (vec3_t normal, vec_t *dist)
{
	SnapVector(normal);

	if (fabs(*dist - Q_rint(*dist)) < DIST_EPSILON)
		*dist = Q_rint(*dist);
}

int FindFloatPlane (vec3_t normal, vec_t dist)
{
	int i;
	plane_t *p;
	int hash, h;

	SnapPlane(normal, &dist);
	hash = (int)fabs(dist) / 8;
	hash &= (PLANE_HASHES - 1);

	/* search the border bins as well */
	for (i = -1; i <= 1; i++) {
		h = (hash + i) & (PLANE_HASHES - 1);
		for (p = planehash[h]; p; p = p->hash_chain) {
			if (PlaneEqual(p, normal, dist))
				return p-mapplanes;
		}
	}

	return CreateNewFloatPlane (normal, dist);
}

static int PlaneFromPoints (int *p0, int *p1, int *p2)
{
	vec3_t t1, t2, normal;
	vec_t dist;

	VectorSubtract(p0, p1, t1);
	VectorSubtract(p2, p1, t2);
	CrossProduct(t1, t2, normal);
	VectorNormalize(normal);

	dist = DotProduct(p0, normal);

	return FindFloatPlane(normal, dist);
}


/*==================================================================== */


static int BrushContents (mapbrush_t *b)
{
	int contentFlags, i, trans;
	side_t *s;

	s = &b->original_sides[0];
	contentFlags = s->contentFlags;
	trans = texinfo[s->texinfo].surfaceFlags;
	for (i = 1; i < b->numsides; i++, s++) {
		s = &b->original_sides[i];
		trans |= texinfo[s->texinfo].surfaceFlags;
		if (s->contentFlags != contentFlags) {
			Sys_FPrintf(SYS_VRB, "Entity %i, Brush %i: mixed face contents (f: %i, %i)\n"
				, b->entitynum, b->brushnum, s->contentFlags, contentFlags);
			break;
		}
	}

	/* if any side is translucent, mark the contents */
	/* and change solid to window */
	if (trans & (SURF_TRANS33 | SURF_TRANS66 | SURF_ALPHATEST)) {
		contentFlags |= CONTENTS_TRANSLUCENT;
		if (contentFlags & CONTENTS_SOLID) {
			contentFlags &= ~CONTENTS_SOLID;
			contentFlags |= CONTENTS_WINDOW;
		}
	}

	return contentFlags;
}


/*============================================================================ */

/**
 * @brief Adds any additional planes necessary to allow the brush to be expanded
 * against axial bounding boxes
 */
static void AddBrushBevels (mapbrush_t *b)
{
	int axis, dir;
	int i, j, k, l, order;
	side_t sidetemp;
	brush_texture_t tdtemp;
	side_t *s, *s2;
	vec3_t normal;
	float dist;
	winding_t *w, *w2;
	vec3_t vec, vec2;
	float d;

	/* add the axial planes */
	order = 0;
	for (axis = 0; axis < 3; axis++) {
		for (dir = -1; dir <= 1; dir += 2, order++) {
			/* see if the plane is already present */
			for (i = 0, s = b->original_sides; i < b->numsides; i++, s++) {
				if (mapplanes[s->planenum].normal[axis] == dir)
					break;
			}

			if (i == b->numsides) {	/* add a new side */
				if (nummapbrushsides == MAX_MAP_BRUSHSIDES)
					Sys_Error("MAX_MAP_BRUSHSIDES (%i)", nummapbrushsides);
				nummapbrushsides++;
				b->numsides++;
				VectorClear (normal);
				normal[axis] = dir;
				if (dir == 1)
					dist = b->maxs[axis];
				else
					dist = -b->mins[axis];
				s->planenum = FindFloatPlane (normal, dist);
				s->texinfo = b->original_sides[0].texinfo;
				s->contentFlags = b->original_sides[0].contentFlags;
				s->bevel = qtrue;
				c_boxbevels++;
			}

			/* if the plane is not in it canonical order, swap it */
			if (i != order) {
				sidetemp = b->original_sides[order];
				b->original_sides[order] = b->original_sides[i];
				b->original_sides[i] = sidetemp;

				j = b->original_sides - brushsides;
				tdtemp = side_brushtextures[j + order];
				side_brushtextures[j + order] = side_brushtextures[j + i];
				side_brushtextures[j + i] = tdtemp;
			}
		}
	}

	/* add the edge bevels */
	if (b->numsides == 6)
		return;		/* pure axial */

	/* test the non-axial plane edges */
	for (i = 6; i < b->numsides; i++) {
		s = b->original_sides + i;
		w = s->winding;
		if (!w)
			continue;
		for (j = 0; j < w->numpoints; j++) {
			k = (j+1) % w->numpoints;
			VectorSubtract(w->p[j], w->p[k], vec);
			if (VectorNormalize(vec) < 0.5)
				continue;
			SnapVector(vec);
			for (k = 0; k < 3; k++)
				if (vec[k] == -1 || vec[k] == 1)
					break;	/* axial */
			if (k != 3)
				continue;	/* only test non-axial edges */

			/* try the six possible slanted axials from this edge */
			for (axis = 0; axis < 3; axis++) {
				for (dir = -1; dir <= 1; dir += 2) {
					/* construct a plane */
					VectorClear(vec2);
					vec2[axis] = dir;
					CrossProduct(vec, vec2, normal);
					if (VectorNormalize(normal) < 0.5)
						continue;
					dist = DotProduct(w->p[j], normal);

					/* if all the points on all the sides are */
					/* behind this plane, it is a proper edge bevel */
					for (k = 0; k < b->numsides; k++) {
						/* FIXME: This leads to different results on different archs
						 * due to float rounding/precision errors */
						/* if this plane has already been used, skip it */
						if (PlaneEqual(&mapplanes[b->original_sides[k].planenum]
							, normal, dist) )
							break;

						w2 = b->original_sides[k].winding;
						if (!w2)
							continue;
						for (l = 0; l < w2->numpoints; l++) {
							d = DotProduct(w2->p[l], normal) - dist;
							if (d > 0.1)
								break;	/* point in front */
						}
						if (l != w2->numpoints)
							break;
					}

					if (k != b->numsides)
						continue;	/* wasn't part of the outer hull */
					/* add this plane */
					if (nummapbrushsides == MAX_MAP_BRUSHSIDES)
						Sys_Error("MAX_MAP_BRUSHSIDES (%i)", nummapbrushsides);
					nummapbrushsides++;
					s2 = &b->original_sides[b->numsides];
					s2->planenum = FindFloatPlane(normal, dist);
					s2->texinfo = b->original_sides[0].texinfo;
					s2->contentFlags = b->original_sides[0].contentFlags;
					s2->bevel = qtrue;
					c_edgebevels++;
					b->numsides++;
				}
			}
		}
	}
}


/**
 * @brief makes basewindigs for sides and mins / maxs for the brush
 */
static qboolean MakeBrushWindings (mapbrush_t *ob)
{
	int i, j;
	winding_t *w;
	side_t *side;
	plane_t *plane;

	ClearBounds(ob->mins, ob->maxs);

	for (i = 0; i < ob->numsides; i++) {
		plane = &mapplanes[ob->original_sides[i].planenum];
		w = BaseWindingForPlane(plane->normal, plane->dist);
		for (j = 0; j < ob->numsides && w; j++) {
			if (i == j)
				continue;
			if (ob->original_sides[j].bevel)
				continue;
			plane = &mapplanes[ob->original_sides[j].planenum^1];
			ChopWindingInPlace(&w, plane->normal, plane->dist, 0); /*CLIP_EPSILON); */
		}

		side = &ob->original_sides[i];
		side->winding = w;
		if (w) {
			side->visible = qtrue;
			for (j = 0; j < w->numpoints; j++)
				AddPointToBounds(w->p[j], ob->mins, ob->maxs);
		}
	}

	for (i = 0; i < 3; i++) {
		if (ob->mins[0] < -4096 || ob->maxs[0] > 4096)
			Com_Printf("entity %i, brush %i: bounds out of range\n", ob->entitynum, ob->brushnum);
		if (ob->mins[0] > 4096 || ob->maxs[0] < -4096)
			Com_Printf("entity %i, brush %i: no visible sides on brush\n", ob->entitynum, ob->brushnum);
	}

	return qtrue;
}

/**
 * @sa FindMiptex
 */
static void ParseBrush (entity_t *mapent)
{
	mapbrush_t *b;
	int i, j, k, mt;
	side_t *side, *s2;
	int planenum;
	brush_texture_t td;
	int planepts[3][3];
	qboolean phongShading;

	if (nummapbrushes == MAX_MAP_BRUSHES)
		Sys_Error("nummapbrushes == MAX_MAP_BRUSHES (%i)", nummapbrushes);

	b = &mapbrushes[nummapbrushes];
	b->original_sides = &brushsides[nummapbrushsides];
	b->entitynum = num_entities-1;
	b->brushnum = nummapbrushes - mapent->firstbrush;

	b->optimizedDetail = qfalse;
	b->isTerrain = (!strcmp("func_group", ValueForKey(&entities[b->entitynum], "classname"))
				&& strlen(ValueForKey(&entities[b->entitynum], "terrain")) > 0);
	b->isGenSurf = (!strcmp("func_group", ValueForKey(&entities[b->entitynum], "classname"))
				&& strlen(ValueForKey(&entities[b->entitynum], "gensurf")) > 0);
	phongShading = (!strcmp("func_group", ValueForKey(&entities[b->entitynum], "classname"))
				&& strlen(ValueForKey(&entities[b->entitynum], "phongshading")) > 0);
#if 1
	if (b->isTerrain)
		Sys_FPrintf(SYS_VRB, "Brush number %i in entity number %i has terrain flag set.\n", b->brushnum, b->entitynum);
	if (phongShading)
		Sys_FPrintf(SYS_VRB, "Brush number %i in entity number %i has phong shading flag set.\n", b->brushnum, b->entitynum);
#endif

	do {
		if (!GetToken(qtrue))
			break;
		if (*token == '}')
			break;

		if (nummapbrushsides == MAX_MAP_BRUSHSIDES)
			Sys_Error("nummapbrushsides == MAX_MAP_BRUSHSIDES (%i)", nummapbrushsides);
		side = &brushsides[nummapbrushsides];

		/* read the three point plane definition */
		for (i = 0; i < 3; i++) {
			if (i != 0)
				GetToken(qtrue);
			if (*token != '(')
				Sys_Error("parsing brush");

			for (j = 0; j < 3; j++) {
				GetToken(qfalse);
				planepts[i][j] = atoi(token);
			}

			GetToken(qfalse);
			if (*token != ')')
				Sys_Error("parsing brush");
		}

		/* read the texturedef */
		GetToken(qfalse);
		strcpy(td.name, token);

		GetToken(qfalse);
		td.shift[0] = atoi(token);
		GetToken(qfalse);
		td.shift[1] = atoi(token);
		GetToken(qfalse);
		td.rotate = atoi(token);
		GetToken(qfalse);
		td.scale[0] = atof(token);
		GetToken(qfalse);
		td.scale[1] = atof(token);

		/* find default flags and values */
		mt = FindMiptex(td.name);
		if (mt >= 0) {
			td.value = textureref[mt].value;
			side->contentFlags = textureref[mt].contentFlags;
			side->surfaceFlags = td.surfaceFlags = textureref[mt].surfaceFlags;
		} else {
			side->surfaceFlags = td.surfaceFlags = side->contentFlags = td.value = 0;
		}

		if (TokenAvailable()) {
			GetToken(qfalse);
			side->contentFlags = atoi(token);
			GetToken(qfalse);
			side->surfaceFlags = td.surfaceFlags = atoi(token);
			GetToken(qfalse);
			td.value = atoi(token);
		}

		/* translucent objects are automatically classified as detail */
		if (side->surfaceFlags & (SURF_TRANS33 | SURF_TRANS66 | SURF_ALPHATEST))
			side->contentFlags |= CONTENTS_DETAIL;
		if (config.fulldetail)
			side->contentFlags &= ~CONTENTS_DETAIL;
		if (!(side->contentFlags & (LAST_VISIBLE_CONTENTS - 1)))
			side->contentFlags |= CONTENTS_SOLID;

		/* hints and skips are never detail, and have no content */
		if (side->surfaceFlags & (SURF_HINT | SURF_SKIP)) {
			side->contentFlags = 0;
			side->surfaceFlags &= ~CONTENTS_DETAIL;
		}

		/* find the plane number */
		planenum = PlaneFromPoints(planepts[0], planepts[1], planepts[2]);
		if (planenum == -1) {
			Com_Printf("Entity %i, Brush %i: plane with no normal\n"
				, b->entitynum, b->brushnum);
			continue;
		}

		/* see if the plane has been used already */
		for (k = 0; k < b->numsides; k++) {
			s2 = b->original_sides + k;
			if (s2->planenum == planenum) {
				Com_Printf("Entity %i, Brush %i: duplicate plane\n"
					, b->entitynum, b->brushnum);
				break;
			}
			if (s2->planenum == (planenum ^ 1) ) {
				Com_Printf("Entity %i, Brush %i: mirrored plane\n"
					, b->entitynum, b->brushnum);
				break;
			}
		}
		if (k != b->numsides)
			continue;		/* duplicated */

		/* keep this side */
		side = b->original_sides + b->numsides;
		side->planenum = planenum;
		side->texinfo = TexinfoForBrushTexture(&mapplanes[planenum],
			&td, vec3_origin, b->isTerrain);

		/* save the td off in case there is an origin brush and we
		 * have to recalculate the texinfo */
		side_brushtextures[nummapbrushsides] = td;

		nummapbrushsides++;
		b->numsides++;
	} while (1);

	/* get the content for the entire brush */
	b->contentFlags = BrushContents(b);

	/* allow detail brushes to be removed  */
	if (config.nodetail && (b->contentFlags & CONTENTS_DETAIL)) {
		b->numsides = 0;
		return;
	}

	/* allow water brushes to be removed */
	if (config.nowater && (b->contentFlags & CONTENTS_WATER)) {
		b->numsides = 0;
		return;
	}

	/* Knightmare- check if this is an optimized detail brush (has caulk faces)
	 * also exclude trans brushes and terrain */
	if ((b->contentFlags & CONTENTS_DETAIL) && (b->contentFlags & CONTENTS_SOLID) && !b->isTerrain && !b->isGenSurf)
		for (i = 0; i < b->numsides; i++) {
			/* nodraw/caulk faces */
			if ((b->original_sides[i].surfaceFlags & SURF_NODRAW) && !(b->original_sides[i].surfaceFlags & SURF_SKIP)) {
				b->optimizedDetail = qtrue;
				Com_Printf("Entity %i, Brush %i: optimized detail\n", b->entitynum, b->brushnum);
				break;
			}
		}

	/* Knightmare- special handling for terrain brushes */
	if (b->isTerrain || b->isGenSurf || phongShading)
		for (i = 0; i < b->numsides; i++) {
			s2 = &b->original_sides[i];
			/* set ArghRad phong shading value (because EasyGen/GTKGenSurf doesn't allow this) */
			if (!(b->original_sides[i].surfaceFlags & SURF_NODRAW)
			 && (b->original_sides[i].surfaceFlags & SURF_SKIP)) {
				texinfo[s2->texinfo].value = 777 + b->entitynum;	/* lucky 7's */
				texinfo[s2->texinfo].surfaceFlags &= ~SURF_LIGHT;			/* must not be light-emitting */
			}
		}

	/* create windings for sides and bounds for brush */
	MakeBrushWindings(b);

	/* origin brushes are removed, but they set
	 * the rotation origin for the rest of the brushes
	 * in the entity.  After the entire entity is parsed,
	 * the planenums and texinfos will be adjusted for
	 * the origin brush */
	if (b->contentFlags & CONTENTS_ORIGIN) {
		char string[32];
		vec3_t origin;

		if (num_entities == 1) {
			Sys_Error("Entity %i, Brush %i: origin brushes not allowed in world"
				, b->entitynum, b->brushnum);
			return;
		}

		VectorAdd(b->mins, b->maxs, origin);
		VectorScale(origin, 0.5, origin);

		sprintf(string, "%i %i %i", (int)origin[0], (int)origin[1], (int)origin[2]);
		SetKeyValue(&entities[b->entitynum], "origin", string);

		VectorCopy(origin, entities[b->entitynum].origin);

		/* don't keep this brush */
		b->numsides = 0;

		return;
	}

	AddBrushBevels(b);

	nummapbrushes++;
	mapent->numbrushes++;
}

/**
 * @brief Takes all of the brushes from the current entity and adds them to the world's brush list.
 * @note Used by func_group and func_areaportal
 */
static void MoveBrushesToWorld (entity_t *mapent)
{
	int newbrushes, worldbrushes, i;
	mapbrush_t *temp;

	/* this is pretty gross, because the brushes are expected to be */
	/* in linear order for each entity */

	newbrushes = mapent->numbrushes;
	worldbrushes = entities[0].numbrushes;

	temp = malloc(newbrushes*sizeof(mapbrush_t));
	memcpy(temp, mapbrushes + mapent->firstbrush, newbrushes*sizeof(mapbrush_t));

#if	0
	/* let them keep their original brush numbers */
	for (i = 0; i < newbrushes; i++)
		temp[i].entitynum = 0;
#endif

	/* make space to move the brushes (overlapped copy) */
	memmove(mapbrushes + worldbrushes + newbrushes,
		mapbrushes + worldbrushes,
		sizeof(mapbrush_t) * (nummapbrushes - worldbrushes - newbrushes));

	/* copy the new brushes down */
	memcpy(mapbrushes + worldbrushes, temp, sizeof(mapbrush_t) * newbrushes);

	/* fix up indexes */
	entities[0].numbrushes += newbrushes;
	for (i = 1; i < num_entities; i++)
		entities[i].firstbrush += newbrushes;
	free(temp);

	mapent->numbrushes = 0;
}

static qboolean ParseMapEntity (void)
{
	entity_t *mapent;
	epair_t *e;
	side_t *s;
	int i, j, startbrush, startsides;
	vec_t newdist;
	mapbrush_t *b;

	if (!GetToken(qtrue))
		return qfalse;

	if (*token != '{')
		Sys_Error("ParseMapEntity: { not found");

	if (num_entities == MAX_MAP_ENTITIES)
		Sys_Error("num_entities == MAX_MAP_ENTITIES (%i)", num_entities);

	startbrush = nummapbrushes;
	startsides = nummapbrushsides;

	mapent = &entities[num_entities];
	num_entities++;
	memset(mapent, 0, sizeof(*mapent));
	mapent->firstbrush = nummapbrushes;
	mapent->numbrushes = 0;
/*	mapent->portalareas[0] = -1; */
/*	mapent->portalareas[1] = -1; */

	do {
		if (!GetToken(qtrue))
			Sys_Error("ParseMapEntity: EOF without closing brace");
		if (*token == '}')
			break;
		if (*token == '{')
			ParseBrush(mapent);
		else {
			e = ParseEpair();
			e->next = mapent->epairs;
			mapent->epairs = e;
		}
	} while (qtrue);

	GetVectorForKey(mapent, "origin", mapent->origin);

	/* if there was an origin brush, offset all of the planes and texinfo */
	if (mapent->origin[0] || mapent->origin[1] || mapent->origin[2]) {
		for (i = 0; i < mapent->numbrushes; i++) {
			b = &mapbrushes[mapent->firstbrush + i];
			for (j = 0; j < b->numsides; j++) {
				s = &b->original_sides[j];
				newdist = mapplanes[s->planenum].dist -
					DotProduct(mapplanes[s->planenum].normal, mapent->origin);
				s->planenum = FindFloatPlane(mapplanes[s->planenum].normal, newdist);
				s->texinfo = TexinfoForBrushTexture(&mapplanes[s->planenum],
					&side_brushtextures[s-brushsides], mapent->origin, qfalse);
			}
			MakeBrushWindings(b);
		}
	}

	/* group entities are just for editor convenience
	 * toss all brushes into the world entity */
	if (!strcmp("func_group", ValueForKey(mapent, "classname"))) {
		MoveBrushesToWorld(mapent);
		mapent->numbrushes = 0;
		num_entities--;
		return qtrue;
	}

	return qtrue;
}

void LoadMapFile (const char *filename)
{
	int i;

	Sys_FPrintf(SYS_VRB, "--- LoadMapFile ---\n");

	LoadScriptFile(filename);

	nummapbrushsides = 0;
	num_entities = 0;

	while (ParseMapEntity());

	ClearBounds(map_mins, map_maxs);
	for (i = 0; i < entities[0].numbrushes; i++) {
		if (mapbrushes[i].mins[0] > 4096)
			continue;	/* no valid points */
		AddPointToBounds(mapbrushes[i].mins, map_mins, map_maxs);
		AddPointToBounds(mapbrushes[i].maxs, map_mins, map_maxs);
	}

	for (i = 0; i < nummapbrushes; i++)
		mapbrushes[i].finished = qfalse;

	/* save a copy of the brushes */
	memcpy(mapbrushes + nummapbrushes, mapbrushes, sizeof(mapbrush_t)*nummapbrushes);

	Sys_FPrintf(SYS_VRB, "%5i brushes\n", nummapbrushes);
	Sys_FPrintf(SYS_VRB, "%5i clipbrushes\n", c_clipbrushes);
	Sys_FPrintf(SYS_VRB, "%5i total sides\n", nummapbrushsides);
	Sys_FPrintf(SYS_VRB, "%5i boxbevels\n", c_boxbevels);
	Sys_FPrintf(SYS_VRB, "%5i edgebevels\n", c_edgebevels);
	Sys_FPrintf(SYS_VRB, "%5i entities\n", num_entities);
	Sys_FPrintf(SYS_VRB, "%5i planes\n", nummapplanes);
	Sys_FPrintf(SYS_VRB, "%5i areaportals\n", c_areaportals);
	Sys_FPrintf(SYS_VRB, "size: %5.0f,%5.0f,%5.0f to %5.0f,%5.0f,%5.0f\n", map_mins[0],map_mins[1],map_mins[2],
		map_maxs[0],map_maxs[1],map_maxs[2]);
}
