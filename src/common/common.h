/**
 * @file common.h
 * @brief definitions common between client and server, but not game lib
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

#ifndef QCOMMON_DEFINED
#define QCOMMON_DEFINED

#include "../game/q_shared.h"
#include "../game/inv_shared.h"
#include "../game/game.h"
#include "mem.h"
#include "http.h"

#define UFO_VERSION "2.2-dev"

#ifdef _WIN32
#  ifdef DEBUG
#    define BUILDSTRING "Win32 DEBUG"
#  else
#    define BUILDSTRING "Win32 RELEASE"
#  endif
#  ifndef SHARED_EXT
#    define SHARED_EXT "dll"
#  endif
#  if defined __i386__
#    define CPUSTRING "x86"
#  elif defined __x86_64__
#    define CPUSTRING "x86_64"
#  elif defined __ia64__
#    define CPUSTRING "x86_64"
#  elif defined __alpha__
#    define CPUSTRING "AXP"
#  else
#    define CPUSTRING "Unknown"
#  endif

#elif defined __linux__
#  ifdef DEBUG
#    define BUILDSTRING "Linux DEBUG"
#  else
#    define BUILDSTRING "Linux RELEASE"
#  endif
#  ifndef SHARED_EXT
#    define SHARED_EXT "so"
#  endif
#  if defined  __i386__
#    define CPUSTRING "i386"
#  elif defined __x86_64__
#    define CPUSTRING "x86_64"
#  elif defined __alpha__
#    define CPUSTRING "axp"
#  else
#    define CPUSTRING "Unknown"
#  endif

#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#  ifdef DEBUG
#    define BUILDSTRING "FreeBSD DEBUG"
#  else
#    define BUILDSTRING "FreeBSD RELEASE"
#  endif
#  ifndef SHARED_EXT
#    define SHARED_EXT "so"
#  endif
#  if defined  __i386__
#    define CPUSTRING "i386"
#  elif defined __x86_64__
#    define CPUSTRING "x86_64"
#  elif defined __alpha__
#    define CPUSTRING "axp"
#  else
#    define CPUSTRING "Unknown"
#  endif

#elif defined __sun
#  ifdef DEBUG
#    define BUILDSTRING "Solaris DEBUG"
#  else
#    define BUILDSTRING "Solaris RELEASE"
#  endif
#  ifndef SHARED_EXT
#    define SHARED_EXT "so"
#  endif
#  if defined __i386__
#    define CPUSTRING "i386"
#  else
#    define CPUSTRING "sparc"
#  endif

#elif defined (__APPLE__) || defined (MACOSX)
#  ifdef DEBUG
#    define BUILDSTRING "MacOSX DEBUG"
#  else
#    define BUILDSTRING "MacOSX RELEASE"
#  endif
#  ifndef SHARED_EXT
#    define SHARED_EXT "dylib"
#  endif
#  if defined __i386__
#    define CPUSTRING "i386"
#  elif defined __powerpc__
#    define CPUSTRING "ppc"
#  else
#    define CPUSTRING "Unknown"
#  endif

#else
#  define BUILDSTRING "NON-WIN32"
#  define CPUSTRING	"NON-WIN32"
#endif

#include "common.h"

#define MASTER_SERVER "http://ufoai.ninex.info/" /* sponsored by NineX */


/*
==============================================================
IRC
==============================================================
*/
void Irc_Init(void);
void Irc_Shutdown(void);

/* client side */
void Irc_Logic_Frame(int now, void *data);
void Irc_Input_KeyEvent(int key);
void Irc_Input_Deactivate(void);
void Irc_Input_Activate(void);

/*
==============================================================
PROTOCOL
==============================================================
*/

/* protocol.h -- communications protocols */

#define	PROTOCOL_VERSION	3

#define	PORT_CLIENT	27901
#define	PORT_SERVER	27910

/**
 * @brief server to client
 *
 * the svc_strings[] array in cl_parse.c should mirror this
 */
enum svc_ops_e {
	svc_bad,

	/** these ops are known to the game dll */
	svc_inventory,

	/** the rest are private to the client and server */
	svc_nop,
	svc_disconnect,
	svc_reconnect,
	svc_sound,					/**< <see code> */
	svc_print,					/**< [byte] id [string] null terminated string */
	svc_stufftext,				/**< [string] stuffed into client's console buffer, should be \n terminated */
	svc_serverdata,				/**< [long] protocol ... */
	svc_configstring,			/**< [short] [string] */
	svc_spawnbaseline,
	svc_centerprint,			/**< [string] to put in center of the screen */
	svc_playerinfo,				/**< variable */
	svc_event,					/**< ... */
	svc_oob = 0xff
};

/*============================================== */

/**
 * @brief client to server
 */
enum clc_ops_e {
	clc_bad,
	clc_nop,
	clc_endround,
	clc_teaminfo,
	clc_action,
	clc_userinfo,				/**< [[userinfo string] */
	clc_stringcmd,				/**< [string] message */
	clc_oob = 0xff				/**< out of band - connectionless */
};


/*============================================== */

/* a sound without an ent or pos will be a local only sound */
#define	SND_VOLUME		(1<<0)	/* a byte */
#define	SND_ATTENUATION	(1<<1)	/* a byte */
#define	SND_POS			(1<<2)	/* three coordinates */
#define	SND_ENT			(1<<3)	/* a short 0-2: channel, 3-12: entity */

#define DEFAULT_SOUND_PACKET_VOLUME	0.8
#define DEFAULT_SOUND_PACKET_ATTENUATION 0.02

/*============================================== */

#include "cmd.h"

#include "cvar.h"

#include "cmodel.h"

#include "filesys.h"

#include "scripts.h"

#include "net.h"

#include "netpack.h"

