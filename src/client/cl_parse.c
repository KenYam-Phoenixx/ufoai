/**
 * @file cl_parse.c
 * @brief Parse a message received from the server.
 */

/*
All original materal Copyright (C) 2002-2007 UFO: Alien Invasion team.

Original file from Quake 2 v3.21: quake2-2.31/client/
Copyright (C) 1997-2001 Id SoftwaR_ Inc.

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
#include "cl_sound.h"

extern cvar_t *fs_gamedir;

/**
 * @brief see also svc_ops_e in common.h
 * @note don't change the array size - a NET_ReadByte can
 * return values between 0 and UCHAR_MAX (-1 is not handled here)
 */
static const char *svc_strings[UCHAR_MAX+1] =
{
	"svc_bad",

	"svc_inventory",

	"svc_nop",
	"svc_disconnect",
	"svc_reconnect",
	"svc_sound",
	"svc_print",
	"svc_stufftext",
	"svc_serverdata",
	"svc_configstring",
	"svc_spawnbaseline",
	"svc_centerprint",
	"svc_playerinfo",
	"svc_event"
};

/*============================================================================= */

/**
 * id	| type		| length (bytes)
 *======================================
 * c	| char		| 1
 * b	| byte		| 1
 * s	| short		| 2
 * l	| long		| 4
 * p	| pos		| 6
 * g	| gpos		| 3
 * d	| dir		| 1
 * a	| angle		| 1
 * !	| do not read the next id | 1
 * *	| pascal string type - SIZE+DATA, SIZE can be read from va_arg
		| 2 + sizeof(DATA)
 * @sa ev_names
 * @sa ev_func
 */
const char *ev_format[] =
{
	"",					/* EV_NULL */
	"bb",				/* EV_RESET */
	"b",				/* EV_START */
	"b",				/* EV_ENDROUND */
	"bb",				/* EV_ENDROUNDANNOUNCE */

	"bb****",			/* EV_RESULTS */
	"g",				/* EV_CENTERVIEW */

	"sbg",				/* EV_ENT_APPEAR */
	"s",				/* EV_ENT_PERISH */
	"sss",				/* EV_ENT_EDICT */

	"!sbbbbgbssssbsbbbs",	/* EV_ACTOR_APPEAR; beware of the '!' */
	"!sbbbbgsb",		/* EV_ACTOR_ADD; beware of the '!' */
	"s",				/* EV_ACTOR_START_MOVE */
	"sb",				/* EV_ACTOR_TURN */
	"!sbbs",			/* EV_ACTOR_MOVE: Don't use this format string - see CL_ActorDoMove for more info */

	"ssbbgg",			/* EV_ACTOR_START_SHOOT */
	"ssbbbbppb",		/* EV_ACTOR_SHOOT; the last 'b' cannot be 'd' */
	"bsbb"	,			/* EV_ACTOR_SHOOT_HIDDEN */
	"ssbbbpp",			/* EV_ACTOR_THROW */

	"ss",				/* EV_ACTOR_DIE */
	"!sbsbb",			/* EV_ACTOR_STATS; beware of the '!' */
	"ss",				/* EV_ACTOR_STATECHANGE */

	"s*",				/* EV_INV_ADD */
	"sbbb",				/* EV_INV_DEL */
	"sbbbbb",			/* EV_INV_AMMO */
	"sbbbbb",			/* EV_INV_RELOAD */
	"ss",				/* EV_INV_HANDS_CHANGED */

	"s",				/* EV_MODEL_PERISH */
	"ss",				/* EV_MODEL_EXPLODE */
	"sp*",				/* EV_SPAWN_PARTICLE */

	"ssp",				/* EV_DOOR_OPEN */
	"ssp"				/* EV_DOOR_CLOSE */
};

/**
 * @sa ev_format
 * @sa ev_func
 */
static const char *ev_names[] =
{
	"EV_NULL",
	"EV_RESET",
	"EV_START",
	"EV_ENDROUND",
	"EV_ENDROUNDANNOUNCE",

	"EV_RESULTS",
	"EV_CENTERVIEW",

	"EV_ENT_APPEAR",
	"EV_ENT_PERISH",
	"EV_ENT_EDICT",

	"EV_ACTOR_APPEAR",
	"EV_ACTOR_ADD",
	"EV_ACTOR_START_MOVE",
	"EV_ACTOR_TURN",
	"EV_ACTOR_MOVE",
	"EV_ACTOR_START_SHOOT",
	"EV_ACTOR_SHOOT",
	"EV_ACTOR_SHOOT_HIDDEN",
	"EV_ACTOR_THROW",
	"EV_ACTOR_DIE",
	"EV_ACTOR_STATS",
	"EV_ACTOR_STATECHANGE",

	"EV_INV_ADD",
	"EV_INV_DEL",
	"EV_INV_AMMO",
	"EV_INV_RELOAD",
	"EV_INV_HANDS_CHANGED",

	"EV_MODEL_PERISH",
	"EV_MODEL_EXPLODE",

	"EV_SPAWN_PARTICLE",

	"EV_DOOR_OPEN",
	"EV_DOOR_CLOSE"
};

static void CL_Reset(struct dbuffer *msg);
static void CL_StartGame(struct dbuffer *msg);
static void CL_CenterView(struct dbuffer *msg);
static void CL_EntAppear(struct dbuffer *msg);
static void CL_EntPerish(struct dbuffer *msg);
static void CL_EntEdict(struct dbuffer *msg);
static void CL_ActorDoStartMove(struct dbuffer *msg);
static void CL_ActorAppear(struct dbuffer *msg);
static void CL_ActorAdd(struct dbuffer *msg);
static void CL_ActorStats(struct dbuffer *msg);
static void CL_ActorStateChange(struct dbuffer *msg);
static void CL_InvAdd(struct dbuffer *msg);
static void CL_InvDel(struct dbuffer *msg);
static void CL_InvAmmo(struct dbuffer *msg);
static void CL_InvReload(struct dbuffer *msg);
static void CL_EndRoundAnnounce(struct dbuffer * msg);

