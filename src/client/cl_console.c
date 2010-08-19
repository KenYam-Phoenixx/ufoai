/**
 * @file cl_console.c
 * @brief Console related code.
 */

/*
All original material Copyright (C) 2002-2010 UFO: Alien Invasion.

Original file from Quake 2 v3.21: quake2-2.31/client/console.c
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

#include "cl_console.h"
#include "client.h"
#include "cl_game.h"
#include "input/cl_keys.h"
#include "renderer/r_draw.h"

#define CONSOLE_CHAR_ALIGN 4
#define NUM_CON_TIMES 8
#define COLORED_TEXT_MASK 128
#define CON_TEXTSIZE 32768
#define CONSOLE_CURSOR_CHAR 11
#define CONSOLE_HISTORY_FILENAME "history"

typedef struct {
	qboolean initialized;

	char text[CON_TEXTSIZE];
	int currentLine;			/**< line where next message will be printed */
	int pos;					/**< offset in current line for next print */
	int displayLine;			/**< bottom of console displays this line */

	int lineWidth;				/**< characters across screen */
	int totalLines;				/**< total lines in console scrollback */

	int visLines;

	float times[NUM_CON_TIMES];	/**< cls.realtime time the line was generated
								 * for transparent notify lines */
} console_t;

static console_t con;
static cvar_t *con_notifytime;
static cvar_t *con_history;
static cvar_t *con_background;
const int con_fontHeight = 12;
const int con_fontWidth = 10;
const int con_fontShift = 3;

static void Con_DisplayString (int x, int y, const char *s)
{
	while (*s) {
		R_DrawChar(x, y, *s);
		x += con_fontWidth;
		s++;
	}
}

static void Key_ClearTyping (void)
{
	keyLines[editLine][1] = 0;	/* clear any typing */
	keyLinePos = 1;
}

void Con_ToggleConsole_f (void)
{
	Key_ClearTyping();
	Con_ClearNotify();

	if (cls.keyDest == key_console) {
		Key_SetDest(key_game);
	} else {
		Key_SetDest(key_console);
	}
}

static void Con_ToggleChat_f (void)
{
	Key_ClearTyping();

	if (cls.keyDest == key_console) {
		if (cls.state == ca_active)
			Key_SetDest(key_game);
	} else
		Key_SetDest(key_console);

	Con_ClearNotify();
}

/**
 * @brief Clears the console buffer
 */
static void Con_Clear_f (void)
{
	memset(con.text, ' ', sizeof(con.text));
}

/**
 * @brief Scrolls the console
 * @param[in] scroll Lines to scroll
 */
void Con_Scroll (int scroll)
{
	con.displayLine += scroll;
	if (con.displayLine > con.currentLine)
		con.displayLine = con.currentLine;
	else if (con.displayLine < 0)
		con.displayLine = 0;
}

/**
 * @brief Clear the notify times to ensure that every message will disappear from screen.
 */
void Con_ClearNotify (void)
{
	int i;

	for (i = 0; i < NUM_CON_TIMES; i++)
		con.times[i] = 0;
}

static void Con_MessageModeSay_f (void)
{
	/* chatting makes only sense in battle field multiplayer mode */
	if (!CL_OnBattlescape() || GAME_IsSingleplayer())
		return;

	msgMode = MSG_SAY;
	Key_SetDest(key_message);
}

static void Con_MessageModeSayTeam_f (void)
{
	/* chatting makes only sense in battle field multiplayer mode */
	if (!CL_OnBattlescape() || GAME_IsSingleplayer())
		return;

	msgMode = MSG_SAY_TEAM;
	Key_SetDest(key_message);
}

/**
 * @brief If the line width has changed, reformat the buffer.
 */
