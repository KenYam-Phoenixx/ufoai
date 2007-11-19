/**
 * @file cl_main.c
 * @brief Primary functions for the client. NB: The main() is system-secific and can currently be found in ports/.
 */

/*
All original materal Copyright (C) 2002-2007 UFO: Alien Invasion team.

Original file from Quake 2 v3.21: quake2-2.31/client/cl_main.c
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
#include "cl_global.h"
#include "../shared/infostring.h"

cvar_t *cl_isometric;

cvar_t *rcon_client_password;

cvar_t *cl_fps;
cvar_t *cl_shownet;
cvar_t *cl_show_tooltips;
cvar_t *cl_show_cursor_tooltips;
cvar_t *cl_particleWeather;
cvar_t *cl_logevents;
cvar_t *cl_centerview;
cvar_t *cl_worldlevel;
cvar_t *cl_selected;
cvar_t *cl_3dmap;
cvar_t *cl_numnames;

cvar_t *mn_serverlist;
cvar_t *mn_main;
cvar_t *mn_sequence;
cvar_t *mn_active;
cvar_t *mn_afterdrop;
cvar_t *mn_main_afterdrop;
cvar_t *mn_hud;
cvar_t *mn_lastsave;

cvar_t *difficulty;
cvar_t *cl_start_employees;
cvar_t *cl_initial_equipment;
cvar_t *cl_start_buildings;

cvar_t *cl_connecttimeout; /* multiplayer connection timeout value (ms) */

/** @brief Confirm actions in tactical mode - valid values are 0, 1 and 2 */
cvar_t *confirm_actions;

static cvar_t *cl_precache;

/* userinfo */
static cvar_t *info_password;
cvar_t *name;
cvar_t *team;
cvar_t *equip;
cvar_t *teamnum;
cvar_t *campaign;
cvar_t *msg;

client_static_t cls;
client_state_t cl;

static qboolean soldiersSpawned = qfalse;

typedef struct teamData_s {
	int teamCount[MAX_TEAMS];	/**< team counter - parsed from server data 'teaminfo' */
	qboolean teamplay;
	int maxteams;
	int maxplayersperteam;		/**< max players per team */
	char teamInfoText[MAX_MESSAGE_TEXT];
	qboolean parsed;
} teamData_t;

static teamData_t teamData;

static int precache_check;

char messageBuffer[MAX_MESSAGE_TEXT];

static void CL_SpawnSoldiers_f(void);

#define MAX_BOOKMARKS 16

struct memPool_s *cl_localPool;		/**< reset on every game restart */
struct memPool_s *cl_genericPool;	/**< permanent client data - menu, fonts */
struct memPool_s *cl_ircSysPool;	/**< irc pool */
struct memPool_s *cl_soundSysPool;
struct memPool_s *cl_menuSysPool;
struct memPool_s *vid_genericPool;	/**< also holds all the static models */
struct memPool_s *vid_imagePool;
struct memPool_s *vid_lightPool;	/**< lightmap - wiped with every new map */
struct memPool_s *vid_modelPool;	/**< modeldata - wiped with every new map */
/*====================================================================== */

/**
 * @brief adds the current command line as a clc_stringcmd to the client message.
 * things like action, turn, etc, are commands directed to the server,
 * so when they are typed in at the console, they will need to be forwarded.
 */
void Cmd_ForwardToServer (void)
{
	const char *cmd = Cmd_Argv(0);
	struct dbuffer *msg;

	if (cls.state <= ca_connected || *cmd == '-' || *cmd == '+') {
		Com_Printf("Unknown command \"%s\"\n", cmd);
		return;
	}

	msg = new_dbuffer();
	NET_WriteByte(msg, clc_stringcmd);
	dbuffer_add(msg, cmd, strlen(cmd));
	if (Cmd_Argc() > 1) {
		dbuffer_add(msg, " ", 1);
		dbuffer_add(msg, Cmd_Args(), strlen(Cmd_Args()));
	}
	dbuffer_add(msg, "", 1);
	NET_WriteMsg(cls.stream, msg);
}

/**
 * @brief Set or print some environment variables via console command
 * @sa Q_putenv
 */
static void CL_Env_f (void)
{
	int argc = Cmd_Argc();

	if (argc == 3) {
		Q_putenv(Cmd_Argv(1), Cmd_Argv(2));
	} else if (argc == 2) {
		char *env = getenv(Cmd_Argv(1));

		if (env)
			Com_Printf("%s=%s\n", Cmd_Argv(1), env);
		else
			Com_Printf("%s undefined\n", Cmd_Argv(1));
	}
}


static void CL_ForwardToServer_f (void)
{
	if (cls.state != ca_connected && cls.state != ca_active) {
		Com_Printf("Can't \"%s\", not connected\n", Cmd_Argv(0));
		return;
	}

	/* don't forward the first argument */
	if (Cmd_Argc() > 1) {
		struct dbuffer *msg;
		msg = new_dbuffer();
		NET_WriteByte(msg, clc_stringcmd);
		dbuffer_add(msg, Cmd_Args(), strlen(Cmd_Args()) + 1);
		NET_WriteMsg(cls.stream, msg);
	}
}

static void CL_Quit_f (void)
{
	CL_Disconnect();
	Com_Quit();
}

/**
 * @brief Disconnects a multiplayer game if singleplayer is true and set ccs.singleplayer to true
 */
void CL_StartSingleplayer (qboolean singleplayer)
{
	const char *type, *name, *text;
	base_t *base;

	if (singleplayer) {
		ccs.singleplayer = qtrue;
		if (Com_ServerState()) {
			/* shutdown server */
			SV_Shutdown("Server was killed.\n", qfalse);
		} else if (cls.state >= ca_connecting) {
			Com_Printf("Disconnect from current server\n");
			CL_Disconnect();
		}
		Cvar_ForceSet("sv_maxclients", "1");
		Com_Printf("Changing to Singleplayer\n");

		/* reset sv_maxsoldiersperplayer and sv_maxsoldiersperteam to default values */
		/* FIXME: these should probably default to something bigger */
		if (Cvar_VariableInteger("sv_maxsoldiersperteam") != 4)
			Cvar_SetValue("sv_maxsoldiersperteam", 4);
		if (Cvar_VariableInteger("sv_maxsoldiersperplayer") != 8)
			Cvar_SetValue("sv_maxsoldiersperplayer", 8);

		/* this is needed to let 'soldier_select 0' do
		 * the right thing while we are on geoscape */
		sv_maxclients->modified = qtrue;
	} else {
		const char *max_s, *max_spp;
		max_s = Cvar_VariableStringOld("sv_maxsoldiersperteam");
		max_spp = Cvar_VariableStringOld("sv_maxsoldiersperplayer");

		/* restore old sv_maxsoldiersperplayer and sv_maxsoldiersperteam
		 * cvars if values were previously set */
		if (strlen(max_s))
			Cvar_Set("sv_maxsoldiersperteam", max_s);
		if (strlen(max_spp))
			Cvar_Set("sv_maxsoldiersperplayer", max_spp);

		ccs.singleplayer = qfalse;
		curCampaign = NULL;
		selMis = NULL;
		base = &gd.bases[0];
		B_ClearBase(base);
		RS_ResetHash();
		gd.numBases = 1;
		gd.numAircraft = 0;

		/* now add a dropship where we can place our soldiers in */
		AIR_NewAircraft(base, "craft_drop_firebird");

		Com_Printf("Changing to Multiplayer\n");
		/* disconnect already running session - when entering the
		 * multiplayer menu while you are still connected */
		if (cls.state >= ca_connecting)
			CL_Disconnect();

		/* pre-stage parsing */
		FS_BuildFileList("ufos/*.ufo");
		FS_NextScriptHeader(NULL, NULL, NULL);
		text = NULL;

		if (!gd.numTechnologies) {
			while ((type = FS_NextScriptHeader("ufos/*.ufo", &name, &text)) != 0)
				if (!Q_strncmp(type, "tech", 4))
					RS_ParseTechnologies(name, &text);

			/* fill in IDXs for required research techs */
			RS_RequiredIdxAssign();
			Com_AddObjectLinks();	/* Add tech links + ammo<->weapon links to items.*/
		}
	}
}

/**
 * @note Called after an ERR_DROP was thrown
 * @sa CL_Disconnect
 */
void CL_Drop (void)
{
	/* drop loading plaque */
	SCR_EndLoadingPlaque();

	MN_PopMenu(qtrue);

	/* make sure that we are in the correct menus in singleplayer after
	 * dropping the game due to a failure */
	if (ccs.singleplayer && curCampaign) {
		Cvar_Set("mn_main", "singleplayerInGame");
		Cvar_Set("mn_active", "map");
		MN_PushMenu("map");
	} else {
		Cvar_Set("mn_main", "main");
		Cvar_Set("mn_active", "");
		MN_PushMenu("main");
		/* the main menu may have a init node - execute it */
		Cbuf_Execute();
	}

	if (*mn_afterdrop->string) {
		MN_PushMenu(mn_afterdrop->string);
		Cvar_Set("mn_afterdrop", "");
	}
	if (*mn_main_afterdrop->string) {
		Cvar_Set("mn_main", mn_main_afterdrop->string);
		Cvar_Set("mn_main_afterdrop", "");
	}

	if (cls.state == ca_uninitialized || cls.state == ca_disconnected)
		return;

	CL_Disconnect();
}

/**
 * @note Only call CL_Connect if there is no connection yet (cls.stream is NULL)
 * @sa CL_Disconnect
 */
static void CL_Connect (void)
{
	userinfo_modified = qfalse;

	close_datagram_socket(cls.datagram_socket);
	cls.datagram_socket = NULL;

	assert(!cls.stream);

	if (cls.servername[0]) {
		cls.stream = NET_Connect(cls.servername, port->string);
	} else
		cls.stream = NET_ConnectToLoopBack();
	if (cls.stream) {
		NET_OOB_Printf(cls.stream, "connect %i \"%s\"\n", PROTOCOL_VERSION, Cvar_Userinfo());
		cls.connectTime = cls.realtime;
	} else
		Com_Printf("Could not connect\n");
}

static void CL_Connect_f (void)
{
	const char *server;
	aircraft_t *aircraft;

	if (Cmd_Argc() != 2) {
		Com_Printf("Usage: %s <server>\n", Cmd_Argv(0));
		return;
	}

	if (ccs.singleplayer) {
		Com_Printf("Start multiplayer first\n");
		return;
	}

	aircraft = AIR_AircraftGetFromIdx(0);
	assert(aircraft);
	if (!B_GetNumOnTeam(aircraft)) {
		MN_Popup(_("Error"), _("Assemble a team first"));
		return;
	}

	/* if running a local server, kill it and reissue */
	if (Com_ServerState())
		SV_Shutdown("Server quit\n", qfalse);

	CL_Disconnect();

	server = Cmd_Argv(1);
	Q_strncpyz(cls.servername, server, sizeof(cls.servername));

	CL_SetClientState(ca_connecting);

	/* everything should be reasearched for multiplayer matches */
/*	RS_MarkResearchedAll(); */

	Cvar_Set("mn_main", "multiplayerInGame");
}

/**
 * Send the rest of the command line over as
 * an unconnected command.
 */
