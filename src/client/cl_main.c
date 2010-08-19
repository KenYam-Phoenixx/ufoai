/**
 * @file cl_main.c
 * @brief Primary functions for the client. NB: The main() is system-specific and can currently be found in ports/.
 */

/*
All original material Copyright (C) 2002-2010 UFO: Alien Invasion.

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
#include "battlescape/cl_localentity.h"
#include "battlescape/events/e_server.h"
#include "battlescape/cl_particle.h"
#include "battlescape/cl_actor.h"
#include "battlescape/cl_hud.h"
#include "battlescape/cl_parse.h"
#include "battlescape/events/e_parse.h"
#include "battlescape/cl_view.h"
#include "cl_console.h"
#include "cl_screen.h"
#include "cl_game.h"
#include "cl_tutorials.h"
#include "cl_tip.h"
#include "cl_team.h"
#include "cl_language.h"
#include "cl_irc.h"
#include "cl_sequence.h"
#include "cl_inventory.h"
#include "cl_menu.h"
#include "cl_http.h"
#include "input/cl_joystick.h"
#include "cinematic/cl_cinematic.h"
#include "sound/s_music.h"
#include "sound/s_sample.h"
#include "renderer/r_main.h"
#include "renderer/r_particle.h"
#include "ui/ui_main.h"
#include "ui/ui_popup.h"
#include "ui/ui_main.h"
#include "ui/ui_font.h"
#include "ui/ui_nodes.h"
#include "ui/ui_parse.h"
#include "multiplayer/mp_callbacks.h"
#include "multiplayer/mp_serverlist.h"
#include "multiplayer/mp_team.h"
#include "../shared/infostring.h"
#include "../shared/parse.h"
#include "../ports/system.h"

cvar_t *cl_fps;
cvar_t *cl_leshowinvis;
cvar_t *cl_selected;

cvar_t *cl_lastsave;

static cvar_t *cl_connecttimeout; /* multiplayer connection timeout value (ms) */

static cvar_t *cl_introshown;

/* userinfo */
static cvar_t *cl_name;
static cvar_t *cl_msg;
static cvar_t *cl_ready;
cvar_t *cl_teamnum;
cvar_t *cl_team;

client_static_t cls;

static int precache_check;

struct memPool_s *cl_genericPool;	/**< permanent client data - menu, fonts */
struct memPool_s *cl_ircSysPool;	/**< irc pool */
struct memPool_s *cl_soundSysPool;
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

	if (cls.state <= ca_connected || cmd[0] == '-' || cmd[0] == '+') {
		Com_Printf("Unknown command \"%s\" - wasn't sent to server\n", cmd);
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
	NET_WriteMsg(cls.netStream, msg);
}

/**
 * @brief Set or print some environment variables via console command
 * @sa Sys_Setenv
 */
static void CL_Env_f (void)
{
	const int argc = Cmd_Argc();

	if (argc == 3) {
		Sys_Setenv(Cmd_Argv(1), Cmd_Argv(2));
	} else if (argc == 2) {
		const char *env = SDL_getenv(Cmd_Argv(1));
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
		NET_WriteMsg(cls.netStream, msg);
	}
}

static void CL_Quit_f (void)
{
	CL_Disconnect();
	Com_Quit();
}

/**
 * @brief Ensures the right menu cvars are set after error drop or map change
 * @note E.g. called after an ERR_DROP was thrown
 * @sa CL_Disconnect
 * @sa SV_Map
 */
void CL_Drop (void)
{
	CL_Disconnect();

	/* drop loading plaque */
	SCR_EndLoadingPlaque();

	GAME_Drop();
}

/**
 * @note Only call @c CL_Connect if there is no connection yet (@c cls.netStream is @c NULL)
 * @sa CL_Disconnect
 * @sa CL_SendChangedUserinfos
 */
static void CL_Connect (void)
{
	Com_SetUserinfoModified(qfalse);

	NET_DatagramSocketClose(cls.netDatagramSocket);
	cls.netDatagramSocket = NULL;

	assert(!cls.netStream);

	if (cls.servername[0] != '\0') {
		assert(cls.serverport[0] != '\0');
		Com_Printf("Connecting to %s %s...\n", cls.servername, cls.serverport);
		cls.netStream = NET_Connect(cls.servername, cls.serverport);
	} else {
		Com_Printf("Connecting to localhost...\n");
		cls.netStream = NET_ConnectToLoopBack();
	}

	if (cls.netStream) {
		NET_OOB_Printf(cls.netStream, "connect %i \"%s\"\n", PROTOCOL_VERSION, Cvar_Userinfo());
		cls.connectTime = CL_Milliseconds();
	} else {
		if (cls.servername[0]) {
			assert(cls.serverport[0]);
			Com_Printf("Could not connect to %s %s\n", cls.servername, cls.serverport);
		} else {
			Com_Printf("Could not connect to localhost\n");
		}
	}
}

