/**
 * @file scriptlib.c
 * @brief
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


#include "cmdlib.h"
#include "scriplib.h"

/*
=============================================================================
PARSING STUFF
=============================================================================
*/

typedef struct
{
	char	filename[1024];
	char    *buffer,*script_p,*end_p;
	int     line;
} script_t;

#define	MAX_INCLUDES	8
static script_t	scriptstack[MAX_INCLUDES];
static script_t	*script;
int			scriptline;

char    token[MAXTOKEN];
qboolean endofscript;
static qboolean tokenready;                     /* only true if UnGetToken was just called */

/**
 * @brief
 * @sa LoadScriptFile
 */
static void AddScriptToStack (char *filename)
{
	int size;

	script++;
	if (script == &scriptstack[MAX_INCLUDES])
		Error ("script file exceeded MAX_INCLUDES");
	strncpy (script->filename, ExpandPath (filename), sizeof(script->filename));

	size = LoadFile (script->filename, (void **)&script->buffer);

	printf ("entering %s\n", script->filename);

	script->line = 1;

	script->script_p = script->buffer;
	script->end_p = script->buffer + size;
}


/**
 * @brief
 */
void LoadScriptFile (char *filename)
{
	script = scriptstack;
	AddScriptToStack (filename);

	endofscript = qfalse;
	tokenready = qfalse;
}


/**
 * @brief
 */
void ParseFromMemory (char *buffer, int size)
{
	script = scriptstack;
	script++;
	if (script == &scriptstack[MAX_INCLUDES])
		Error ("script file exceeded MAX_INCLUDES");
	strncpy (script->filename, "memory buffer", sizeof(script->filename));

	script->buffer = buffer;
	script->line = 1;
	script->script_p = script->buffer;
	script->end_p = script->buffer + size;

	endofscript = qfalse;
	tokenready = qfalse;
}


/**
 * @brief Signals that the current token was not used, and should be reported for the next GetToken.
 * @note that
 * GetToken (qtrue);
 * UnGetToken ();
 * GetToken (qfalse);
 * could cross a line boundary.
 */
void UnGetToken (void)
{
	tokenready = qtrue;
}


/**
 * @brief
 */
qboolean EndOfScript (qboolean crossline)
{
	if (!crossline)
		Error ("Line %i is incomplete\n",scriptline);

	if (!strcmp (script->filename, "memory buffer")) {
		endofscript = qtrue;
		return qfalse;
	}

	free (script->buffer);
	if (script == scriptstack+1) {
		endofscript = qtrue;
		return qfalse;
	}
	script--;
	scriptline = script->line;
	printf ("returning to %s\n", script->filename);
	return GetToken (crossline);
}

/**
 * @brief
 */
qboolean GetToken (qboolean crossline)
{
	char *token_p;
	/* is a token allready waiting? */
	if (tokenready) {
		tokenready = qfalse;
		return qtrue;
	}

	if (script->script_p >= script->end_p)
		return EndOfScript (crossline);

	/* skip space */
skipspace:
	while (*script->script_p <= 32) {
		if (script->script_p >= script->end_p)
			return EndOfScript (crossline);
		if (*script->script_p++ == '\n') {
			if (!crossline)
				Error ("Line %i is incomplete\n",scriptline);
			scriptline = script->line++;
		}
	}

	if (script->script_p >= script->end_p)
		return EndOfScript (crossline);

	/* ; # // comments */
	if (*script->script_p == ';' || *script->script_p == '#'
		|| ( script->script_p[0] == '/' && script->script_p[1] == '/') ) {
		if (!crossline)
			Error ("Line %i is incomplete\n",scriptline);
		while (*script->script_p++ != '\n')
			if (script->script_p >= script->end_p)
				return EndOfScript (crossline);
		goto skipspace;
	}

	/* c-style comments */
	if (script->script_p[0] == '/' && script->script_p[1] == '*') {
		if (!crossline)
			Error ("Line %i is incomplete\n",scriptline);
		script->script_p+=2;
		while (script->script_p[0] != '*' && script->script_p[1] != '/') {
			script->script_p++;
			if (script->script_p >= script->end_p)
				return EndOfScript (crossline);
		}
		script->script_p += 2;
		goto skipspace;
	}

	/* copy token */
	token_p = token;

	if (*script->script_p == '"') {
		/* quoted token */
		script->script_p++;
		while (*script->script_p != '"') {
			*token_p++ = *script->script_p++;
			if (script->script_p == script->end_p)
				break;
			if (token_p == &token[MAXTOKEN])
				Error ("Token too large on line %i\n",scriptline);
		}
		script->script_p++;
	} else	/* regular token */
		while ( *script->script_p > 32 && *script->script_p != ';') {
			*token_p++ = *script->script_p++;
			if (script->script_p == script->end_p)
				break;
			if (token_p == &token[MAXTOKEN])
				Error ("Token too large on line %i\n",scriptline);
		}

	*token_p = 0;

	if (!strcmp (token, "$include")) {
		GetToken (qfalse);
		AddScriptToStack (token);
		return GetToken (crossline);
	}

	return qtrue;
}


/**
 * @brief Returns true if there is another token on the line
 */
qboolean TokenAvailable (void)
{
	char *search_p;

	search_p = script->script_p;

	if (search_p >= script->end_p)
		return qfalse;

	while ( *search_p <= 32) {
		if (*search_p == '\n')
			return qfalse;
		search_p++;
		if (search_p == script->end_p)
			return qfalse;
	}

	if (*search_p == ';')
		return qfalse;

	return qtrue;
}