static void (*ev_func[])( struct dbuffer *msg ) =
{
	NULL,
	CL_Reset,		/* EV_RESET */
	CL_StartGame,	/* EV_START */
	CL_DoEndRound,
	CL_EndRoundAnnounce,

	CL_ParseResults,
	CL_CenterView,

	CL_EntAppear,
	CL_EntPerish,
	CL_EntEdict,

	CL_ActorAppear,
	CL_ActorAdd,
	CL_ActorDoStartMove,
	CL_ActorDoTurn,
	CL_ActorDoMove,
	CL_ActorStartShoot,
	CL_ActorDoShoot,
	CL_ActorShootHidden,
	CL_ActorDoThrow,
	CL_ActorDie,
	CL_ActorStats,
	CL_ActorStateChange,

	CL_InvAdd,
	CL_InvDel,
	CL_InvAmmo,
	CL_InvReload,
	CL_InvCheckHands,

	LM_Perish,
	LM_Explode,

	CL_ParticleSpawnFromSizeBuf,

	LM_DoorOpen,
	LM_DoorClose
};

typedef struct evTimes_s {
	int when;
	int eType;
	struct dbuffer *msg;
	struct evTimes_s *next;
} evTimes_t;

static evTimes_t *events = NULL;

qboolean blockEvents;	/**< block network events - see CL_Events */

/** @brief CL_ParseEvent timers and vars */
static int nextTime;	/**< time when the next event should be executed */
static int shootTime;	/**< time when the shoot was fired */
static int impactTime;	/**< time when the shoot hits the target */
static qboolean parsedDeath = qfalse;

/*============================================================================= */

/**
 * @return true if the file exists, otherwise it attempts to start a download via curl
 * @sa CL_CheckAndQueueDownload
 * @sa CL_RequestNextDownload
 */
qboolean CL_CheckOrDownloadFile (const char *filename)
{
	static char lastfilename[MAX_OSPATH] = {0};

	/* r1: don't attempt same file many times */
	if (!strcmp(filename, lastfilename))
		return qtrue;

	strcpy(lastfilename, filename);

	if (strstr(filename, "..")) {
		Com_Printf("Refusing to check a path with .. (%s)\n",  filename);
		return qtrue;
	}

	if (strchr(filename, ' ')) {
		Com_Printf("Refusing to check a path containing spaces (%s)\n", filename);
		return qtrue;
	}

	if (strchr(filename, ':')) {
		Com_Printf("Refusing to check a path containing a colon (%s)\n", filename);
		return qtrue;
	}

	if (filename[0] == '/') {
		Com_Printf("Refusing to check a path starting with / (%s)\n", filename);
		return qtrue;
	}

	if (FS_LoadFile(filename, NULL) != -1) {
		/* it exists, no need to download */
		return qtrue;
	}

	if (CL_QueueHTTPDownload(filename))
		return qfalse;

	return qtrue;
}

/**
 * @sa Call before entering a new level, or after snd_restart
 * @sa CL_LoadMedia
 * @sa S_Restart_f
 * @sa CL_RequestNextDownload
 */
void CL_RegisterSounds (void)
{
	int i, j, k;

	/* load weapon sounds */
	for (i = 0; i < csi.numODs; i++) { /* i = obj */
		for (j = 0; j < csi.ods[i].numWeapons; j++) {	/* j = weapon-entry per obj */
			for (k = 0; k < csi.ods[i].numFiredefs[j]; j++) { /* k = firedef per wepaon */
				if (csi.ods[i].fd[j][k].fireSound[0])
					S_RegisterSound(csi.ods[i].fd[j][k].fireSound);
				if (csi.ods[i].fd[j][k].impactSound[0])
					S_RegisterSound(csi.ods[i].fd[j][k].impactSound);
				if (csi.ods[i].fd[j][k].hitBodySound[0])
					S_RegisterSound(csi.ods[i].fd[j][k].hitBodySound);
				if (csi.ods[i].fd[j][k].bounceSound[0])
					S_RegisterSound(csi.ods[i].fd[j][k].bounceSound);
			}
		}
	}
}

/*
=====================================================================
SERVER CONNECTING MESSAGES
=====================================================================
*/

/**
 * @brief Written by SV_New_f in sv_user.c
 */
static void CL_ParseServerData (struct dbuffer *msg)
{
	char *str;
	int i;

	Com_DPrintf(DEBUG_CLIENT, "Serverdata packet received.\n");

	/* wipe the client_state_t struct */
	CL_ClearState();
	CL_SetClientState(ca_connected);

	/* parse protocol version number */
	i = NET_ReadLong(msg);
	/* compare versions */
	if (i != PROTOCOL_VERSION)
		Com_Error(ERR_DROP, "Server returned version %i, not %i", i, PROTOCOL_VERSION);

	cl.servercount = NET_ReadLong(msg);

	/* game directory */
	str = NET_ReadString(msg);
	Q_strncpyz(cl.gamedir, str, sizeof(cl.gamedir));

	/* set gamedir */
	if ((*str && (!fs_gamedir->string || !*fs_gamedir->string || strcmp(fs_gamedir->string, str))) || (!*str && (fs_gamedir->string || *fs_gamedir->string)))
		Cvar_Set("fs_gamedir", str);

	/* parse player entity number */
	cl.pnum = NET_ReadShort(msg);

	/* get the full level name */
	str = NET_ReadString(msg);

	Com_DPrintf(DEBUG_CLIENT, "serverdata: count %d, gamedir %s, pnum %d, level %s\n",
		cl.servercount, cl.gamedir, cl.pnum, str);

	if (cl.pnum >= 0) {
		/* seperate the printfs so the server message can have a color */
/*		Com_Printf("%c%s\n", 2, str);*/
		/* need to prep refresh at next oportunity */
		cl.refresh_prepped = qfalse;
	}
}

/**
 * @brief Stores the client name
 * @sa CL_ParseClientinfo
 */
static void CL_LoadClientinfo (clientinfo_t *ci, const char *s)
{
	Q_strncpyz(ci->cinfo, s, sizeof(ci->cinfo));

	/* isolate the player's name */
	Q_strncpyz(ci->name, s, sizeof(ci->name));
}

/**
 * @brief Parses client names that are displayed on the targeting box for
 * multiplayer games
 * @sa CL_AddTargetingBoX
 */
static void CL_ParseClientinfo (int player)
{
	char *s;
	clientinfo_t *ci;

	s = cl.configstrings[player+CS_PLAYERNAMES];

	ci = &cl.clientinfo[player];

	CL_LoadClientinfo(ci, s);
}

/**
 * @sa PF_Configstring
 */