static void CL_Rcon_f (void)
{
	char message[1024];
	int i;

	if (Cmd_Argc() < 2) {
		Com_Printf("Usage: %s <command>\n", Cmd_Argv(0));
		return;
	}

	if (!rcon_client_password->string) {
		Com_Printf("You must set 'rcon_password' before issuing an rcon command.\n");
		return;
	}

	if (cls.state < ca_connected)
		return;

	Com_sprintf(message, sizeof(message), "rcon %s ", rcon_client_password->string);

	for (i = 1; i < Cmd_Argc(); i++) {
		Q_strcat(message, Cmd_Argv(i), sizeof(message));
		Q_strcat(message, " ", sizeof(message));
	}

	NET_OOB_Printf(cls.stream, message);
}

/**
 * @sa CL_ParseServerData
 * @sa CL_Disconnect
 */
void CL_ClearState (void)
{
	CL_ClearEffects();

	/* wipe the entire cl structure */
	memset(&cl, 0, sizeof(cl));
	cl.cam.zoom = 1.0;
	V_CalcFovX();

	numLEs = 0;
	numLMs = 0;
	numMPs = 0;
	numPtls = 0;
}

/**
 * @brief Sets the cls.state to ca_disconnected and informs the server
 * @sa CL_Disconnect_f
 * @sa CL_Drop
 * @note Goes from a connected state to full screen console state
 * Sends a disconnect message to the server
 * This is also called on Com_Error, so it shouldn't cause any errors
 */
void CL_Disconnect (void)
{
	struct dbuffer *msg;

	/* If playing a cinematic, stop it */
	CIN_StopCinematic();

	if (cls.state == ca_disconnected)
		return;

	/* send a disconnect message to the server */
	if (!Com_ServerState()) {
		msg = new_dbuffer();
		NET_WriteByte(msg, clc_stringcmd);
		NET_WriteString(msg, "disconnect");
		NET_WriteMsg(cls.stream, msg);
		/* make sure, that this is send */
		NET_Wait(0);
	}

	stream_finished(cls.stream);
	cls.stream = NULL;

	CL_ClearState();

	S_StopAllSounds();

	CL_SetClientState(ca_disconnected);
}

/**
 * @brief Binding for disconnect console command
 * @sa CL_Disconnect
 * @sa CL_Drop
 * @sa SV_ShutdownWhenEmpty
 */
static void CL_Disconnect_f (void)
{
	SV_ShutdownWhenEmpty();
	CL_Drop();
}

/* it's dangerous to activate this */
/*#define ACTIVATE_PACKET_COMMAND*/
#ifdef ACTIVATE_PACKET_COMMAND
/**
 * @brief This function allows you to send network commands from commandline
 * @note This function is only for debugging and testing purposes
 * It is dangerous to leave this activated in final releases
 * packet [destination] [contents]
 * Contents allows \n escape character
 */
static void CL_Packet_f (void)
{
	int i, l;
	const char *in;
	char *out;
	struct net_stream *s;

	if (Cmd_Argc() != 4) {
		Com_Printf("Usage: %s <destination> <port> <contents>\n", Cmd_Argv(0));
		return;
	}

	s = NET_Connect(Cmd_Argv(1), Cmd_Argv(2));
	if (!s) {
		Com_Printf("Could not connect to %s at port %s\n", Cmd_Argv(1), Cmd_Argv(2));
		return;
	}

	in = Cmd_Argv(3);

	l = strlen(in);
	for (i = 0; i < l; i++) {
		if (in[i] == '\\' && in[i + 1] == 'n') {
			*out++ = '\n';
			i++;
		} else
			*out++ = in[i];
	}
	*out = 0;

	NET_OOB_Printf(s, va("%s %i", out, PROTOCOL_VERSION));
}
#endif

/**
 * @brief The server is changing levels
 */
static void CL_Reconnect_f (void)
{
	if (Com_ServerState())
		return;

	if (ccs.singleplayer) {
		Com_Printf("Start multiplayer first\n");
		return;
	}

	if (*cls.servername) {
		if (cls.state >= ca_connecting) {
			Com_Printf("disconnecting...\n");
			CL_Disconnect();
		}

		cls.connectTime = cls.realtime - 1500;

		CL_SetClientState(ca_connecting);
		Com_Printf("reconnecting...\n");
	} else
		Com_Printf("No server to reconnect to\n");
}

typedef struct serverList_s {
	char *node;
	char *service;
	qboolean pinged;
	char sv_hostname[16];
	char mapname[16];
	char version[8];
	char gametype[8];
	qboolean sv_dedicated;
	int sv_maxclients;
	int clients;
	int serverListPos;
} serverList_t;

#define MAX_SERVERLIST 128
static char serverText[1024];
static int serverListLength = 0;
static int serverListPos = 0;
static serverList_t serverList[MAX_SERVERLIST];

/**
 * @sa CL_PingServerCallback
 */
static void CL_ProcessPingReply (serverList_t *server, const char *msg)
{
	if (PROTOCOL_VERSION != atoi(Info_ValueForKey(msg, "protocol"))) {
		Com_DPrintf(DEBUG_CLIENT, "CL_ProcessPingReply: Protocol mismatch\n");
		return;
	}
	if (Q_strcmp(UFO_VERSION, Info_ValueForKey(msg, "version"))) {
		Com_DPrintf(DEBUG_CLIENT, "CL_ProcessPingReply: Version mismatch\n");
	}

	if (server->pinged)
		return;

	server->pinged = qtrue;
	Q_strncpyz(server->sv_hostname, Info_ValueForKey(msg, "sv_hostname"),
		sizeof(server->sv_hostname));
	Q_strncpyz(server->version, Info_ValueForKey(msg, "version"),
		sizeof(server->version));
	Q_strncpyz(server->mapname, Info_ValueForKey(msg, "mapname"),
		sizeof(server->mapname));
	Q_strncpyz(server->gametype, Info_ValueForKey(msg, "gametype"),
		sizeof(server->gametype));
	server->clients = atoi(Info_ValueForKey(msg, "clients"));
	server->sv_dedicated = atoi(Info_ValueForKey(msg, "sv_dedicated"));
	server->sv_maxclients = atoi(Info_ValueForKey(msg, "sv_maxclients"));
}

/**
 * @brief CL_PingServer
 */
static void CL_PingServerCallback (struct net_stream *s)
{
	struct dbuffer *buf = NET_ReadMsg(s);
	serverList_t *server = stream_data(s);
	int cmd = NET_ReadByte(buf);
	char *str = NET_ReadStringLine(buf);
	char string[MAX_INFO_STRING];

	if (cmd == clc_oob && Q_strncmp(str, "info", 4) == 0) {
		str = NET_ReadString(buf);
		if (!str)
			return;
		CL_ProcessPingReply(server, str);
	} else
		return;

	if (mn_serverlist->integer == 0
	|| (mn_serverlist->integer == 1 && server->clients < server->sv_maxclients)
	|| (mn_serverlist->integer == 2 && server->clients)) {
		Com_sprintf(string, sizeof(string), "%s\t\t\t%s\t\t\t%s\t\t%i/%i\n",
			server->sv_hostname,
			server->mapname,
			server->gametype,
			server->clients,
			server->sv_maxclients);
		server->serverListPos = serverListPos;
		serverListPos++;
		Q_strcat(serverText, string, sizeof(serverText));
	}
	free_stream(s);
}

/**
 * @brief Pings all servers in serverList
 * @sa CL_AddServerToList
 * @sa CL_ParseServerInfoMessage
 */
static void CL_PingServer (serverList_t *server)
{
	struct net_stream *s = NET_Connect(server->node, server->service);

	if (s) {
		Com_DPrintf(DEBUG_CLIENT, "pinging [%s]:%s...\n", server->node, server->service);
		NET_OOB_Printf(s, "info %i", PROTOCOL_VERSION);
		set_stream_data(s, server);
		stream_callback(s, &CL_PingServerCallback);
	} else {
		Com_Printf("pinging failed [%s]:%s...\n", server->node, server->service);
	}
}

/**
 * @brief Prints all the servers on the list to game console
 */
static void CL_PrintServerList_f (void)
{
	int i;

	Com_Printf("%i servers on the list\n", serverListLength);

	for (i = 0; i < serverListLength; i++) {
		Com_Printf("%02i: [%s]:%s (pinged: %i)\n", i, serverList[i].node, serverList[i].service, serverList[i].pinged);
	}
}

typedef enum {
	SERVERLIST_SHOWALL,
	SERVERLIST_HIDEFULL,
	SERVERLIST_HIDEEMPTY
} serverListStatus_t;

/**
 * @brief Adds a server to the serverlist cache
 * @return false if it is no valid address or server already exists
 * @sa CL_ParseStatusMessage
 */
static void CL_AddServerToList (const char *node, const char *service)
{
	int i;

	if (serverListLength >= MAX_SERVERLIST)
		return;

	for (i = 0; i < serverListLength; i++)
		if (strcmp(serverList[i].node, node) == 0 && strcmp(serverList[i].service, service) == 0)
			return;

	memset(&(serverList[serverListLength]), 0, sizeof(serverList_t));
	serverList[serverListLength].node = strdup(node);
	serverList[serverListLength].service = strdup(service);
	CL_PingServer(&serverList[serverListLength]);
	serverListLength++;
}

/**
 * @brief Multiplayer wait menu init function
 */
static void CL_WaitInit_f (void)
{
	static qboolean reconnect = qfalse;
	char buf[32];

	/* the server knows this already */
	if (!Com_ServerState()) {
		Cvar_SetValue("sv_maxteams", atoi(cl.configstrings[CS_MAXTEAMS]));
		Cvar_Set("mp_wait_init_show_force", "0");
	} else {
		Cvar_Set("mp_wait_init_show_force", "1");
	}
	Com_sprintf(buf, sizeof(buf), "%s/%s", cl.configstrings[CS_PLAYERCOUNT], cl.configstrings[CS_MAXCLIENTS]);
	Cvar_Set("mp_wait_init_players", buf);
	if (!*cl.configstrings[CS_NAME]) {
		if (!reconnect) {
			reconnect = qtrue;
			CL_Reconnect_f();
			MN_PopMenu(qfalse);
		} else {
			CL_Disconnect_f();
			MN_PopMenu(qfalse);
			MN_Popup(_("Error"), _("Server needs restarting - something went wrong"));
		}
	} else
		reconnect = qfalse;
}

/**
 * @brief Team selection text
 *
 * This function fills the multiplayer_selectteam menu with content
 * @sa NET_OOB_Printf
 * @sa SV_TeamInfoString
 * @sa CL_SelectTeam_Init_f
 */
static void CL_ParseTeamInfoMessage (struct dbuffer *msg)
{
	char *s = NET_ReadString(msg);
	char *var = NULL;
	char *value = NULL;
	int cnt = 0, n;

	if (!s) {
		MN_MenuTextReset(TEXT_LIST);
		Com_DPrintf(DEBUG_CLIENT, "CL_ParseTeamInfoMessage: No teaminfo string\n");
		return;
	}

	memset(&teamData, 0, sizeof(teamData_t));

#if 0
	Com_Printf("CL_ParseTeamInfoMessage: %s\n", s);
#endif

	value = s;
	var = strstr(value, "\n");
	*var++ = '\0';

	teamData.teamplay = atoi(value);

	value = var;
	var = strstr(var, "\n");
	*var++ = '\0';
	teamData.maxteams = atoi(value);

	value = var;
	var = strstr(var, "\n");
	*var++ = '\0';
	teamData.maxplayersperteam = atoi(value);

	s = var;
	if (s)
		Q_strncpyz(teamData.teamInfoText, s, sizeof(teamData.teamInfoText));

	while (s != NULL) {
		value = strstr(s, "\n");
		if (value)
			*value++ = '\0';
		else
			break;
		/* get teamnum */
		var = strstr(s, "\t");
		if (var)
			*var++ = '\0';

		n = atoi(s);
		if (n > 0 && n < MAX_TEAMS)
			teamData.teamCount[n]++;
		s = value;
		cnt++;
	};

	/* no players are connected atm */
	if (!cnt)
		Q_strcat(teamData.teamInfoText, _("No player connected\n"), sizeof(teamData.teamInfoText));

	Cvar_SetValue("mn_maxteams", teamData.maxteams);
	Cvar_SetValue("mn_maxplayersperteam", teamData.maxplayersperteam);

	menuText[TEXT_LIST] = teamData.teamInfoText;
	teamData.parsed = qtrue;

	/* spawn multi-player death match soldiers here */
	if (!ccs.singleplayer && baseCurrent && !teamData.teamplay)
		CL_SpawnSoldiers_f();
}

