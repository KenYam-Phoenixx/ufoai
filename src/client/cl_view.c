/**
 * @file cl_view.c
 * @brief Player rendering positioning.
 */

/*
All original materal Copyright (C) 2002-2007 UFO: Alien Invasion team.

15/06/06, Eddy Cullen (ScreamingWithNoSound):
	Reformatted to agreed style.
	Added doxygen file comment.
	Updated copyright notice.

Original file from Quake 2 v3.21: quake2-2.31/client/cl_view.c
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

#include "client.h"

/* global vars */
cvar_t *cursor;

cvar_t *map_dropship;
vec3_t map_dropship_coord;
int map_maxlevel;
int map_maxlevel_base = 0;
sun_t map_sun;

/* static vars */
static cvar_t *cl_drawgrid;
static cvar_t *cl_stats;

static int r_numdlights;
static dlight_t r_dlights[MAX_DLIGHTS];

static int r_numentities;
static entity_t r_entities[MAX_ENTITIES];

static lightstyle_t r_lightstyles[MAX_LIGHTSTYLES];

static dlight_t map_lights[MAX_MAP_LIGHTS];
static int map_numlights;

static float map_fog;
static vec4_t map_fogColor;

/**
 * @brief
 * @sa V_RenderView
 */
void V_ClearScene (void)
{
	r_numdlights = 0;
	r_numentities = 0;
}


/**
 * @brief
 * @sa
 */
entity_t *V_GetEntity (void)
{
	return r_entities + r_numentities;
}


/**
 * @brief
 * @param[in] ent
 * @sa
 */
void V_AddEntity (entity_t * ent)
{
	if (r_numentities >= MAX_ENTITIES)
		return;

	r_entities[r_numentities++] = *ent;
}


/**
 * @brief
 * @param
 * @sa
 */
void V_AddLight (vec3_t org, float intensity, float r, float g, float b)
{
	dlight_t *dl;

	if (r_numdlights >= MAX_DLIGHTS)
		return;
	dl = &r_dlights[r_numdlights++];
	VectorCopy(org, dl->origin);
	dl->intensity = intensity;
	dl->color[0] = r;
	dl->color[1] = g;
	dl->color[2] = b;
}


/**
 * @brief
 * @param
 * @sa CL_AddLightStyles
 */
void V_AddLightStyle (int style, float r, float g, float b)
{
	lightstyle_t *ls;

	if (style < 0 || style > MAX_LIGHTSTYLES)
		Com_Error(ERR_DROP, "Bad light style %i", style);
	ls = &r_lightstyles[style];

	ls->white = r + g + b;
	ls->rgb[0] = r;
	ls->rgb[1] = g;
	ls->rgb[2] = b;
}

/**
 * @brief
 * @param
 * @sa
 */
