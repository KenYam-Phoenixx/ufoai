/**
 * @file cl_vid.c
 * @brief Shared client functions for windowed and fullscreen graphics interface module.
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

#include "client.h"

viddef_t viddef;	/* global video state; used by other modules */

cvar_t *vid_fullscreen;
cvar_t *vid_mode;
cvar_t *vid_grabmouse;
cvar_t *vid_gamma;
cvar_t *vid_xpos;
cvar_t *vid_ypos;
static cvar_t *vid_height;
static cvar_t *vid_width;

/**
 * @brief All possible video modes
 */
const vidmode_t vid_modes[] =
{
	{ 320, 240,   0 },
	{ 400, 300,   1 },
	{ 512, 384,   2 },
	{ 640, 480,   3 },
	{ 800, 600,   4 },
	{ 960, 720,   5 },
	{ 1024, 768,  6 },
	{ 1152, 864,  7 },
	{ 1280, 1024, 8 },
	{ 1600, 1200, 9 },
	{ 2048, 1536, 10 },
	{ 1024,  480, 11 }, /* Sony VAIO Pocketbook */
	{ 1152,  768, 12 }, /* Apple TiBook */
	{ 1280,  854, 13 }, /* Apple TiBook */
	{ 640,  400, 14 }, /* generic 16:10 widescreen*/
	{ 800,  500, 15 }, /* as found modern */
	{ 1024,  640, 16 }, /* notebooks    */
	{ 1280,  800, 17 },
	{ 1680, 1050, 18 },
	{ 1920, 1200, 19 },
	{ 1400, 1050, 20 }, /* samsung x20 */
	{ 1440, 900, 21 }
};

/**
 * @brief Returns the amount of available video modes
 */
int VID_GetModeNums (void)
{
	return (sizeof(vid_modes) / sizeof(vidmode_t));
}

qboolean VID_GetModeInfo (void)
{
	if (viddef.mode >= VID_GetModeNums())
		return qfalse;
	else if (viddef.mode < 0) {
		viddef.width = vid_width->integer;
		viddef.height = vid_height->integer;
	} else {
		viddef.width  = vid_modes[viddef.mode].width;
		viddef.height = vid_modes[viddef.mode].height;
	}

	return qtrue;
}

/**
 * @brief Perform a renderer restart
 */
void VID_Restart_f (void)
{
	cl.refresh_prepped = qfalse;

	R_Shutdown();
	R_Init();
	CL_InitFonts();

	CL_LoadMedia();
}

/**
 * @sa R_Shutdown
 */
void VID_Init (void)
{
	vid_fullscreen = Cvar_Get("vid_fullscreen", "0", CVAR_ARCHIVE, "Run the game in fullscreen mode");
	vid_mode = Cvar_Get("vid_mode", "6", CVAR_ARCHIVE, "The video mode - set to -1 and use vid_width and vid_height to use a custom resolution");
	vid_grabmouse = Cvar_Get("vid_grabmouse", "0", CVAR_ARCHIVE, "Grab the mouse in the game window - open the console to switch back to your desktop via Alt+Tab");
	vid_gamma = Cvar_Get("vid_gamma", "1", CVAR_ARCHIVE, NULL);
	vid_height = Cvar_Get("vid_height", "768", CVAR_ARCHIVE, "Custom video height - set vid_mode to -1 to use this");
	vid_width = Cvar_Get("vid_width", "1024", CVAR_ARCHIVE, "Custom video width - set vid_mode to -1 to use this");
	vid_xpos = Cvar_Get("vid_xpos", "3", CVAR_ARCHIVE, "Position of the ufo window");
	vid_ypos = Cvar_Get("vid_ypos", "22", CVAR_ARCHIVE, "Position of the ufo window");

	Cmd_AddCommand("vid_restart", VID_Restart_f, "Restart the renderer - or change the resolution");

	/* memory pools */
	vid_genericPool = Mem_CreatePool("Vid: Generic");
	vid_imagePool = Mem_CreatePool("Vid: Image system");
	vid_lightPool = Mem_CreatePool("Vid: Light system");
	vid_modelPool = Mem_CreatePool("Vid: Model system");

	/* Start the graphics mode */
	R_Init();
}


void *VID_TagAlloc (struct memPool_s *pool, int size, int tagNum)
{
	if (tagNum < 0)
		tagNum *= -1;

	assert(pool);
	return _Mem_Alloc(size, qtrue, pool, tagNum, "RENDERER", 0);
}

void VID_MemFree (void *ptr)
{
	_Mem_Free(ptr, "RENDERER", -1);
}


void VID_FreeTags (struct memPool_s *pool, int tagNum)
{
	assert(pool);
	if (tagNum < 0)
		tagNum *= -1;

	assert(pool);
	_Mem_FreeTag(pool, tagNum, "RENDERER", 0);
}
