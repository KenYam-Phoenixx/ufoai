/**
 * @file cl_cinematic.h
 * @brief Header file for cinematics
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

#ifndef CLIENT_CL_CINEMATIC_H

void CIN_StopCinematic(void);
void CIN_PlayCinematic(const char *name);
void CIN_Shutdown(void);
void CIN_Init(void);
void CIN_SetParameters(int x, int y, int w, int h, int cinStatus);

typedef enum {
	CIN_STATUS_NONE,	/**< not playing */
	CIN_STATUS_FULLSCREEN,	/**< fullscreen cinematic */

	/* don't stop running sounds for these - but the above */
	CIN_STATUS_MENU,		/**< cinematic inside a menu node */
	CIN_STATUS_SURFACE		/**< cinematic on a map surface */
} cinStatus_t;

#endif /* CLIENT_CL_CINEMATIC_H */