void CL_ParseEntitystring (char *es)
{
	char *strstart;
	char *com_token;
	char keyname[256];

	char classname[MAX_VAR];
	char animname[MAX_QPATH];
	char model[MAX_VAR];
	char particle[MAX_VAR];
	float light;
	vec3_t color, ambient, origin, angles, lightangles, dropship_coord;
	vec2_t wait;
	int spawnflags;
	int maxlevel = 8;
	int maxmultiplayerteams = 2;
	int entnum;
	int skin;
	int frame;

	/* dropship default values */
	/* -1.0f means - don't use it */

	/*
	TODO: Implement me: use cvar map_dropship to get the current used dropship
	this cvar is set at mission start and inited with craft_dropship
	(e.g. for multiplayer missions)

	This assemble step should be done in sv_init SV_Map (we need to check whether
	this is syncs over network)
	*/
	VectorSet(dropship_coord, -1.0f, -1.0f, -1.0f);

	VectorSet(map_fogColor, 0.5f, 0.5f, 0.5f);
	map_fogColor[3] = 1.0f;
	map_maxlevel = 8;
	if (map_maxlevel_base >= 1) {
		map_maxlevel = maxlevel = map_maxlevel_base;
	}
	map_numlights = 0;
	map_fog = 0.0;
	entnum = 0;
	numLMs = 0;
	numMPs = 0;

	/* parse ents */
	while (1) {
		/* initialize */
		VectorCopy(vec3_origin, ambient);
		color[0] = color[1] = color[2] = 1.0f;
		VectorCopy(vec3_origin, origin);
		VectorCopy(vec3_origin, angles);
		wait[0] = wait[1] = 0;
		spawnflags = 0;
		light = 0;
		frame = 0;
		animname[0] = 0;
		model[0] = 0;
		particle[0] = 0;
		skin = 0;

		/* parse the opening brace */
		com_token = COM_Parse(&es);
		if (!es)
			break;
		if (com_token[0] != '{')
			Com_Error(ERR_DROP, "CL_ParseEntitystring: found %s when expecting {", com_token);

		/* memorize the start */
		strstart = es;

		/* go through all the dictionary pairs */
		while (1) {
			/* parse key */
			com_token = COM_Parse(&es);
			if (com_token[0] == '}')
				break;
			if (!es)
				Com_Error(ERR_DROP, "CL_ParseEntitystring: EOF without closing brace");

			Q_strncpyz(keyname, com_token, sizeof(keyname));

			/* parse value */
			com_token = COM_Parse(&es);
			if (!es)
				Com_Error(ERR_DROP, "CL_ParseEntitystring: EOF without closing brace");

			if (com_token[0] == '}')
				Com_Error(ERR_DROP, "CL_ParseEntitystring: closing brace without data");

			/* filter interesting keys */
			if (!Q_strcmp(keyname, "classname"))
				Q_strncpyz(classname, com_token, sizeof(classname));

			if (!Q_strcmp(keyname, "model"))
				Q_strncpyz(model, com_token, sizeof(model));

			if (!Q_strcmp(keyname, "frame"))
				frame = atoi(com_token);

			if (!Q_strcmp(keyname, "anim"))
				Q_strncpyz(animname, com_token, sizeof(animname));

			if (!Q_strcmp(keyname, "particle"))
				Q_strncpyz(particle, com_token, sizeof(particle));

			if (!Q_strcmp(keyname, "_color") || !Q_strcmp(keyname, "lightcolor"))
				sscanf(com_token, "%f %f %f", &(color[0]), &(color[1]), &(color[2]));

			if (!Q_strcmp(keyname, "origin"))
				sscanf(com_token, "%f %f %f", &(origin[0]), &(origin[1]), &(origin[2]));

			if (!Q_strcmp(keyname, "ambient") || !Q_strcmp(keyname, "lightambient"))
				sscanf(com_token, "%f %f %f", &(ambient[0]), &(ambient[1]), &(ambient[2]));

			if (!Q_strcmp(keyname, "angles") && !angles[YAW])
				sscanf(com_token, "%f %f %f", &(angles[PITCH]), &(angles[YAW]), &(angles[ROLL]));

			if (!Q_strcmp(keyname, "angle") && !angles[YAW])
				angles[YAW] = atof(com_token);

			if (!Q_strcmp(keyname, "wait"))
				sscanf(com_token, "%f %f", &(wait[0]), &(wait[1]));

			if (!Q_strcmp(keyname, "light"))
				light = atof(com_token);

			if (!Q_strcmp(keyname, "lightangles"))
				sscanf(com_token, "%f %f %f", &(lightangles[0]), &(lightangles[1]), &(lightangles[2]));

			if (!Q_strcmp(keyname, "spawnflags"))
				spawnflags = atoi(com_token);

			if (!Q_strcmp(keyname, "maxlevel"))
				maxlevel = atoi(com_token);

			if (!Q_strcmp(keyname, "maxteams"))
				maxmultiplayerteams = atoi(com_token);

			if (!Q_strcmp(keyname, "dropship_coord"))
				sscanf(com_token, "%f %f %f", &(dropship_coord[0]), &(dropship_coord[1]), &(dropship_coord[2]));

			if (!Q_strcmp(keyname, "fog"))
				map_fog = atof(com_token);

			if (!Q_strcmp(keyname, "fogcolor"))
				sscanf(com_token, "%f %f %f", &(map_fogColor[0]), &(map_fogColor[1]), &(map_fogColor[2]));

			if (!Q_strcmp(keyname, "skin"))
				skin = atoi(com_token);
		}

		/* analyze values */
		if (!Q_strcmp(classname, "worldspawn")) {
			/* init sun */
			angles[YAW] *= torad;
			angles[PITCH] *= torad;
			map_sun.dir[0] = cos(angles[YAW]) * sin(angles[PITCH]);
			map_sun.dir[1] = sin(angles[YAW]) * sin(angles[PITCH]);
			map_sun.dir[2] = cos(angles[PITCH]);

			VectorNormalize(color);
			VectorScale(color, light / 100, map_sun.color);
			map_sun.color[3] = 1.0;

			/* init ambient */
			VectorScale(ambient, 1.4, map_sun.ambient);
			map_sun.ambient[3] = 1.0;

			/* maximum level */
			map_maxlevel = maxlevel;
			VectorCopy(map_dropship_coord, dropship_coord);

			if (!ccs.singleplayer && (teamnum->value > maxmultiplayerteams
									|| teamnum->value <= TEAM_CIVILIAN)) {
				Com_Printf("The selected team is not useable. "
					"The map doesn't support %.0f teams but only %i teams\n",
					teamnum->value, maxmultiplayerteams);
				Cvar_SetValue("teamnum", DEFAULT_TEAMNUM);
				Com_Printf("Set teamnum to %.0f\n", teamnum->value);
			}
		} else if (!Q_strcmp(classname, "light") && light) {
			dlight_t *newlight;

			/* add light to list */
			if (map_numlights >= MAX_MAP_LIGHTS)
				Com_Error(ERR_DROP, "Too many lights...\n");

			newlight = &(map_lights[map_numlights++]);
			VectorCopy(origin, newlight->origin);
			VectorNormalize2(color, newlight->color);
			newlight->intensity = light;
		} else if (!Q_strcmp(classname, "misc_model")) {
			lm_t *lm;

			if (!model[0]) {
				Com_Printf("misc_model without \"model\" specified\n");
				continue;
			}

			/* add it */
			lm = CL_AddLocalModel(model, particle, origin, angles, entnum, (spawnflags & 0xFF));

			/* get light parameters */
			if (lm) {
				if (light != 0.0) {
					lm->flags |= LMF_LIGHTFIXED;
					VectorCopy(color, lm->lightcolor);
					VectorCopy(ambient, lm->lightambient);
					lm->lightcolor[3] = light;
					lm->lightambient[3] = 1.0;

					lightangles[YAW] *= torad;
					lightangles[PITCH] *= torad;
					lm->lightorigin[0] = cos(lightangles[YAW]) * sin(lightangles[PITCH]);
					lm->lightorigin[1] = sin(lightangles[YAW]) * sin(lightangles[PITCH]);
					lm->lightorigin[2] = cos(lightangles[PITCH]);
					lm->lightorigin[3] = 0.0;
				}
				lm->skin = skin;
				lm->frame = frame;
				if (!lm->frame)
					Q_strncpyz(lm->animname, animname, sizeof(lm->animname));
				else
					Com_Printf("Warning: Model has frame and anim parameters - using frame (no animation)\n");
			}
		} else if (!Q_strcmp(classname, "misc_particle")) {
			CL_AddMapParticle(particle, origin, wait, strstart, (spawnflags & 0xFF));
		} else if (!Q_strcmp(classname, "misc_sound")) {
			Com_Printf("implement misc_sound\n");
		} else if (!Q_strcmp(classname, "misc_rope")) {
			Com_Printf("implement misc_rope\n");
		} else if (!Q_strcmp(classname, "misc_decal")) {
			Com_Printf("implement misc_decal\n");
		} else if (!Q_strcmp(classname, "func_breakable")
			|| !Q_strcmp(classname, "func_door")) {
			VectorClear(angles);
			CL_AddLocalModel(model, particle, origin, angles, entnum, (spawnflags & 0xFF));
			Com_DPrintf("Add %i as local model (%s)\n", entnum, classname);
		}

		entnum++;
	}
}


