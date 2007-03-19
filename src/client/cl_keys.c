/**
 * @file keys.c
 * @brief Keyboard handling routines.
 *
 * Note: Key up events are sent even if in console mode
 */

/*
All original materal Copyright (C) 2002-2006 UFO: Alien Invasion team.

15/06/06, Eddy Cullen (ScreamingWithNoSound):
	Reformatted to agreed style.
	Added doxygen file comment.
	Updated copyright notice.

Original file from Quake 2 v3.21: quake2-2.31/client/keys.c
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

#include "client.h"

#define		MAXCMDLINE	256
char key_lines[MAXKEYLINES][MAXCMDLINE];
int key_linepos;
static qboolean shift_down = qfalse;
static int anykeydown;

static int key_insert = 1;

int edit_line = 0;
int history_line = 0;

int msg_mode;
char msg_buffer[MAXCMDLINE];
size_t msg_bufferlen = 0;

static int key_waiting;
char *keybindings[K_LAST_KEY];
static char *menukeybindings[K_LAST_KEY];
static qboolean consolekeys[K_LAST_KEY];		/* if true, can't be rebound while in console */
static int keyshift[K_LAST_KEY];				/* key to map to if shift held down in console */
static int key_repeats[K_LAST_KEY];			/* if > 1, it is autorepeating */
qboolean keydown[K_LAST_KEY];

typedef struct {
	char *name;
	int keynum;
} keyname_t;

static const keyname_t keynames[] = {
	{"TAB", K_TAB},
	{"ENTER", K_ENTER},
	{"ESCAPE", K_ESCAPE},
	{"SPACE", K_SPACE},
	{"BACKSPACE", K_BACKSPACE},
	{"UPARROW", K_UPARROW},
	{"DOWNARROW", K_DOWNARROW},
	{"LEFTARROW", K_LEFTARROW},
	{"RIGHTARROW", K_RIGHTARROW},

	{"ALT", K_ALT},
	{"CTRL", K_CTRL},
	{"SHIFT", K_SHIFT},

	{"F1", K_F1},
	{"F2", K_F2},
	{"F3", K_F3},
	{"F4", K_F4},
	{"F5", K_F5},
	{"F6", K_F6},
	{"F7", K_F7},
	{"F8", K_F8},
	{"F9", K_F9},
	{"F10", K_F10},
	{"F11", K_F11},
	{"F12", K_F12},

	{"INS", K_INS},
	{"DEL", K_DEL},
	{"PGDN", K_PGDN},
	{"PGUP", K_PGUP},
	{"HOME", K_HOME},
	{"END", K_END},

	{"MOUSE1", K_MOUSE1},
	{"MOUSE2", K_MOUSE2},
	{"MOUSE3", K_MOUSE3},
	{"MOUSE4", K_MOUSE4},
	{"MOUSE5", K_MOUSE5},

	{"AUX1", K_AUX1},
	{"AUX2", K_AUX2},
	{"AUX3", K_AUX3},
	{"AUX4", K_AUX4},
	{"AUX5", K_AUX5},
	{"AUX6", K_AUX6},
	{"AUX7", K_AUX7},
	{"AUX8", K_AUX8},
	{"AUX9", K_AUX9},
	{"AUX10", K_AUX10},
	{"AUX11", K_AUX11},
	{"AUX12", K_AUX12},
	{"AUX13", K_AUX13},
	{"AUX14", K_AUX14},
	{"AUX15", K_AUX15},
	{"AUX16", K_AUX16},
	{"AUX17", K_AUX17},
	{"AUX18", K_AUX18},
	{"AUX19", K_AUX19},
	{"AUX20", K_AUX20},
	{"AUX21", K_AUX21},
	{"AUX22", K_AUX22},
	{"AUX23", K_AUX23},
	{"AUX24", K_AUX24},
	{"AUX25", K_AUX25},
	{"AUX26", K_AUX26},
	{"AUX27", K_AUX27},
	{"AUX28", K_AUX28},
	{"AUX29", K_AUX29},
	{"AUX30", K_AUX30},
	{"AUX31", K_AUX31},
	{"AUX32", K_AUX32},

	{"KP_HOME", K_KP_HOME},
	{"KP_UPARROW", K_KP_UPARROW},
	{"KP_PGUP", K_KP_PGUP},
	{"KP_LEFTARROW", K_KP_LEFTARROW},
	{"KP_5", K_KP_5},
	{"KP_RIGHTARROW", K_KP_RIGHTARROW},
	{"KP_END", K_KP_END},
	{"KP_DOWNARROW", K_KP_DOWNARROW},
	{"KP_PGDN", K_KP_PGDN},
	{"KP_ENTER", K_KP_ENTER},
	{"KP_INS", K_KP_INS},
	{"KP_DEL", K_KP_DEL},
	{"KP_SLASH", K_KP_SLASH},
	{"KP_MINUS", K_KP_MINUS},
	{"KP_PLUS", K_KP_PLUS},

	{"MWHEELUP", K_MWHEELUP},
	{"MWHEELDOWN", K_MWHEELDOWN},

	{"PAUSE", K_PAUSE},

	{"SEMICOLON", ';'},			/* because a raw semicolon seperates commands */

	{"WINDOWS", K_SUPER},
	{"COMPOSE", K_COMPOSE},
	{"MODE", K_MODE},
	{"HELP", K_HELP},
	{"PRINT", K_PRINT},
	{"SYSREQ", K_SYSREQ},
	{"SCROLLOCK", K_SCROLLOCK},
	{"BREAK", K_BREAK},
	{"MENU", K_MENU},
	{"POWER", K_POWER},
	{"EURO", K_EURO},
	{"UNDO", K_UNDO},

	{NULL, 0}
};

