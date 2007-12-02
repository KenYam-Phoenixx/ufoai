/**
 * @file cl_scrn.c
 * @brief Master for refresh, status bar, console, chat, notify, etc
 *
 * Full screen console.
 * Put up loading plaque.
 * Blanked background with loading plaque.
 * Blanked background with menu.
 * Full screen image for quit and victory.
 * End of unit intermissions.
 */

/*
All original materal Copyright (C) 2002-2007 UFO: Alien Invasion team.

Original file from Quake 2 v3.21: quake2-2.31/client/
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

static float scr_con_current;			/* aproaches scr_conlines at scr_conspeed */
static float scr_conlines;				/* 0.0 to 1.0 lines of console to display */

static qboolean scr_initialized = qfalse;/* ready to draw */

static int scr_draw_loading = 0;

vrect_t scr_vrect;				/* position of render window on screen */

static cvar_t *scr_conspeed;
static cvar_t *scr_consize;
static cvar_t *scr_centertime;

static cvar_t *scr_timegraph;
static cvar_t *scr_debuggraph;
static cvar_t *scr_graphheight;
static cvar_t *scr_graphscale;
static cvar_t *scr_graphshift;
static cvar_t *scr_rspeed;

static char cursor_pic[MAX_QPATH];

static void SCR_TimeRefresh_f(void);
static void SCR_DrawString(int x, int y, const char *string, qboolean bitmapFont);

/*
===============================================================================
BAR GRAPHS
===============================================================================
*/

/** @brief Graph color and value */
typedef struct {
	float value;
	int color;		/**< color value */
} graphsamp_t;

#define GRAPH_WIDTH 256

static int currentPos = 0;
static graphsamp_t values[GRAPH_WIDTH];

/**
 * @sa SCR_DrawDebugGraph
 */
void SCR_DebugGraph (float value, int color)
{
	values[currentPos & (GRAPH_WIDTH-1)].value = value;
	values[currentPos & (GRAPH_WIDTH-1)].color = color;
	currentPos++;
}

/**
 * @sa SCR_DebugGraph
 */
static void SCR_DrawDebugGraph (void)
{
	int a, x, y, w, i, h, min, max;
	float v;
	vec4_t color = {1, 0, 0, 0.5};
	static float lasttime = 0;
	static int fps;
	struct tm *now;
	time_t tnow;

	tnow = time((time_t *) 0);
	now = localtime(&tnow);

	h = w = GRAPH_WIDTH;

	x = scr_vrect.width - (w + 2) - 1;
	y = scr_vrect.height - (h + 2) - 1;

	R_DrawFill(x, y, w, h, 0, color);

	for (a = 0; a < GRAPH_WIDTH; a++) {
		i = (currentPos - 1 - a + GRAPH_WIDTH) & (GRAPH_WIDTH-1);
		v = values[i].value;
		Vector4Set(color, values[i].color, values[i].color, values[i].color, 1.0f);
		v = v * scr_graphscale->value;

		if (v < 1)
			v += h * (1 + (int)(-v / h));

		max = (int)v % h + 1;
		min = y + h - max - scr_graphshift->integer;

		/* bind to box! */
		if (min < y + 1)
			min = y + 1;
		if (min > y + h)
			min = y + h;
		if (min + max > y + h)
			max = y + h - max;

		R_DrawFill(x + w - a, min, 1, max, 0, color);
	}

	if (cls.realtime - lasttime > 50) {
		lasttime = cls.realtime;
		fps = (cls.frametime) ? 1 / cls.frametime : 0;
	}
}

/*
===============================================================================
CENTER PRINTING
===============================================================================
*/

static char scr_centerstring[1024];
static int scr_centertime_off;
static int scr_center_lines;

/**
 * @brief Called for important messages that should stay in the center of the screen for a few moments
 */
