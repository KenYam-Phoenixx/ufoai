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
/* common.c -- misc functions used in client and server */
#include "qcommon.h"
#include <setjmp.h>
#include <ctype.h>
#define	MAXPRINTMSG	4096

#define MAX_NUM_ARGVS	50


csi_t	csi;

int		com_argc;
char	*com_argv[MAX_NUM_ARGVS+1];

int		realtime;

jmp_buf abortframe;		/* an ERR_DROP occured, exit the entire frame */


FILE	*log_stats_file;

cvar_t	*s_language;
cvar_t	*host_speeds;
cvar_t	*log_stats;
cvar_t	*developer;
cvar_t	*timescale;
cvar_t	*fixedtime;
cvar_t	*logfile_active;	/* 1 = buffer log, 2 = flush after each print */
cvar_t	*showtrace;
cvar_t	*dedicated;

FILE	*logfile;

int			server_state;

/* host_speeds times */
int		time_before_game;
int		time_after_game;
int		time_before_ref;
int		time_after_ref;


int dstrcmp( char *source, char *s1, char *s2 )
{
	if ( !s1 && !s2 ) Sys_Error( "strcmp %s * *\n", source, s2 );
	else if ( !s1 ) Sys_Error( "strcmp %s * %s\n", source, s2 );
	else if ( !s2 ) Sys_Error( "strcmp %s %s *\n", source, s1 );
	return strcmp( s1, s2 );
}


/*
============================================================================

CLIENT / SERVER interactions

============================================================================
*/

static int	rd_target;
static char	*rd_buffer;
static int	rd_buffersize;
static void	(*rd_flush)(int target, char *buffer);

/**
  * @brief
  */
void Com_BeginRedirect (int target, char *buffer, int buffersize, void (*flush)(int, char *))
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
  * @brief
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
void Com_Printf (char *fmt, ...)
{
	va_list		argptr;
	char		msg[MAXPRINTMSG];

	va_start (argptr,fmt);
#ifndef _WIN32
	vsnprintf (msg,MAXPRINTMSG,fmt,argptr);
#else
	vsprintf (msg,fmt,argptr);
#endif
	va_end (argptr);

	if (rd_target) {
		if ((strlen (msg) + strlen(rd_buffer)) > (rd_buffersize - 1)) {
			rd_flush(rd_target, rd_buffer);
			*rd_buffer = 0;
		}
		strcat (rd_buffer, msg);
		return;
	}

	Con_Print (msg);

	/* also echo to debugging console */
	Sys_ConsoleOutput (msg);

	/* logfile */
	if (logfile_active && logfile_active->value) {
		char	name[MAX_QPATH];

		if (!logfile) {
			Com_sprintf (name, sizeof(name), "%s/ufoconsole.log", FS_Gamedir ());
			if (logfile_active->value > 2)
				logfile = fopen (name, "a");
			else
				logfile = fopen (name, "w");
		}
		if (logfile)
			fprintf (logfile, "%s", msg);
		if (logfile_active->value > 1)
			fflush (logfile);		/* force it to save every time */
	}
}


/**
  * @brief
  *
  * A Com_Printf that only shows up if the "developer" cvar is set
  */
void Com_DPrintf (char *fmt, ...)
{
	va_list		argptr;
	char		msg[MAXPRINTMSG];

	/* don't confuse non-developers with techie stuff... */
	if (!developer || !developer->value)
		return;

	va_start (argptr,fmt);
#ifndef _WIN32
	vsnprintf (msg,MAXPRINTMSG,fmt,argptr);
#else
	vsprintf (msg,fmt,argptr);
#endif
	va_end (argptr);

	Com_Printf ("%s", msg);
}


/**
  * @brief
  *
  * Both client and server can use this, and it will
  * do the apropriate things.
  */
void Com_Error (int code, char *fmt, ...)
{
	va_list		argptr;
	static char	msg[MAXPRINTMSG];
	static qboolean	recursive = qfalse;

	if (recursive)
		Sys_Error ("recursive error after: %s", msg);
	recursive = qtrue;

	va_start (argptr,fmt);
#ifndef _WIN32
	vsnprintf (msg,MAXPRINTMSG,fmt,argptr);
#else
	vsprintf (msg,fmt,argptr);
#endif
	va_end (argptr);

	if (code == ERR_DISCONNECT) {
		CL_Drop ();
		recursive = qfalse;
		longjmp (abortframe, -1);
	} else if (code == ERR_DROP) {
		Com_Printf ("********************\nERROR: %s\n********************\n", msg);
		SV_Shutdown (va("Server crashed: %s\n", msg), qfalse);
		CL_Drop ();
		recursive = qfalse;
		longjmp (abortframe, -1);
	} else {
		SV_Shutdown (va("Server fatal crashed: %s\n", msg), qfalse);
		CL_Shutdown ();
	}

	if (logfile) {
		fclose (logfile);
		logfile = NULL;
	}

	Sys_Error ("%s", msg);
}


/**
  * @brief
  */
void Com_Drop( void )
{
	SV_Shutdown ( "Server disconnected\n", qfalse);
	CL_Drop ();
	longjmp (abortframe, -1);
}


/**
  * @brief
  *
  * Both client and server can use this, and it will
  * do the apropriate things.
  */
void Com_Quit (void)
{
	SV_Shutdown ("Server quit\n", qfalse);
#ifndef DEDICATED_ONLY
	CL_Shutdown ();
#endif
	if (logfile) {
		fclose (logfile);
		logfile = NULL;
	}

	Sys_Quit ();
}


/**
  * @brief
  */
int Com_ServerState (void)
{
	return server_state;
}

/**
  * @brief
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

vec3_t	bytedirs[NUMVERTEXNORMALS] =
{
#include "../client/anorms.h"
};

/* writing functions */

/**
  * @brief
  */
void MSG_WriteChar (sizebuf_t *sb, int c)
{
	byte	*buf;

#ifdef PARANOID
	if (c < -128 || c > 127)
		Com_Error (ERR_FATAL, "MSG_WriteChar: range error %c", c);
#endif

	buf = SZ_GetSpace (sb, 1);
	buf[0] = c;
}

