/**
 * @file unix_main.c
 * @brief Some generic *nix functions
 */

/*
Copyright (C) 1997-2001 UFO:AI team

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
#include <sys/file.h>

#include "../../common/common.h"
#include "../unix/unix_glob.h"

static void *game_library;

const char *Sys_GetCurrentUser (void)
{
	struct passwd *p;

	if ((p = getpwuid(getuid())) == NULL) {
		return "player";
	}
	return p->pw_name;
}

/**
 * @return NULL if getcwd failed
 */
char *Sys_Cwd (void)
{
	static char cwd[MAX_OSPATH];

	if (getcwd(cwd, sizeof(cwd) - 1) == NULL)
		return NULL;
	cwd[MAX_OSPATH-1] = 0;

	return cwd;
}

void Sys_Error (const char *error, ...)
{
	va_list argptr;
	char string[1024];

	/* change stdin to non blocking */
	fcntl(0, F_SETFL, fcntl (0, F_GETFL, 0) & ~FNDELAY);

	CL_Shutdown();
	Qcommon_Shutdown();

	va_start(argptr,error);
	Q_vsnprintf(string, sizeof(string), error, argptr);
	va_end(argptr);

	fprintf(stderr, "Error: %s\n", string);

	exit(1);
}

/**
 * @brief
 * @sa Qcommon_Shutdown
 */
void Sys_Quit (void)
{
	CL_Shutdown();
	Qcommon_Shutdown();
	Sys_ConsoleShutdown();

	fcntl(0, F_SETFL, fcntl (0, F_GETFL, 0) & ~FNDELAY);
	exit(0);
}

void Sys_Sleep (int milliseconds)
{
	if (milliseconds < 1)
		milliseconds = 1;
	usleep(milliseconds * 1000);
}

/**
 * @brief Returns the home environment variable
 * (which hold the path of the user's homedir)
 */
char *Sys_GetHomeDirectory (void)
{
	return getenv("HOME");
}

void Sys_NormPath (char* path)
{
}

static	char	findbase[MAX_OSPATH];
static	char	findpath[MAX_OSPATH];
static	char	findpattern[MAX_OSPATH];
static	DIR		*fdir;

static qboolean CompareAttributes (const char *path, const char *name, unsigned musthave, unsigned canthave)
{
	struct stat st;
	char fn[MAX_OSPATH];

	/* . and .. never match */
	if (Q_strcmp(name, ".") == 0 || Q_strcmp(name, "..") == 0)
		return qfalse;

	Com_sprintf(fn, sizeof(fn), "%s/%s", path, name);
	if (stat(fn, &st) == -1) {
		Com_Printf("CompareAttributes: Warning, stat failed: %s\n", name);
		return qfalse; /* shouldn't happen */
	}

	if ((st.st_mode & S_IFDIR) && (canthave & SFF_SUBDIR))
		return qfalse;

	if ((musthave & SFF_SUBDIR) && !(st.st_mode & S_IFDIR))
		return qfalse;

	return qtrue;
}

/**
 * @brief Opens the directory and returns the first file that matches our searchrules
 * @sa Sys_FindNext
 * @sa Sys_FindClose
 */
char *Sys_FindFirst (const char *path, unsigned musthave, unsigned canhave)
{
	struct dirent *d;
	char *p;

	if (fdir)
		Sys_Error("Sys_BeginFind without close");

	Q_strncpyz(findbase, path, sizeof(findbase));

	if ((p = strrchr(findbase, '/')) != NULL) {
		*p = 0;
		Q_strncpyz(findpattern, p + 1, sizeof(findpattern));
	} else
		Q_strncpyz(findpattern, "*", sizeof(findpattern));

	if (Q_strcmp(findpattern, "*.*") == 0)
		Q_strncpyz(findpattern, "*", sizeof(findpattern));

	if ((fdir = opendir(findbase)) == NULL)
		return NULL;

	while ((d = readdir(fdir)) != NULL) {
		if (!*findpattern || glob_match(findpattern, d->d_name)) {
/*			if (*findpattern) */
/*				printf("%s matched %s\n", findpattern, d->d_name); */
			if (CompareAttributes(findbase, d->d_name, musthave, canhave)) {
				Com_sprintf(findpath, sizeof(findpath), "%s/%s", findbase, d->d_name);
				return findpath;
			}
		}
	}
	return NULL;
}

/**
 * @brief Returns the next file of the already opened directory (Sys_FindFirst) that matches our search mask
 * @sa Sys_FindClose
 * @sa Sys_FindFirst
 * @sa static var findpattern
 */
char *Sys_FindNext (unsigned musthave, unsigned canhave)
{
	struct dirent *d;

	if (fdir == NULL)
		return NULL;
	while ((d = readdir(fdir)) != NULL) {
		if (!*findpattern || glob_match(findpattern, d->d_name)) {
/*			if (*findpattern) */
/*				printf("%s matched %s\n", findpattern, d->d_name); */
			if (CompareAttributes(findbase, d->d_name, musthave, canhave)) {
				Com_sprintf(findpath, sizeof(findpath), "%s/%s", findbase, d->d_name);
				return findpath;
			}
		}
	}
	return NULL;
}

void Sys_FindClose (void)
{
	if (fdir != NULL)
		closedir(fdir);
	fdir = NULL;
}

void Sys_OSPath (char* path)
{
}

void Sys_UnloadGame (void)
{
	if (game_library)
		Sys_FreeLibrary(game_library);
	game_library = NULL;
}

/**
 * @brief Loads the game dll
 */