/*
==============================================================================
LINE TYPING INTO THE CONSOLE
==============================================================================
*/

/**
 * @brief Console completion for command and variables
 * @sa Key_Console
 * @sa Cmd_CompleteCommand
 * @sa Cvar_CompleteVariable
 */
static void Key_CompleteCommand (void)
{
	const char *cmd = NULL, *cvar = NULL, *use = NULL, *s;
	int cntCmd = 0, cntCvar = 0, cntParams = 0;
	char cmdLine[MAXCMDLINE] = "";
	qboolean append = qtrue;
	char *tmp;

	s = key_lines[edit_line] + 1;
	if (*s == '\\' || *s == '/')
		s++;
	if (!*s || *s == ' ')
		return;

	/* don't try to search a command or cvar if we are already in the
	 * parameter stage */
	if (strstr(s, " ")) {
		tmp = cmdLine;
		while (*s != ' ')
			*tmp++ = *s++;
		/* terminate the string at whitespace position to seperate the cmd */
		*tmp++ = '\0';
		/* get rid of the whitespace */
		s++;
		cntParams = Cmd_CompleteCommandParameters(cmdLine, s, &cmd);
		if (cntParams == 1) {
			/* append the found parameter */
			Q_strcat(cmdLine, " ", sizeof(cmdLine));
			Q_strcat(cmdLine, cmd, sizeof(cmdLine));
			append = qfalse;
			use = cmdLine;
		} else if (cntParams > 1) {
			Com_Printf("\n");
		} else
			return;
	} else {
		cntCmd = Cmd_CompleteCommand(s, &cmd);
		cntCvar = Cvar_CompleteVariable(s, &cvar);

		if (cntCmd == 1 && !cntCvar)
			use = cmd;
		else if (!cntCmd && cntCvar == 1)
			use = cvar;
		else
			Com_Printf("\n");
	}

	if (use) {
		key_lines[edit_line][1] = '/';
		Q_strncpyz(key_lines[edit_line] + 2, use, MAXCMDLINE - 2);
		key_linepos = strlen(use) + 2;
		if (append) {
			key_lines[edit_line][key_linepos] = ' ';
			key_linepos++;
		}
		key_lines[edit_line][key_linepos] = 0;
		return;
	}
}

/**
 * @brief Interactive line editing and console scrollback
 * @param[in] key
 */
