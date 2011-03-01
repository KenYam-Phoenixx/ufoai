/**
 * @file unix_main.c
 * @brief Some generic *nix functions
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

#include <android/log.h>
#include "../android/android_debugger.h"

const char *Sys_GetCurrentUser (void)
{
	char * user = getenv("USER");
	if( user )
		return user;
	return "Player";
}

/**
 * @return NULL if getcwd failed
 */
char *Sys_Cwd (void)
{
	static char cwd[MAX_OSPATH];

	if (getcwd(cwd, sizeof(cwd) - 1) == NULL)
		return NULL;
	cwd[MAX_OSPATH - 1] = 0;

	return cwd;
}

/**
 * @brief Errors out of the game.
 * @note The error message should not have a newline - it's added inside of this function
 * @note This function does never return
 */
void Sys_Error (const char *error, ...)
{
	va_list argptr;
	char string[1024];

	Sys_Backtrace();

#ifdef COMPILE_UFO
	Sys_ConsoleShutdown();
#endif

#ifdef COMPILE_MAP
	Mem_Shutdown();
#endif

	va_start(argptr,error);
	Q_vsnprintf(string, sizeof(string), error, argptr);
	va_end(argptr);

	__android_log_print(ANDROID_LOG_FATAL, "UFOAI", "Error: %s", string);

	exit(1);
}

/**
 * @brief
 * @sa Qcommon_Shutdown
 */
void Sys_Quit (void)
{
#ifdef COMPILE_UFO
	CL_Shutdown();
	Qcommon_Shutdown();
	Sys_ConsoleShutdown();
#elif COMPILE_MAP
	Mem_Shutdown();
#endif

	exit(0);
}

void Sys_Sleep (int milliseconds)
{
	if (milliseconds < 1)
		milliseconds = 1;
	usleep(milliseconds * 1000);
}

#ifdef COMPILE_UFO
const char *Sys_SetLocale (const char *localeID)
{
	const char *locale;

	unsetenv("LANGUAGE");

	Sys_Setenv("LC_NUMERIC", "C");
	setlocale(LC_NUMERIC, "C");

	/* set to system default */
	setlocale(LC_ALL, "C");
	locale = setlocale(LC_MESSAGES, localeID);
	if (!locale) {
		Com_DPrintf(DEBUG_CLIENT, "...could not set to language: %s\n", localeID);
		locale = setlocale(LC_MESSAGES, "");
		if (!locale) {
			Com_DPrintf(DEBUG_CLIENT, "...could not set to system language\n");
		}
		return NULL;
	} else {
		Com_Printf("...using language: %s\n", locale);
	}

	return locale;
}

const char *Sys_GetLocale (void)
{
	/* Calling with NULL param should return current system settings. */
	const char *currentLocale = setlocale(LC_MESSAGES, NULL);
	if (currentLocale != NULL && currentLocale[0] != '\0')
		return currentLocale;
	else
		return "C";
}
#endif

/**
 * @brief set/unset environment variables (empty value removes it)
 */
int Sys_Setenv (const char *name, const char *value)
{
	if (value && value[0] != '\0')
		return setenv(name, value, 1);
	else
		return unsetenv(name);
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
	if (Q_streq(name, ".") || Q_streq(name, ".."))
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

	if (Q_streq(findpattern, "*.*"))
		Q_strncpyz(findpattern, "*", sizeof(findpattern));

	if ((fdir = opendir(findbase)) == NULL)
		return NULL;

	while ((d = readdir(fdir)) != NULL) {
		if (!*findpattern || Com_Filter(findpattern, d->d_name)) {
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
		if (!*findpattern || Com_Filter(findpattern, d->d_name)) {
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

#define MAX_FOUND_FILES 0x1000

void Sys_ListFilteredFiles (const char *basedir, const char *subdirs, const char *filter, linkedList_t **list)
{
	char search[MAX_OSPATH], newsubdirs[MAX_OSPATH];
	char filename[MAX_OSPATH];
	DIR *directory;
	struct dirent *d;
	struct stat st;

	if (subdirs[0] != '\0') {
		Com_sprintf(search, sizeof(search), "%s/%s", basedir, subdirs);
	} else {
		Com_sprintf(search, sizeof(search), "%s", basedir);
	}

	if ((directory = opendir(search)) == NULL)
		return;

	while ((d = readdir(directory)) != NULL) {
		Com_sprintf(filename, sizeof(filename), "%s/%s", search, d->d_name);
		if (stat(filename, &st) == -1)
			continue;

		if (st.st_mode & S_IFDIR) {
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

#ifdef COMPILE_UFO
void Sys_SetAffinityAndPriority (void)
{
	if (sys_affinity->modified) {
		sys_affinity->modified = qfalse;
	}

	if (sys_priority->modified) {
		sys_priority->modified = qfalse;
	}
}
#endif

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

/**
 * @brief On platforms supporting it, print a backtrace.
 */
void Sys_Backtrace (void)
{
	const char *dumpFile = "crashdump.txt";
	FILE *file = fopen(dumpFile, "w");
	FILE *crash = file != NULL ? file : stderr;

	fprintf(crash, "======start======\n");

	fprintf(crash, BUILDSTRING"\n\n");
	fflush(crash);

	androidDumpBacktrace(crash);

	fprintf(crash, "======end========\n");

	if (file != NULL)
		fclose(file);

#ifdef COMPILE_UFO
	Com_UploadCrashDump(dumpFile);
#endif
}

#ifdef USE_SIGNALS
/**
 * @brief Catch kernel interrupts and dispatch the appropriate exit routine.
 */
static void Sys_Signal (int s)
{
	switch (s) {
	case SIGHUP:
	case SIGINT:
	case SIGQUIT:
	case SIGTERM:
		Com_Printf("Received signal %d, quitting..\n", s);
		Sys_Quit();
		break;
	default:
		Sys_Error("Received signal %d.\n", s);
		break;
	}
}
#endif

void Sys_InitSignals (void)
{
#ifdef USE_SIGNALS
	signal(SIGHUP, Sys_Signal);
	signal(SIGINT, Sys_Signal);
	signal(SIGQUIT, Sys_Signal);
	signal(SIGILL, Sys_Signal);
	signal(SIGABRT, Sys_Signal);
	signal(SIGFPE, Sys_Signal);
	signal(SIGSEGV, Sys_Signal);
	signal(SIGTERM, Sys_Signal);
#endif
}
