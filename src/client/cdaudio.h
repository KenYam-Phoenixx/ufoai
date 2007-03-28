/**
 * @file cdaudio.h
 * @brief Header for CD Audio.
 */

/*
All original materal Copyright (C) 2002-2007 UFO: Alien Invasion team.

Changes:
11/06/06, Eddy Cullen (ScreamingWithNoSound): 
          Added documentation comments and updated copyright notice.
          Added inclusion guard.

Original file from Quake 2 v3.21: quake2-2.31/client/cdaudio.h

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
#ifndef CLIENT_CDAUDIO_H
#define CLIENT_CDAUDIO_H

int CDAudio_Init(void);
void CDAudio_Shutdown(void);
void CDAudio_Play(int track, qboolean looping);
void CDAudio_Stop(void);
void CDAudio_Update(void);
void CDAudio_Activate (qboolean active);
void CDAudio_RandomPlay(void);

#endif /* CLIENT_CDAUDIO_H */
