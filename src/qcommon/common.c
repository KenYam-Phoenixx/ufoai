/**
 * @file common.c
 * @brief Misc functions used in client and server
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

#include "qcommon.h"
#include "../server/server.h"
#include <setjmp.h>

#if defined DEBUG && defined _MSC_VER
#include <intrin.h>
#endif

#define	MAXPRINTMSG	4096
#define MAX_NUM_ARGVS	50

csi_t csi;

int com_argc;
char *com_argv[MAX_NUM_ARGVS + 1];

jmp_buf abortframe;				/* an ERR_DROP occured, exit the entire frame */

FILE *log_stats_file;

cvar_t *s_sleep;
cvar_t *s_language;
cvar_t *host_speeds;
cvar_t *log_stats;
cvar_t *developer;
cvar_t *timescale;
cvar_t *fixedtime;
cvar_t *logfile_active;			/* 1 = buffer log, 2 = flush after each print */
cvar_t *showtrace;
cvar_t *dedicated;
cvar_t *cl_maxfps;
cvar_t *teamnum;
cvar_t *gametype;
static FILE *logfile;

static int server_state;

/* host_speeds times */
int time_before_game;
int time_after_game;
int time_before_ref;
int time_after_ref;

/*
============================================================================
CLIENT / SERVER interactions
============================================================================
*/

static int rd_target; /**< redirect the output to e.g. an rcon user */
static char *rd_buffer;
static unsigned int rd_buffersize;
static void (*rd_flush) (int target, char *buffer);

/**
 * @brief Redirect pakets/ouput from server to client
 * @sa Com_EndRedirect
 *
 * This is used to redirect printf outputs for rcon commands
 */
void Com_BeginRedirect (int target, char *buffer, int buffersize, void (*flush) (int, char *))
{
	if (!target || !buffer || !buffersize || !flush)
		return;
	rd_target = target;
	rd_buffer = buffer;
	rd_buffersize = buffersize;
	rd_flush = flush;

	*rd_buffer = 0;
}

/**
 * @brief End the redirection of pakets/output
 * @sa Com_BeginRedirect
 */
void Com_EndRedirect (void)
{
	rd_flush(rd_target, rd_buffer);

	rd_target = 0;
	rd_buffer = NULL;
	rd_buffersize = 0;
	rd_flush = NULL;
}

/**
 * @brief
 *
 * Both client and server can use this, and it will output
 * to the apropriate place.
 */
void Com_Printf (const char *fmt, ...)
{
	va_list argptr;
	char msg[MAXPRINTMSG];

	va_start(argptr, fmt);
	Q_vsnprintf(msg, MAXPRINTMSG, fmt, argptr);
	va_end(argptr);

	msg[sizeof(msg)-1] = 0;

	/* redirect the output? */
	if (rd_target) {
		if ((strlen(msg) + strlen(rd_buffer)) > (rd_buffersize - 1)) {
			rd_flush(rd_target, rd_buffer);
			*rd_buffer = 0;
		}
		Q_strcat(rd_buffer, msg, sizeof(rd_buffer));
		return;
	}

	Con_Print(msg);

	/* also echo to debugging console */
	Sys_ConsoleOutput(msg);

	/* logfile */
	if (logfile_active && logfile_active->value) {
		char name[MAX_OSPATH];

		if (!logfile) {
			Com_sprintf(name, sizeof(name), "%s/ufoconsole.log", FS_Gamedir());
			if (logfile_active->value > 2)
				logfile = fopen(name, "a");
			else
				logfile = fopen(name, "w");
		}
		if (logfile)
			fprintf(logfile, "%s", msg);
		if (logfile_active->value > 1)
			fflush(logfile);	/* force it to save every time */
	}
}


/**
 * @brief
 *
 * A Com_Printf that only shows up if the "developer" cvar is set
 */
void Com_DPrintf (const char *fmt, ...)
{
	va_list argptr;
	char msg[MAXPRINTMSG];

	/* don't confuse non-developers with techie stuff... */
	if (!developer || !developer->value)
		return;

	va_start(argptr, fmt);
	Q_vsnprintf(msg, MAXPRINTMSG, fmt, argptr);
	va_end(argptr);

	msg[sizeof(msg)-1] = 0;

	Com_Printf("%s", msg);
}


/**
 * @brief
 *
 * Both client and server can use this, and it will
 * do the apropriate things.
 */
void Com_Error (int code, const char *fmt, ...)
{
	va_list argptr;
	static char msg[MAXPRINTMSG];
	static qboolean recursive = qfalse;

	if (recursive)
		Sys_Error("recursive error after: %s", msg);
	recursive = qtrue;

	va_start(argptr, fmt);
	Q_vsnprintf(msg, MAXPRINTMSG, fmt, argptr);
	va_end(argptr);

	msg[sizeof(msg)-1] = 0;

	if (code == ERR_DISCONNECT) {
		CL_Drop();
		recursive = qfalse;
		longjmp(abortframe, -1);
	} else if (code == ERR_DROP) {
		Com_Printf("********************\nERROR: %s\n********************\n", msg);
		SV_Shutdown(va("Server crashed: %s\n", msg), qfalse);
		CL_Drop();
		recursive = qfalse;
		longjmp(abortframe, -1);
	} else {
		SV_Shutdown(va("Server fatal crashed: %s\n", msg), qfalse);
		CL_Shutdown();
	}

	if (logfile) {
		fclose(logfile);
		logfile = NULL;
	}

	Sys_Error("%s", msg);
}


/**
 * @brief
 */