static void CL_ParseConfigString (struct dbuffer *msg)
{
	int i;
	char *s;

	/* which configstring? */
	i = NET_ReadShort(msg);
	if (i < 0 || i >= MAX_CONFIGSTRINGS)
		Com_Error(ERR_DROP, "configstring > MAX_CONFIGSTRINGS");
	/* value */
	s = NET_ReadString(msg);

	Com_DPrintf(DEBUG_CLIENT, "configstring %d: %s\n", i, s);

	/* there may be overflows in i==CS_TILES - but thats ok */
	/* see definition of configstrings and MAX_TILESTRINGS */
	switch (i) {
	case CS_TILES:
	case CS_POSITIONS:
		Q_strncpyz(cl.configstrings[i], s, MAX_TOKEN_CHARS*MAX_TILESTRINGS);
		break;
	default:
		Q_strncpyz(cl.configstrings[i], s, MAX_TOKEN_CHARS);
		break;
	}

	/* do something apropriate */
	if (i >= CS_LIGHTS && i < CS_LIGHTS + MAX_LIGHTSTYLES)
		CL_SetLightstyle(i - CS_LIGHTS);
	else if (i >= CS_MODELS && i < CS_MODELS + MAX_MODELS) {
		if (cl.refresh_prepped) {
			cl.model_draw[i-CS_MODELS] = R_RegisterModelShort(cl.configstrings[i]);
			if (cl.configstrings[i][0] == '*')
				cl.model_clip[i-CS_MODELS] = CM_InlineModel(cl.configstrings[i]);
			else
				cl.model_clip[i-CS_MODELS] = NULL;
		}
	} else if (i >= CS_PLAYERNAMES && i < CS_PLAYERNAMES + MAX_CLIENTS) {
		CL_ParseClientinfo(i-CS_PLAYERNAMES);
	}
}


/*
=====================================================================
ACTION MESSAGES
=====================================================================
*/

/**
 * @brief Parsed a server send sound package
 * @sa svc_sound
 * @sa SV_StartSound
 */
static void CL_ParseStartSoundPacket (struct dbuffer *msg)
{
	vec3_t pos_v;
	float *pos;
	int channel, ent, flags;
	float volume;
	float attenuation;
	const char *sound;
	sfx_t *sfx;

	flags = NET_ReadByte(msg);
	sound = NET_ReadString(msg);

	if (flags & SND_VOLUME)
		volume = NET_ReadByte(msg) / 128.0;
	else
		volume = DEFAULT_SOUND_PACKET_VOLUME;

	if (flags & SND_ATTENUATION)
		attenuation = NET_ReadByte(msg) / 64.0;
	else
		attenuation = DEFAULT_SOUND_PACKET_ATTENUATION;

	/* entity relative */
	if (flags & SND_ENT) {
		channel = NET_ReadShort(msg);
		ent = channel >> 3;
		if (ent > MAX_EDICTS)
			Com_Error(ERR_DROP,"CL_ParseStartSoundPacket: ent = %i", ent);

		channel &= 7;
	} else {
		ent = 0;
		channel = 0;
	}

	/* positioned in space */
	if (flags & SND_POS) {
		NET_ReadPos(msg, pos_v);

		pos = pos_v;
	} else /* use entity number */
		pos = NULL;

	Com_DPrintf(DEBUG_SOUND, "startsoundpacket: flags %x, sound %s, volume %.3f, attenuation %.2f,"
		" channel %d, ent %d, pos %.3f, %.3f, %.3f\n",
		flags, sound, volume, attenuation, channel, ent, pos[0], pos[1], pos[2]);

	sfx = S_RegisterSound(sound);
	S_StartSound(pos, sfx, volume, attenuation);
}

/**
 * @brief Reset the events
 * @note Also sets etUnused - if you get Timetable overflow messages, etUnused is NULL
 */
static void CL_EventReset (void)
{
	evTimes_t *event;
	evTimes_t *next;
	for (event = events; event; event = next) {
		next = event->next;
		free_dbuffer(event->msg);
		free(event);
	}
	events = NULL;
}

/**
 * @sa G_ClientSpawn
 * @sa EV_RESET
 */
static void CL_Reset (struct dbuffer *msg)
{
	/* clear local entities */
	numLEs = 0;
	selActor = NULL;
	cl.numTeamList = 0;
	Cbuf_AddText("numonteam1\n");

	CL_EventReset();
	parsedDeath = qfalse;
	cl.eventTime = 0;
	nextTime = 0;
	shootTime = 0;
	impactTime = 0;
	blockEvents = qfalse;

	/* set the active player */
	NET_ReadFormat(msg, ev_format[EV_RESET], &cls.team, &cl.actTeam);

	Com_Printf("(player %i) It's team %i's round\n", cl.pnum, cl.actTeam);
	/* if in multiplayer spawn the soldiers */
	if (!ccs.singleplayer) {
		/* pop any waiting menu and activate the HUD */
		MN_PopMenu(qtrue);
		MN_PushMenu(mn_hud->string);
		Cvar_Set("mn_active", mn_hud->string);
		Cvar_Set("mn_main", "multiplayerInGame");
	}
	if (cls.team == cl.actTeam)
		Cbuf_AddText("startround\n");
	else
		Com_Printf("You lost the coin-toss for first-turn\n");
}


/**
 * @brief Activates the map render screen (ca_active)
 * @sa SCR_EndLoadingPlaque
 * @sa G_ClientBegin
 * @note EV_START
 */
static void CL_StartGame (struct dbuffer *msg)
{
	int team_play = NET_ReadByte(msg);

	/* init camera position and angles */
	memset(&cl.cam, 0, sizeof(camera_t));
	VectorSet(cl.cam.angles, 60.0, 60.0, 0.0);
	VectorSet(cl.cam.omega, 0.0, 0.0, 0.0);
	cl.cam.zoom = 1.25;
	V_CalcFovX();
	camera_mode = CAMERA_MODE_REMOTE;

	SCR_SetLoadingBackground(NULL);

	Com_Printf("Starting the game...\n");

	/* make sure selActor is null (after reconnect or server change this is needed) */
	selActor = NULL;

	/* center on first actor */
	cl_worldlevel->modified = qtrue;
	if (cl.numTeamList) {
		le_t *le;
		le = cl.teamList[0];
		VectorCopy(le->origin, cl.cam.reforg);
		Cvar_SetValue("cl_worldlevel", le->pos[2]);
	}

	/* activate the renderer */
	CL_SetClientState(ca_active);

	CL_EventReset();

	/* back to the console */
	MN_PopMenu(qtrue);

	if (!ccs.singleplayer && baseCurrent) {
		if (team_play) {
			MN_PushMenu("multiplayer_selectteam");
			Cvar_Set("mn_active", "multiplayer_selectteam");
		} else {
			Cbuf_AddText("mp_selectteam_init\n");
		}
	} else {
		MN_PushMenu(mn_hud->string);
		Cvar_Set("mn_active", mn_hud->string);
	}
	SCR_EndLoadingPlaque();	/* get rid of loading plaque */
}


