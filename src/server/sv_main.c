/**
 * @file sv_main.c
 * @brief Main server code?
 */

/*
All original materal Copyright (C) 2002-2007 UFO: Alien Invasion team.

Original file from Quake 2 v3.21: quake2-2.31/server/sv_main.c
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

#include "server.h"

client_t *sv_client;			/* current client */

cvar_t *rcon_password;			/* password for remote server commands */

static cvar_t *sv_downloadserver;

cvar_t *sv_maxclients = NULL;
cvar_t *sv_enablemorale;
cvar_t *sv_maxsoldiersperteam;
cvar_t *sv_maxsoldiersperplayer;

cvar_t *sv_hostname;
cvar_t *sv_public;			/**< should heartbeats be sent */
cvar_t *sv_mapname;

static cvar_t *sv_reconnect_limit;		/**< minimum seconds between connect messages */

void Master_Shutdown(void);

static qboolean abandon = qfalse;		/**< shutdown server when all clients disconnect and don't accept new connections */
static qboolean killserver = qfalse;	/**< will initiate shutdown once abandon is set */

mapcycle_t *mapcycleList;
int mapcycleCount;

struct memPool_s *sv_gameSysPool;
struct memPool_s *sv_genericPool;

/*============================================================================ */


/**
 * @brief Called when the player is totally leaving the server, either willingly
 * or unwillingly. This is NOT called if the entire server is quiting
 * or crashing.
 */
void SV_DropClient (client_t * drop, const char *message)
{
	/* add the disconnect */
	struct dbuffer *msg = new_dbuffer();
	NET_WriteByte(msg, svc_disconnect);
	NET_WriteString(msg, message);
	NET_WriteMsg(drop->stream, msg);
	SV_BroadcastPrintf(PRINT_CHAT, "%s was dropped from the server - reason: %s\n", drop->name, message);

	if (drop->state == cs_spawned || drop->state == cs_spawning) {
		/* call the prog function for removing a client */
		/* this will remove the body, among other things */
		ge->ClientDisconnect(drop->player);
	}

	stream_finished(drop->stream);
	drop->stream = NULL;

	drop->player->inuse = qfalse;
	SV_SetClientState(drop, cs_free);
	drop->name[0] = 0;

	if (abandon) {
		int count = 0;
		int i;
		for (i = 0; i < sv_maxclients->integer; i++)
			if (svs.clients[i].state >= cs_connected)
				count++;
		if (count == 0)
			killserver = qtrue;
	}
}

/*
==============================================================================
CONNECTIONLESS COMMANDS
==============================================================================
*/

/**
 * @brief Responds with teaminfo such as free team num
 */
static void SVC_TeamInfo (struct net_stream *s)
{
	char player[1024];
	int i;
	client_t *cl;
	struct dbuffer *msg = new_dbuffer();
	NET_WriteByte(msg, clc_oob);
	NET_WriteRawString(msg, "teaminfo\n");

	NET_WriteRawString(msg, Cvar_VariableString("sv_teamplay"));
	NET_WriteRawString(msg, "\n");
	NET_WriteRawString(msg, Cvar_VariableString("sv_maxteams"));
	NET_WriteRawString(msg, "\n");
	NET_WriteRawString(msg, Cvar_VariableString("sv_maxplayersperteam"));
	NET_WriteRawString(msg, "\n");
	for (i = 0, cl = svs.clients; i < sv_maxclients->integer; i++, cl++) {
		if (cl->state >= cs_connected) {
			Com_DPrintf(DEBUG_SERVER, "SV_TeamInfoString: connected client: %i %s\n", i, cl->name);
			/* show players that already have a team with their teamnum */
			if (ge->ClientGetTeamNum(cl->player))
				Com_sprintf(player, sizeof(player), "%i\t\"%s\"\n", ge->ClientGetTeamNum(cl->player), cl->name);
			else if (ge->ClientGetTeamNumPref(cl->player))
				Com_sprintf(player, sizeof(player), "%i\t\"%s\"\n", ge->ClientGetTeamNumPref(cl->player), cl->name);
			else
				Com_sprintf(player, sizeof(player), "-\t\"%s\"\n", cl->name);
			NET_WriteRawString(msg, player);
		} else {
			Com_DPrintf(DEBUG_SERVER, "SV_TeamInfoString: unconnected client: %i %s\n", i, cl->name);
		}
	}

	NET_WriteByte(msg, 0);

	NET_WriteMsg(s, msg);
}

