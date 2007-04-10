/**
 * @file sv_user.c
 * @brief Server code for moving users.
 */

/*
All original materal Copyright (C) 2002-2007 UFO: Alien Invasion team.

26/06/06, Eddy Cullen (ScreamingWithNoSound):
	Reformatted to agreed style.
	Added doxygen file comment.
	Updated copyright notice.

Original file from Quake 2 v3.21: quake2-2.31/server/sv_users.c
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

player_t *sv_player;

/*
============================================================
USER STRINGCMD EXECUTION

sv_client and sv_player will be valid.
============================================================
*/

/**
 * @brief
 */
void SV_BeginDemoserver (void)
{
	char name[MAX_OSPATH];

	Com_sprintf(name, sizeof(name), "demos/%s", sv.name);
	FS_FOpenFile(name, &sv.demofile);
	if (!sv.demofile.f)
		Com_Error(ERR_DROP, "Couldn't open %s\n", name);
}

/**
 * @brief Sends the first message from the server to a connected client.
 * This will be sent on the initial connection and upon each server load.
 * Client reads via CL_ParseServerData in cl_parse.c
 */
static void SV_New_f (void)
{
	char *gamedir;
	int playernum;

/*	edict_t		*ent; */

	Com_DPrintf("New() from %s\n", sv_client->name);

	if (sv_client->state != cs_connected) {
		if (sv_client->state == cs_spawning) {
			/* client typed 'reconnect/new' while connecting. */
			MSG_WriteByte(&sv_client->netchan.message, svc_stufftext);
			MSG_WriteString (&sv_client->netchan.message, "\ndisconnect\nreconnect\n");
			SV_DropClient (sv_client);
		} else
			Com_DPrintf ("WARNING: Illegal 'new' from %s, client state %d. This shouldn't happen...\n", sv_client->name, sv_client->state);
		return;
	}

	/* demo servers just dump the file message */
	if (sv.state == ss_demo) {
		SV_BeginDemoserver();
		return;
	}

	/* client state to prevent multiple new from causing high cpu / overflows. */
	sv_client->state = cs_spawning;

	/* fix for old clients that don't respond to stufftext due to pending cmd buffer */
	MSG_WriteByte(&sv_client->netchan.message, svc_stufftext);
	MSG_WriteString(&sv_client->netchan.message, "\n");

	/* serverdata needs to go over for all types of servers */
	/* to make sure the protocol is right, and to set the gamedir */
	gamedir = Cvar_VariableString("fs_gamedir");

	/* send the serverdata */
	MSG_WriteByte(&sv_client->netchan.message, svc_serverdata);
	MSG_WriteLong(&sv_client->netchan.message, PROTOCOL_VERSION);
	MSG_WriteLong(&sv_client->netchan.message, svs.spawncount);
	MSG_WriteByte(&sv_client->netchan.message, sv.attractloop);
	MSG_WriteString(&sv_client->netchan.message, gamedir);

	if (sv.state == ss_pic)
		playernum = -1;
	else
		playernum = sv_client - svs.clients;
	MSG_WriteShort(&sv_client->netchan.message, playernum);

	/* send full levelname */
	MSG_WriteString(&sv_client->netchan.message, sv.configstrings[CS_NAME]);

	/* game server */
	if (sv.state == ss_game) {
		/* begin fetching configstrings */
		MSG_WriteByte(&sv_client->netchan.message, svc_stufftext);
		MSG_WriteString(&sv_client->netchan.message, va("cmd configstrings %i 0\n", svs.spawncount));
	}
}

/**
 * @brief Send the configstrings to the client
 */