static void Key_Console (int key)
{
	int i;

	switch (key) {
	case K_KP_SLASH:
		key = '/';
		break;
	case K_KP_MINUS:
		key = '-';
		break;
	case K_KP_PLUS:
		key = '+';
		break;
	case K_KP_HOME:
		key = '7';
		break;
	case K_KP_UPARROW:
		key = '8';
		break;
	case K_KP_PGUP:
		key = '9';
		break;
	case K_KP_LEFTARROW:
		key = '4';
		break;
	case K_KP_5:
		key = '5';
		break;
	case K_KP_RIGHTARROW:
		key = '6';
		break;
	case K_KP_END:
		key = '1';
		break;
	case K_KP_DOWNARROW:
		key = '2';
		break;
	case K_KP_PGDN:
		key = '3';
		break;
	case K_KP_INS:
		key = '0';
		break;
	case K_KP_DEL:
		key = '.';
		break;
	default:
		break;
	}

	/* ctrl-V gets clipboard data */
	if ((toupper(key) == 'V' && keydown[K_CTRL]) || (((key == K_INS) || (key == K_KP_INS)) && keydown[K_SHIFT])) {
		char *cbd;

		if ((cbd = Sys_GetClipboardData()) != 0) {
			int i;

			strtok(cbd, "\n\r\b");

			i = strlen(cbd);
			if (i + key_linepos >= MAXCMDLINE)
				i = MAXCMDLINE - key_linepos;

			if (i > 0) {
				cbd[i] = 0;
				Q_strcat(key_lines[edit_line], cbd, MAXCMDLINE);
				key_linepos += i;
			}
			free(cbd);
		}

		return;
	}

	if (keydown[K_CTRL]) {
		switch (toupper(key)) {
		/* ctrl-L clears screen */
		case 'L':
			Cbuf_AddText("clear\n");
			return;
		/* jump to beginning of line */
		case 'A':
			key_linepos = 1;
			return;
		case 'E':
			/* end of line */
			key_linepos = strlen(key_lines[edit_line]);
			return;
		}
	}

	if (key == K_ENTER || key == K_KP_ENTER) {	/* backslash text are commands, else chat */
		if (key_lines[edit_line][1] == '\\' || key_lines[edit_line][1] == '/')
			Cbuf_AddText(key_lines[edit_line] + 2);	/* skip the > */
		/* no command - just enter */
		else if (!key_lines[edit_line][1])
			return;
		else
			Cbuf_AddText(key_lines[edit_line] + 1);	/* valid command */

		Cbuf_AddText("\n");
		Com_Printf("%s\n", key_lines[edit_line]);
		edit_line = (edit_line + 1) & (MAXKEYLINES-1);
		history_line = edit_line;
		key_lines[edit_line][0] = ']';
		/**
		 * maybe MAXKEYLINES was reached - we don't want to spawn 'random' strings
		 * from history buffer in our console
		 */
		key_lines[edit_line][1] = '\0';
		key_linepos = 1;
		if (cls.state == ca_disconnected)
			SCR_UpdateScreen();	/* force an update, because the command */
		/* may take some time */
		return;
	}

	/* command completion */
	if (key == K_TAB) {
		Key_CompleteCommand();
		return;
	}

	if (key == K_BACKSPACE || ((key == 'h') && (keydown[K_CTRL]))) {
		if (key_linepos > 1) {
			strcpy(key_lines[edit_line] + key_linepos - 1, key_lines[edit_line] + key_linepos);
			key_linepos--;
		}
		return;
	}
	/* delete char on cursor */
	if (key == K_DEL) {
		if (key_linepos < strlen(key_lines[edit_line]))
			strcpy(key_lines[edit_line] + key_linepos, key_lines[edit_line] + key_linepos + 1);
		return;
	}

	if (key == K_UPARROW || key == K_KP_UPARROW || ((tolower(key) == 'p') && keydown[K_CTRL])) {
		do {
			history_line = (history_line - 1) & (MAXKEYLINES-1);
		} while (history_line != edit_line && !key_lines[history_line][1]);

		if (history_line == edit_line)
			history_line = (edit_line + 1) & (MAXKEYLINES-1);

		Q_strncpyz(key_lines[edit_line], key_lines[history_line], MAXCMDLINE);
		key_linepos = strlen(key_lines[edit_line]);
		return;
	} else if (key == K_DOWNARROW || key == K_KP_DOWNARROW || ((tolower(key) == 'n') && keydown[K_CTRL])) {
		if (history_line == edit_line)
			return;
		do {
			history_line = (history_line + 1) & (MAXKEYLINES-1);
		} while (history_line != edit_line && !key_lines[history_line][1]);

		if (history_line == edit_line) {
			key_lines[edit_line][0] = ']';
			/* fresh edit line */
			key_lines[edit_line][1] = '\0';
			key_linepos = 1;
		} else {
			Q_strncpyz(key_lines[edit_line], key_lines[history_line], MAXCMDLINE);
			key_linepos = strlen(key_lines[edit_line]);
		}
		return;
	}

	if (key == K_LEFTARROW){  /* move cursor left */
		if (keydown[K_CTRL]) { /* by a whole word */
			while (key_linepos > 1 && key_lines[edit_line][key_linepos - 1] == ' ')
				key_linepos--;  /* get off current word */
			while (key_linepos > 1 && key_lines[edit_line][key_linepos - 1] != ' ')
				key_linepos--;  /* and behind previous word */
			return;
		}

		if(key_linepos > 1)  /* or just a char */
			key_linepos--;
		return;
	} else if (key == K_RIGHTARROW) {  /* move cursor right */
		if ((i = strlen(key_lines[edit_line])) == key_linepos)
			return; /* no character to get */
		if (keydown[K_CTRL]){  /* by a whole word */
			while (key_linepos < i && key_lines[edit_line][key_linepos + 1] == ' ')
				key_linepos++;  /* get off current word */
			while (key_linepos < i && key_lines[edit_line][key_linepos + 1] != ' ')
				key_linepos++;  /* and in front of next word */
			if (key_linepos < i)  /* all the way in front */
				key_linepos++;
			return;
		}
		key_linepos++;  /* or just a char */
		return;
	}

	/* toggle insert mode */
	if (key == K_INS) {
		key_insert ^= 1;
		return;
	}

	if (key == K_PGUP || key == K_KP_PGUP || key == K_MWHEELUP) {
		con.display -= 2;
		return;
	}

	if (key == K_PGDN || key == K_KP_PGDN || key == K_MWHEELDOWN) {
		con.display += 2;
		if (con.display > con.current)
			con.display = con.current;
		return;
	}

	if (key == K_HOME || key == K_KP_HOME) {
		key_linepos = 1;
		return;
	}

	if (key == K_END || key == K_KP_END) {
		key_linepos = strlen(key_lines[edit_line]);
		return;
	}

	if (key < 32 || key > 127)
		return;					/* non printable */

	if (key_linepos < MAXCMDLINE - 1) {
		int i;

		if (key_insert) {  /* can't do strcpy to move string to right */
			i = strlen(key_lines[edit_line]) - 1;

			if (i == MAXCMDLINE - 2)
				i--;
			for (; i >= key_linepos; i--)
				key_lines[edit_line][i + 1] = key_lines[edit_line][i];
		}
		i = key_lines[edit_line][key_linepos];
		key_lines[edit_line][key_linepos] = key;
		key_linepos++;
		if (!i)  /* only null terminate if at the end */
			key_lines[edit_line][key_linepos] = 0;
	}
}