void SCR_CenterPrint (const char *str)
{
	const char *s;
	char line[64];
	int i, j, l;

	strncpy(scr_centerstring, str, sizeof(scr_centerstring) - 1);
	scr_centertime_off = scr_centertime->integer;

	/* count the number of lines for centering */
	scr_center_lines = 1;
	s = str;
	while (*s) {
		if (*s == '\n')
			scr_center_lines++;
		s++;
	}

	s = str;
	do {
		/* scan the width of the line */
		for (l = 0; l < 40; l++)
			if (s[l] == '\n' || !s[l])
				break;
		for (i = 0; i < (40 - l) / 2; i++)
			line[i] = ' ';

		for (j = 0; j < l; j++) {
			line[i++] = s[j];
		}

		line[i] = '\n';
		line[i + 1] = 0;

		Com_Printf("%s", line);

		while (*s && *s != '\n')
			s++;

		if (!*s)
			break;
		s++;					/* skip the \n */
	} while (1);
	Con_ClearNotify();
}

/**
 * @brief Draws the center string parsed from svc_centerprint events
 * @sa SCR_CheckDrawCenterString
 * @sa SCR_CenterPrint
 */
static void SCR_DrawCenterString (void)
{
	char *start;
	int l;
	int j;
	int x, y;
	int remaining;

	/* the finale prints the characters one at a time */
	remaining = 9999;

	start = scr_centerstring;

	if (scr_center_lines <= 4)
		y = viddef.height * 0.35;
	else
		y = 48;

	do {
		/* scan the width of the line */
		for (l = 0; l < 40; l++)
			if (start[l] == '\n' || !start[l])
				break;
		x = (viddef.width - l * 8) / 2;
		for (j = 0; j < l; j++, x += 8) {
			R_DrawChar(x, y, start[j]);
			if (!remaining--)
				return;
		}

		y += 8;

		while (*start && *start != '\n')
			start++;

		if (!*start)
			break;
		start++;				/* skip the \n */
	} while (1);
}

/**
 * @brief Checks whether center string is outdated or should still be shown
 * @sa SCR_DrawCenterString
 * @sa PF_centerprintf
 * @sa svc_centerprint
 */
static void SCR_CheckDrawCenterString (void)
{
	scr_centertime_off -= cls.frametime;

	if (scr_centertime_off <= 0)
		return;

	SCR_DrawCenterString();
}

/**
 * @sa CL_Init
 */
void SCR_Init (void)
{
	scr_conspeed = Cvar_Get("scr_conspeed", "3", 0, NULL);
	scr_consize = Cvar_Get("scr_consize", "1.0", 0, NULL);
	scr_centertime = Cvar_Get("scr_centertime", "2.5", 0, NULL);
	scr_timegraph = Cvar_Get("timegraph", "0", 0, NULL);
	scr_debuggraph = Cvar_Get("debuggraph", "0", 0, NULL);
	scr_graphheight = Cvar_Get("graphheight", "32", 0, NULL);
	scr_graphscale = Cvar_Get("graphscale", "1", 0, NULL);
	scr_graphshift = Cvar_Get("graphshift", "0", 0, NULL);
	scr_rspeed = Cvar_Get("r_speeds", "0", 0, NULL);

	/* register our commands */
	Cmd_AddCommand("timerefresh", SCR_TimeRefresh_f, "Run a benchmark");

	SCR_TouchPics();

	scr_initialized = qtrue;
}


/**
 * @sa SCR_DrawLoading
 */
static void SCR_DrawLoadingBar (int x, int y, int w, int h, int percent)
{
	static vec4_t color = {0.3f, 0.3f, 0.3f, 0.7f};
	static vec4_t color_bar = {0.8f, 0.8f, 0.8f, 0.7f};

	R_DrawFill(x, y, w, h, ALIGN_UL, color);

	if (percent != 0)
		R_DrawFill((int)(x+(h*0.2)), (int)(y+(h*0.2)), (int)((w-(h*0.4))*percent*0.01), (int)(h*0.6), ALIGN_UL, color_bar);
}