void Com_Drop (void)
{
	SV_Shutdown("Server disconnected\n", qfalse);
	CL_Drop();
	longjmp(abortframe, -1);
}


/**
 * @brief
 *
 * Both client and server can use this, and it will
 * do the apropriate things.
 */
void Com_Quit (void)
{
	SV_Shutdown("Server quit\n", qfalse);
	SV_Clear();
#ifndef DEDICATED_ONLY
	CL_Shutdown();
#else
	Cvar_WriteVariables(va("%s/config.cfg", FS_Gamedir()));
#endif
	if (logfile) {
		fclose(logfile);
		logfile = NULL;
	}

	Sys_Quit();
}


/**
 * @brief Check whether we are the server or have a singleplayer tactical mission
 * @sa Com_SetServerState
 */
int Com_ServerState (void)
{
	return server_state;
}

/**
 * @brief
 * @sa SV_SpawnServer
 * @sa Com_ServerState
 */
void Com_SetServerState (int state)
{
	server_state = state;
}


/*
==============================================================================
MESSAGE IO FUNCTIONS

Handles byte ordering and avoids alignment errors
==============================================================================
*/

const vec3_t bytedirs[NUMVERTEXNORMALS] = {
#include "../client/anorms.h"
};

/* writing functions */

/**
 * @brief
 */
void MSG_WriteChar (sizebuf_t * sb, int c)
{
	byte *buf;

#ifdef PARANOID
	if (c < SCHAR_MIN || c > SCHAR_MAX)
		Com_Error(ERR_FATAL, "MSG_WriteChar: range error %i", c);
#endif

	buf = SZ_GetSpace(sb, 1);
	buf[0] = c;
}

/**
 * @brief
 */
#ifdef DEBUG
void MSG_WriteByteDebug (sizebuf_t * sb, int c, char *file, int line)
#else
void MSG_WriteByte (sizebuf_t * sb, int c)
#endif
{
	byte *buf;

	/* PARANOID is only possible in debug mode (when DEBUG was set, too) */
#ifdef DEBUG
	if (c < 0 || c > UCHAR_MAX)
		Com_Printf("MSG_WriteByte: range error %i ('%s', line %i)\n", c, file, line);
#endif

	buf = SZ_GetSpace(sb, 1);
	buf[0] = c & UCHAR_MAX;
}

/**
 * @brief
 */
#ifdef DEBUG
void MSG_WriteShortDebug (sizebuf_t * sb, int c, char* file, int line)
#else
void MSG_WriteShort (sizebuf_t * sb, int c)
#endif
{
	byte *buf;

#ifdef DEBUG
	if (c < SHRT_MIN || c > USHRT_MAX)
		Com_Printf("MSG_WriteShort: range error %i ('%s', line %i)\n", c, file, line);
#endif

	buf = SZ_GetSpace(sb, 2);
	buf[0] = c & UCHAR_MAX;
	buf[1] = (c >> 8) & UCHAR_MAX;
}

/**
 * @brief
 */
void MSG_WriteLong (sizebuf_t * sb, int c)
{
	byte *buf;

	buf = SZ_GetSpace(sb, 4);
	buf[0] = c & UCHAR_MAX;
	buf[1] = (c >> 8) & UCHAR_MAX;
	buf[2] = (c >> 16) & UCHAR_MAX;
	buf[3] = (c >> 24) & UCHAR_MAX;
}

/**
 * @brief
 */
void MSG_WriteFloat (sizebuf_t * sb, float f)
{
	union {
		float f;
		int l;
	} dat;


	dat.f = f;
	dat.l = LittleLong(dat.l);

	SZ_Write(sb, &dat.l, 4);
}

/**
 * @brief
 */
void MSG_WriteString (sizebuf_t * sb, const char *s)
{
	if (!s)
		SZ_Write(sb, "", 1);
	else
		SZ_Write(sb, s, strlen(s) + 1);
}

/**
 * @brief
 */
void MSG_WriteCoord (sizebuf_t * sb, float f)
{
	MSG_WriteLong(sb, (int) (f * 32));
}

/**
 * @brief
 */
void MSG_WritePos (sizebuf_t * sb, vec3_t pos)
{
	MSG_WriteLong(sb, (long) (pos[0] * 32.));
	MSG_WriteLong(sb, (long) (pos[1] * 32.));
	MSG_WriteLong(sb, (long) (pos[2] * 32.));
}

/**
  * @brief
  */
void MSG_WriteGPos (sizebuf_t * sb, pos3_t pos)
{
	MSG_WriteByte(sb, pos[0]);
	MSG_WriteByte(sb, pos[1]);
	MSG_WriteByte(sb, pos[2]);
}

/**
 * @brief
 */
void MSG_WriteAngle (sizebuf_t * sb, float f)
{
	MSG_WriteByte(sb, (int) (f * 256 / 360) & 255);
}

/**
 * @brief
 */
void MSG_WriteAngle16 (sizebuf_t * sb, float f)
{
	MSG_WriteShort(sb, ANGLE2SHORT(f));
}


/**
 * @brief
 */
void MSG_WriteDir (sizebuf_t * sb, vec3_t dir)
{
	int i, best;
	float d, bestd;

	if (!dir) {
		MSG_WriteByte(sb, 0);
		return;
	}

	bestd = 0;
	best = 0;
	for (i = 0; i < NUMVERTEXNORMALS; i++) {
		d = DotProduct(dir, bytedirs[i]);
		if (d > bestd) {
			bestd = d;
			best = i;
		}
	}
	MSG_WriteByte(sb, best);
}