/**
 * @brief Responds with all the info that qplug or qspy can see
 * @sa SV_StatusString
 */
static void SVC_Status (struct net_stream *s)
{
	char player[1024];
	int i;
	client_t *cl;
	struct dbuffer *msg = new_dbuffer();
	NET_WriteByte(msg, clc_oob);
	NET_WriteRawString(msg, "print\n");

	NET_WriteRawString(msg, Cvar_Serverinfo());
	NET_WriteRawString(msg, "\n");

	for (i = 0; i < sv_maxclients->integer; i++) {
		cl = &svs.clients[i];
		if (cl->state > cs_free) {
			Com_sprintf(player, sizeof(player), "%i \"%s\"\n", ge->ClientGetTeamNum(cl->player), cl->name);
			NET_WriteRawString(msg, player);
		}
	}

	NET_WriteMsg(s, msg);
}

/**
 * @brief Responds with short info for broadcast scans
 * @note The second parameter should be the current protocol version number.
 * @note Only a short server description - the user can determine whether he is
 * interested in a full status
 * @sa CL_ParseStatusMessage
 */
static void SVC_Info (struct net_stream *s)
{
	char string[64];
	int i, count;
	int version;
	char infostring[MAX_INFO_STRING];

	if (sv_maxclients->integer == 1) {
		Com_DPrintf(DEBUG_SERVER, "Ignore info string in singleplayer mode\n");
		return;	/* ignore in single player */
	}

	version = atoi(Cmd_Argv(1));

	if (version != PROTOCOL_VERSION)
		Com_sprintf(string, sizeof(string), "%s: wrong version (client: %i, host: %i)\n", sv_hostname->string, version, PROTOCOL_VERSION);
	else {
		count = 0;
		for (i = 0; i < sv_maxclients->integer; i++)
			if (svs.clients[i].state >= cs_spawning)
				count++;

		infostring[0] = '\0';

		Info_SetValueForKey(infostring, "protocol", va("%i", PROTOCOL_VERSION));
		Info_SetValueForKey(infostring, "sv_hostname", sv_hostname->string);
		Info_SetValueForKey(infostring, "sv_dedicated", sv_dedicated->string);
		Info_SetValueForKey(infostring, "gametype", gametype->string);
		Info_SetValueForKey(infostring, "mapname", sv.name);
		Info_SetValueForKey(infostring, "clients", va("%i", count));
		Info_SetValueForKey(infostring, "sv_maxclients", sv_maxclients->string);
		Info_SetValueForKey(infostring, "version", UFO_VERSION);
	}

	NET_OOB_Printf(s, "info\n%s", infostring);
}


/**
 * @brief A connection request that did not come from the master
 */
