/**
 * @file ui_main.c
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

#include "ui_main.h"
#include "ui_internal.h"
#include "ui_draw.h"
#include "ui_timer.h"
#include "ui_font.h"
#include "ui_sound.h"

#include "../client.h"

/** @todo client code should manage itself this vars */
cvar_t *mn_sequence;
cvar_t *mn_hud;

uiGlobal_t ui_global;

struct memPool_s *ui_dynStringPool;
struct memPool_s *ui_dynPool;
struct memPool_s *ui_sysPool;

#ifdef DEBUG
static cvar_t *ui_debug;
#endif

/**
 * @sa UI_DisplayNotice
 * @todo move it into a better file
 */
static void UI_CheckCvar (const cvar_t *cvar)
{
	if (cvar->modified) {
		if (cvar->flags & CVAR_R_CONTEXT) {
			UI_DisplayNotice(_("This change requires a restart"), 2000, NULL);
		} else if (cvar->flags & CVAR_R_IMAGES) {
			UI_DisplayNotice(_("This change might require a restart"), 2000, NULL);
		}
	}
}

/**
 * @sa UI_DisplayNotice
 * @todo move it into a better file
 */
int UI_DebugMode (void)
{
#ifdef DEBUG
	return ui_debug->integer;
#else
	return 0;
#endif
}

/**
 * @param[in] name Name of the cvar
 * @param[in] str Might be NULL if you want to set a float value
 * @param[in] value Float value to set
 * @todo move it into a better file
 */
void UI_SetCvar (const char *name, const char *str, float value)
{
	const cvar_t *cvar;
	cvar = Cvar_FindVar(name);
	if (!cvar) {
		Com_Printf("Could not find cvar '%s'\n", name);
		return;
	}
	if (str)
		Cvar_Set(cvar->name, str);
	else
		Cvar_SetValue(cvar->name, value);
	UI_CheckCvar(cvar);
}

/**
 * @brief Adds a given value to the numeric representation of a cvar. Also
 * performs min and max check for that value.
 * @sa UI_ModifyWrap_f
 */
static void UI_Modify_f (void)
{
	float value;

	if (Cmd_Argc() < 5)
		Com_Printf("Usage: %s <name> <amount> <min> <max>\n", Cmd_Argv(0));

	value = Cvar_GetValue(Cmd_Argv(1));
	value += atof(Cmd_Argv(2));
	if (value < atof(Cmd_Argv(3)))
		value = atof(Cmd_Argv(3));
	else if (value > atof(Cmd_Argv(4)))
		value = atof(Cmd_Argv(4));

	Cvar_SetValue(Cmd_Argv(1), value);
}

/**
 * @brief Adds a given value to the numeric representation of a cvar. Also
 * performs min and max check for that value. If there would be an overflow
 * we use the min value - and if there would be an underrun, we use the max
 * value.
 * @sa UI_Modify_f
 */
static void UI_ModifyWrap_f (void)
{
	float value;

	if (Cmd_Argc() < 5) {
		Com_Printf("Usage: %s <name> <amount> <min> <max>\n", Cmd_Argv(0));
		return;
	}

	value = Cvar_GetValue(Cmd_Argv(1));
	value += atof(Cmd_Argv(2));
	if (value < atof(Cmd_Argv(3)))
		value = atof(Cmd_Argv(4));
	else if (value > atof(Cmd_Argv(4)))
		value = atof(Cmd_Argv(3));

	Cvar_SetValue(Cmd_Argv(1), value);
}

/**
 * @brief Shows the corresponding strings in the UI
 * Example: Options window - fullscreen: yes
 */