/**
 * @brief Writes to buffer according to format; version without syntactic sugar for variable arguments, to call it from other functions with variable arguments
 */
void MSG_V_WriteFormat (sizebuf_t * sb, const char *format, va_list ap)
{
	char typeID;

	Com_DPrintf("MSG_WriteFormat: %s\n", format);

	while (*format) {
		typeID = *format++;

		switch (typeID) {
		case 'c':
			MSG_WriteChar(sb, va_arg(ap, int));

			break;
		case 'b':
			MSG_WriteByte(sb, va_arg(ap, int));

			break;
		case 's':
			MSG_WriteShort(sb, va_arg(ap, int));

			break;
		case 'l':
			MSG_WriteLong(sb, va_arg(ap, int));

			break;
		case 'f':
			/* NOTE: float is promoted to double through ... */
			MSG_WriteFloat(sb, va_arg(ap, double));

			break;
		case 'p':
			MSG_WritePos(sb, va_arg(ap, float *));

			break;
		case 'g':
			MSG_WriteGPos(sb, va_arg(ap, byte *));
			break;
		case 'd':
			MSG_WriteDir(sb, va_arg(ap, float *));

			break;
		case 'a':
			/* NOTE: float is promoted to double through ... */
			MSG_WriteAngle(sb, va_arg(ap, double));

			break;
		case '!':
			break;
		case '*':
			{
				int i, n;
				byte *p;

				n = va_arg(ap, int);

				p = va_arg(ap, byte *);
				MSG_WriteShort(sb, n);
				for (i = 0; i < n; i++)
					MSG_WriteByte(sb, *p++);
			}
			break;
		default:
			Com_Error(ERR_DROP, "WriteFormat: Unknown type!\n");
		}
	}
	/* Too many arguments for the given format; too few cause crash above */
	if (!ap)
		Com_Error(ERR_DROP, "WriteFormat: Too many arguments!\n");
}

/**
 * @brief The user-friendly version of MSG_WriteFormat that writes variable arguments to buffer according to format
 */
void MSG_WriteFormat (sizebuf_t * sb, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	MSG_V_WriteFormat(sb, format, ap);
	va_end(ap);
}


/* reading functions */

/**
 * @brief
 */
void MSG_BeginReading (sizebuf_t * msg)
{
	msg->readcount = 0;
}

/**
 * @brief
 *
 * returns -1 if no more characters are available
 */
int MSG_ReadChar (sizebuf_t * msg_read)
{
	int c;

	if (msg_read->readcount + 1 > msg_read->cursize)
		c = -1;
	else
		c = (signed char) msg_read->data[msg_read->readcount];
	msg_read->readcount++;

	return c;
}

/**
 * @brief Reads a byte from the netchannel
 * @note Beware that you don't put this into a byte or short - this will overflow
 * use an int value to store the return value!!!
 */
int MSG_ReadByte (sizebuf_t * msg_read)
{
	int c;

	if (msg_read->readcount + 1 > msg_read->cursize)
		c = -1;
	else
		c = (unsigned char) msg_read->data[msg_read->readcount];
	msg_read->readcount++;

	return c;
}

/**
 * @brief
 */
int MSG_ReadShort (sizebuf_t * msg_read)
{
	int c;

	if (msg_read->readcount + 2 > msg_read->cursize)
		c = -1;
	else
		c = (short) (msg_read->data[msg_read->readcount]
					+ (msg_read->data[msg_read->readcount + 1] << 8));

	msg_read->readcount += 2;

	return c;
}

/**
 * @brief
 */
int MSG_ReadLong (sizebuf_t * msg_read)
{
	int c;

	if (msg_read->readcount + 4 > msg_read->cursize)
		c = -1;
	else
		c = msg_read->data[msg_read->readcount]
			+ (msg_read->data[msg_read->readcount + 1] << 8)
			+ (msg_read->data[msg_read->readcount + 2] << 16)
			+ (msg_read->data[msg_read->readcount + 3] << 24);

	msg_read->readcount += 4;

	return c;
}

/**
 * @brief
 */
float MSG_ReadFloat (sizebuf_t * msg_read)
{
	union {
		byte b[4];
		float f;
		int l;
	} dat;

	if (msg_read->readcount + 4 > msg_read->cursize)
		dat.f = -1;
	else {
		dat.b[0] = msg_read->data[msg_read->readcount];
		dat.b[1] = msg_read->data[msg_read->readcount + 1];
		dat.b[2] = msg_read->data[msg_read->readcount + 2];
		dat.b[3] = msg_read->data[msg_read->readcount + 3];
	}
	msg_read->readcount += 4;

	dat.l = LittleLong(dat.l);

	return dat.f;
}

/**
 * @brief
 */
char *MSG_ReadStringRaw (sizebuf_t * msg_read)
{
	static char string[2048];
	unsigned int l;
	int c;

	l = 0;
	do {
		c = MSG_ReadByte(msg_read);
		if (c == -1 || c == 0)
			break;
		string[l] = c;
		l++;
	} while (l < sizeof(string) - 1);

	string[l] = 0;

	return string;
}

/**
 * @brief
 */
char *MSG_ReadString (sizebuf_t * msg_read)
{
	static char string[2048];
	unsigned int l;
	int c;

	l = 0;
	do {
		c = MSG_ReadByte(msg_read);
		if (c == -1 || c == 0)
			break;
		/* translate all format specs to avoid crash bugs */
		/* don't allow higher ascii values */
		if (c == '%' || c > 127)
			c = '.';
		string[l] = c;
		l++;
	} while (l < sizeof(string) - 1);

	string[l] = 0;

	return string;
}