/**
 * @brief Handles input when cls.key_dest == key_message
 * @note Used for chatting and cvar editing via menu
 * @sa Key_Event
 * @sa MN_Click
 * @sa CL_MessageMenu_f
 */
static void Key_Message (int key)
{
	if (key == K_ENTER || key == K_KP_ENTER) {
		qboolean send = qtrue;

		switch (msg_mode) {
		case MSG_SAY:
			if (msg_buffer[0])
				Cbuf_AddText("say \"");
			else
				send = qfalse;
			break;
		case MSG_SAY_TEAM:
			if (msg_buffer[0])
				Cbuf_AddText("say_team \"");
			else
				send = qfalse;
			break;
		case MSG_MENU:
			/* end the editing (don't cancel) */
			Cbuf_AddText("msgmenu \":");
			break;
		}
		if (send) {
			Cbuf_AddText(msg_buffer);
			Cbuf_AddText("\"\n");
		}

		cls.key_dest = key_game;
		msg_bufferlen = 0;
		msg_buffer[0] = 0;
		return;
	}

	if (key == K_ESCAPE) {
		cls.key_dest = key_game;
		msg_bufferlen = 0;
		msg_buffer[0] = 0;
		/* cancel the inline cvar editing */
		if (msg_mode == MSG_MENU)
			Cbuf_AddText("msgmenu !");
		return;
	}

	if (key < 32 || key > 127)
		return;					/* non printable */

	if (key == K_BACKSPACE) {
		if (msg_bufferlen) {
			msg_bufferlen--;
			msg_buffer[msg_bufferlen] = 0;
			if (msg_mode == MSG_MENU)
				Cbuf_AddText(va("msgmenu \"%s\"\n", msg_buffer));
		}
		return;
	}

	if (msg_bufferlen == sizeof(msg_buffer) - 1)
		return;					/* all full */

	/* limit the length for cvar inline editing */
	if (msg_mode != MSG_MENU || msg_bufferlen < MAX_CVAR_EDITING_LENGTH) {
		msg_buffer[msg_bufferlen++] = key;
		msg_buffer[msg_bufferlen] = 0;
	}

	if (msg_mode == MSG_MENU)
		Cbuf_AddText(va("msgmenu \"%s\"\n", msg_buffer));
}