static void UI_Translate_f (void)
{
	const char *current, *list;
	char original[MAX_VAR], translation[MAX_VAR];

	if (Cmd_Argc() < 4) {
		Com_Printf("Usage: %s <source> <dest> <list>\n", Cmd_Argv(0));
		return;
	}

	current = Cvar_GetString(Cmd_Argv(1));
	list = Cmd_Argv(3);

	while (list[0] != '\0') {
		char *trans;
		char *orig = original;
		while (list[0] != '\0' && list[0] != ':') {
			/** @todo overflow check */
			*orig++ = *list++;
		}
		*orig = '\0';
		list++;

		trans = translation;
		while (list[0] != '\0' && list[0] != ',') {
			/** @todo overflow check */
			*trans++ = *list++;
		}
		*trans = '\0';
		list++;

		if (!strcmp(current, original)) {
			Cvar_Set(Cmd_Argv(2), _(translation));
			return;
		}
	}

	if (current[0] != '\0') {
		/* nothing found, copy value */
		Cvar_Set(Cmd_Argv(2), _(current));
	} else {
		Cvar_Set(Cmd_Argv(2), "");
	}
}

#ifdef DEBUG
/**
 * @brief display info about UI memory
 * @todo it can be nice to have number of nodes per behaviours
 */
static void UI_Memory_f (void)
{
	int i;
	const size_t nodeSize = sizeof(uiNode_t);
	size_t size;
	Com_Printf("Allocation:\n");
	Com_Printf("\t-Window allocation: %i/%i\n", ui_global.numWindows, UI_MAX_WINDOWS);
	Com_Printf("\t-Rendering window stack slot: %i\n", UI_MAX_WINDOWSTACK);
	Com_Printf("\t-Action allocation: %i/%i\n", ui_global.numActions, UI_MAX_ACTIONS);
	Com_Printf("\t-Model allocation: %i/%i\n", ui_global.numModels, UI_MAX_MODELS);
	Com_Printf("\t-Exclude rect allocation: %i/%i\n", ui_global.numExcludeRect, UI_MAX_EXLUDERECTS);
	Com_Printf("\t -Node allocation: %i\n", ui_global.numNodes);

	Com_Printf("Memory:\n");
	Com_Printf("\t-Action structure size: "UFO_SIZE_T" B\n", sizeof(uiAction_t));
	Com_Printf("\t-Model structure size: "UFO_SIZE_T" B\n", sizeof(uiModel_t));
	Com_Printf("\t-Node structure size: "UFO_SIZE_T" B x%d\n", sizeof(uiNode_t), ui_global.numNodes);
	for (i = 0; i < UI_GetNodeBehaviourCount(); i++) {
		uiBehaviour_t *b = UI_GetNodeBehaviourByIndex(i);
		Com_Printf("\t -Behaviour %20s structure size: "UFO_SIZE_T" (+"UFO_SIZE_T" B) x%4u\n", b->name, sizeof(uiNode_t) + b->extraDataSize, b->extraDataSize, b->count);
	}

	size = 0;
	for (i = 0; i < UI_GetNodeBehaviourCount(); i++) {
		uiBehaviour_t *b = UI_GetNodeBehaviourByIndex(i);
		size += nodeSize * b->count + b->extraDataSize * b->count;
	}
	Com_Printf("Global memory:\n");
	Com_Printf("\t-System pool: %ui B\n", _Mem_PoolSize(ui_sysPool));
	Com_Printf("\t -AData allocation: "UFO_SIZE_T"/%i B\n", (ptrdiff_t)(ui_global.curadata - ui_global.adata), ui_global.adataize);
	Com_Printf("\t -AData used by nodes: "UFO_SIZE_T" B\n", size);
	Com_Printf("\t-Dynamic node/data pool: %ui B\n", _Mem_PoolSize(ui_dynPool));
	Com_Printf("\t-Dynamic strings pool: %ui B\n", _Mem_PoolSize(ui_dynStringPool));

	size = _Mem_PoolSize(ui_sysPool) + _Mem_PoolSize(ui_dynPool) + _Mem_PoolSize(ui_dynStringPool);
	Com_Printf("\t-Full size: "UFO_SIZE_T" B\n", size);
}
#endif

#define MAX_CONFUNC_SIZE 512
/**
 * @brief Executes confunc - just to identify those confuncs in the code - in
 * this frame.
 * @param[in] fmt Construct string it will execute (command and param)
 */