static char serverInfoText[MAX_MESSAGE_TEXT];
static char userInfoText[MAX_MESSAGE_TEXT];
/**
 * @brief Serverbrowser text
 * @sa CL_PingServer
 * @sa CL_PingServers_f
 * @note This function fills the network browser server information with text
 * @sa NET_OOB_Printf
 * @sa CL_ServerInfoCallback
 */
static void CL_ParseServerInfoMessage (struct net_stream *stream, const char *s)
{
	const char *value = NULL;
	const char *users;
	int team;
	const char *token;
	char buf[256];

	if (!s)
		return;

	/* check for server status response message */
	value = Info_ValueForKey(s, "sv_dedicated");
	if (*value) {
		Com_DPrintf(DEBUG_CLIENT, "%s\n", s); /* status string */
		/* server info cvars and users are seperated via newline */
		users = strstr(s, "\n");
		if (!users) {
			Com_Printf("%c%s\n", 1, s);
			return;
		}

		Cvar_Set("mn_mappic", "maps/shots/na.jpg");
		Cvar_Set("mn_server_need_password", "0"); /* string */

		Com_sprintf(serverInfoText, sizeof(serverInfoText), _("IP\t%s\n\n"), stream_peer_name(stream, buf, sizeof(buf), qtrue));
		value = Info_ValueForKey(s, "mapname");
		assert(value);
		Cvar_Set("mn_svmapname", value);
		Q_strncpyz(buf, value, sizeof(buf));
		if (buf[strlen(buf)-1] == 'd' || buf[strlen(buf)-1] == 'n')
			buf[strlen(buf)-1] = '\0';
		Com_sprintf(serverInfoText + strlen(serverInfoText), sizeof(serverInfoText) - strlen(serverInfoText), _("Map:\t%s\n"), value);
		if (FS_CheckFile(va("pics/maps/shots/%s.jpg", buf)) != -1)
			Cvar_Set("mn_mappic", va("maps/shots/%s.jpg", buf));
		else {
			char filename[MAX_QPATH];
			Q_strncpyz(filename, "pics/maps/shots/", sizeof(filename));
			Q_strcat(filename, buf, sizeof(filename));
			if (FS_CheckFile(filename) != -1)
				Cvar_Set("mn_mappic", filename);
		}
		Com_sprintf(serverInfoText + strlen(serverInfoText), sizeof(serverInfoText) - strlen(serverInfoText), _("Servername:\t%s\n"), Info_ValueForKey(s, "sv_hostname"));
		Com_sprintf(serverInfoText + strlen(serverInfoText), sizeof(serverInfoText) - strlen(serverInfoText), _("Moralestates:\t%s\n"), Info_ValueForKey(s, "sv_enablemorale"));
		Com_sprintf(serverInfoText + strlen(serverInfoText), sizeof(serverInfoText) - strlen(serverInfoText), _("Gametype:\t%s\n"), Info_ValueForKey(s, "gametype"));
		Com_sprintf(serverInfoText + strlen(serverInfoText), sizeof(serverInfoText) - strlen(serverInfoText), _("Gameversion:\t%s\n"), Info_ValueForKey(s, "ver"));
		Com_sprintf(serverInfoText + strlen(serverInfoText), sizeof(serverInfoText) - strlen(serverInfoText), _("Dedicated server:\t%s\n"), Info_ValueForKey(s, "sv_dedicated"));
		Com_sprintf(serverInfoText + strlen(serverInfoText), sizeof(serverInfoText) - strlen(serverInfoText), _("Operating system:\t%s\n"), Info_ValueForKey(s, "sys_os"));
		Com_sprintf(serverInfoText + strlen(serverInfoText), sizeof(serverInfoText) - strlen(serverInfoText), _("Network protocol:\t%s\n"), Info_ValueForKey(s, "protocol"));
		Com_sprintf(serverInfoText + strlen(serverInfoText), sizeof(serverInfoText) - strlen(serverInfoText), _("Roundtime:\t%s\n"), Info_ValueForKey(s, "sv_roundtimelimit"));
		Com_sprintf(serverInfoText + strlen(serverInfoText), sizeof(serverInfoText) - strlen(serverInfoText), _("Teamplay:\t%s\n"), Info_ValueForKey(s, "sv_teamplay"));
		Com_sprintf(serverInfoText + strlen(serverInfoText), sizeof(serverInfoText) - strlen(serverInfoText), _("Max. players per team:\t%s\n"), Info_ValueForKey(s, "sv_maxplayersperteam"));
		Com_sprintf(serverInfoText + strlen(serverInfoText), sizeof(serverInfoText) - strlen(serverInfoText), _("Max. teams allowed in this map:\t%s\n"), Info_ValueForKey(s, "sv_maxteams"));
		Com_sprintf(serverInfoText + strlen(serverInfoText), sizeof(serverInfoText) - strlen(serverInfoText), _("Max. clients:\t%s\n"), Info_ValueForKey(s, "sv_maxclients"));
		Com_sprintf(serverInfoText + strlen(serverInfoText), sizeof(serverInfoText) - strlen(serverInfoText), _("Max. soldiers per player:\t%s\n"), Info_ValueForKey(s, "sv_maxsoldiersperplayer"));
		Com_sprintf(serverInfoText + strlen(serverInfoText), sizeof(serverInfoText) - strlen(serverInfoText), _("Max. soldiers per team:\t%s\n"), Info_ValueForKey(s, "sv_maxsoldiersperteam"));
		Com_sprintf(serverInfoText + strlen(serverInfoText), sizeof(serverInfoText) - strlen(serverInfoText), _("Password protected:\t%s\n"), Info_ValueForKey(s, "sv_needpass"));
		menuText[TEXT_STANDARD] = serverInfoText;
		userInfoText[0] = '\0';
		do {
			token = COM_Parse(&users);
			if (!users)
				break;
			team = atoi(token);
			token = COM_Parse(&users);
			if (!users)
				break;
			Com_sprintf(userInfoText + strlen(userInfoText), sizeof(userInfoText) - strlen(userInfoText), "%s\t%i\n", token, team);
		} while (1);
		menuText[TEXT_LIST] = userInfoText;
		Cvar_Set("mn_server_ip", stream_peer_name(stream, buf, sizeof(buf), qtrue));
		MN_PushMenu("serverinfo");
	} else
		Com_Printf("%c%s", 1, s);
}

/**
 * @sa CL_ServerInfo_f
 * @sa CL_ParseServerInfoMessage
 */
static void CL_ServerInfoCallback (struct net_stream *s)
{
	struct dbuffer *buf = NET_ReadMsg(s);

	if (!buf)
		return;

	{
		int cmd = NET_ReadByte(buf);
		char *str = NET_ReadStringLine(buf);

		if (cmd == clc_oob && Q_strncmp(str, "print", 5) == 0) {
			str = NET_ReadString(buf);
			if (str)
				CL_ParseServerInfoMessage(s, str);
		}
	}
	free_stream(s);
}

/**
 * @sa CL_PingServers_f
 * @todo Not yet thread-safe
 */
static int CL_QueryMasterServer (void *data)
{
	char *responseBuf;
	const char *serverList;
	const char *token;
	char node[MAX_VAR], service[MAX_VAR];
	int i, num;

	responseBuf = HTTP_GetURL(va("%s/ufo/masterserver.php?query", masterserver_url->string));
	if (!responseBuf) {
		Com_Printf("Could not query masterserver\n");
		return 1;
	}

	serverList = responseBuf;

	Com_DPrintf(DEBUG_CLIENT, "masterserver response: %s\n", serverList);
	token = COM_Parse(&serverList);

	num = atoi(token);
	if (num >= MAX_SERVERLIST) {
		Com_DPrintf(DEBUG_CLIENT, "Too many servers: %i\n", num);
		num = MAX_SERVERLIST;
	}
	for (i = 0; i < num; i++) {
		/* host */
		token = COM_Parse(&serverList);
		if (!*token || !serverList) {
			Com_Printf("Could not finish the masterserver response parsing\n");
			break;
		}
		Q_strncpyz(node, token, sizeof(node));
		/* port */
		token = COM_Parse(&serverList);
		if (!*token || !serverList) {
			Com_Printf("Could not finish the masterserver response parsing\n");
			break;
		}
		Q_strncpyz(service, token, sizeof(service));
		CL_AddServerToList(node, service);
	}

	Mem_Free(responseBuf);

	return 0;
}

/**
 * @sa SV_DiscoveryCallback
 */
static void CL_ServerListDiscoveryCallback (struct datagram_socket *s, const char *buf, int len, struct sockaddr *from)
{
	const char match[] = "discovered";
	if (len == sizeof(match) && memcmp(buf, match, len) == 0) {
		char node[MAX_VAR];
		char service[MAX_VAR];
		sockaddr_to_strings(s, from, node, sizeof(node), service, sizeof(service));
		CL_AddServerToList(node, service);
	}
}

/**
 * @brief Command callback when you click the connect button from
 * the multiplayer menu
 * @note called via server_connect
 */
static void CL_ServerConnect_f (void)
{
	const char *ip = Cvar_VariableString("mn_server_ip");
	aircraft_t *aircraft;

	aircraft = AIR_AircraftGetFromIdx(0);
	if (!aircraft) {
		Com_Printf("CL_ServerConnect_f: Not in multiplayer mode\n");
		return;
	}

	/* @todo: if we are in multiplayer auto generate a team */
	if (!B_GetNumOnTeam(aircraft)) {
		MN_Popup(_("Error"), _("Assemble a team first"));
		return;
	}

	if (Cvar_VariableInteger("mn_server_need_password") && !*info_password->string) {
		MN_PushMenu("serverpassword");
		return;
	}

	if (ip) {
		Com_DPrintf(DEBUG_CLIENT, "CL_ServerConnect_f: connect to %s\n", ip);
		Cbuf_AddText(va("connect %s\n", ip));
	}
}

/**
 * @brief Add a new bookmark
 *
 * bookmarks are saved in cvar adr[0-15]
 */
static void CL_BookmarkAdd_f (void)
{
	int i;
	const char *bookmark = NULL;
	const char *newBookmark = NULL;

	if (Cmd_Argc() < 2) {
		newBookmark = Cvar_VariableString("mn_server_ip");
		if (!newBookmark) {
			Com_Printf("Usage: %s <ip>\n", Cmd_Argv(0));
			return;
		}
	} else
		newBookmark = Cmd_Argv(1);

	for (i = 0; i < MAX_BOOKMARKS; i++) {
		bookmark = Cvar_VariableString(va("adr%i", i));
		if (!*bookmark) {
			Cvar_Set(va("adr%i", i), newBookmark);
			return;
		}
	}
	/* bookmarks are full - overwrite the first entry */
	MN_Popup(_("Notice"), _("All bookmark slots are used - please removed unused entries and repeat this step"));
}