/**
 * @brief
 */
char *MSG_ReadStringLine (sizebuf_t * msg_read)
{
	static char string[2048];
	unsigned int l;
	int c;

	l = 0;
	do {
		c = MSG_ReadByte(msg_read);
		if (c == -1 || c == 0 || c == '\n')
			break;
		/* translate all format specs to avoid crash bugs */
		/* don't allow higher ascii values */
		if (c == '%' || c > 127)
			c = '.';
		string[l] = c;
		l++;
	} while (l < sizeof(string) - 1);

	string[l] = 0;

	return string;
}

/**
 * @brief
 */
float MSG_ReadCoord (sizebuf_t * msg_read)
{
	return (float) MSG_ReadLong(msg_read) * (1.0 / 32);
}

/**
 * @brief
 */
void MSG_ReadPos (sizebuf_t * msg_read, vec3_t pos)
{
	pos[0] = MSG_ReadLong(msg_read) / 32.;
	pos[1] = MSG_ReadLong(msg_read) / 32.;
	pos[2] = MSG_ReadLong(msg_read) / 32.;
}

/**
 * @brief
 */
void MSG_ReadGPos (sizebuf_t * msg_read, pos3_t pos)
{
	pos[0] = MSG_ReadByte(msg_read);
	pos[1] = MSG_ReadByte(msg_read);
	pos[2] = MSG_ReadByte(msg_read);
}

/**
 * @brief
 */
float MSG_ReadAngle (sizebuf_t * msg_read)
{
	return (float) MSG_ReadChar(msg_read) * (360.0 / 256);
}

/**
 * @brief
 */
float MSG_ReadAngle16 (sizebuf_t * msg_read)
{
	short s;

	s = MSG_ReadShort(msg_read);
	return (float) SHORT2ANGLE(s);
}

/**
 * @brief
 */
void MSG_ReadData (sizebuf_t * msg_read, void *data, int len)
{
	int i;

	for (i = 0; i < len; i++)
		((byte *) data)[i] = MSG_ReadByte(msg_read);
}

/**
 * @brief
 */
void MSG_ReadDir (sizebuf_t * sb, vec3_t dir)
{
	int b;

	b = MSG_ReadByte(sb);
	if (b >= NUMVERTEXNORMALS)
		Com_Error(ERR_DROP, "MSG_ReadDir: out of range");
	VectorCopy(bytedirs[b], dir);
}


/**
 * @brief Reads from a buffer according to format; version without syntactic sugar for variable arguments, to call it from other functions with variable arguments
 */
void MSG_V_ReadFormat (sizebuf_t * msg_read, const char *format, va_list ap)
{
	char typeID;

	while (*format) {
		typeID = *format++;

		switch (typeID) {
		case 'c':
			*va_arg(ap, int *) = MSG_ReadChar(msg_read);

			break;
		case 'b':
			*va_arg(ap, int *) = MSG_ReadByte(msg_read);

			break;
		case 's':
			*va_arg(ap, int *) = MSG_ReadShort(msg_read);

			break;
		case 'l':
			*va_arg(ap, int *) = MSG_ReadLong(msg_read);

			break;
		case 'f':
			*va_arg(ap, float *) = MSG_ReadFloat(msg_read);

			break;
		case 'p':
			MSG_ReadPos(msg_read, *va_arg(ap, vec3_t *));
			break;
		case 'g':
			MSG_ReadGPos(msg_read, *va_arg(ap, pos3_t *));
			break;
		case 'd':
			MSG_ReadDir(msg_read, *va_arg(ap, vec3_t *));
			break;
		case 'a':
			*va_arg(ap, float *) = MSG_ReadAngle(msg_read);

			break;
		case '!':
			format++;
			break;
		case '*':
			{
				int i, n;
				byte *p;

				n = MSG_ReadShort(msg_read);
				*va_arg(ap, int *) = n;
				p = va_arg(ap, void *);

				for (i = 0; i < n; i++)
					*p++ = MSG_ReadByte(msg_read);
			}
			break;
		default:
			Com_Error(ERR_DROP, "ReadFormat: Unknown type!\n");
		}
	}
	/* Too many arguments for the given format; too few cause crash above */
	if (!ap)
		Com_Error(ERR_DROP, "ReadFormat: Too many arguments!\n");
}

/**
 * @brief The user-friendly version of MSG_ReadFormat that reads variable arguments from a buffer according to format
 */
void MSG_ReadFormat (sizebuf_t * msg_read, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	MSG_V_ReadFormat(msg_read, format, ap);
	va_end(ap);
}


/**
 * @brief returns the length of a sizebuf_t
 *
 * calculated the length of a sizebuf_t by summing up
 * the size of each format char
 */
int MSG_LengthFormat (sizebuf_t * sb, const char *format)
{
	char typeID;
	int length, delta = 0;
	int oldCount;

	length = 0;
	oldCount = sb->readcount;

	while (*format) {
		typeID = *format++;

		switch (typeID) {
		case 'c':
			delta = 1;
			break;
		case 'b':
			delta = 1;
			break;
		case 's':
			delta = 2;
			break;
		case 'l':
			delta = 4;
			break;
		case 'f':
			delta = 4;
			break;
		case 'p':
			delta = 12;
			break;
		case 'g':
			delta = 3;
			break;
		case 'd':
			delta = 1;
			break;
		case 'a':
			delta = 1;
			break;
		case '!':
			delta = 0;
			break;
		case '*':
			delta = MSG_ReadShort(sb);
			length += 2;
			break;
		default:
			Com_Error(ERR_DROP, "LengthFormat: Unknown type!\n");
		}

		/* advance in buffer */
		sb->readcount += delta;
		length += delta;
	}

	sb->readcount = oldCount;
	return length;
}