game_export_t *Sys_GetGameAPI (game_import_t *parms)
{
	void *(*GetGameAPI) (void *);

	char name[MAX_OSPATH];
	const char *path;

	setreuid(getuid(), getuid());
	setegid(getgid());

	if (game_library)
		Com_Error(ERR_FATAL, "Sys_GetGameAPI without Sys_UnloadingGame");

	Com_Printf("------- Loading game.%s -------\n", SHARED_EXT);

	/* now run through the search paths */
	path = NULL;
	while (1) {
		path = FS_NextPath(path);
		if (!path) {
			Com_Printf("LoadLibrary failed (game."SHARED_EXT")\n");
			Com_DPrintf(DEBUG_SYSTEM, "%s\n", dlerror());
			return NULL;		/* couldn't find one anywhere */
		}
		Com_sprintf(name, sizeof(name), "%s/game_"CPUSTRING".%s", path, SHARED_EXT);
		game_library = dlopen(name, RTLD_LAZY);
		if (game_library) {
			Com_Printf("LoadLibrary (%s)\n", name);
			break;
		} else {
			Com_sprintf(name, sizeof(name), "%s/game.%s", path, SHARED_EXT);
			game_library = dlopen(name, RTLD_LAZY);
			if (game_library) {
				Com_Printf("LoadLibrary (%s)\n", name);
				break;
			}
		}
	}

	GetGameAPI = (void *)dlsym(game_library, "GetGameAPI");
	if (!GetGameAPI) {
		Sys_UnloadGame();
		return NULL;
	}

	return GetGameAPI(parms);
}

/**
 * @sa Sys_FreeLibrary
 * @sa Sys_GetProcAddress
 */
void *Sys_LoadLibrary (const char *name, int flags)
{
	void *lib;
	cvar_t *s_libdir;
	char libName[MAX_OSPATH];
	char libDir[MAX_OSPATH];

	s_libdir = Cvar_Get("s_libdir", "", CVAR_ARCHIVE, "Library dir for graphic, sound and game libraries");

	/* try path given via cvar */
	if (*s_libdir->string) {
		Com_Printf("...also try library search path: '%s'\n", s_libdir->string);
		Q_strncpyz(libDir, s_libdir->string, sizeof(libDir));
	} else {
		Q_strncpyz(libDir, ".", sizeof(libDir));
	}

	/* first try system wide */
	Com_sprintf(libName, sizeof(libName), "%s_"CPUSTRING"."SHARED_EXT, name);
	Com_DPrintf(DEBUG_SYSTEM, "Sys_LoadLibrary: try %s\n", libName);
	lib = dlopen(libName, flags|RTLD_LAZY|RTLD_GLOBAL);
	if (lib)
		return lib;
	else
		Com_DPrintf(DEBUG_SYSTEM, "%s\n", dlerror());

	/* then use s_libdir cvar or current dir */
	Com_sprintf(libName, sizeof(libName), "%s/%s_"CPUSTRING"."SHARED_EXT, libDir, name);
	Com_DPrintf(DEBUG_SYSTEM, "Sys_LoadLibrary: try %s\n", libName);
	lib = dlopen(libName, flags|RTLD_LAZY);
	if (lib)
		return lib;
	else
		Com_DPrintf(DEBUG_SYSTEM, "%s\n", dlerror());

	/* and not both again but without CPUSTRING */
	/* system wide */
	Com_sprintf(libName, sizeof(libName), "%s.%s", name, SHARED_EXT);
	Com_DPrintf(DEBUG_SYSTEM, "Sys_LoadLibrary: try %s\n", libName);
	lib = dlopen(libName, flags|RTLD_LAZY|RTLD_GLOBAL);
	if (lib)
		return lib;
	else
		Com_DPrintf(DEBUG_SYSTEM, "%s\n", dlerror());

	/* then use s_libdir cvar or current dir */
	Com_sprintf(libName, sizeof(libName), "%s/%s."SHARED_EXT, libDir, name);
	Com_DPrintf(DEBUG_SYSTEM, "Sys_LoadLibrary: try %s\n", libName);
	lib = dlopen(libName, flags|RTLD_LAZY);
	if (lib)
		return lib;
	else
		Com_DPrintf(DEBUG_SYSTEM, "%s\n", dlerror());

	Com_Printf("Could not load %s."SHARED_EXT" and %s_"CPUSTRING"."SHARED_EXT"\n", name, name);
	return NULL;
}

/**
 * @sa Sys_LoadLibrary
 */
void Sys_FreeLibrary (void *libHandle)
{
	if (!libHandle)
		Com_Error(ERR_DROP, "Sys_FreeLibrary: No valid handle given");
	if (dlclose(libHandle) != 0)
		Com_Error(ERR_DROP, "Sys_FreeLibrary: dlclose() failed - %s", dlerror());
}

/**
 * @sa Sys_LoadLibrary
 */
void *Sys_GetProcAddress (void *libHandle, const char *procName)
{
	if (!libHandle)
		Com_Error(ERR_DROP, "Sys_GetProcAddress: No valid libHandle given");
	return dlsym(libHandle, procName);
}

int Sys_Milliseconds (void)
{
	struct timeval tp;
	struct timezone tzp;
	static int secbase = 0;

	gettimeofday(&tp, &tzp);

	if (!secbase) {
		secbase = tp.tv_sec;
		return tp.tv_usec / 1000;
	}

	return (tp.tv_sec - secbase) * 1000 + tp.tv_usec / 1000;
}

void Sys_Mkdir (const char *thePath)
{
	if (mkdir(thePath, 0777) != -1)
		return;

	if (errno != EEXIST)
		Com_Printf("\"mkdir %s\" failed, reason: \"%s\".", thePath, strerror(errno));
}

void Sys_SetAffinityAndPriority (void)
{
	if (sys_affinity->modified) {
		sys_affinity->modified = qfalse;
	}

	if (sys_priority->modified) {
		sys_priority->modified = qfalse;
	}
}
