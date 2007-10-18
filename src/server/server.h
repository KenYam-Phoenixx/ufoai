/**
 * @file server.h
 * @brief Main server include file.
 */

/*
All original materal Copyright (C) 2002-2007 UFO: Alien Invasion team.

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

#include "../common/common.h"
#include "../shared/infostring.h"

extern struct memPool_s *sv_gameSysPool;
extern struct memPool_s *sv_genericPool;

/*============================================================================= */

typedef enum {
	ss_dead,					/**< no map loaded */
	ss_loading,					/**< spawning level edicts */
	ss_game						/**< actively running */
} server_state_t;

/** some qc commands are only valid before the server has finished
 * initializing (precache commands, static sounds / objects, etc) */

typedef struct {
	server_state_t state;		/**< precache commands are only valid during load */

	int framenum;

	char name[MAX_QPATH];		/**< map name, or cinematic name */
	char assembly[MAX_QPATH];		/**< random map assembly name */
	struct cBspModel_s *models[MAX_MODELS];

	char configstrings[MAX_CONFIGSTRINGS][MAX_TOKEN_CHARS];
} server_t;

#define EDICT_NUM(n) ((edict_t *)((byte *)ge->edicts + ge->edict_size * (n)))
#define NUM_FOR_EDICT(e) (((byte *)(e) - (byte *)ge->edicts) / ge->edict_size)

#define PLAYER_NUM(n) ((player_t *)((byte *)ge->players + ge->player_size * (n)))
#define NUM_FOR_PLAYER(e) (((byte *)(e) - (byte *)ge->players) / ge->player_size)

typedef enum {
	cs_free,					/**< can be reused for a new connection */
	cs_connected,				/**< has been assigned to a client_t, but not in game yet */
	cs_spawning,				/**< received new, not begin yet */
	cs_spawned					/**< client is fully in game */
} client_state_t;

typedef struct client_s {
	client_state_t state;
	int lastframe;				/**< for delta compression */
	char userinfo[MAX_INFO_STRING];
	player_t *player;			/**< game client structure */
	char name[32];				/**< extracted from userinfo, high bits masked */
	int messagelevel;			/**< for filtering printed messages */
	int lastconnect;
	char peername[256];
	struct net_stream *stream;
} client_t;

/**
 * a client can leave the server in one of four ways:
 * dropping properly by quiting or disconnecting
 * timing out if no valid messages are received
 * getting kicked off by the server operator
 * a program error, like an overflowed reliable buffer
 */

/*============================================================================= */


typedef struct {
	qboolean initialized;		/**< sv_init has completed */
	int realtime;				/**< always increasing, no clamping, etc */
	struct datagram_socket *datagram_socket;
	int spawncount;				/**< incremented each server start - used to check late spawns */
	client_t *clients;			/**< [sv_maxclients->value]; */
	int last_heartbeat;
} server_static_t;

/**
 * @brief map cycle list element
*/
typedef struct mapcycle_s {
	char *map;			/**< map name */
	char *type;			/**< gametype to play on this map */
	struct mapcycle_s* next;	/**< pointer to the next map in cycle */
} mapcycle_t;

extern mapcycle_t *mapcycleList;	/**< map cycle linked list */
extern int mapcycleCount;		/**< number of maps in the cycle */

/*============================================================================= */

/*extern	sizebuf_t	net_message; */

extern server_static_t svs;		/**< persistant server info */
extern server_t sv;				/**< local server */

extern cvar_t *sv_mapname;
extern cvar_t *sv_public;			/**< should heartbeats be sent */

extern client_t *sv_client;
extern player_t *sv_player;
extern struct dbuffer *sv_msg;

/*=========================================================== */

/* sv_main.c */
void SV_DropClient(client_t *drop, const char *message);

int SV_ModelIndex(const char *name);

void SV_InitOperatorCommands(void);

void SV_UserinfoChanged(client_t *cl);

#define	HEARTBEAT_SECONDS	300
void Master_Heartbeat(void);

void SV_NextMapcycle(void);
void SV_MapcycleClear(void);
void SV_MapcycleAdd(const char* mapName, const char* gameType);

void SV_ReadPacket(struct net_stream *s);

/* sv_init.c */
void SV_Map(const char *levelstring, const char *assembly);

void SV_Multicast(int mask, struct dbuffer *msg);
void SV_StartSound(int mask, vec3_t origin, edict_t *entity, const char* sound, int channel, float volume, float attenuation);
void SV_ClientCommand(client_t *client, const char *fmt, ...) __attribute__((format(printf,2,3)));
void SV_ClientPrintf(client_t * cl, int level, const char *fmt, ...) __attribute__((format(printf,3,4)));
void SV_BroadcastPrintf(int level, const char *fmt, ...) __attribute__((format(printf,2,3)));

/* sv_user.c */
void SV_ExecuteClientMessage(client_t * cl, int cmd, struct dbuffer *msg);
int SV_CountPlayers(void);
void SV_SetClientState(client_t* client, int state);

/* sv_ccmds.c */
void SV_SetMaster_f(void);

/* sv_game.c */
extern game_export_t *ge;

void SV_InitGameProgs(void);
void SV_ShutdownGameProgs(void);

/*============================================================ */

void SV_ClearWorld(void);

void SV_UnlinkEdict(edict_t * ent);
void SV_LinkEdict(edict_t * ent);

/*=================================================================== */

/* returns the CONTENTS_* value from the world at the given point. */
int SV_PointContents(vec3_t p);
const char *SV_GetFootstepSound(const char *texture);
trace_t SV_Trace(vec3_t start, const vec3_t mins, const vec3_t maxs, vec3_t end, edict_t * passedict, int contentmask);

#endif /* SERVER_SERVER_H */