/**
 * @brief Precache and loading screen at startup
 * @sa CL_InitAfter
 * @param[in] string Draw the loading string - if the scripts are not parsed, this is
 * not possible, so use qfalse for very early calls
 */
void SCR_DrawPrecacheScreen (qboolean string)
{
	R_BeginFrame();
	R_DrawNormPic(0, 0, VID_NORM_WIDTH, VID_NORM_HEIGHT, 0, 0, 0, 0, ALIGN_UL, qfalse, "loading");
	if (string) {
		/* Not used with gettext because it would make removing it too easy. */
		R_FontDrawString("f_menubig", ALIGN_UC,
			(int)(VID_NORM_WIDTH / 2),
			30,
			0, 1, VID_NORM_WIDTH, VID_NORM_HEIGHT, 50, "Download this game for free at http://ufoai.sf.net", 0, 0, NULL, qfalse);
	}
	SCR_DrawLoadingBar((int)(VID_NORM_WIDTH / 2) - 300, VID_NORM_HEIGHT - 30, 600, 20, (int)cls.loadingPercent);
	R_EndFrame();
}

/**
 * @brief Updates needed cvar for loading screens
 */
const char* SCR_SetLoadingBackground (const char *mapString)
{
	char loadingPic[MAX_QPATH];
	char tmpPicName[MAX_VAR];
	const char *mapname;
	size_t len;

	if (!mapString || Com_ServerState())
		mapname = Cvar_VariableString("mapname");
	else {
		mapname = mapString;
		Cvar_Set("mapname", mapString);
	}

	if (!*mapname)
		return NULL;
	else if (*mapname != '+') {
		Q_strncpyz(tmpPicName, mapname, sizeof(tmpPicName));
		len = strlen(tmpPicName);
		/* strip of the day and night letter */
		if (tmpPicName[len - 1] == 'n' || tmpPicName[len - 1] == 'd')
			tmpPicName[len - 1] = '\0';
		if (FS_CheckFile(va("pics/maps/loading/%s.jpg", tmpPicName)) > 0)
			Com_sprintf(loadingPic, sizeof(loadingPic), "maps/loading/%s.jpg", tmpPicName);
		else
			Q_strncpyz(loadingPic, "maps/loading/default.jpg", sizeof(loadingPic));
		Cvar_Set("mn_mappicbig", loadingPic);
	} else {
		Q_strncpyz(loadingPic, "maps/loading/default.jpg", sizeof(loadingPic));
		Cvar_Set("mn_mappicbig", loadingPic);
	}
	return Cvar_VariableString("mn_mappicbig");
}

/**
 * @brief Draws the current downloading status
 * @sa SCR_DrawLoadingBar
 * @sa CL_StartHTTPDownload
 * @sa CL_HTTP_Progress
 */
static void SCR_DrawDownloading (void)
{
	const char *dlmsg = va(_("Downloading [%s]"), cls.downloadName);
	R_FontDrawString("f_menubig", ALIGN_UC,
		(int)(VID_NORM_WIDTH / 2),
		(int)(VID_NORM_HEIGHT / 2 - 60),
		(int)(VID_NORM_WIDTH / 2),
		(int)(VID_NORM_HEIGHT / 2 - 60),
		VID_NORM_WIDTH, VID_NORM_HEIGHT, 50, dlmsg, 1, 0, NULL, qfalse);

	SCR_DrawLoadingBar((int)(VID_NORM_WIDTH / 2) - 300, VID_NORM_HEIGHT - 30, 600, 20, (int)cls.downloadPercent);
}

/**
 * @brief Draws the current loading pic of the map from base/pics/maps/loading
 * @sa SCR_DrawLoadingBar
 */
