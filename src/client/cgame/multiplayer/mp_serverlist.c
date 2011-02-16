/**
 * @file mp_serverlist.c
 * @brief Serverlist management for multiplayer
 */

/*
Copyright (C) 2002-2010 UFO: Alien Invasion.

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

#include "../../cl_shared.h"
#include "../cl_game.h"
#include "../../ui/ui_main.h"
#include "../../ui/ui_popup.h"
#include "../../../shared/infostring.h"
#include "../../../shared/parse.h"
#include "mp_serverlist.h"
#include "mp_callbacks.h"
#include "../../../shared/mutex.h"

#define MAX_SERVERLIST 128

static serverList_t serverList[MAX_SERVERLIST];
serverList_t *selectedServer;
static char serverText[1024];
static int serverListLength;
static int serverListPos;
static cvar_t *cl_serverlist;

/**
 * @brief Parsed the server ping response.
 * @param[out] server The server data to store the parsed information in
 * @param[in] msg The ping response with the server information to parse
 * @sa CL_PingServerCallback
 * @sa SVC_Info
 * @return @c true if the server is compatible, @c msg is not @c null and the server
 * wasn't pinged already, @c false otherwise
 */
static qboolean CL_ProcessPingReply (serverList_t *server, const char *msg)
{
	if (!msg)
		return qfalse;

	if (PROTOCOL_VERSION != Info_IntegerForKey(msg, "sv_protocol")) {
		Com_DPrintf(DEBUG_CLIENT, "CL_ProcessPingReply: Protocol mismatch\n");
		return qfalse;
	}
	if (!Q_streq(UFO_VERSION, Info_ValueForKey(msg, "sv_version"))) {
		Com_DPrintf(DEBUG_CLIENT, "CL_ProcessPingReply: Version mismatch\n");
	}

	if (server->pinged)
		return qfalse;

	server->pinged = qtrue;
	Q_strncpyz(server->sv_hostname, Info_ValueForKey(msg, "sv_hostname"),
		sizeof(server->sv_hostname));
	Q_strncpyz(server->version, Info_ValueForKey(msg, "sv_version"),
		sizeof(server->version));
	Q_strncpyz(server->mapname, Info_ValueForKey(msg, "sv_mapname"),
		sizeof(server->mapname));
	Q_strncpyz(server->gametype, Info_ValueForKey(msg, "sv_gametype"),
		sizeof(server->gametype));
	server->clients = Info_IntegerForKey(msg, "clients");
	server->sv_dedicated = Info_IntegerForKey(msg, "sv_dedicated");
	server->sv_maxclients = Info_IntegerForKey(msg, "sv_maxclients");
	return qtrue;
}

typedef enum {
	SERVERLIST_SHOWALL,
	SERVERLIST_HIDEFULL,
	SERVERLIST_HIDEEMPTY
} serverListStatus_t;

/**
 * @brief Perform the server filtering
 * @param[in] server The server data
 * @return @c true if the server should be visible for the current filter settings, @c false otherwise
 */
static inline qboolean CL_ShowServer (const serverList_t *server)
{
	if (cl_serverlist->integer == SERVERLIST_SHOWALL)
		return qtrue;
	if (cl_serverlist->integer == SERVERLIST_HIDEFULL && server->clients < server->sv_maxclients)
		return qtrue;
	if (cl_serverlist->integer == SERVERLIST_HIDEEMPTY && server->clients > 0)
		return qtrue;

	return qfalse;
}

/**
 * @brief CL_PingServer
 */
static void CL_PingServerCallback (struct net_stream *s)
{
	struct dbuffer *buf = NET_ReadMsg(s);
	serverList_t *server = (serverList_t *)NET_StreamGetData(s);
	const int cmd = NET_ReadByte(buf);
	char str[512];

	NET_ReadStringLine(buf, str, sizeof(str));

	if (cmd == clc_oob && strncmp(str, "info", 4) == 0) {
		NET_ReadString(buf, str, sizeof(str));
		if (CL_ProcessPingReply(server, str)) {
			if (CL_ShowServer(server)) {
				char string[MAX_INFO_STRING];
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
		}
	}
	NET_StreamFree(s);
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
		NET_StreamSetData(s, server);
		NET_StreamSetCallback(s, &CL_PingServerCallback);
	} else {
		Com_Printf("pinging failed [%s]:%s...\n", server->node, server->service);
	}
}

/**
 * @brief Prints all the servers on the list to game console
 */