static void CL_CenterView (struct dbuffer *msg)
{
	pos3_t pos;

	NET_ReadFormat(msg, ev_format[EV_CENTERVIEW], &pos);
	V_CenterView(pos);
}


static void CL_EntAppear (struct dbuffer *msg)
{
	le_t	*le;
	int		entnum, type;
	pos3_t	pos;

	NET_ReadFormat(msg, ev_format[EV_ENT_APPEAR], &entnum, &type, &pos);

	/* check if the ent is already visible */
	le = LE_Get(entnum);
	if (!le) {
		le = LE_Add(entnum);
	} else {
		Com_DPrintf(DEBUG_CLIENT, "Entity appearing already visible... overwriting the old one\n");
		le->inuse = qtrue;
	}

	le->type = type;

	VectorCopy(pos, le->pos);
	Grid_PosToVec(&clMap, le->pos, le->origin);
}


/**
 * @brief Called whenever an entity disappears from view
 */
static void CL_EntPerish (struct dbuffer *msg)
{
	int		entnum, i;
	le_t	*le, *actor;

	NET_ReadFormat(msg, ev_format[EV_ENT_PERISH], &entnum);

	le = LE_Get(entnum);

	if (!le) {
		Com_Printf("CL_EntPerish: Delete request ignored... LE not found (number: %i)\n", entnum);
		return;
	}

	/* decrease the count of spotted aliens */
	if ((le->type == ET_ACTOR || le->type == ET_ACTOR2x2) && !(le->state & STATE_DEAD) && le->team != cls.team && le->team != TEAM_CIVILIAN)
		cl.numAliensSpotted--;

	switch (le->type) {
	case ET_ITEM:
		INVSH_EmptyContainer(&le->i, csi.idFloor);

		/* search owners (there can be many, some of them dead) */
		for (i = 0, actor = LEs; i < numLEs; i++, actor++)
			if (actor->inuse
				 && (actor->type == ET_ACTOR || actor->type == ET_ACTOR2x2)
				 && VectorCompare(actor->pos, le->pos)) {
				Com_DPrintf(DEBUG_CLIENT, "CL_EntPerish: le of type ET_ITEM hidden\n");
				FLOOR(actor) = NULL;
			}
		break;
	case ET_ACTOR:
	case ET_ACTOR2x2:
		INVSH_DestroyInventory(&le->i);
		break;
	case ET_BREAKABLE:
	case ET_DOOR:
		break;
	default:
		break;
	}

	if (!(le->team && le->state && le->team == cls.team && !(le->state & STATE_DEAD)))
		le->inuse = qfalse;
}

/**
 * @brief Inits the breakable or other solid objects
 * @note func_breakable, func_door
 * @sa G_SendVisibleEdicts
 */
static void CL_EntEdict (struct dbuffer *msg)
{
	le_t *le;
	int entnum, modelnum1, type;
	char *inline_model_name;
	cBspModel_t *model;

	NET_ReadFormat(msg, ev_format[EV_ENT_EDICT], &type, &entnum, &modelnum1);

	/* check if the ent is already visible */
	le = LE_Get(entnum);
	if (!le) {
		le = LE_Add(entnum);
	} else {
		Com_DPrintf(DEBUG_CLIENT, "Entity appearing already visible... overwriting the old one\n");
		le->inuse = qtrue;
	}

	le->invis = qtrue;
	le->type = type;
	le->modelnum1 = modelnum1;
	inline_model_name = va("*%i", le->modelnum1);
	model = CM_InlineModel(inline_model_name);
	if (!model)
		Com_Error(ERR_DROP, "CL_EntEdict: Could not find inline model %i", le->modelnum1);
	VectorCopy(model->origin, le->origin);
	VectorCopy(model->mins, le->mins);
	VectorCopy(model->maxs, le->maxs);

	/* to allow tracing against this le */
	le->contents = CONTENTS_SOLID;
}

/**
 * @brief Announces that a player ends his round
 * @param[in] msg The message buffer to read from
 * @sa CL_DoEndRound
 * @note event EV_ENDROUNDANNOUNCE
 * @todo Build into hud
 */
static void CL_EndRoundAnnounce (struct dbuffer * msg)
{
	int playerNum, team;
	const char *playerName;
	char buf[128];

	/* get the needed values */
	playerNum = NET_ReadByte(msg);
	team = NET_ReadByte(msg);
	playerName = cl.configstrings[CS_PLAYERNAMES + playerNum];

	/* not needed to announce this for singleplayer games */
	if (ccs.singleplayer)
		return;

	/* it was our own round */
	if (cl.pnum == playerNum) {
		/* add translated message to chat buffer */
		Com_sprintf(buf, sizeof(buf), _("You've ended your round\n"));
		MN_AddChatMessage(buf);

		/* don't translate on the game console */
		Com_Printf("You've ended your round\n");
	} else {
		/* add translated message to chat buffer */
		Com_sprintf(buf, sizeof(buf), _("%s ended his round (team %i)\n"), playerName, team);
		MN_AddChatMessage(buf);

		/* don't translate on the game console */
		Com_Printf("%s ended his round (team %i)\n", playerName, team);
	}
}

static le_t	*lastMoving;

/**
 * @brief Set the lastMoving entity (set to the actor who last
 * walked, turned, crouched or stood up).
 */
void CL_SetLastMoving (le_t *le)
{
	lastMoving = le;
}

/**
 * @sa EV_ACTOR_START_MOVE
 */
static void CL_ActorDoStartMove (struct dbuffer *msg)
{
	int	entnum;

	NET_ReadFormat(msg, ev_format[EV_ACTOR_START_MOVE], &entnum);

	CL_SetLastMoving(LE_Get(entnum));
}

/**
 * @brief draw a simple 'spotted' line from a spotter to the spotted
 */
void CL_DrawLineOfSight (le_t *watcher, le_t *target)
{
	ptl_t *ptl = NULL;
	vec3_t eyes;

	if (!watcher || !target)
		return;

	/* start is the watchers origin */
	VectorCopy(watcher->origin, eyes);
	if (watcher->state & STATE_CROUCHED)
		eyes[2] += EYE_HT_CROUCH;
	else
		eyes[2] += EYE_HT_STAND;

	ptl = CL_ParticleSpawn("fadeTracer", 0, eyes, target->origin, NULL);

	if (target->team == TEAM_CIVILIAN)
		VectorSet(ptl->color, 0.2, 0.2, 1);
}

