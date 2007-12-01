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

#include "common.h"
#include "../server/server.h"
#include <setjmp.h>

#define	MAXPRINTMSG	4096
#define MAX_NUM_ARGVS	50

csi_t csi;

static int com_argc;
static const char *com_argv[MAX_NUM_ARGVS + 1];

jmp_buf abortframe;				/* an ERR_DROP occured, exit the entire frame */

cvar_t *s_sleep;
cvar_t *s_language;
cvar_t *developer;
cvar_t *http_proxy;
cvar_t *http_timeout;
cvar_t *logfile_active;			/* 1 = buffer log, 2 = flush after each print */
cvar_t *sv_dedicated;
cvar_t *cl_maxfps;
cvar_t *teamnum;
cvar_t *gametype;
cvar_t *masterserver_url;
cvar_t *port;

static FILE *logfile;

static int server_state;

struct memPool_s *com_aliasSysPool;
struct memPool_s *com_cmdSysPool;
struct memPool_s *com_cmodelSysPool;
struct memPool_s *com_cvarSysPool;
struct memPool_s *com_fileSysPool;
struct memPool_s *com_genericPool;

struct event {
	int when;
	event_func *func;
	void *data;
	struct event *next;
};

static struct event *event_queue = NULL;

#define TIMER_CHECK_INTERVAL 100
#define TIMER_CHECK_LAG 3
#define TIMER_LATENESS_HIGH 200
#define TIMER_LATENESS_LOW 50
#define TIMER_LATENESS_HISTORY 32

struct timer {
	cvar_t *min_freq;
	int interval;
	int recent_lateness[TIMER_LATENESS_HISTORY];
	int next_lateness;
	int total_lateness;
	int next_check;
	int checks_high;
	int checks_low;

	event_func *func;
	void *data;
};

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
 * @brief Redirect packets/ouput from server to client
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
 * @brief End the redirection of packets/output
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
		Q_strcat(rd_buffer, msg, sizeof(char) * rd_buffersize);
		return;
	}

#ifndef DEDICATED_ONLY
	Con_Print(msg);
#endif

	/* also echo to debugging console */
	Sys_ConsoleOutput(msg);

	/* logfile */
	if (logfile_active && logfile_active->integer) {
		char name[MAX_OSPATH];

		if (!logfile) {
			Com_sprintf(name, sizeof(name), "%s/ufoconsole.log", FS_Gamedir());
			if (logfile_active->integer > 2)
				logfile = fopen(name, "a");
			else
				logfile = fopen(name, "w");
		}
		if (logfile)
			fprintf(logfile, "%s", msg);
		if (logfile_active->integer > 1)
			fflush(logfile);	/* force it to save every time */
	}
}


/**
 * @brief A Com_Printf that only shows up if the "developer" cvar is set
 */
void Com_DPrintf (int level, const char *fmt, ...)
{
	va_list argptr;
	char msg[MAXPRINTMSG];

	/* don't confuse non-developers with techie stuff... */
	if (!developer || developer->integer == 0)
		return;

	if (developer->integer != DEBUG_ALL && developer->integer & ~level)
		return;

	va_start(argptr, fmt);
	Q_vsnprintf(msg, MAXPRINTMSG, fmt, argptr);
	va_end(argptr);

	msg[sizeof(msg)-1] = 0;

	Com_Printf("%s", msg);
}


/**
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

	Com_Printf("%s\n", msg);

	switch (code) {
	case ERR_DISCONNECT:
		Cvar_Set("mn_afterdrop", "popup");
		CL_Drop();
		recursive = qfalse;
		longjmp(abortframe, -1);
		break;
	case ERR_DROP:
		Com_Printf("********************\nERROR: %s\n********************\n", msg);
		SV_Shutdown("Server crashed\n", qfalse);
		CL_Drop();
		recursive = qfalse;
		longjmp(abortframe, -1);
		break;
	default:
		SV_Shutdown("Server fatal crashed: %s\n", qfalse);
		CL_Shutdown();
	}

	/* send an receive net messages a last time */
	NET_Wait(0);

	if (logfile) {
		fclose(logfile);
		logfile = NULL;
	}

	Sys_Error("Shutdown");
}