/**
 * @brief Called after tactical missions to wipe away the tactical-mission-only data.
 * @sa CL_ParseServerData
 * @sa CL_Disconnect
 * @sa R_ClearScene
 */
static void CL_ClearState (void)
{
	LE_Cleanup();

	/* wipe the entire cl structure */
	memset(&cl, 0, sizeof(cl));
	cl.cam.zoom = 1.0;
	CL_ViewCalcFieldOfViewX();

	/* wipe the particles with every new map */
	r_numParticles = 0;
	/* reset ir goggle state with every new map */
	refdef.rendererFlags &= ~RDF_IRGOGGLES;
}

/**
 * @brief Sets the @c cls.state to @c ca_disconnected and informs the server
 * @sa CL_Disconnect_f
 * @sa CL_Drop
 * @note Goes from a connected state to disconnected state
 * Sends a disconnect message to the server
 * This is also called on @c Com_Error, so it shouldn't cause any errors
 */
void CL_Disconnect (void)
{
	struct dbuffer *msg;

	if (cls.state == ca_disconnected)
		return;

	/* send a disconnect message to the server */
	if (!Com_ServerState()) {
		msg = new_dbuffer();
		NET_WriteByte(msg, clc_stringcmd);
		NET_WriteString(msg, "disconnect\n");
		NET_WriteMsg(cls.netStream, msg);
		/* make sure, that this is send */
		NET_Wait(0);
	}

	NET_StreamFinished(cls.netStream);
	cls.netStream = NULL;

	CL_ClearState();

	S_Stop();

	R_ShutdownModels(qfalse);

	CL_SetClientState(ca_disconnected);
	CL_ClearBattlescapeEvents();
	GAME_EndBattlescape();
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

	NET_OOB_Printf(s, "%s %i", out, PROTOCOL_VERSION);
	NET_StreamFinished(s);
}
#endif

/**
 * @brief Responses to broadcasts, etc
 * @sa CL_ReadPackets
 * @sa CL_Frame
 * @sa SVC_DirectConnect
 * @param[in,out] msg The client stream message buffer to read from
 */