/**
 * @sa CL_ParseServerInfoMessage
 */
static void CL_BookmarkListClick_f (void)
{
	int num;
	const char *bookmark = NULL;

	if (Cmd_Argc() < 2) {
		Com_Printf("Usage: %s <num>\n", Cmd_Argv(0));
		return;
	}

	num = atoi(Cmd_Argv(1));
	if (num < 0 || num >= MAX_BOOKMARKS)
		return;

	bookmark = Cvar_VariableString(va("adr%i", num));

	if (bookmark) {
		Cvar_Set("mn_server_ip", bookmark);
		Cbuf_AddText("server_info;");
	}
}

/**
 * @sa CL_ServerInfoCallback
 */
static void CL_ServerInfo_f (void)
{
	struct net_stream *s;
	switch (Cmd_Argc()) {
	case 2:
		s = NET_Connect(Cmd_Argv(1), va("%d", PORT_SERVER));
		break;
	case 3:
		s = NET_Connect(Cmd_Argv(1), Cmd_Argv(2));
		break;
	default:
		s = NET_Connect(Cvar_VariableString("mn_server_ip"), va("%d", PORT_SERVER));
		break;
	}
	if (s) {
		NET_OOB_Printf(s, "status %i", PROTOCOL_VERSION);
		stream_callback(s, &CL_ServerInfoCallback);
	} else
		Com_Printf("Could not connect to host\n");
}

/**
 * @brief Callback for bookmark nodes in multiplayer menu (mp_bookmarks)
 * @sa CL_ParseServerInfoMessage
 */
static void CL_ServerListClick_f (void)
{
	int num, i;

	if (Cmd_Argc() < 2) {
		Com_Printf("Usage: %s <num>\n", Cmd_Argv(0));
		return;
	}
	num = atoi(Cmd_Argv(1));

	menuText[TEXT_STANDARD] = serverInfoText;
	if (num >= 0 && num < serverListLength)
		for (i = 0; i < serverListLength; i++)
			if (serverList[i].pinged && serverList[i].serverListPos == num) {
				/* found the server - grab the infos for this server */
				Cbuf_AddText(va("server_info %s %s;", serverList[i].node, serverList[i].service));
				return;
			}
}

/**
 * @brief Send the teaminfo string to server
 * @sa CL_ParseTeamInfoMessage
 */
static void CL_SelectTeam_Init_f (void)
{
	/* reset menu text */
	MN_MenuTextReset(TEXT_STANDARD);

	NET_OOB_Printf(cls.stream, "teaminfo %i", PROTOCOL_VERSION);
	menuText[TEXT_STANDARD] = _("Select a free team or your coop team");
}

/**< this is true if pingservers was already executed */
static qboolean serversAlreadyQueried = qfalse;

static int lastServerQuery = 0;

/* ms until the server query timed out */
#define SERVERQUERYTIMEOUT 40000

/**
 * @brief The first function called when entering the multiplayer menu, then CL_Frame takes over
 * @sa CL_ParseServerInfoMessage
 * @note Use a parameter for pingservers to update the current serverlist
 */
static void CL_PingServers_f (void)
{
	int i;
	char name[6];
	const char *adrstring;

	/* refresh the list */
	if (Cmd_Argc() == 2) {
		/* reset current list */
		serverText[0] = 0;
		serverListPos = 0;
		serverListLength = 0;
		serversAlreadyQueried = qfalse;
		for (i = 0; i < MAX_SERVERLIST; i++) {
			free(serverList[i].node);
			free(serverList[i].service);
		}
		memset(serverList, 0, sizeof(serverList_t) * MAX_SERVERLIST);
	} else {
		menuText[TEXT_LIST] = serverText;
		return;
	}

	if (!cls.datagram_socket)
		cls.datagram_socket = new_datagram_socket(NULL, va("%d", PORT_CLIENT), &CL_ServerListDiscoveryCallback);

	/* broadcast search for all the servers int the local network */
	if (cls.datagram_socket) {
		char buf[] = "discover";
		broadcast_datagram(cls.datagram_socket, buf, sizeof(buf), PORT_SERVER);
	}

	for (i = 0; i < MAX_BOOKMARKS; i++) {
		char service[256];
		const char *p;
		Com_sprintf(name, sizeof(name), "adr%i", i);
		adrstring = Cvar_VariableString(name);
		if (!adrstring || !adrstring[0])
			continue;

		p = strrchr(adrstring, ':');
		if (p)
			Q_strncpyz(service, p + 1, sizeof(service));
		else
			Com_sprintf(service, sizeof(service), "%d", PORT_SERVER);
		CL_AddServerToList(adrstring, service);
	}

	menuText[TEXT_LIST] = serverText;

	/* don't query the masterservers with every call */
	if (serversAlreadyQueried) {
		if (lastServerQuery + SERVERQUERYTIMEOUT > cls.realtime)
			return;
	} else
		serversAlreadyQueried = qtrue;

	lastServerQuery = cls.realtime;

	/* query master server? */
	if (Cmd_Argc() == 2 && Q_strcmp(Cmd_Argv(1), "local")) {
		Com_DPrintf(DEBUG_CLIENT, "Query masterserver\n");
		/*SDL_CreateThread(CL_QueryMasterServer, NULL);*/
		CL_QueryMasterServer(NULL);
	}
}


/**
 * @brief Responses to broadcasts, etc
 * @sa CL_ReadPackets
 * @sa CL_Frame
 */
static void CL_ConnectionlessPacket (struct dbuffer *msg)
{
	char *s;
	const char *c;
	int i;

	s = NET_ReadStringLine(msg);

	Cmd_TokenizeString(s, qfalse);

	c = Cmd_Argv(0);

	Com_DPrintf(DEBUG_CLIENT, "server OOB: %s\n", Cmd_Args());

	/* server connection */
	if (!Q_strncmp(c, "client_connect", 13)) {
		const char *p;
		for (i = 1; i < Cmd_Argc(); i++) {
			p = Cmd_Argv(i);
			if (!Q_strncmp(p, "dlserver=", 9)) {
				p += 9;
				Com_sprintf(cls.downloadReferer, sizeof(cls.downloadReferer), "ufo://%s", cls.servername);
				CL_SetHTTPServer(p);
				if (cls.downloadServer[0])
					Com_Printf("HTTP downloading enabled, URL: %s\n", cls.downloadServer);
			}
		}
		if (cls.state == ca_connected) {
			Com_Printf("Dup connect received. Ignored.\n");
			return;
		}
		msg = new_dbuffer();
		NET_WriteByte(msg, clc_stringcmd);
		NET_WriteString(msg, "new");
		NET_WriteMsg(cls.stream, msg);
		CL_SetClientState(ca_connected);
		return;
	}

	/* remote command from gui front end */
	if (!Q_strncmp(c, "cmd", 3)) {
		if (!stream_is_loopback(cls.stream)) {
			Com_Printf("Command packet from remote host. Ignored.\n");
			return;
		}
		s = NET_ReadString(msg);
		Cbuf_AddText(s);
		Cbuf_AddText("\n");
		return;
	}

	/* teaminfo command */
	if (!Q_strncmp(c, "teaminfo", 8)) {
		CL_ParseTeamInfoMessage(msg);
		return;
	}

	/* ping from server */
	if (!Q_strncmp(c, "ping", 4)) {
		NET_OOB_Printf(cls.stream, "ack");
		return;
	}

	/* echo request from server */
	if (!Q_strncmp(c, "echo", 4)) {
		NET_OOB_Printf(cls.stream, "%s", Cmd_Argv(1));
		return;
	}

	/* print */
	if (!Q_strncmp(c, "print", 4)) {
		s = NET_ReadString(msg);
		MN_Popup(_("Notice"), _(s));
		return;
	}

	Com_Printf("Unknown command '%s'.\n", c);
}

/**
 * @sa CL_ConnectionlessPacket
 * @sa CL_Frame
 * @sa CL_ParseServerMessage
 * @sa NET_ReadMsg
 * @sa SV_ReadPacket
 */
static void CL_ReadPackets (void)
{
	struct dbuffer *msg;
	while ((msg = NET_ReadMsg(cls.stream))) {
		int cmd = NET_ReadByte(msg);
		if (cmd == clc_oob)
			CL_ConnectionlessPacket(msg);
		else
			CL_ParseServerMessage(cmd, msg);
		free_dbuffer(msg);
	}
}


static void CL_Userinfo_f (void)
{
	Com_Printf("User info settings:\n");
	Info_Print(Cvar_Userinfo());
}

/**
 * @brief Increase or decrease the teamnum
 * @sa CL_SelectTeam_Init_f
 */
static void CL_TeamNum_f (void)
{
	int max = 4;
	int maxteamnum = 0;
	int i = teamnum->integer;
	static char buf[MAX_STRING_CHARS];

	maxteamnum = Cvar_VariableInteger("mn_maxteams");

	if (maxteamnum > 0)
		max = maxteamnum;

	teamnum->modified = qfalse;

	if (i <= TEAM_CIVILIAN || i > teamData.maxteams) {
		Cvar_SetValue("teamnum", DEFAULT_TEAMNUM);
		i = DEFAULT_TEAMNUM;
	}

	if (Q_strncmp(Cmd_Argv(0), "teamnum_dec", 11)) {
		for (i--; i > TEAM_CIVILIAN; i--) {
			if (teamData.maxplayersperteam > teamData.teamCount[i]) {
				Cvar_SetValue("teamnum", i);
				Com_sprintf(buf, sizeof(buf), _("Current team: %i"), i);
				menuText[TEXT_STANDARD] = buf;
				break;
			} else {
				menuText[TEXT_STANDARD] = _("Team is already in use");
#if DEBUG
				Com_DPrintf(DEBUG_CLIENT, "team %i: %i (max: %i)\n", i, teamData.teamCount[i], teamData.maxplayersperteam);
#endif
			}
		}
	} else {
		for (i++; i <= teamData.maxteams; i++) {
			if (teamData.maxplayersperteam > teamData.teamCount[i]) {
				Cvar_SetValue("teamnum", i);
				Com_sprintf(buf, sizeof(buf), _("Current team: %i"), i);
				menuText[TEXT_STANDARD] = buf;
				break;
			} else {
				menuText[TEXT_STANDARD] = _("Team is already in use");
#if DEBUG
				Com_DPrintf(DEBUG_CLIENT, "team %i: %i (max: %i)\n", i, teamData.teamCount[i], teamData.maxplayersperteam);
#endif
			}
		}
	}

#if 0
	if (!teamnum->modified)
		menuText[TEXT_STANDARD] = _("Invalid or full team");
#endif
	CL_SelectTeam_Init_f();
}

static int spawnCountFromServer = -1;
/**
 * @brief Send the clc_teaminfo command to server
 * @sa CL_SendCurTeamInfo
 */
