/**
 * @file sv_game.c
 * @brief Interface to the game library.
 */

/*
All original materal Copyright (C) 2002-2006 UFO: Alien Invasion team.

26/06/06, Eddy Cullen (ScreamingWithNoSound):
	Reformatted to agreed style.
	Added doxygen file comment.
	Updated copyright notice.

Original file from Quake 2 v3.21: quake2-2.31/server/sv_game.c
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

#if defined DEBUG && defined _MSC_VER
#include <intrin.h>
#endif

game_export_t *ge;

/**
 * @brief Sends the contents of the mutlicast buffer to a single client
 */
static void PF_Unicast(player_t * player)
{
	client_t *client;

	if (!player || !player->inuse)
		return;

	if (player->num < 0 || player->num >= ge->max_players)
		return;

	client = svs.clients + player->num;

	SZ_Write(&client->netchan.message, sv.multicast.data, sv.multicast.cursize);

	SZ_Clear(&sv.multicast);
}

/**
 * @brief Debug print to server console
 */
static void PF_dprintf(char *fmt, ...)
{
	char msg[1024];
	va_list argptr;

	va_start(argptr, fmt);
	vsprintf(msg, fmt, argptr);
	va_end(argptr);

	Com_Printf("%s", msg);
}


/**
 * @brief Print to a single client
 */
static void PF_cprintf(player_t * player, int level, char *fmt, ...)
{
	char msg[1024];
	va_list argptr;
	int n;

	if (level == PRINT_NONE)
		return;

	n = 0;
	if (player) {
		n = player->num;
		if (n < 0 || n >= ge->max_players)
			return;
	}

	va_start(argptr, fmt);
	vsprintf(msg, fmt, argptr);
	va_end(argptr);

	if (player)
		SV_ClientPrintf(svs.clients + n, level, "%s", msg);
	else
		Com_Printf("%s", msg);
}


/**
 * @brief Centerprint to a single client
 */
static void PF_centerprintf(player_t * player, char *fmt, ...)
{
	char msg[1024];
	va_list argptr;
	int n;

	if (!player)
		return;

	n = player->num;
	if (n < 0 || n >= ge->max_players)
		return;

	va_start(argptr, fmt);
	vsprintf(msg, fmt, argptr);
	va_end(argptr);

	MSG_WriteByte(&sv.multicast, svc_centerprint);
	MSG_WriteString(&sv.multicast, msg);
	PF_Unicast(player);
}


static void PF_error(char *fmt, ...) __attribute__((noreturn));
/**
 * @brief Abort the server with a game error
 */
static void PF_error(char *fmt, ...)
{
	char msg[1024];
	va_list argptr;

#if defined DEBUG && defined _MSC_VER
	__debugbreak();	/* break execution before game shutdown */
#endif

	va_start(argptr, fmt);
	vsprintf(msg, fmt, argptr);
	va_end(argptr);

	Com_Error(ERR_DROP, "Game Error: %s", msg);
}


/**
 * @brief
 * Also sets mins and maxs for inline bmodels
 */
static void PF_SetModel(edict_t * ent, char *name)
{
	cmodel_t *mod;

	if (!name)
		Com_Error(ERR_DROP, "PF_setmodel: NULL");

	ent->modelindex = SV_ModelIndex(name);

	/* if it is an inline model, get the size information for it */
	if (name[0] == '*') {
		mod = CM_InlineModel(name);
		VectorCopy(mod->mins, ent->mins);
		VectorCopy(mod->maxs, ent->maxs);
		ent->solid = SOLID_BSP;
		SV_LinkEdict(ent);
	}
}

/**
 * @brief
 * @sa CL_ParseConfigString
 */
static void PF_Configstring(int index, char *val)
{
	if (index < 0 || index >= MAX_CONFIGSTRINGS)
		Com_Error(ERR_DROP, "configstring: bad index %i\n", index);

	if (!val)
		val = "";

	/* change the string in sv */
	/* there may be overflows in i==CS_TILES - but thats ok */
	/* see definition of configstrings and MAX_TILESTRINGS */
	switch (index) {
	case CS_TILES:
	case CS_POSITIONS:
		Q_strncpyz(sv.configstrings[index], val, MAX_TOKEN_CHARS*MAX_TILESTRINGS);
		break;
	default:
		Q_strncpyz(sv.configstrings[index], val, MAX_TOKEN_CHARS);
		break;
	}

	if (sv.state != ss_loading) {	/* send the update to everyone */
		SZ_Clear(&sv.multicast);
		MSG_WriteChar(&sv.multicast, svc_configstring);
		MSG_WriteShort(&sv.multicast, index);
		MSG_WriteString(&sv.multicast, val);

		SV_Multicast(~0);
	}
}

/**
 * @brief
 */