/**
 * @brief Convert to given string to keynum
 * @param[in] str The keystring to convert to keynum
 * @return a key number to be used to index keybindings[] by looking at
 * the given string.  Single ascii characters return themselves, while
 * the K_* names are matched up.
 */
static int Key_StringToKeynum (const char *str)
{
	const keyname_t *kn;

	if (!str || !str[0])
		return -1;
	if (!str[1])
		return str[0];

	for (kn = keynames; kn->name; kn++) {
		if (!Q_strcasecmp(str, kn->name))
			return kn->keynum;
	}
	return -1;
}

/**
 * @brief Convert a given keynum to string
 * @param[in] keynum The keynum to convert to string
 * @return a string (either a single ascii char, or a K_* name) for the given keynum.
 * FIXME: handle quote special (general escape sequence?)
 */
char *Key_KeynumToString (int keynum)
{
	const keyname_t *kn;
	static char tinystr[2];

	if (keynum == -1)
		return "<KEY NOT FOUND>";
	if (keynum > 32 && keynum < 127) {	/* printable ascii */
		tinystr[0] = keynum;
		tinystr[1] = 0;
		return tinystr;
	}

	for (kn = keynames; kn->name; kn++)
		if (keynum == kn->keynum)
			return kn->name;

	return "<UNKNOWN KEYNUM>";
}

/**
 * @brief Return the key binding for a given script command
 * @param[in] binding The script command to bind keynum to
 * @sa Key_SetBinding
 * @return the binded key or empty string if not found
 */
char* Key_GetBinding (const char *binding, keyBindSpace_t space)
{
	int i;
	char **keySpace = NULL;

	switch (space) {
	case KEYSPACE_MENU:
		keySpace = menukeybindings;
		break;
	case KEYSPACE_GAME:
		keySpace = keybindings;
		break;
	default:
		return "";
	}

	for (i = 0; i < K_LAST_KEY; i++)
		if (keySpace[i] && *keySpace[i] && !Q_strncmp(keySpace[i], binding, strlen(binding))) {
			return Key_KeynumToString(i);
		}

	/* not found */
	return "";
}

/**
 * @brief Bind a keynum to script command
 * @param[in] keynum Converted from string to keynum
 * @param[in] binding The script command to bind keynum to
 * @sa Key_Bind_f
 * @sa Key_StringToKeynum
 */
static void Key_SetBinding (int keynum, char *binding, keyBindSpace_t space)
{
	char *new;
	char **keySpace = NULL;
	int l;

	if (keynum == -1)
		return;

	Com_DPrintf("Binding for '%s' for space ", binding);
	switch (space) {
	case KEYSPACE_MENU:
		keySpace = &menukeybindings[keynum];
		Com_DPrintf("menu\n");
		break;
	case KEYSPACE_GAME:
		keySpace = &keybindings[keynum];
		Com_DPrintf("game\n");
		break;
	default:
		Com_DPrintf("failure\n");
		return;
	}

	/* free old bindings */
	if (keySpace) {
		Mem_Free(*keySpace);
		*keySpace = NULL;
	}

	/* allocate memory for new binding */
	l = strlen(binding);
	new = Mem_Alloc(l + 1);
	Q_strncpyz(new, binding, l + 1);
	*keySpace = new;
}

/**
 * @brief Unbind a given key binding
 * @sa Key_SetBinding
 */