/**
 * @brief Call before entering a new level, or after changing dlls
 */
void CL_PrepRefresh (void)
{
	le_t *le;
	int i, max;
	char name[37];
	char *str;

	/* this is needed to get shaders/image filters in game */
	/* the renderer needs to know them before the textures get loaded */
	V_UpdateRefDef();
	/* set ref def - do this even in non 3d mode - we need shaders at loading time */
	re.SetRefDef(&cl.refdef);

	if (!cl.configstrings[CS_TILES][0])
		return;					/* no map loaded */

	loadingMessage = qtrue;
	Com_sprintf(loadingMessages, sizeof(loadingMessages), _("loading %s"), _(cl.configstrings[CS_MAPTITLE]));
	loadingPercent = 0;

	CL_ResetWeaponButtons();

	SCR_AddDirtyPoint(0, 0);
	SCR_AddDirtyPoint(viddef.width - 1, viddef.height - 1);

	/* register models, pics, and skins */
	Com_Printf("Map: %s\n", cl.configstrings[CS_NAME]);
	SCR_UpdateScreen();
	re.BeginRegistration(cl.configstrings[CS_TILES], cl.configstrings[CS_POSITIONS]);
	CL_ParseEntitystring(map_entitystring);
	Com_Printf("                                     \r");

	Com_sprintf(loadingMessages, sizeof(loadingMessages), _("loading models..."));
	loadingPercent += 10.0f;
	/* precache status bar pics */
	Com_Printf("pics\n");
	SCR_UpdateScreen();
	SCR_TouchPics();
	Com_Printf("                                     \r");

	CL_RegisterLocalModels();
	CL_ParticleRegisterArt();

	for (i = 1, max = 0; i < MAX_MODELS && cl.configstrings[CS_MODELS+i][0]; i++)
		max++;

	max += csi.numODs;

	for (i = 1; i < MAX_MODELS && cl.configstrings[CS_MODELS + i][0]; i++) {
		Q_strncpyz(name, cl.configstrings[CS_MODELS + i], sizeof(name));
		if (name[0] != '*') {
			Com_Printf("%s\r", name);
			Com_sprintf(loadingMessages, sizeof(loadingMessages),
				_("loading %s"), (strlen(name) > 40)? &name[strlen(name)-40]: name);
		}
		SCR_UpdateScreen();
		Sys_SendKeyEvents();	/* pump message loop */
		cl.model_draw[i] = re.RegisterModel(cl.configstrings[CS_MODELS + i]);
		if (name[0] == '*')
			cl.model_clip[i] = CM_InlineModel(cl.configstrings[CS_MODELS + i]);
		else
			cl.model_clip[i] = NULL;
		if (name[0] != '*')
			Com_Printf("                                     \r");

		loadingPercent += 80.0f/(float)max;
	}

	/* update le model references */
	for (i = 0, le = LEs; i < numLEs; i++, le++)
		if (le->inuse) {
			if (le->modelnum1)
				le->model1 = cl.model_draw[le->modelnum1];
			if (le->modelnum2)
				le->model2 = cl.model_draw[le->modelnum2];
		}

	/* load weapons stuff */
	for (i = 0; i < csi.numODs; i++) {
		str = csi.ods[i].model;
		SCR_UpdateScreen();
		cl.model_weapons[i] = re.RegisterModel(str);
		Com_sprintf(loadingMessages, sizeof(loadingMessages),
			_("loading %s"), (strlen(str) > 40)? &str[strlen(str)-40]: str);
		loadingPercent += 80.0f/(float)max;
	}

	/* images */
	Com_Printf("images\n");
	for (i = 1, max = 0; i < MAX_IMAGES && cl.configstrings[CS_IMAGES + i][0]; i++)
		max++;

	for (i = 1; i < MAX_IMAGES && cl.configstrings[CS_IMAGES + i][0]; i++) {
		str = cl.configstrings[CS_IMAGES + i];
		Com_sprintf(loadingMessages, sizeof(loadingMessages),
			_("loading %s"), (strlen(str) > 40)? &str[strlen(str)-40]: str);
		SCR_UpdateScreen();
		cl.image_precache[i] = re.RegisterPic(str);
		Sys_SendKeyEvents();	/* pump message loop */
		loadingPercent += 10.0f/(float)max;
	}

	loadingPercent = 100.0f;
	SCR_UpdateScreen();

	/* the renderer can now free unneeded stuff */
	re.EndRegistration();

	SCR_EndLoadingPlaque();

	SCR_UpdateScreen();
	cl.refresh_prepped = qtrue;
	cl.force_refdef = qtrue;	/* make sure we have a valid refdef */
}