static void SVC_DirectConnect (struct net_stream *stream)
{
	char userinfo[MAX_INFO_STRING];
	int i;
	client_t *cl, *newcl;
	client_t temp;
	player_t *player;
	int playernum;
	int version;
	char buf[256];
	const char *peername = stream_peer_name(stream, buf, sizeof(buf), qtrue);

	Com_DPrintf(DEBUG_SERVER, "SVC_DirectConnect ()\n");

	version = atoi(Cmd_Argv(1));
	if (version != PROTOCOL_VERSION) {
		NET_OOB_Printf(stream, "print\nServer is version %s.\n", UFO_VERSION);
		Com_Printf("rejected connect from version %i - %s\n", version, peername);
		return;
	}

	strncpy(userinfo, Cmd_Argv(2), sizeof(userinfo) - 1);
	userinfo[sizeof(userinfo) - 1] = 0;

	if (!strlen(userinfo)) {  /* catch empty userinfo */
		Com_Printf("Empty userinfo from %s\n", peername);
		NET_OOB_Printf(stream, "print\nConnection refused.\n");
		return;
	}

	if (strchr(userinfo, '\xFF')) {  /* catch end of message in string exploit */
		Com_Printf("Illegal userinfo contained xFF from %s\n", peername);
		NET_OOB_Printf(stream, "print\nConnection refused.\n");
		return;
	}

	if (strlen(Info_ValueForKey(userinfo, "ip"))) {  /* catch spoofed ips  */
		Com_Printf("Illegal userinfo contained ip from %s\n", peername);
		NET_OOB_Printf(stream, "print\nConnection refused.\n");
		return;
	}

	/* force the IP key/value pair so the game can filter based on ip */
	Info_SetValueForKey(userinfo, "ip", peername);

	newcl = &temp;
	memset(newcl, 0, sizeof(client_t));

	/* if there is already a slot for this ip, reuse it */
	for (i = 0, cl = svs.clients; i < sv_maxclients->integer; i++, cl++) {
		if (cl->state == cs_free)
			continue;
		if (strcmp(peername, cl->peername) == 0) {
			if (!stream_is_loopback(stream) && (svs.realtime - cl->lastconnect) < (sv_reconnect_limit->integer * 1000)) {
				Com_Printf("%s:reconnect rejected : too soon\n", peername);
				return;
			}
			Com_Printf("%s:reconnect\n", peername);
			free_stream(cl->stream);
			newcl = cl;
			goto gotnewcl;
		}
	}

	/* find a client slot */
	newcl = NULL;
	for (i = 0, cl = svs.clients; i < sv_maxclients->integer; i++, cl++) {
		if (cl->state == cs_free) {
			newcl = cl;
			break;
		}
	}
	if (!newcl) {
		NET_OOB_Printf(stream, "print\nServer is full.\n");
		Com_Printf("Rejected a connection - server is full.\n");

		return;
	}

	/* @todo: Check if the teamnum preference has already reached maxsoldiers */
	/* and reject connection if so */

	gotnewcl:
	/* build a new connection */
	/* accept the new client */
	/* this is the only place a client_t is ever initialized */
	*newcl = temp;
	sv_client = newcl;
	playernum = newcl - svs.clients;
	player = PLAYER_NUM(playernum);
	newcl->player = player;
	newcl->player->num = playernum;

	/* get the game a chance to reject this connection or modify the userinfo */
	if (!(ge->ClientConnect(player, userinfo))) {
		if (*Info_ValueForKey(userinfo, "rejmsg"))
			NET_OOB_Printf(stream, "print\n%s\nConnection refused.\n", Info_ValueForKey(userinfo, "rejmsg"));
		else
			NET_OOB_Printf(stream, "print\nConnection refused.\n");
		Com_Printf("Game rejected a connection.\n");
		return;
	}

	/* new player */
	newcl->player->inuse = qtrue;

	/* parse some info from the info strings */
	strncpy(newcl->userinfo, userinfo, sizeof(newcl->userinfo) - 1);
	SV_UserinfoChanged(newcl);

	/* send the connect packet to the client */
	if (sv_downloadserver->string[0])
		NET_OOB_Printf(stream, "client_connect dlserver=%s", sv_downloadserver->string);
	else
		NET_OOB_Printf(stream, "client_connect");

	SV_SetClientState(newcl, cs_connected);

	newcl->lastconnect = svs.realtime;
	Q_strncpyz(newcl->peername, peername, sizeof(newcl->peername));
	newcl->stream = stream;
	set_stream_data(stream, newcl);
}

/**
 * @brief Checks whether the remote connection is allowed (rcon_password must be
 * set on the server) - and verify the user given password with the cvar value.
 */
static qboolean Rcon_Validate (void)
{
	if (!strlen(rcon_password->string))
		return qfalse;

	if (strcmp(Cmd_Argv(1), rcon_password->string))
		return qfalse;

	return qtrue;
}

/**
 * @brief A client issued an rcon command. Shift down the remaining args. Redirect all printfs
 */
static void SVC_RemoteCommand (struct net_stream *stream)
{
	int i;
	qboolean valid;
	char remaining[1024];
	char buf[256];
	const char *peername = stream_peer_name(stream, buf, sizeof(buf), qtrue);

	valid = Rcon_Validate();

	if (!valid)
		Com_Printf("Bad rcon from %s:\n%s\n", peername, Cmd_Argv(1));
	else
		Com_Printf("Rcon from %s:\n%s\n", peername, Cmd_Argv(1));

	if (!valid)
		/* inform the client */
		Com_Printf("Bad rcon_password.\n");
	else {
		/* execute the rcon commands */
		remaining[0] = 0;

		for (i = 2; i < Cmd_Argc(); i++) {
			Q_strcat(remaining, Cmd_Argv(i), sizeof(remaining));
			Q_strcat(remaining, " ", sizeof(remaining));
		}

		/* execute the string */
		Cmd_ExecuteString(remaining);
	}
}