/*=========================================================================== */

/**
 * @brief
 */
void SZ_Init (sizebuf_t * buf, byte * data, int length)
{
	memset(buf, 0, sizeof(*buf));
	buf->data = data;
	buf->maxsize = length;
}

/**
 * @brief
 */
void SZ_Clear (sizebuf_t * buf)
{
	buf->cursize = 0;
	buf->overflowed = qfalse;
}

/**
 * @brief
 */
void *SZ_GetSpace (sizebuf_t * buf, int length)
{
	void *data;

	if (buf->cursize + length > buf->maxsize) {
		if (!buf->allowoverflow)
			Com_Error(ERR_FATAL, "SZ_GetSpace: overflow without allowoverflow set");

		if (length > buf->maxsize)
			Com_Error(ERR_FATAL, "SZ_GetSpace: %i is > full buffer size", length);

		Com_Printf("SZ_GetSpace: overflow\n");
		SZ_Clear(buf);
		buf->overflowed = qtrue;
	}

	data = buf->data + buf->cursize;
	buf->cursize += length;

	return data;
}

/**
 * @brief
 */
void SZ_Write (sizebuf_t * buf, const void *data, int length)
{
	memcpy(SZ_GetSpace(buf, length), data, length);
}

/**
 * @brief
 */
void SZ_Print (sizebuf_t * buf, const char *data)
{
	int len;

	len = strlen(data) + 1;

	if (buf->cursize) {
		if (buf->data[buf->cursize - 1])
			memcpy((byte *) SZ_GetSpace(buf, len), data, len);	/* no trailing 0 */
		else
			memcpy((byte *) SZ_GetSpace(buf, len - 1) - 1, data, len);	/* write over trailing 0 */
	} else
		memcpy((byte *) SZ_GetSpace(buf, len), data, len);
}


/*============================================================================ */

/**
 * @brief returns hash key for a string
 */
unsigned int Com_HashKey (const char *name, int hashsize)
{
	int i;
	unsigned int v;
	unsigned int c;

	v = 0;
	for (i = 0; name[i]; i++) {
		c = name[i];
		v = (v + i) * 37 + tolower( c );	/* case insensitivity */
	}

	return v % hashsize;
}

/**
 * @brief Checks whether a given commandline paramter was set
 * @param[in] parm Check for this commandline parameter
 * @return the position (1 to argc-1) in the program's argument list
 * where the given parameter apears, or 0 if not present
 * @sa COM_InitArgv
 */
int COM_CheckParm (char *parm)
{
	int i;

	for (i = 1; i < com_argc; i++) {
		if (!com_argv[i])
			continue;               /* NEXTSTEP sometimes clears appkit vars. */
		if (!Q_strcmp (parm, com_argv[i]))
			return i;
	}

	return 0;
}

/**
 * @brief Returns the script commandline argument count
 */
int COM_Argc (void)
{
	return com_argc;
}

/**
 * @brief Returns an argument of script commandline
 */
char *COM_Argv (int arg)
{
	if (arg < 0 || arg >= com_argc || !com_argv[arg])
		return "";
	return com_argv[arg];
}

/**
 * @brief Reset com_argv entry to empty string
 * @param[in] arg Which argument in com_argv
 * @sa COM_InitArgv
 * @sa COM_CheckParm
 * @sa COM_AddParm
 */
void COM_ClearArgv (int arg)
{
	if (arg < 0 || arg >= com_argc || !com_argv[arg])
		return;
	com_argv[arg] = "";
}


/**
 * @brief
 * @sa COM_CheckParm
 */
void COM_InitArgv (int argc, char **argv)
{
	int i;

	if (argc > MAX_NUM_ARGVS)
		Com_Error(ERR_FATAL, "argc > MAX_NUM_ARGVS");
	com_argc = argc;
	for (i = 0; i < argc; i++) {
		if (!argv[i] || strlen(argv[i]) >= MAX_TOKEN_CHARS)
			com_argv[i] = "";
		else
			com_argv[i] = argv[i];
	}
}

/**
 * @brief Adds the given string at the end of the current argument list
 * @sa COM_InitArgv
 */
void COM_AddParm (char *parm)
{
	if (com_argc == MAX_NUM_ARGVS)
		Com_Error(ERR_FATAL, "COM_AddParm: MAX_NUM)ARGS");
	com_argv[com_argc++] = parm;
}

/**
 * @brief
 * @note just for debugging
 */
int memsearch (byte * start, int count, int search)
{
	int i;

	for (i = 0; i < count; i++)
		if (start[i] == search)
			return i;
	return -1;
}


/**
 * @brief
 */
char *CopyString (const char *in)
{
	char *out;
	int l = strlen(in);

	out = Mem_Alloc(l + 1);
	Q_strncpyz(out, in, l + 1);
	return out;
}


/**
 * @brief
 */
void Info_Print (char *s)
{
	char key[512];
	char value[512];
	char *o;
	int l;

	if (*s == '\\')
		s++;
	while (*s) {
		o = key;
		while (*s && *s != '\\')
			*o++ = *s++;

		l = o - key;
		if (l < 20) {
			memset(o, ' ', 20 - l);
			key[20] = 0;
		} else
			*o = 0;
		Com_Printf("%s", key);

		if (!*s) {
			Com_Printf("MISSING VALUE\n");
			return;
		}

		o = value;
		s++;
		while (*s && *s != '\\')
			*o++ = *s++;
		*o = 0;

		if (*s)
			s++;
		Com_Printf("%s\n", value);
	}
}


