/**
 * @file cl_shader.c
 * @brief Parses and loads shaders from files and passes them to the renderer.
 *
 *
 * Reads in a shader program from a file, puts it in a struct and tell the renderer about the location of this struct.
 * This is to allow the adding of shaders without recompiling the binaries.
 * You can add shaders for each texture; new maps can bring new textures.
 * Without a scriptable shader interface we have to rebuild the renderer to get a shader for a specific texture.
 * They are here and not in the renderer because the file parsers are part of the client.
 * TODO: Add command to re-initialise shaders to allow interactive debugging.
 */

/*
Copyright (C) 2002-2007 UFO: Alien Invasion team.

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


#include "client.h"

/* extern in client.h */
int r_numshaders;

/* shader_t is in client/ref.h */
/* r_shaders is external and is linked to cl.refdef.shaders in cl_view.c V_RenderView */
shader_t r_shaders[MAX_SHADERS];

static const value_t shader_values[] = {
	{"filename",	V_STRING,	offsetof( shader_t, filename)},
	{"normal",	V_STRING,	offsetof( shader_t, normal)},
	{"distort",	V_STRING,	offsetof( shader_t, distort)},
	{"frag",	V_BOOL,	offsetof( shader_t, frag)},
	{"vertex",	V_BOOL,	offsetof( shader_t, vertex)},
	{"glsl",	V_BOOL,	offsetof( shader_t, glsl)},
	{"glmode",	V_BLEND,	offsetof( shader_t, glMode)},
	{"emboss",	V_BOOL,	offsetof( shader_t, emboss)},
	{"emboss_high",	V_BOOL,	offsetof( shader_t, embossHigh)},
	{"emboss_low",	V_BOOL,	offsetof( shader_t, embossLow)},
	{"emboss_2",	V_BOOL,	offsetof( shader_t, emboss2)},
	{"blur",	V_BOOL,	offsetof( shader_t, blur)},
	{"light",	V_BOOL,	offsetof( shader_t, light)},
	{"edge",	V_BOOL,	offsetof( shader_t, edge)},

	{NULL, 0, 0}
};

/**
 * @brief Parses all shader script from ufo script files
 * @note Called from CL_ParseScriptSecond
 * @sa CL_ParseScriptSecond
 */
void CL_ParseShaders (const char *name, char **text)
{
	shader_t *entry;
	const value_t *v;
	const char *errhead = "CL_ParseShaders: unexptected end of file (names ";
	char *token;

	/* get name list body body */
	token = COM_Parse(text);
	if (!*text || *token != '{') {
		Com_Printf("CL_ParseShaders: shader \"%s\" without body ignored\n", name);
		return;
	}

	/* new entry */
	entry = &r_shaders[r_numshaders++];
	memset(entry, 0, sizeof(shader_t));

	/* default value */
	entry->glMode = BLEND_FILTER;

	Q_strncpyz(entry->name, name, sizeof(entry->name));
	do {
		/* get the name type */
		token = COM_EParse(text, errhead, name);
		if (!*text)
			break;
		if (*token == '}')
			break;

		/* get values */
		for (v = shader_values; v->string; v++)
			if (!Q_strcmp(token, v->string)) {
				/* found a definition */
				token = COM_EParse(text, errhead, name);
				if (!*text)
					return;

				if (v->ofs && v->type != V_NULL)
					Com_ParseValue(entry, token, v->type, v->ofs);
				break;
			}

		if (!v->string)
			Com_Printf("CL_ParseShaders: unknown token \"%s\" ignored (entry %s)\n", token, name);

	} while (*text);
}

/**
 * @brief List all loaded shaders via console
 */
void CL_ShaderList_f (void)
{
	int i;

	for (i = 0; i < r_numshaders; i++) {
		Com_Printf("Shader %s\n", r_shaders[i].name);
		Com_Printf("..filename: %s\n", r_shaders[i].filename);
		Com_Printf("..frag %i\n", (int) r_shaders[i].frag);
		Com_Printf("..vertex %i\n", (int) r_shaders[i].vertex);
		Com_Printf("..emboss %i\n", (int) r_shaders[i].emboss);
		Com_Printf("..emboss low %i\n", (int) r_shaders[i].embossLow);
		Com_Printf("..emboss high %i\n", (int) r_shaders[i].embossHigh);
		Com_Printf("..emboss #2 %i\n", (int) r_shaders[i].emboss2);
		Com_Printf("..blur %i\n", (int) r_shaders[i].blur);
		Com_Printf("..light blur %i\n", (int) r_shaders[i].light);
		Com_Printf("..edge %i\n", (int) r_shaders[i].edge);
		Com_Printf("..glMode '%s'\n", Com_ValueToStr(&r_shaders[i], V_BLEND, offsetof( shader_t, glMode)));
		Com_Printf("\n");
	}
}