void CL_PrintServerList_f (void)
{
	int i;

	Com_Printf("%i servers on the list\n", serverListLength);

	for (i = 0; i < serverListLength; i++) {
		Com_Printf("%02i: [%s]:%s (pinged: %i)\n", i, serverList[i].node, serverList[i].service, serverList[i].pinged);
	}
}

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
		if (Q_streq(serverList[i].node, node) && Q_streq(serverList[i].service, service))
			return;

	OBJZERO(serverList[serverListLength]);
	serverList[serverListLength].node = Mem_PoolStrDup(node, cl_genericPool, 0);
	serverList[serverListLength].service = Mem_PoolStrDup(service, cl_genericPool, 0);
	CL_PingServer(&serverList[serverListLength]);
	serverListLength++;
}

/**
 * @brief Team selection text
 *
 * This function fills the multiplayer_selectteam menu with content
 * @sa NET_OOB_Printf
 * @sa SVC_TeamInfo
 * @sa CL_SelectTeam_Init_f
 */
void CL_ParseTeamInfoMessage (struct dbuffer *msg)
{
	char str[4096];
	int cnt = 0;
	linkedList_t *userList = NULL;
	linkedList_t *userTeam = NULL;

	if (NET_ReadString(msg, str, sizeof(str)) == 0) {
		UI_ResetData(TEXT_MULTIPLAYER_USERLIST);
		UI_ResetData(TEXT_MULTIPLAYER_USERTEAM);
		UI_ExecuteConfunc("multiplayer_playerNumber 0");
		Com_DPrintf(DEBUG_CLIENT, "CL_ParseTeamInfoMessage: No teaminfo string\n");
		return;
	}

	OBJZERO(teamData);

	teamData.maxteams = Info_IntegerForKey(str, "sv_maxteams");
	teamData.maxPlayersPerTeam = Info_IntegerForKey(str, "sv_maxplayersperteam");

	/* for each lines */
	while (NET_ReadString(msg, str, sizeof(str)) > 0) {
		const int team = Info_IntegerForKey(str, "cl_team");
		const int isReady = Info_IntegerForKey(str, "cl_ready");
		const char *user = Info_ValueForKey(str, "cl_name");

		if (team > 0 && team < MAX_TEAMS)
			teamData.teamCount[team]++;

		/* store data */
		LIST_AddString(&userList, user);
		if (team != TEAM_NO_ACTIVE)
			LIST_AddString(&userTeam, va(_("Team %d"), team));
		else
			LIST_AddString(&userTeam, _("No team"));

		UI_ExecuteConfunc("multiplayer_playerIsReady %i %i", cnt, isReady);

		cnt++;
	}

	UI_RegisterLinkedListText(TEXT_MULTIPLAYER_USERLIST, userList);
	UI_RegisterLinkedListText(TEXT_MULTIPLAYER_USERTEAM, userTeam);
	UI_ExecuteConfunc("multiplayer_playerNumber %i", cnt);

	/* no players are connected ATM */
	if (!cnt) {
		/** @todo warning must not be into the player list.
		 * if we see this we are the first player that would be connected to the server */
		/* Q_strcat(teamData.teamInfoText, _("No player connected\n"), sizeof(teamData.teamInfoText)); */
	}

	Cvar_SetValue("mn_maxteams", teamData.maxteams);
	Cvar_SetValue("mn_maxplayersperteam", teamData.maxPlayersPerTeam);
}

static char serverInfoText[1024];
static char userInfoText[256];
/**
 * @brief Serverbrowser text
 * @sa CL_PingServer
 * @sa CL_PingServers_f
 * @note This function fills the network browser server information with text
 * @sa NET_OOB_Printf
 * @sa CL_ServerInfoCallback
 * @sa SVC_Info
 */