void Con_CheckResize (void)
{
	int i, j, oldWidth, oldTotalLines, numLines, numChars;
	char tbuf[CON_TEXTSIZE];
	const int width = (viddef.width >> con_fontShift);

	if (width == con.lineWidth)
		return;

	oldWidth = con.lineWidth;
	con.lineWidth = width;
	oldTotalLines = con.totalLines;
	con.totalLines = CON_TEXTSIZE / con.lineWidth;
	numLines = oldTotalLines;

	if (con.totalLines < numLines)
		numLines = con.totalLines;

	numChars = oldWidth;

	if (con.lineWidth < numChars)
		numChars = con.lineWidth;

	memcpy(tbuf, con.text, sizeof(tbuf));
	memset(con.text, ' ', sizeof(con.text));

	for (i = 0; i < numLines; i++) {
		for (j = 0; j < numChars; j++) {
			con.text[(con.totalLines - 1 - i) * con.lineWidth + j] = tbuf[((con.currentLine - i + oldTotalLines) % oldTotalLines) * oldWidth + j];
		}
	}

	Con_ClearNotify();

	con.currentLine = con.totalLines - 1;
	con.displayLine = con.currentLine;
}

/**
 * @brief Load the console history
 * @sa Con_SaveConsoleHistory
 */
void Con_LoadConsoleHistory (void)
{
	qFILE f;
	char line[MAXCMDLINE];

	if (!con_history->integer)
		return;

	memset(&f, 0, sizeof(f));

	FS_OpenFile(CONSOLE_HISTORY_FILENAME, &f, FILE_READ);
	if (!f.f)
		return;

	/* we have to skip the initial line char and the string end char */
	while (fgets(line, MAXCMDLINE - 2, f.f)) {
		if (line[strlen(line) - 1] == '\n')
			line[strlen(line) - 1] = 0;
		Q_strncpyz(&keyLines[editLine][1], line, MAXCMDLINE - 1);
		editLine = (editLine + 1) % MAXKEYLINES;
		historyLine = editLine;
		keyLines[editLine][1] = 0;
	}

	FS_CloseFile(&f);
}

/**
 * @brief Stores the console history
 * @sa Con_LoadConsoleHistory
 */
void Con_SaveConsoleHistory (void)
{
	int i;
	qFILE f;
	const char *lastLine = NULL;

	/* maybe con_history is not initialized here (early Sys_Error) */
	if (!con_history || !con_history->integer)
		return;

	FS_OpenFile(CONSOLE_HISTORY_FILENAME, &f, FILE_WRITE);
	if (!f.f) {
		Com_Printf("Can not open "CONSOLE_HISTORY_FILENAME" for writing\n");
		return;
	}

	for (i = 0; i < historyLine; i++) {
		if (lastLine && !strncmp(lastLine, &(keyLines[i][1]), MAXCMDLINE - 1))
			continue;

		lastLine = &(keyLines[i][1]);
		if (*lastLine) {
			FS_Write(lastLine, strlen(lastLine), &f);
			FS_Write("\n", 1, &f);
		}
	}
	FS_CloseFile(&f);
}

void Con_Init (void)
{
	Com_Printf("\n----- console initialization -------\n");

	/* register our commands and cvars */
	con_notifytime = Cvar_Get("con_notifytime", "10", CVAR_ARCHIVE, "How many seconds console messages should be shown before they fade away");
	con_history = Cvar_Get("con_history", "1", CVAR_ARCHIVE, "Permanent console history");
	con_background = Cvar_Get("con_background", "1", CVAR_ARCHIVE, "Console is rendered with background image");

	Cmd_AddCommand("toggleconsole", Con_ToggleConsole_f, _("Bring up the in-game console"));
	Cmd_AddCommand("togglechat", Con_ToggleChat_f, NULL);
	Cmd_AddCommand("messagesay", Con_MessageModeSay_f, _("Send a message to all players"));
	Cmd_AddCommand("messagesayteam", Con_MessageModeSayTeam_f, _("Send a message to allied team members"));
	Cmd_AddCommand("clear", Con_Clear_f, "Clear console text");

	/* load console history if con_history is true */
	Con_LoadConsoleHistory();

	memset(&con, 0, sizeof(con));
	con.lineWidth = VID_NORM_WIDTH / con_fontWidth;
	con.totalLines = sizeof(con.text) / con.lineWidth;
	con.initialized = qtrue;

	Com_Printf("Console initialized.\n");
}