/**
 * @brief Adds an hidden actor to the list of le's
 * @sa CL_ActorAppear
 * @note Actor is invisible until CL_ActorAppear (EV_ACTOR_APPEAR) was triggered
 * @note EV_ACTOR_ADD
 * @sa G_SendInvisible
 */
static void CL_ActorAdd (struct dbuffer *msg)
{
	le_t *le;
	int entnum;
	int teamDefID;

	/* check if the actor is already visible */
	entnum = NET_ReadShort(msg);
	le = LE_Get(entnum);
	if (le) {
		Com_Printf("CL_ActorAdd: Actor with number %i already exists\n", entnum);
		return;
	}
	le = LE_Add(entnum);

	/* get the info */
	NET_ReadFormat(msg, ev_format[EV_ACTOR_ADD],
				&le->team, &teamDefID,
				&le->gender, &le->pnum, &le->pos,
				&le->state, &le->fieldSize);

	if (teamDefID < 0 || teamDefID > csi.numTeamDefs)
		Com_Printf("CL_ActorAdd: Invalid teamDef index\n");
	else
		le->teamDef = &csi.teamDef[teamDefID];

	/*Com_Printf("CL_ActorAdd: Add number: %i\n", entnum);*/

	le->type = ET_ACTORHIDDEN;

	Grid_PosToVec(&clMap, le->pos, le->origin);
	le->invis = qtrue;
}

/**
 * @sa CL_AddActorToTeamList
 * @sa G_AppearPerishEvent
 * @sa CL_ActorAdd
 * @note EV_ACTOR_APPEAR
 */
static void CL_ActorAppear (struct dbuffer *msg)
{
	qboolean newActor;
	le_t *le;
	char tmpbuf[128];
	int entnum, modelnum1, modelnum2;
	int teamDefID = -1;

	/* check if the actor is already visible */
	entnum = NET_ReadShort(msg);
	le = LE_Get(entnum);

	if (!le) {
		le = LE_Add(entnum);
		newActor = qtrue;
	} else {
/*		Com_Printf("Actor appearing already visible... overwriting the old one\n"); */
		le->inuse = qtrue;
		if (le->type == ET_ACTORHIDDEN)
			newActor = qtrue;
		else
			newActor = qfalse;
	}
	/* maybe added via CL_ActorAdd before */
	le->invis = qfalse;

	/* get the info */
	NET_ReadFormat(msg, ev_format[EV_ACTOR_APPEAR],
				&le->team, &teamDefID, &le->gender, &le->pnum, &le->pos,
				&le->dir, &le->right, &le->left,
				&modelnum1, &modelnum2, &le->skinnum,
				&le->state, &le->fieldSize,
				&le->maxTU, &le->maxMorale, &le->maxHP);

	if (teamDefID < 0 || teamDefID > csi.numTeamDefs)
		Com_Printf("CL_ActorAppear: Invalid teamDef index\n");
	else
		le->teamDef = &csi.teamDef[teamDefID];

	switch (le->fieldSize) {
	case ACTOR_SIZE_NORMAL:
		le->addFunc = CL_AddActor;
		le->type = ET_ACTOR;
		break;
	case ACTOR_SIZE_2x2:
		le->addFunc = CL_AddUGV;
		le->type = ET_ACTOR2x2;
		break;
	default:
		Com_Error(ERR_DROP, "Unknown fieldSize for le in CL_ActorAppear (EV_ACTOR_APPEAR)");
	}
	le->modelnum1 = modelnum1;
	le->modelnum2 = modelnum2;
	le->model1 = cl.model_draw[modelnum1];
	le->model2 = cl.model_draw[modelnum2];
	Grid_PosToVec(&clMap, le->pos, le->origin);
	le->angles[YAW] = dangle[le->dir];

	le->contents = CONTENTS_ACTOR;
	VectorCopy(player_mins, le->mins);
	if (le->state & STATE_DEAD)
		VectorCopy(player_dead_maxs, le->maxs);
	else
		VectorCopy(player_maxs, le->maxs);

	le->think = LET_StartIdle;

	/* count spotted aliens */
	if (!(le->state & STATE_DEAD) && newActor && le->team != cls.team && le->team != TEAM_CIVILIAN)
		cl.numAliensSpotted++;

	if (cls.state == ca_active && !(le->state & STATE_DEAD)) {
		/* center view (if wanted) */
		if (cl_centerview->integer > 1 || (cl_centerview->integer == 1 && cl.actTeam != cls.team)) {
			VectorCopy(le->origin, cl.cam.reforg);
			Cvar_SetValue("cl_worldlevel", le->pos[2]);
		}

		/* draw line of sight */
		if (le->team != cls.team) {
			if (cl.actTeam == cls.team && lastMoving) {
				CL_DrawLineOfSight(lastMoving, le);
			}

			/* message */
			if (le->team != TEAM_CIVILIAN) {
				if (curCampaign) {
					if (le->teamDef) {
						if (RS_IsResearched_idx(RS_GetTechIdxByName(le->teamDef->tech))) {
							Com_sprintf(tmpbuf, sizeof(tmpbuf), _("Alien spotted: %s!"), _(le->teamDef->name));
							CL_DisplayHudMessage(tmpbuf, 2000);
						} else
							CL_DisplayHudMessage(_("Alien spotted!\n"), 2000);
					} else
						CL_DisplayHudMessage(_("Alien spotted!\n"), 2000);
				} else
					CL_DisplayHudMessage(_("Enemy spotted!\n"), 2000);
			} else
				CL_DisplayHudMessage(_("Civilian spotted!\n"), 2000);

			/* update pathing as new actor could block path */
			if (newActor)
				CL_ConditionalMoveCalc(&clMap, selActor, MAX_ROUTE);
		}
	}

	/* add team members to the actor list */
	CL_AddActorToTeamList(le);

/*	Com_Printf("Player at (%i %i %i) -> (%f %f %f)\n", */
/*		le->pos[0], le->pos[1], le->pos[2], le->origin[0], le->origin[1], le->origin[2]); */
}


/**
 * @brief Parses the actor stats that comes from the netchannel
 * @sa CL_ParseEvent
 * @sa G_SendStats
 * @sa ev_func
 */