static void SCR_DrawLoading (void)
{
	static const char *loadingPic;
	const vec4_t color = {0.0, 0.7, 0.0, 0.8};
	char *mapmsg;

	if (cls.downloadName[0]) {
		SCR_DrawDownloading();
		return;
	}

	if (!scr_draw_loading) {
		loadingPic = NULL;
		return;
	}

	if (!loadingPic)
		loadingPic = SCR_SetLoadingBackground(selMis ? selMis->def->mapDef->map : cl.configstrings[CS_MAPTITLE]);
	if (!loadingPic)
		return;
	R_DrawNormPic(0, 0, VID_NORM_WIDTH, VID_NORM_HEIGHT, 0, 0, 0, 0, ALIGN_UL, qfalse, loadingPic);
	R_Color(color);

	if (cl.configstrings[CS_TILES][0]) {
		mapmsg = va(_("Loading Map [%s]"), _(cl.configstrings[CS_MAPTITLE]));
		R_FontDrawString("f_menubig", ALIGN_UC,
			(int)(VID_NORM_WIDTH / 2),
			(int)(VID_NORM_HEIGHT / 2 - 60),
			(int)(VID_NORM_WIDTH / 2),
			(int)(VID_NORM_HEIGHT / 2 - 60),
			VID_NORM_WIDTH, VID_NORM_HEIGHT, 50, mapmsg, 1, 0, NULL, qfalse);
	}

	R_FontDrawString("f_menu", ALIGN_UC,
		(int)(VID_NORM_WIDTH / 2),
		(int)(VID_NORM_HEIGHT / 2),
		(int)(VID_NORM_WIDTH / 2),
		(int)(VID_NORM_HEIGHT / 2),
		VID_NORM_WIDTH, VID_NORM_HEIGHT, 50, cls.loadingMessages, 1, 0, NULL, qfalse);

	SCR_DrawLoadingBar((int)(VID_NORM_WIDTH / 2) - 300, VID_NORM_HEIGHT - 30, 600, 20, (int)cls.loadingPercent);
}

static const vec4_t cursorBG = { 0.0f, 0.0f, 0.0f, 0.7f };
/**
 * @brief Draws the 3D-cursor in battlemode and the icons/info next to it.
 */
static void SCR_DrawCursor (void)
{
	int icon_offset_x = 16;	/* Offset of the first icon on the x-axis. */
	int icon_offset_y = 16;	/* Offset of the first icon on the y-axis. */
	int icon_spacing = 2;	/* the space between different icons. */

	if (!cursor->integer || cls.playingCinematic)
		return;

	if (cursor->modified) {
		cursor->modified = qfalse;
		SCR_TouchPics();
	}

	if (!cursor_pic[0])
		return;

	if (mouseSpace != MS_DRAG) {
		if (cls.state == ca_active && cls.team != cl.actTeam)
			R_DrawNormPic(mousePosX, mousePosY, 0, 0, 0, 0, 0, 0, ALIGN_CC, qtrue, "wait");
		else
			R_DrawNormPic(mousePosX, mousePosY, 0, 0, 0, 0, 0, 0, ALIGN_CC, qtrue, cursor_pic);

		if (cls.state == ca_active && mouseSpace == MS_WORLD) {
			if (selActor) {
				/* Display 'crouch' icon if actor is crouched. */
				if (selActor->state & STATE_CROUCHED)
					R_DrawNormPic(mousePosX + icon_offset_x, mousePosY + icon_offset_y, 0, 0, 0, 0, 0, 0, ALIGN_CC, qtrue, "ducked");
				icon_offset_y += 16;	/* Height of 'crouched' icon. */
				icon_offset_y += icon_spacing;

				/* Display 'Reaction shot' icon if actor has it activated. */
				if (selActor->state & STATE_REACTION_ONCE)
					R_DrawNormPic(mousePosX + icon_offset_x, mousePosY + icon_offset_y, 0, 0, 0, 0, 0, 0, ALIGN_CC, qtrue, "reactionfire");
				else if (selActor->state & STATE_REACTION_MANY)
					R_DrawNormPic(mousePosX + icon_offset_x, mousePosY + icon_offset_y, 0, 0, 0, 0, 0, 0, ALIGN_CC, qtrue, "reactionfiremany");
				icon_offset_y += 16;	/* Height of 'reaction fire' icon. ... just in case we add further icons below.*/
				icon_offset_y += icon_spacing;

				/* Display weaponmode (text) heR_ */
				if (menuText[TEXT_MOUSECURSOR_RIGHT] && cl_show_cursor_tooltips->integer)
					SCR_DrawString(mousePosX + icon_offset_x, mousePosY - 16, menuText[TEXT_MOUSECURSOR_RIGHT], qfalse);
			}

			/* playernames */
			if (menuText[TEXT_MOUSECURSOR_PLAYERNAMES] && cl_show_cursor_tooltips->integer) {
				/*@todo: activate this:
				R_DrawFill(mx + icon_offset_x - 1, my - 33, 20, 128, 0, cursorBG);
				*/
				SCR_DrawString(mousePosX + icon_offset_x, mousePosY - 32, menuText[TEXT_MOUSECURSOR_PLAYERNAMES], qfalse);
				MN_MenuTextReset(TEXT_MOUSECURSOR_PLAYERNAMES);
			}
		}
	} else {
		vec3_t scale = { 3.5, 3.5, 3.5 };
		vec3_t org = { mousePosX, mousePosY, -50 };
		vec4_t color = { 0.5, 0.5, 0.5, 1.0 };
		MN_DrawItem(org, dragItem, 0, 0, 0, 0, scale, color);
	}
}