static void PF_WriteChar(int c)
{
	MSG_WriteChar(&sv.multicast, c);
}

/**
 * @brief
 */
#ifdef DEBUG
static void PF_WriteByte(int c, char* file, int line)
{
	MSG_WriteByteDebug(&sv.multicast, c, file, line);
}
#else
static void PF_WriteByte(int c)
{
	MSG_WriteByte(&sv.multicast, c);
}
#endif

/**
 * @brief
 */
#ifdef DEBUG
static void PF_WriteShort(int c, char* file, int line)
{
	MSG_WriteShortDebug(&sv.multicast, c, file, line);
}
#else
static void PF_WriteShort(int c)
{
	MSG_WriteShort(&sv.multicast, c);
}
#endif

/**
 * @brief
 */
static void PF_WriteLong(int c)
{
	MSG_WriteLong(&sv.multicast, c);
}

/**
 * @brief
 */
static void PF_WriteFloat(float f)
{
	MSG_WriteFloat(&sv.multicast, f);
}

/**
 * @brief
 */
static void PF_WriteString(char *s)
{
	MSG_WriteString(&sv.multicast, s);
}

/**
 * @brief
 */
static void PF_WritePos(vec3_t pos)
{
	MSG_WritePos(&sv.multicast, pos);
}

/**
 * @brief
 */
static void PF_WriteGPos(pos3_t pos)
{
	MSG_WriteGPos(&sv.multicast, pos);
}

/**
 * @brief
 */
static void PF_WriteDir(vec3_t dir)
{
	MSG_WriteDir(&sv.multicast, dir);
}

/**
 * @brief
 */
static void PF_WriteAngle(float f)
{
	MSG_WriteAngle(&sv.multicast, f);
}

static void PF_WriteFormat(char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	MSG_V_WriteFormat(&sv.multicast, format, ap);
	va_end(ap);
}

static byte *pf_save;
/**
 * @brief
 */
static void PF_WriteNewSave(int c)
{
#ifdef PARANOID
	if (c < SHRT_MIN || c > USHRT_MAX)
		Com_Printf("MSG_WriteShort: range error %i.\n", c);
#endif

 	pf_save = sv.multicast.data + sv.multicast.cursize;
	MSG_WriteShort(&sv.multicast, c);

}

/**
 * @brief
 */
static void PF_WriteToSave(int c)
{
#ifdef PARANOID
	if (c < SHRT_MIN || c > USHRT_MAX)
		Com_Printf("MSG_WriteShort: range error %i.\n", c);
#endif

	pf_save[0] = c & UCHAR_MAX; /* a hack, see MSG_WriteShort */
 	pf_save[1] = (c >> 8) & UCHAR_MAX;
}

/**
 * @brief
 */
static int PF_ReadChar(void)
{
	return MSG_ReadChar(&net_message);
}

/**
 * @brief
 */
static int PF_ReadByte(void)
{
	return MSG_ReadByte(&net_message);
}

/**
 * @brief
 */
static int PF_ReadShort(void)
{
	return MSG_ReadShort(&net_message);
}

/**
 * @brief
 */
static int PF_ReadLong(void)
{
	return MSG_ReadLong(&net_message);
}

/**
 * @brief
 */
static float PF_ReadFloat(void)
{
	return MSG_ReadFloat(&net_message);
}

/**
 * @brief
 */
static char *PF_ReadString(void)
{
	return MSG_ReadString(&net_message);
}

/**
 * @brief
 */
static void PF_ReadPos(vec3_t pos)
{
	MSG_ReadPos(&net_message, pos);
}

/**
 * @brief
 */
static void PF_ReadGPos(pos3_t pos)
{
	MSG_ReadGPos(&net_message, pos);
}

/**
 * @brief
 */
static void PF_ReadDir(vec3_t vector)
{
	MSG_ReadDir(&net_message, vector);
}

/**
 * @brief
 */
static float PF_ReadAngle(void)
{
	return MSG_ReadAngle(&net_message);
}

/**
 * @brief
 */
static void PF_ReadData(void *buffer, int size)
{
	MSG_ReadData(&net_message, buffer, size);
}

static void PF_ReadFormat(char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	MSG_V_ReadFormat(&net_message, format, ap);
	va_end(ap);
}

static qboolean pfe_pending = qfalse;
static int pfe_mask = 0;

/**
 * @brief
 */
static void PF_EndEvents(void)
{
	if (!pfe_pending)
		return;

	MSG_WriteByte(&sv.multicast, EV_NULL);
	SV_Multicast(pfe_mask);
/*	SV_SendClientMessages(); */
	pfe_pending = qfalse;
}

/**
 * @brief
 */
