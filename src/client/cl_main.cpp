/**
 * @file
 * @brief Primary functions for the client. NB: The main() is system-specific and can currently be found in ports/.
 */

/*
All original material Copyright (C) 2002-2025 UFO: Alien Invasion.

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
#include "battlescape/cl_radar.h"
#include "battlescape/cl_actor.h"
#include "battlescape/cl_hud.h"
#include "battlescape/cl_parse.h"
#include "battlescape/events/e_parse.h"
#include "battlescape/cl_view.h"
#include "cl_console.h"
#include "cl_screen.h"
#include "cgame/cl_game.h"
#include "cl_tutorials.h"
#include "cl_tip.h"
#include "cl_team.h"
#include "cl_language.h"
#include "cl_irc.h"
#include "cinematic/cl_sequence.h"
#include "cl_inventory.h"
#include "cl_menu.h"
#include "cl_http.h"
#include "cl_lua.h"
#include "input/cl_joystick.h"
#include "cinematic/cl_cinematic.h"
#include "sound/s_music.h"
#include "sound/s_mumble.h"
#include "web/web_main.h"
#include "renderer/r_main.h"
#include "renderer/r_particle.h"
#include "ui/ui_main.h"
#include "ui/ui_popup.h"
#include "ui/ui_draw.h"
#include "ui/ui_font.h"
#include "ui/ui_nodes.h"
#include "ui/ui_parse.h"
#include "ui/ui_lua.h"
#include "cgame/cl_game_team.h"
#include "../shared/infostring.h"
#include "../shared/parse.h"
#include "../ports/system.h"

cvar_t* cl_fps;
cvar_t* cl_leshowinvis;
cvar_t* cl_selected;

static cvar_t* cl_connecttimeout; /* multiplayer connection timeout value (ms) */

/* userinfo */
static cvar_t* cl_name;
static cvar_t* cl_msg;
static cvar_t* cl_ready;
cvar_t* cl_teamnum;

client_static_t cls;
static bool isdown;

memPool_t* cl_genericPool;	/**< permanent client data - menu, fonts */
memPool_t* vid_genericPool;	/**< also holds all the static models */
memPool_t* vid_imagePool;
memPool_t* vid_lightPool;	/**< lightmap - wiped with every new map */
memPool_t* vid_modelPool;	/**< modeldata - wiped with every new map */
/*====================================================================== */

/**
 * @brief adds the current command line as a clc_stringcmd to the client message.
 * things like action, turn, etc, are commands directed to the server,
 * so when they are typed in at the console, they will need to be forwarded.
 */