/**
 * @brief Calculates cl.refdef's FOV_X.
 * Should generally be called after any changes are made to the zoom level (via cl.cam.zoom)
 * @sa
 */
void CalcFovX (void)
{
	if (cl_isometric->value) {
		float zoom =  3.6*(cl.cam.zoom - cl_camzoommin->value) + 0.3*cl_camzoommin->value;
		cl.refdef.fov_x = max(min(FOV / zoom, 140.0), 1.0);
	} else {
		cl.refdef.fov_x = max(min(FOV / cl.cam.zoom, 95.0), 55.0);
	}
}

/**
 * @brief
 * @param
 * @sa
 */
static void CalcFovY (float width, float height)
{
	float x;

	x = width / tan(cl.refdef.fov_x / 360 * M_PI);
	cl.refdef.fov_y = atan(height / x) * 360 / M_PI;
}

/**
 * @brief
 * @param
 * @sa
 */
void CL_CalcRefdef (void)
{
	VectorCopy(cl.cam.camorg, cl.refdef.vieworg);
	VectorCopy(cl.cam.angles, cl.refdef.viewangles);

	VectorSet(cl.refdef.blend, 0.0, 0.0, 0.0);

	/* set dependant variables */
	CalcFovY(scr_vrect.width, scr_vrect.height);
	cl.refdef.x = scr_vrect.x;
	cl.refdef.y = scr_vrect.y;
	cl.refdef.width = scr_vrect.width;
	cl.refdef.height = scr_vrect.height;
	cl.refdef.time = cl.time * 0.001;
	cl.refdef.areabits = 0;
}