/**
  * @brief
  */
#ifdef DEBUG
void MSG_WriteByteDebug (sizebuf_t *sb, int c, char* file, int line)
#else
void MSG_WriteByte (sizebuf_t *sb, int c)
#endif
{
	byte	*buf;

	/* PARANOID is only possible in debug mode (when DEBUG was set, too) */
#ifdef PARANOID
	if (c < 0 || c > 255)
		Com_Error (ERR_FATAL, "MSG_WriteByte: range error %c ('%s', line %i)", c, file, line);
#endif

	buf = SZ_GetSpace (sb, 1);
	buf[0] = c;
}

/**
  * @brief
  */
void MSG_WriteShort (sizebuf_t *sb, int c)
{
	byte	*buf;

#ifdef PARANOID
	if (c < ((short)0x8000) || c > (short)0x7fff)
		Com_Error (ERR_FATAL, "MSG_WriteShort: range error");
#endif

	buf = SZ_GetSpace (sb, 2);
	buf[0] = c&0xff;
	buf[1] = c>>8;
}

/**
  * @brief
  */
void MSG_WriteLong (sizebuf_t *sb, int c)
{
	byte	*buf;

	buf = SZ_GetSpace (sb, 4);
	buf[0] = c&0xff;
	buf[1] = (c>>8)&0xff;
	buf[2] = (c>>16)&0xff;
	buf[3] = c>>24;
}

/**
  * @brief
  */
void MSG_WriteFloat (sizebuf_t *sb, float f)
{
	union {
		float	f;
		int	l;
	} dat;


	dat.f = f;
	dat.l = LittleLong (dat.l);

	SZ_Write (sb, &dat.l, 4);
}

/**
  * @brief
  */
void MSG_WriteString (sizebuf_t *sb, char *s)
{
	if (!s)
		SZ_Write (sb, "", 1);
	else
		SZ_Write (sb, s, strlen(s)+1);
}

/**
  * @brief
  */
void MSG_WriteCoord (sizebuf_t *sb, float f)
{
	MSG_WriteLong (sb, (int)(f*32));
}

/**
  * @brief
  */
void MSG_WritePos (sizebuf_t *sb, vec3_t pos)
{
	MSG_WriteShort (sb, (int)(pos[0]*8));
	MSG_WriteShort (sb, (int)(pos[1]*8));
	MSG_WriteShort (sb, (int)(pos[2]*8));
}

/**
  * @brief
  */
void MSG_WriteGPos (sizebuf_t *sb, pos3_t pos)
{
	MSG_WriteByte (sb, pos[0]);
	MSG_WriteByte (sb, pos[1]);
	MSG_WriteByte (sb, pos[2]);
}

/**
  * @brief
  */
void MSG_WriteAngle (sizebuf_t *sb, float f)
{
	MSG_WriteByte (sb, (int)(f*256/360) & 255);
}

/**
  * @brief
  */
void MSG_WriteAngle16 (sizebuf_t *sb, float f)
{
	MSG_WriteShort (sb, ANGLE2SHORT(f));
}


/**
  * @brief
  */
void MSG_WriteDir (sizebuf_t *sb, vec3_t dir)
{
	int		i, best;
	float	d, bestd;

	if (!dir) {
		MSG_WriteByte (sb, 0);
		return;
	}

	bestd = 0;
	best = 0;
	for (i=0 ; i<NUMVERTEXNORMALS ; i++) {
		d = DotProduct (dir, bytedirs[i]);
		if (d > bestd) {
			bestd = d;
			best = i;
		}
	}
	MSG_WriteByte (sb, best);
}


/**
  * @brief
  */
void MSG_WriteFormat( sizebuf_t *sb, char *format, ... )
{
	va_list	ap;
	char	typeID;

	/* initialize ap */
	va_start( ap, format );

	while ( *format ) {
		typeID = *format++;

		switch ( typeID ) {
		case 'c':
			MSG_WriteChar( sb, va_arg( ap, int ) );
			break;
		case 'b':
			MSG_WriteByte( sb, va_arg( ap, int ) );
			break;
		case 's':
			MSG_WriteShort( sb, va_arg( ap, int ) );
			break;
		case 'l':
			MSG_WriteLong( sb, va_arg( ap, int ) );
			break;
		case 'f':
			/* NOTE: float is promoted to double through ... */
			MSG_WriteFloat( sb, va_arg( ap, double ) );
			break;
		case 'p':
 			MSG_WritePos( sb, va_arg( ap, float* ) );
			break;
		case 'g':
			MSG_WriteGPos( sb, va_arg( ap, byte* ) );
			break;
		case 'd':
			MSG_WriteDir( sb, va_arg( ap, float* ) );
			break;
		case 'a':
			/* NOTE: float is promoted to double through ... */
			MSG_WriteAngle( sb, va_arg( ap, double ) );
			break;
		case '!':
			break;
		case '*':
			{
				int		i, n;
				byte	*p;

				n = va_arg( ap, int );
				p = va_arg( ap, byte* );
				MSG_WriteByte( sb, n );
				for ( i = 0; i < n; i++ )
					MSG_WriteByte( sb, *p++ );
			}
			break;
		default:
			Com_Error( ERR_DROP, "WriteFormat: Unknown type!\n" );
		}
	}

	va_end( ap );
}


/*============================================================ */

/* reading functions */

/**
  * @brief
  */
void MSG_BeginReading (sizebuf_t *msg)
{
	msg->readcount = 0;
}

/**
  * @brief
  *
  * returns -1 if no more characters are available
  */
int MSG_ReadChar (sizebuf_t *msg_read)
{
	int	c;

	if (msg_read->readcount+1 > msg_read->cursize)
		c = -1;
	else
		c = (signed char)msg_read->data[msg_read->readcount];
	msg_read->readcount++;

	return c;
}

/**
  * @brief
  */