static void CL_ActorStats (struct dbuffer *msg)
{
	le_t	*le;
	int		number, selActorTU = 0;

	number = NET_ReadShort(msg);
	le = LE_Get(number);
	if (!le) {
		Com_Printf("CL_ActorStats: LE with number %i not found\n", number);
		return;
	}

	switch (le->type) {
	case ET_ACTORHIDDEN:
	case ET_ACTOR:
	case ET_ACTOR2x2:
		break;
	default:
		Com_Printf("CL_ActorStats: LE (%i) not an actor (type: %i)\n", number, le->type);
		return;
	}

	if (le == selActor)
		selActorTU = selActor->TU;

	NET_ReadFormat(msg, ev_format[EV_ACTOR_STATS], &le->TU, &le->HP, &le->STUN, &le->AP, &le->morale);

	if (le->TU > le->maxTU)
		le->maxTU = le->TU;
	if (le->HP > le->maxHP)
		le->maxHP = le->HP;
	if (le->morale > le->maxMorale)
		le->maxMorale = le->morale;

	/* if selActor's timeunits have changed, update movelength */
	if (le == selActor)
		if (le->TU != selActorTU) {
			CL_ResetActorMoveLength();
		}
}


static void CL_ActorStateChange (struct dbuffer *msg)
{
	le_t	*le;
	int		number, state;

	NET_ReadFormat(msg, ev_format[EV_ACTOR_STATECHANGE], &number, &state);

	le = LE_Get(number);
	if (!le) {
		Com_Printf("Could not get le with id %i\n", number);
		return;
	}

	switch (le->type) {
	case ET_ACTORHIDDEN:
	case ET_ACTOR:
	case ET_ACTOR2x2:
		break;
	default:
		Com_Printf("StateChange message ignored... LE not found or not an actor (number: %i, state: %i, type: %i)\n",
			number, state, le->type);
		return;
	}

	/* if standing up or crouching down, set this le as the last moving */
	if ((state & STATE_CROUCHED && !(le->state & STATE_CROUCHED)) ||
		 (!(state & STATE_CROUCHED) && le->state & STATE_CROUCHED))
		CL_SetLastMoving(le);

	/* killed by the server: no animation is played, etc. */
	if (state & STATE_DEAD && !(le->state & STATE_DEAD)) {
		le->state = state;
		FLOOR(le) = NULL;
		le->think = NULL;
		VectorCopy(player_dead_maxs, le->maxs);
		CL_RemoveActorFromTeamList(le);
		CL_ConditionalMoveCalc(&clMap, selActor, MAX_ROUTE);
		return;
	} else {
		le->state = state;
		le->think = LET_StartIdle;
	}

	/* state change may have affected move length */
	if (selActor)
		CL_ResetActorMoveLength();
}


/**
 * @brief Returns the index of the biggest item in the inventory list
 * @note This item is the only one that will be drawn when lying at the floor
 * @sa CL_PlaceItem
 */
static int CL_BiggestItem (invList_t *ic)
{
	int size, max;
	int maxSize = 0;

	for (max = ic->item.t; ic; ic = ic->next) {
		size = Com_ShapeUsage(csi.ods[ic->item.t].shape);
		if (size > maxSize) {
			max = ic->item.t;
			maxSize = size;
		}
	}

	return max;
}


/**
 * @sa CL_BiggestItem
 */
static void CL_PlaceItem (le_t *le)
{
	le_t *actor;
	int	biggest;
	int i;

	/* search owners (there can be many, some of them dead) */
	for (i = 0, actor = LEs; i < numLEs; i++, actor++)
		if (actor->inuse
		 && (actor->type == ET_ACTOR || actor->type == ET_ACTOR2x2)
		 && VectorCompare(actor->pos, le->pos) ) {
#if PARANOID
			Com_DPrintf(DEBUG_CLIENT, "CL_PlaceItem: shared container: '%p'\n", FLOOR(le));
#endif
			if (FLOOR(le))
				FLOOR(actor) = FLOOR(le);
		}

	if (FLOOR(le)) {
		biggest = CL_BiggestItem(FLOOR(le));
		le->model1 = cls.model_weapons[biggest];
		Grid_PosToVec(&clMap, le->pos, le->origin);
		VectorSubtract(le->origin, csi.ods[biggest].center, le->origin);
		/* fall to ground */
		le->origin[2] -= GROUND_DELTA;
		le->angles[ROLL] = 90;
/*		le->angles[YAW] = 10*(int)(le->origin[0] + le->origin[1] + le->origin[2]) % 360; */
	} else {
		/* If no items in floor inventory, don't draw this le */
		/* DEBUG
		 * This is disabled for now because it'll prevent LE_Add to get a floor-edict when in mid-move.
		 * See g_client.c:G_ClientInvMove
		 */
		/* le->inuse = qfalse; */
	}
}


/**
 * @sa CL_InvDel
 * @sa G_SendInventory
 */
static void CL_InvAdd (struct dbuffer *msg)
{
	int		number, nr;
	int 	container, x, y;
	le_t	*le;
	item_t 	item;

	number = NET_ReadShort(msg);

	le = LE_Get(number);
	if (!le) {
		nr = NET_ReadShort(msg) / INV_INVENTORY_BYTES;
		Com_Printf("InvAdd: message ignored... LE %i not found\n", number);
		for (; nr-- > 0;) {
			CL_NetReceiveItem(msg, &item, &container, &x, &y);
			Com_Printf("InvAdd: ignoring:\n");
			INVSH_PrintItemDescription(item.t);
		}
		return;
	}
	if (!le->inuse)
		Com_DPrintf(DEBUG_CLIENT, "InvAdd: warning... LE found but not in-use\n");

	nr = NET_ReadShort(msg) / INV_INVENTORY_BYTES;

	for (; nr-- > 0;) {
		CL_NetReceiveItem(msg, &item, &container, &x, &y);
		Com_AddToInventory(&le->i, item, container, x, y, 1);

		if (container == csi.idRight)
			le->right = item.t;
		else if (container == csi.idLeft)
			le->left = item.t;
		else if (container == csi.idExtension)
			le->extension = item.t;
	}

	switch (le->type) {
	case ET_ACTOR:
	case ET_ACTOR2x2:
		le->think = LET_StartIdle;
		break;
	case ET_ITEM:
		CL_PlaceItem(le);
		break;
	}
}


/**
 * @sa CL_InvAdd
 */