static void CL_SpawnSoldiers_f (void)
{
	int n = teamnum->integer;
	base_t *base = NULL;
	aircraft_t *aircraft = cls.missionaircraft;
	chrList_t chr_list_temp;
	int i;

	if (!cls.missionaircraft) {
		Com_Printf("CL_SpawnSoldiers_f: No mission aircraft\n");
		return;
	}

	base = CP_GetMissionBase();

	if (!ccs.singleplayer && base && !teamData.parsed) {
		Com_Printf("CL_SpawnSoldiers_f: teaminfo unparsed\n");
		return;
	}

	if (soldiersSpawned) {
		Com_Printf("CL_SpawnSoldiers_f: Soldiers are already spawned\n");
		return;
	}

	if (!ccs.singleplayer && base) {
		/* we are already connected and in this list */
		if (n <= TEAM_CIVILIAN || teamData.maxplayersperteam < teamData.teamCount[n]) {
			menuText[TEXT_STANDARD] = _("Invalid or full team");
			Com_Printf("CL_SpawnSoldiers_f: Invalid or full team %i\n"
				"  maxplayers per team: %i - players on team: %i",
				n, teamData.maxplayersperteam, teamData.teamCount[n]);
			return;
		}
	}

	/* maybe we start the map directly from commandline for testing */
	if (base) {
		/* convert aircraft team to chr_list */
		for (i = 0, chr_list_temp.num = 0; i < aircraft->maxTeamSize; i++) {
			if (aircraft->teamIdxs[i] != -1) {
				chr_list_temp.chr[chr_list_temp.num] = &gd.employees[aircraft->teamTypes[i]][aircraft->teamIdxs[i]].chr;
				chr_list_temp.num++;
			}
		}

		if (chr_list_temp.num <= 0) {
			Com_DPrintf(DEBUG_CLIENT, "CL_SpawnSoldiers_f: Error - team number <= zero - %i\n", chr_list_temp.num);
		} else {
			/* send team info */
			struct dbuffer *msg = new_dbuffer();
			CL_SendCurTeamInfo(msg, &chr_list_temp);
			NET_WriteMsg(cls.stream, msg);
		}
	}

	{
		struct dbuffer *msg = new_dbuffer();
		NET_WriteByte(msg, clc_stringcmd);
		NET_WriteString(msg, va("spawn %i\n", spawnCountFromServer));
		NET_WriteMsg(cls.stream, msg);
	}

	soldiersSpawned = qtrue;

	/* spawn immediately if in single-player, otherwise wait for other players to join */
	if (ccs.singleplayer) {
		/* activate hud */
		MN_PushMenu(mn_hud->string);
		Cvar_Set("mn_active", mn_hud->string);
	} else {
		MN_PushMenu("multiplayer_wait");
	}
}

/**
 * @brief
 * @note Called after precache was sent from the server
 * @sa SV_Configstrings_f
 * @sa CL_Precache_f
 */
void CL_RequestNextDownload (void)
{
	unsigned map_checksum = 0;
	unsigned ufoScript_checksum = 0;
	const char *buf;

	if (cls.state != ca_connected) {
		Com_Printf("CL_RequestNextDownload: Not connected (%i)\n", cls.state);
		return;
	}

	/* for singleplayer game this is already loaded in our local server
	 * and if we are the server we don't have to reload the map here, too */
	if (!Com_ServerState()) {
		/* activate the map loading screen for multiplayer, too */
		SCR_BeginLoadingPlaque();

		/* check download */
		if (precache_check == CS_MODELS) { /* confirm map */
			if (*cl.configstrings[CS_TILES] != '+') {
				if (!CL_CheckOrDownloadFile(va("maps/%s.bsp", cl.configstrings[CS_TILES])))
					return; /* started a download */
			}
			precache_check = CS_MODELS + 1;
		}

		/* map might still be downloading? */
		if (CL_PendingHTTPDownloads())
			return;

		while ((buf = FS_GetFileData("ufos/*.ufo")) != NULL)
			ufoScript_checksum += LittleLong(Com_BlockChecksum(buf, strlen(buf)));
		FS_GetFileData(NULL);

		CM_LoadMap(cl.configstrings[CS_TILES], cl.configstrings[CS_POSITIONS], &map_checksum);
		if (!*cl.configstrings[CS_VERSION] || !*cl.configstrings[CS_MAPCHECKSUM]
		 || !*cl.configstrings[CS_UFOCHECKSUM] || !*cl.configstrings[CS_OBJECTAMOUNT]) {
			Com_sprintf(popupText, sizeof(popupText), _("Local game version (%s) differs from the servers"), UFO_VERSION);
			MN_Popup(_("Error"), popupText);
			Com_Error(ERR_DISCONNECT, "Local game version (%s) differs from the servers", UFO_VERSION);
			return;
		/* checksum doesn't match with the one the server gave us via configstring */
		} else if (map_checksum != atoi(cl.configstrings[CS_MAPCHECKSUM])) {
			MN_Popup(_("Error"), _("Local map version differs from server"));
			Com_Error(ERR_DISCONNECT, "Local map version differs from server: %u != '%s'",
				map_checksum, cl.configstrings[CS_MAPCHECKSUM]);
			return;
		/* amount of objects from script files doensn't match */
		} else if (csi.numODs != atoi(cl.configstrings[CS_OBJECTAMOUNT])) {
			MN_Popup(_("Error"), _("Script files are not the same"));
			Com_Error(ERR_DISCONNECT, "Script files are not the same");
			return;
		/* checksum doesn't match with the one the server gave us via configstring */
		} else if (atoi(cl.configstrings[CS_UFOCHECKSUM]) && ufoScript_checksum != atoi(cl.configstrings[CS_UFOCHECKSUM])) {
			Com_Printf("You are using modified ufo script files - might produce problems\n");
		} else if (Q_strncmp(UFO_VERSION, cl.configstrings[CS_VERSION], sizeof(UFO_VERSION))) {
			Com_sprintf(popupText, sizeof(popupText), _("Local game version (%s) differs from the servers (%s)"), UFO_VERSION, cl.configstrings[CS_VERSION]);
			MN_Popup(_("Error"), popupText);
			Com_Error(ERR_DISCONNECT, "Local game version (%s) differs from the servers (%s)", UFO_VERSION, cl.configstrings[CS_VERSION]);
			return;
		}
	}

	CL_RegisterSounds();
	CL_LoadMedia();

	soldiersSpawned = qfalse;
	spawnCountFromServer = atoi(Cmd_Argv(1));

	{
		struct dbuffer *msg = new_dbuffer();
		/* send begin */
		/* this will activate the render process (see client state ca_active) */
		NET_WriteByte(msg, clc_stringcmd);
		/* see CL_StartGame */
		NET_WriteString(msg, va("begin %i\n", spawnCountFromServer));
		NET_WriteMsg(cls.stream, msg);
	}

	/* for singleplayer the soldiers get spawned here */
	if (ccs.singleplayer && cls.missionaircraft)
		CL_SpawnSoldiers_f();

	cls.waitingForStart = cls.realtime;
}


/**
 * @brief The server will send this command right before allowing the client into the server
 * @sa CL_StartGame
 * @todo recheck the checksum server side
 * @sa SV_Configstrings_f
 */
static void CL_Precache_f (void)
{
	precache_check = CS_MODELS;

	CL_RequestNextDownload();
}

/**
 * @brief Precaches all models at game startup - for faster access
 * @todo In case of vid restart due to changed settings the vid_genericPool is
 * wiped away, too. So the models has to be reloaded with every map change
 */
static void CL_PrecacheModels (void)
{
	int i;
	float loading;
	float percent = 40.0f;

	if (cl_precache->integer)
		Com_PrecacheCharacterModels(); /* 55% */
	else
		percent = 95.0f;

	loading = cls.loadingPercent;

	for (i = 0; i < csi.numODs; i++) {
		if (*csi.ods[i].model) {
			cls.model_weapons[i] = R_RegisterModelShort(csi.ods[i].model);
			if (!cls.model_weapons[i])
				Com_Printf("CL_PrecacheModels: Could not register object model: '%s'\n", csi.ods[i].model);
		}
		cls.loadingPercent += percent / csi.numODs;
		SCR_DrawPrecacheScreen(qtrue);
	}
}

/**
 * @brief Init function for clients - called after menu was inited and ufo-scripts were parsed
 * @sa Qcommon_Init
 */
void CL_InitAfter (void)
{
	int i;
	menu_t* menu;
	menuNode_t* vidModesOptions;
	selectBoxOptions_t* selectBoxOption;

	cls.loadingPercent = 2.0f;

	/* precache loading screen */
	SCR_DrawPrecacheScreen(qtrue);

	/* init irc commands and cvars */
	Irc_Init();

	/* this will init some more employee stuff */
	E_Init();

	/* init some production menu nodes */
	PR_Init();

	cls.loadingPercent = 5.0f;
	SCR_DrawPrecacheScreen(qtrue);

	/* preload all models for faster access */
	CL_PrecacheModels(); /* 95% */

	cls.loadingPercent = 100.0f;
	SCR_DrawPrecacheScreen(qtrue);

	/* link for faster access */
	MN_LinkMenuModels();

	menu = MN_GetMenu("options_video");
	if (!menu)
		Sys_Error("Could not find menu options_video\n");
	vidModesOptions = MN_GetNode(menu, "select_res");
	if (!vidModesOptions)
		Sys_Error("Could not find node select_res in menu options_video\n");
	for (i = 0; i < VID_GetModeNums(); i++) {
		selectBoxOption = MN_AddSelectboxOption(vidModesOptions);
		if (!selectBoxOption) {
			return;
		}
		Com_sprintf(selectBoxOption->label, sizeof(selectBoxOption->label), "%i:%i", vid_modes[i].width, vid_modes[i].height);
		Com_sprintf(selectBoxOption->value, sizeof(selectBoxOption->value), "%i", vid_modes[i].mode);
	}

	CL_LanguageInit();

	/* now make sure that all the precached models are stored until we quit the game
	 * otherwise they would be freed with every map change */
	R_SwitchModelMemPoolTag();
}

/**
 * @brief Called at client startup
 * @note not called for dedicated servers
 * parses all *.ufos that are needed for single- and multiplayer
 * @sa Com_ParseScripts
 * @sa CL_ParseScriptSecond
 * @sa CL_ParseScriptFirst
 * @note Nothing here should depends on items, equipments, actors and all other
 * entities that are parsed in Com_ParseScripts (because maybe items are not parsed
 * but e.g. techs would need those parsed items - thus we have to parse e.g. techs
 * at a later stage)
 * @note This data should not go into cl_localPool memory pool - this data is
 * persistent until you shutdown the game
 */
void CL_ParseClientData (const char *type, const char *name, const char **text)
{
	if (!Q_strncmp(type, "shader", 6))
		CL_ParseShaders(name, text);
	else if (!Q_strncmp(type, "font", 4))
		CL_ParseFont(name, text);
	else if (!Q_strncmp(type, "tutorial", 8))
		MN_ParseTutorials(name, text);
	else if (!Q_strncmp(type, "menu_model", 10))
		MN_ParseMenuModel(name, text);
	else if (!Q_strncmp(type, "menu", 4))
		MN_ParseMenu(name, text);
	else if (!Q_strncmp(type, "particle", 8))
		CL_ParseParticle(name, text);
	else if (!Q_strncmp(type, "sequence", 8))
		CL_ParseSequence(name, text);
	else if (!Q_strncmp(type, "aircraft", 8))
		AIR_ParseAircraft(name, text, qfalse);
	else if (!Q_strncmp(type, "campaign", 8))
		CL_ParseCampaign(name, text);
	else if (!Q_strncmp(type, "ugv", 3))
		CL_ParseUGVs(name, text);
	else if (!Q_strncmp(type, "tips", 4))
		CL_ParseTipsOfTheDay(name, text);
	else if (!Q_strncmp(type, "language", 8))
		CL_ParseLanguages(name, text);
}

/**
 * @brief Parsing only for singleplayer
 *
 * parsed if we are no dedicated server
 * first stage parses all the main data into their struct
 * see CL_ParseScriptSecond for more details about parsing stages
 * @sa CL_ReadSinglePlayerData
 * @sa Com_ParseScripts
 * @sa CL_ParseScriptSecond
 * @note write into cl_localPool - free on every game restart and reparse
 */