static void Con_Linefeed (void)
{
	con.pos = 0;
	if (con.displayLine == con.currentLine)
		con.displayLine++;
	con.currentLine++;
	memset(&con.text[(con.currentLine % con.totalLines) * con.lineWidth],' ', con.lineWidth);
}

/**
 * @brief Handles cursor positioning, line wrapping, etc
 * All console printing must go through this in order to be logged to disk
 * If no console is visible, the text will appear at the top of the game window
 * @sa Sys_ConsoleOutput
 */
void Con_Print (const char *txt)
{
	int y;
	int c, l;
	static int cr;
	int mask;

	if (!con.initialized)
		return;

	if (txt[0] == 1 || txt[0] == 2) {
		mask = CONSOLE_COLORED_TEXT_MASK; /* go to colored text */
		txt++;
	} else
		mask = 0;

	while ((c = *txt) != 0) {
		/* count word length */
		for (l = 0; l < con.lineWidth; l++)
			if (txt[l] <= ' ')
				break;

		/* word wrap */
		if (l != con.lineWidth && (con.pos + l > con.lineWidth))
			con.pos = 0;

		txt++;

		if (cr) {
			con.currentLine--;
			cr = qfalse;
		}

		if (!con.pos) {
			Con_Linefeed();
			/* mark time for transparent overlay */
			if (con.currentLine >= 0)
				con.times[con.currentLine % NUM_CON_TIMES] = CL_Milliseconds();
		}

		switch (c) {
		case '\n':
			con.pos = 0;
			break;

		case '\r':
			con.pos = 0;
			cr = 1;
			break;

		default:	/* display character and advance */
			y = con.currentLine % con.totalLines;
			con.text[y * con.lineWidth + con.pos] = c | mask;
			con.pos++;
			if (con.pos >= con.lineWidth)
				con.pos = 0;
			break;
		}
	}
}

/*
==============================================================================
DRAWING
==============================================================================
*/

/**
 * @brief Hide the gameconsole if active
 */
void Con_Close (void)
{
	if (cls.keyDest == key_console)
		Key_SetDest(key_game);
}

/**
 * @brief The input line scrolls horizontally if typing goes beyond the right edge
 */
static void Con_DrawInput (void)
{
	int y;
	int i;
	char editlinecopy[MAXCMDLINE], *text;

	if (cls.keyDest != key_console && cls.state == ca_active)
		return;					/* don't draw anything (always draw if not active) */

	Q_strncpyz(editlinecopy, keyLines[editLine], sizeof(editlinecopy));
	text = editlinecopy;
	y = strlen(text);

	/* add the cursor frame */
	if ((int)(CL_Milliseconds() >> 8) & 1) {
		text[keyLinePos] = CONSOLE_CURSOR_CHAR | CONSOLE_COLORED_TEXT_MASK;
		if (keyLinePos == y)
			y++;
	}

	/* fill out remainder with spaces */
	for (i = y; i < MAXCMDLINE; i++)
		text[i] = ' ';

	/* prestep if horizontally scrolling */
	if (keyLinePos >= con.lineWidth)
		text += 1 + keyLinePos - con.lineWidth;

	/* draw it */
	y = con.visLines - con_fontHeight;

	for (i = 0; i < con.lineWidth; i++)
		R_DrawChar((i + 1) << con_fontShift, y - CONSOLE_CHAR_ALIGN, text[i]);
}


/**
 * @brief Draws the last few lines of output transparently over the game top
 * @sa SCR_DrawConsole
 */
