/**
 * @file netpack.c
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

#include "common.h"

/* writing functions */

void NET_WriteChar (struct dbuffer *buf, char c)
{
	dbuffer_add(buf, &c, 1);
}

void NET_WriteByte (struct dbuffer *buf, unsigned char c)
{
	dbuffer_add(buf, (char *)&c, 1);
}

void NET_WriteShort (struct dbuffer *buf, int c)
{
	unsigned short v = LittleShort(c);
	dbuffer_add(buf, (char *)&v, 2);
}

void NET_WriteLong (struct dbuffer *buf, int c)
{
	int v = LittleLong(c);
	dbuffer_add(buf, (char *)&v, 4);
}

void NET_WriteString (struct dbuffer *buf, const char *str)
{
	if (!str)
		dbuffer_add(buf, "", 1);
	else
		dbuffer_add(buf, str, strlen(str) + 1);
}

void NET_WriteRawString (struct dbuffer *buf, const char *str)
{
	if (str)
		dbuffer_add(buf, str, strlen(str));
}

void NET_WriteCoord (struct dbuffer *buf, float f)
{
	NET_WriteLong(buf, (int) (f * 32));
}

/**
 * @sa NET_Read2Pos
 */
void NET_Write2Pos (struct dbuffer *buf, vec2_t pos)
{
	NET_WriteLong(buf, (long) (pos[0] * 32.));
	NET_WriteLong(buf, (long) (pos[1] * 32.));
}

/**
 * @sa NET_ReadPos
 */
void NET_WritePos (struct dbuffer *buf, vec3_t pos)
{
	NET_WriteLong(buf, (long) (pos[0] * 32.));
	NET_WriteLong(buf, (long) (pos[1] * 32.));
	NET_WriteLong(buf, (long) (pos[2] * 32.));
}

void NET_WriteGPos (struct dbuffer *buf, pos3_t pos)
{
	NET_WriteByte(buf, pos[0]);
	NET_WriteByte(buf, pos[1]);
	NET_WriteByte(buf, pos[2]);
}

void NET_WriteAngle (struct dbuffer *buf, float f)
{
	NET_WriteByte(buf, (int) (f * 256 / 360) & 255);
}

void NET_WriteAngle16 (struct dbuffer *buf, float f)
{
	NET_WriteShort(buf, ANGLE2SHORT(f));
}