static void Key_Unbind_f (void)
{
	int b;

	if (Cmd_Argc() != 2) {
		Com_Printf("Usage: unbind <key> : remove commands from a key\n");
		return;
	}

	b = Key_StringToKeynum(Cmd_Argv(1));
	if (b == -1) {
		Com_Printf("\"%s\" isn't a valid key\n", Cmd_Argv(1));
		return;
	}

	if (!Q_strncmp(Cmd_Argv(0), "unbindmenu", MAX_VAR))
		Key_SetBinding(b, "", KEYSPACE_MENU);
	else
		Key_SetBinding(b, "", KEYSPACE_GAME);
}

#if 0
/**
 * @brief Translate the key space string to enum value of keyBindSpace_t
 */
static int Key_SpaceStringToEnumValue (char* keySpace)
{
	if (Q_strncmp(keySpace, "KEYSPACE_MENU", MAX_VAR))
		return KEYSPACE_MENU;
	else if (Q_strncmp(keySpace, "KEYSPACE_GAME", MAX_VAR))
		return KEYSPACE_MENU;
	else
		return -1;
}
#endif

/**
 * @brief Unbind all key bindings
 * @sa Key_SetBinding
 */
static void Key_Unbindall_f (void)
{
	int i;

	for (i = 0; i < K_LAST_KEY; i++)
		if (keybindings[i]) {
			if (!Q_strncmp(Cmd_Argv(0), "unbindallmenu", MAX_VAR))
				Key_SetBinding(i, "", KEYSPACE_MENU);
			else
				Key_SetBinding(i, "", KEYSPACE_GAME);
		}
}


/**
 * @brief Binds a key to a given script command
 * @sa Key_SetBinding
 */
static void Key_Bind_f (void)
{
	int i, c, b;
	char cmd[1024];

	c = Cmd_Argc();

	if (c < 2) {
		Com_Printf("Usage: bind <key> [command] : attach a command to a key\n");
		return;
	}
	b = Key_StringToKeynum(Cmd_Argv(1));
	if (b == -1) {
		Com_Printf("\"%s\" isn't a valid key\n", Cmd_Argv(1));
		return;
	}

	if (c == 2) {
		if (keybindings[b])
			Com_Printf("\"%s\" = \"%s\"\n", Cmd_Argv(1), keybindings[b]);
		else
			Com_Printf("\"%s\" is not bound\n", Cmd_Argv(1));
		return;
	}

	/* copy the rest of the command line */
	cmd[0] = '\0';					/* start out with a null string */
	for (i = 2; i < c; i++) {
		Q_strcat(cmd, Cmd_Argv(i), sizeof(cmd));
		if (i != (c - 1))
			Q_strcat(cmd, " ", sizeof(cmd));
	}

	if (!Q_strncmp(Cmd_Argv(0), "bindmenu", MAX_VAR))
		Key_SetBinding(b, cmd, KEYSPACE_MENU);
	else
		Key_SetBinding(b, cmd, KEYSPACE_GAME);
}

#if 0
/**
 * @brief Writes lines containing "bind key value"
 * @param[in] path path to print the keybinding too
 */
void Key_WriteBindings (char* path)
{
	int i;
	FILE *f;

	f = fopen(path, "w");
	if (!f)
		return;

	fprintf(f, "// generated by ufo, do not modify\n");
	fprintf(f, "unbindallmenu\n");
	fprintf(f, "unbindall\n");
	for (i = 0; i < K_LAST_KEY; i++)
		if (menukeybindings[i] && menukeybindings[i][0])
			fprintf(f, "bindmenu %s \"%s\"\n", Key_KeynumToString(i), keybindings[i]);
	for (i = 0; i < K_LAST_KEY; i++)
		if (keybindings[i] && keybindings[i][0])
			fprintf(f, "bind %s \"%s\"\n", Key_KeynumToString(i), keybindings[i]);
	fclose(f);
}
#endif

/**
 * @brief List all binded keys with its function
 */
static void Key_Bindlist_f (void)
{
	int i;

	Com_Printf("key space: game\n");
	for (i = 0; i < K_LAST_KEY; i++)
		if (keybindings[i] && keybindings[i][0])
			Com_Printf("- %s \"%s\"\n", Key_KeynumToString(i), keybindings[i]);
	Com_Printf("key space: menu\n");
	for (i = 0; i < K_LAST_KEY; i++)
		if (menukeybindings[i] && menukeybindings[i][0])
			Com_Printf("- %s \"%s\"\n", Key_KeynumToString(i), menukeybindings[i]);
}