static void PF_AddEvent(int mask, int eType)
{
	if (!pfe_pending || mask != pfe_mask) {
		/* the target clients have changed or nothing is pending */
		if (pfe_pending)
			/* finish the last event chain */
			PF_EndEvents();

		/* start the new event */
		pfe_pending = qtrue;
		pfe_mask = mask;
		MSG_WriteByte(&sv.multicast, svc_event);
	}

	/* write header */
	MSG_WriteByte(&sv.multicast, eType);
}

/**
 * @brief Called when either the entire server is being killed, or it is changing to a different game directory.
 */
void SV_ShutdownGameProgs(void)
{
	if (!ge)
		return;
	ge->Shutdown();
	Sys_UnloadGame();
	ge = NULL;
}

/**
 * @brief
 */
void SCR_DebugGraph(float value, int color);

/**
 * @brief Init the game subsystem for a new map
 */
void SV_InitGameProgs(void)
{
	game_import_t import;

	/* unload anything we have now */
	if (ge)
		SV_ShutdownGameProgs();

	/* load a new game dll */
	import.multicast = SV_Multicast;
	import.unicast = PF_Unicast;
	import.bprintf = SV_BroadcastPrintf;
	import.dprintf = PF_dprintf;
	import.cprintf = PF_cprintf;
	import.centerprintf = PF_centerprintf;
	import.error = PF_error;

	import.trace = SV_Trace;
	import.linkentity = SV_LinkEdict;
	import.unlinkentity = SV_UnlinkEdict;

	import.TestLine = CM_TestLine;
	import.GrenadeTarget = Com_GrenadeTarget;

	import.MoveCalc = Grid_MoveCalc;
	import.MoveStore = Grid_MoveStore;
	import.MoveLength = Grid_MoveLength;
	import.MoveNext = Grid_MoveNext;
	import.GridHeight = Grid_Height;
	import.GridFall = Grid_Fall;
	import.GridPosToVec = Grid_PosToVec;
	import.GridRecalcRouting = Grid_RecalcRouting;

	import.modelindex = SV_ModelIndex;
	import.soundindex = SV_SoundIndex;
	import.imageindex = SV_ImageIndex;

	import.setmodel = PF_SetModel;

	import.configstring = PF_Configstring;
	import.positioned_sound = SV_StartSound;

	import.WriteChar = PF_WriteChar;
	import.WriteByte = PF_WriteByte;
	import.WriteShort = PF_WriteShort;
	import.WriteLong = PF_WriteLong;
	import.WriteFloat = PF_WriteFloat;
	import.WriteString = PF_WriteString;
	import.WritePos = PF_WritePos;
	import.WriteGPos = PF_WriteGPos;
	import.WriteDir = PF_WriteDir;
	import.WriteAngle = PF_WriteAngle;
	import.WriteFormat = PF_WriteFormat;

	import.WriteNewSave = PF_WriteNewSave;
	import.WriteToSave = PF_WriteToSave;

	import.EndEvents = PF_EndEvents;
	import.AddEvent = PF_AddEvent;

	import.ReadChar = PF_ReadChar;
	import.ReadByte = PF_ReadByte;
	import.ReadShort = PF_ReadShort;
	import.ReadLong = PF_ReadLong;
	import.ReadFloat = PF_ReadFloat;
	import.ReadString = PF_ReadString;
	import.ReadPos = PF_ReadPos;
	import.ReadGPos = PF_ReadGPos;
	import.ReadDir = PF_ReadDir;
	import.ReadAngle = PF_ReadAngle;
	import.ReadData = PF_ReadData;
	import.ReadFormat = PF_ReadFormat;

	import.GetModelAndName = Com_GetModelAndName;

	import.TagMalloc = Mem_TagMalloc;
	import.TagFree = Mem_Free;
	import.FreeTags = Mem_FreeTags;

	import.cvar = Cvar_Get;
	import.cvar_set = Cvar_Set;
	import.cvar_forceset = Cvar_ForceSet;
	import.cvar_string = Cvar_VariableString;

	import.argc = Cmd_Argc;
	import.argv = Cmd_Argv;
	import.args = Cmd_Args;
	import.AddCommandString = Cbuf_AddText;

	import.DebugGraph = SCR_DebugGraph;
/*	import.SetAreaPortalState = CM_SetAreaPortalState; */
/*	import.AreasConnected = CM_AreasConnected; */

	import.seed = Sys_Milliseconds();
	import.csi = &csi;
	import.map = (void *) &svMap;

	ge = Sys_GetGameAPI(&import);

	if (!ge)
		Com_Error(ERR_DROP, "failed to load game library");
	if (ge->apiversion != GAME_API_VERSION)
		Com_Error(ERR_DROP, "game is version %i, not %i", ge->apiversion, GAME_API_VERSION);

	ge->Init();
}