/**
 * @brief Function to draw the grid and the forbidden places
 *
 * TODO: Implement and extend this function
 */
static void CL_DrawGrid (void)
{
	int i;

	/* only in 3d view */
	if (!CL_OnBattlescape())
		return;

	Com_DPrintf("CL_DrawGrid: %i\n", fb_length);
	for (i = 0; i < fb_length; i++) {
	}
}

/**
 * @brief Updates the cl.refdef
 */
void V_UpdateRefDef (void)
{
	/* setup refdef */
	cl.refdef.worldlevel = cl_worldlevel->value;
	cl.refdef.num_entities = r_numentities;
	cl.refdef.entities = r_entities;
	cl.refdef.num_shaders = r_numshaders;
	cl.refdef.shaders = r_shaders;
	cl.refdef.num_dlights = r_numdlights;
	cl.refdef.dlights = r_dlights;
	cl.refdef.lightstyles = r_lightstyles;
	cl.refdef.fog = map_fog;
	cl.refdef.fogColor = map_fogColor;

	cl.refdef.num_ptls = numPtls;
	cl.refdef.ptls = ptl;
	cl.refdef.ptl_art = ptlArt;

	cl.refdef.sun = &map_sun;
	if (cls.state == ca_sequence || cls.state == ca_ptledit)
		cl.refdef.num_lights = 0;
	else {
		cl.refdef.ll = map_lights;
		cl.refdef.num_lights = map_numlights;
	}
}