static void CL_InvDel (struct dbuffer *msg)
{
	le_t	*le;
	int		number;
	int 	container, x, y;

	NET_ReadFormat(msg, ev_format[EV_INV_DEL],
		&number, &container, &x, &y);

	le = LE_Get(number);
	if (!le) {
		Com_DPrintf(DEBUG_CLIENT, "InvDel message ignored... LE not found\n");
		return;
	}

	Com_RemoveFromInventory(&le->i, container, x, y);
	if (container == csi.idRight)
		le->right = NONE;
	else if (container == csi.idLeft)
		le->left = NONE;
	else if (container == csi.idExtension)
		le->extension = NONE;

	switch (le->type) {
	case ET_ACTOR:
	case ET_ACTOR2x2:
		le->think = LET_StartIdle;
		break;
	case ET_ITEM:
		CL_PlaceItem(le);
		break;
	}
}


static void CL_InvAmmo (struct dbuffer *msg)
{
	invList_t	*ic;
	le_t	*le;
	int		number;
	int		ammo, type, container, x, y;

	NET_ReadFormat(msg, ev_format[EV_INV_AMMO],
		&number, &ammo, &type, &container, &x, &y);

	le = LE_Get(number);
	if (!le) {
		Com_DPrintf(DEBUG_CLIENT, "InvAmmo message ignored... LE not found\n");
		return;
	}

	if (le->team != cls.team)
		return;

	ic = Com_SearchInInventory(&le->i, container, x, y);
	if (!ic)
		return;

	/* set new ammo */
	ic->item.a = ammo;
	ic->item.m = type;
}


static void CL_InvReload (struct dbuffer *msg)
{
	invList_t	*ic;
	le_t	*le;
	int		number;
	int		ammo, type, container, x, y;

	NET_ReadFormat(msg, ev_format[EV_INV_RELOAD],
		&number, &ammo, &type, &container, &x, &y);

	S_StartLocalSound("weapons/reload");

	le = LE_Get(number);
	if (!le) {
		Com_DPrintf(DEBUG_CLIENT, "CL_InvReload: only sound played\n");
		return;
	}

	if (le->team != cls.team)
		return;

	ic = Com_SearchInInventory(&le->i, container, x, y);
	if (!ic)
		return;

	/* if the displaced clip had any remaining bullets */
	/* store them as loose, unless the removed clip was full */
	if (curCampaign
		 && ic->item.a > 0
		 && ic->item.a != csi.ods[ic->item.t].ammo) {
		assert(ammo == csi.ods[ic->item.t].ammo);
		ccs.eMission.num_loose[ic->item.m] += ic->item.a;
		/* Accumulate loose ammo into clips (only accessible post-mission) */
		if (ccs.eMission.num_loose[ic->item.m] >= csi.ods[ic->item.t].ammo) {
			ccs.eMission.num_loose[ic->item.m] -= csi.ods[ic->item.t].ammo;
			ccs.eMission.num[ic->item.m]++;
		}
	}

	/* set new ammo */
	ic->item.a = ammo;
	ic->item.m = type;
}

/**
 * @sa CL_Events
 */
static void CL_LogEvent (int num)
{
	FILE *logfile;

	if (!cl_logevents->integer)
		return;

	logfile = fopen(va("%s/events.log", FS_Gamedir()), "a");
	fprintf(logfile, "%10i %s\n", cl.eventTime, ev_names[num]);
	fclose(logfile);
}

static void CL_ScheduleEvent(void);

static void CL_ExecuteEvent (int now, void *data)
{
	while (events && !blockEvents) {
		evTimes_t *event = events;

		if (event->when > cl.eventTime) {
			CL_ScheduleEvent();
			return;
		}

		events = event->next;

		Com_DPrintf(DEBUG_CLIENT, "event(dispatching): %s %p\n", ev_names[event->eType], event);
		CL_LogEvent(event->eType);
		ev_func[event->eType](event->msg);

		free_dbuffer(event->msg);
		free(event);
	}
}

/**
 * @brief Schedule the first event in the queue
 * @sa Schedule_Event
 * @sa do_event
 */
static void CL_ScheduleEvent (void)
{
	/* We need to schedule the first event in the queue. Unfortunately,
	 * events don't run on the master timer (yet - this should change),
	 * so we have to convert from one timescale to the other */
	int timescale_delta = Sys_Milliseconds() - cl.eventTime;
	if (!events)
		return;
	Schedule_Event(events->when + timescale_delta, &CL_ExecuteEvent, NULL);
}

/**
 * @sa CL_UnblockEvents
 */
void CL_BlockEvents (void)
{
	blockEvents = qtrue;
}

/**
 * @sa CL_BlockEvents
 */
void CL_UnblockEvents (void)
{
	blockEvents = qfalse;
	CL_ScheduleEvent();
}

/**
 * @brief Called in case a svc_event was send via the network buffer
 * @sa CL_ParseServerMessage
 * @param[in] msg The client stream message buffer to read from
 */