/*
==============================================================================
ZONE MEMORY ALLOCATION

just cleared malloc with counters now...
==============================================================================
*/

#define	Z_MAGIC		0x1d1d

typedef struct zhead_s {
	struct zhead_s *prev, *next;
	short magic;
	short tag;					/* for group free */
	size_t size;
} zhead_t;

zhead_t z_chain;
int z_count, z_bytes;

/**
 * @brief Frees a Mem_Alloc'ed pointer
 */
extern void Mem_Free (void *ptr)
{
	zhead_t *z;

	if (!ptr)
		return;

	z = ((zhead_t *) ptr) - 1;

	if (z->magic != Z_MAGIC)
		Com_Error(ERR_FATAL, "Mem_Free: bad magic (%i)", z->magic);

	z->prev->next = z->next;
	z->next->prev = z->prev;

	z_count--;
	z_bytes -= z->size;
	free(z);
}


/**
 * @brief Stats about the allocated bytes via Mem_Alloc
 */
static void Mem_Stats_f (void)
{
	Com_Printf("%i bytes in %i blocks\n", z_bytes, z_count);
}

/**
 * @brief Frees a memory block with a given tag
 */
extern void Mem_FreeTags (int tag)
{
	zhead_t *z, *next;

	for (z = z_chain.next; z != &z_chain; z = next) {
		next = z->next;
		if (z->tag == tag)
			Mem_Free((void *) (z + 1));
	}
}

/**
 * @brief Allocates a memory block with a given tag
 *
 * and fills with 0
 */
extern void *Mem_TagMalloc (size_t size, int tag)
{
	zhead_t *z;

	size = size + sizeof(zhead_t);
	z = malloc(size);
	if (!z)
		Com_Error(ERR_FATAL, "Mem_TagMalloc: failed on allocation of %Zu bytes", size);
	memset(z, 0, size);
	z_count++;
	z_bytes += size;
	z->magic = Z_MAGIC;
	z->tag = tag;
	z->size = size;

	z->next = z_chain.next;
	z->prev = &z_chain;
	z_chain.next->prev = z;
	z_chain.next = z;

	return (void *) (z + 1);
}

/**
 * @brief Allocate a memory block with default tag
 *
 * and fills with 0
 */
extern void *Mem_Alloc (size_t size)
{
	return Mem_TagMalloc(size, 0);
}

/*======================================================== */

void Key_Init(void);
void SCR_EndLoadingPlaque(void);

/**
 * @brief
 *
 * Just throw a fatal error to
 * test error shutdown procedures
 */
static void Com_Error_f (void)
{
	Com_Error(ERR_FATAL, "%s", Cmd_Argv(1));
}

#ifdef HAVE_GETTEXT
/**
 * @brief Gettext init function
 *
 * Initialize the locale settings for gettext
 * po files are searched in ./base/i18n
 * You can override the language-settings in setting
 * the cvar s_language to a valid language-string like
 * e.g. de_DE, en or en_US
 *
 * This function is only build in and called when
 * defining HAVE_GETTEXT
 * Under Linux see Makefile options for this
 *
 * @sa Qcommon_LocaleInit
 */
extern void Qcommon_LocaleInit (void)
{
	char *locale;

	s_language = Cvar_Get("s_language", "", CVAR_ARCHIVE, NULL);
	s_language->modified = qfalse;

#ifdef _WIN32
	Q_putenv("LANG", s_language->string);
	Q_putenv("LANGUAGE", s_language->string);
#else /* _WIN32 */
# ifndef SOLARIS
	unsetenv("LANGUAGE");
# endif /* SOLARIS */
# ifdef __APPLE__
	if (Q_putenv("LANGUAGE", s_language->string) == -1)
		Com_Printf("...setenv for LANGUAGE failed: %s\n", s_language->string);
	if (Q_putenv("LC_ALL", s_language->string) == -1)
		Com_Printf("...setenv for LC_ALL failed: %s\n", s_language->string);
# endif /* __APPLE__ */
#endif /* _WIN32 */

	/* set to system default */
	setlocale(LC_ALL, "C");
	locale = setlocale(LC_MESSAGES, s_language->string);
	if (!locale) {
		Com_Printf("...could not set to language: %s\n", s_language->string);
		locale = setlocale(LC_MESSAGES, "");
		if (!locale) {
			Com_Printf("...could not set to system language\n");
			return;
		}
	} else {
		Com_Printf("...using language: %s\n", locale);
		Cvar_Set("s_language", locale);
		s_language->modified = qfalse;
	}
}
#endif /* HAVE_GETTEXT */

/**
 * @brief
 */
extern void Com_SetGameType (void)
{
	int i, j;
	gametype_t* gt;
	cvarlist_t* list;

	for (i = 0; i < numGTs; i++) {
		gt = &gts[i];
		if (!Q_strncmp(gt->id, gametype->string, MAX_VAR)) {
			if (dedicated->value)
				Com_Printf("set gametype to: %s\n", gt->id);
			for (j = 0, list = gt->cvars; j < gt->num_cvars; j++, list++) {
				Cvar_Set(list->name, list->value);
				if (dedicated->value)
					Com_Printf("  %s = %s\n", list->name, list->value);
			}
			break;
		}
	}

	if (i == numGTs)
		Com_Printf("Can't set the gametype - unknown value for cvar gametype: '%s'\n", gametype->string);
}

/**
 * @brief
 */
