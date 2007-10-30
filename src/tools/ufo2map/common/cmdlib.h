/**
 * @file cmdlib.h
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

#ifndef __CMDLIB__
#define __CMDLIB__

#include "../../../shared/byte.h"

char *strlower(char *in);
int Q_strncasecmp(const char *s1, const char *s2, int n);
int Q_strcasecmp(const char *s1, const char *s2);
void Q_getwd(char *out);

int Q_filelength(qFILE *f);

void SetQdirFromPath(char *path);
char *ExpandArg(const char *path);	/* from cmd line */
char *ExpandPath(const char *path);	/* from scripts */

double I_FloatTime(void);

qFILE *SafeOpenWrite(const char *filename, qFILE *f);
qFILE *SafeOpenRead(const char *filename, qFILE *f);
void SafeRead(qFILE *f, void *buffer, int count);
void SafeWrite(qFILE *f, void *buffer, int count);

int LoadFile(const char *filename, void **bufferptr);
void CloseFile(qFILE *f);
void FreeFile(void *buffer);
int TryLoadFile(const char *filename, void **bufferptr);

void DefaultExtension(char *path, const char *extension);

char *copystring(const char *s);

#endif
