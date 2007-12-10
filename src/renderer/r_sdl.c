/**
 * @file r_sdl.c
 * @brief This file contains SDL specific stuff having to do with the OpenGL refresh
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

#include "r_local.h"

SDL_Surface *r_surface;

#ifndef _WIN32
static void R_SetSDLIcon (void)
{
#include "../ports/linux/ufoicon.xbm"
	SDL_Surface *icon;
	SDL_Color color;
	Uint8 *ptr;
	unsigned int i, mask;

	icon = SDL_CreateRGBSurface(SDL_SWSURFACE, ufoicon_width, ufoicon_height, 8, 0, 0, 0, 0);
	if (icon == NULL)
		return;
	SDL_SetColorKey(icon, SDL_SRCCOLORKEY, 0);

	color.r = color.g = color.b = 255;
	SDL_SetColors(icon, &color, 0, 1); /* just in case */
	color.r = color.b = 0;
	color.g = 16;
	SDL_SetColors(icon, &color, 1, 1);

	ptr = (Uint8 *)icon->pixels;
	for (i = 0; i < sizeof(ufoicon_bits); i++) {
		for (mask = 1; mask != 0x100; mask <<= 1) {
			*ptr = (ufoicon_bits[i] & mask) ? 1 : 0;
			ptr++;
		}
	}

	SDL_WM_SetIcon(icon, NULL);
	SDL_FreeSurface(icon);
}
#endif

qboolean Rimp_Init (void)
{
	SDL_version version;
	int attrValue;
	const SDL_VideoInfo* info;

	r_surface = NULL;

	Com_Printf("\n------- video initialization -------\n");

	if (*r_driver->string) {
		Com_Printf("using driver: %s\n", r_driver->string);
		SDL_GL_LoadLibrary(r_driver->string);
	}

	if (SDL_WasInit(SDL_INIT_AUDIO|SDL_INIT_VIDEO) == 0) {
		if (SDL_Init(SDL_INIT_VIDEO) < 0) {
			Sys_Error("Video SDL_Init failed: %s\n", SDL_GetError());
			return qfalse;
		}
	} else if (SDL_WasInit(SDL_INIT_VIDEO) == 0) {
		if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0) {
			Sys_Error("Video SDL_InitSubsystem failed: %s\n", SDL_GetError());
			return qfalse;
		}
	}

	SDL_VERSION(&version);
	Com_Printf("SDL version: %i.%i.%i\n", version.major, version.minor, version.patch);

	info = SDL_GetVideoInfo();
	if (info)
		Com_Printf("I: desktop depth: %ibpp\n", info->vfmt->BitsPerPixel);

	if (!R_SetMode()) {
		Sys_Error("Video subsystem failed to initialize\n");
		return qfalse;
	}

	SDL_WM_SetCaption(GAME_TITLE, GAME_TITLE_LONG);

#ifndef _WIN32
	R_SetSDLIcon(); /* currently uses ufoicon.xbm data */
#endif

	if (!SDL_GL_GetAttribute(SDL_GL_STENCIL_SIZE, &attrValue))
		Com_Printf("I: got %d bits of stencil\n", attrValue);
	if (!SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &attrValue))
		Com_Printf("I: got %d bits of depth buffer\n", attrValue);
	if (!SDL_GL_GetAttribute(SDL_GL_DOUBLEBUFFER, &attrValue))
		Com_Printf("I: got double buffer\n");
	if (!SDL_GL_GetAttribute(SDL_GL_RED_SIZE, &attrValue))
		Com_Printf("I: got %d bits for red\n", attrValue);
	if (!SDL_GL_GetAttribute(SDL_GL_GREEN_SIZE, &attrValue))
		Com_Printf("I: got %d bits for green\n", attrValue);
	if (!SDL_GL_GetAttribute(SDL_GL_BLUE_SIZE, &attrValue))
		Com_Printf("I: got %d bits for blue\n", attrValue);

	/* we need this in the renderer because if we issue an vid_restart we have
	 * to set these values again, too */
	SDL_EnableUNICODE(SDL_ENABLE);
	SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);

	return qtrue;
}

/**
 * @brief Init the SDL window
 * @param fullscreen Start in fullscreen or not (bool value)
 */
qboolean R_InitGraphics (void)
{
	uint32_t flags;

	vid_fullscreen->modified = qfalse;
	vid_mode->modified = qfalse;
	r_ext_texture_compression->modified = qfalse;

	/* Just toggle fullscreen if that's all that has been changed */
	if (r_surface && (r_surface->w == viddef.width) && (r_surface->h == viddef.height)) {
		qboolean isfullscreen = (r_surface->flags & SDL_FULLSCREEN) ? qtrue : qfalse;
		if (viddef.fullscreen != isfullscreen)
			if (!SDL_WM_ToggleFullScreen(r_surface))
				Com_Printf("R_InitGraphics: Could not set to fullscreen mode\n");

		isfullscreen = (r_surface->flags & SDL_FULLSCREEN) ? qtrue : qfalse;
		if (viddef.fullscreen == isfullscreen)
			return qtrue;
	}

	/* free resources in use */
	if (r_surface)
		SDL_FreeSurface(r_surface);

	switch (r_bitdepth->integer) {
	case 0:
	case 24:
	case 32:
		SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
		SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
		SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
		SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 1);
		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
		break;
	case 15:
	case 16:
		SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 5);
		SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 5);
		SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 5);
		SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
		break;
	default:
		Sys_Error("Invalid r_bitdepth value");
	}

	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, r_acceleratedvisuals->integer);
	SDL_GL_SetAttribute(SDL_GL_SWAP_CONTROL, r_swapinterval->integer);

	flags = SDL_OPENGL;
	if (viddef.fullscreen)
		flags |= SDL_FULLSCREEN;

	if ((r_surface = SDL_SetVideoMode(viddef.width, viddef.height, 0, flags)) == NULL) {
		const char *error = SDL_GetError();
		Com_Printf("SDL SetVideoMode failed: %s - try to reduce the r_bitdepth value\n", error);
		return qfalse;
	}

	SDL_ShowCursor(SDL_DISABLE);

	return qtrue;
}

void Rimp_Shutdown (void)
{
	if (r_surface)
		SDL_FreeSurface(r_surface);
	r_surface = NULL;

	SDL_ShowCursor(SDL_ENABLE);

	if (SDL_WasInit(SDL_INIT_EVERYTHING) == SDL_INIT_VIDEO)
		SDL_Quit();
	else
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