static void CL_ParseScriptFirst (const char *type, const char *name, const char **text)
{
	/* check for client interpretable scripts */
	if (!Q_strncmp(type, "mission", 7))
		CL_ParseMission(name, text);
	else if (!Q_strncmp(type, "up_chapters", 11))
		UP_ParseUpChapters(name, text);
	else if (!Q_strncmp(type, "building", 8))
		B_ParseBuildings(name, text, qfalse);
	else if (!Q_strncmp(type, "researched", 10))
		CL_ParseResearchedCampaignItems(name, text);
	else if (!Q_strncmp(type, "researchable", 12))
		CL_ParseResearchableCampaignStates(name, text, qtrue);
	else if (!Q_strncmp(type, "notresearchable", 15))
		CL_ParseResearchableCampaignStates(name, text, qfalse);
	else if (!Q_strncmp(type, "tech", 4))
		RS_ParseTechnologies(name, text);
	else if (!Q_strncmp(type, "base", 4))
		B_ParseBases(name, text);
	else if (!Q_strncmp(type, "nation", 6))
		CL_ParseNations(name, text);
	else if (!Q_strncmp(type, "rank", 4))
		CL_ParseMedalsAndRanks(name, text, qtrue);
	else if (!Q_strncmp(type, "mail", 4))
		CL_ParseEventMails(name, text);
	else if (!Q_strncmp(type, "components", 10))
		INV_ParseComponents(name, text);
#if 0
	else if (!Q_strncmp(type, "medal", 5))
		Com_ParseMedalsAndRanks(name, &text, qfalse);
#endif
}

/**
 * @brief Parsing only for singleplayer
 *
 * parsed if we are no dedicated server
 * second stage links all the parsed data from first stage
 * example: we need a techpointer in a building - in the second stage the buildings and the
 * techs are already parsed - so now we can link them
 * @sa CL_ReadSinglePlayerData
 * @sa Com_ParseScripts
 * @sa CL_ParseScriptFirst
 * @note make sure that the client hunk was cleared - otherwise it may overflow
 * @note write into cl_localPool - free on every game restart and reparse
 */
static void CL_ParseScriptSecond (const char *type, const char *name, const char **text)
{
	/* check for client interpretable scripts */
	if (!Q_strncmp(type, "stage", 5))
		CL_ParseStage(name, text);
	else if (!Q_strncmp(type, "building", 8))
		B_ParseBuildings(name, text, qtrue);
	else if (!Q_strncmp(type, "aircraft", 8))
		AIR_ParseAircraft(name, text, qtrue);
}

/** @brief struct that holds the sanity check data */
typedef struct {
	qboolean (*check)(void);	/**< function pointer to check function */
	const char* name;			/**< name of the subsystem to check */
} sanity_functions_t;

/** @brief Data for sanity check of parsed script data */
static const sanity_functions_t sanity_functions[] = {
	{B_ScriptSanityCheck, "buildings"},
	{RS_ScriptSanityCheck, "tech"},
	{AIR_ScriptSanityCheck, "aircraft"},
	{MN_ScriptSanityCheck, "menu"},

	{NULL, NULL}
};

/**
 * @brief Check the parsed script values for errors after parsing every script file
 * @sa CL_ReadSinglePlayerData
 */
void CL_ScriptSanityCheck (void)
{
	qboolean status;
	const sanity_functions_t *s;

	Com_Printf("Sanity check for script data\n");
	s = sanity_functions;
	while (s->check) {
		status = s->check();
		Com_Printf("...%s %s\n", s->name, (status ? "ok" : "failed"));
		s++;
	}
}

/**
 * @brief Read the data into gd for singleplayer campaigns
 * @sa SAV_GameLoad
 * @sa CL_GameNew_f
 * @sa CL_ResetSinglePlayerData
 */
void CL_ReadSinglePlayerData (void)
{
	const char *type, *name, *text;

	/* pre-stage parsing */
	FS_BuildFileList("ufos/*.ufo");
	FS_NextScriptHeader(NULL, NULL, NULL);
	text = NULL;

	CL_ResetSinglePlayerData();
	while ((type = FS_NextScriptHeader("ufos/*.ufo", &name, &text)) != 0)
		CL_ParseScriptFirst(type, name, &text);

	/* fill in IDXs for required research techs */
	RS_RequiredIdxAssign();

	/* stage two parsing */
	FS_NextScriptHeader(NULL, NULL, NULL);
	text = NULL;

	Com_DPrintf(DEBUG_CLIENT, "Second stage parsing started...\n");
	while ((type = FS_NextScriptHeader("ufos/*.ufo", &name, &text)) != 0)
		CL_ParseScriptSecond(type, name, &text);

	Com_Printf("Global data loaded - size "UFO_SIZE_T" bytes\n", sizeof(gd));
	Com_Printf("...techs: %i\n", gd.numTechnologies);
	Com_Printf("...buildings: %i\n", gd.numBuildingTypes);
	Com_Printf("...ranks: %i\n", gd.numRanks);
	Com_Printf("...nations: %i\n", gd.numNations);
	Com_Printf("\n");
}

/**
 * @brief Writes key bindings and archived cvars to config.cfg
 */
static void CL_WriteConfiguration (void)
{
	char path[MAX_OSPATH];

	if (cls.state == ca_uninitialized)
		return;

	if (strlen(FS_Gamedir()) >= MAX_OSPATH) {
		Com_Printf("Error: Can't save. Write path exceeded MAX_OSPATH\n");
		return;
	}
	Com_sprintf(path, sizeof(path), "%s/config.cfg", FS_Gamedir());
	Com_Printf("Save user settings to %s\n", path);
	Cvar_WriteVariables(path);
}

/** @brief Cvars for initial check (popup at first start) */
static cvarList_t checkcvar[] = {
	{"name", NULL, NULL},

	{NULL, NULL, NULL}
};
/**
 * @brief Check cvars for some initial values that should/must be set
 */
static void CL_CheckCvars_f (void)
{
	int i = 0;

	while (checkcvar[i].name) {
		if (!checkcvar[i].var)
			checkcvar[i].var = Cvar_Get(checkcvar[i].name, "", 0, NULL);
		if (!*(checkcvar[i].var->string))
			Cbuf_AddText("mn_push checkcvars;");
		i++;
	}
}

#ifdef DEBUG
/**
 * @brief Print the configstrings to game console
 */
static void CL_ShowConfigstrings_f (void)
{
	int i;
	for (i = 0; i < MAX_CONFIGSTRINGS; i++)
		if (*cl.configstrings[i])
			Com_Printf("cl.configstrings[%2i]: %s\n", i, cl.configstrings[i]);
}

/**
 * @brief Shows the sizes some parts of globalData_t uses - this is only to
 * analyse where the most optimization potential is hiding
 */
static void CL_GlobalDataSizes_f (void)
{
	Com_Printf(
		"globalData_t size: %10Zu bytes\n"
		"bases              %10Zu bytes\n"
		"buildings          %10Zu bytes\n"
		"nations            %10Zu bytes\n"
		"ranks              %10Zu bytes\n"
		"ugv                %10Zu bytes\n"
		"productions        %10Zu bytes\n"
		"buildingTypes      %10Zu bytes\n"
		"employees          %10Zu bytes\n"
		"eventMails         %10Zu bytes\n"
		"upChapters         %10Zu bytes\n"
		"technologies       %10Zu bytes\n"
		,
		sizeof(globalData_t),
		sizeof(gd.bases),
		sizeof(gd.buildings),
		sizeof(gd.nations),
		sizeof(gd.ranks),
		sizeof(gd.ugvs),
		sizeof(gd.productions),
		sizeof(gd.buildingTypes),
		sizeof(gd.employees),
		sizeof(gd.eventMails),
		sizeof(gd.upChapters),
		sizeof(gd.technologies)
	);

	Com_Printf(
		"bases:\n"
		"alienscont         %10Zu bytes\n"
		"capacities         %10Zu bytes\n"
		"equipByBuyType     %10Zu bytes\n"
		"hospitalList       %10Zu bytes\n"
		"hospitalMissionLst %10Zu bytes\n"
		"aircraft           %10Zu bytes\n"
		"aircraft (single)  %10Zu bytes\n"
		"allBuildingsList   %10Zu bytes\n"
		"radar              %10Zu bytes\n"
		,
		sizeof(gd.bases[0].alienscont),
		sizeof(gd.bases[0].capacities),
		sizeof(gd.bases[0].equipByBuyType),
		sizeof(gd.bases[0].hospitalList),
		sizeof(gd.bases[0].hospitalMissionList),
		sizeof(gd.bases[0].aircraft),
		sizeof(aircraft_t),
		sizeof(gd.bases[0].allBuildingsList),
		sizeof(gd.bases[0].radar)
	);
}

/**
 * @brief Dumps the globalData_t structure to a file
 */
static void CL_DumpGlobalDataToFile_f (void)
{
	qFILE f;

	memset(&f, 0, sizeof(qFILE));
	FS_FOpenFileWrite(va("%s/gd.dump", FS_Gamedir()), &f);
	if (!f.f) {
		Com_Printf("CL_DumpGlobalDataToFile_f: Error opening dump file for writing");
		return;
	}

	FS_Write(&gd, sizeof(gd), &f);

	FS_FCloseFile(&f);
}
#endif /* DEBUG */

/**
 * @brief Autocomplete function for some network functions
 * @sa Cmd_AddParamCompleteFunction
 * @todo Extend this for all the servers on the server browser list
 */
static int CL_CompleteNetworkAddress (const char *partial, const char **match)
{
	int i, matches = 0;
	const char *localMatch[MAX_COMPLETE];
	const char *adrStr;
	size_t len;

	len = strlen(partial);
	if (!len) {
		/* list them all if there was no parameter given */
		for (i = 0; i < MAX_BOOKMARKS; i++) {
			adrStr = Cvar_VariableString(va("adr%i", i));
			if (*adrStr)
				Com_Printf("%s\n", adrStr);
		}
		return 0;
	}

	localMatch[matches] = NULL;

	/* search all matches and fill the localMatch array */
	for (i = 0; i < MAX_BOOKMARKS; i++) {
		adrStr = Cvar_VariableString(va("adr%i", i));
		if (*adrStr && !Q_strncmp(partial, adrStr, len)) {
			Com_Printf("%s\n", adrStr);
			localMatch[matches++] = adrStr;
			if (matches >= MAX_COMPLETE)
				break;
		}
	}

	return Cmd_GenericCompleteFunction(len, match, matches, localMatch);
}

/**
 * @brief Calls all reset functions for all subsystems like production and research
 * also inits the cvars and commands
 * @sa CL_Init
 */