void CL_ParseServerInfoMessage (struct dbuffer *msg, const char *hostname)
{
	const char *value;
	const char *token;
	char str[MAX_INFO_STRING];
	char buf[256];

	NET_ReadString(msg, str, sizeof(str));

	/* check for server status response message */
	value = Info_ValueForKey(str, "sv_dedicated");
	if (*value) {
		/* server info cvars and users are seperated via newline */
		const char *users = strstr(str, "\n");
		if (!users) {
			Com_Printf(S_COLOR_GREEN "%s\n", str);
			return;
		}
		Com_DPrintf(DEBUG_CLIENT, "%s\n", str); /* status string */

		Cvar_Set("mn_mappic", "maps/shots/default.jpg");
		if (*Info_ValueForKey(str, "sv_needpass") == '1')
			Cvar_Set("mn_server_need_password", "1");
		else
			Cvar_Set("mn_server_need_password", "0");

		Com_sprintf(serverInfoText, sizeof(serverInfoText), _("IP\t%s\n\n"), hostname);
		Cvar_Set("mn_server_ip", hostname);
		value = Info_ValueForKey(str, "sv_mapname");
		assert(value);
		Cvar_Set("mn_svmapname", value);
		Q_strncpyz(buf, value, sizeof(buf));
		token = buf;
		/* skip random map char */
		if (token[0] == '+')
			token++;

		Com_sprintf(serverInfoText + strlen(serverInfoText), sizeof(serverInfoText) - strlen(serverInfoText), _("Map:\t%s\n"), value);
		if (FS_CheckFile("pics/maps/shots/%s.jpg", token) != -1) {
			/* store it relative to pics/ dir - not relative to game dir */
			Cvar_Set("mn_mappic", va("maps/shots/%s", token));
		}
		Com_sprintf(serverInfoText + strlen(serverInfoText), sizeof(serverInfoText) - strlen(serverInfoText), _("Servername:\t%s\n"), Info_ValueForKey(str, "sv_hostname"));
		Com_sprintf(serverInfoText + strlen(serverInfoText), sizeof(serverInfoText) - strlen(serverInfoText), _("Moralestates:\t%s\n"), _(Info_BoolForKey(str, "sv_enablemorale")));
		Com_sprintf(serverInfoText + strlen(serverInfoText), sizeof(serverInfoText) - strlen(serverInfoText), _("Gametype:\t%s\n"), Info_ValueForKey(str, "sv_gametype"));
		Com_sprintf(serverInfoText + strlen(serverInfoText), sizeof(serverInfoText) - strlen(serverInfoText), _("Gameversion:\t%s\n"), Info_ValueForKey(str, "ver"));
		Com_sprintf(serverInfoText + strlen(serverInfoText), sizeof(serverInfoText) - strlen(serverInfoText), _("Dedicated server:\t%s\n"), _(Info_BoolForKey(str, "sv_dedicated")));
		Com_sprintf(serverInfoText + strlen(serverInfoText), sizeof(serverInfoText) - strlen(serverInfoText), _("Operating system:\t%s\n"), Info_ValueForKey(str, "sys_os"));
		Com_sprintf(serverInfoText + strlen(serverInfoText), sizeof(serverInfoText) - strlen(serverInfoText), _("Network protocol:\t%s\n"), Info_ValueForKey(str, "sv_protocol"));
		Com_sprintf(serverInfoText + strlen(serverInfoText), sizeof(serverInfoText) - strlen(serverInfoText), _("Roundtime:\t%s\n"), Info_ValueForKey(str, "sv_roundtimelimit"));
		Com_sprintf(serverInfoText + strlen(serverInfoText), sizeof(serverInfoText) - strlen(serverInfoText), _("Teamplay:\t%s\n"), _(Info_BoolForKey(str, "sv_teamplay")));
		Com_sprintf(serverInfoText + strlen(serverInfoText), sizeof(serverInfoText) - strlen(serverInfoText), _("Max. players per team:\t%s\n"), Info_ValueForKey(str, "sv_maxplayersperteam"));
		Com_sprintf(serverInfoText + strlen(serverInfoText), sizeof(serverInfoText) - strlen(serverInfoText), _("Max. teams allowed in this map:\t%s\n"), Info_ValueForKey(str, "sv_maxteams"));
		Com_sprintf(serverInfoText + strlen(serverInfoText), sizeof(serverInfoText) - strlen(serverInfoText), _("Max. clients:\t%s\n"), Info_ValueForKey(str, "sv_maxclients"));
		Com_sprintf(serverInfoText + strlen(serverInfoText), sizeof(serverInfoText) - strlen(serverInfoText), _("Max. soldiers per player:\t%s\n"), Info_ValueForKey(str, "sv_maxsoldiersperplayer"));
		Com_sprintf(serverInfoText + strlen(serverInfoText), sizeof(serverInfoText) - strlen(serverInfoText), _("Max. soldiers per team:\t%s\n"), Info_ValueForKey(str, "sv_maxsoldiersperteam"));
		Com_sprintf(serverInfoText + strlen(serverInfoText), sizeof(serverInfoText) - strlen(serverInfoText), _("Password protected:\t%s\n"), _(Info_BoolForKey(str, "sv_needpass")));
		UI_RegisterText(TEXT_STANDARD, serverInfoText);
		userInfoText[0] = '\0';
		do {
			int team;
			token = Com_Parse(&users);
			if (!users)
				break;
			team = atoi(token);
			token = Com_Parse(&users);
			if (!users)
				break;
			Com_sprintf(userInfoText + strlen(userInfoText), sizeof(userInfoText) - strlen(userInfoText), "%s\t%i\n", token, team);
		} while (1);
		UI_RegisterText(TEXT_LIST, userInfoText);
		UI_PushWindow("serverinfo", NULL, NULL);
	} else
		Com_Printf(S_COLOR_GREEN "%s", str);
}