void Cmd_ForwardToServer (void)
{
	const char* cmd = Cmd_Argv(0);

	if (cls.state <= ca_connected || cmd[0] == '-' || cmd[0] == '+') {
		Com_Printf("Unknown command \"%s\" - wasn't sent to server\n", cmd);
		return;
	}

	dbuffer msg;
	NET_WriteByte(&msg, clc_stringcmd);
	msg.add(cmd, strlen(cmd));
	if (Cmd_Argc() > 1) {
		msg.add(" ", 1);
		msg.add(Cmd_Args(), strlen(Cmd_Args()));
	}
	msg.add("", 1);
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
		const char* env = SDL_getenv(Cmd_Argv(1));
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
		const int len = strlen(Cmd_Args()) + 1;
		dbuffer msg(len + 1);
		NET_WriteByte(&msg, clc_stringcmd);
		msg.add(Cmd_Args(), len);
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

static void CL_Reconnect (void)
{
	if (cls.reconnectTime == 0 || cls.reconnectTime > CL_Milliseconds())
		return;

	Com_Printf("Reconnecting...\n");
	CL_Disconnect();
	CL_SetClientState(ca_connecting);
	/* otherwise we would time out */
	cls.connectTime = CL_Milliseconds() - 1500;
}

static void CL_FreeClientStream (void)
{
	cls.netStream = nullptr;
	Com_Printf("Client stream was closed\n");
}

/**
 * @note Only call @c CL_Connect if there is no connection yet (@c cls.netStream is @c nullptr)
 * @sa CL_Disconnect
 * @sa CL_SendChangedUserinfos
 */
static void CL_Connect (void)
{
	Com_SetUserinfoModified(false);

	assert(!cls.netStream);

	if (cls.servername[0] != '\0') {
		assert(cls.serverport[0] != '\0');
		Com_Printf("Connecting to %s %s...\n", cls.servername, cls.serverport);
		cls.netStream = NET_Connect(cls.servername, cls.serverport, CL_FreeClientStream);
	} else {
		Com_Printf("Connecting to localhost...\n");
		cls.netStream = NET_ConnectToLoopBack(CL_FreeClientStream);
	}

	if (cls.netStream) {
		char info[MAX_INFO_STRING];
		NET_OOB_Printf(cls.netStream, SV_CMD_CONNECT " %i \"%s\"\n", PROTOCOL_VERSION, Cvar_Userinfo(info, sizeof(info)));
		cls.connectTime = CL_Milliseconds();
	} else {
		if (cls.servername[0] != '\0') {
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

	cl = clientBattleScape_t();
	cl.cam.zoom = 1.0;
	CL_ViewCalcFieldOfViewX();

	/* wipe the particles with every new map */
	r_numParticles = 0;
	/* reset ir goggle state with every new map */
	refdef.rendererFlags &= ~RDF_IRGOGGLES;
}

/**
 * @brief Sets the @c cls.state to @c ca_disconnected and informs the server
 * @sa CL_Drop
 * @note Goes from a connected state to disconnected state
 * Sends a disconnect message to the server
 * This is also called on @c Com_Error, so it shouldn't cause any errors
 */
void CL_Disconnect (void)
{
	if (cls.state < ca_connecting)
		return;

	Com_Printf("Disconnecting...\n");

	/* send a disconnect message to the server */
	if (!Com_ServerState()) {
		dbuffer msg;
		NET_WriteByte(&msg, clc_stringcmd);
		NET_WriteString(&msg, NET_STATE_DISCONNECT "\n");
		NET_WriteMsg(cls.netStream, msg);
		/* make sure, that this is send */
		NET_Wait(0);
	}

	NET_StreamFinished(cls.netStream);
	cls.netStream = nullptr;

	CL_ClearState();

	S_Stop();

	R_ShutdownModels(false);
	R_FreeWorldImages();

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
	if (Cmd_Argc() != 4) {
		Com_Printf("Usage: %s <destination> <port> <contents>\n", Cmd_Argv(0));
		return;
	}

	struct net_stream* s = NET_Connect(Cmd_Argv(1), Cmd_Argv(2));
	if (!s) {
		Com_Printf("Could not connect to %s at port %s\n", Cmd_Argv(1), Cmd_Argv(2));
		return;
	}

	const char* in = Cmd_Argv(3);

	const int l = strlen(in);
	char buf[MAX_STRING_TOKENS];
	char* out = buf;

	for (int i = 0; i < l; i++) {
		if (in[i] == '\\' && in[i + 1] == 'n') {
			*out++ = '\n';
			i++;
		} else {
			*out++ = in[i];
		}
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
static void CL_ConnectionlessPacket (dbuffer* msg)
{
	char s[512];
	NET_ReadStringLine(msg, s, sizeof(s));

	Cmd_TokenizeString(s, false);

	const char* c = Cmd_Argv(0);
	Com_DPrintf(DEBUG_CLIENT, "server OOB: %s (%s)\n", c, Cmd_Args());

	/* server connection */
	if (Q_streq(c, CL_CMD_CLIENT_CONNECT)) {
		int i;
		for (i = 1; i < Cmd_Argc(); i++) {
			if (char const* const p = Q_strstart(Cmd_Argv(i), "dlserver=")) {
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
		dbuffer buf(5);
		NET_WriteByte(&buf, clc_stringcmd);
		NET_WriteString(&buf, NET_STATE_NEW "\n");
		NET_WriteMsg(cls.netStream, buf);
		GAME_InitMissionBriefing(_("Loading"));
		return;
	}

	/* remote command from gui front end */
	if (Q_streq(c, CL_CMD_COMMAND)) {
		if (!NET_StreamIsLoopback(cls.netStream)) {
			Com_Printf("Command packet from remote host. Ignored.\n");
			return;
		} else {
			char str[512];
			NET_ReadString(msg, str, sizeof(str));
			Cbuf_AddText("%s\n", str);
		}
		return;
	}

	/* ping from server */
	if (Q_streq(c, CL_CMD_PING)) {
		NET_OOB_Printf(cls.netStream, SV_CMD_ACK);
		return;
	}

	/* echo request from server */
	if (Q_streq(c, CL_CMD_ECHO)) {
		NET_OOB_Printf(cls.netStream, "%s", Cmd_Argv(1));
		return;
	}

	/* print */
	if (Q_streq(c, SV_CMD_PRINT)) {
		NET_ReadString(msg, popupText, sizeof(popupText));
		/* special reject messages needs proper handling */
		if (strstr(popupText, REJ_PASSWORD_REQUIRED_OR_INCORRECT)) {
			UI_PushWindow("serverpassword");
			if (Q_strvalid(Cvar_GetString("password"))) {
				Cvar_Set("password", "%s", "");
				CL_Drop();
				UI_Popup(_("Connection failure"), _("The password you specified was wrong."));
			} else {
				CL_Drop();
				UI_Popup(_("Connection failure"), _("This server requires a password."));
			}
		} else if (strstr(popupText, REJ_SERVER_FULL)) {
			CL_Drop();
			UI_Popup(_("Connection failure"), _("This server is full."));
		} else if (strstr(popupText, REJ_BANNED)) {
			CL_Drop();
			UI_Popup(_("Connection failure"), _("You are banned on this server."));
		} else if (strstr(popupText, REJ_GAME_ALREADY_STARTED)) {
			CL_Drop();
			UI_Popup(_("Connection failure"), _("The game has already started."));
		} else if (strstr(popupText, REJ_SERVER_VERSION_MISMATCH)) {
			CL_Drop();
			UI_Popup(_("Connection failure"), _("The server is running a different version of the game."));
		} else if (strstr(popupText, BAD_RCON_PASSWORD)) {
			Cvar_Set("rcon_password", "%s", "");
			UI_Popup(_("Bad rcon password"), _("The rcon password you specified was wrong."));
		} else if (strstr(popupText, REJ_CONNECTION_REFUSED)) {
			CL_Drop();
			UI_Popup(_("Connection failure"), _("The server refused the connection."));
		} else if (Q_strvalid(popupText)) {
			UI_Popup(_("Notice"), _(popupText));
		}
		return;
	}

	if (!GAME_HandleServerCommand(c, msg))
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
	dbuffer* msg;
	while ((msg = NET_ReadMsg(cls.netStream))) {
		const svc_ops_t cmd = NET_ReadByte(msg);
		if (cmd == svc_oob)
			CL_ConnectionlessPacket(msg);
		else
			CL_ParseServerMessage(cmd, msg);
		delete msg;
	}
}

/**
 * @brief Prints the current userinfo string to the game console
 * @sa SV_UserInfo_f
 */
static void CL_UserInfo_f (void)
{
	Com_Printf("User info settings:\n");
	char info[MAX_INFO_STRING];
	Info_Print(Cvar_Userinfo(info, sizeof(info)));
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

	cl.spawned = true;
	GAME_SpawnSoldiers();
}

static void CL_StartMatch_f (void)
{
	if (!cl.spawned)
		return;

	if (cl.started)
		return;

	cl.started = true;
	GAME_StartMatch();
}

static bool CL_DownloadUMPMap (const char* tiles)
{
	char name[MAX_VAR];
	char base[MAX_QPATH];
	bool startedDownload = false;

	/* load tiles */
	while (tiles) {
		/* get tile name */
		const char* token = Com_Parse(&tiles);
		if (!tiles)
			return startedDownload;

		/* get base path */
		if (token[0] == '-') {
			Q_strncpyz(base, token + 1, sizeof(base));
			continue;
		}

		/* get tile name */
		if (token[0] == '+')
			Com_sprintf(name, sizeof(name), "%s%s", base, token + 1);
		else
			Q_strncpyz(name, token, sizeof(name));

		startedDownload |= !CL_CheckOrDownloadFile(va("maps/%s.bsp", name));
	}

	return startedDownload;
}

static bool CL_DownloadMap (const char* map)
{
	bool startedDownload;
	if (map[0] != '+') {
		startedDownload = !CL_CheckOrDownloadFile(va("maps/%s.bsp", map));
	} else {
		startedDownload = !CL_CheckOrDownloadFile(va("maps/%s.ump", map + 1));
		if (!startedDownload) {
			const char* tiles = CL_GetConfigString(CS_TILES);
			startedDownload = CL_DownloadUMPMap(tiles);
		}
	}

	return startedDownload;
}

/**
 * @return @c true if are a compatible client and nothing else must be downloaded or no downloads are still running,
 * @c false if the start of the match must get a little bit postponed (running downloads).
 * @note throws ERR_DISCONNECT if we are not compatible to the server
 */
static bool CL_CanMultiplayerStart (void)
{
	const int day = CL_GetConfigStringInteger(CS_LIGHTMAP);
	const char* serverVersion = CL_GetConfigString(CS_VERSION);

	/* checksum doesn't match with the one the server gave us via configstring */
	if (!Q_streq(UFO_VERSION, serverVersion)) {
		Com_sprintf(popupText, sizeof(popupText), _("Local game version (%s) differs from the server version (%s)"), UFO_VERSION, serverVersion);
		UI_Popup(_("Error"), popupText);
		Com_Error(ERR_DISCONNECT, "Local game version (%s) differs from the server version (%s)", UFO_VERSION, serverVersion);
	/* amount of objects from script files doesn't match */
	} else if (csi.numODs != CL_GetConfigStringInteger(CS_OBJECTAMOUNT)) {
		UI_Popup(_("Error"), _("Script files are not the same"));
		Com_Error(ERR_DISCONNECT, "Script files are not the same");
	}

	/* activate the map loading screen for multiplayer, too */
	SCR_BeginLoadingPlaque();

	/* check download */
	if (cls.downloadMaps) { /* confirm map */
		if (CL_DownloadMap(CL_GetConfigString(CS_NAME)))
			return false;
		cls.downloadMaps = false;
	}

	/* map might still be downloading? */
	if (CL_PendingHTTPDownloads())
		return false;

	if (Com_GetScriptChecksum() != CL_GetConfigStringInteger(CS_UFOCHECKSUM))
		Com_Printf("You are using modified ufo script files - might produce problems\n");

	CM_LoadMap(CL_GetConfigString(CS_TILES), day, CL_GetConfigString(CS_POSITIONS), CL_GetConfigString(CS_ENTITYSTRING), cl.mapData, cl.mapTiles);

#if 0
	if (cl.mapData->mapChecksum != CL_GetConfigStringInteger(CS_MAPCHECKSUM)) {
		UI_Popup(_("Error"), _("Local map version differs from server"));
		Com_Error(ERR_DISCONNECT, "Local map version differs from server: %u != '%i'",
			cl.mapData->mapChecksum, CL_GetConfigStringInteger(CS_MAPCHECKSUM));
	}
#endif

	return true;
}

/**
 * @brief
 * @note Called after precache was sent from the server
 * @sa SV_Configstrings_f
 * @sa CL_Precache_f
 */
void CL_RequestNextDownload (void)
{
	if (cls.state != ca_connected) {
		Com_Printf("CL_RequestNextDownload: Not connected (%i)\n", cls.state);
		return;
	}

	/* Use the map data from the server */
	cl.mapTiles = SV_GetMapTiles();
	cl.mapData = SV_GetMapData();

	/* as a multiplayer client we have to load the map here and
	 * check the compatibility with the server */
	if (!Com_ServerState() && !CL_CanMultiplayerStart())
		return;

	CL_ViewLoadMedia();

	dbuffer msg(7);
	/* send begin */
	/* this will activate the render process (see client state ca_active) */
	NET_WriteByte(&msg, clc_stringcmd);
	/* see CL_StartGame */
	NET_WriteString(&msg, NET_STATE_BEGIN "\n");
	NET_WriteMsg(cls.netStream, msg);

	cls.waitingForStart = CL_Milliseconds();

	S_MumbleLink();
}


/**
 * @brief The server will send this command right before allowing the client into the server
 * @sa CL_StartGame
 * @sa SV_Configstrings_f
 */
static void CL_Precache_f (void)
{
	cls.downloadMaps = true;

	CL_RequestNextDownload();
}

static void CL_SetRatioFilter_f (void)
{
	uiNode_t* firstOption = UI_GetOption(OPTION_VIDEO_RESOLUTIONS);
	uiNode_t* option = firstOption;
	float requestedRation = atof(Cmd_Argv(1));
	bool all = false;
	bool custom = false;
	const float delta = 0.01;

	if (Cmd_Argc() != 2) {
		Com_Printf("Usage: %s <all|floatration>\n", Cmd_Argv(0));
		return;
	}

	if (Q_streq(Cmd_Argv(1), "all"))
		all = true;
	else if (Q_streq(Cmd_Argv(1), "custom"))
		custom = true;
	else
		requestedRation = atof(Cmd_Argv(1));

	while (option) {
		int width;
		int height;
		bool visible = false;
		const int result = sscanf(OPTIONEXTRADATA(option).label, "%i x %i", &width, &height);
		if (result != 2)
			Com_Error(ERR_FATAL, "CL_SetRatioFilter_f: Impossible to decode resolution label.\n");
		const float ratio = (float)width / (float)height;

		if (all)
			visible = true;
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

/** @brief valid actorskin descriptors */
static const value_t actorskin_vals[] = {
	{"name", V_STRING, offsetof(actorSkin_t, name), 0},
	{"singleplayer", V_BOOL, offsetof(actorSkin_t, singleplayer), MEMBER_SIZEOF(actorSkin_t, singleplayer)},
	{"multiplayer", V_BOOL, offsetof(actorSkin_t, multiplayer), MEMBER_SIZEOF(actorSkin_t, multiplayer)},

	{nullptr, V_NULL, 0, 0}
};


static void CL_ParseActorSkin (const char* name, const char** text)
{
	/* NOTE: first skin is special cause we don't get the skin with suffix */
	if (CL_GetActorSkinCount() == 0) {
		if (!Q_streq(name, "default") != 0) {
			Com_Error(ERR_DROP, "CL_ParseActorSkin: First actorskin read from script must be \"default\" skin.");
		}
	}

	actorSkin_t* skin = CL_AllocateActorSkin(name);

	Com_ParseBlock(name, text, skin, actorskin_vals, nullptr);
}

/**
 * @sa FS_MapDefSort
 */
static int Com_MapDefSort (const void* mapDef1, const void* mapDef2)
{
	const char* map1 = ((const mapDef_t*)mapDef1)->mapTheme;
	const char* map2 = ((const mapDef_t*)mapDef2)->mapTheme;

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
	if (sv_dedicated->integer)
		return;

	/* start the music track already while precaching data */
	S_Frame();
	S_LoadSamples();

	/* preload all models for faster access */
	CL_ViewPrecacheModels();
	CL_LanguageInit();

	CLMN_Init();

	/* sort the mapdef array */
	qsort(csi.mds, csi.numMDs, sizeof(mapDef_t), Com_MapDefSort);
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
 * @return True if the parsing function succeeded.
 */
bool CL_ParseClientData (const char* type, const char* name, const char** text)
{
#ifndef COMPILE_UNITTESTS
	static int progressCurrent = 0;

	progressCurrent++;
	if (progressCurrent % 10 == 0)
		SCR_DrawLoadingScreen(false, std::min(progressCurrent * 30 / 1500, 30));
#endif

	if (Q_streq(type, "window"))
		return UI_ParseWindow(type, name, text);
	else if (Q_streq(type, "component"))
		return UI_ParseComponent(type, name, text);
	else if (Q_streq(type, "particle"))
		CL_ParseParticle(name, text);
	else if (Q_streq(type, "language"))
		CL_ParseLanguages(name, text);
	else if (Q_streq(type, "font"))
		return UI_ParseFont(name, text);
	else if (Q_streq(type, "tutorial"))
		TUT_ParseTutorials(name, text);
	else if (Q_streq(type, "menu_model"))
		return UI_ParseUIModel(name, text);
	else if (Q_streq(type, "sprite"))
		return UI_ParseSprite(name, text);
	else if (Q_streq(type, "sequence"))
		CL_ParseSequence(name, text);
	else if (Q_streq(type, "music"))
		M_ParseMusic(name, text);
	else if (Q_streq(type, "actorskin"))
		CL_ParseActorSkin(name, text);
	else if (Q_streq(type, "cgame"))
		GAME_ParseModes(name, text);
	else if (Q_streq(type, "tip"))
		CL_ParseTipOfTheDay(name, text);
	else if (Q_streq(type, "lua"))
		return UI_ParseAndLoadLuaScript(name, text);

	return true;
}

/**
 * @brief Print the configstrings to game console
 * @sa SV_PrintConfigStrings_f
 */
static void CL_ShowConfigstrings_f (void)
{
	for (int i = 0; i < MAX_CONFIGSTRINGS; i++) {
		const char* configString;
		/* CS_TILES and CS_POSITIONS can stretch over multiple configstrings,
		 * so don't print the middle parts */
		if (i > CS_TILES && i < CS_POSITIONS)
			continue;
		if (i > CS_POSITIONS && i < CS_MODELS)
			continue;

		configString = CL_GetConfigString(i);
		if (configString[0] == '\0')
			continue;
		Com_Printf("configstring[%3i]: %s\n", i, configString);
	}
}

/**
 * @brief Opens the specified URL and minimizes the game window. You have to specify the whole
 * url including the protocol.
 */
static void CL_OpenURL_f (void)
{
	if (Cmd_Argc() != 2) {
		Com_Printf("usage: %s <url>\n", Cmd_Argv(0));
		return;
	}

	VID_Minimize();
	Sys_OpenURL(Cmd_Argv(1));
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
	cl_fps = Cvar_Get("cl_fps", "0", CVAR_ARCHIVE, "Show frames per second");
	cl_log_battlescape_events = Cvar_Get("cl_log_battlescape_events", "1", 0, "Log all battlescape events to events.log");
	cl_selected = Cvar_Get("cl_selected", "0", CVAR_NOSET, "Current selected soldier");
	cl_connecttimeout = Cvar_Get("cl_connecttimeout", "25000", CVAR_ARCHIVE, "Connection timeout for multiplayer connects");
	/* userinfo */
	cl_name = Cvar_Get("cl_name", Sys_GetCurrentUser(), CVAR_USERINFO | CVAR_ARCHIVE, "Playername");
	cl_teamnum = Cvar_Get("cl_teamnum", "1", CVAR_USERINFO | CVAR_ARCHIVE, "Preferred teamnum for multiplayer teamplay games");
	cl_ready = Cvar_Get("cl_ready", "0", CVAR_USERINFO, "Ready indicator in the userinfo for tactical missions");
	cl_msg = Cvar_Get("cl_msg", "2", CVAR_USERINFO | CVAR_ARCHIVE, "Sets the message level for server messages the client receives");
	sv_maxclients = Cvar_Get("sv_maxclients", "1", CVAR_SERVERINFO, "If sv_maxclients is 1 we are in singleplayer - otherwise we are multiplayer mode (see sv_teamplay)");

	masterserver_url = Cvar_Get("masterserver_url", MASTER_SERVER, CVAR_ARCHIVE, "URL of UFO:AI masterserver");

	cl_map_debug = Cvar_Get("debug_map", "0", 0, "Activate realtime map debugging options - bitmask. Valid values are 0, 1, 3 and 7");
	cl_le_debug = Cvar_Get("debug_le", "0", 0, "Activates some local entity debug rendering");
	cl_trace_debug = Cvar_Get("debug_trace", "0", 0, "Activates some client side trace debug rendering");
	cl_leshowinvis = Cvar_Get("cl_leshowinvis", "0", CVAR_ARCHIVE, "Show invisible local entities as null models");

	/* register our commands */
	Cmd_AddCommand("targetalign", CL_ActorTargetAlign_f, N_("Target your shot to the ground"));

	Cmd_AddCommand("cl_setratiofilter", CL_SetRatioFilter_f, "Filter the resolution screen list with a ratio");

	Cmd_AddCommand("cmd", CL_ForwardToServer_f, "Forward to server");
	Cmd_AddCommand("cl_userinfo", CL_UserInfo_f, "Prints your userinfo string");
#ifdef ACTIVATE_PACKET_COMMAND
	/* this is dangerous to leave in */
	Cmd_AddCommand("packet", CL_Packet_f, "Dangerous debug function for network testing");
#endif
	Cmd_AddCommand("quit", CL_Quit_f, "Quits the game");
	Cmd_AddCommand("env", CL_Env_f);

	Cmd_AddCommand(CL_PRECACHE, CL_Precache_f, "Function that is called at mapload to precache map data");
	Cmd_AddCommand(CL_SPAWNSOLDIERS, CL_SpawnSoldiers_f, "Spawns the soldiers for the selected teamnum");
	Cmd_AddCommand(CL_STARTMATCH, CL_StartMatch_f, "Start the match once every player is ready");
	Cmd_AddCommand("cl_configstrings", CL_ShowConfigstrings_f, "Print client configstrings to game console");
	Cmd_AddCommand("cl_openurl", CL_OpenURL_f, "Opens the given url in a browser");

	/* forward to server commands
	 * the only thing this does is allow command completion
	 * to work -- all unknown commands are automatically
	 * forwarded to the server */
	Cmd_AddCommand("say", nullptr, "Chat command");
	Cmd_AddCommand("say_team", nullptr, "Team chat command");
	Cmd_AddCommand("players", nullptr, "List of team and player name");
#ifdef DEBUG
	Cmd_AddCommand("debug_cgrid", Grid_DumpWholeClientMap_f, "Shows the whole client side pathfinding grid of the current loaded map");
	Cmd_AddCommand("debug_croute", Grid_DumpClientRoutes_f, "Shows the whole client side pathfinding grid of the current loaded map");
	Cmd_AddCommand("debug_listle", LE_List_f, "Shows a list of current know local entities with type and status");
	Cmd_AddCommand("debug_listlm", LM_List_f, "Shows a list of current know local models");
	/* forward commands again */
	Cmd_AddCommand("debug_edictdestroy", nullptr, "Call the 'destroy' function of a given edict");
	Cmd_AddCommand("debug_edictuse", nullptr, "Call the 'use' function of a given edict");
	Cmd_AddCommand("debug_edicttouch", nullptr, "Call the 'touch' function of a given edict");
	Cmd_AddCommand("debug_killteam", nullptr, "Kills a given team");
	Cmd_AddCommand("debug_stunteam", nullptr, "Stuns a given team");
	Cmd_AddCommand("debug_listscore", nullptr, "Shows mission-score entries of all team members");
	Cmd_AddCommand("debug_statechange", nullptr, "Change the state of an edict");
#endif

	IN_Init();
	CL_ServerEventsInit();
	CL_CameraInit();
	CL_BattlescapeRadarInit();

	UI_Init();
	TUT_InitStartup();
	PTL_InitStartup();
	GAME_InitStartup();
	WEB_InitStartup();
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
	if (cls.state < ca_connected)
		return;
	if (!Com_IsUserinfoModified())
		return;
	char info[MAX_INFO_STRING];
	const char* userInfo = Cvar_Userinfo(info, sizeof(info));
	dbuffer msg(strlen(userInfo) + 2);
	NET_WriteByte(&msg, clc_userinfo);
	NET_WriteString(&msg, userInfo);
	NET_WriteMsg(cls.netStream, msg);
	Com_SetUserinfoModified(false);
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
				SCR_DrawLoading(100);
			}
		}
		break;
	default:
		break;
	}
}

int CL_GetClientState (void)
{
	return cls.state;
}

/**
 * @brief Sets the client state
 */
void CL_SetClientState (connstate_t state)
{
	Com_DPrintf(DEBUG_CLIENT, "CL_SetClientState: Set new state to %i (old was: %i)\n", state, cls.state);
	cls.state = state;

	switch (cls.state) {
	case ca_uninitialized:
		Com_Error(ERR_FATAL, "CL_SetClientState: Don't set state ca_uninitialized\n");
		break;
	case ca_active:
		cls.waitingForStart = 0;
		break;
	case ca_connecting:
		cls.reconnectTime = 0;
		CL_Connect();
		break;
	case ca_disconnected:
		cls.waitingForStart = 0;
		break;
	case ca_connected:
		/* wipe the client_state_t struct */
		CL_ClearState();
		Cvar_Set("cl_ready", "0");
		break;
	default:
		break;
	}
}

/**
 * @sa Qcommon_Frame
 */
void CL_Frame (int now, void* data)
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

	if (cls.state == ca_active)
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
void CL_SlowFrame (int now, void* data)
{
	/* language */
	if (s_language->modified) {
		CL_LanguageTryToSet(s_language->string);
	}

	Irc_Logic_Frame();

	CL_Reconnect();

	HUD_Update();
}

static void CL_InitMemPools (void)
{
	cl_genericPool = Mem_CreatePool("Client: Generic");
}

static void CL_RContextCvarChange (const char* cvarName, const char* oldValue, const char* newValue, void* data)
{
	UI_DisplayNotice(_("This change requires a restart!"), 2000, nullptr);
}

static void CL_RImagesCvarChange (const char* cvarName, const char* oldValue, const char* newValue, void* data)
{
	UI_DisplayNotice(_("This change might require a restart."), 2000, nullptr);
}

/**
 * @sa CL_Shutdown
 * @sa CL_InitAfter
 */
void CL_Init (void)
{
	/* i18n through gettext */
	char languagePath[MAX_OSPATH];
	cvar_t* fs_i18ndir;

	isdown = false;

	if (sv_dedicated->integer)
		return;					/* nothing running on the client */

	OBJZERO(cls);

	fs_i18ndir = Cvar_Get("fs_i18ndir", "", 0, "System path to language files");
	/* i18n through gettext */
	setlocale(LC_ALL, "C");

// MSVC 2013 do not accept LC_MESSAGES
#ifndef _MSC_VER
	setlocale(LC_MESSAGES, "");
#endif

	/* use system locale dir if we can't find in gamedir */
	if (fs_i18ndir->string[0] != '\0')
		Q_strncpyz(languagePath, fs_i18ndir->string, sizeof(languagePath));
	else
#ifdef LOCALEDIR
		Com_sprintf(languagePath, sizeof(languagePath), LOCALEDIR);
#else
		Com_sprintf(languagePath, sizeof(languagePath), "%s/" BASEDIRNAME "/i18n/", FS_GetCwd());
#endif
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
	SCR_DrawLoadingScreen(false, 0);
	S_Init();
	SCR_Init();

	CL_InitLua();
	CL_InitLocal();

	Irc_Init();
	CL_ViewInit();

	CL_ClearState();

	/* cvar feedback */
	for (const cvar_t* var = Cvar_GetFirst(); var; var = var->next) {
		if (var->flags & CVAR_R_CONTEXT)
			Cvar_RegisterChangeListener(var->name, CL_RContextCvarChange);
		if (var->flags & CVAR_R_IMAGES)
			Cvar_RegisterChangeListener(var->name, CL_RImagesCvarChange);
	}
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
	if (isdown) {
		printf("recursive shutdown\n");
		return;
	}
	isdown = true;

	/* remove cvar feedback */
	for (const cvar_t* var = Cvar_GetFirst(); var; var = var->next) {
		if (var->flags & CVAR_R_CONTEXT)
			Cvar_UnRegisterChangeListener(var->name, CL_RContextCvarChange);
		if (var->flags & CVAR_R_IMAGES)
			Cvar_UnRegisterChangeListener(var->name, CL_RImagesCvarChange);
	}

	CLMN_Shutdown();
	GAME_SetMode(nullptr);
	GAME_UnloadGame();
	CL_HTTP_Cleanup();
	Irc_Shutdown();
	Con_SaveConsoleHistory();
	Key_WriteBindings("keys.cfg");
	S_Shutdown();
	R_Shutdown();
	UI_Shutdown();
	CL_ShutdownLua();
	CIN_Shutdown();
	SEQ_Shutdown();
	GAME_Shutdown();
	CL_LanguageShutdown();
	TOTD_Shutdown();
	SCR_Shutdown();
}