static void Com_GameTypeList_f (void)
{
	int i, j;
	gametype_t* gt;
	cvarlist_t* list;

	Com_Printf("Available gametypes:\n");
	for (i = 0; i < numGTs; i++) {
		gt = &gts[i];
		Com_Printf("%s\n", gt->id);
		for (j = 0, list = gt->cvars; j < gt->num_cvars; j++, list++)
			Com_Printf("  %s = %s\n", list->name, list->value);
	}
}

/**
 * @brief Init function
 *
 * @param[in] argc int
 * @param[in] argv char**
 * @sa Com_ParseScripts
 * @sa Sys_Init
 * @sa CL_Init
 * @sa Qcommon_LocaleInit
 *
 * To compile language support into UFO:AI you need to activate the preprocessor variable
 * HAVE_GETTEXT (for linux have a look at the makefile)
 */
extern void Qcommon_Init (int argc, char **argv)
{
	char *s;

#ifndef DEDICATED_ONLY
#ifdef HAVE_GETTEXT
	/* i18n through gettext */
	char languagePath[MAX_OSPATH];
	cvar_t *fs_i18ndir;
#endif /* HAVE_GETTEXT */
#endif /* DEDICATED_ONLY */

	if (setjmp(abortframe))
		Sys_Error("Error during initialization");

	z_chain.next = z_chain.prev = &z_chain;

	/* prepare enough of the subsystems to handle
	   cvar and command buffer management */
	COM_InitArgv(argc, argv);

	Swap_Init();
	Cbuf_Init();

	Cmd_Init();
	Cvar_Init();

	Key_Init();

	/* we need to add the early commands twice, because
	   a basedir needs to be set before execing
	   config files, but we want other parms to override
	   the settings of the config files */
	Cbuf_AddEarlyCommands(qfalse);
	Cbuf_Execute();

	FS_InitFilesystem();

	Cbuf_AddText("exec default.cfg\n");
	Cbuf_AddText("exec config.cfg\n");
#ifndef DEDICATED_ONLY
	Cbuf_AddText("exec keys.cfg\n");
#endif

	Cbuf_AddEarlyCommands(qtrue);
	Cbuf_Execute();

	/* init commands and vars */
	Cmd_AddCommand("mem_stats", Mem_Stats_f, NULL);
	Cmd_AddCommand("error", Com_Error_f, NULL);
	Cmd_AddCommand("gametypelist", Com_GameTypeList_f, "List all available multiplayer game types");

	s_sleep = Cvar_Get("s_sleep", "1", CVAR_ARCHIVE, "Use the sleep function to redruce cpu usage");
	host_speeds = Cvar_Get("host_speeds", "0", 0, NULL);
	log_stats = Cvar_Get("log_stats", "0", 0, NULL);
	developer = Cvar_Get("developer", "0", 0, "Activate developer output to logfile and gameconsole");
	timescale = Cvar_Get("timescale", "1", 0, NULL);
	fixedtime = Cvar_Get("fixedtime", "0", 0, NULL);
	logfile_active = Cvar_Get("logfile", "1", 0, "0 = deacticate logfile, 1 = write normal logfile, 2 = flush on every new line");
	showtrace = Cvar_Get("showtrace", "0", 0, NULL);
	gametype = Cvar_Get("gametype", "1on1", CVAR_ARCHIVE | CVAR_SERVERINFO, "Sets the multiplayer gametype - see gametypelist command for a list of all gametypes");
#ifdef DEDICATED_ONLY
	dedicated = Cvar_Get("dedicated", "1", CVAR_SERVERINFO | CVAR_NOSET, "Is this a dedicated server?");
#else
	dedicated = Cvar_Get("dedicated", "0", CVAR_SERVERINFO | CVAR_NOSET, "Is this a dedicated server?");

	/* set this to false for client - otherwise Qcommon_Frame would set the initial values to multiplayer */
	gametype->modified = qfalse;
#endif
	cl_maxfps = Cvar_Get("cl_maxfps", "90", 0, NULL);
	teamnum = Cvar_Get("teamnum", "1", CVAR_ARCHIVE, NULL);

	s = va("UFO: Alien Invasion %s %s %s %s", UFO_VERSION, CPUSTRING, __DATE__, BUILDSTRING);
	Cvar_Get("version", s, CVAR_NOSET, NULL);
	Cvar_Get("ver", UFO_VERSION, CVAR_SERVERINFO | CVAR_NOSET, NULL);

	if (dedicated->value)
		Cmd_AddCommand("quit", Com_Quit, NULL);

	Sys_Init();

	NET_Init();
	Netchan_Init();

	SV_Init();

#ifndef DEDICATED_ONLY /* no gettext support for dedicated servers please */
#ifdef HAVE_GETTEXT
	fs_i18ndir = Cvar_Get("fs_i18ndir", "", 0, "System path to language files");
	/* i18n through gettext */
	setlocale(LC_ALL, "C");
	setlocale(LC_MESSAGES, "");
	/* use system locale dir if we can't find in gamedir */
	if (*fs_i18ndir->string)
		Q_strncpyz(languagePath, fs_i18ndir->string, sizeof(languagePath));
	else
		Com_sprintf(languagePath, sizeof(languagePath), "%s/base/i18n/", FS_GetCwd());
	Com_DPrintf("...using mo files from %s\n", languagePath);
	bindtextdomain(TEXT_DOMAIN, languagePath);
	bind_textdomain_codeset(TEXT_DOMAIN, "UTF-8");
	/* load language file */
	textdomain(TEXT_DOMAIN);

	Qcommon_LocaleInit();
#else /* HAVE_GETTEXT */
	Com_Printf("..no gettext compiled into this binary\n");
#endif /* HAVE_GETTEXT */
#endif /* DEDICATED_ONLY */

	CL_Init();

	Com_ParseScripts();

	if (!dedicated->value)
		Cbuf_AddText("init\n");
	else
		Cbuf_AddText("dedicated_start\n");
	Cbuf_Execute();

	/* add + commands from command line
	   if the user didn't give any commands, run default action */
	if (Cbuf_AddLateCommands()) {
		/* the user asked for something explicit
		 * so drop the loading plaque */
		SCR_EndLoadingPlaque();
	}

#ifndef DEDICATED_ONLY
	/* init irc commands and cvars */
	Irc_Init();

	if (Cvar_VariableInteger("cl_precachemenus"))
		MN_PrecacheMenus();

	CL_InitAfter();
#else
	Com_AddObjectLinks();	/* Add tech links + ammo<->weapon links to items.*/
#endif

	Com_Printf("====== UFO Initialized ======\n\n");
}