void Com_Drop (void)
{
	SV_Shutdown("Server disconnected\n", qfalse);
	CL_Drop();
	longjmp(abortframe, -1);
}


/**
 * Both client and server can use this, and it will
 * do the apropriate things.
 */
void Com_Quit (void)
{
	SV_Shutdown("Server quit\n", qfalse);
	SV_Clear();
	CL_Shutdown();
#ifdef DEDICATED_ONLY
	Cvar_WriteVariables(va("%s/config.cfg", FS_Gamedir()));
#endif
	/* send an receive net messages a last time */
	NET_Wait(0);
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
 * @sa SV_SpawnServer
 * @sa Com_ServerState
 */
void Com_SetServerState (int state)
{
	Com_DPrintf(DEBUG_ENGINE, "Set server state to %i\n", state);
	server_state = state;
}

/*=========================================================================== */

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
int COM_CheckParm (const char *parm)
{
	int i;

	for (i = 1; i < com_argc; i++) {
		if (!com_argv[i])
			continue;               /* NEXTSTEP sometimes clears appkit vars. */
		if (!Q_strcmp(parm, com_argv[i]))
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
const char *COM_Argv (int arg)
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
 * @sa COM_CheckParm
 */
void COM_InitArgv (int argc, const char **argv)
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
void COM_AddParm (const char *parm)
{
	if (com_argc == MAX_NUM_ARGVS)
		Com_Error(ERR_FATAL, "COM_AddParm: MAX_NUM)ARGS");
	com_argv[com_argc++] = parm;
}

/*======================================================== */

void Com_SetGameType (void)
{
	int i, j;
	gametype_t* gt;
	cvarlist_t* list;

	for (i = 0; i < numGTs; i++) {
		gt = &gts[i];
		if (!Q_strncmp(gt->id, gametype->string, MAX_VAR)) {
			if (sv_dedicated->integer)
				Com_Printf("set gametype to: %s\n", gt->id);
			for (j = 0, list = gt->cvars; j < gt->num_cvars; j++, list++) {
				Cvar_Set(list->name, list->value);
				if (sv_dedicated->integer)
					Com_Printf("  %s = %s\n", list->name, list->value);
			}
			/*Com_Printf("Make sure to restart the map if you switched during a game\n");*/
			break;
		}
	}

	if (i == numGTs)
		Com_Printf("Can't set the gametype - unknown value for cvar gametype: '%s'\n", gametype->string);
}

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

#ifdef DEBUG
/**
 * @brief Prints some debugging help to the game console
 */
static void Com_DebugHelp_f (void)
{
	Com_Printf("Debugging cvars:\n"
			"------------------------------\n"
			" * developer\n"
			" * g_nodamage\n"
			" * mn_debugmenu\n"
			"------------------------------\n"
			"\n"
			"Debugging commands:\n"
			"------------------------------\n"
			" * debug_additems\n"
			" * debug_aircraftlist\n"
			" * debug_baselist\n"
			" * debug_buildinglist\n"
			" * debug_campaignstats\n"
			" * debug_configstrings\n"
			" * debug_capacities\n"
			" * debug_destroyallufos\n"
			" * debug_drawblocked\n"
			"   prints forbidden list to console\n"
			" * debug_error\n"
			"   throw and error to test the Com_Error function\n"
			" * debug_fullcredits\n"
			"   restore to full credits\n"
			" * debug_hosp_hurt_all\n"
			" * debug_hosp_heal_all\n"
			" * debug_listufo\n"
			" * debug_menueditnode\n"
			" * debug_menuprint\n"
			" * debug_menureload\n"
			"   reload the menu if you changed it and don't want to restart\n"
			" * debug_newemployees\n"
			"   add 5 new unhired employees of each type\n"
			" * debug_researchall\n"
			"   mark all techs as researched\n"
			" * debug_researchableall\n"
			"   mark all techs as researchable\n"
			" * debug_showitems\n"
			" * debug_statsupdate\n"
			" * debug_techlist\n"
			" * killteam <teamid>\n"
			"   kills all living actors in the given team\n"
			" * sv showall\n"
			"   make everything visible to everyone\n"
			"------------------------------\n"
			"\n"
			"Network debugging:\n"
			"------------------------------\n"
			" * net_showdrop"
			"   console message if we have to drop a packet\n"
			" * net_showpackets"
			"   show packets (reliable and unreliable)\n"
			" * net_showpacketsreliable"
			"   only show reliable packets (net_showpackets must be 1, too)\n"
			" * net_showpacketsdata"
			"   also print the received and sent data packets to console\n"
			"------------------------------\n"
			);
}

/**
 * @brief Just throw a fatal error to test error shutdown procedures
 */
static void Com_DebugError_f (void)
{
	if (Cmd_Argc() == 3) {
		const char *errorType = Cmd_Argv(1);
		if (!Q_strcmp(errorType, "ERR_DROP"))
			Com_Error(ERR_DROP, "%s", Cmd_Argv(2));
		else if (!Q_strcmp(errorType, "ERR_FATAL"))
			Com_Error(ERR_FATAL, "%s", Cmd_Argv(2));
		else if (!Q_strcmp(errorType, "ERR_DISCONNECT"))
			Com_Error(ERR_DISCONNECT, "%s", Cmd_Argv(2));
	}
	Com_Printf("usage: %s <ERR_FATAL|ERR_DROP|ERR_DISCONNECT> <msg>\n", Cmd_Argv(0));
}
#endif


typedef struct debugLevel_s {
	const char *str;
	int debugLevel;
} debugLevel_t;

static const debugLevel_t debugLevels[] = {
	{"DEBUG_ALL", DEBUG_ALL},
	{"DEBUG_ENGINE", DEBUG_ENGINE},
	{"DEBUG_SHARED", DEBUG_SHARED},
	{"DEBUG_SYSTEM", DEBUG_SYSTEM},
	{"DEBUG_COMMANDS", DEBUG_COMMANDS},
	{"DEBUG_CLIENT", DEBUG_CLIENT},
	{"DEBUG_SERVER", DEBUG_SERVER},
	{"DEBUG_GAME", DEBUG_GAME},
	{"DEBUG_RENDERER", DEBUG_RENDERER},
	{"DEBUG_SOUND", DEBUG_SOUND},

	{NULL, 0}
};

static void Com_DeveloperSet_f (void)
{
	int oldValue = Cvar_VariableInteger("developer");
	int newValue = oldValue;
	int i = 0;

	if (Cmd_Argc() == 2) {
		const char *debugLevel = Cmd_Argv(1);
		while (debugLevels[i].str) {
			if (!Q_strcmp(debugLevel, debugLevels[i].str)) {
				if (oldValue & debugLevels[i].debugLevel)
					newValue &= ~debugLevels[i].debugLevel;
				else
					newValue |= debugLevels[i].debugLevel;
				break;
			}
			i++;
		}
		if (!debugLevels[i].str) {
			Com_Printf("No valid debug mode paramter\n");
			return;
		}
		Cvar_SetValue("developer", newValue);
		Com_Printf("Currently selected debug print levels\n");
		i = 0;
		while (debugLevels[i].str) {
			if (newValue & debugLevels[i].debugLevel)
				Com_Printf("* %s\n", debugLevels[i].str);
			i++;
		}
	} else {
		Com_Printf("usage: %s <debug_level>\n", Cmd_Argv(0));
		Com_Printf("  valid debug_levels are:\n");
		while (debugLevels[i].str) {
			Com_Printf("  * %s\n", debugLevels[i].str);
			i++;
		}
	}
}

#ifndef DEDICATED_ONLY
/**
 * @brief Watches that the cvar cl_maxfps is never getting lower than 10
 */
static qboolean Com_CvarCheckMaxFPS (cvar_t *cvar)
{
	/* don't allow setting maxfps too low (or game could stop responding) */
	return Cvar_AssertValue(cvar, 10, 1000, qtrue);
}
#endif

static void Cbuf_Execute_timer (int now, void *data)
{
	Cbuf_Execute();
}

/**
 * @brief Init function
 * @param[in] argc int
 * @param[in] argv char**
 * @sa Com_ParseScripts
 * @sa Qcommon_Shutdown
 * @sa Sys_Init
 * @sa CL_Init
 */
void Qcommon_Init (int argc, const char **argv)
{
	char *s;

	/* random seed */
	srand(time(NULL));

	com_aliasSysPool = Mem_CreatePool("Common: Alias system");
	com_cmdSysPool = Mem_CreatePool("Common: Command system");
	com_cmodelSysPool = Mem_CreatePool("Common: Collision model");
	com_cvarSysPool = Mem_CreatePool("Common: Cvar system");
	com_fileSysPool = Mem_CreatePool("Common: File system");
	com_genericPool = Mem_CreatePool("Generic");

	if (setjmp(abortframe))
		Sys_Error("Error during initialization");

	memset(&csi, 0, sizeof(csi));

	/* prepare enough of the subsystems to handle
	 * cvar and command buffer management */
	COM_InitArgv(argc, argv);

	Swap_Init();
	Cbuf_Init();

	Cmd_Init();
	Cvar_Init();

	Key_Init();

	/* we need to add the early commands twice, because
	 * a basedir needs to be set before execing
	 * config files, but we want other parms to override
	 * the settings of the config files */
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
	Cmd_AddCommand("gametypelist", Com_GameTypeList_f, "List all available multiplayer game types");
#ifdef DEBUG
	Cmd_AddCommand("debug_help", Com_DebugHelp_f, "Show some debugging help");
	Cmd_AddCommand("debug_error", Com_DebugError_f, "Just throw a fatal error to test error shutdown procedures");
#endif
	Cmd_AddCommand("setdeveloper", Com_DeveloperSet_f, "Set the developer cvar to only get the debug output you want");

	s_sleep = Cvar_Get("s_sleep", "1", CVAR_ARCHIVE, "Use the sleep function to reduce cpu usage");
	developer = Cvar_Get("developer", "0", 0, "Activate developer output to logfile and gameconsole");
	logfile_active = Cvar_Get("logfile", "1", 0, "0 = deacticate logfile, 1 = write normal logfile, 2 = flush on every new line");
	gametype = Cvar_Get("gametype", "1on1", CVAR_ARCHIVE | CVAR_SERVERINFO, "Sets the multiplayer gametype - see gametypelist command for a list of all gametypes");
	http_proxy = Cvar_Get("http_proxy", "", CVAR_ARCHIVE, "Use this proxy for http transfers");
	http_timeout = Cvar_Get("http_timeout", "3", CVAR_ARCHIVE, "Http connection timeout");
	port = Cvar_Get("port", va("%i", PORT_SERVER), CVAR_NOSET, NULL);
	masterserver_url = Cvar_Get("masterserver_url", MASTER_SERVER, CVAR_ARCHIVE, "URL of UFO:AI masterserver");
#ifdef DEDICATED_ONLY
	sv_dedicated = Cvar_Get("sv_dedicated", "1", CVAR_SERVERINFO | CVAR_NOSET, "Is this a dedicated server?");
#else
	sv_dedicated = Cvar_Get("sv_dedicated", "0", CVAR_SERVERINFO | CVAR_NOSET, "Is this a dedicated server?");

	/* set this to false for client - otherwise Qcommon_Frame would set the initial values to multiplayer */
	gametype->modified = qfalse;

	s_language = Cvar_Get("s_language", "", CVAR_ARCHIVE, "Game language");
	s_language->modified = qfalse;
	cl_maxfps = Cvar_Get("cl_maxfps", "90", 0, NULL);
	Cvar_SetCheckFunction("cl_maxfps", Com_CvarCheckMaxFPS);
#endif
	teamnum = Cvar_Get("teamnum", "1", CVAR_ARCHIVE, "Multiplayer team num");

	s = va("UFO: Alien Invasion %s %s %s %s", UFO_VERSION, CPUSTRING, __DATE__, BUILDSTRING);
	Cvar_Get("version", s, CVAR_NOSET, "Full version string");
	Cvar_Get("ver", UFO_VERSION, CVAR_SERVERINFO | CVAR_NOSET, "Version number");

	if (sv_dedicated->integer)
		Cmd_AddCommand("quit", Com_Quit, "Quits the game");

	Mem_Init();
	Sys_Init();

	NET_Init();

	curl_global_init(CURL_GLOBAL_NOTHING);
	Com_Printf("%s initialized.\n", curl_version());

	SV_Init();

	/* e.g. init the client hunk that is used in script parsing */
	CL_Init();

	Com_ParseScripts();

	if (!sv_dedicated->integer)
		Cbuf_AddText("init\n");
	else
		Cbuf_AddText("dedicated_start\n");
	Cbuf_Execute();

	FS_ExecAutoexec();

	/* add + commands from command line
	 * if the user didn't give any commands, run default action */
	if (Cbuf_AddLateCommands()) {
		/* the user asked for something explicit
		 * so drop the loading plaque */
		SCR_EndLoadingPlaque();
	}

#ifndef DEDICATED_ONLY
	CL_InitAfter();
#else
	Com_AddObjectLinks();	/* Add tech links + ammo<->weapon links to items.*/
#endif

	/* Check memory integrity */
	Mem_CheckGlobalIntegrity();

	/* Touch memory */
	Mem_TouchGlobal();

#ifndef DEDICATED_ONLY
	if (!sv_dedicated->integer) {
		Schedule_Timer(cl_maxfps, &CL_Frame, NULL);
		Schedule_Timer(Cvar_Get("cl_slowfreq", "10", 0, NULL), &CL_SlowFrame, NULL);
	}

	/* now hide the console */
	Sys_ShowConsole(qfalse);
#endif

	Schedule_Timer(Cvar_Get("sv_freq", "10", CVAR_NOSET, NULL), &SV_Frame, NULL);

	/* XXX: These next two lines want to be removed */

#ifndef DEDICATED_ONLY
	/* Temporary hack: IRC logic shouldn't poll a timer like this */
	Schedule_Timer(Cvar_Get("irc_freq", "10", 0, NULL), &Irc_Logic_Frame, NULL);
#endif

	Schedule_Timer(Cvar_Get("cbuf_freq", "10", 0, NULL), &Cbuf_Execute_timer, NULL);

	Com_Printf("====== UFO Initialized ======\n\n");
}

static void tick_timer (int now, void *data)
{
	struct timer *timer = data;
	int old_interval = timer->interval;

	/* Compute and store the lateness, updating the total */
	int lateness = Sys_Milliseconds() - now;
	timer->total_lateness -= timer->recent_lateness[timer->next_lateness];
	timer->recent_lateness[timer->next_lateness] = lateness;
	timer->total_lateness += lateness;
	timer->next_lateness++;
	timer->next_lateness %= TIMER_LATENESS_HISTORY;

	/* Is it time to check the mean yet? */
	timer->next_check--;
	if (timer->next_check <= 0) {
		int mean = timer->total_lateness / TIMER_LATENESS_HISTORY;

		/* We use a saturating counter to damp the adjustment */

		/* When we stay above the high water mark, increase the interval */
		if (mean > TIMER_LATENESS_HIGH)
			timer->checks_high = min(TIMER_CHECK_LAG, timer->checks_high + 1);
		else
			timer->checks_high = max(0, timer->checks_high - 1);

		if (timer->checks_high > TIMER_CHECK_LAG)
			timer->interval += 2;

		/* When we stay below the low water mark, decrease the interval */
		if (mean < TIMER_LATENESS_LOW)
			timer->checks_low = min(TIMER_CHECK_LAG, timer->checks_high + 1);
		else
			timer->checks_low = max(0, timer->checks_low - 1);

		if (timer->checks_low > TIMER_CHECK_LAG)
			timer->interval -= 1;

		/* Note that we slow the timer more quickly than we speed it up,
		 * so it should tend to settle down in the vicinity of the low
		 * water mark */

		timer->next_check = TIMER_CHECK_INTERVAL;
	}

	timer->interval = max(timer->interval, 1000 / timer->min_freq->integer);

	if (timer->interval != old_interval)
		Com_DPrintf(DEBUG_ENGINE, "Adjusted timer on %s to interval %d\n", timer->min_freq->name, timer->interval);

	if (setjmp(abortframe) == 0)
		timer->func(now, timer->data);

	/* We correct for the lateness of this frame. We do not correct for
	 * the time consumed by this frame - that's billed to the lateness
	 * of future frames (so that the automagic slowdown can work) */
	Schedule_Event(now + lateness + timer->interval, &tick_timer, timer);
}

void Schedule_Timer (cvar_t *freq, event_func *func, void *data)
{
	struct timer *timer = malloc(sizeof(*timer));
	int i;
	timer->min_freq = freq;
	timer->interval = 1000 / freq->integer;
	timer->next_lateness = 0;
	timer->total_lateness = 0;
	timer->next_check = TIMER_CHECK_INTERVAL;
	timer->checks_high = 0;
	timer->checks_low = 0;
	timer->func = func;
	timer->data = data;
	for (i = 0; i < TIMER_LATENESS_HISTORY; i++)
		timer->recent_lateness[i] = 0;

	Schedule_Event(Sys_Milliseconds() + timer->interval, &tick_timer, timer);
}

void Schedule_Event (int when, event_func *func, void *data)
{
	struct event *event = malloc(sizeof(*event));
	event->when = when;
	event->func = func;
	event->data = data;

	if (!event_queue || event_queue->when > when) {
		event->next = event_queue;
		event_queue = event;
	} else {
		struct event *e = event_queue;
		while (e->next && e->next->when <= when)
			e = e->next;
		event->next = e->next;
		e->next = event;
	}

#ifdef DEBUG
	for (event = event_queue; event && event->next; event = event->next)
		if (event->when > event->next->when)
			abort();
#endif
}

/**
 * @brief This is the function that is called directly from main()
 * @sa main
 * @sa Qcommon_Init
 * @sa Qcommon_Shutdown
 * @sa SV_Frame
 * @sa CL_Frame
 */
void Qcommon_Frame (void)
{
	int time_to_next;

	/* an ERR_DROP was thrown */
	if (setjmp(abortframe))
		return;

	/* If the next event is due... */
	if (event_queue && Sys_Milliseconds() >= event_queue->when) {
		struct event *event = event_queue;

		/* Remove the event from the queue */
		event_queue = event->next;

		if (setjmp(abortframe)) {
			free(event);
			return;
		}

		/* Dispatch the event */
		event->func(event->when, event->data);

		if (setjmp(abortframe))
			return;

		free(event);
	}

	/* Now we spend time_to_next milliseconds working on whatever
	 * IO is ready (but always try at least once, to make sure IO
	 * doesn't stall) */
	do {
		char *s;

		/* XXX: This shouldn't exist */
		do {
			s = Sys_ConsoleInput();
			if (s)
				Cbuf_AddText(va("%s\n", s));
		} while (s);

		time_to_next = event_queue ? (event_queue->when - Sys_Milliseconds()) : 1000;
		if (time_to_next < 0)
			time_to_next = 0;

		NET_Wait(time_to_next);
	} while (time_to_next > 0);
}

/**
 * @brief
 * @sa Qcommon_Init
 * @sa Sys_Quit
 * @note Don't call anything that depends on cvars, command system, or any other
 * subsystem that is allocated in the mem pools and maybe already freed
 */
void Qcommon_Shutdown (void)
{
	HTTP_Cleanup();

	Mem_Shutdown();
}

/*
============================================================
LINKED LIST
============================================================
*/

/**
 * @brief Adds an entry to a new or to an already existing linked list
 * @sa LIST_AddString
 * @sa LIST_Remove
 * @return Returns a pointer to the data that has been added, wrapped in a linkedList_t
 */
linkedList_t* LIST_Add (linkedList_t** listDest, const byte* data, size_t length)
{
	linkedList_t *newEntry;
	linkedList_t *list;

	assert(listDest);
	assert(data);

	/* create the list */
	if (!*listDest) {
		*listDest = (linkedList_t*)Mem_PoolAlloc(sizeof(linkedList_t), com_genericPool, 0);
		(*listDest)->data = (byte*)Mem_PoolAlloc(length, com_genericPool, 0);
		memcpy(((*listDest)->data), data, length);
		(*listDest)->next = NULL; /* not really needed - but for better readability */
		return *listDest;
	} else
		list = *listDest;

	while (list->next)
		list = list->next;

	newEntry = (linkedList_t*)Mem_PoolAlloc(sizeof(linkedList_t), com_genericPool, 0);
	list->next = newEntry;
	newEntry->data = (byte*)Mem_PoolAlloc(length, com_genericPool, 0);
	memcpy(newEntry->data, data, length);
	newEntry->next = NULL; /* not really needed - but for better readability */

	return newEntry;
}

/**
 * @brief Searches for the first occurrence of a given string
 * @return true if the string is found, otherwise false
 * @note if string is NULL, the function returns false
 * @sa LIST_AddString
 */
qboolean LIST_ContainsString (const linkedList_t* list, const char* string)
{
	assert(list);

	while ((string != NULL) && (list != NULL)) {
		if (!Q_strcmp((char*)list->data, string))
			return qtrue;
			/* Com_Printf("%.0f: %.0f\n", (float)list->data[0], (float)list->data[1]); */
		list = list->next;
	}

	return qfalse;
}

/**
 * @brief Adds an string to a new or to an already existing linked list
 * @sa LIST_Add
 * @sa LIST_Remove
 */
void LIST_AddString (linkedList_t** listDest, const char* data)
{
	linkedList_t *newEntry;
	linkedList_t *list;

	assert(listDest);
	assert(data);

	/* create the list */
	if (!*listDest) {
		*listDest = (linkedList_t*)Mem_PoolAlloc(sizeof(linkedList_t), com_genericPool, 0);
		(*listDest)->data = (byte*)Mem_PoolStrDup(data, com_genericPool, 0);
		(*listDest)->next = NULL; /* not really needed - but for better readability */
		return;
	} else
		list = *listDest;

	while (list->next)
		list = list->next;

	newEntry = (linkedList_t*)Mem_PoolAlloc(sizeof(linkedList_t), com_genericPool, 0);
	list->next = newEntry;
	newEntry->data = (byte*)Mem_PoolStrDup(data, com_genericPool, 0);
	newEntry->next = NULL; /* not really needed - but for better readability */
}

/**
 * @brief Adds just a pointer to a new or to an already existing linked list
 * @sa LIST_Add
 * @sa LIST_Remove
 */
void LIST_AddPointer (linkedList_t** listDest, void* data)
{
	linkedList_t *newEntry;
	linkedList_t *list;

	assert(listDest);
	assert(data);

	/* create the list */
	if (!*listDest) {
		*listDest = (linkedList_t*)Mem_PoolAlloc(sizeof(linkedList_t), com_genericPool, 0);
		(*listDest)->data = data;
		(*listDest)->ptr = qtrue;
		(*listDest)->next = NULL; /* not really needed - but for better readability */
		return;
	} else
		list = *listDest;

	while (list->next)
		list = list->next;

	newEntry = (linkedList_t*)Mem_PoolAlloc(sizeof(linkedList_t), com_genericPool, 0);
	list->next = newEntry;
	newEntry->data = data;
	newEntry->ptr = qtrue;
	newEntry->next = NULL; /* not really needed - but for better readability */
}

/**
 * @brief Removes one entry from the linked list
 * @sa LIST_AddString
 * @sa LIST_Add
 * @sa LIST_AddPointer
 * @sa LIST_Delete
 */
void LIST_Remove (linkedList_t **list, linkedList_t *entry)
{
	linkedList_t* prev, *listPos;

	assert(list);
	assert(entry);

	prev = *list;
	listPos = *list;

	/* first entry */
	if (*list == entry) {
		if (!(*list)->ptr)
			Mem_Free((*list)->data);
		listPos = (*list)->next;
		Mem_Free(*list);
		*list = listPos;
	} else {
		while (listPos) {
			if (listPos == entry) {
				prev->next = listPos->next;
				if (!listPos->ptr)
					Mem_Free(listPos->data);
				Mem_Free(listPos);
				break;
			}
			prev = listPos;
			listPos = listPos->next;
		}
	}
}

/**
 * @sa LIST_Add
 * @sa LIST_Remove
 */
void LIST_Delete (linkedList_t *list)
{
	linkedList_t *l = list;

	while (l) {
		list = list->next;
		if (!l->ptr)
			Mem_Free(l->data);
		Mem_Free(l);
		l = list;
	}
}