/**
 * @brief
 * @todo Fix this crappy shift key assignment for win32 and sdl (no _)
 */
void Key_Init (void)
{
	int i;

	for (i = 0; i < MAXKEYLINES; i++) {
		key_lines[i][0] = ']';
		key_lines[i][1] = 0;
	}
	key_linepos = 1;

	/* init ascii characters in console mode */
	for (i = 32; i < 128; i++)
		consolekeys[i] = qtrue;
	consolekeys[K_ENTER] = qtrue;
	consolekeys[K_KP_ENTER] = qtrue;
	consolekeys[K_TAB] = qtrue;
	consolekeys[K_LEFTARROW] = qtrue;
	consolekeys[K_KP_LEFTARROW] = qtrue;
	consolekeys[K_RIGHTARROW] = qtrue;
	consolekeys[K_KP_RIGHTARROW] = qtrue;
	consolekeys[K_UPARROW] = qtrue;
	consolekeys[K_KP_UPARROW] = qtrue;
	consolekeys[K_DOWNARROW] = qtrue;
	consolekeys[K_KP_DOWNARROW] = qtrue;
	consolekeys[K_BACKSPACE] = qtrue;
	consolekeys[K_HOME] = qtrue;
	consolekeys[K_KP_HOME] = qtrue;
	consolekeys[K_END] = qtrue;
	consolekeys[K_KP_END] = qtrue;
	consolekeys[K_PGUP] = qtrue;
	consolekeys[K_KP_PGUP] = qtrue;
	consolekeys[K_PGDN] = qtrue;
	consolekeys[K_KP_PGDN] = qtrue;
	consolekeys[K_SHIFT] = qtrue;
	consolekeys[K_INS] = qtrue;
	consolekeys[K_KP_INS] = qtrue;
	consolekeys[K_KP_DEL] = qtrue;
	consolekeys[K_KP_SLASH] = qtrue;
	consolekeys[K_KP_PLUS] = qtrue;
	consolekeys[K_KP_MINUS] = qtrue;
	consolekeys[K_KP_5] = qtrue;
	consolekeys[K_MWHEELUP] = qtrue;
	consolekeys[K_MWHEELDOWN] = qtrue;

	consolekeys['`'] = qfalse;
	consolekeys['~'] = qfalse;

	for (i = 0; i < K_LAST_KEY; i++)
		keyshift[i] = i;
	for (i = 'a'; i <= 'z'; i++)
		keyshift[i] = i - 'a' + 'A';

#if _WIN32
	keyshift['1'] = '!';
	keyshift['2'] = '@';
	keyshift['3'] = '#';
	keyshift['4'] = '$';
	keyshift['5'] = '%';
	keyshift['6'] = '^';
	keyshift['7'] = '&';
	keyshift['8'] = '*';
	keyshift['9'] = '(';
	keyshift['0'] = ')';
	keyshift['-'] = '_';
	keyshift['='] = '+';
	keyshift[','] = '<';
	keyshift['.'] = '>';
	keyshift['/'] = '?';
	keyshift[';'] = ':';
	keyshift['\''] = '"';
	keyshift['['] = '{';
	keyshift[']'] = '}';
	keyshift['`'] = '~';
	keyshift['\\'] = '|';
#endif

	/* register our functions */
	Cmd_AddCommand("bindmenu", Key_Bind_f, NULL);
	Cmd_AddCommand("bind", Key_Bind_f, NULL);
	Cmd_AddCommand("unbindmenu", Key_Unbind_f, NULL);
	Cmd_AddCommand("unbind", Key_Unbind_f, NULL);
	Cmd_AddCommand("unbindallmenu", Key_Unbindall_f, NULL);
	Cmd_AddCommand("unbindall", Key_Unbindall_f, NULL);
	Cmd_AddCommand("bindlist", Key_Bindlist_f, NULL);
}

/**
 * @brief Called by the system between frames for both key up and key down events
 * @note Should NOT be called during an interrupt!
 * @sa Key_Message
 */
