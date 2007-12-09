/**
 * @file r_rmisc.c
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

#include "r_local.h"
#include "r_error.h"

static const byte gridtexture[8][8] = {
	{1, 1, 1, 1, 1, 1, 1, 1},
	{1, 0, 0, 0, 0, 0, 0, 1},
	{1, 0, 0, 0, 0, 0, 0, 1},
	{1, 0, 0, 0, 0, 0, 0, 1},
	{1, 0, 0, 0, 0, 0, 0, 1},
	{1, 0, 0, 0, 0, 0, 0, 1},
	{1, 0, 0, 0, 0, 0, 0, 1},
	{1, 1, 1, 1, 1, 1, 1, 1},
};

void R_InitMiscTexture (void)
{
	int x, y;
	byte data[8][8][4];

	/* also use this for bad textures, but without alpha */
	for (x = 0; x < 8; x++) {
		for (y = 0; y < 8; y++) {
			data[y][x][0] = gridtexture[x][y] * 255;
			data[y][x][1] = 0;
			data[y][x][2] = 0;
			data[y][x][3] = 255;
		}
	}
	r_notexture = R_LoadPic("***r_notexture***", (byte *) data, 8, 8, it_wall, 32);

	/* empty pic in the texture chain for cinematic frames */
	R_LoadPic("***cinematic***", NULL, VID_NORM_WIDTH, VID_NORM_HEIGHT, it_pic, 32);
}


/*
==============================================================================
SCREEN SHOTS
==============================================================================
*/

typedef struct _TargaHeader {
	unsigned char id_length, colormap_type, image_type;
	unsigned short colormap_index, colormap_length;
	unsigned char colormap_size;
	unsigned short x_origin, y_origin, width, height;
	unsigned char pixel_size, attributes;
} TargaHeader;

enum {
	SSHOTTYPE_JPG,
	SSHOTTYPE_PNG,
	SSHOTTYPE_TGA,
};

void R_ScreenShot_f (void)
{
	char	checkName[MAX_OSPATH];
	int		type, shotNum, quality = 100;
	const char	*ext;
	byte	*buffer;
	FILE	*f;

	/* Find out what format to save in */
	if (Cmd_Argc() > 1)
		ext = Cmd_Argv(1);
	else
		ext = r_screenshot->string;

	if (!Q_stricmp (ext, "png"))
		type = SSHOTTYPE_PNG;
	else if (!Q_stricmp (ext, "jpg"))
		type = SSHOTTYPE_JPG;
	else
		type = SSHOTTYPE_TGA;

	/* Set necessary values */
	switch (type) {
	case SSHOTTYPE_TGA:
		Com_Printf("Taking TGA screenshot...\n");
		ext = "tga";
		break;

	case SSHOTTYPE_PNG:
		Com_Printf("Taking PNG screenshot...\n");
		ext = "png";
		break;

	case SSHOTTYPE_JPG:
		if (Cmd_Argc() == 3)
			quality = atoi(Cmd_Argv(2));
		else
			quality = r_screenshot_jpeg_quality->integer;
		if (quality > 100 || quality <= 0)
			quality = 100;

		Com_Printf("Taking JPG screenshot (at %i%% quality)...\n", quality);
		ext = "jpg";
		break;
	}

	/* Find a file name to save it to */
	for (shotNum = 0; shotNum < 1000; shotNum++) {
		Com_sprintf(checkName, MAX_OSPATH, "%s/scrnshot/ufo%i%i.%s", FS_Gamedir(), shotNum / 10, shotNum % 10, ext);
		f = fopen(checkName, "rb");
		if (!f)
			break;
		fclose(f);
	}

	FS_CreatePath(checkName);

	/* Open it */
	f = fopen(checkName, "wb");

	if (!f) {
		Com_Printf("R_ScreenShot_f: Couldn't create file: %s\n", checkName);
		return;
	}

	if (shotNum == 1000) {
		Com_Printf("R_ScreenShot_f: screenshot limit (of 1000) exceeded!\n");
		fclose(f);
		return;
	}

	/* Allocate room for a copy of the framebuffer */
	buffer = VID_TagAlloc(vid_imagePool, viddef.width * viddef.height * 3, 0);
	if (!buffer) {
		Com_Printf("R_ScreenShot_f: Could not allocate %i bytes for screenshot!\n", viddef.width * viddef.height * 3);
		fclose(f);
		return;
	}

	/* Read the framebuffer into our storage */
	qglReadPixels(0, 0, viddef.width, viddef.height, GL_RGB, GL_UNSIGNED_BYTE, buffer);
	R_CheckError();

	/* Write */
	switch (type) {
	case SSHOTTYPE_TGA:
		R_WriteTGA(f, buffer, viddef.width, viddef.height);
		break;

	case SSHOTTYPE_PNG:
		R_WritePNG(f, buffer, viddef.width, viddef.height);
		break;

	case SSHOTTYPE_JPG:
		R_WriteJPG(f, buffer, viddef.width, viddef.height, quality);
		break;
	}

	/* Finish */
	fclose(f);
	VID_MemFree(buffer);

	Com_Printf("Wrote %s\n", checkName);
}
