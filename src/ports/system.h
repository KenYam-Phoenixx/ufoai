/**
 * @file
 * @brief System specific stuff
 */

/*
All original material Copyright (C) 2002-2025 UFO: Alien Invasion.

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

#include "../shared/cxx.h"
#include "../common/list.h"
#include <stdio.h>

/*
==============================================================
NON-PORTABLE SYSTEM SERVICES
==============================================================
*/

struct qFILE_s;

void Sys_Init(void);
void Sys_NormPath(char* path);
void Sys_Sleep(int milliseconds);
const char* Sys_GetCurrentUser(void);
int Sys_Setenv(const char* name, const char* value);
void Sys_InitSignals(void);
const char* Sys_SetLocale(const char* localeID);
const char* Sys_GetLocale(void);

const char* Sys_ConsoleInput(void);
void Sys_ConsoleOutput(const char* string);
void Sys_Error(const char* error, ...) __attribute__((noreturn, format(__printf__, 1, 2)));
void Sys_Quit(void);
char* Sys_GetHomeDirectory(void);

void Sys_ConsoleShutdown(void);
void Sys_ConsoleInit(void);
void Sys_ShowConsole(bool show);

/* pass in an attribute mask of things you wish to REJECT */
char* Sys_FindFirst(const char* path, unsigned musthave, unsigned canthave);
char* Sys_FindNext(unsigned musthave, unsigned canthave);
void Sys_FindClose(void);
void Sys_ListFilteredFiles(const char* basedir, const char* subdirs, const char* filter, linkedList_t** list);
void Sys_Mkdir(const char* path);
void Sys_Mkfifo(const char* ospath, struct qFILE_s* f);
char* Sys_Cwd(void);
void Sys_SetAffinityAndPriority(void);
int Sys_Milliseconds(void);
void Sys_Backtrace(void);
void Sys_Breakpoint(void);
void Sys_OpenURL(const char* url);
FILE* Sys_Fopen(const char* filename, const char* mode);
int Sys_Remove (const char* filename);
int Sys_Rename (const char* oldname, const char* newname);
int Sys_Access (const char* filename, int mode);