static void CL_ParseEvent (struct dbuffer *msg)
{
	evTimes_t *cur;
	qboolean now;
	int eType = NET_ReadByte(msg);
	if (eType == 0)
		return;

	/* check instantly flag */
	if (eType & EVENT_INSTANTLY) {
		now = qtrue;
		eType &= ~EVENT_INSTANTLY;
	} else
		now = qfalse;

	/* check if eType is valid */
	if (eType < 0 || eType >= EV_NUM_EVENTS)
		Com_Error(ERR_DROP, "CL_ParseEvent: invalid event %s", ev_names[eType]);

	if (!ev_func[eType])
		Com_Error(ERR_DROP, "CL_ParseEvent: no handling function for event %i", eType);

	if (now) {
		/* log and call function */
		CL_LogEvent(eType);
		Com_DPrintf(DEBUG_CLIENT, "event(now): %s\n", ev_names[eType]);
		ev_func[eType](msg);
	} else {
		struct dbuffer *event_msg = dbuffer_dup(msg);
		int event_time;

		/* get event time */
		if (nextTime < cl.eventTime) {
			nextTime = cl.eventTime;
		}
		if (impactTime < cl.eventTime) {
			impactTime = cl.eventTime;
		}

		if (eType == EV_ACTOR_DIE)
			parsedDeath = qtrue;

		if (eType == EV_ACTOR_DIE || eType == EV_MODEL_EXPLODE) {
			event_time = impactTime;
		} else if (eType == EV_ACTOR_SHOOT || eType == EV_ACTOR_SHOOT_HIDDEN) {
			event_time = shootTime;
		} else {
			event_time = nextTime;
		}

		if ((eType == EV_ENT_APPEAR || eType == EV_INV_ADD)) {
			if (parsedDeath) { /* drop items after death (caused by impact) */
				event_time = impactTime + 400;
				/* EV_INV_ADD messages are the last events sent after a death */
				if (eType == EV_INV_ADD)
					parsedDeath = qfalse;
			} else if (impactTime > cl.eventTime) { /* item thrown on the ground */
				event_time = impactTime + 75;
			}
		}

		/* calculate time interval before the next event */
		switch (eType) {
		case EV_ACTOR_APPEAR:
			if (cls.state == ca_active && cl.actTeam != cls.team) {
				nextTime += 600;
			}
			break;
		case EV_INV_RELOAD:
			/* let the reload sound play */
			nextTime += 600;
			break;
		case EV_ACTOR_START_SHOOT:
			nextTime += 300;
			shootTime = nextTime;
			break;
		case EV_ACTOR_SHOOT_HIDDEN:
			{
				fireDef_t *fd;
				int first;
				int obj_idx;
				int weap_fds_idx, fd_idx;

				NET_ReadFormat(msg, ev_format[EV_ACTOR_SHOOT_HIDDEN], &first, &obj_idx, &weap_fds_idx, &fd_idx);

				if (first) {
					nextTime += 500;
					impactTime = shootTime = nextTime;
				} else {
					fd = FIRESH_GetFiredef(obj_idx, weap_fds_idx, fd_idx);
					/* impact right away - we don't see it at all
					 * bouncing is not needed here, too (we still don't see it) */
					impactTime = shootTime;
					nextTime = shootTime + 1400;
					if (fd->delayBetweenShots)
						shootTime += 1000 / fd->delayBetweenShots;
				}
				parsedDeath = qfalse;
			}
			break;
		case EV_ACTOR_SHOOT:
			{
				fireDef_t	*fd;
				int flags, dummy;
				int obj_idx, surfaceFlags;
				int weap_fds_idx, fd_idx;
				vec3_t muzzle, impact;

				/* read data */
				NET_ReadFormat(msg, ev_format[EV_ACTOR_SHOOT], &dummy, &obj_idx, &weap_fds_idx, &fd_idx, &flags, &surfaceFlags, &muzzle, &impact, &dummy);

				fd = FIRESH_GetFiredef(obj_idx, weap_fds_idx, fd_idx);

				if (!(flags & SF_BOUNCED)) {
					/* shooting */
					if (fd->speed && !CL_OutsideMap(impact)) {
						impactTime = shootTime + 1000 * VectorDist(muzzle, impact) / fd->speed;
					} else {
						impactTime = shootTime;
					}
					if (cl.actTeam != cls.team)
						nextTime = impactTime + 1400;
					else
						nextTime = impactTime + 400;
					if (fd->delayBetweenShots)
						shootTime += 1000 / fd->delayBetweenShots;
				} else {
					/* only a bounced shot */
					event_time = impactTime;
					if (fd->speed) {
						impactTime += 1000 * VectorDist(muzzle, impact) / fd->speed;
						nextTime = impactTime;
					}
				}
				parsedDeath = qfalse;
			}
			break;
		case EV_ACTOR_THROW:
			nextTime += NET_ReadShort(msg);
			impactTime = shootTime = nextTime;
			parsedDeath = qfalse;
			break;
		default:
			break;
		}

		cur = malloc(sizeof(*cur));
		cur->when = event_time;
		cur->eType = eType;
		cur->msg = event_msg;

		if (!events || (events)->when > event_time) {
			cur->next = events;
			events = cur;
			CL_ScheduleEvent();
		} else {
			evTimes_t *e = events;
			while (e->next && e->next->when <= event_time)
				e = e->next;
			cur->next = e->next;
			e->next = cur;
		}

		Com_DPrintf(DEBUG_CLIENT, "event(at %d): %s %p\n", event_time, ev_names[eType], cur);
	}
}

/**
 * @sa CL_ReadPackets
 * @param[in] msg The client stream message buffer to read from
 */
void CL_ParseServerMessage (int cmd, struct dbuffer *msg)
{
	char *s;
	int i;

	/* parse the message */
	if (cmd == -1)
		return;

	Com_DPrintf(DEBUG_CLIENT, "command: %s\n", svc_strings[cmd]);

	/* commands */
	switch (cmd) {
	case svc_nop:
/*		Com_Printf("svc_nop\n"); */
		break;

	case svc_disconnect:
		s = NET_ReadString(msg);
		Com_Printf("%s\n", s);
		CL_Drop();	/* ensure the right menu cvars are set */
		MN_PopMenu(qfalse);	/* leave the hud mode */
		if (!ccs.singleplayer)
			MN_Popup(_("Notice"), _("The server has disconnected.\n"));
		break;

	case svc_reconnect:
		s = NET_ReadString(msg);
		Com_Printf("%s\n", s);
		CL_Disconnect();
		CL_SetClientState(ca_connecting);
		/* otherwise we would time out */
		cls.connectTime = cls.realtime - 1500;
		break;

	case svc_print:
		i = NET_ReadByte(msg);
		s = NET_ReadString(msg);
		switch (i) {
		case PRINT_CHAT:
			S_StartLocalSound("misc/talk");
			MN_AddChatMessage(s);
			/* skip format strings */
			if (*s == '^')
				s += 2;
			/* also print to console */
			break;
		case PRINT_HUD:
			/* all game lib messages or server messages should be printed
			 * untranslated with bprintf or cprintf */
			/* see src/po/OTHER_STRINGS */
			CL_DisplayHudMessage(_(s), 2000);
			/* this is utf-8 - so no console print please */
			return;
		default:
			break;
		}
		Com_DPrintf(DEBUG_CLIENT, "svc_print(%d): %s\n", i, s);
		Com_Printf("%s", s);
		break;

	case svc_centerprint:
		s = NET_ReadString(msg);
		Com_DPrintf(DEBUG_CLIENT, "svc_centerprint: %s\n", s);
		SCR_CenterPrint(s);
		break;

	case svc_stufftext:
		s = NET_ReadString(msg);
		Com_DPrintf(DEBUG_CLIENT, "stufftext: %s\n", s);
		Cbuf_AddText(s);
		break;

	case svc_serverdata:
		Cbuf_Execute();		/* make sure any stuffed commands are done */
		CL_ParseServerData(msg);
		break;

	case svc_configstring:
		CL_ParseConfigString(msg);
		break;

	case svc_sound:
		CL_ParseStartSoundPacket(msg);
		break;

	case svc_event:
		CL_ParseEvent(msg);
		break;

	case svc_bad:
		Com_Printf("CL_ParseServerMessage: bad server message %d\n", cmd);
		break;

	default:
		Com_Error(ERR_DROP,"CL_ParseServerMessage: Illegible server message %d", cmd);
		break;
	}
}