void Key_Event (int key, qboolean down, unsigned time)
{
	char *kb = NULL;
	char cmd[MAX_STRING_CHARS];

	/* hack for modal presses */
	if (key_waiting == -1) {
		if (down)
			key_waiting = key;
		return;
	}

	/* update auto-repeat status */
	if (down) {
		key_repeats[key]++;
		if (key != K_BACKSPACE && key != K_PAUSE && key != K_PGUP && key != K_KP_PGUP && key != K_PGDN && key != K_KP_PGDN && key_repeats[key] > 1)
			return;				/* ignore most autorepeats */
#if 0
		if (key >= K_LAST_KEY && !keybindings[key])
			Com_Printf("%s is unbound, hit F4 to set.\n", Key_KeynumToString(key));
#endif
	} else {
		key_repeats[key] = 0;
	}

	if (key == K_SHIFT)
		shift_down = down;

	/* console key is hardcoded, so the user can never unbind it */
	if (key == '`' || key == '~' || (key == K_ESCAPE && shift_down)) {
		if (!down)
			return;
		Con_ToggleConsole_f();
		return;
	}

	/* any key during the attract mode will bring up the menu */
	if (cl.attractloop && !(key >= K_F1 && key <= K_F12))
		key = K_ESCAPE;

	/* menu key is hardcoded, so the user can never unbind it */
	if (key == K_ESCAPE) {
		if (!down)
			return;

		switch (cls.key_dest) {
		case key_message:
			Key_Message(key);
			break;
		case key_irc:
		case key_game:
			Cbuf_AddText("mn_pop esc;");
			break;
		case key_console:
			Con_ToggleConsole_f();
			break;
		default:
			Com_Error(ERR_FATAL, "Bad cls.key_dest");
		}
		return;
	}

	/* track if any key is down for BUTTON_ANY */
	keydown[key] = down;
	if (down) {
		if (key_repeats[key] == 1)
			anykeydown++;
	} else {
		anykeydown--;
		if (anykeydown < 0)
			anykeydown = 0;
	}

	/* key up events only generate commands if the game key binding is */
	/* a button command (leading + sign).  These will occur even in console mode, */
	/* to keep the character from continuing an action started before a console */
	/* switch.  Button commands include the kenum as a parameter, so multiple */
	/* downs can be matched with ups */
	if (!down) {
		kb = keybindings[key];
		if (kb && kb[0] == '+') {
			Com_sprintf(cmd, sizeof(cmd), "-%s %i %i\n", kb + 1, key, time);
			Cbuf_AddText(cmd);
		}
		if (keyshift[key] != key) {
			kb = keybindings[keyshift[key]];
			if (kb && kb[0] == '+') {
				Com_sprintf(cmd, sizeof(cmd), "-%s %i %i\n", kb + 1, key, time);
				Cbuf_AddText(cmd);
			}
		}
		return;
	}

	/* if not a consolekey, send to the interpreter no matter what mode is */
	if (cls.key_dest == key_game) {	/*&& !consolekeys[key] ) */
		if (mouseSpace == MS_MENU)
			kb = menukeybindings[key];
		if (!kb)
			kb = keybindings[key];
		if (kb) {
			if (kb[0] == '+') {	/* button commands add keynum and time as a parm */
				Com_sprintf(cmd, sizeof(cmd), "%s %i %i\n", kb, key, time);
				Cbuf_AddText(cmd);
			} else {
				Cbuf_AddText(kb);
				Cbuf_AddText("\n");
			}
		}
		return;
	}

	if (!down)
		return;					/* other systems only care about key down events */

	if (shift_down)
		key = keyshift[key];

	Irc_Input_KeyEvent(key);

	switch (cls.key_dest) {
	case key_irc:
	case key_message:
		Key_Message(key);
		break;
	case key_game:
	case key_console:
		Key_Console(key);
		break;
	default:
		Com_Error(ERR_FATAL, "Bad cls.key_dest");
	}
}

/**
 * @brief
 */
void Key_ClearStates (void)
{
	int i;

	anykeydown = qfalse;

	for (i = 0; i < K_LAST_KEY; i++) {
		if (keydown[i] || key_repeats[i])
			Key_Event(i, qfalse, 0);
		keydown[i] = 0;
		key_repeats[i] = 0;
	}
}


/**
 * @brief
 */
int Key_GetKey (void)
{
	key_waiting = -1;

	while (key_waiting == -1)
		Sys_SendKeyEvents();

	return key_waiting;
}