int MSG_ReadByte (sizebuf_t *msg_read)
{
	int	c;

	if (msg_read->readcount+1 > msg_read->cursize)
		c = -1;
	else
		c = (unsigned char)msg_read->data[msg_read->readcount];
	msg_read->readcount++;

	return c;
}

/**
  * @brief
  */
int MSG_ReadShort (sizebuf_t *msg_read)
{
	int	c;

	if (msg_read->readcount+2 > msg_read->cursize)
		c = -1;
	else
		c = (short)(msg_read->data[msg_read->readcount]
		  + (msg_read->data[msg_read->readcount+1]<<8));

	msg_read->readcount += 2;

	return c;
}

/**
  * @brief
  */
int MSG_ReadLong (sizebuf_t *msg_read)
{
	int	c;

	if (msg_read->readcount+4 > msg_read->cursize)
		c = -1;
	else
		c = msg_read->data[msg_read->readcount]
		+ (msg_read->data[msg_read->readcount+1]<<8)
		+ (msg_read->data[msg_read->readcount+2]<<16)
		+ (msg_read->data[msg_read->readcount+3]<<24);

	msg_read->readcount += 4;

	return c;
}

/**
  * @brief
  */
float MSG_ReadFloat (sizebuf_t *msg_read)
{
	union {
		byte	b[4];
		float	f;
		int	l;
	} dat;

	if (msg_read->readcount+4 > msg_read->cursize)
		dat.f = -1;
	else {
		dat.b[0] =	msg_read->data[msg_read->readcount];
		dat.b[1] =	msg_read->data[msg_read->readcount+1];
		dat.b[2] =	msg_read->data[msg_read->readcount+2];
		dat.b[3] =	msg_read->data[msg_read->readcount+3];
	}
	msg_read->readcount += 4;

	dat.l = LittleLong (dat.l);

	return dat.f;
}

/**
  * @brief
  */
char *MSG_ReadString (sizebuf_t *msg_read)
{
	static char	string[2048];
	int		l,c;

	l = 0;
	do {
		c = MSG_ReadByte(msg_read);
		if (c == -1 || c == 0)
			break;
		string[l] = c;
		l++;
	} while (l < sizeof(string)-1);

	string[l] = 0;

	return string;
}

/**
  * @brief
  */
char *MSG_ReadStringLine (sizebuf_t *msg_read)
{
	static char	string[2048];
	int		l,c;

	l = 0;
	do {
		c = MSG_ReadByte(msg_read);
		if (c == -1 || c == 0 || c == '\n')
			break;
		string[l] = c;
		l++;
	} while (l < sizeof(string)-1);

	string[l] = 0;

	return string;
}

/**
  * @brief
  */
float MSG_ReadCoord (sizebuf_t *msg_read)
{
	return (float)MSG_ReadLong(msg_read) * (1.0/32);
}

/**
  * @brief
  */
void MSG_ReadPos (sizebuf_t *msg_read, vec3_t pos)
{
	pos[0] = MSG_ReadShort(msg_read) * (1.0/8);
	pos[1] = MSG_ReadShort(msg_read) * (1.0/8);
	pos[2] = MSG_ReadShort(msg_read) * (1.0/8);
}

/**
  * @brief
  */
void MSG_ReadGPos (sizebuf_t *msg_read, pos3_t pos)
{
	pos[0] = MSG_ReadByte(msg_read);
	pos[1] = MSG_ReadByte(msg_read);
	pos[2] = MSG_ReadByte(msg_read);
}

/**
  * @brief
  */
float MSG_ReadAngle (sizebuf_t *msg_read)
{
	return (float)MSG_ReadChar(msg_read) * (360.0/256);
}

/**
  * @brief
  */
float MSG_ReadAngle16 (sizebuf_t *msg_read)
{
	return (float)SHORT2ANGLE(MSG_ReadShort(msg_read));
}

/**
  * @brief
  */
void MSG_ReadData (sizebuf_t *msg_read, void *data, int len)
{
	int	i;

	for (i=0 ; i<len ; i++)
		((byte *)data)[i] = MSG_ReadByte (msg_read);
}

/**
  * @brief
  */
void MSG_ReadDir (sizebuf_t *sb, vec3_t dir)
{
	int	b;

	b = MSG_ReadByte (sb);
	if (b >= NUMVERTEXNORMALS)
		Com_Error (ERR_DROP, "MSF_ReadDir: out of range");
	VectorCopy (bytedirs[b], dir);
}


/**
  * @brief
  */
void MSG_ReadFormat( sizebuf_t *msg_read, char *format, ... )
{
	va_list	ap;
	char	typeID;

	/* initialize ap */
	va_start( ap, format );

	while ( *format ) {
		typeID = *format++;

		switch ( typeID ) {
		case 'c':
			*va_arg( ap, int* ) = MSG_ReadChar( msg_read );
			break;
		case 'b':
			*va_arg( ap, int* ) = MSG_ReadByte( msg_read );
			break;
		case 's':
			*va_arg( ap, int* ) = MSG_ReadShort( msg_read );
			break;
		case 'l':
			*va_arg( ap, int* ) = MSG_ReadLong( msg_read );
			break;
		case 'f':
			*va_arg( ap, float* ) = MSG_ReadFloat( msg_read );
			break;
		case 'p':
			MSG_ReadPos( msg_read, *va_arg( ap, vec3_t* ) );
			break;
		case 'g':
			MSG_ReadGPos( msg_read, *va_arg( ap, pos3_t* ) );
			break;
		case 'd':
			MSG_ReadDir( msg_read, *va_arg( ap, vec3_t* ) );
			break;
		case 'a':
			*va_arg( ap, float* ) = MSG_ReadAngle( msg_read );
			break;
		case '!':
			format++;
			break;
		case '*':
			{
				int	i, n;
				byte	*p;

				n = MSG_ReadByte( msg_read );
				*va_arg( ap, int* ) = n;
				p = va_arg( ap, void* );
				for ( i = 0; i < n; i++ )
					*p++ = MSG_ReadByte( msg_read );
			}
			break;
		default:
			Com_Error( ERR_DROP, "ReadFormat: Unknown type!\n" );
		}
	}

	va_end( ap );
}


