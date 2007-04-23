/**
 * @file filesys.h
 * @brief Filesystem header file.
 */

/*
All original materal Copyright (C) 2002-2007 UFO: Alien Invasion team.

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


#ifndef QCOMMON_FILESYS_H
#define QCOMMON_FILESYS_H

/*
==============================================================
FILESYSTEM
==============================================================
*/
#define MAX_MAPS 400
extern char *maps[MAX_MAPS];
extern int numInstalledMaps;
extern int mapInstalledIndex;

typedef struct qFILE_s {
	void *z; /* in case of the file being a zip archive */
	FILE *f; /* in case the file being part of a pak or the actual file */
	char name[MAX_OSPATH];
	unsigned long filepos;
} qFILE;

typedef enum {
	FS_READ,
	FS_WRITE,
	FS_APPEND,
	FS_APPEND_SYNC
} fsMode_t;

typedef enum {
	FS_SEEK_CUR,
	FS_SEEK_END,
	FS_SEEK_SET
} fsOrigin_t;


typedef struct {
	char name[MAX_QPATH];
	unsigned long filepos;
	unsigned long filelen;
} packfile_t;

typedef struct pack_s {
	char filename[MAX_OSPATH];
	qFILE handle;
	int numfiles;
	packfile_t *files;
} pack_t;

typedef struct searchpath_s {
	char filename[MAX_OSPATH];
	pack_t *pack;				/* only one of filename / pack will be used */
	struct searchpath_s *next;
} searchpath_t;

extern searchpath_t *fs_searchpaths;
extern searchpath_t *fs_base_searchpaths;	/* without gamedirs */

int FS_FileLength(qFILE * f);
void FS_FOpenFileWrite(const char *filename, qFILE * f);
int FS_Seek(qFILE * f, long offset, int origin);
int FS_WriteFile(const void *buffer, size_t len, const char *filename);
int FS_Write(const void *buffer, int len, qFILE * f);
void FS_InitFilesystem(void);
void FS_SetGamedir(const char *dir);
char *FS_Gamedir(void);
char *FS_NextPath(const char *prevpath);
void FS_ExecAutoexec(void);
char *FS_GetCwd(void);
void FS_NormPath(char *path);
qboolean FS_FileExists(const char *filename);
char* FS_GetBasePath(char* filename);
void FS_SkipBlock(char **text);

void FS_GetMaps(qboolean reset);

int FS_FOpenFile(const char *filename, qFILE * file);
void FS_FCloseFile(qFILE * f);

/* note: this can't be called from another DLL, due to MS libc issues */

int FS_LoadFile(const char *path, void **buffer);

/* a null buffer will just return the file length without loading */
/* a -1 length is not present */

#ifdef DEBUG
#define FS_Read(buffer, len, f) FS_ReadDebug(buffer, len, f, __FILE__, __LINE__)
int FS_ReadDebug(void *buffer, int len, qFILE * f, char* file, int line);
#else
int FS_Read(void *buffer, int len, qFILE * f);
#endif
/* properly handles partial reads */

void FS_FreeFile(void *buffer);

int FS_CheckFile(const char *path);

void FS_BuildFileList(char *files);
char *FS_NextScriptHeader(const char *files, char **name, char **text);
void FS_CreatePath(const char *path);

#endif /* QCOMMON_FILESYS_H */