static void SV_ConnectionlessPacket (struct net_stream *stream, struct dbuffer *msg)
{
	const char *s, *c;
	char buf[256];

	s = NET_ReadStringLine(msg);

	Cmd_TokenizeString(s, qfalse);

	c = Cmd_Argv(0);
	Com_DPrintf(DEBUG_SERVER, "Packet : %s\n", c);

	if (!strcmp(c, "teaminfo"))
		SVC_TeamInfo(stream);
	else if (!strcmp(c, "info"))
		SVC_Info(stream);
	else if (!strcmp(c, "status"))
		SVC_Status(stream);
	else if (!strcmp(c, "connect"))
		SVC_DirectConnect(stream);
	else if (!strcmp(c, "rcon"))
		SVC_RemoteCommand(stream);
	else
		Com_Printf("bad connectionless packet from %s:\n%s\n", stream_peer_name(stream, buf, sizeof(buf), qfalse), s);
}

/**
 * @sa CL_ReadPacket
 * @sa NET_ReadMsg
 * @sa SV_Start
 */
void SV_ReadPacket (struct net_stream *s)
{
	client_t *cl = stream_data(s);
	struct dbuffer *msg;

	while ((msg = NET_ReadMsg(s))) {
		int cmd = NET_ReadByte(msg);

		if (cmd == clc_oob)
			SV_ConnectionlessPacket(s, msg);
		else if (cl) {
			SV_ExecuteClientMessage(cl, cmd, msg);
		} else
			free_stream(s);

		free_dbuffer(msg);
	}
}

/**
 * @brief Start the next map in the cycle
 */
void SV_NextMapcycle (void)
{
	int i;
	const char *map = NULL, *gameType = NULL;
	char *base;
	char assembly[MAX_QPATH];
	char expanded[MAX_QPATH];
	char cmd[MAX_QPATH];
	mapcycle_t *mapcycle;

	mapcycle = mapcycleList;
	if (*sv.name) {
		Com_Printf("current map: %s\n", sv.name);
		for (i = 0; i < mapcycleCount; i++) {
			/* random maps may have a theme - but that's not stored in sv.name
			* but in sv.assembly */
			if (*mapcycle->map == '+') {
				Q_strncpyz(expanded, mapcycle->map, sizeof(expanded));
				base = strstr(expanded, " ");
				if (base) {
					*base = '\0'; /* split the strings */
					Q_strncpyz(assembly, base+1, sizeof(assembly));
					/* get current position */
					if (!Q_strcmp(sv.name, expanded) && !Q_strcmp(sv.assembly, assembly)) {
						/* next map in cycle */
						if (mapcycle->next) {
							map = mapcycle->next->map;
							gameType = mapcycle->next->type;
							Com_DPrintf(DEBUG_SERVER, "SV_NextMapcycle: next one: '%s' (gametype: %s)\n", map, gameType);
						/* switch back to first list on cycle - if there is one */
						} else {
							map = mapcycleList->map;
							gameType = mapcycleList->type;
							Com_DPrintf(DEBUG_SERVER, "SV_NextMapcycle: first one: '%s' (gametype: %s)\n", map, gameType);
						}
						break;
					}
				} else {
					Com_Printf("ignore mapcycle entry for random map (%s) with"
						" no assembly given\n", mapcycle->map);
				}
			} else {
				/* get current position */
				if (!Q_strcmp(sv.name, mapcycle->map)) {
					/* next map in cycle */
					if (mapcycle->next) {
						map = mapcycle->next->map;
						gameType = mapcycle->next->type;
						Com_DPrintf(DEBUG_SERVER, "SV_NextMapcycle: next one: '%s' (gametype: %s)\n", map, gameType);
					/* switch back to first list on cycle - if there is one */
					} else {
						map = mapcycleList->map;
						gameType = mapcycleList->type;
						Com_DPrintf(DEBUG_SERVER, "SV_NextMapcycle: first one: '%s' (gametype: %s)\n", map, gameType);
					}
					Com_sprintf(expanded, sizeof(expanded), "maps/%s.bsp", map);

					/* check for bsp file */
					if (FS_CheckFile(expanded) < 0) {
						Com_Printf("SV_NextMapcycle: Can't find '%s' - mapcycle error\n"
							"Use the 'maplist' command to get a list of valid maps\n", expanded);
						map = NULL;
						gameType = NULL;
					} else
						break;
				}
			}
			mapcycle = mapcycle->next;
		}
	}

	if (!map) {
		if (mapcycleCount > 0) {
			map = mapcycleList->map;
			gameType = mapcycleList->type;
			if (*map != '+') {
				Com_sprintf(expanded, sizeof(expanded), "maps/%s.bsp", map);

				/* check for bsp file */
				if (FS_CheckFile(expanded) < 0) {
					Com_Printf("SV_NextMapcycle: Can't find '%s' - mapcycle error\n"
						"Use the 'maplist' command to get a list of valid maps\n", expanded);
					return;
				}
			}
		} else if (*sv.name) {
			Com_Printf("No mapcycle - restart the current map (%s)\n", sv.name);
			map = sv.name;
			gameType = NULL;
		} else {
			Com_Printf("No mapcycle and no running map\n");
			return;
		}
		/* still not set? */
		if (!map)
			return;
	}

	/* check whether we want to change the gametype, too */
	if (gameType && *gameType)
		Cvar_Set("gametype", gameType);

	Com_sprintf(cmd, sizeof(cmd), "map %s", map);
	Cbuf_AddText(cmd);
	Cbuf_Execute();
}

