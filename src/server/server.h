/**
 * @file server.h
 * @brief Main server include file.
 */

/*
All original materal Copyright (C) 2002-2006 UFO: Alien Invasion team.

26/06/06, Eddy Cullen (ScreamingWithNoSound):
	Reformatted to agreed style.
	Added doxygen file comment.
	Updated copyright notice.
	Added inclusion guard.

Original file from Quake 2 v3.21: quake2-2.31/server/server.h
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

#ifndef SERVER_SERVER_H
#define SERVER_SERVER_H

#include "../qcommon/qcommon.h"

/*============================================================================= */

#define	MAX_MASTERS	8			/* max recipients for heartbeat packets */

typedef enum {
	ss_dead,					/* no map loaded */
	ss_loading,					/* spawning level edicts */
	ss_game,					/* actively running */
	ss_cinematic,
	ss_demo,
	ss_pic
} server_state_t;

/* some qc commands are only valid before the server has finished */
/* initializing (precache commands, static sounds / objects, etc) */

typedef struct {
	server_state_t state;		/* precache commands are only valid during load */

	qboolean active;			/* false if only a net client */

	qboolean attractloop;		/* running cinematics and demos for the local system only */
	qboolean loadgame;			/* client begins should reuse existing entity */

	unsigned time;				/* always sv.framenum * 100 msec */
	int framenum;

	char name[MAX_QPATH];		/* map name, or cinematic name */
	struct cmodel_s *models[MAX_MODELS];

	char configstrings[MAX_CONFIGSTRINGS][MAX_TOKEN_CHARS];

	/* the multicast buffer is used to send a message to a set of clients */
	/* it is only used to marshall data until SV_Multicast is called */
	sizebuf_t multicast;
	byte multicast_buf[MAX_MSGLEN];

	/* demo server information */
	FILE *demofile;
	qboolean timedemo;			/* don't time sync */
} server_t;

#define EDICT_NUM(n) ((edict_t *)((byte *)ge->edicts + ge->edict_size*(n)))
#define NUM_FOR_EDICT(e) ( ((byte *)(e)-(byte *)ge->edicts ) / ge->edict_size)

#define PLAYER_NUM(n) ((player_t *)((byte *)ge->players + ge->player_size*(n)))
#define NUM_FOR_PLAYER(e) ( ((byte *)(e)-(byte *)ge->players ) / ge->player_size)

typedef enum {
	cs_free,					/* can be reused for a new connection */
	cs_zombie,					/* client has been disconnected, but don't reuse */
	/* connection for a couple seconds */
	cs_connected,				/* has been assigned to a client_t, but not in game yet */
	cs_spawned					/* client is fully in game */
} client_state_t;

#define	LATENCY_COUNTS	16
#define	RATE_MESSAGES	10

#define RELIABLEBUFFERS	16

typedef struct client_s {
	client_state_t state;

	int lastframe;				/* for delta compression */

	int commandMsec;			/* every seconds this is reset, if user */
	/* commands exhaust it, assume time cheating */

	int frame_latency[LATENCY_COUNTS];
	int ping;

	int message_size[RATE_MESSAGES];	/* used to rate drop packets */
	int rate;
	int surpressCount;			/* number of messages rate supressed */

	char userinfo[MAX_INFO_STRING];

	player_t *player;			/* game client structure */
	char name[32];				/* extracted from userinfo, high bits masked */
	int messagelevel;			/* for filtering printed messages */

	/* The datagram is written to by sound calls, prints, temp ents, etc. */
	/* It can be harmlessly overflowed. */
	sizebuf_t datagram;
	byte datagram_buf[MAX_MSGLEN];

	int lastmessage;			/* sv.framenum when packet was last received */
	int lastconnect;

	int challenge;				/* challenge of this user, randomly generated */

	int curMsg;
	int addMsg;
	sizebuf_t reliable[RELIABLEBUFFERS];
	byte reliable_buf[RELIABLEBUFFERS * MAX_MSGLEN];

	netchan_t netchan;
} client_t;

/* a client can leave the server in one of four ways: */
/* dropping properly by quiting or disconnecting */
/* timing out if no valid messages are received for timeout.value seconds */
/* getting kicked off by the server operator */
/* a program error, like an overflowed reliable buffer */

/*============================================================================= */

/* MAX_CHALLENGES is made large to prevent a denial */
/* of service attack that could cycle all of them */
/* out before legitimate users connected */
#define	MAX_CHALLENGES	1024