void UI_ExecuteConfunc (const char *fmt, ...)
{
	va_list ap;
	char confunc[MAX_CONFUNC_SIZE];

	va_start(ap, fmt);
	Q_vsnprintf(confunc, sizeof(confunc), fmt, ap);
	Cmd_ExecuteString(confunc);
	va_end(ap);
}

/**
 * Reinit input and font
 */
void UI_Reinit (void)
{
	UI_InitFonts();
	UI_ReleaseInput();
	UI_InvalidateMouse();
}

/**
 * @brief Reset and free the UI data hunk
 * @note Even called in case of an error when CL_Shutdown was called - maybe even
 * before CL_InitLocal (and thus UI_Init) was called
 * @sa CL_Shutdown
 * @sa UI_Init
 */
void UI_Shutdown (void)
{
	int i;
	const uiBehaviour_t *confunc;

	/* MN is not yet initialized */
	if (ui_global.adata == NULL)
		return;

	confunc = UI_GetNodeBehaviour("confunc");

	/* remove all confunc commands */
	for (i = 0; i < ui_global.numWindows; i++) {
		uiNode_t *node = ui_global.windows[i];
		while (node) {

			/* remove the command */
			if (node->behaviour == confunc) {
				/* many nodes to one command is allowed */
				if (Cmd_Exists(node->name))
					Cmd_RemoveCommand(node->name);
			}

			/* recursive next */
			if (node->firstChild != NULL)
				node = node->firstChild;
			else {
				while (node) {
					if (node->next != NULL) {
						node = node->next;
						break;
					}
					node = node->parent;
				}
			}
		}
	}

	if (ui_global.adataize)
		Mem_Free(ui_global.adata);
	ui_global.adata = NULL;
	ui_global.adataize = 0;

	/* release pools */
	Mem_FreePool(ui_sysPool);
	Mem_FreePool(ui_dynStringPool);
	Mem_FreePool(ui_dynPool);
	ui_sysPool = NULL;
	ui_dynStringPool = NULL;
	ui_dynPool = NULL;
}

#define UI_HUNK_SIZE 2*1024*1024

void UI_Init (void)
{
#ifdef DEBUG
	ui_debug = Cvar_Get("debug_ui", "0", CVAR_DEVELOPER, "Prints node names for debugging purposes - valid values are 1 and 2");
#endif

	/* reset global UI structures */
	memset(&ui_global, 0, sizeof(ui_global));

	/* add cvars */
	/** @todo not sure it is a ui var */
	mn_sequence = Cvar_Get("mn_sequence", "sequence", 0, "This is the sequence window to render the sequence in");
	/** @todo should be a client Cvar, not a ui */
	mn_hud = Cvar_Get("mn_hud", "hud", CVAR_ARCHIVE | CVAR_LATCH, "This is the current selected HUD");

	ui_sounds = Cvar_Get("ui_sounds", "1", CVAR_ARCHIVE, "Activates UI sounds");

	/* add global UI commands */
	Cmd_AddCommand("mn_modify", UI_Modify_f, NULL);
	Cmd_AddCommand("mn_modifywrap", UI_ModifyWrap_f, NULL);
	Cmd_AddCommand("mn_translate", UI_Translate_f, NULL);
#ifdef DEBUG
	Cmd_AddCommand("debug_mnmemory", UI_Memory_f, "Display info about UI memory allocation");
#endif

	ui_sysPool = Mem_CreatePool("Client: UI");
	ui_dynStringPool = Mem_CreatePool("Client: Dynamic string for UI");
	ui_dynPool = Mem_CreatePool("Client: Dynamic memory for UI");

	/* 256kb */
	ui_global.adataize = UI_HUNK_SIZE;
	ui_global.adata = (byte*)Mem_PoolAlloc(ui_global.adataize, ui_sysPool, 0);
	ui_global.curadata = ui_global.adata;

	UI_InitData();
	UI_InitNodes();
	UI_InitWindows();
	UI_InitDraw();
	UI_InitActions();
}