/**
 * @brief
 * @param stereo_separation
 * @sa SCR_UpdateScreen
 */
void V_RenderView (float stereo_separation)
{
	if (cls.state != ca_active && cls.state != ca_sequence && cls.state != ca_ptledit)
		return;

	if (!scr_vrect.width || !scr_vrect.height)
		return;

	/* still loading */
	if (!cl.refresh_prepped)
		return;

	if (cl_timedemo->value) {
		if (!cl.timedemo_start)
			cl.timedemo_start = Sys_Milliseconds();
		cl.timedemo_frames++;
	}

	V_ClearScene();
/*	CL_RunLightStyles (); */

	if (cls.state == ca_sequence) {
		CL_SequenceRender();
		cl.refdef.rdflags |= RDF_NOWORLDMODEL;
	} else if (cls.state == ca_ptledit) {
		PE_RenderParticles();
		cl.refdef.rdflags |= RDF_NOWORLDMODEL;
	} else {
		LM_AddToScene();
		LE_AddToScene();
		CL_AddTargeting();
		CL_AddLightStyles();
	}

	/* update ref def - do this even in non 3d mode - we need shaders at loading time */
	V_UpdateRefDef();
	CL_CalcRefdef();

	/* offset vieworg appropriately if we're doing stereo separation */
	if (stereo_separation != 0) {
		vec3_t tmp;

		VectorScale(cl.cam.axis[1], stereo_separation, tmp);
		VectorAdd(cl.refdef.vieworg, tmp, cl.refdef.vieworg);
	}

	/* render the frame */
	re.RenderFrame(&cl.refdef);
	if (cl_stats->value)
		Com_Printf("ent:%i  lt:%i\n", r_numentities, r_numdlights);
	if (log_stats->value && (log_stats_file != 0))
		fprintf(log_stats_file, "%i,%i,", r_numentities, r_numdlights);
	if (cl_drawgrid->value)
		CL_DrawGrid();

	SCR_AddDirtyPoint(scr_vrect.x, scr_vrect.y);
	SCR_AddDirtyPoint(scr_vrect.x + scr_vrect.width - 1, scr_vrect.y + scr_vrect.height - 1);

	if (cls.state == ca_sequence)
		CL_Sequence2D();
}


/**
 * @brief
 * @param pos
 * @sa
 */
void V_CenterView(pos3_t pos)
{
	vec3_t vec;

	PosToVec(pos, vec);
	VectorCopy(vec, cl.cam.reforg);
	Cvar_SetValue("cl_worldlevel", pos[2]);
}


/**
 * @brief Just prints the vieworg vector to console
 */
void V_Viewpos_f(void)
{
	Com_Printf("(%i %i %i) : %i\n", (int) cl.refdef.vieworg[0], (int) cl.refdef.vieworg[1], (int) cl.refdef.vieworg[2], (int) cl.refdef.viewangles[YAW]);
}

/**
 * @brief
 * @param
 * @sa CL_Init
 */
void V_Init(void)
{
	Cmd_AddCommand("viewpos", V_Viewpos_f, NULL);
	Cmd_AddCommand("shaderlist", CL_ShaderList_f, NULL);

	cursor = Cvar_Get("cursor", "1", CVAR_ARCHIVE, NULL);

	cl_drawgrid = Cvar_Get("drawgrid", "0", 0, NULL);

	cl_stats = Cvar_Get("cl_stats", "0", 0, NULL);
}