static void SV_Configstrings_f (void)
{
	int start;

	Com_DPrintf("Configstrings() from %s\n", sv_client->name);

	if (sv_client->state != cs_spawning) {
		Com_Printf("configstrings not valid -- already spawning\n");
		return;
	}

	/* handle the case of a level changing while a client was connecting */
	if (atoi(Cmd_Argv(1)) != svs.spawncount) {
		Com_Printf("SV_Configstrings_f from different level\n");
		SV_New_f();
		return;
	}

	start = atoi(Cmd_Argv(2));

	/* write a packet full of data */
	while (sv_client->netchan.message.cursize < MAX_MSGLEN / 2 && start < MAX_CONFIGSTRINGS) {
		if (sv.configstrings[start][0]) {
			MSG_WriteByte(&sv_client->netchan.message, svc_configstring);
			MSG_WriteShort(&sv_client->netchan.message, start);
			MSG_WriteString(&sv_client->netchan.message, sv.configstrings[start]);
		}
		start++;
	}

	/* send next command */
	if (start == MAX_CONFIGSTRINGS) {
		MSG_WriteByte(&sv_client->netchan.message, svc_stufftext);
		MSG_WriteString(&sv_client->netchan.message, va("precache %i\n", svs.spawncount));
	} else {
		MSG_WriteByte(&sv_client->netchan.message, svc_stufftext);
		MSG_WriteString(&sv_client->netchan.message, va("cmd configstrings %i %i\n", svs.spawncount, start));
	}
}

/**
 * @brief Returns the number of spawned players
 * @sa SV_ShutdownWhenEmpty
 */
int SV_CountPlayers (void)
{
	int i;
	int count = 0;
	client_t *cl;

	if (!svs.initialized)
		return 0;

	for (i = 0, cl = svs.clients; i < sv_maxclients->integer; i++, cl++) {
		if (cl->state != cs_spawned)
			continue;

		count++;
	}

	return count;
}

/**
 * @brief
 * @sa SV_Spawn_f
 */
static void SV_Begin_f (void)
{
	Com_DPrintf("Begin() from %s\n", sv_client->name);

	/* could be abused to respawn or cause spam/other mod-specific problems */
	if (sv_client->state != cs_spawning) {
		Com_Printf("EXPLOIT: Illegal 'begin' from %s (already spawned), client dropped.\n", sv_client->name);
		SV_DropClient(sv_client);
		return;
	}

	/* handle the case of a level changing while a client was connecting */
	if (atoi(Cmd_Argv(1)) != svs.spawncount) {
		Com_Printf("SV_Begin_f from different level\n");
		SV_New_f();
		return;
	}

	/* call the game begin function */
	ge->ClientBegin(sv_player);

	Cbuf_InsertFromDefer();
}

/**
 * @brief Spawns all connected clients if they are not already spawned
 * @sa CL_SpawnSoldiers_f
 */
void SV_SpawnAllPending (void)
{
	int i;
	client_t *cl;

	if (!svs.initialized)
		return;

 	/* TODO: how to handle spectators (skip spawning them?) */

	for (i = 0, cl = svs.clients; i < sv_maxclients->integer; i++, cl++) {
		if (cl && cl->state == cs_spawning) {
			if (ge->ClientSpawn(cl->player))
				cl->state = cs_spawned;
		}
	}
}

/**
 * @brief
 * @sa SV_Begin_f
 */
static void SV_Spawn_f (void)
{
	Com_DPrintf("Spawn() from %s\n", sv_client->name);

	/* handle the case of a level changing while a client was connecting */
	if (atoi(Cmd_Argv(1)) != svs.spawncount) {
		Com_Printf("SV_Spawn_f from different level\n");
		SV_New_f();
		return;
	}

	/* try and spawn any connected but unspawned players */
	SV_SpawnAllPending();

	Cbuf_InsertFromDefer();
}

/*============================================================================ */

/**
 * @brief The client is going to disconnect, so remove the connection immediately
 */
static void SV_Disconnect_f (void)
{
/*	SV_EndRedirect (); */
	SV_DropClient(sv_client);
}


/**
 * @brief Dumps the serverinfo info string
 */
static void SV_ShowServerinfo_f (void)
{
	Info_Print(Cvar_Serverinfo());
}