static void CL_InitLocal (void)
{
	int i;

	memset(serverList, 0, sizeof(serverList_t) * MAX_SERVERLIST);

	CL_SetClientState(ca_disconnected);
	cls.stream = NULL;
	cls.realtime = Sys_Milliseconds();

	INVSH_InitInventory(invList);
	Con_CheckResize();
	IN_Init();

	SAV_Init();
	MN_ResetMenus();
	CL_ResetParticles();
	CL_ResetCampaign();
	BS_ResetMarket();
	CL_ResetSequences();
	CL_ResetTeams();

	CL_TipOfTheDayInit();

	for (i = 0; i < MAX_BOOKMARKS; i++)
		Cvar_Get(va("adr%i", i), "", CVAR_ARCHIVE, "Bookmark for network ip");

	/* register our variables */
	cl_isometric = Cvar_Get("r_isometric", "0", CVAR_ARCHIVE, "Draw the world in isometric mode");

	cl_show_tooltips = Cvar_Get("cl_show_tooltips", "1", CVAR_ARCHIVE, "Show tooltips in menus and hud");
	cl_show_cursor_tooltips = Cvar_Get("cl_show_cursor_tooltips", "1", CVAR_ARCHIVE, "Show cursor tooltips in tactical game mode");

	cl_camrotspeed = Cvar_Get("cl_camrotspeed", "250", CVAR_ARCHIVE, NULL);
	cl_camrotaccel = Cvar_Get("cl_camrotaccel", "400", CVAR_ARCHIVE, NULL);
	cl_cammovespeed = Cvar_Get("cl_cammovespeed", "750", CVAR_ARCHIVE, NULL);
	cl_cammoveaccel = Cvar_Get("cl_cammoveaccel", "1250", CVAR_ARCHIVE, NULL);
	cl_camyawspeed = Cvar_Get("cl_camyawspeed", "160", CVAR_ARCHIVE, NULL);
	cl_campitchmax = Cvar_Get("cl_campitchmax", "90", 0, "Max camera pitch - over 90 presents apparent mouse inversion");
	cl_campitchmin = Cvar_Get("cl_campitchmin", "35", 0, "Min camera pitch - under 35 presents difficulty positioning cursor");
	cl_campitchspeed = Cvar_Get("cl_campitchspeed", "0.5", CVAR_ARCHIVE, NULL);
	cl_camzoomquant = Cvar_Get("cl_camzoomquant", "0.16", CVAR_ARCHIVE, NULL);
	cl_camzoommin = Cvar_Get("cl_camzoommin", "0.7", 0, "Minimum zoom value for tactical missions");
	cl_camzoommax = Cvar_Get("cl_camzoommax", "3.4", 0, "Maximum zoom value for tactical missions");
	cl_centerview = Cvar_Get("cl_centerview", "1", CVAR_ARCHIVE, "Center the view when selecting a new soldier");
	cl_mapzoommax = Cvar_Get("cl_mapzoommax", "6.0", CVAR_ARCHIVE, "Maximum geoscape zooming value");
	cl_mapzoommin = Cvar_Get("cl_mapzoommin", "1.0", CVAR_ARCHIVE, "Minimum geoscape zooming value");
	cl_precache = Cvar_Get("cl_precache", "1", CVAR_ARCHIVE, "Precache character models at startup - more memory usage but smaller loading times in the game");
	cl_particleWeather = Cvar_Get("cl_particleweather", "0", CVAR_ARCHIVE | CVAR_LATCH, "Switch the weather particles on or off");
	cl_fps = Cvar_Get("cl_fps", "0", CVAR_ARCHIVE, "Show frames per second");
	cl_shownet = Cvar_Get("cl_shownet", "0", CVAR_ARCHIVE, NULL);
	rcon_client_password = Cvar_Get("rcon_password", "", 0, "Remote console password");
	cl_logevents = Cvar_Get("cl_logevents", "0", 0, "Log all events to events.log");
	cl_worldlevel = Cvar_Get("cl_worldlevel", "0", 0, "Current worldlevel in tactical mode");
	cl_worldlevel->modified = qfalse;
	cl_selected = Cvar_Get("cl_selected", "0", CVAR_NOSET, "Current selected soldier");
	cl_3dmap = Cvar_Get("cl_3dmap", "0", CVAR_ARCHIVE, "3D geoscape or float geoscape");
	/* only 19 soldiers in soldier selection list */
	cl_numnames = Cvar_Get("cl_numnames", "19", CVAR_NOSET, NULL);
	difficulty = Cvar_Get("difficulty", "0", CVAR_NOSET, "Difficulty level");
	cl_start_employees = Cvar_Get("cl_start_employees", "1", CVAR_ARCHIVE, "Start with hired employees");
	cl_initial_equipment = Cvar_Get("cl_initial_equipment", "human_phalanx_initial", CVAR_ARCHIVE, "Start with assigned equipment - see cl_start_employees");
	cl_start_buildings = Cvar_Get("cl_start_buildings", "1", CVAR_ARCHIVE, "Start with initial buildings in your first base");
	cl_connecttimeout = Cvar_Get("cl_connecttimeout", "8000", CVAR_ARCHIVE, "Connection timeout for multiplayer connects");

	confirm_actions = Cvar_Get("confirm_actions", "0", CVAR_ARCHIVE, "Confirm all actions in tactical mode");

	mn_main = Cvar_Get("mn_main", "main", 0, "Which is the main menu id to return to - also see mn_active");
	mn_sequence = Cvar_Get("mn_sequence", "sequence", 0, "Which is the sequence menu node to render the sequence in");
	mn_active = Cvar_Get("mn_active", "", 0, "The active menu can will return to when hitting esc - also see mn_main");
	mn_afterdrop = Cvar_Get("mn_afterdrop", "", 0, "The menu that should be pushed after the drop function was called");
	mn_main_afterdrop = Cvar_Get("mn_main_afterdrop", "", 0, "The main menu that should be returned to after the drop function was called - will be the new mn_main value then");
	mn_hud = Cvar_Get("mn_hud", "hud", CVAR_ARCHIVE, "Which is the current selected hud");
	mn_lastsave = Cvar_Get("mn_lastsave", "", CVAR_ARCHIVE, "Last saved slot - use for the continue-campaign function");

	mn_serverlist = Cvar_Get("mn_serverlist", "0", CVAR_ARCHIVE, "0=show all, 1=hide full - servers on the serverlist");

	mn_inputlength = Cvar_Get("mn_inputlength", "32", 0, "Limit the input length for messagemenu input");
	mn_inputlength->modified = qfalse;

	/* userinfo */
	info_password = Cvar_Get("password", "", CVAR_USERINFO, NULL);
	name = Cvar_Get("name", "", CVAR_USERINFO | CVAR_ARCHIVE, "Playername");
	team = Cvar_Get("team", "human", CVAR_USERINFO | CVAR_ARCHIVE, NULL);
	equip = Cvar_Get("equip", "multiplayer_initial", CVAR_USERINFO | CVAR_ARCHIVE, NULL);
	teamnum = Cvar_Get("teamnum", "1", CVAR_USERINFO | CVAR_ARCHIVE, "Teamnum for multiplayer teamplay games");
	campaign = Cvar_Get("campaign", "main", 0, "Which is the current selected campaign id");
	msg = Cvar_Get("msg", "2", CVAR_USERINFO | CVAR_ARCHIVE, "Sets the message level for server messages the client receives");
	sv_maxclients = Cvar_Get("sv_maxclients", "1", CVAR_SERVERINFO, "If sv_maxclients is 1 we are in singleplayer - otherwise we are mutliplayer mode (see sv_teamplay)");

	masterserver_url = Cvar_Get("masterserver_url", MASTER_SERVER, CVAR_ARCHIVE, "URL of UFO:AI masterserver");

	cl_http_filelists = Cvar_Get("cl_http_filelists", "1", 0, NULL);
	cl_http_downloads = Cvar_Get("cl_http_downloads", "1", 0, "Try to download files via http");
	cl_http_max_connections = Cvar_Get("cl_http_max_connections", "1", 0, NULL);

	/* register our commands */
	Cmd_AddCommand("cmd", CL_ForwardToServer_f, "Forward to server");
	Cmd_AddCommand("pingservers", CL_PingServers_f, "Ping all servers in local network to get the serverlist");

	Cmd_AddCommand("check_cvars", CL_CheckCvars_f, "Check cvars like playername and so on");

	Cmd_AddCommand("saveconfig", CL_WriteConfiguration, "Save the configuration");

	Cmd_AddCommand("targetalign", CL_ActorTargetAlign_f, _("Target your shot to the ground"));
	Cmd_AddCommand("invopen", CL_ActorInventoryOpen_f, _("Open the actors inventory while we are in tactical mission"));

	/* text id is servers in menu_multiplayer.ufo */
	Cmd_AddCommand("server_info", CL_ServerInfo_f, NULL);
	Cmd_AddCommand("serverlist", CL_PrintServerList_f, NULL);
	Cmd_AddCommand("servers_click", CL_ServerListClick_f, NULL);
	Cmd_AddCommand("server_connect", CL_ServerConnect_f, NULL);
	Cmd_AddCommand("bookmarks_click", CL_BookmarkListClick_f, NULL);
	Cmd_AddCommand("bookmark_add", CL_BookmarkAdd_f, "Add a new bookmark - see adrX cvars");

	Cmd_AddCommand("userinfo", CL_Userinfo_f, "Prints your userinfo string");

	Cmd_AddCommand("disconnect", CL_Disconnect_f, "Disconnect from the current server");

	Cmd_AddCommand("quit", CL_Quit_f, "Quits the game");

	Cmd_AddCommand("connect", CL_Connect_f, "Connect to given ip");
	Cmd_AddParamCompleteFunction("connect", CL_CompleteNetworkAddress);
	Cmd_AddCommand("reconnect", CL_Reconnect_f, "Reconnect to last server");

	Cmd_AddCommand("rcon", CL_Rcon_f, "Execute a rcon command - see rcon_password");
	Cmd_AddParamCompleteFunction("rcon", CL_CompleteNetworkAddress);

#ifdef ACTIVATE_PACKET_COMMAND
	/* this is dangerous to leave in */
	Cmd_AddCommand("packet", CL_Packet_f, "Dangerous debug function for network testing");
#endif

	Cmd_AddCommand("env", CL_Env_f, NULL);

	Cmd_AddCommand("precache", CL_Precache_f, "Function that is called at mapload to precache map data");
	Cmd_AddCommand("mp_selectteam_init", CL_SelectTeam_Init_f, "Function that gets all connected players and let you choose a free team");
	Cmd_AddCommand("mp_wait_init", CL_WaitInit_f, "Function that inits some nodes");
	Cmd_AddCommand("spawnsoldiers", CL_SpawnSoldiers_f, "Spawns the soldiers for the selected teamnum");
	Cmd_AddCommand("teamnum_dec", CL_TeamNum_f, "Decrease the prefered teamnum");
	Cmd_AddCommand("teamnum_inc", CL_TeamNum_f, "Increase the prefered teamnum");

	Cmd_AddCommand("seq_click", CL_SequenceClick_f, NULL);
	Cmd_AddCommand("seq_start", CL_SequenceStart_f, NULL);
	Cmd_AddCommand("seq_end", CL_SequenceEnd_f, NULL);

	/* forward to server commands
	 * the only thing this does is allow command completion
	 * to work -- all unknown commands are automatically
	 * forwarded to the server */
	Cmd_AddCommand("say", NULL, NULL);
	Cmd_AddCommand("say_team", NULL, NULL);
	Cmd_AddCommand("info", NULL, NULL);
	Cmd_AddCommand("playerlist", NULL, NULL);
	Cmd_AddCommand("players", NULL, NULL);
#ifdef DEBUG
	Cmd_AddCommand("debug_aircraftsamplelist", AIR_ListAircraftSamples_f, "Show aircraft parameter on game console");
	Cmd_AddCommand("debug_configstrings", CL_ShowConfigstrings_f, "Print configstrings to game console");
	Cmd_AddCommand("debug_gddump", CL_DumpGlobalDataToFile_f, "Dumps gd to a file");
	Cmd_AddCommand("debug_gdstats", CL_GlobalDataSizes_f, "Show globalData_t sizes");
	Cmd_AddCommand("actorinvlist", NULL, "Shows the inventory list of all actors");
	Cmd_AddCommand("debug_listle", LE_List_f, "Shows a list of current know local entities with type and status");
	Cmd_AddCommand("killteam", NULL, NULL);
	Cmd_AddCommand("stunteam", NULL, NULL);
#endif
}