/**
 * @brief Empty the mapcycle list
 * @sa SV_MapcycleAdd
 */
void SV_MapcycleClear (void)
{
	int i;
	mapcycle_t *mapcycle, *oldMapcycle;
	mapcycle = mapcycleList;
	for (i = 0; i < mapcycleCount; i++) {
		oldMapcycle = mapcycle;
		mapcycle = mapcycle->next;
		Mem_Free(oldMapcycle->type);
		Mem_Free(oldMapcycle->map);
		Mem_Free(oldMapcycle);
	}
	/* reset the mapcycle data */
	mapcycleList = NULL;
	mapcycleCount = 0;
}

/**
 * @brief Append a new mapname to the list of maps for the cycle
 * @todo check for maps and valid gametypes here
 * @sa SV_MapcycleClear
 */
void SV_MapcycleAdd (const char* mapName, const char* gameType)
{
	mapcycle_t *mapcycle;

	if (!mapcycleList) {
		mapcycleList = Mem_PoolAlloc(sizeof(mapcycle_t), sv_genericPool, 0);
		mapcycle = mapcycleList; /* first one */
	} else {
		/* go the the last entry */
		mapcycle = mapcycleList;
		while (mapcycle->next)
			mapcycle = mapcycle->next;
		mapcycle->next = Mem_PoolAlloc(sizeof(mapcycle_t), sv_genericPool, 0);
		mapcycle = mapcycle->next;
	}
	mapcycle->map = Mem_PoolStrDup(mapName, sv_genericPool, 0);
	mapcycle->type = Mem_PoolStrDup(gameType, sv_genericPool, 0);
	Com_DPrintf(DEBUG_SERVER, "mapcycle add: '%s' type '%s'\n", mapcycle->map, mapcycle->type);
	mapcycle->next = NULL;
	mapcycleCount++;
}

/**
 * @brief Parses the server mapcycle
 * @sa SV_MapcycleAdd
 * @sa SV_MapcycleClear
 */
static void SV_ParseMapcycle (void)
{
	int length = 0;
	byte *buffer = NULL;
	const char *token;
	const char *buf;
	char map[MAX_VAR], gameType[MAX_VAR];

	mapcycleCount = 0;
	mapcycleList = NULL;

	length = FS_LoadFile("mapcycle.txt", &buffer);
	if (length == -1 || !buffer)
		return;

	if (length != -1) {
		buf = (const char*)buffer;
		do {
			token = COM_Parse(&buf);
			if (!buf)
				break;
			Q_strncpyz(map, token, sizeof(map));
			token = COM_Parse(&buf);
			if (!buf)
				break;
			Q_strncpyz(gameType, token, sizeof(gameType));
			SV_MapcycleAdd(map, gameType);
		} while (buf);

		Com_Printf("added %i maps to the mapcycle\n", mapcycleCount);
	}
	FS_FreeFile(buffer);
}

/**
 * @brief Calls the G_RunFrame function from game api
 * @sa G_RunFrame
 * @sa SV_Frame
 */
