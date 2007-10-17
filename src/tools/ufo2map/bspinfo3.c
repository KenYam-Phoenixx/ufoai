/**
 * @file bspinfo3.c
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

#include "common/shared.h"
#include "common/cmdlib.h"
#include "common/mathlib.h"
#include "common/bspfile.h"

/**
 * @sa LoadBSPFile
 * @sa PrintBSPFileSizes
 */
int main (int argc, char **argv)
{
	int i, size;
	char source[1024];
	FILE *f;

	if (argc == 1)
		Sys_Error("usage: bspinfo bspfile [bspfiles]");

	for (i = 1; i < argc; i++) {
		Com_Printf(("---------------------\n");
		strcpy(source, argv[i]);
		DefaultExtension(source, ".bsp");
		f = fopen(source, "rb");
		if (f) {
			size = Q_filelength(f);
			fclose(f);
		} else
			size = 0;
		Com_Printf(("%s: %i\n", source, size);

		LoadBSPFile(source);
		PrintBSPFileSizes();
		Com_Printf(("---------------------\n");
	}
	return 0;
}