/**
 * @brief Scroll it up or down
 */
void SCR_RunConsole (void)
{
	/* decide on the height of the console */
	if (cls.key_dest == key_console)
		scr_conlines = scr_consize->value;	/* half screen */
	else
		scr_conlines = 0;		/* none visible */

	if (scr_conlines < scr_con_current) {
		scr_con_current -= scr_conspeed->value * cls.frametime;
		if (scr_conlines > scr_con_current)
			scr_con_current = scr_conlines;

	} else if (scr_conlines > scr_con_current) {
		scr_con_current += scr_conspeed->value * cls.frametime;
		if (scr_conlines < scr_con_current)
			scr_con_current = scr_conlines;
	}
}

/**
 * @sa SCR_UpdateScreen
 * @sa Con_DrawConsole
 * @sa Con_DrawNotify
 */
static void SCR_DrawConsole (void)
{
	Con_CheckResize();

	if (!scr_vrect.width || !scr_vrect.height) {
		/* active full screen menu */
		/* draw the console like in game */
		if (scr_con_current)
			Con_DrawConsole(scr_con_current);
		/* allow chat in waiting dialoges */
		if (cls.key_dest == key_message)
			Con_DrawNotify(); /* only draw notify in game */
		return;
	}

#if 0
	if (cls.state == ca_connecting || cls.state == ca_connected) {	/* forced full screen console */
		Con_DrawConsole(1.0);
		return;
	}
#endif

	if (scr_con_current) {
		Con_DrawConsole(scr_con_current);
	} else {
		if ((cls.key_dest == key_game || cls.key_dest == key_message) && cls.state != ca_sequence)
			Con_DrawNotify(); /* only draw notify in game */
	}
}

/**
 * @sa SCR_UpdateScreen
 * @sa SCR_EndLoadingPlaque
 * @sa SCR_DrawLoading
 */
void SCR_BeginLoadingPlaque (void)
{
	if (developer->integer)
		return;

	scr_draw_loading = 1;

	SCR_UpdateScreen();
	cls.disable_screen = cls.realtime;
}

/**
 * @sa SCR_BeginLoadingPlaque
 */
void SCR_EndLoadingPlaque (void)
{
	cls.disable_screen = 0;
	scr_draw_loading = 0;
	SCR_DrawLoading(); /* reset the loadingPic pointer */
	/* clear any lines of console text */
	Con_ClearNotify();
}