static qboolean SV_RunGameFrame (void)
{
	qboolean gameEnd = qfalse;

	/* we always need to bump framenum, even if we
	 * don't run the world, otherwise the delta
	 * compression can get confused when a client
	 * has the "current" frame */
	sv.framenum++;

	gameEnd = ge->RunFrame();

	/* next map in the cycle */
	if (gameEnd && sv_maxclients->integer > 1)
		SV_NextMapcycle();

	return gameEnd;
}

/**
 * @sa Qcommon_Frame
 */
void SV_Frame (int now, void *data)
{
	/* change the gametype even if no server is running (e.g. the first time) */
	if (sv_dedicated->integer && gametype->modified) {
		Com_SetGameType();
		gametype->modified = qfalse;
	}

	/* if server is not active, do nothing */
	if (!svs.initialized)
		return;

	svs.realtime = now;

	/* keep the random time dependent */
	rand();

	/* let everything in the world think and move */
	SV_RunGameFrame();

	/* send a heartbeat to the master if needed */
	Master_Heartbeat();

	/* server is empty - so shutdown */
	if (abandon && killserver) {
		abandon = qfalse;
		killserver = qfalse;
		SV_Shutdown("Server disconnected\n", qfalse);
	}
}

/*============================================================================ */

/**
 * @brief Send a message to the master every few minutes to
 * let it know we are alive, and log information
 */
void Master_Heartbeat (void)
{
	char *responseBuf;

	if (!sv_dedicated || !sv_dedicated->integer)
		return;		/* only dedicated servers send heartbeats */

	if (!sv_public || !sv_public->integer)
		return;		/* a private dedicated game */

	/* check for time wraparound */
	if (svs.last_heartbeat > svs.realtime)
		svs.last_heartbeat = svs.realtime;

	if (svs.realtime - svs.last_heartbeat < HEARTBEAT_SECONDS * 1000)
		return;					/* not time to send yet */

	svs.last_heartbeat = svs.realtime;

	/* send to master */
	responseBuf = HTTP_GetURL(va("%s/ufo/masterserver.php?heartbeat&port=%s", masterserver_url->string, port->string));
	if (responseBuf) {
		Com_DPrintf(DEBUG_SERVER, "response: %s\n", responseBuf);
		Mem_Free(responseBuf);
	}
}

/**
 * @brief Informs all masters that this server is going down
 */
void Master_Shutdown (void)
{
	char *responseBuf;

	if (!sv_dedicated || !sv_dedicated->integer)
		return;					/* only dedicated servers send heartbeats */

	if (!sv_public || !sv_public->integer)
		return;					/* a private dedicated game */

	/* send to master */
	responseBuf = HTTP_GetURL(va("%s/ufo/masterserver.php?shutdown&port=%s", masterserver_url->string, port->string));
	if (responseBuf) {
		Com_DPrintf(DEBUG_SERVER, "response: %s\n", responseBuf);
		Mem_Free(responseBuf);
	}
}

/*============================================================================ */

/**
 * @brief Pull specific info from a newly changed userinfo string into a more C freindly form.
 */
void SV_UserinfoChanged (client_t * cl)
{
	const char *val;
	unsigned int i;

	/* call prog code to allow overrides */
	ge->ClientUserinfoChanged(cl->player, cl->userinfo);

	/* name for C code */
	strncpy(cl->name, Info_ValueForKey(cl->userinfo, "name"), sizeof(cl->name) - 1);
	/* mask off high bit */
	for (i = 0; i < sizeof(cl->name); i++)
		cl->name[i] &= 127;

	/* msg command */
	val = Info_ValueForKey(cl->userinfo, "msg");
	if (strlen(val))
		cl->messagelevel = atoi(val);
}

/**
 * @brief Only called once at startup, not for each game
 */