typedef struct {
	netadr_t adr;
	int challenge;
	int time;
} challenge_t;


typedef struct {
	qboolean initialized;		/* sv_init has completed */
	int realtime;				/* always increasing, no clamping, etc */

	char mapcmd[MAX_TOKEN_CHARS];	/* ie: *intro.cin+base  */

	int spawncount;				/* incremented each server start */
	/* used to check late spawns */

	client_t *clients;			/* [maxclients->value]; */
	int num_client_entities;	/* maxclients->value*UPDATE_BACKUP*MAX_PACKET_ENTITIES */
	int next_client_entities;	/* next client_entity to use */

	int last_heartbeat;

	challenge_t challenges[MAX_CHALLENGES];	/* to prevent invalid IPs from connecting */

	/* serverrecord values */
	FILE *demofile;
	sizebuf_t demo_multicast;
	byte demo_multicast_buf[MAX_MSGLEN];
} server_static_t;

/*============================================================================= */

extern netadr_t net_from;

/*extern	sizebuf_t	net_message; */

extern netadr_t master_adr[MAX_MASTERS];	/* address of the master server */

extern server_static_t svs;		/* persistant server info */
extern server_t sv;				/* local server */

extern cvar_t *sv_paused;
extern cvar_t *sv_noreload;		/* don't reload level state when reentering */
					/* development tool */
extern cvar_t *sv_enforcetime;

extern client_t *sv_client;
extern player_t *sv_player;

/*=========================================================== */

/* sv_main.c */
void SV_FinalMessage(char *message, qboolean reconnect);
void SV_DropClient(client_t * drop);

int SV_ModelIndex(char *name);
int SV_SoundIndex(char *name);
int SV_ImageIndex(char *name);

void SV_WriteClientdataToMessage(client_t * client, sizebuf_t * msg);

void SV_InitOperatorCommands(void);

void SV_SendServerinfo(client_t * client);
void SV_UserinfoChanged(client_t * cl);

void SV_SpawnServer(char *server, char *param, server_state_t serverstate, qboolean attractloop, qboolean loadgame);

void Master_Heartbeat(void);
void Master_Packet(void);

/* sv_init.c */
void SV_InitGame(void);
void SV_Map(qboolean attractloop, char *levelstring, qboolean loadgame);


/* sv_phys.c */
void SV_PrepWorldFrame(void);

/* sv_send.c */
typedef enum { RD_NONE, RD_CLIENT, RD_PACKET } redirect_t;

#define	SV_OUTPUTBUF_LENGTH	(MAX_MSGLEN - 16)

extern char sv_outputbuf[SV_OUTPUTBUF_LENGTH];

void SV_FlushRedirect(int sv_redirected, char *outputbuf);

void SV_DemoCompleted(void);
void SV_SendClientMessages(void);

void SV_Multicast(int mask);
void SV_StartSound(vec3_t origin, edict_t * entity, int channel, int soundindex, float volume, float attenuation, float timeofs);
void SV_ClientPrintf(client_t * cl, int level, char *fmt, ...);
void SV_BroadcastPrintf(int level, char *fmt, ...);
void SV_BroadcastCommand(char *fmt, ...);

/* sv_user.c */
void SV_ExecuteClientMessage(client_t * cl);

/* sv_ccmds.c */
void SV_ReadLevelFile(void);
void SV_Status_f(void);

/* sv_ents.c */
void SV_WriteFrameToClient(client_t * client, sizebuf_t * msg);
void SV_RecordDemoMessage(void);
void SV_BuildClientFrame(client_t * client);


void SV_Error(char *error, ...);

/* sv_game.c */
extern game_export_t *ge;

void SV_InitGameProgs(void);
void SV_ShutdownGameProgs(void);
void SV_InitEdict(edict_t * e);



/*============================================================ */

void SV_ClearWorld(void);

void SV_UnlinkEdict(edict_t * ent);
void SV_LinkEdict(edict_t * ent);

int SV_AreaEdicts(vec3_t mins, vec3_t maxs, edict_t ** list, int maxcount, int areatype);

/*=================================================================== */

/* returns the CONTENTS_* value from the world at the given point. */
/* Quake 2 extends this to also check entities, to allow moving liquids */

trace_t SV_Trace(vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end, edict_t * passedict, int contentmask);

#endif /* SERVER_SERVER_H */