static void SCR_TimeRefresh_f (void)
{
	int i;
	int start, stop;
	float time;

	if (cls.state != ca_active)
		return;

	start = Sys_Milliseconds();

	if (Cmd_Argc() == 2) {		/* run without page flipping */
		R_BeginFrame();
		for (i = 0; i < 128; i++) {
			refdef.viewangles[1] = i / 128.0 * 360.0;
			R_RenderFrame();
		}
		R_EndFrame();
	} else {
		for (i = 0; i < 128; i++) {
			refdef.viewangles[1] = i / 128.0 * 360.0;

			R_BeginFrame();
			R_RenderFrame();
			R_EndFrame();
		}
	}

	stop = Sys_Milliseconds();
	time = (stop - start) / 1000.0;
	Com_Printf("%f seconds (%f fps)\n", time, 128 / time);
}

/**
 * @brief Allows rendering code to cache all needed sbar graphics
 */
void SCR_TouchPics (void)
{
	if (cursor->integer) {
		if (cursor->integer > 9 || cursor->integer < 0)
			Cvar_SetValue("cursor", 1);

		R_RegisterPic("wait");
		R_RegisterPic("ducked");
		Com_sprintf(cursor_pic, sizeof(cursor_pic), "cursor%i", cursor->integer);
		if (!R_RegisterPic(cursor_pic)) {
			Com_Printf("SCR_TouchPics: Could not register cursor: %s\n", cursor_pic);
			cursor_pic[0] = 0;
		}
	}
}

/**
 * @sa Font_DrawString
 */
static void SCR_DrawString (int x, int y, const char *string, qboolean bitmapFont)
{
	if (!string || !*string)
		return;

	if (bitmapFont) {
		while (*string) {
			R_DrawChar(x, y, *string);
			x += con_fontWidth->integer;
			string++;
		}
	} else
		R_FontDrawString("f_verysmall", ALIGN_UL, x, y, 0, 0, VID_NORM_WIDTH, VID_NORM_HEIGHT, 12, string, 0, 0, NULL, qfalse);
}

/**
 * @brief This is called every frame, and can also be called explicitly to flush text to the screen
 * @sa MN_DrawMenus
 * @sa V_RenderView
 * @sa SCR_DrawConsole
 * @sa SCR_DrawCursor
 */
void SCR_UpdateScreen (void)
{
	/* if the screen is disabled (loading plaque is up, or vid mode changing)
	 * do nothing at all */
	if (cls.disable_screen) {
		if (cls.realtime - cls.disable_screen > 120000 && cl.refresh_prepped) {
			cls.disable_screen = 0;
			Com_Printf("Loading plaque timed out.\n");
			return;
		}
	}

	/* not initialized yet */
	if (!scr_initialized || !con.initialized)
		return;

	R_BeginFrame();

	if (cls.state == ca_disconnected && !scr_draw_loading)
		SCR_EndLoadingPlaque();

	if (scr_draw_loading)
		SCR_DrawLoading();
	else {
		MN_SetViewRect(MN_ActiveMenu());

		if (cls.playingCinematic) {
			CIN_RunCinematic();
		} else {
			/* draw scene */
			V_RenderView();

			/* draw the menus on top of the render view (for hud and so on) */
			MN_DrawMenus();

			SCR_CheckDrawCenterString();
		}

		SCR_DrawConsole();

		if (cl_fps->integer)
			SCR_DrawString(viddef.width - con_fontWidth->integer * 10, 4, va("fps: %3.1f", cls.framerate), qtrue);
		if (cls.state == ca_active && scr_rspeed->integer)
			SCR_DrawString(viddef.width - con_fontWidth->integer * 30, 20, va("brushes: %6i alias: %6i\n", c_brush_polys, c_alias_polys), qtrue);

		if (scr_timegraph->integer)
			SCR_DebugGraph(cls.frametime * 300, 0);

		if (scr_debuggraph->integer || scr_timegraph->integer)
			SCR_DrawDebugGraph();

		if (cls.state != ca_sequence)
			SCR_DrawCursor();
	}

	R_EndFrame();
}
