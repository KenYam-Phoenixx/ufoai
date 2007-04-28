/**
 * @file qal_osx.c
 */

/*
Copyright (C) 2001-2007 UFO:Alien Invasion team

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

#include "../../client/qal.h"
#include "qal_osx.h"

#ifdef HAVE_OPENAL

qboolean QAL_Init (void)
{
	char libPath[MAX_OSPATH];
	cvar_t* s_libdir = Cvar_Get("s_libdir", "", CVAR_ARCHIVE, "lib dir for graphic and sound renderer - no game libs");

	Com_Printf("QAL_Init: Loading \"%s\"...\n", AL_DRIVER_OPENAL);

	if ((oalState.hInstOpenAL = dlopen(AL_DRIVER_OPENAL, RTLD_LAZY|RTLD_GLOBAL)) == 0) {
		/* try path given via cvar */
		if (strlen(s_libdir->string))
			Q_strncpyz(libPath, s_libdir->string, sizeof(libPath));
		else
			strcpy(libPath, ".");

		Q_strcat(libPath, "/", sizeof(libPath));
		Q_strcat(libPath, AL_DRIVER_OPENAL, sizeof(libPath));

		if ((oalState.hInstOpenAL = dlopen(libPath, RTLD_LAZY)) == 0) {
			Com_Printf("%s\n", dlerror());
			return qfalse;
		}
	}

	if ((oalState.hInstALUT = dlopen(AL_DRIVER_ALUT, RTLD_LAZY|RTLD_GLOBAL)) == 0) {
		/* try path given via cvar */
		if (strlen(s_libdir->string))
			Q_strncpyz(libPath, s_libdir->string, sizeof(libPath));
		else
			strcpy(libPath, ".");

		Q_strcat(libPath, "/", sizeof(libPath));
		Q_strcat(libPath, AL_DRIVER_ALUT, sizeof(libPath));

		if ((oalState.hInstOpenAL = dlopen(libPath, RTLD_LAZY)) == 0) {
			Com_Printf("%s\n", dlerror());
			return qfalse;
		}
	}

	if (QAL_Link()) {
		openal_active = qtrue;
		return qtrue;
	} else
		return qfalse;
}

void QAL_Shutdown (void)
{
	if (oalState.hInstOpenAL)
		dlclose(oalState.hInstOpenAL);

	oalState.hInstOpenAL = NULL;

	/* general pointers */
	QAL_Unlink();
}

#endif