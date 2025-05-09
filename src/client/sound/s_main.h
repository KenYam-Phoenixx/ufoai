/**
 * @file
 * @brief Specifies sound API?
 */

/*
All original material Copyright (C) 2002-2025 UFO: Alien Invasion.

Original file from Quake 2 v3.21: quake2-2.31/client/sound.h
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

#pragma once

#include "../../shared/mathlib.h"	/* for vec3_t */

/** @brief These sounds are precached in S_LoadSamples */
typedef enum {
	SOUND_WATER_IN,
	SOUND_WATER_OUT,
	SOUND_WATER_MOVE,

	MAX_SOUNDIDS
} stdsound_t;

#define SND_VOLUME_DEFAULT 1.0f
#define SND_VOLUME_WEAPONS 1.0f

void S_Init(void);
void S_Shutdown(void);
void S_Frame(void);
void S_Stop(void);
void S_PlayStdSample(const stdsound_t sId, const vec3_t origin, float atten, float volume);
void S_StartLocalSample(const char* s, float volume);
int S_LoadSampleIdx (const char* soundFile);
bool S_LoadAndPlaySample(const char* s, const vec3_t origin, float atten, float volume);
void S_SetSampleRepeatRate(int sampleRepeatRate);
void S_LoadSamples(void);