/**
 * @brief This variable holds the name of the next alias to be executed.
 * A looping series of aliases is created which look something like
 * 'alias l1 "somecommandhere; set nextserver l2"'
 * and
 * 'alias l2 "someothercommandhere; set nextserver l1"'.
 * These two aliases would loop the commands until a key is pressed.
 */
static void SV_Nextserver (void)
{
	char *v;

	/* can't nextserver while playing a normal game */
	if (sv.state == ss_game)
		return;

	svs.spawncount++;			/* make sure another doesn't sneak in */
	v = Cvar_VariableString("nextserver");
	if (!v[0])
		Cbuf_AddText("killserver\n");
	else {
		Cbuf_AddText(v);
		Cbuf_AddText("\n");
	}
	Cvar_Set("nextserver", "");
}

/**
 * @brief A cinematic has completed or been aborted by a client, so move to the next server,
 */
static void SV_Nextserver_f (void)
{
	if (atoi(Cmd_Argv(1)) != svs.spawncount) {
		Com_DPrintf("Nextserver() from wrong level, from %s\n", sv_client->name);
		return;					/* leftover from last server */
	}

	Com_DPrintf("Nextserver() from %s\n", sv_client->name);

	SV_Nextserver();
}

typedef struct {
	char *name;
	void (*func) (void);
} ucmd_t;

static const ucmd_t ucmds[] = {
	/* auto issued */
	{"new", SV_New_f},
	{"configstrings", SV_Configstrings_f},
	{"begin", SV_Begin_f},
	{"spawn", SV_Spawn_f},

	{"nextserver", SV_Nextserver_f},

	{"disconnect", SV_Disconnect_f},

	/* issued by hand at client consoles */
	{"info", SV_ShowServerinfo_f},

	{NULL, NULL}
};

/**
 * @brief
 */
static void SV_ExecuteUserCommand (char *s)
{
	const ucmd_t *u;

	Cmd_TokenizeString(s, qfalse);
	sv_player = sv_client->player;

/*	SV_BeginRedirect (RD_CLIENT); */

	for (u = ucmds; u->name; u++)
		if (!Q_strncmp(Cmd_Argv(0), u->name, MAX_VAR)) {
			u->func();
			break;
		}

	if (!u->name && sv.state == ss_game)
		ge->ClientCommand(sv_player);

/*	SV_EndRedirect (); */
}


#define	MAX_STRINGCMDS	8
/**
 * @brief The current net_message is parsed for the given client
 */
void SV_ExecuteClientMessage (client_t * cl)
{
	int c;
	char *s;

	/* only allow one move command */
	int stringCmdCount = 0;

	sv_client = cl;
	sv_player = sv_client->player;

	while (1) {
		if (net_message.readcount > net_message.cursize) {
			Com_Printf("SV_ExecuteClientMessage: badread, %d > %d\n", net_message.readcount, net_message.cursize);
			SV_DropClient(cl);
			return;
		}

		c = MSG_ReadByte(&net_message);
		if (c == -1)
			break;

		switch (c) {
		default:
			Com_Printf("SV_ExecuteClientMessage: unknown command char '%d'\n", c);
			SV_DropClient(cl);
			return;

		case clc_nop:
			break;

		case clc_userinfo:
			Q_strncpyz(cl->userinfo, MSG_ReadString(&net_message), sizeof(cl->userinfo));
			SV_UserinfoChanged(cl);
			break;

		case clc_stringcmd:
			s = MSG_ReadString(&net_message);

			/* malicious users may try using too many string commands */
			if (++stringCmdCount < MAX_STRINGCMDS)
				SV_ExecuteUserCommand(s);

			if (cl->state == cs_zombie)
				return;			/* disconnect command */
			break;

		case clc_action:
			/* client actions are handled by the game module */
			ge->ClientAction(sv_player);
			break;

		case clc_endround:
			/* player wants to end round */
			ge->ClientEndRound(sv_player, NOISY);
			break;

		case clc_teaminfo:
			/* player sends team info */
			/* actors spawn accordingly */
			ge->ClientTeamInfo(sv_player);
			break;
		}
	}
}