void NET_WriteDir (struct dbuffer *buf, vec3_t dir)
{
	int i, best;
	float d, bestd;

	if (!dir) {
		NET_WriteByte(buf, 0);
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
	NET_WriteByte(buf, best);
}


/**
 * @brief Writes to buffer according to format; version without syntactic sugar for variable arguments, to call it from other functions with variable arguments
 */
void NET_V_WriteFormat (struct dbuffer *buf, const char *format, va_list ap)
{
	char typeID;

	while (*format) {
		typeID = *format++;

		switch (typeID) {
		case 'c':
			NET_WriteChar(buf, va_arg(ap, int));

			break;
		case 'b':
			NET_WriteByte(buf, va_arg(ap, int));

			break;
		case 's':
			NET_WriteShort(buf, va_arg(ap, int));

			break;
		case 'l':
			NET_WriteLong(buf, va_arg(ap, int));

			break;
		case 'p':
			NET_WritePos(buf, va_arg(ap, float *));

			break;
		case 'g':
			NET_WriteGPos(buf, va_arg(ap, byte *));
			break;
		case 'd':
			NET_WriteDir(buf, va_arg(ap, float *));

			break;
		case 'a':
			/* NOTE: float is promoted to double through ... */
			NET_WriteAngle(buf, va_arg(ap, double));

			break;
		case '!':
			break;
		case '*':
			{
				int i, n;
				byte *p;

				n = va_arg(ap, int);

				p = va_arg(ap, byte *);
				NET_WriteShort(buf, n);
				for (i = 0; i < n; i++)
					NET_WriteByte(buf, *p++);
			}
			break;
		default:
			Com_Error(ERR_DROP, "WriteFormat: Unknown type!");
		}
	}
	/* Too many arguments for the given format; too few cause crash above */
	if (!ap)
		Com_Error(ERR_DROP, "WriteFormat: Too many arguments!");
}

/**
 * @brief The user-friendly version of NET_WriteFormat that writes variable arguments to buffer according to format
 */
void NET_WriteFormat (struct dbuffer *buf, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	NET_V_WriteFormat(buf, format, ap);
	va_end(ap);
}


/* reading functions */

/**
 * returns -1 if no more characters are available
 */
int NET_ReadChar (struct dbuffer *buf)
{
	char c;
	if (dbuffer_extract(buf, &c, 1) == 0)
		return -1;
	else
		return c;
}

/**
 * @brief Reads a byte from the netchannel
 * @note Beware that you don't put this into a byte or short - this will overflow
 * use an int value to store the return value!!!
 */
int NET_ReadByte (struct dbuffer *buf)
{
	unsigned char c;
	if (dbuffer_extract(buf, (char *)&c, 1) == 0)
		return -1;
	else
		return c;
}

int NET_ReadShort (struct dbuffer *buf)
{
	unsigned short v;
	if (dbuffer_extract(buf, (char *)&v, 2) < 2)
		return -1;

	return LittleShort(v);
}

int NET_ReadLong (struct dbuffer *buf)
{
	unsigned int v;
	if (dbuffer_extract(buf, (char *)&v, 4) < 4)
		return -1;

	return LittleLong(v);
}

/**
 * @brief Don't strip high bits - use this for utf-8 strings
 * @note Don't use this function in a way like
 * <code> char *s = NET_ReadStringRaw(sb);
 * char *t = NET_ReadStringRaw(sb);</code>
 * The second reading uses the same data buffer for the string - so
 * s is no longer the first - but the second string
 * @sa NET_ReadString
 * @sa NET_ReadStringLine
 */
char *NET_ReadStringRaw (struct dbuffer *buf)
{
	static char string[2048];
	unsigned int l;
	int c;

	l = 0;
	do {
		c = NET_ReadByte(buf);
		if (c == -1 || c == 0)
			break;
		string[l] = c;
		l++;
	} while (l < sizeof(string) - 1);

	string[l] = 0;

	return string;
}

/**
 * @note Don't use this function in a way like
 * <code> char *s = NET_ReadStringRaw(sb);
 * char *t = NET_ReadStringRaw(sb);</code>
 * The second reading uses the same data buffer for the string - so
 * s is no longer the first - but the second string
 * @note strip high bits - don't use this for utf-8 strings
 * @sa NET_ReadStringRaw
 * @sa NET_ReadStringLine
 */
char *NET_ReadString (struct dbuffer *buf)
{
	static char string[2048];
	unsigned int l;
	int c;

	l = 0;
	do {
		c = NET_ReadByte(buf);
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
 * @sa NET_ReadString
 * @sa NET_ReadStringRaw
 */
char *NET_ReadStringLine (struct dbuffer *buf)
{
	static char string[2048];
	unsigned int l;
	int c;

	l = 0;
	do {
		c = NET_ReadByte(buf);
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

float NET_ReadCoord (struct dbuffer *buf)
{
	return (float) NET_ReadLong(buf) * (1.0 / 32);
}

/**
 * @sa NET_Write2Pos
 */
void NET_Read2Pos (struct dbuffer *buf, vec2_t pos)
{
	pos[0] = NET_ReadLong(buf) / 32.;
	pos[1] = NET_ReadLong(buf) / 32.;
}

/**
 * @sa NET_WritePos
 */
void NET_ReadPos (struct dbuffer *buf, vec3_t pos)
{
	pos[0] = NET_ReadLong(buf) / 32.;
	pos[1] = NET_ReadLong(buf) / 32.;
	pos[2] = NET_ReadLong(buf) / 32.;
}

/**
 * @sa NET_WriteGPos
 * @sa NET_ReadByte
 * @note pos3_t are byte values
 */
void NET_ReadGPos (struct dbuffer *buf, pos3_t pos)
{
	pos[0] = NET_ReadByte(buf);
	pos[1] = NET_ReadByte(buf);
	pos[2] = NET_ReadByte(buf);
}

float NET_ReadAngle (struct dbuffer *buf)
{
	return (float) NET_ReadChar(buf) * (360.0 / 256);
}

float NET_ReadAngle16 (struct dbuffer *buf)
{
	short s;

	s = NET_ReadShort(buf);
	return (float) SHORT2ANGLE(s);
}

void NET_ReadData (struct dbuffer *buf, void *data, int len)
{
	int i;

	for (i = 0; i < len; i++)
		((byte *) data)[i] = NET_ReadByte(buf);
}

void NET_ReadDir (struct dbuffer *buf, vec3_t dir)
{
	int b;

	b = NET_ReadByte(buf);
	if (b >= NUMVERTEXNORMALS)
		Com_Error(ERR_DROP, "NET_ReadDir: out of range");
	VectorCopy(bytedirs[b], dir);
}


/**
 * @brief Reads from a buffer according to format; version without syntactic sugar for variable arguments, to call it from other functions with variable arguments
 * @sa PF_ReadFormat
 * @param[in] format The format string (see e.g. pa_format array) - may not be NULL
 */
void NET_V_ReadFormat (struct dbuffer *buf, const char *format, va_list ap)
{
	char typeID;

	assert(format); /* may not be null */
	while (*format) {
		typeID = *format++;

		switch (typeID) {
		case 'c':
			*va_arg(ap, int *) = NET_ReadChar(buf);
			break;
		case 'b':
			*va_arg(ap, int *) = NET_ReadByte(buf);
			break;
		case 's':
			*va_arg(ap, int *) = NET_ReadShort(buf);
			break;
		case 'l':
			*va_arg(ap, int *) = NET_ReadLong(buf);
			break;
		case 'p':
			NET_ReadPos(buf, *va_arg(ap, vec3_t *));
			break;
		case 'g':
			NET_ReadGPos(buf, *va_arg(ap, pos3_t *));
			break;
		case 'd':
			NET_ReadDir(buf, *va_arg(ap, vec3_t *));
			break;
		case 'a':
			*va_arg(ap, float *) = NET_ReadAngle(buf);
			break;
		case '!':
			format++;
			break;
		case '*':
			{
				int i, n;
				byte *p;

				n = NET_ReadShort(buf);
				*va_arg(ap, int *) = n;
				p = va_arg(ap, void *);

				for (i = 0; i < n; i++)
					*p++ = NET_ReadByte(buf);
			}
			break;
		default:
			Com_Error(ERR_DROP, "ReadFormat: Unknown type!");
		}
	}
	/* Too many arguments for the given format; too few cause crash above */
	if (!ap)
		Com_Error(ERR_DROP, "ReadFormat: Too many arguments!");
}

/**
 * @brief The user-friendly version of NET_ReadFormat that reads variable arguments from a buffer according to format
 */
void NET_ReadFormat (struct dbuffer *buf, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	NET_V_ReadFormat(buf, format, ap);
	va_end(ap);
}

/**
 * @brief Out of band print
 */
void NET_OOB_Printf (struct net_stream *s, const char *format, ...)
{
	va_list argptr;
	static char string[4096];
	char cmd = clc_oob;
	int len;

	va_start(argptr, format);
	Q_vsnprintf(string, sizeof(string), format, argptr);
	va_end(argptr);

	string[sizeof(string)-1] = 0;

	len = LittleLong(strlen(string) + 1);
	stream_enqueue(s, (char *)&len, 4);
	stream_enqueue(s, &cmd, 1);
	stream_enqueue(s, string, strlen(string));
}

/**
 * @brief Enqueue the buffer in the net stream for ONE client
 * @note Frees the msg buffer
 * @sa NET_WriteConstMsg
 */
void NET_WriteMsg (struct net_stream *s, struct dbuffer *buf)
{
	char tmp[4096];
	int len = LittleLong(dbuffer_len(buf));
	stream_enqueue(s, (char *)&len, 4);

	while (dbuffer_len(buf)) {
		int len = dbuffer_extract(buf, tmp, sizeof(tmp));
		stream_enqueue(s, tmp, len);
	}

	/* and now free the buffer */
	free_dbuffer(buf);
}

/**
 * @brief Enqueue the buffer in the net stream for MULTIPLE clients
 * @note Same as NET_WriteMsg but doesn't free the buffer, use this if you send
 * the same buffer to more than one connected clients
 * @note Make sure that you free the msg buffer after you called this
 * @sa NET_WriteMsg
 */
void NET_WriteConstMsg (struct net_stream *s, const struct dbuffer *buf)
{
	char tmp[4096];
	int len = LittleLong(dbuffer_len(buf));
	int pos = 0;
	stream_enqueue(s, (char *)&len, 4);

	while (pos < dbuffer_len(buf)) {
		int x = dbuffer_get_at(buf, pos, tmp, sizeof(tmp));
		stream_enqueue(s, tmp, x);
		pos += x;
	}
}

/**
 * @brief Reads messages from the network channel and adds them to the dbuffer
 * where you can use the NET_Read* functions to get the values in the correct
 * order
 * @sa stream_dequeue
 * @sa dbuffer_add
 */
struct dbuffer *NET_ReadMsg (struct net_stream *s)
{
	unsigned int v;
	unsigned int len;
	struct dbuffer *buf;
	char tmp[4096];
	if (stream_peek(s, (char *)&v, 4) < 4)
		return NULL;

	len = LittleLong(v);
	if (stream_length(s) < (4 + len))
		return NULL;

	stream_dequeue(s, tmp, 4);

	buf = new_dbuffer();
	while (len > 0) {
		int x = stream_dequeue(s, tmp, min(len, 4096));
		dbuffer_add(buf, tmp, x);
		len -= x;
	}

	return buf;
}

void NET_VPrintf (struct dbuffer *buf, const char *format, va_list ap)
{
	static char str[32768];
	int len = Q_vsnprintf(str, sizeof(str), format, ap);
	dbuffer_add(buf, str, len);
}

void NET_Printf (struct dbuffer *buf, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	NET_VPrintf(buf, format, ap);
	va_end(ap);
}