void Con_DrawNotify (void)
{
	const char *text;
	int i, time, skip, x;
	int v = 60 * viddef.rx;
	const int l = 120 * viddef.ry;

	for (i = con.currentLine - NUM_CON_TIMES + 1; i <= con.currentLine; i++) {
		qboolean draw = qfalse;
		if (i < 0)
			continue;
		time = con.times[i % NUM_CON_TIMES];
		if (time == 0)
			continue;
		time = CL_Milliseconds() - time;
		if (time > con_notifytime->integer * 1000)
			continue;
		text = con.text + (i % con.totalLines) * con.lineWidth;

		for (x = 0; x < con.lineWidth; x++) {
			/* only draw chat or check for developer mode */
			if (developer->integer || text[x] & CONSOLE_COLORED_TEXT_MASK) {
				R_DrawChar(l + (x << con_fontShift), v, text[x]);
				draw = qtrue;
			}
		}
		if (draw)
			v += con_fontHeight;
	}

	if (cls.keyDest == key_message && (msgMode == MSG_SAY_TEAM || msgMode == MSG_SAY)) {
		const char *s = msgBuffer;
		int x;

		if (msgMode == MSG_SAY) {
			Con_DisplayString(l, v, "say:");
			skip = 4;
		} else {
			Con_DisplayString(l, v, "say_team:");
			skip = 10;
		}

		if (msgBufferLen > (viddef.width >> con_fontShift) - (skip + 1))
			s += msgBufferLen - ((viddef.width >> con_fontShift) - (skip + 1));

		x = 0;
		while (s[x]) {
			R_DrawChar(l + ((x + skip) << con_fontShift), v, s[x]);
			x++;
		}
		R_DrawChar(l + ((x + skip) << con_fontShift), v, 10 + ((CL_Milliseconds() >> 8) & 1));
		v += con_fontHeight;
	}
}

/**
 * @brief Draws the console with the solid background
 * @param[in] frac
 */
void Con_DrawConsole (float frac)
{
	int i, x, y;
	int rows, row, lines;
	char *text;
	char consoleMessage[128];

	lines = viddef.height * frac;
	if (lines <= 0)
		return;

	if (lines > viddef.height)
		lines = viddef.height;

	/* draw the background */
	if (con_background->integer)
		R_DrawStretchImage(0, viddef.virtualHeight * (frac - 1) , viddef.virtualWidth, viddef.virtualHeight, R_FindImage("pics/background/conback", it_pic));

	Com_sprintf(consoleMessage, sizeof(consoleMessage), "Hit esc to close - v%s", UFO_VERSION);
	{
		const int len = strlen(consoleMessage);
		const int versionX = viddef.width - (len * con_fontWidth) - CONSOLE_CHAR_ALIGN;
		const int versionY = lines - (con_fontHeight + CONSOLE_CHAR_ALIGN);
		for (x = 0; x < len; x++)
			R_DrawChar(versionX + x * con_fontWidth, versionY, consoleMessage[x] | CONSOLE_COLORED_TEXT_MASK);
	}

	/* draw the text */
	con.visLines = lines;

	rows = (lines - con_fontHeight * 2) >> con_fontShift;	/* rows of text to draw */

	y = lines - con_fontHeight * 3;

	/* draw from the bottom up */
	if (con.displayLine != con.currentLine) {
		/* draw arrows to show the buffer is backscrolled */
		for (x = 0; x < con.lineWidth; x += 4)
			R_DrawChar((x + 1) << con_fontShift, y, '^');

		y -= con_fontHeight;
		rows--;
	}

	row = con.displayLine;
	for (i = 0; i < rows; i++, y -= con_fontHeight, row--) {
		if (row < 0)
			break;
		if (con.currentLine - row >= con.totalLines)
			break;				/* past scrollback wrap point */

		text = con.text + (row % con.totalLines) * con.lineWidth;

		for (x = 0; x < con.lineWidth; x++)
			R_DrawChar((x + 1) << con_fontShift, y, text[x]);
	}

	/* draw the input prompt, user text, and cursor if desired */
	Con_DrawInput();
}