void SV_Init (void)
{
	Com_Printf("\n------ server initialization -------\n");

	sv_gameSysPool = Mem_CreatePool("Server: Game system");
	sv_genericPool = Mem_CreatePool("Server: Generic");

	memset(&svs, 0, sizeof(svs));

	SV_InitOperatorCommands();

	rcon_password = Cvar_Get("rcon_password", "", 0, NULL);
	Cvar_Get("deathmatch", "0", CVAR_LATCH, NULL);
	Cvar_Get("timelimit", "0", CVAR_SERVERINFO, NULL);
	Cvar_Get("cheats", "0", CVAR_SERVERINFO | CVAR_LATCH, NULL);
	Cvar_Get("protocol", va("%i", PROTOCOL_VERSION), CVAR_SERVERINFO | CVAR_NOSET, NULL);
	/* this cvar will become a latched cvar when you start the server */
	sv_maxclients = Cvar_Get("sv_maxclients", "1", CVAR_SERVERINFO, "Max. connected clients");
	sv_hostname = Cvar_Get("sv_hostname", "noname", CVAR_SERVERINFO | CVAR_ARCHIVE, "The name of the server that is displayed in the serverlist");
	sv_downloadserver = Cvar_Get("sv_downloadserver", "", CVAR_ARCHIVE, "URL to a location where clients can download game content over HTTP");
	sv_enablemorale = Cvar_Get("sv_enablemorale", "1", CVAR_ARCHIVE | CVAR_SERVERINFO | CVAR_LATCH, "Enable morale changes in multiplayer");
	sv_maxsoldiersperteam = Cvar_Get("sv_maxsoldiersperteam", "4", CVAR_ARCHIVE | CVAR_SERVERINFO, "Max. amount of soldiers per team (see sv_maxsoldiersperplayer and sv_teamplay)");
	sv_maxsoldiersperplayer = Cvar_Get("sv_maxsoldiersperplayer", "8", CVAR_ARCHIVE | CVAR_SERVERINFO, "Max. amount of soldiers each player can controll (see maxsoldiers and sv_teamplay)");

	sv_public = Cvar_Get("sv_public", "1", 0, "Should heartbeats be send to the masterserver");
	sv_reconnect_limit = Cvar_Get("sv_reconnect_limit", "3", CVAR_ARCHIVE, "Minimum seconds between connect messages");

	if (sv_dedicated->integer)
		Cvar_Set("sv_maxclients", "8");

	sv_maxclients->modified = qfalse;

	SV_ParseMapcycle();
}

/**
 * @brief Used by SV_Shutdown to send a final message to all
 * connected clients before the server goes down.
 * @sa SV_Shutdown
 * @todo Find out why this isn't send
 */
static void SV_FinalMessage (const char *message, qboolean reconnect)
{
	int i;
	client_t *cl;
	struct dbuffer *msg = new_dbuffer();

	if (reconnect)
		NET_WriteByte(msg, svc_reconnect);
	else
		NET_WriteByte(msg, svc_disconnect);
	NET_WriteString(msg, message);

	for (i = 0, cl = svs.clients; i < sv_maxclients->integer; i++, cl++)
		if (cl->state >= cs_connected) {
			NET_WriteConstMsg(cl->stream, msg);
			stream_finished(cl->stream);
			cl->stream = NULL;
		}

	free_dbuffer(msg);
}

/**
 * @brief Cleanup on game shutdown
 * @sa SV_Shutdown
 * @sa Com_Quit
 */
void SV_Clear (void)
{
	SV_MapcycleClear();
}

/**
 * @brief Called when each game quits, before Sys_Quit or Sys_Error
 * @param[in] finalmsg The message all clients get as server shutdown message
 * @param[in] reconnect True if this is only a restart (new map or map restart),
 * false if the server shutdown completly and you also want to disconnect all clients
 */
void SV_Shutdown (const char *finalmsg, qboolean reconnect)
{
	if (svs.clients)
		SV_FinalMessage(finalmsg, reconnect);

	Master_Shutdown();
	SV_ShutdownGameProgs();

	close_datagram_socket(svs.datagram_socket);
	svs.datagram_socket = NULL;
	SV_Stop();

	/* free current level */
	memset(&sv, 0, sizeof(sv));
	Com_SetServerState(ss_dead);

	/* free server static data */
	if (svs.clients)
		Mem_Free(svs.clients);

	memset(&svs, 0, sizeof(svs));

	/* maybe we shut down before we init - e.g. in case of an error */
	if (sv_maxclients)
		sv_maxclients->flags &= ~CVAR_LATCH;

	if (sv_mapname)
		sv_mapname->flags &= ~CVAR_NOSET;
}

/**
 * @brief Will eventually shutdown the server once all clients have disconnected
 * @sa SV_CountPlayers
 */
void SV_ShutdownWhenEmpty (void)
{
	abandon = qtrue;
	/* pretend server is already dead, otherwise clients may try and reconnect */
	Com_SetServerState(ss_dead);
}