static void CL_ConnectionlessPacket (struct dbuffer *msg)
{
	char s[1024];
	const char *c;
	int i;

	NET_ReadStringLine(msg, s, sizeof(s));

	Cmd_TokenizeString(s, qfalse);

	c = Cmd_Argv(0);

	Com_DPrintf(DEBUG_CLIENT, "server OOB: %s\n", Cmd_Args());

	/* server connection */
	if (!strncmp(c, "client_connect", 13)) {
		for (i = 1; i < Cmd_Argc(); i++) {
			const char *p = Cmd_Argv(i);
			if (!strncmp(p, "dlserver=", 9)) {
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
		NET_WriteString(msg, "new\n");
		NET_WriteMsg(cls.netStream, msg);
		return;
	}

	/* remote command from gui front end */
	if (!strncmp(c, "cmd", 3)) {
		if (!NET_StreamIsLoopback(cls.netStream)) {
			Com_Printf("Command packet from remote host. Ignored.\n");
			return;
		} else {
			char str[1024];
			NET_ReadString(msg, str, sizeof(str));
			Cbuf_AddText(str);
			Cbuf_AddText("\n");
		}
		return;
	}

	/* teaminfo command */
	if (!strncmp(c, "teaminfo", 8)) {
		CL_ParseTeamInfoMessage(msg);
		return;
	}

	/* ping from server */
	if (!strncmp(c, "ping", 4)) {
		NET_OOB_Printf(cls.netStream, "ack");
		return;
	}

	/* echo request from server */
	if (!strncmp(c, "echo", 4)) {
		NET_OOB_Printf(cls.netStream, "%s", Cmd_Argv(1));
		return;
	}

	/* print */
	if (!strncmp(c, "print", 5)) {
		char str[1024];
		NET_ReadString(msg, str, sizeof(str));
		/* special reject messages needs proper handling */
		if (strstr(s, REJ_PASSWORD_REQUIRED_OR_INCORRECT))
			UI_PushWindow("serverpassword", NULL);
		UI_Popup(_("Notice"), _(str));
		return;
	}

	Com_Printf("Unknown command received \"%s\"\n", c);
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
	while ((msg = NET_ReadMsg(cls.netStream))) {
		const svc_ops_t cmd = NET_ReadByte(msg);
		if (cmd == clc_oob)
			CL_ConnectionlessPacket(msg);
		else
			CL_ParseServerMessage(cmd, msg);
		free_dbuffer(msg);
	}
}

/**
 * @brief Prints the current userinfo string to the game console
 * @sa SV_UserInfo_f
 */
static void CL_UserInfo_f (void)
{
	Com_Printf("User info settings:\n");
	Info_Print(Cvar_Userinfo());
}

/**
 * @brief Send the clc_teaminfo command to server
 * @sa GAME_SendCurrentTeamSpawningInfo
 */
static void CL_SpawnSoldiers_f (void)
{
	if (!CL_OnBattlescape())
		return;

	if (cl.spawned)
		return;

	cl.spawned = qtrue;
	GAME_SpawnSoldiers();
}

/**
 * @brief
 * @note Called after precache was sent from the server
 * @sa SV_Configstrings_f
 * @sa CL_Precache_f
 */
void CL_RequestNextDownload (void)
{
	unsigned mapChecksum = 0;
	unsigned scriptChecksum = 0;
	const char *buf;

	if (cls.state != ca_connected) {
		Com_Printf("CL_RequestNextDownload: Not connected (%i)\n", cls.state);
		return;
	}

	/* for singleplayer game this is already loaded in our local server
	 * and if we are the server we don't have to reload the map here, too */
	if (!Com_ServerState()) {
		qboolean day = atoi(cl.configstrings[CS_LIGHTMAP]);

		/* activate the map loading screen for multiplayer, too */
		SCR_BeginLoadingPlaque();

		/* check download */
		if (precache_check == CS_MODELS) { /* confirm map */
			if (cl.configstrings[CS_NAME][0] != '+') {
				if (!CL_CheckOrDownloadFile(va("maps/%s.bsp", cl.configstrings[CS_NAME])))
					return; /* started a download */
			} else {
				/** @todo request ump bsp files */
			}
			precache_check = CS_MODELS + 1;
		}

		/* map might still be downloading? */
		if (CL_PendingHTTPDownloads())
			return;

		while ((buf = FS_GetFileData("ufos/*.ufo")) != NULL)
			scriptChecksum += LittleLong(Com_BlockChecksum(buf, strlen(buf)));
		FS_GetFileData(NULL);

		CM_LoadMap(cl.configstrings[CS_TILES], day, cl.configstrings[CS_POSITIONS], &mapChecksum, cl.clMap, lengthof(cl.clMap));
		if (!*cl.configstrings[CS_VERSION] || !*cl.configstrings[CS_MAPCHECKSUM]
		 || !*cl.configstrings[CS_UFOCHECKSUM] || !*cl.configstrings[CS_OBJECTAMOUNT]) {
			Com_sprintf(popupText, sizeof(popupText), _("Local game version (%s) differs from the servers"), UFO_VERSION);
			UI_Popup(_("Error"), popupText);
			Com_Error(ERR_DISCONNECT, "Local game version (%s) differs from the servers", UFO_VERSION);
			return;
		/* checksum doesn't match with the one the server gave us via configstring */
		} else if (mapChecksum != atoi(cl.configstrings[CS_MAPCHECKSUM])) {
			UI_Popup(_("Error"), _("Local map version differs from server"));
			Com_Error(ERR_DISCONNECT, "Local map version differs from server: %u != '%s'",
				mapChecksum, cl.configstrings[CS_MAPCHECKSUM]);
			return;
		/* amount of objects from script files doensn't match */
		} else if (csi.numODs != atoi(cl.configstrings[CS_OBJECTAMOUNT])) {
			UI_Popup(_("Error"), _("Script files are not the same"));
			Com_Error(ERR_DISCONNECT, "Script files are not the same");
			return;
		/* checksum doesn't match with the one the server gave us via configstring */
		} else if (atoi(cl.configstrings[CS_UFOCHECKSUM]) && scriptChecksum != atoi(cl.configstrings[CS_UFOCHECKSUM])) {
			Com_Printf("You are using modified ufo script files - might produce problems\n");
		} else if (strncmp(UFO_VERSION, cl.configstrings[CS_VERSION], sizeof(UFO_VERSION))) {
			Com_sprintf(popupText, sizeof(popupText), _("Local game version (%s) differs from the servers (%s)"), UFO_VERSION, cl.configstrings[CS_VERSION]);
			UI_Popup(_("Error"), popupText);
			Com_Error(ERR_DISCONNECT, "Local game version (%s) differs from the servers (%s)", UFO_VERSION, cl.configstrings[CS_VERSION]);
			return;
		}
	} else {
		/* Copy the client map from the server */
		memcpy(&cl.clMap, SV_GetRoutingMap(), sizeof(cl.clMap));
	}

	CL_ViewLoadMedia();

	{
		struct dbuffer *msg = new_dbuffer();
		/* send begin */
		/* this will activate the render process (see client state ca_active) */
		NET_WriteByte(msg, clc_stringcmd);
		/* see CL_StartGame */
		NET_WriteString(msg, "begin\n");
		NET_WriteMsg(cls.netStream, msg);
	}

	cls.waitingForStart = CL_Milliseconds();
}


/**
 * @brief The server will send this command right before allowing the client into the server
 * @sa CL_StartGame
 * @sa SV_Configstrings_f
 */
static void CL_Precache_f (void)
{
	precache_check = CS_MODELS;

	CL_RequestNextDownload();
}

static void CL_SetRatioFilter_f (void)
{
	uiNode_t* firstOption = UI_GetOption(OPTION_VIDEO_RESOLUTIONS);
	uiNode_t* option = firstOption;
	float requestedRation = atof(Cmd_Argv(1));
	qboolean all = qfalse;
	qboolean custom = qfalse;
	const float delta = 0.01;

	if (Cmd_Argc() != 2) {
		Com_Printf("Usage: %s <all|floatration>\n", Cmd_Argv(0));
		return;
	}

	if (!strcmp(Cmd_Argv(1), "all"))
		all = qtrue;
	else if (!strcmp(Cmd_Argv(1), "custom"))
		custom = qtrue;
	else
		requestedRation = atof(Cmd_Argv(1));

	while (option) {
		int width;
		int height;
		float ratio;
		qboolean visible = qfalse;
		int result = sscanf(OPTIONEXTRADATA(option).label, "%i x %i", &width, &height);
		if (result != 2)
			Com_Error(ERR_FATAL, "CL_SetRatioFilter_f: Impossible to decode resolution label.\n");
		ratio = (float)width / (float)height;

		if (all)
			visible = qtrue;
		else if (custom)
			/** @todo We should check the ratio list and remove matched resolutions, here it is a hack */
			visible = ratio > 2 || (ratio > 1.7 && ratio < 1.76);
		else
			visible = ratio - delta < requestedRation && ratio + delta > requestedRation;

		option->invis = !visible;
		option = option->next;
	}

	/* the content change */
	UI_RegisterOption(OPTION_VIDEO_RESOLUTIONS, firstOption);
}

static void CL_VideoInitMenu (void)
{
	uiNode_t* option = UI_GetOption(OPTION_VIDEO_RESOLUTIONS);
	if (option == NULL) {
		int i;
		for (i = 0; i < VID_GetModeNums(); i++) {
			vidmode_t vidmode;
			if (VID_GetModeInfo(i, &vidmode))
				UI_AddOption(&option, va("r%ix%i", vidmode.width, vidmode.height), va("%i x %i", vidmode.width, vidmode.height), va("%i", i));
		}
		UI_RegisterOption(OPTION_VIDEO_RESOLUTIONS, option);
	}
}

static void CL_TeamDefInitMenu (void)
{
	uiNode_t* option = UI_GetOption(OPTION_TEAMDEFS);
	if (option == NULL) {
		int i;
		for (i = 0; i < csi.numTeamDefs; i++) {
			teamDef_t *td = &csi.teamDef[i];
			if (td->race != RACE_CIVILIAN)
				UI_AddOption(&option, td->id, _(td->name), td->id);
		}
		UI_RegisterOption(OPTION_TEAMDEFS, option);
	}
}

/** @brief valid mapdef descriptors */
static const value_t mapdef_vals[] = {
	{"description", V_TRANSLATION_STRING, offsetof(mapDef_t, description), 0},
	{"map", V_CLIENT_HUNK_STRING, offsetof(mapDef_t, map), 0},
	{"param", V_CLIENT_HUNK_STRING, offsetof(mapDef_t, param), 0},
	{"size", V_CLIENT_HUNK_STRING, offsetof(mapDef_t, size), 0},

	{"maxaliens", V_INT, offsetof(mapDef_t, maxAliens), MEMBER_SIZEOF(mapDef_t, maxAliens)},
	{"storyrelated", V_BOOL, offsetof(mapDef_t, storyRelated), MEMBER_SIZEOF(mapDef_t, storyRelated)},
	{"hurtaliens", V_BOOL, offsetof(mapDef_t, hurtAliens), MEMBER_SIZEOF(mapDef_t, hurtAliens)},

	{"teams", V_INT, offsetof(mapDef_t, teams), MEMBER_SIZEOF(mapDef_t, teams)},
	{"multiplayer", V_BOOL, offsetof(mapDef_t, multiplayer), MEMBER_SIZEOF(mapDef_t, multiplayer)},

	{"onwin", V_CLIENT_HUNK_STRING, offsetof(mapDef_t, onwin), 0},
	{"onlose", V_CLIENT_HUNK_STRING, offsetof(mapDef_t, onlose), 0},

	{NULL, 0, 0, 0}
};

static void CL_ParseMapDefinition (const char *name, const char **text)
{
	const char *errhead = "Com_ParseMapDefinition: unexpected end of file (mapdef ";
	mapDef_t *md;
	const value_t *vp;
	const char *token;

	/* get it's body */
	token = Com_Parse(text);

	if (!*text || *token != '{') {
		Com_Printf("Com_ParseMapDefinition: mapdef \"%s\" without body ignored\n", name);
		return;
	}

	md = Com_GetMapDefByIDX(cls.numMDs);
	cls.numMDs++;
	if (cls.numMDs >= lengthof(cls.mds))
		Sys_Error("Com_ParseMapDefinition: Max mapdef hit");

	memset(md, 0, sizeof(*md));
	md->id = Mem_PoolStrDup(name, com_genericPool, 0);

	do {
		token = Com_EParse(text, errhead, name);
		if (!*text)
			break;
		if (*token == '}')
			break;

		for (vp = mapdef_vals; vp->string; vp++)
			if (!strcmp(token, vp->string)) {
				/* found a definition */
				token = Com_EParse(text, errhead, name);
				if (!*text)
					return;

				switch (vp->type) {
				default:
					Com_EParseValue(md, token, vp->type, vp->ofs, vp->size);
					break;
				case V_TRANSLATION_STRING:
					if (*token == '_')
						token++;
				/* fall through */
				case V_CLIENT_HUNK_STRING:
					Mem_PoolStrDupTo(token, (char**) ((char*)md + (int)vp->ofs), com_genericPool, 0);
					break;
				}
				break;
			}

		if (!vp->string) {
			linkedList_t **list;
			if (!strcmp(token, "ufos")) {
				list = &md->ufos;
			} else if (!strcmp(token, "aircraft")) {
				list = &md->aircraft;
			} else if (!strcmp(token, "terrains")) {
				list = &md->terrains;
			} else if (!strcmp(token, "populations")) {
				list = &md->populations;
			} else if (!strcmp(token, "cultures")) {
				list = &md->cultures;
			} else if (!strcmp(token, "gametypes")) {
				list = &md->gameTypes;
			} else {
				Com_Printf("Com_ParseMapDefinition: unknown token \"%s\" ignored (mapdef %s)\n", token, name);
				continue;
			}
			token = Com_EParse(text, errhead, name);
			if (!*text || *token != '{') {
				Com_Printf("Com_ParseMapDefinition: mapdef \"%s\" has ufos, gametypes, terrains, populations or cultures block with no opening brace\n", name);
				break;
			}
			do {
				token = Com_EParse(text, errhead, name);
				if (!*text || *token == '}')
					break;
				LIST_AddString(list, token);
			} while (*text);
		}
	} while (*text);

	if (!md->map) {
		Com_Printf("Com_ParseMapDefinition: mapdef \"%s\" with no map\n", name);
		cls.numMDs--;
	}

	if (!md->description) {
		Com_Printf("Com_ParseMapDefinition: mapdef \"%s\" with no description\n", name);
		cls.numMDs--;
	}
}

/**
 * @sa FS_MapDefSort
 */
static int Com_MapDefSort (const void *mapDef1, const void *mapDef2)
{
	const char *map1 = ((const mapDef_t *)mapDef1)->map;
	const char *map2 = ((const mapDef_t *)mapDef2)->map;

	/* skip special map chars for rma and base attack */
	if (map1[0] == '+' || map1[0] == '.')
		map1++;
	if (map2[0] == '+' || map2[0] == '.')
		map2++;

	return Q_StringSort(map1, map2);
}

/**
 * @brief Init function for clients - called after menu was initialized and ufo-scripts were parsed
 * @sa Qcommon_Init
 */
void CL_InitAfter (void)
{
	/* start the music track already while precaching data */
	S_Frame();
	S_LoadSamples();

	/* preload all models for faster access */
	CL_ViewPrecacheModels(); /* 95% */

	CL_TeamDefInitMenu();
	CL_VideoInitMenu();
	IN_JoystickInitMenu();

	CL_LanguageInit();

	/* sort the mapdef array */
	qsort(cls.mds, cls.numMDs, sizeof(mapDef_t), Com_MapDefSort);
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
 * @note This data is persistent until you shutdown the game
 */
void CL_ParseClientData (const char *type, const char *name, const char **text)
{
	if (!strcmp(type, "font"))
		UI_ParseFont(name, text);
	else if (!strcmp(type, "tutorial"))
		TUT_ParseTutorials(name, text);
	else if (!strcmp(type, "menu_model"))
		UI_ParseUIModel(name, text);
	else if (!strcmp(type, "icon"))
		UI_ParseIcon(name, text);
	else if (!strcmp(type, "particle"))
		CL_ParseParticle(name, text);
	else if (!strcmp(type, "sequence"))
		CL_ParseSequence(name, text);
	else if (!strcmp(type, "music"))
		M_ParseMusic(name, text);
	else if (!strcmp(type, "tips"))
		CL_ParseTipsOfTheDay(name, text);
	else if (!strcmp(type, "language"))
		CL_ParseLanguages(name, text);
	else if (!strcmp(type, "window"))
		UI_ParseWindow(type, name, text);
	else if (!strcmp(type, "component"))
		UI_ParseComponent(type, text);
	else if (!strcmp(type, "mapdef"))
		CL_ParseMapDefinition(name, text);
}

/** @brief Cvars for initial check (popup at first start) */
static cvarList_t checkcvar[] = {
	{"cl_name", NULL, NULL},
	{"s_language", NULL, NULL},

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
		if (checkcvar[i].var->string[0] == '\0') {
			Com_Printf("%s has no value\n", checkcvar[i].var->name);
			UI_PushWindow("checkcvars", NULL);
			break;
		}
		i++;
	}
}

/**
 * @brief Print the configstrings to game console
 * @sa SV_PrintConfigStrings_f
 */
static void CL_ShowConfigstrings_f (void)
{
	int i;

	for (i = 0; i < MAX_CONFIGSTRINGS; i++) {
		if (cl.configstrings[i][0] == '\0')
			continue;
		Com_Printf("cl.configstrings[%3i]: %s\n", i, cl.configstrings[i]);
	}
}

/**
 * @brief Calls all reset functions for all subsystems like production and research
 * also initializes the cvars and commands
 * @sa CL_Init
 */
static void CL_InitLocal (void)
{
	CL_SetClientState(ca_disconnected);
	cls.realtime = Sys_Milliseconds();

	/* register our variables */
	cl_introshown = Cvar_Get("cl_introshown", "0", CVAR_ARCHIVE, "Only show the intro once at the initial start");
	cl_fps = Cvar_Get("cl_fps", "0", CVAR_ARCHIVE, "Show frames per second");
	cl_log_battlescape_events = Cvar_Get("cl_log_battlescape_events", "1", 0, "Log all battlescape events to events.log");
	cl_selected = Cvar_Get("cl_selected", "0", CVAR_NOSET, "Current selected soldier");
	cl_connecttimeout = Cvar_Get("cl_connecttimeout", "25000", CVAR_ARCHIVE, "Connection timeout for multiplayer connects");
	cl_lastsave = Cvar_Get("cl_lastsave", "", CVAR_ARCHIVE, "Last saved slot - use for the continue-campaign function");
	/* userinfo */
	cl_name = Cvar_Get("cl_name", Sys_GetCurrentUser(), CVAR_USERINFO | CVAR_ARCHIVE, "Playername");
	cl_team = Cvar_Get("cl_team", "1", CVAR_USERINFO, "Humans (0) or aliens (7)");
	cl_teamnum = Cvar_Get("cl_teamnum", "1", CVAR_USERINFO | CVAR_ARCHIVE, "Teamnum for multiplayer teamplay games");
	cl_ready = Cvar_Get("cl_ready", "0", CVAR_USERINFO, "Ready indicator in the userinfo for tactical missions");
	cl_msg = Cvar_Get("cl_msg", "2", CVAR_USERINFO | CVAR_ARCHIVE, "Sets the message level for server messages the client receives");
	sv_maxclients = Cvar_Get("sv_maxclients", "1", CVAR_SERVERINFO, "If sv_maxclients is 1 we are in singleplayer - otherwise we are multiplayer mode (see sv_teamplay)");

	masterserver_url = Cvar_Get("masterserver_url", MASTER_SERVER, CVAR_ARCHIVE, "URL of UFO:AI masterserver");

	cl_map_debug = Cvar_Get("debug_map", "0", 0, "Activate realtime map debugging options - bitmask. Valid values are 0, 1, 3 and 7");
	cl_le_debug = Cvar_Get("debug_le", "0", 0, "Activates some local entity debug rendering");
	cl_leshowinvis = Cvar_Get("cl_leshowinvis", "0", CVAR_ARCHIVE, "Show invisible local entities as null models");

	/* register our commands */
	Cmd_AddCommand("check_cvars", CL_CheckCvars_f, "Check cvars like playername and so on");
	Cmd_AddCommand("targetalign", CL_ActorTargetAlign_f, _("Target your shot to the ground"));

	Cmd_AddCommand("cl_setratiofilter", CL_SetRatioFilter_f, "Filter the resolution screen list with a ration");

	Cmd_AddCommand("cmd", CL_ForwardToServer_f, "Forward to server");
	Cmd_AddCommand("cl_userinfo", CL_UserInfo_f, "Prints your userinfo string");
#ifdef ACTIVATE_PACKET_COMMAND
	/* this is dangerous to leave in */
	Cmd_AddCommand("packet", CL_Packet_f, "Dangerous debug function for network testing");
#endif
	Cmd_AddCommand("quit", CL_Quit_f, "Quits the game");
	Cmd_AddCommand("env", CL_Env_f, NULL);

	Cmd_AddCommand("precache", CL_Precache_f, "Function that is called at mapload to precache map data");
	Cmd_AddCommand("spawnsoldiers", CL_SpawnSoldiers_f, "Spawns the soldiers for the selected teamnum");
	Cmd_AddCommand("cl_configstrings", CL_ShowConfigstrings_f, "Print client configstrings to game console");

	/* forward to server commands
	 * the only thing this does is allow command completion
	 * to work -- all unknown commands are automatically
	 * forwarded to the server */
	Cmd_AddCommand("say", NULL, "Chat command");
	Cmd_AddCommand("say_team", NULL, "Team chat command");
	Cmd_AddCommand("players", NULL, "List of team and player name");
#ifdef DEBUG
	Cmd_AddCommand("debug_cgrid", Grid_DumpWholeClientMap_f, "Shows the whole client side pathfinding grid of the current loaded map");
	Cmd_AddCommand("debug_croute", Grid_DumpClientRoutes_f, "Shows the whole client side pathfinding grid of the current loaded map");
	Cmd_AddCommand("debug_listle", LE_List_f, "Shows a list of current know local entities with type and status");
	Cmd_AddCommand("debug_listlm", LM_List_f, "Shows a list of current know local models");
	/* forward commands again */
	Cmd_AddCommand("debug_edictdestroy", NULL, "Call the 'destroy' function of a given edict");
	Cmd_AddCommand("debug_edictuse", NULL, "Call the 'use' function of a given edict");
	Cmd_AddCommand("debug_edicttouch", NULL, "Call the 'touch' function of a given edict");
	Cmd_AddCommand("debug_killteam", NULL, "Kills a given team");
	Cmd_AddCommand("debug_stunteam", NULL, "Stuns a given team");
	Cmd_AddCommand("debug_listscore", NULL, "Shows mission-score entries of all team members");
#endif

	IN_Init();
	CL_ServerEventsInit();
	CL_CameraInit();

	CLMN_InitStartup();
	TUT_InitStartup();
	PTL_InitStartup();
	GAME_InitStartup();
	SEQ_InitStartup();
	ACTOR_InitStartup();
	TEAM_InitStartup();
	TOTD_InitStartup();
	HUD_InitStartup();
	INV_InitStartup();
	HTTP_InitStartup();
}

/**
 * @brief Send the userinfo to the server (and to all other clients)
 * when they changed (CVAR_USERINFO)
 * @sa CL_Connect
 */
static void CL_SendChangedUserinfos (void)
{
	/* send a userinfo update if needed */
	if (cls.state >= ca_connected) {
		if (Com_IsUserinfoModified()) {
			struct dbuffer *msg = new_dbuffer();
			NET_WriteByte(msg, clc_userinfo);
			NET_WriteString(msg, Cvar_Userinfo());
			NET_WriteMsg(cls.netStream, msg);
			Com_SetUserinfoModified(qfalse);
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
	CL_SendChangedUserinfos();

	/* fix any cheating cvars */
	Cvar_FixCheatVars();

	switch (cls.state) {
	case ca_disconnected:
		/* if the local server is running and we aren't connected then connect */
		if (Com_ServerState()) {
			cls.servername[0] = '\0';
			cls.serverport[0] = '\0';
			CL_SetClientState(ca_connecting);
			return;
		}
		break;
	case ca_connecting:
		if (CL_Milliseconds() - cls.connectTime > cl_connecttimeout->integer) {
			if (GAME_IsMultiplayer())
				Com_Error(ERR_DROP, "Server is not reachable");
		}
		break;
	case ca_connected:
		if (cls.waitingForStart) {
			if (CL_Milliseconds() - cls.waitingForStart > cl_connecttimeout->integer) {
				Com_Error(ERR_DROP, "Server aborted connection - the server didn't response in %is. You can try to increase the cvar cl_connecttimeout",
						cl_connecttimeout->integer / 1000);
			} else {
				Com_sprintf(cls.loadingMessages, sizeof(cls.loadingMessages),
					"%s (%i)", _("Awaiting game start"), (CL_Milliseconds() - cls.waitingForStart) / 1000);
				SCR_UpdateScreen();
			}
		}
		break;
	default:
		break;
	}
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
		Com_Error(ERR_FATAL, "CL_SetClientState: Don't set state ca_uninitialized\n");
		break;
	case ca_sequence:
		refdef.ready = qtrue;
		break;
	case ca_active:
		cls.waitingForStart = 0;
		break;
	case ca_connecting:
		CL_Connect();
		break;
	case ca_disconnected:
		cls.waitingForStart = 0;
		break;
	case ca_connected:
		/* wipe the client_state_t struct */
		CL_ClearState();
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
	static int lastFrame = 0;
	int delta;

	if (sys_priority->modified || sys_affinity->modified)
		Sys_SetAffinityAndPriority();

	/* decide the simulation time */
	delta = now - lastFrame;
	if (lastFrame)
		cls.frametime = delta / 1000.0;
	else
		cls.frametime = 0;
	cls.realtime = Sys_Milliseconds();
	cl.time = now;
	lastFrame = now;

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

	GAME_Frame();

	/* update camera position */
	CL_CameraMove();

	/* end the rounds when no soldier is alive */
	CL_RunMapParticles();
	CL_ParticleRun();

	/* update the screen */
	SCR_UpdateScreen();

	/* advance local effects for next frame */
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
	/* language */
	if (s_language->modified)
		CL_LanguageTryToSet(s_language->string);

	Irc_Logic_Frame();

	HUD_Update();
}

static void CL_InitMemPools (void)
{
	cl_genericPool = Mem_CreatePool("Client: Generic");
	cl_soundSysPool = Mem_CreatePool("Client: Sound system");
	cl_ircSysPool = Mem_CreatePool("Client: IRC system");
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

	memset(&cls, 0, sizeof(cls));

	fs_i18ndir = Cvar_Get("fs_i18ndir", "", 0, "System path to language files");
	/* i18n through gettext */
	setlocale(LC_ALL, "C");
	setlocale(LC_MESSAGES, "");
	/* use system locale dir if we can't find in gamedir */
	if (fs_i18ndir->string[0] != '\0')
		Q_strncpyz(languagePath, fs_i18ndir->string, sizeof(languagePath));
	else
		Com_sprintf(languagePath, sizeof(languagePath), "%s/"BASEDIRNAME"/i18n/", FS_GetCwd());
	Com_DPrintf(DEBUG_CLIENT, "...using mo files from %s\n", languagePath);
	bindtextdomain(TEXT_DOMAIN, languagePath);
	bind_textdomain_codeset(TEXT_DOMAIN, "UTF-8");
	/* load language file */
	textdomain(TEXT_DOMAIN);

	CL_InitMemPools();

	/* all archived variables will now be loaded */
	Con_Init();

	CIN_Init();

	VID_Init();
	S_Init();
	SCR_Init();

	CL_InitLocal();

	Irc_Init();
	CL_ViewInit();

	CL_InitParticles();

	CL_ClearState();

	/* Touch memory */
	Mem_TouchGlobal();
}

int CL_Milliseconds (void)
{
	return cls.realtime;
}

/**
 * @brief Saves configuration file and shuts the client systems down
 * @todo this is a callback from @c Sys_Quit and @c Com_Error. It would be better
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

	GAME_SetMode(GAME_NONE);
	CL_HTTP_Cleanup();
	Irc_Shutdown();
	Con_SaveConsoleHistory();
	Key_WriteBindings("keys.cfg");
	S_Shutdown();
	R_Shutdown();
	UI_Shutdown();
	CIN_Shutdown();
	FS_Shutdown();
}
