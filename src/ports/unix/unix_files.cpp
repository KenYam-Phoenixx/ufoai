/**
 * @file
 * @brief Some generic *nix file related functions
 */

/*
Copyright (C) 2002-2025 UFO: Alien Invasion.

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

#include <unistd.h>
#include <sys/time.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <locale.h>
#include <signal.h>
#include <dirent.h>

#include "../../common/common.h"
#include "../system.h"

/**
 * @brief Returns the home environment variable
 * (which hold the path of the user's homedir)
 */
char* Sys_GetHomeDirectory (void)
{
	return getenv("HOME");
}

void Sys_NormPath (char* path)
{
}

static	char	findbase[MAX_OSPATH];
static	char	findpath[MAX_OSPATH];
static	char	findpattern[MAX_OSPATH];
static	DIR*	fdir;

static bool CompareAttributes (const char* path, const char* name, unsigned musthave, unsigned canthave)
{
	/* . and .. never match */
	if (Q_streq(name, ".") || Q_streq(name, ".."))
		return false;

	char fn[MAX_OSPATH];
	Com_sprintf(fn, sizeof(fn), "%s/%s", path, name);
	struct stat st;
	if (stat(fn, &st) == -1) {
		Com_Printf("CompareAttributes: Warning, stat failed: %s\n", name);
		return false; /* shouldn't happen */
	}

	if ((st.st_mode & S_IFDIR) && (canthave & SFF_SUBDIR))
		return false;

	if ((musthave & SFF_SUBDIR) && !(st.st_mode & S_IFDIR))
		return false;

	return true;
}

/**
 * @brief Opens the directory and returns the first file that matches our searchrules
 * @sa Sys_FindNext
 * @sa Sys_FindClose
 */
char* Sys_FindFirst (const char* path, unsigned musthave, unsigned canhave)
{
	if (fdir)
		Sys_Error("Sys_BeginFind without close");

	Q_strncpyz(findbase, path, sizeof(findbase));

	char* p;
	if ((p = strrchr(findbase, '/')) != nullptr) {
		*p = 0;
		Q_strncpyz(findpattern, p + 1, sizeof(findpattern));
	} else
		Q_strncpyz(findpattern, "*", sizeof(findpattern));

	if (Q_streq(findpattern, "*.*"))
		Q_strncpyz(findpattern, "*", sizeof(findpattern));

	if ((fdir = opendir(findbase)) == nullptr)
		return nullptr;

	const struct dirent* d;
	while ((d = readdir(fdir)) != nullptr) {
		if (!*findpattern || Com_Filter(findpattern, d->d_name)) {
			if (CompareAttributes(findbase, d->d_name, musthave, canhave)) {
				Com_sprintf(findpath, sizeof(findpath), "%s/%s", findbase, d->d_name);
				return findpath;
			}
		}
	}
	return nullptr;
}

/**
 * @brief Returns the next file of the already opened directory (Sys_FindFirst) that matches our search mask
 * @sa Sys_FindClose
 * @sa Sys_FindFirst
 * @sa static var findpattern
 */
char* Sys_FindNext (unsigned musthave, unsigned canhave)
{
	if (fdir == nullptr)
		return nullptr;

	const struct dirent* d;
	while ((d = readdir(fdir)) != nullptr) {
		if (!*findpattern || Com_Filter(findpattern, d->d_name)) {
			if (CompareAttributes(findbase, d->d_name, musthave, canhave)) {
				Com_sprintf(findpath, sizeof(findpath), "%s/%s", findbase, d->d_name);
				return findpath;
			}
		}
	}
	return nullptr;
}

void Sys_FindClose (void)
{
	if (fdir != nullptr)
		closedir(fdir);
	fdir = nullptr;
}

#define MAX_FOUND_FILES 0x1000

void Sys_ListFilteredFiles (const char* basedir, const char* subdirs, const char* filter, linkedList_t** list)
{
	char search[MAX_OSPATH];

	if (subdirs[0] != '\0') {
		Com_sprintf(search, sizeof(search), "%s/%s", basedir, subdirs);
	} else {
		Com_sprintf(search, sizeof(search), "%s", basedir);
	}

	DIR* directory;
	if ((directory = opendir(search)) == nullptr)
		return;

	char filename[MAX_OSPATH];
	const struct dirent* d;
	struct stat st;

	while ((d = readdir(directory)) != nullptr) {
		Com_sprintf(filename, sizeof(filename), "%s/%s", search, d->d_name);
		if (stat(filename, &st) == -1)
			continue;

		if (st.st_mode & S_IFDIR) {
			char newsubdirs[MAX_OSPATH];
			if (Q_strcasecmp(d->d_name, ".") && Q_strcasecmp(d->d_name, "..")) {
				if (subdirs[0] != '\0') {
					Com_sprintf(newsubdirs, sizeof(newsubdirs), "%s/%s", subdirs, d->d_name);
				} else {
					Com_sprintf(newsubdirs, sizeof(newsubdirs), "%s", d->d_name);
				}
				Sys_ListFilteredFiles(basedir, newsubdirs, filter, list);
			}
		}
		Com_sprintf(filename, sizeof(filename), "%s/%s", subdirs, d->d_name);
		if (!Com_Filter(filter, filename))
			continue;
		LIST_AddString(list, filename);
	}

	closedir(directory);
}

/**
 * @return nullptr if getcwd failed
 */
char* Sys_Cwd (void)
{
	static char cwd[MAX_OSPATH];

	if (getcwd(cwd, sizeof(cwd) - 1) == nullptr)
		return nullptr;
	cwd[MAX_OSPATH - 1] = 0;

	return cwd;
}

void Sys_Mkdir (const char* thePath)
{
	if (mkdir(thePath, 0777) != -1)
		return;

	if (errno != EEXIST)
		Com_Printf("\"mkdir %s\" failed, reason: \"%s\".", thePath, strerror(errno));
}

void Sys_Mkfifo (const char* ospath, qFILE* f)
{
	struct stat buf;

	/* if file already exists AND is a pipefile, remove it */
	if (!stat(ospath, &buf) && S_ISFIFO(buf.st_mode))
		FS_RemoveFile(ospath);

	const int result = mkfifo(ospath, 0600);
	if (result != 0)
		return;

	FILE* fifo = Sys_Fopen(ospath, "w+");
	if (fifo) {
		const int fn = fileno(fifo);
		fcntl(fn, F_SETFL, O_NONBLOCK);
		f->f = fifo;
	} else {
		Com_Printf("WARNING: Could not create fifo pipe at %s.\n", ospath);
		f->f = nullptr;
	}
}

FILE* Sys_Fopen (const char* filename, const char* mode)
{
	return fopen(filename, mode);
}

int Sys_Remove (const char* filename)
{
	return remove(filename);
}

int Sys_Rename (const char* oldname, const char* newname)
{
	return rename(oldname, newname);
}

int Sys_Access (const char* filename, int mode)
{
	return access(filename, mode);
}
