/**
 * @file m_main.h
 */

/*
Copyright (C) 2002-2010 UFO: Alien Invasion.

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

#ifndef CLIENT_MENU_M_MAIN_H
#define CLIENT_MENU_M_MAIN_H

#include "m_data.h"
#include "m_windows.h"

/** @todo client code should manage itself this vars */
struct cvar_s;
extern struct cvar_s *mn_sequence;
extern struct cvar_s *mn_hud;

/* initialization */
void MN_Init(void);
void MN_Shutdown(void);
void MN_Reinit(void);

/* misc */
void MN_SetCvar(const char *name, const char *str, float value);
void MN_ExecuteConfunc(const char *fmt, ...) __attribute__((format(__printf__, 1, 2)));
int MN_DebugMode(void);

#endif