/**
 * @sa CL_ServerInfo_f
 * @sa CL_ParseServerInfoMessage
 */
static void CL_ServerInfoCallback (struct net_stream *s)
{
	struct dbuffer *buf = NET_ReadMsg(s);
	if (buf) {
		const int cmd = NET_ReadByte(buf);
		char str[8];
		NET_ReadStringLine(buf, str, sizeof(str));

		if (cmd == clc_oob && Q_streq(str, "print")) {
			char hostname[256];
			NET_StreamPeerToName(s, hostname, sizeof(hostname), qtrue);
			CL_ParseServerInfoMessage(buf, hostname);
		}
	}
	NET_StreamFree(s);
}

static SDL_Thread *masterServerQueryThread;

static int CL_QueryMasterServerThread (void *data)
{
	char *responseBuf;
	const char *serverListBuf;
	const char *token;
	char node[MAX_VAR], service[MAX_VAR];
	int i, num;

	responseBuf = HTTP_GetURL(va("%s/ufo/masterserver.php?query", masterserver_url->string));
	if (!responseBuf) {
		Com_Printf("Could not query masterserver\n");
		return 1;
	}

	serverListBuf = responseBuf;

	Com_DPrintf(DEBUG_CLIENT, "masterserver response: %s\n", serverListBuf);
	token = Com_Parse(&serverListBuf);

	num = atoi(token);
	if (num >= MAX_SERVERLIST) {
		Com_DPrintf(DEBUG_CLIENT, "Too many servers: %i\n", num);
		num = MAX_SERVERLIST;
	}
	for (i = 0; i < num; i++) {
		/* host */
		token = Com_Parse(&serverListBuf);
		if (!*token || !serverListBuf) {
			Com_Printf("Could not finish the masterserver response parsing\n");
			break;
		}
		Q_strncpyz(node, token, sizeof(node));
		/* port */
		token = Com_Parse(&serverListBuf);
		if (!*token || !serverListBuf) {
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
 * @sa CL_PingServers_f
 */
static void CL_QueryMasterServer (void)
{
	if (masterServerQueryThread != NULL)
		SDL_WaitThread(masterServerQueryThread, NULL);

	masterServerQueryThread = SDL_CreateThread(CL_QueryMasterServerThread, NULL);
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
		NET_SockaddrToStrings(s, from, node, sizeof(node), service, sizeof(service));
		CL_AddServerToList(node, service);
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
	const char *newBookmark;

	if (Cmd_Argc() < 2) {
		newBookmark = Cvar_GetString("mn_server_ip");
		if (!newBookmark) {
			Com_Printf("Usage: %s <ip>\n", Cmd_Argv(0));
			return;
		}
	} else
		newBookmark = Cmd_Argv(1);

	for (i = 0; i < MAX_BOOKMARKS; i++) {
		const char *bookmark = Cvar_GetString(va("adr%i", i));
		if (bookmark[0] == '\0') {
			Cvar_Set(va("adr%i", i), newBookmark);
			return;
		}
	}
	/* bookmarks are full - overwrite the first entry */
	UI_Popup(_("Notice"), _("All bookmark slots are used - please removed unused entries and repeat this step"));
}

/**
 * @sa CL_ServerInfoCallback
 */
static void CL_ServerInfo_f (void)
{
	struct net_stream *s;
	const char *host;
	const char *port;

	switch (Cmd_Argc()) {
	case 2:
		host = Cmd_Argv(1);
		port = DOUBLEQUOTE(PORT_SERVER);
		break;
	case 3:
		host = Cmd_Argv(1);
		port = Cmd_Argv(2);
		break;
	default:
		if (selectedServer) {
			host = selectedServer->node;
			port = selectedServer->service;
		} else {
			host = Cvar_GetString("mn_server_ip");
			port = DOUBLEQUOTE(PORT_SERVER);
		}
		break;
	}
	s = NET_Connect(host, port);
	if (s) {
		NET_OOB_Printf(s, "status %i", PROTOCOL_VERSION);
		NET_StreamSetCallback(s, &CL_ServerInfoCallback);
	} else
		Com_Printf("Could not connect to %s %s\n", host, port);
}

/**
 * @brief Callback for bookmark nodes in multiplayer menu (mp_bookmarks)
 * @sa CL_ParseServerInfoMessage
 */
static void CL_ServerListClick_f (void)
{
	int num;

	if (Cmd_Argc() < 2) {
		Com_Printf("Usage: %s <num>\n", Cmd_Argv(0));
		return;
	}
	num = atoi(Cmd_Argv(1));

	UI_RegisterText(TEXT_STANDARD, serverInfoText);
	if (num >= 0 && num < serverListLength) {
		int i;
		for (i = 0; i < serverListLength; i++)
			if (serverList[i].pinged && serverList[i].serverListPos == num) {
				/* found the server - grab the infos for this server */
				selectedServer = &serverList[i];
				Cbuf_AddText(va("server_info %s %s;", serverList[i].node, serverList[i].service));
				return;
			}
	}
}

/** this is true if pingservers was already executed */
static qboolean serversAlreadyQueried = qfalse;
static int lastServerQuery = 0;
/** ms until the server query timed out */
#define SERVERQUERYTIMEOUT 40000

/**
 * @brief The first function called when entering the multiplayer menu, then CL_Frame takes over
 * @sa CL_ParseServerInfoMessage
 * @note Use a parameter for pingservers to update the current serverlist
 */
void CL_PingServers_f (void)
{
	selectedServer = NULL;

	/* refresh the list */
	if (Cmd_Argc() == 2) {
		int i;
		/* reset current list */
		serverText[0] = 0;
		serversAlreadyQueried = qfalse;
		for (i = 0; i < serverListLength; i++) {
			Mem_Free(serverList[i].node);
			Mem_Free(serverList[i].service);
		}
		serverListPos = 0;
		serverListLength = 0;
		OBJZERO(serverList);
	} else {
		UI_RegisterText(TEXT_LIST, serverText);
		return;
	}

	if (!cls.netDatagramSocket)
		cls.netDatagramSocket = NET_DatagramSocketNew(NULL, DOUBLEQUOTE(PORT_CLIENT), &CL_ServerListDiscoveryCallback);

	/* broadcast search for all the servers int the local network */
	if (cls.netDatagramSocket) {
		const char buf[] = "discover";
		NET_DatagramBroadcast(cls.netDatagramSocket, buf, sizeof(buf), PORT_SERVER);
	}
	UI_RegisterText(TEXT_LIST, serverText);

	/* don't query the masterservers with every call */
	if (serversAlreadyQueried) {
		if (lastServerQuery + SERVERQUERYTIMEOUT > CL_Milliseconds())
			return;
	} else
		serversAlreadyQueried = qtrue;

	lastServerQuery = CL_Milliseconds();

	/* query master server? */
	if (Cmd_Argc() == 2 && !Q_streq(Cmd_Argv(1), "local")) {
		Com_DPrintf(DEBUG_CLIENT, "Query masterserver\n");
		CL_QueryMasterServer();
	}
}

void MP_ServerListInit (void)
{
	int i;

	/* register our variables */
	for (i = 0; i < MAX_BOOKMARKS; i++)
		Cvar_Get(va("adr%i", i), "", CVAR_ARCHIVE, "Bookmark for network ip");
	cl_serverlist = Cvar_Get("cl_serverlist", "0", CVAR_ARCHIVE, "0=show all, 1=hide full - servers on the serverlist");

	Cmd_AddCommand("bookmark_add", CL_BookmarkAdd_f, "Add a new bookmark - see adrX cvars");
	Cmd_AddCommand("server_info", CL_ServerInfo_f, NULL);
	Cmd_AddCommand("serverlist", CL_PrintServerList_f, NULL);
	/* text id is servers in menu_multiplayer.ufo */
	Cmd_AddCommand("servers_click", CL_ServerListClick_f, NULL);
}

void MP_ServerListShutdown (void)
{
	Cmd_RemoveCommand("bookmark_add");
	Cmd_RemoveCommand("server_info");
	Cmd_RemoveCommand("serverlist");
	Cmd_RemoveCommand("servers_click");

	if (masterServerQueryThread)
		SDL_KillThread(masterServerQueryThread);
	masterServerQueryThread = NULL;
}