/**
 * @brief This is the function that is called directly from main()
 * @sa main
 * @sa Qcommon_Init
 * @sa Qcommon_Shutdown
 * @sa SV_Frame
 * @sa CL_Frame
 * @param[in] msec Passed milliseconds since last frame
 */
float Qcommon_Frame (int msec)
{
	char *s;
	int wait;
	/* static to fix some warnings in relation to setjmp */
	static int time_before = 0, time_between = 0, time_after = 0;
	static int sv_timer = 0, cl_timer = 0;

	/* an ERR_DROP was thrown */
	if (setjmp(abortframe))
		return 1.0;

	/* if there is some time remaining from last frame, reset the timers */
	if (cl_timer > 0)
		cl_timer = 0;
	if (sv_timer > 0)
		sv_timer = 0;

	/* accumulate the new frametime into the timers */
	cl_timer += msec;
	sv_timer += msec;

	if (timescale->value > 5.0)
		Cvar_SetValue("timescale", 5.0);
	else if (timescale->value < 0.2)
		Cvar_SetValue("timescale", 0.2);

	if (dedicated->value) {
		cl_timer = 0;
		wait = -sv_timer;
	} else if (!sv.active) {
		sv_timer = 0;
		wait = -cl_timer;
	} else
		wait = -max(cl_timer, sv_timer);
	wait = min(wait, 100);

	if (wait >= 1 && s_sleep->value) {
		if (COM_CheckParm("-paranoid"))
			Com_DPrintf("Sys_Sleep for %i ms\n", wait);
		Sys_Sleep(wait);
		return timescale->value;
	}

	if (gametype->modified) {
		Com_SetGameType();
		gametype->modified = qfalse;
	}

	if (log_stats->modified) {
		log_stats->modified = qfalse;
		if (log_stats->value) {
			if (log_stats_file) {
				fclose(log_stats_file);
				log_stats_file = NULL;
			}
			log_stats_file = fopen("stats.log", "w");
			if (log_stats_file)
				fprintf(log_stats_file, "entities,dlights,parts,frame time\n");
		} else {
			if (log_stats_file) {
				fclose(log_stats_file);
				log_stats_file = NULL;
			}
		}
	}

	if (showtrace->value) {
		extern int c_traces, c_brush_traces;
		extern int c_pointcontents;

		Com_Printf("%4i traces  %4i points\n", c_traces, c_pointcontents);
		c_traces = 0;
		c_brush_traces = 0;
		c_pointcontents = 0;
	}

	Cbuf_Execute();

	do {
		s = Sys_ConsoleInput();
		if (s)
			Cbuf_AddText(va("%s\n", s));
	} while (s);

	if (host_speeds->value)
		time_before = Sys_Milliseconds();

	/* run a server frame every 100ms (10fps) */
	if (sv_timer > 0) {
		int frametime = 100;
		SV_Frame(frametime);
		sv_timer -= frametime;
	}

#if 0
	/* currently not used - set dedicated cvar to CVAR_LATCH and remove CVAR_NOSET to use this */
#ifndef DEDICATED_ONLY
	if (dedicated->modified) {
		dedicated->modified = qfalse;
		if (dedicated->value) {
			CL_Shutdown();
		}
	}
#endif
#endif

	if (host_speeds->value)
		time_between = Sys_Milliseconds();

	/* run client frames according to cl_maxfps */
	if (cl_timer > 0) {
		int frametime = max(cl_timer, 1000/cl_maxfps->integer);
		frametime = min(frametime, 1000);
#ifndef DEDICATED_ONLY
		Irc_Logic_Frame(cl_timer);
#endif
		cl_timer -= frametime;
		CL_Frame(frametime);
	}

	if (host_speeds->value) {
		int all, sv, gm, cl, rf;

		time_after = Sys_Milliseconds();
		all = time_after - time_before;
		sv = time_between - time_before;
		cl = time_after - time_between;
		gm = time_after_game - time_before_game;
		rf = time_after_ref - time_before_ref;
		sv -= gm;
		cl -= rf;
		Com_Printf("all:%3i sv:%3i game:%3i cl:%3i ref:%3i cltimer:%3i svtimer:%3i \n", all, sv, gm, cl, rf, cl_timer, sv_timer);
	}

	return timescale->value;
}

/**
 * @brief
 */
void Qcommon_Shutdown (void)
{
}

/**
 * @brief
 * @note This is here to let the client know (without sv or server.h) about the server active bool
 * @sa SV_SpawnServer
 */
qboolean Qcommon_ServerActive (void)
{
	return sv.active;
}