static void CL_SendCmd (void)
{
	/* send a userinfo update if needed */
	if (cls.state >= ca_connected) {
		if (userinfo_modified) {
			struct dbuffer *msg = new_dbuffer();
			NET_WriteByte(msg, clc_userinfo);
			NET_WriteString(msg, Cvar_Userinfo());
			NET_WriteMsg(cls.stream, msg);
			userinfo_modified = qfalse;
		}
	}
}


/**
 * @sa CL_Frame
 */
static void CL_SendCommand (void)
{
	/* get new key events */
	IN_SendKeyEvents();

	/* process console commands */
	Cbuf_Execute();

	/* send intentions now */
	CL_SendCmd();

	/* fix any cheating cvars */
	Cvar_FixCheatVars();

	/* if the local server is running and we aren't connected then connect */
	switch (cls.state) {
	case ca_disconnected:
		if (Com_ServerState()) {
			cls.servername[0] = '\0';
			CL_SetClientState(ca_connecting);
			userinfo_modified = qfalse;
			return;
		}
		break;
	case ca_connecting:
		if (cls.realtime - cls.connectTime > cl_connecttimeout->integer) {
			Com_Error(ERR_DROP, "Server is not reachable");
		}
		break;
	case ca_connected:
		if (cls.waitingForStart) {
			if (cls.realtime - cls.waitingForStart > cl_connecttimeout->integer) {
				Com_Error(ERR_DROP, "Server is not reachable");
			} else {
				Com_sprintf(cls.loadingMessages, sizeof(cls.loadingMessages),
					"%s (%i)", _("Awaiting game start"), (cls.realtime - cls.waitingForStart) / 1000);
				SCR_UpdateScreen();
			}
		}
		break;
	default:
		break;
	}
}

/**
 * @brief Translate the difficulty int to a translated string
 * @param difficulty the difficulty integer value
 */
const char* CL_ToDifficultyName (int difficulty)
{
	switch (difficulty) {
	case -4:
		return _("Chicken-hearted");
	case -3:
		return _("Very Easy");
	case -2:
	case -1:
		return _("Easy");
	case 0:
		return _("Normal");
	case 1:
	case 2:
		return _("Hard");
	case 3:
		return _("Very Hard");
	case 4:
		return _("Insane");
	default:
		Sys_Error("Unknown difficulty id %i\n", difficulty);
		break;
	}
	return NULL;
}

static void CL_CvarCheck (void)
{
	int v;

	/* worldlevel */
	if (cl_worldlevel->modified) {
		int i;
		if (cl_worldlevel->integer < 0) {
			CL_Drop();
			Com_DPrintf(DEBUG_CLIENT, "CL_CvarCheck: Called drop - something went wrong\n");
			return;
		}

		if (cl_worldlevel->integer >= map_maxlevel - 1)
			Cvar_SetValue("cl_worldlevel", map_maxlevel - 1);
		else if (cl_worldlevel->integer < 0)
			Cvar_SetValue("cl_worldlevel", 0);
		for (i = 0; i < map_maxlevel; i++)
			Cbuf_AddText(va("deselfloor%i\n", i));
		for (; i < 8; i++)
			Cbuf_AddText(va("disfloor%i\n", i));
		Cbuf_AddText(va("selfloor%i\n", cl_worldlevel->integer));
		cl_worldlevel->modified = qfalse;
	}

	/* language */
	if (s_language->modified)
		CL_LanguageTryToSet(s_language->string);

	if (mn_inputlength->modified) {
		if (mn_inputlength->integer >= MAX_CVAR_EDITING_LENGTH)
			Cvar_SetValue("mn_inputlength", MAX_CVAR_EDITING_LENGTH);
	}

	/* r_mode and fullscreen */
	v = Cvar_VariableInteger("mn_vidmode");
	if (v < -1 || v >= VID_GetModeNums()) {
		Com_Printf("Max vid_mode value is %i (%i)\n", VID_GetModeNums(), v);
		v = Cvar_VariableInteger("vid_mode");
		Cvar_SetValue("mn_vidmode", v);
	}
	if (v >= 0)
		Cvar_Set("mn_vidmodestr", va("%i*%i", vid_modes[v].width, vid_modes[v].height));
	else
		Cvar_Set("mn_vidmodestr", _("Custom"));
}

/**
 * @brief Sets the client state
 */
void CL_SetClientState (int state)
{
	Com_DPrintf(DEBUG_CLIENT, "CL_SetClientState: Set new state to %i (old was: %i)\n", state, cls.state);
	cls.state = state;

	switch (cls.state) {
	case ca_uninitialized:
		Sys_Error("CL_SetClientState: Don't set state ca_uninitialized\n");
		break;
	case ca_ptledit:
	case ca_sequence:
		cl.refresh_prepped = qtrue;
		break;
	case ca_active:
		cls.waitingForStart = 0;
		break;
	case ca_connecting:
		Com_Printf("Connecting to %s...\n", *cls.servername ? cls.servername : "localhost");
		CL_Connect();
		break;
	case ca_disconnected:
		cls.waitingForStart = 0;
	case ca_connected:
		break;
	default:
		break;
	}
}

/**
 * @sa Qcommon_Frame
 */
void CL_Frame (int now, void *data)
{
	static int last_frame = 0;
	int delta;

	if (sv_maxclients->modified) {
		if (sv_maxclients->integer > 1 && ccs.singleplayer) {
			CL_StartSingleplayer(qfalse);
		} else if (sv_maxclients->integer == 1) {
			CL_StartSingleplayer(qtrue);
		}
		sv_maxclients->modified = qfalse;
	}

	if (sys_priority->modified || sys_affinity->modified)
		Sys_SetAffinityAndPriority();

	/* decide the simulation time */
	delta = now - last_frame;
	if (last_frame)
		cls.frametime = delta / 1000.0;
	else
		cls.frametime = 0;
	cls.realtime = Sys_Milliseconds();
	cl.time = now;
	last_frame = now;
	if (!blockEvents)
		cl.eventTime += delta;

	/* frame rate calculation */
	if (delta)
		cls.framerate = 1000.0 / delta;

	if (cls.state == ca_connected) {
		/* we run full speed when connecting */
		CL_RunHTTPDownloads();
	}

	/* fetch results from server */
	CL_ReadPackets();

	CL_SendCommand();

	IN_Frame();

	/* update camera position */
	CL_CameraMove();

	/* end the rounds when no soldier is alive */
	CL_RunMapParticles();
	CL_ParticleRun();

	/* update the screen */
	SCR_UpdateScreen();

	/* advance local effects for next frame */
	CL_RunLightStyles();
	SCR_RunConsole();

	/* LE updates */
	LE_Think();

	/* sound frame */
	S_Frame();

	/* send a new command message to the server */
	CL_SendCommand();
}

/**
 * @sa CL_Frame
 */
void CL_SlowFrame (int now, void *data)
{
	CL_CvarCheck();

	CL_ActorUpdateCVars();
}


/**
 * @brief Gettext init function
 *
 * Initialize the locale settings for gettext
 * po files are searched in ./base/i18n
 * You can override the language-settings in setting
 * the cvar s_language to a valid language-string like
 * e.g. de_DE, en or en_US
 *
 * @sa CL_Init
 */
static qboolean CL_LocaleSet (void)
{
	char *locale;

#ifdef _WIN32
	Q_putenv("LANG", s_language->string);
	Q_putenv("LANGUAGE", s_language->string);
#else /* _WIN32 */
# ifndef __sun
	unsetenv("LANGUAGE");
# endif /* __sun */
# ifdef __APPLE__
	if (*s_language->string && Q_putenv("LANGUAGE", s_language->string) == -1)
		Com_Printf("...setenv for LANGUAGE failed: %s\n", s_language->string);
	if (*s_language->string && Q_putenv("LC_ALL", s_language->string) == -1)
		Com_Printf("...setenv for LC_ALL failed: %s\n", s_language->string);
# endif /* __APPLE__ */
#endif /* _WIN32 */

	/* set to system default */
	setlocale(LC_ALL, "C");
	locale = setlocale(LC_MESSAGES, s_language->string);
	if (!locale) {
		Com_DPrintf(DEBUG_CLIENT, "...could not set to language: %s\n", s_language->string);
		locale = setlocale(LC_MESSAGES, "");
		if (!locale) {
			Com_DPrintf(DEBUG_CLIENT, "...could not set to system language\n");
			return qfalse;
		}
	} else {
		Com_Printf("...using language: %s\n", locale);
		Cvar_Set("s_language", locale);
		s_language->modified = qfalse;
	}
	return qtrue;
}

/**
 * @sa CL_Shutdown
 * @sa CL_InitAfter
 */
void CL_Init (void)
{
	/* i18n through gettext */
	char languagePath[MAX_OSPATH];
	cvar_t *fs_i18ndir;

	if (sv_dedicated->integer)
		return;					/* nothing running on the client */

	fs_i18ndir = Cvar_Get("fs_i18ndir", "", 0, "System path to language files");
	/* i18n through gettext */
	setlocale(LC_ALL, "C");
	setlocale(LC_MESSAGES, "");
	/* use system locale dir if we can't find in gamedir */
	if (*fs_i18ndir->string)
		Q_strncpyz(languagePath, fs_i18ndir->string, sizeof(languagePath));
	else
		Com_sprintf(languagePath, sizeof(languagePath), "%s/"BASEDIRNAME"/i18n/", FS_GetCwd());
	Com_DPrintf(DEBUG_CLIENT, "...using mo files from %s\n", languagePath);
	bindtextdomain(TEXT_DOMAIN, languagePath);
	bind_textdomain_codeset(TEXT_DOMAIN, "UTF-8");
	/* load language file */
	textdomain(TEXT_DOMAIN);

	CL_LocaleSet();

	cl_localPool = Mem_CreatePool("Client: Local (per game)");
	cl_genericPool = Mem_CreatePool("Client: Generic");
	cl_menuSysPool = Mem_CreatePool("Client: Menu");
	cl_soundSysPool = Mem_CreatePool("Client: Sound system");
	cl_ircSysPool = Mem_CreatePool("Client: IRC system");

	/* all archived variables will now be loaded */
	Con_Init();

	CIN_Init();

	VID_Init();
	S_Init();

	V_Init();

	SCR_Init();
	cls.loadingPercent = 0.0f;
	SCR_DrawPrecacheScreen(qfalse);

	CL_InitLocal();

	memset(&teamData, 0, sizeof(teamData_t));
	/* Default to single-player mode */
	ccs.singleplayer = qtrue;

	CL_InitParticles();

	CL_ClearState();

	/* Touch memory */
	Mem_TouchGlobal();
}


/**
 * @brief Saves configuration file and shuts the client systems down
 * FIXME: this is a callback from Sys_Quit and Com_Error.  It would be better
 * to run quit through here before the final handoff to the sys code.
 * @sa Sys_Quit
 * @sa CL_Init
 */
void CL_Shutdown (void)
{
	static qboolean isdown = qfalse;

	if (isdown) {
		printf("recursive shutdown\n");
		return;
	}
	isdown = qtrue;

	CL_HTTP_Cleanup();
	Irc_Shutdown();
	CL_WriteConfiguration();
	Con_SaveConsoleHistory(FS_Gamedir());
	S_Shutdown();
	R_Shutdown();
	MN_Shutdown();
	CIN_Shutdown();
	FS_Shutdown();
}