/*
==============================================================
MISC
==============================================================
*/

#define ANGLE2SHORT(x)		((int)((x)*65536/360) & 65535)
#define SHORT2ANGLE(x)		((x)*(360.0/65536))

#define	ERR_FATAL	0			/* exit the entire game with a popup window */
#define	ERR_DROP	1			/* print to console and disconnect from game */
#define	ERR_QUIT	2			/* not an error, just a normal exit */

void Com_BeginRedirect(int target, char *buffer, int buffersize, void (*flush) (int, char *));
void Com_EndRedirect(void);
void Com_Printf(const char *msg, ...) __attribute__((format(printf, 1, 2)));
void Com_DPrintf(int level, const char *msg, ...) __attribute__((format(printf, 2, 3)));
void Com_Error(int code, const char *fmt, ...) __attribute__((noreturn, format(printf, 2, 3)));
void Com_Drop(void);
void Com_Quit(void);

int Com_ServerState(void);		/* this should have just been a cvar... */
void Com_SetServerState(int state);

#include "md4.h"
char *Com_MD5File(const char *fn, int length);

extern cvar_t *http_proxy;
extern cvar_t *developer;
extern cvar_t *sv_dedicated;
extern cvar_t *host_speeds;
extern cvar_t *sv_maxclients;
extern cvar_t *sv_reaction_leftover;
extern cvar_t *sv_shot_origin;
extern cvar_t *cl_maxfps;
extern cvar_t *teamnum;
extern cvar_t *gametype;
extern cvar_t *masterserver_url;
extern cvar_t *port;

extern cvar_t *con_fontHeight;
extern cvar_t *con_font;
extern cvar_t *con_fontWidth;
extern cvar_t *con_fontShift;

/* host_speeds times */
extern int time_before_game;
extern int time_after_game;
extern int time_before_ref;
extern int time_after_ref;

extern csi_t csi;

extern char map_entitystring[MAX_MAP_ENTSTRING];


#define MAX_CVARLISTINGAMETYPE 16
typedef struct cvarlist_s {
	char name[MAX_VAR];
	char value[MAX_VAR];
} cvarlist_t;

typedef struct gametype_s {
	char id[MAX_VAR];	/**< script id */
	char name[MAX_VAR];	/**< translated menu name */
	struct cvarlist_s cvars[MAX_CVARLISTINGAMETYPE];
	int num_cvars;
} gametype_t;

/** game types */
#define MAX_GAMETYPES 16
extern gametype_t gts[MAX_GAMETYPES];
extern int numGTs;

void Qcommon_Init(int argc, const char **argv);
void Qcommon_Frame(void);
void Qcommon_Shutdown(void);
void Com_SetGameType(void);

#define NUMVERTEXNORMALS	162
extern const vec3_t bytedirs[NUMVERTEXNORMALS];

/** this is in the client code, but can be used for debugging from server */
void SCR_DebugGraph(float value, int color);
void Con_Print(const char *txt);

/* Event timing */

typedef void event_func(int now, void *data);
void Schedule_Event(int when, event_func *func, void *data);
void Schedule_Timer(cvar_t *interval, event_func *func, void *data);

/*
==============================================================
NON-PORTABLE SYSTEM SERVICES
==============================================================
*/

void Sys_Init(void);
void Sys_NormPath(char *path);
void Sys_OSPath(char* path);
void Sys_Sleep(int milliseconds);
const char *Sys_GetCurrentUser(void);

void Sys_UnloadGame(void);
/** loads the game dll and calls the api init function */
game_export_t *Sys_GetGameAPI(game_import_t * parms);

#define MAXCMDLINE 256
char *Sys_ConsoleInput(void);
void Sys_ConsoleOutput(const char *string);
void Sys_Error(const char *error, ...) __attribute__((noreturn, format(printf, 1, 2)));
void Sys_Quit(void);
char *Sys_GetHomeDirectory(void);

void Sys_ConsoleShutdown(void);
void Sys_ConsoleInit(void);
void Sys_ShowConsole(qboolean show);

void *Sys_LoadLibrary(const char *name, int flags);
void Sys_FreeLibrary(void *libHandle);
void *Sys_GetProcAddress(void *libHandle, const char *procName);

/*
==============================================================
CLIENT / SERVER SYSTEMS
==============================================================
*/

void CL_Init(void);
void CL_Drop(void);
void CL_Shutdown(void);
void CL_Frame(int now, void *data);
void CL_SlowFrame(int now, void *data);
void CL_ParseClientData(const char *type, const char *name, const char **text);
void CIN_RunCinematic(int now, void *data);
void SCR_BeginLoadingPlaque(void);
void CL_InitAfter(void);

void SV_Init(void);
void SV_Clear(void);
void SV_Shutdown(const char *finalmsg, qboolean reconnect);
void SV_ShutdownWhenEmpty(void);
void SV_Frame(int now, void *);

/*============================================================================ */

extern struct memPool_s *com_aliasSysPool;
extern struct memPool_s *com_cmdSysPool;
extern struct memPool_s *com_cmodelSysPool;
extern struct memPool_s *com_cvarSysPool;
extern struct memPool_s *com_fileSysPool;
extern struct memPool_s *com_genericPool;

/*============================================================================ */

struct usercmd_s;
struct entity_state_s;

/*============================================================================ */

int COM_Argc(void);
const char *COM_Argv(int arg);		/* range and null checked */
void COM_ClearArgv(int arg);
unsigned int Com_HashKey(const char *name, int hashsize);
int COM_CheckParm(const char *parm);
void COM_AddParm(const char *parm);

void COM_Init(void);
void COM_InitArgv(int argc, const char **argv);

void Key_Init(void);
void SCR_EndLoadingPlaque(void);

#endif							/* QCOMMON_DEFINED */