/**
 * @brief returns the length of a sizebuf_t
 *
 * calculated the length of a sizebuf_t by summing up
 * the size of each format char
 */
int MSG_LengthFormat( sizebuf_t *sb, char *format )
{
	char	typeID;
	int	length, delta;
	int	oldCount;

	length = 0;
	delta = 0;
	oldCount = sb->readcount;

	while ( *format ) {
		typeID = *format++;

		switch ( typeID ) {
		case 'c': delta = 1; break;
		case 'b': delta = 1; break;
		case 's': delta = 2; break;
		case 'l': delta = 4; break;
		case 'f': delta = 4; break;
		case 'p': delta = 6; break;
		case 'g': delta = 3; break;
		case 'd': delta = 1; break;
		case 'a': delta = 1; break;
		case '!': break;
		case '*': delta = MSG_ReadByte( sb ); length++; break;
		case '&': delta = 1; while ( MSG_ReadByte( sb ) != NONE ) length++; break;
		default:
			Com_Error( ERR_DROP, "LengthFormat: Unknown type!\n" );
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
void SZ_Init (sizebuf_t *buf, byte *data, int length)
{
	memset (buf, 0, sizeof(*buf));
	buf->data = data;
	buf->maxsize = length;
}

/**
  * @brief
  */
void SZ_Clear (sizebuf_t *buf)
{
	buf->cursize = 0;
	buf->overflowed = qfalse;
}

/**
  * @brief
  */
void *SZ_GetSpace (sizebuf_t *buf, int length)
{
	void	*data;

	if (buf->cursize + length > buf->maxsize) {
		if (!buf->allowoverflow)
			Com_Error (ERR_FATAL, "SZ_GetSpace: overflow without allowoverflow set" );

		if (length > buf->maxsize)
			Com_Error (ERR_FATAL, "SZ_GetSpace: %i is > full buffer size", length);

		Com_Printf ("SZ_GetSpace: overflow\n");
		SZ_Clear (buf);
		buf->overflowed = qtrue;
	}

	data = buf->data + buf->cursize;
	buf->cursize += length;

	return data;
}

/**
  * @brief
  */
void SZ_Write (sizebuf_t *buf, void *data, int length)
{
	memcpy (SZ_GetSpace(buf,length),data,length);
}

/**
  * @brief
  */
void SZ_Print (sizebuf_t *buf, char *data)
{
	int		len;

	len = strlen(data)+1;

	if (buf->cursize) {
		if (buf->data[buf->cursize-1])
			memcpy ((byte *)SZ_GetSpace(buf, len),data,len); /* no trailing 0 */
		else
			memcpy ((byte *)SZ_GetSpace(buf, len-1)-1,data,len); /* write over trailing 0 */
	} else
		memcpy ((byte *)SZ_GetSpace(buf, len),data,len);
}


/*============================================================================ */


/**
  * @brief
  *
  * Returns the position (1 to argc-1) in the program's argument list
  * where the given parameter apears, or 0 if not present
  */
int COM_CheckParm (char *parm)
{
	int		i;

	for (i=1 ; i<com_argc ; i++) {
		if (!strcmp (parm,com_argv[i]))
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
  * @brief
  */
void COM_ClearArgv (int arg)
{
	if (arg < 0 || arg >= com_argc || !com_argv[arg])
		return;
	com_argv[arg] = "";
}


/**
  * @brief
  */
void COM_InitArgv (int argc, char **argv)
{
	int		i;

	if (argc > MAX_NUM_ARGVS)
		Com_Error (ERR_FATAL, "argc > MAX_NUM_ARGVS");
	com_argc = argc;
	for (i=0 ; i<argc ; i++) {
		if (!argv[i] || strlen(argv[i]) >= MAX_TOKEN_CHARS )
			com_argv[i] = "";
		else
			com_argv[i] = argv[i];
	}
}

/**
  * @brief Adds the given string at the end of the current argument list
  */
void COM_AddParm (char *parm)
{
	if (com_argc == MAX_NUM_ARGVS)
		Com_Error (ERR_FATAL, "COM_AddParm: MAX_NUM)ARGS");
	com_argv[com_argc++] = parm;
}

/**
  * @brief
  *
  * just for debugging
  */
int	memsearch (byte *start, int count, int search)
{
	int		i;

	for (i=0 ; i<count ; i++)
		if (start[i] == search)
			return i;
	return -1;
}


/**
  * @brief
  */
char *CopyString (char *in)
{
	char	*out;
	int l = strlen(in);

	out = Z_Malloc (l+1);
	Q_strncpyz (out, in, l+1);
	return out;
}


/**
  * @brief
  */
void Info_Print (char *s)
{
	char	key[512];
	char	value[512];
	char	*o;
	int		l;

	if (*s == '\\')
		s++;
	while (*s) {
		o = key;
		while (*s && *s != '\\')
			*o++ = *s++;

		l = o - key;
		if (l < 20) {
			memset (o, ' ', 20-l);
			key[20] = 0;
		}
		else
			*o = 0;
		Com_Printf ("%s", key);

		if (!*s) {
			Com_Printf ("MISSING VALUE\n");
			return;
		}

		o = value;
		s++;
		while (*s && *s != '\\')
			*o++ = *s++;
		*o = 0;

		if (*s)
			s++;
		Com_Printf ("%s\n", value);
	}
}


/*
==============================================================================

ZONE MEMORY ALLOCATION

just cleared malloc with counters now...

==============================================================================
*/

#define	Z_MAGIC		0x1d1d

typedef struct zhead_s
{
	struct zhead_s	*prev, *next;
	short	magic;
	short	tag;			/* for group free */
	int		size;
} zhead_t;

zhead_t		z_chain;
int		z_count, z_bytes;

/**
  * @brief
  *
  * Frees a Z_Malloc'ed pointer
  */
void Z_Free (void *ptr)
{
	zhead_t	*z;

	if ( ! ptr )
		return;

	z = ((zhead_t *)ptr) - 1;

	if (z->magic != Z_MAGIC)
		Com_Error (ERR_FATAL, "Z_Free: bad magic (%i)", z->magic );

	z->prev->next = z->next;
	z->next->prev = z->prev;

	z_count--;
	z_bytes -= z->size;
	free (z);
}


/**
  * @brief
  *
  * Stats about the allocated bytes via Z_Malloc
  */
void Z_Stats_f (void)
{
	Com_Printf ("%i bytes in %i blocks\n", z_bytes, z_count);
}

/**
  * @brief
  *
  * Frees a memory block with a given tag
  */
void Z_FreeTags (int tag)
{
	zhead_t	*z, *next;

	for (z=z_chain.next ; z != &z_chain ; z=next) {
		next = z->next;
		if (z->tag == tag)
			Z_Free ((void *)(z+1));
	}
}

/**
  * @brief
  *
  * Allocates a memory block with a given tag
  */
void *Z_TagMalloc (int size, int tag)
{
	zhead_t	*z;

	size = size + sizeof(zhead_t);
	z = malloc(size);
	if (!z)
		Com_Error (ERR_FATAL, "Z_Malloc: failed on allocation of %i bytes",size);
	memset (z, 0, size);
	z_count++;
	z_bytes += size;
	z->magic = Z_MAGIC;
	z->tag = tag;
	z->size = size;

	z->next = z_chain.next;
	z->prev = &z_chain;
	z_chain.next->prev = z;
	z_chain.next = z;

	return (void *)(z+1);
}

/**
  * @brief
  *
  * Allocate a memory block with default tag
  */
void *Z_Malloc (int size)
{
	return Z_TagMalloc (size, 0);
}


/*============================================================================ */


static byte chktbl[1024] = {
0x84, 0x47, 0x51, 0xc1, 0x93, 0x22, 0x21, 0x24, 0x2f, 0x66, 0x60, 0x4d, 0xb0, 0x7c, 0xda,
0x88, 0x54, 0x15, 0x2b, 0xc6, 0x6c, 0x89, 0xc5, 0x9d, 0x48, 0xee, 0xe6, 0x8a, 0xb5, 0xf4,
0xcb, 0xfb, 0xf1, 0x0c, 0x2e, 0xa0, 0xd7, 0xc9, 0x1f, 0xd6, 0x06, 0x9a, 0x09, 0x41, 0x54,
0x67, 0x46, 0xc7, 0x74, 0xe3, 0xc8, 0xb6, 0x5d, 0xa6, 0x36, 0xc4, 0xab, 0x2c, 0x7e, 0x85,
0xa8, 0xa4, 0xa6, 0x4d, 0x96, 0x19, 0x19, 0x9a, 0xcc, 0xd8, 0xac, 0x39, 0x5e, 0x3c, 0xf2,
0xf5, 0x5a, 0x72, 0xe5, 0xa9, 0xd1, 0xb3, 0x23, 0x82, 0x6f, 0x29, 0xcb, 0xd1, 0xcc, 0x71,
0xfb, 0xea, 0x92, 0xeb, 0x1c, 0xca, 0x4c, 0x70, 0xfe, 0x4d, 0xc9, 0x67, 0x43, 0x47, 0x94,
0xb9, 0x47, 0xbc, 0x3f, 0x01, 0xab, 0x7b, 0xa6, 0xe2, 0x76, 0xef, 0x5a, 0x7a, 0x29, 0x0b,
0x51, 0x54, 0x67, 0xd8, 0x1c, 0x14, 0x3e, 0x29, 0xec, 0xe9, 0x2d, 0x48, 0x67, 0xff, 0xed,
0x54, 0x4f, 0x48, 0xc0, 0xaa, 0x61, 0xf7, 0x78, 0x12, 0x03, 0x7a, 0x9e, 0x8b, 0xcf, 0x83,
0x7b, 0xae, 0xca, 0x7b, 0xd9, 0xe9, 0x53, 0x2a, 0xeb, 0xd2, 0xd8, 0xcd, 0xa3, 0x10, 0x25,
0x78, 0x5a, 0xb5, 0x23, 0x06, 0x93, 0xb7, 0x84, 0xd2, 0xbd, 0x96, 0x75, 0xa5, 0x5e, 0xcf,
0x4e, 0xe9, 0x50, 0xa1, 0xe6, 0x9d, 0xb1, 0xe3, 0x85, 0x66, 0x28, 0x4e, 0x43, 0xdc, 0x6e,
0xbb, 0x33, 0x9e, 0xf3, 0x0d, 0x00, 0xc1, 0xcf, 0x67, 0x34, 0x06, 0x7c, 0x71, 0xe3, 0x63,
0xb7, 0xb7, 0xdf, 0x92, 0xc4, 0xc2, 0x25, 0x5c, 0xff, 0xc3, 0x6e, 0xfc, 0xaa, 0x1e, 0x2a,
0x48, 0x11, 0x1c, 0x36, 0x68, 0x78, 0x86, 0x79, 0x30, 0xc3, 0xd6, 0xde, 0xbc, 0x3a, 0x2a,
0x6d, 0x1e, 0x46, 0xdd, 0xe0, 0x80, 0x1e, 0x44, 0x3b, 0x6f, 0xaf, 0x31, 0xda, 0xa2, 0xbd,
0x77, 0x06, 0x56, 0xc0, 0xb7, 0x92, 0x4b, 0x37, 0xc0, 0xfc, 0xc2, 0xd5, 0xfb, 0xa8, 0xda,
0xf5, 0x57, 0xa8, 0x18, 0xc0, 0xdf, 0xe7, 0xaa, 0x2a, 0xe0, 0x7c, 0x6f, 0x77, 0xb1, 0x26,
0xba, 0xf9, 0x2e, 0x1d, 0x16, 0xcb, 0xb8, 0xa2, 0x44, 0xd5, 0x2f, 0x1a, 0x79, 0x74, 0x87,
0x4b, 0x00, 0xc9, 0x4a, 0x3a, 0x65, 0x8f, 0xe6, 0x5d, 0xe5, 0x0a, 0x77, 0xd8, 0x1a, 0x14,
0x41, 0x75, 0xb1, 0xe2, 0x50, 0x2c, 0x93, 0x38, 0x2b, 0x6d, 0xf3, 0xf6, 0xdb, 0x1f, 0xcd,
0xff, 0x14, 0x70, 0xe7, 0x16, 0xe8, 0x3d, 0xf0, 0xe3, 0xbc, 0x5e, 0xb6, 0x3f, 0xcc, 0x81,
0x24, 0x67, 0xf3, 0x97, 0x3b, 0xfe, 0x3a, 0x96, 0x85, 0xdf, 0xe4, 0x6e, 0x3c, 0x85, 0x05,
0x0e, 0xa3, 0x2b, 0x07, 0xc8, 0xbf, 0xe5, 0x13, 0x82, 0x62, 0x08, 0x61, 0x69, 0x4b, 0x47,
0x62, 0x73, 0x44, 0x64, 0x8e, 0xe2, 0x91, 0xa6, 0x9a, 0xb7, 0xe9, 0x04, 0xb6, 0x54, 0x0c,
0xc5, 0xa9, 0x47, 0xa6, 0xc9, 0x08, 0xfe, 0x4e, 0xa6, 0xcc, 0x8a, 0x5b, 0x90, 0x6f, 0x2b,
0x3f, 0xb6, 0x0a, 0x96, 0xc0, 0x78, 0x58, 0x3c, 0x76, 0x6d, 0x94, 0x1a, 0xe4, 0x4e, 0xb8,
0x38, 0xbb, 0xf5, 0xeb, 0x29, 0xd8, 0xb0, 0xf3, 0x15, 0x1e, 0x99, 0x96, 0x3c, 0x5d, 0x63,
0xd5, 0xb1, 0xad, 0x52, 0xb8, 0x55, 0x70, 0x75, 0x3e, 0x1a, 0xd5, 0xda, 0xf6, 0x7a, 0x48,
0x7d, 0x44, 0x41, 0xf9, 0x11, 0xce, 0xd7, 0xca, 0xa5, 0x3d, 0x7a, 0x79, 0x7e, 0x7d, 0x25,
0x1b, 0x77, 0xbc, 0xf7, 0xc7, 0x0f, 0x84, 0x95, 0x10, 0x92, 0x67, 0x15, 0x11, 0x5a, 0x5e,
0x41, 0x66, 0x0f, 0x38, 0x03, 0xb2, 0xf1, 0x5d, 0xf8, 0xab, 0xc0, 0x02, 0x76, 0x84, 0x28,
0xf4, 0x9d, 0x56, 0x46, 0x60, 0x20, 0xdb, 0x68, 0xa7, 0xbb, 0xee, 0xac, 0x15, 0x01, 0x2f,
0x20, 0x09, 0xdb, 0xc0, 0x16, 0xa1, 0x89, 0xf9, 0x94, 0x59, 0x00, 0xc1, 0x76, 0xbf, 0xc1,
0x4d, 0x5d, 0x2d, 0xa9, 0x85, 0x2c, 0xd6, 0xd3, 0x14, 0xcc, 0x02, 0xc3, 0xc2, 0xfa, 0x6b,
0xb7, 0xa6, 0xef, 0xdd, 0x12, 0x26, 0xa4, 0x63, 0xe3, 0x62, 0xbd, 0x56, 0x8a, 0x52, 0x2b,
0xb9, 0xdf, 0x09, 0xbc, 0x0e, 0x97, 0xa9, 0xb0, 0x82, 0x46, 0x08, 0xd5, 0x1a, 0x8e, 0x1b,
0xa7, 0x90, 0x98, 0xb9, 0xbb, 0x3c, 0x17, 0x9a, 0xf2, 0x82, 0xba, 0x64, 0x0a, 0x7f, 0xca,
0x5a, 0x8c, 0x7c, 0xd3, 0x79, 0x09, 0x5b, 0x26, 0xbb, 0xbd, 0x25, 0xdf, 0x3d, 0x6f, 0x9a,
0x8f, 0xee, 0x21, 0x66, 0xb0, 0x8d, 0x84, 0x4c, 0x91, 0x45, 0xd4, 0x77, 0x4f, 0xb3, 0x8c,
0xbc, 0xa8, 0x99, 0xaa, 0x19, 0x53, 0x7c, 0x02, 0x87, 0xbb, 0x0b, 0x7c, 0x1a, 0x2d, 0xdf,
0x48, 0x44, 0x06, 0xd6, 0x7d, 0x0c, 0x2d, 0x35, 0x76, 0xae, 0xc4, 0x5f, 0x71, 0x85, 0x97,
0xc4, 0x3d, 0xef, 0x52, 0xbe, 0x00, 0xe4, 0xcd, 0x49, 0xd1, 0xd1, 0x1c, 0x3c, 0xd0, 0x1c,
0x42, 0xaf, 0xd4, 0xbd, 0x58, 0x34, 0x07, 0x32, 0xee, 0xb9, 0xb5, 0xea, 0xff, 0xd7, 0x8c,
0x0d, 0x2e, 0x2f, 0xaf, 0x87, 0xbb, 0xe6, 0x52, 0x71, 0x22, 0xf5, 0x25, 0x17, 0xa1, 0x82,
0x04, 0xc2, 0x4a, 0xbd, 0x57, 0xc6, 0xab, 0xc8, 0x35, 0x0c, 0x3c, 0xd9, 0xc2, 0x43, 0xdb,
0x27, 0x92, 0xcf, 0xb8, 0x25, 0x60, 0xfa, 0x21, 0x3b, 0x04, 0x52, 0xc8, 0x96, 0xba, 0x74,
0xe3, 0x67, 0x3e, 0x8e, 0x8d, 0x61, 0x90, 0x92, 0x59, 0xb6, 0x1a, 0x1c, 0x5e, 0x21, 0xc1,
0x65, 0xe5, 0xa6, 0x34, 0x05, 0x6f, 0xc5, 0x60, 0xb1, 0x83, 0xc1, 0xd5, 0xd5, 0xed, 0xd9,
0xc7, 0x11, 0x7b, 0x49, 0x7a, 0xf9, 0xf9, 0x84, 0x47, 0x9b, 0xe2, 0xa5, 0x82, 0xe0, 0xc2,
0x88, 0xd0, 0xb2, 0x58, 0x88, 0x7f, 0x45, 0x09, 0x67, 0x74, 0x61, 0xbf, 0xe6, 0x40, 0xe2,
0x9d, 0xc2, 0x47, 0x05, 0x89, 0xed, 0xcb, 0xbb, 0xb7, 0x27, 0xe7, 0xdc, 0x7a, 0xfd, 0xbf,
0xa8, 0xd0, 0xaa, 0x10, 0x39, 0x3c, 0x20, 0xf0, 0xd3, 0x6e, 0xb1, 0x72, 0xf8, 0xe6, 0x0f,
0xef, 0x37, 0xe5, 0x09, 0x33, 0x5a, 0x83, 0x43, 0x80, 0x4f, 0x65, 0x2f, 0x7c, 0x8c, 0x6a,
0xa0, 0x82, 0x0c, 0xd4, 0xd4, 0xfa, 0x81, 0x60, 0x3d, 0xdf, 0x06, 0xf1, 0x5f, 0x08, 0x0d,
0x6d, 0x43, 0xf2, 0xe3, 0x11, 0x7d, 0x80, 0x32, 0xc5, 0xfb, 0xc5, 0xd9, 0x27, 0xec, 0xc6,
0x4e, 0x65, 0x27, 0x76, 0x87, 0xa6, 0xee, 0xee, 0xd7, 0x8b, 0xd1, 0xa0, 0x5c, 0xb0, 0x42,
0x13, 0x0e, 0x95, 0x4a, 0xf2, 0x06, 0xc6, 0x43, 0x33, 0xf4, 0xc7, 0xf8, 0xe7, 0x1f, 0xdd,
0xe4, 0x46, 0x4a, 0x70, 0x39, 0x6c, 0xd0, 0xed, 0xca, 0xbe, 0x60, 0x3b, 0xd1, 0x7b, 0x57,
0x48, 0xe5, 0x3a, 0x79, 0xc1, 0x69, 0x33, 0x53, 0x1b, 0x80, 0xb8, 0x91, 0x7d, 0xb4, 0xf6,
0x17, 0x1a, 0x1d, 0x5a, 0x32, 0xd6, 0xcc, 0x71, 0x29, 0x3f, 0x28, 0xbb, 0xf3, 0x5e, 0x71,
0xb8, 0x43, 0xaf, 0xf8, 0xb9, 0x64, 0xef, 0xc4, 0xa5, 0x6c, 0x08, 0x53, 0xc7, 0x00, 0x10,
0x39, 0x4f, 0xdd, 0xe4, 0xb6, 0x19, 0x27, 0xfb, 0xb8, 0xf5, 0x32, 0x73, 0xe5, 0xcb, 0x32
};

/**
  * @brief
  *
  * For proxy protecting
  */
byte	COM_BlockSequenceCRCByte (byte *base, int length, int sequence)
{
	int		n;
	byte	*p;
	int		x;
	byte chkb[60 + 4];
	unsigned short crc;


	if (sequence < 0)
		Sys_Error("sequence < 0, this shouldn't happen\n");

	p = chktbl + (sequence % (sizeof(chktbl) - 4));

	if (length > 60)
		length = 60;
	memcpy (chkb, base, length);

	chkb[length] = p[0];
	chkb[length+1] = p[1];
	chkb[length+2] = p[2];
	chkb[length+3] = p[3];

	length += 4;

	crc = CRC_Block(chkb, length);

	for (x=0, n=0; n<length; n++)
		x += chkb[n];

	crc = (crc ^ x) & 0xff;

	return (byte)crc;
}

/*======================================================== */

void Key_Init (void);
void SCR_EndLoadingPlaque (void);

/**
  * @brief
  *
  * Just throw a fatal error to
  * test error shutdown procedures
  */
void Com_Error_f (void)
{
	Com_Error (ERR_FATAL, "%s", Cmd_Argv(1));
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
  */
void Qcommon_LocaleInit ( void )
{
	char* locale;
#ifdef _WIN32
	char languageID[32];
#endif
	s_language = Cvar_Get("s_language", "", CVAR_ARCHIVE );
	s_language->modified = qfalse;

#ifdef _WIN32
	Com_sprintf( languageID, 32, "LANG=%s", s_language->string );
	Q_putenv( languageID );
#else
#ifndef SOLARIS
	unsetenv("LANGUAGE");
#endif
#ifdef __APPLE__
	if(setenv ("LANGUAGE", s_language->string, 1) == -1)
		Com_Printf("...setenv for LANGUAGE failed: %s\n", s_language->string );
	if(setenv ("LC_ALL", s_language->string, 1) == -1)
		Com_Printf("...setenv for LC_ALL failed: %s\n", s_language->string );
#endif
#endif

	/* set to system default */
	setlocale( LC_ALL, "C" );
	locale = setlocale( LC_MESSAGES, s_language->string );
	if ( ! locale ) {
		Com_Printf("...could not set to language: %s\n", s_language->string );
		locale = setlocale( LC_MESSAGES, "" );
		if ( !locale ) {
			Com_Printf("...could not set to system language\n" );
			return;
		}
	} else {
		Com_Printf("...using language: %s\n", locale );
		Cvar_Set("s_language", locale );
	}
	s_language->modified = qfalse;
}
#endif

/**
  * @brief Init function
  */
void Qcommon_Init (int argc, char **argv)
{
	char	*s;
#ifdef HAVE_GETTEXT
	/* i18n through gettext */
	char languagePath[MAX_OSPATH];
#endif

	if (setjmp (abortframe) )
		Sys_Error ("Error during initialization");

	z_chain.next = z_chain.prev = &z_chain;

	/* prepare enough of the subsystems to handle
	   cvar and command buffer management */
	COM_InitArgv (argc, argv);

	Swap_Init ();
	Cbuf_Init ();

	Cmd_Init ();
	Cvar_Init ();

	Key_Init ();

	/* we need to add the early commands twice, because
	   a basedir or cddir needs to be set before execing
	   config files, but we want other parms to override
	   the settings of the config files */
	Cbuf_AddEarlyCommands (qfalse);
	Cbuf_Execute ();

	FS_InitFilesystem ();

	Cbuf_AddText ("exec default.cfg\n");
	Cbuf_AddText ("exec config.cfg\n");
	Cbuf_AddText ("exec keys.cfg\n");

	Cbuf_AddEarlyCommands (qtrue);
	Cbuf_Execute ();

	/* init commands and vars */
	Cmd_AddCommand ("z_stats", Z_Stats_f);
	Cmd_AddCommand ("error", Com_Error_f);

	host_speeds = Cvar_Get ("host_speeds", "0", 0);
	log_stats = Cvar_Get ("log_stats", "0", 0);
	developer = Cvar_Get ("developer", "0", 0);
	timescale = Cvar_Get ("timescale", "1", 0);
	fixedtime = Cvar_Get ("fixedtime", "0", 0);
	logfile_active = Cvar_Get ("logfile", "1", 0);
	showtrace = Cvar_Get ("showtrace", "0", 0);
#ifdef DEDICATED_ONLY
	dedicated = Cvar_Get ("dedicated", "1", CVAR_NOSET);
#else
	dedicated = Cvar_Get ("dedicated", "0", CVAR_NOSET);
#endif

	s = va("UFO: Alien Invasion %s %s %s %s", UFO_VERSION, CPUSTRING, __DATE__, BUILDSTRING);
	Cvar_Get ("version", s, CVAR_SERVERINFO|CVAR_NOSET);
	Cvar_Get ("ver", va("%4.2f", UFO_VERSION), CVAR_NOSET);

	if (dedicated->value)
		Cmd_AddCommand ("quit", Com_Quit);

	Sys_Init ();

	NET_Init ();
	Netchan_Init ();

	SV_Init ();
	CL_Init ();

#ifdef HAVE_GETTEXT
	/* i18n through gettext */
	setlocale( LC_ALL, "C" );
	setlocale( LC_MESSAGES, "" );
	/* use system locale dir if we can't find in gamedir */
	Com_sprintf( languagePath, MAX_QPATH, "%s/base/i18n/", FS_GetCwd() );
	Com_DPrintf("...using mo files from %s\n", languagePath );
	bindtextdomain( TEXT_DOMAIN, languagePath );
	bind_textdomain_codeset( TEXT_DOMAIN, "UTF-8" );
	/* load language file */
	textdomain( TEXT_DOMAIN );

	Qcommon_LocaleInit();
#else
	Com_Printf("..no gettext compiled into this binary\n");
#endif

	Com_ParseScripts ();

	/* add + commands from command line
	   if the user didn't give any commands, run default action */
	if (!Cbuf_AddLateCommands () ) {
		if (!dedicated->value)
			Cbuf_AddText ("init\n");
		else
			Cbuf_AddText ("dedicated_start\n");
		Cbuf_Execute ();
	/* the user asked for something explicit */
	} else {
		/* so drop the loading plaque */
		SCR_EndLoadingPlaque ();
	}

#ifndef DEDICATED_ONLY
	if ( (int)Cvar_VariableValue("cl_precachemenus") )
		MN_PrecacheMenus();
#endif

	Com_Printf ("====== UFO Initialized ======\n\n");
}

/**
  * @brief
  */
float Qcommon_Frame (int msec)
{
	char	*s;
	int		time_before = 0, time_between = 0, time_after = 0;

	/* an ERR_DROP was thrown */
	if (setjmp (abortframe))
		return 1.0;

	if ( timescale->value > 5.0 )
		Cvar_SetValue( "timescale", 5.0 );
	else if ( timescale->value < 0.2 )
		Cvar_SetValue( "timescale", 0.2 );

	if ( log_stats->modified ) {
		log_stats->modified = qfalse;
		if ( log_stats->value ) {
			if ( log_stats_file ) {
				fclose( log_stats_file );
				log_stats_file = 0;
			}
			log_stats_file = fopen( "stats.log", "w" );
			if ( log_stats_file )
				fprintf( log_stats_file, "entities,dlights,parts,frame time\n" );
		} else {
			if ( log_stats_file ) {
				fclose( log_stats_file );
				log_stats_file = 0;
			}
		}
	}

/*	if (fixedtime->value) */
/*		msec = fixedtime->value; */

	if (showtrace->value) {
		extern	int c_traces, c_brush_traces;
		extern	int	c_pointcontents;

		Com_Printf ("%4i traces  %4i points\n", c_traces, c_pointcontents);
		c_traces = 0;
		c_brush_traces = 0;
		c_pointcontents = 0;
	}

	do {
		s = Sys_ConsoleInput ();
		if (s)
			Cbuf_AddText (va("%s\n",s));
	} while (s);
	Cbuf_Execute ();

	if (host_speeds->value)
		time_before = Sys_Milliseconds ();

	SV_Frame (msec);

	if (host_speeds->value)
		time_between = Sys_Milliseconds ();

	CL_Frame (msec);

	if (host_speeds->value)
		time_after = Sys_Milliseconds ();


	if (host_speeds->value) {
		int			all, sv, gm, cl, rf;

		all = time_after - time_before;
		sv = time_between - time_before;
		cl = time_after - time_between;
		gm = time_after_game - time_before_game;
		rf = time_after_ref - time_before_ref;
		sv -= gm;
		cl -= rf;
		Com_Printf ("all:%3i sv:%3i gm:%3i cl:%3i rf:%3i\n",
			all, sv, gm, cl, rf);
	}

	return timescale->value;
}

/**
  * @brief
  */
void Qcommon_Shutdown (void)
{
}

