/**
 * @file cl_menu.c
 * @brief Client menu functions.
 */

/*
Copyright (C) 2002-2007 UFO: Alien Invasion team.

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
#include "cl_global.h"

#define MENU_HUNK_SIZE 0x40000

message_t *messageStack;

/** @brief Stores all chat messages from a multiplayer game */
typedef struct chatMessage_s {
	char *text;
	struct chatMessage_s *next;
} chatMessage_t;

static chatMessage_t *chatMessageStack = NULL;

static menuNode_t *focusNode;

static void CL_ShowChatMessagesOnStack_f(void);

static const vec4_t tooltipBG = { 0.0f, 0.0f, 0.0f, 0.7f };
static const vec4_t tooltipColor = { 0.0f, 0.8f, 0.0f, 1.0f };

/* =========================================================== */

typedef enum ea_s {
	EA_NULL,
	EA_CMD,

	EA_CALL,
	EA_NODE,
	EA_VAR,

	EA_NUM_EVENTACTION
} ea_t;

/** @brief valid node event actions */
static const char *ea_strings[EA_NUM_EVENTACTION] = {
	"",
	"cmd",

	"",
	"*",
	"&"
};

static const int ea_values[EA_NUM_EVENTACTION] = {
	V_NULL,
	V_LONGSTRING,

	V_NULL,
	V_NULL,
	V_NULL
};

/* =========================================================== */

typedef enum ne_s {
	NE_NULL,
	NE_CLICK,
	NE_RCLICK,
	NE_MCLICK,
	NE_WHEEL,
	NE_MOUSEIN,
	NE_MOUSEOUT,
	NE_WHEELUP,
	NE_WHEELDOWN,

	NE_NUM_NODEEVENT
} ne_t;

/** @brief valid node event ids */
static const char *ne_strings[NE_NUM_NODEEVENT] = {
	"",
	"click",
	"rclick",
	"mclick",
	"wheel",
	"in",
	"out",
	"whup",
	"whdown"
};

static size_t const ne_values[NE_NUM_NODEEVENT] = {
	0,
	offsetof(menuNode_t, click),
	offsetof(menuNode_t, rclick),
	offsetof(menuNode_t, mclick),
	offsetof(menuNode_t, wheel),
	offsetof(menuNode_t, mouseIn),
	offsetof(menuNode_t, mouseOut),
	offsetof(menuNode_t, wheelUp),
	offsetof(menuNode_t, wheelDown)
};

/* =========================================================== */

/** @brief valid properties for a menu node */
static const value_t nps[] = {
	{"invis", V_BOOL, offsetof(menuNode_t, invis), MEMBER_SIZEOF(menuNode_t, invis)},
	{"mousefx", V_BOOL, offsetof(menuNode_t, mousefx), MEMBER_SIZEOF(menuNode_t, mousefx)},
	{"blend", V_BOOL, offsetof(menuNode_t, blend), MEMBER_SIZEOF(menuNode_t, blend)},
	{"texh", V_POS, offsetof(menuNode_t, texh), MEMBER_SIZEOF(menuNode_t, texh)},
	{"texl", V_POS, offsetof(menuNode_t, texl), MEMBER_SIZEOF(menuNode_t, texl)},
	{"border", V_INT, offsetof(menuNode_t, border), MEMBER_SIZEOF(menuNode_t, border)},
	{"padding", V_INT, offsetof(menuNode_t, padding), MEMBER_SIZEOF(menuNode_t, padding)},
	{"pos", V_POS, offsetof(menuNode_t, pos), MEMBER_SIZEOF(menuNode_t, pos)},
	{"size", V_POS, offsetof(menuNode_t, size), MEMBER_SIZEOF(menuNode_t, size)},
	{"format", V_POS, offsetof(menuNode_t, texh), MEMBER_SIZEOF(menuNode_t, texh)},
	{"origin", V_VECTOR, offsetof(menuNode_t, origin), MEMBER_SIZEOF(menuNode_t, origin)},
	{"center", V_VECTOR, offsetof(menuNode_t, center), MEMBER_SIZEOF(menuNode_t, center)},
	{"scale", V_VECTOR, offsetof(menuNode_t, scale), MEMBER_SIZEOF(menuNode_t, scale)},
	{"angles", V_VECTOR, offsetof(menuNode_t, angles), MEMBER_SIZEOF(menuNode_t, angles)},
	{"num", V_INT, offsetof(menuNode_t, num), MEMBER_SIZEOF(menuNode_t, num)},
	{"height", V_INT, offsetof(menuNode_t, height), MEMBER_SIZEOF(menuNode_t, height)},
	{"text_scroll", V_INT, offsetof(menuNode_t, textScroll), MEMBER_SIZEOF(menuNode_t, textScroll)},
	{"timeout", V_INT, offsetof(menuNode_t, timeOut), MEMBER_SIZEOF(menuNode_t, timeOut)},
	{"timeout_once", V_BOOL, offsetof(menuNode_t, timeOutOnce), MEMBER_SIZEOF(menuNode_t, timeOutOnce)},
	{"bgcolor", V_COLOR, offsetof(menuNode_t, bgcolor), MEMBER_SIZEOF(menuNode_t, bgcolor)},
	{"bordercolor", V_COLOR, offsetof(menuNode_t, bordercolor), MEMBER_SIZEOF(menuNode_t, bordercolor)},
	{"key", V_STRING, offsetof(menuNode_t, key), 0},
	/* 0, -1, -2, -3, -4, -5 fills the data array in menuNode_t */
	{"tooltip", V_STRING, -5, 0},	/* translated in MN_Tooltip */
	{"image", V_STRING, 0, 0},
	{"md2", V_STRING, 0, 0},
	{"anim", V_STRING, -1, 0},
	{"tag", V_STRING, -2, 0},
	{"cvar", V_STRING, -3, 0},	/* for selectbox */
	{"skin", V_STRING, -3, 0},
	/* -4 is animation state */
	{"string", V_LONGSTRING, 0, 0},	/* no gettext here - this can be a cvar, too */
	{"font", V_STRING, -1, 0},
	{"max", V_FLOAT, 0, 0},
	{"min", V_FLOAT, -1, 0},
	{"current", V_FLOAT, -2, 0},
	{"weapon", V_STRING, 0, 0},
	{"color", V_COLOR, offsetof(menuNode_t, color), MEMBER_SIZEOF(menuNode_t, color)},
	{"align", V_ALIGN, offsetof(menuNode_t, align), MEMBER_SIZEOF(menuNode_t, align)},
	{"if", V_IF, offsetof(menuNode_t, depends), 0},
	{"repeat", V_BOOL, offsetof(menuNode_t, repeat), MEMBER_SIZEOF(menuNode_t, repeat)},
	{"scrollbar", V_BOOL, offsetof(menuNode_t, scrollbar), MEMBER_SIZEOF(menuNode_t, scrollbar)},
	{"scrollbarleft", V_BOOL, offsetof(menuNode_t, scrollbarLeft), MEMBER_SIZEOF(menuNode_t, scrollbarLeft)},

	{NULL, V_NULL, 0, 0},
};

/** @brief valid properties for a select box */
static const value_t selectBoxValues[] = {
	{"label", V_TRANSLATION_MANUAL_STRING, offsetof(selectBoxOptions_t, label), 0},
	{"action", V_STRING, offsetof(selectBoxOptions_t, action), 0},
	{"value", V_STRING, offsetof(selectBoxOptions_t, value), 0},

	{NULL, V_NULL, 0, 0},
};

/** @brief valid properties for a menu model definition */
static const value_t menuModelValues[] = {
	{"model", V_CLIENT_HUNK_STRING, offsetof(menuModel_t, model), 0},
	{"need", V_NULL, 0, 0},
	{"menutransform", V_NULL, 0, 0},
	{"anim", V_CLIENT_HUNK_STRING, offsetof(menuModel_t, anim), 0},
	{"skin", V_INT, offsetof(menuModel_t, skin), sizeof(int)},
	{"origin", V_VECTOR, offsetof(menuModel_t, origin), sizeof(vec3_t)},
	{"center", V_VECTOR, offsetof(menuModel_t, center), sizeof(vec3_t)},
	{"scale", V_VECTOR, offsetof(menuModel_t, scale), sizeof(vec3_t)},
	{"angles", V_VECTOR, offsetof(menuModel_t, angles), sizeof(vec3_t)},
	{"color", V_COLOR, offsetof(menuModel_t, color), sizeof(vec4_t)},
	{"tag", V_CLIENT_HUNK_STRING, offsetof(menuModel_t, tag), 0},
	{"parent", V_CLIENT_HUNK_STRING, offsetof(menuModel_t, parent), 0},

	{NULL, V_NULL, 0, 0},
};

/* =========================================================== */

/** @brief possible menu node types */
typedef enum mn_s {
	MN_NULL,
	MN_CONFUNC,
	MN_CVARFUNC,
	MN_FUNC,
	MN_ZONE,
	MN_PIC,
	MN_STRING,
	MN_TEXT,
	MN_BAR,
	MN_MODEL,
	MN_CONTAINER,
	MN_ITEM,
	MN_MAP,
	MN_BASEMAP,
	MN_CHECKBOX,
	MN_SELECTBOX,
	MN_LINESTRIP,

	MN_NUM_NODETYPE
} mn_t;

/** @brief node type strings */
static const char *nt_strings[MN_NUM_NODETYPE] = {
	"",
	"confunc",
	"cvarfunc",
	"func",
	"zone",
	"pic",
	"string",
	"text",
	"bar",
	"model",
	"container",
	"item",
	"map",
	"basemap",
	"checkbox",
	"selectbox",
	"linestrip"
};


/* =========================================================== */


static menuModel_t menuModels[MAX_MENUMODELS];
static menuAction_t menuActions[MAX_MENUACTIONS];
static menuNode_t menuNodes[MAX_MENUNODES];
static menu_t menus[MAX_MENUS];

static int numActions;
static int numNodes;
static int numMenus;
static int numMenuModels;
static int numTutorials;

static byte *adata, *curadata;
static int adataize = 0;

static menu_t *menuStack[MAX_MENUSTACK];
static int menuStackPos = -1;

inventory_t *menuInventory = NULL;
/**
 * @brief Holds static array of characters to display
 * @note The array id is given via num in the menuNode definitions
 * @sa MN_MenuTextReset
 * @sa menuTextLinkedList
 */
const char *menuText[MAX_MENUTEXTS];
/**
 * @brief Holds a linked list for displaying in the menu
 * @note The array id is given via num in the menuNode definitions
 * @sa MN_MenuTextReset
 * @sa menuText
 */
linkedList_t *menuTextLinkedList[MAX_MENUTEXTS];

static selectBoxOptions_t menuSelectBoxes[MAX_SELECT_BOX_OPTIONS];
static int numSelectBoxes;

static cvar_t *mn_escpop;
static cvar_t *mn_debugmenu;
cvar_t *mn_inputlength;

char popupText[MAX_SMALLMENUTEXTLEN];

typedef struct tutorial_s {
	char name[MAX_VAR];
	char *sequence;
} tutorial_t;

#define MAX_TUTORIALS 16
static tutorial_t tutorials[MAX_TUTORIALS];

/* some function prototypes */
static void MN_GetMaps_f(void);
static void MN_NextMap_f(void);
static void MN_PrevMap_f(void);
static int MN_DrawTooltip(const char *font, char *string, int x, int y, int maxWidth, int maxHeight);
static void CL_ShowMessagesOnStack_f(void);
static void MN_TimestampedText(char *text, message_t *message, size_t textsize);

mouseRepeat_t mouseRepeat;

/**
 * @brief Just translate the bool value to translateable yes or no strings
 */
const char* MN_TranslateBool (qboolean value)
{
	if (value)
		return _("yes");
	else
		return _("no");
}

/**
 * @brief Adds a new selectbox option to a selectbox node
 * @sa MN_SELECTBOX
 * @return NULL if menuSelectBoxes is 'full' - otherwise pointer to the selectBoxOption
 * @param[in] node The node (must be of type MN_SELECTBOX) where you want to append
 * the option
 * @note You have to add the values manually to the option pointer
 */
selectBoxOptions_t* MN_AddSelectboxOption (menuNode_t *node)
{
	selectBoxOptions_t *selectBoxOption;

	assert(node->type == MN_SELECTBOX);

	if (numSelectBoxes >= MAX_SELECT_BOX_OPTIONS) {
		Com_Printf("MN_AddSelectboxOption: numSelectBoxes exceeded - increase MAX_SELECT_BOX_OPTIONS\n");
		return NULL;
	}
	/* initial options entry */
	if (!node->options)
		node->options = &menuSelectBoxes[numSelectBoxes];
	else {
		/* link it in */
		for (selectBoxOption = node->options; selectBoxOption->next; selectBoxOption = selectBoxOption->next);
		selectBoxOption->next = &menuSelectBoxes[numSelectBoxes];
		selectBoxOption->next->next = NULL;
	}
	selectBoxOption = &menuSelectBoxes[numSelectBoxes];
	node->height++;
	numSelectBoxes++;
	return selectBoxOption;
}

/**
 * @brief Returns the current active menu from the menu stack or NULL if there is none
 * @return menu_t pointer from menu stack
 * @sa MN_ActiveMenu
 */
static menu_t* inline MN_GetCurrentMenu (void)
{
	return (menuStackPos > 0 ? menuStack[menuStackPos - 1] : NULL);
}

/**
 * @brief Returns the current active menu from the menu stack or NULL if there is none
 * @return menu_t pointer from menu stack
 * @sa MN_GetCurrentMenu
 */
menu_t* MN_ActiveMenu (void)
{
	return MN_GetCurrentMenu();
}

/**
 * @brief Searches all menus for the specified one
 * @param[in] name If name is NULL this function will return the current menu
 * on the stack - otherwise it will search the hole stack for a menu with the
 * id name
 * @return NULL if not found or no menu on the stack
 */
menu_t *MN_GetMenu (const char *name)
{
	int i;

	/* get the current menu */
	if (name == NULL)
		return MN_GetCurrentMenu();

	for (i = 0; i < numMenus; i++)
		if (!Q_strncmp(menus[i].name, name, MAX_VAR))
			return &menus[i];

	Sys_Error("Could not find menu '%s'\n", name);
	return NULL;
}

/**
 * @brief Searches all nodes in the given menu for a given nodename
 * @sa MN_GetNodeFromCurrentMenu
 */
menuNode_t *MN_GetNode (const menu_t* const menu, const char *name)
{
	menuNode_t *node = NULL;

	if (!menu)
		return NULL;

	for (node = menu->firstNode; node; node = node->next)
		if (!Q_strncmp(name, node->name, sizeof(node->name)))
			break;

	return node;
}

/**
 * @brief Check the if conditions for a given node
 * @sa MN_DrawMenus
 * @sa V_IF
 * @returns qfalse if the node is not drawn due to not meet if conditions
 */
static qboolean MN_CheckCondition (menuNode_t *node)
{
	if (*node->depends.var) {
		const char* cond;
		/* menuIfCondition_t */
		switch (node->depends.cond) {
		case IF_EQ:
			if (atof(node->depends.value) != Cvar_Get(node->depends.var, node->depends.value, 0, NULL)->value)
				return qfalse;
			break;
		case IF_LE:
			if (Cvar_Get(node->depends.var, node->depends.value, 0, NULL)->value > atof(node->depends.value))
				return qfalse;
			break;
		case IF_GE:
			if (Cvar_Get(node->depends.var, node->depends.value, 0, NULL)->value < atof(node->depends.value))
				return qfalse;
			break;
		case IF_GT:
			if (Cvar_Get(node->depends.var, node->depends.value, 0, NULL)->value <= atof(node->depends.value))
				return qfalse;
			break;
		case IF_LT:
			if (Cvar_Get(node->depends.var, node->depends.value, 0, NULL)->value >= atof(node->depends.value))
				return qfalse;
			break;
		case IF_NE:
			if (Cvar_Get(node->depends.var, node->depends.value, 0, NULL)->value == atof(node->depends.value))
				return qfalse;
			break;
		case IF_EXISTS:
			cond = Cvar_VariableString(node->depends.var);
			if (!*cond)
				return qfalse;
			break;
		default:
			Sys_Error("Unknown condition for if statement: %i\n", node->depends.cond);
			break;
		}
	}
	return qtrue;
}

/**
 * @brief This will reinit the current visible menu
 * @note also available as script command mn_reinit
 */
static void MN_ReinitCurrentMenu_f (void)
{
	menu_t* menu = MN_GetCurrentMenu();
	/* initialize it */
	if (menu) {
		MN_ExecuteActions(menu, menu->initNode->click);
		Com_DPrintf(DEBUG_CLIENT, "Reinit %s\n", menu->name);
	}
}

/**
 * @brief Searches a given node in the current menu
 * @sa MN_GetNode
 */
menuNode_t* MN_GetNodeFromCurrentMenu (const char *name)
{
	return MN_GetNode(MN_GetCurrentMenu(), name);
}

/*
==============================================================
ACTION EXECUTION
==============================================================
*/

/**
 * @sa MN_FocusExecuteActionNode
 * @note node must not be in menu
 */
static menuNode_t *MN_GetNextActionNode (menuNode_t* node)
{
	if (node)
		node = node->next;
	while (node) {
		if (MN_CheckCondition(node) && !node->invis
		&& ((node->click && node->mouseIn) || node->mouseIn))
			return node;
		node = node->next;
	}
	return NULL;
}

/**
 * @sa MN_FocusExecuteActionNode
 * @sa MN_FocusNextActionNode
 * @sa IN_Frame
 * @sa Key_Event
 */
void MN_FocusRemove (void)
{
	if (mouseSpace != MS_MENU)
		return;

	if (focusNode)
		MN_ExecuteActions(focusNode->menu, focusNode->mouseOut);
	focusNode = NULL;
}

/**
 * @brief Execute the current focused action node
 * @note Action nodes are nodes with click defined
 * @sa Key_Event
 * @sa MN_FocusNextActionNode
 */
qboolean MN_FocusExecuteActionNode (void)
{
	if (mouseSpace != MS_MENU)
		return qfalse;

	if (focusNode) {
		if (focusNode->click) {
			MN_ExecuteActions(focusNode->menu, focusNode->click);
		}
		MN_ExecuteActions(focusNode->menu, focusNode->mouseOut);
		focusNode = NULL;
		return qtrue;
	}

	return qfalse;
}

/**
 * @sa MN_FocusRemove
 */
static qboolean MN_FocusSetNode (menuNode_t* node)
{
	if (!node) {
		MN_FocusRemove();
		return qfalse;
	}

	/* reset already focused node */
	MN_FocusRemove();

	focusNode = node;
	MN_ExecuteActions(node->menu, node->mouseIn);

	return qtrue;
}

/**
 * @brief Returns the number of currently renderer menus on the menustack
 * @note Checks for a render node - if invis is true there, it's the last
 * visible menu
 */
static int MN_GetVisibleMenuCount (void)
{
	/* stack pos */
	int sp = menuStackPos;
	while (sp > 0)
		if (menuStack[--sp]->renderNode)
			break;
	/*Com_DPrintf(DEBUG_CLIENT, "visible menus on stacks: %i\n", sp);*/
	return sp;
}

/**
 * @brief Set the focus to the next action node
 * @note Action nodes are nodes with click defined
 * @sa Key_Event
 * @sa MN_FocusExecuteActionNode
 */
qboolean MN_FocusNextActionNode (void)
{
	menu_t* menu;
	static int i = MAX_MENUSTACK + 1;	/* to cycle between all menus */

	if (mouseSpace != MS_MENU)
		return qfalse;

	if (i >= menuStackPos)
		i = MN_GetVisibleMenuCount();

	assert(i >= 0);

	if (focusNode) {
		menuNode_t *node = MN_GetNextActionNode(focusNode);
		if (node)
			return MN_FocusSetNode(node);
	}

	while (i < menuStackPos) {
		menu = menuStack[i++];
		if (MN_FocusSetNode(MN_GetNextActionNode(menu->firstNode)))
			return qtrue;
	}
	i = MN_GetVisibleMenuCount();

	/* no node to focus */
	MN_FocusRemove();

	return qfalse;
}

/**
 * @brief Sets new x and y coordinates for a given node
 */
void MN_SetNewNodePos (menuNode_t* node, int x, int y)
{
	if (node) {
		node->pos[0] = x;
		node->pos[1] = y;
	}
}

static const char *MN_GetReferenceString (const menu_t* const menu, char *ref)
{
	if (!ref)
		return NULL;
	if (*ref == '*') {
		char ident[MAX_VAR];
		const char *text, *token;
		char command[MAX_VAR];
		char param[MAX_VAR];

		/* get the reference and the name */
		text = ref + 1;
		token = COM_Parse(&text);
		if (!text)
			return NULL;
		Q_strncpyz(ident, token, MAX_VAR);
		token = COM_Parse(&text);
		if (!text)
			return NULL;

		if (!Q_strncmp(ident, "cvar", 4)) {
			/* get the cvar value */
			return Cvar_VariableString(token);
		} else if (!Q_strncmp(ident, "binding", 7)) {
			/* get the cvar value */
			if (*text && *text <= ' ') {
				/* check command and param */
				Q_strncpyz(command, token, MAX_VAR);
				token = COM_Parse(&text);
				Q_strncpyz(param, token, MAX_VAR);
				/*Com_sprintf(token, MAX_VAR, "%s %s", command, param);*/
			}
			return Key_GetBinding(token, (cls.state != ca_active ? KEYSPACE_MENU : KEYSPACE_GAME));
		} else if (!Q_strncmp(ident, "cmd", 3)) {
			/* @todo: get the command output */
			return "TOOD";
		} else {
			menuNode_t *refNode;
			const value_t *val;

			/* draw a reference to a node property */
			refNode = MN_GetNode(menu, ident);
			if (!refNode)
				return NULL;

			/* get the property */
			for (val = nps; val->type; val++)
				if (!Q_stricmp(token, val->string))
					break;

			if (!val->type)
				return NULL;

			/* get the string */
			/* 0, -1, -2, -3, -4, -5 fills the data array in menuNode_t */
			if ((val->ofs > 0) && (val->ofs < (size_t)-5))
				return Com_ValueToStr(refNode, val->type, val->ofs);
			else
				return Com_ValueToStr(refNode->data[-((int)val->ofs)], val->type, 0);
		}
	} else if (*ref == '_') {
		ref++;
		return _(ref);
	} else {
		/* just get the data */
		return ref;
	}
}


static float MN_GetReferenceFloat (const menu_t* const menu, void *ref)
{
	if (!ref)
		return 0.0;
	if (*(char *) ref == '*') {
		char ident[MAX_VAR];
		const char *token, *text;

		/* get the reference and the name */
		text = (char *) ref + 1;
		token = COM_Parse(&text);
		if (!text)
			return 0.0;
		Q_strncpyz(ident, token, MAX_VAR);
		token = COM_Parse(&text);
		if (!text)
			return 0.0;

		if (!Q_strncmp(ident, "cvar", 4)) {
			/* get the cvar value */
			return Cvar_VariableValue(token);
		} else {
			menuNode_t *refNode;
			const value_t *val;

			/* draw a reference to a node property */
			refNode = MN_GetNode(menu, ident);
			if (!refNode)
				return 0.0;

			/* get the property */
			for (val = nps; val->type; val++)
				if (!Q_stricmp(token, val->string))
					break;

			if (val->type != V_FLOAT)
				return 0.0;

			/* get the string */
			/* 0, -1, -2, -3, -4, -5 fills the data array in menuNode_t */
			if ((val->ofs > 0) && (val->ofs < (size_t)-5))
				return *(float *) ((byte *) refNode + val->ofs);
			else
				return *(float *) refNode->data[-((int)val->ofs)];
		}
	} else {
		/* just get the data */
		return *(float *) ref;
	}
}

/**
 * @brief Update the menu values with current gametype values
 */
static void MN_UpdateGametype_f (void)
{
	Com_SetGameType();
}

/**
 * @brief Switch to the next multiplayer game type
 * @sa MN_PrevGametype_f
 */
static void MN_ChangeGametype_f (void)
{
	int i, newType;
	gametype_t* gt;
	mapDef_t *md;
	const char *newGameTypeID = NULL;
	qboolean next = qtrue;

	/* no types defined or parsed */
	if (numGTs == 0)
		return;

	md = &csi.mds[ccs.multiplayerMapDefinitionIndex];
	if (!md || !md->multiplayer) {
		Com_Printf("MN_ChangeGametype_f: No mapdef for the map\n");
		return;
	}

	/* previous? */
	if (!Q_strcmp(Cmd_Argv(0), "mn_prevgametype")) {
		next = qfalse;
	}

	if (md->gameTypes) {
		linkedList_t *list = md->gameTypes;
		linkedList_t *old = NULL;
		while (list) {
			if (!Q_strcmp((const char*)list->data, gametype->string)) {
				if (next) {
					if (list->next)
						newGameTypeID = (const char *)list->next->data;
					else
						newGameTypeID = (const char *)md->gameTypes->data;
				} else {
					/* only one or the first entry */
					if (old) {
						newGameTypeID = (const char *)old->data;
					} else {
						while (list->next)
							list = list->next;
						newGameTypeID = (const char *)list->data;
					}
				}
				break;
			}
			old = list;
			list = list->next;
		}
		/* current value is not valid for this map, set to first valid gametype */
		if (!list)
			newGameTypeID = (const char *)md->gameTypes->data;
	} else {
		for (i = 0; i < numGTs; i++) {
			gt = &gts[i];
			if (!Q_strncmp(gt->id, gametype->string, MAX_VAR)) {
				if (next) {
					newType = (i + 1);
					if (newType >= numGTs)
						newType = 0;
				} else {
					newType = (i - 1);
					if (newType < 0)
						newType = numGTs - 1;
				}

				newGameTypeID = gts[newType].id;
				break;
			}
		}
	}
	if (newGameTypeID) {
		Cvar_Set("gametype", newGameTypeID);
		Com_SetGameType();
	}
}

/**
 * @brief Starts a server and checks if the server loads a team unless he is a dedicated
 * server admin
 */
static void MN_StartServer_f (void)
{
	char map[MAX_VAR];
	mapDef_t *md;
	cvar_t* mn_serverday = Cvar_Get("mn_serverday", "1", 0, "Decides whether the server starts the day or the night version of the selected map");
	aircraft_t *aircraft;

	if (ccs.singleplayer)
		return;

	aircraft = AIR_AircraftGetFromIdx(0);
	assert(aircraft);

	if (!sv_dedicated->integer && !B_GetNumOnTeam(aircraft)) {
		Com_Printf("MN_StartServer_f: Multiplayer team not loaded, please choose your team now.\n");
		Cmd_ExecuteString("assign_initial");
		return;
	}

	if (Cvar_VariableInteger("sv_teamplay")
		&& Cvar_VariableValue("sv_maxsoldiersperplayer") > Cvar_VariableValue("sv_maxsoldiersperteam")) {
		MN_Popup(_("Settings doesn't make sense"), _("Set soldiers per player lower than soldiers per team"));
		return;
	}

	md = &csi.mds[ccs.multiplayerMapDefinitionIndex];
	if (!md || !md->multiplayer)
		return;
	assert(md->map);

	Com_sprintf(map, sizeof(map), "map %s%c %s", md->map, mn_serverday->integer ? 'd' : 'n', md->param ? md->param : "");

	/* let the (local) server know which map we are running right now */
	csi.currentMD = md;

	Cmd_ExecuteString(map);

	Cvar_Set("mn_main", "multiplayerInGame");
	MN_PushMenu("multiplayer_wait");
	Cvar_Set("mn_active", "multiplayer_wait");
}

/**
 * @brief Draws the rectangle in a 'free' style on position posx/posy (pixel) in the size sizex/sizey (pixel)
 */
static void MN_DrawDisabled (menuNode_t* node)
{
	static vec4_t color = { 0.3f, 0.3f, 0.3f, 0.7f };
	R_DrawFill(node->pos[0], node->pos[1], node->size[0], node->size[1], ALIGN_UL, color);
}

/**
 * @brief Draws the rectangle in a 'free' style on position posx/posy (pixel) in the size sizex/sizey (pixel)
 */
static void MN_DrawFree (int container, menuNode_t * node, int posx, int posy, int sizex, int sizey, qboolean showTUs)
{
	static vec4_t color = { 0.0f, 1.0f, 0.0f, 0.7f };
	invDef_t* inv = &csi.ids[container];

	R_DrawFill(posx, posy, sizex, sizey, ALIGN_UL, color);

	/* if showTUs is true (only the first time in none single containers)
	 * and we are connected to a game */
	if (showTUs && cls.state == ca_active)
		R_FontDrawString("f_verysmall", 0, node->pos[0] + 3, node->pos[1] + 3,
			node->pos[0] + 3, node->pos[1] + 3, node->size[0] - 6, 0, 0,
			va(_("In: %i Out: %i"), inv->in, inv->out), 0, 0, NULL, qfalse);
}

/**
 * @brief Draws the free and usable inventory positions when dragging an item.
 * @note Only call this function in dragging mode (mouseSpace == MS_DRAG)
 */
static void MN_InvDrawFree (inventory_t * inv, menuNode_t * node)
{
	/* get the 'type' of the dragged item */
	int item = dragItem.t;
	int container = node->mousefx;
	uint32_t itemshape;

	qboolean showTUs = qtrue;

	/* The shape of the free positions. */
	uint32_t free[SHAPE_BIG_MAX_HEIGHT];
	int x, y;

	/* Draw only in dragging-mode and not for the equip-floor */
	assert(mouseSpace == MS_DRAG);
	assert(inv);

	/* if single container (hands, extension, headgear) */
	if (csi.ids[container].single) {
		/* if container is free or the dragged-item is in it */
		if (node->mousefx == dragFrom || Com_CheckToInventory(inv, item, container, 0, 0))
			MN_DrawFree(container, node, node->pos[0], node->pos[1], node->size[0], node->size[1], qtrue);
	} else {
		memset(free, 0, sizeof(free));
		for (y = 0; y < SHAPE_BIG_MAX_HEIGHT; y++) {
			for (x = 0; x < SHAPE_BIG_MAX_WIDTH; x++) {
				/* Check if the current position is usable (topleft of the item) */
				if (Com_CheckToInventory(inv, item, container, x, y)) {
					itemshape = csi.ods[dragItem.t].shape;
					/* add '1's to each position the item is 'blocking' */
					Com_MergeShapes(free, itemshape, x, y);
				}
				/* Only draw on existing positions */
				if (Com_CheckShape(csi.ids[container].shape, x, y)) {
					if (Com_CheckShape(free, x, y)) {
						MN_DrawFree(container, node, node->pos[0] + x * C_UNIT, node->pos[1] + y * C_UNIT, C_UNIT, C_UNIT, showTUs);
						showTUs = qfalse;
					}
				}
			}	/* for x */
		}	/* for y */
	}
}


/**
 * @brief Popup in geoscape
 * @note Only use static strings here - or use popupText if you really have to
 * build the string
 */
void MN_Popup (const char *title, const char *text)
{
	menuText[TEXT_POPUP] = title;
	menuText[TEXT_POPUP_INFO] = text;
	if (ccs.singleplayer)
		CL_GameTimeStop();
	MN_PushMenu("popup");
}

/**
 * @sa MN_ParseAction
 */
void MN_ExecuteActions (const menu_t* const menu, menuAction_t* const first)
{
	menuAction_t *action;
	byte *data;

	for (action = first; action; action = action->next)
		switch (action->type) {
		case EA_NULL:
			/* do nothing */
			break;
		case EA_CMD:
			/* execute a command */
			if (action->data)
				Cbuf_AddText(va("%s\n", (char*)action->data));
			break;
		case EA_CALL:
			/* call another function */
			MN_ExecuteActions(menu, **(menuAction_t ***) action->data);
			break;
		case EA_NODE:
			/* set a property */
			if (action->data) {
				menuNode_t *node;
				int np;

				data = action->data;
				data += ALIGN(strlen(action->data) + 1);
				np = *((int *) data);
				data += ALIGN(sizeof(int));

				assert(np >= 0);
				assert(np <= sizeof(nps) / sizeof(value_t));

				/* search the node */
				node = MN_GetNode(menu, (char *) action->data);

				if (!node) {
					/* didn't find node -> "kill" action and print error */
					action->type = EA_NULL;
					Com_Printf("MN_ExecuteActions: node \"%s\" doesn't exist\n", (char *) action->data);
					break;
				}

				/* 0, -1, -2, -3, -4, -5 fills the data array in menuNode_t */
				if ((nps[np].ofs > 0) && (nps[np].ofs < (size_t)-5))
					Com_SetValue(node, (char *) data, nps[np].type, nps[np].ofs, nps[np].size);
				else
					node->data[-((int)nps[np].ofs)] = data;
			}
			break;
		default:
			Sys_Error("unknown action type\n");
			break;
		}
}


static void MN_Command_f (void)
{
	menuNode_t *node;
	const char *name;
	int i;

	name = Cmd_Argv(0);

	/* first search all menus on the stack */
	for (i = 0; i < menuStackPos; i++)
		for (node = menuStack[i]->firstNode; node; node = node->next)
			if (node->type == MN_CONFUNC && !Q_strncmp(node->name, name, sizeof(node->name))) {
				/* found the node */
				Com_DPrintf(DEBUG_CLIENT, "MN_Command_f: menu '%s'\n", menuStack[i]->name);
				MN_ExecuteActions(menuStack[i], node->click);
				return;
			}

	/* not found - now query all in the menu definitions */
	for (i = 0; i < numMenus; i++)
		for (node = menus[i].firstNode; node; node = node->next)
			if (node->type == MN_CONFUNC && !Q_strncmp(node->name, name, sizeof(node->name))) {
				/* found the node */
				Com_DPrintf(DEBUG_CLIENT, "MN_Command_f: menu '%s'\n", menus[i].name);
				MN_ExecuteActions(&menus[i], node->click);
				return;
			}

	Com_Printf("MN_Command_f: confunc '%s' was not found in any menu\n", name);
}


/*
==============================================================
MENU ZONE DETECTION
==============================================================
*/

static void MN_FindContainer (menuNode_t* const node)
{
	invDef_t *id;
	int i, j;

	for (i = 0, id = csi.ids; i < csi.numIDs; id++, i++)
		if (!Q_strncmp(node->name, id->name, sizeof(node->name)))
			break;

	if (i == csi.numIDs)
		node->mousefx = NONE;
	else
		node->mousefx = id - csi.ids;

	/* start on the last bit of the shape mask */
	for (i = 31; i >= 0; i--) {
		for (j = 0; j < SHAPE_BIG_MAX_HEIGHT; j++)
			if (id->shape[j] & (1 << i))
				break;
		if (j < SHAPE_BIG_MAX_HEIGHT)
			break;
	}
	node->size[0] = C_UNIT * (i + 1) + 0.01;

	/* start on the lower row of the shape mask */
	for (i = SHAPE_BIG_MAX_HEIGHT - 1; i >= 0; i--)
		if (id->shape[i] & ~0x0)
			break;
	node->size[1] = C_UNIT * (i + 1) + 0.01;
}

#if 0
/** @todo to be integrated into MN_CheckNodeZone */
/**
 * @brief Check if the node is an image and if it is transparent on the given (global) position.
 * @param[in] node A menunode pointer to be checked.
 * @param[in] x X position on screen.
 * @param[in] y Y position on screen.
 * @return qtrue if an image is used and it is on transparent on the current position.
 */
static qboolean MN_NodeWithVisibleImage (menuNode_t* const node, int x, int y)
{
	byte *picture = NULL;	/**< Pointer to image (4 bytes == 1 pixel) */
	int width, height;	/**< Width and height for the pic. */
	int pic_x, pic_y;	/**< Position inside image */
	byte *color = NULL;	/**< Pointer to specific pixel in image. */

	if (!node || node->type != MN_PIC || !node->data[MN_DATA_STRING_OR_IMAGE_OR_MODEL])
		return qfalse;

	R_LoadImage(va("pics/menu/%s",path), &picture, &width, &height);

	if (!picture || !width || !height) {
		Com_DPrintf(DEBUG_CLIENT, "Couldn't load image %s in pics/menu\n", path);
		/* We return true here because we do not know if there is another image (withouth transparency) or another reason it might still be valid. */
		return qtrue;
	}

	/** @todo Get current location _inside_ image from global position. CHECKME */
	pic_x = x - node->pos[0];
	pic_y = y - node->pos[1];

	if (pic_x < 0 || pic_y < 0)
		return qfalse;

	/* Get pixel at current location. */
	color = picture + (4 * height * pic_y + 4 * pic_x); /* 4 means 4 values for each point */

	/* Return qtrue if pixel is visible (we check the alpha value here). */
	if (color[3] != 0)
		return qtrue;

	/* Image is transparent at this position. */
	return qfalse;
}
#endif

/**
 * @sa MN_Click
 */
static qboolean MN_CheckNodeZone (menuNode_t* const node, int x, int y)
{
	int sx, sy, tx, ty, i;

	/* don't hover nodes if we are executing an action on geoscape like rotating or moving */
	if (mouseSpace >= MS_ROTATE && mouseSpace <= MS_SHIFT3DMAP)
		return qfalse;

	if (focusNode && mouseSpace != MS_NULL)
		return qfalse;

	switch (node->type) {
	case MN_CONTAINER:
		if (node->mousefx == C_UNDEFINED)
			MN_FindContainer(node);
		if (node->mousefx == NONE)
			return qfalse;

		/* check bounding box */
		if (x < node->pos[0] || x > node->pos[0] + node->size[0] || y < node->pos[1] || y > node->pos[1] + node->size[1])
			return qfalse;

		/* found a container */
		return qtrue;
	/* checkboxes don't need action nodes */
	case MN_CHECKBOX:
	case MN_SELECTBOX:
		break;
	default:
		/* check for click action */
		if (node->invis || (!node->click && !node->rclick && !node->mclick && !node->wheel && !node->mouseIn && !node->mouseOut && !node->wheelUp && !node->wheelDown)
		 || !MN_CheckCondition(node))
			return qfalse;
	}

	if (!node->size[0] || !node->size[1]) {
		if (node->type == MN_CHECKBOX) {
			/* the checked and unchecked should always have the same dimensions */
			if (!node->data[MN_DATA_STRING_OR_IMAGE_OR_MODEL]) {
				R_DrawGetPicSize(&sx, &sy, "menu/checkbox_checked");
			} else {
				R_DrawGetPicSize(&sx, &sy, va("%s_checked", (char*)node->data[MN_DATA_STRING_OR_IMAGE_OR_MODEL]));
			}
		} else if (node->type == MN_PIC && node->data[MN_DATA_STRING_OR_IMAGE_OR_MODEL]) {
			if (node->texh[0] && node->texh[1]) {
				sx = node->texh[0] - node->texl[0];
				sy = node->texh[1] - node->texl[1];
			} else
				R_DrawGetPicSize(&sx, &sy, node->data[MN_DATA_STRING_OR_IMAGE_OR_MODEL]);
		} else
			return qfalse;
#if 0
		/* @todo: Check this */
		Vector2Set(node->size, sx, sy); /* speed up further calls */
#endif
	} else {
		sx = node->size[0];
		sy = node->size[1];
	}

	/* hardcoded value for select box from selectbox.tga image */
	if (node->type == MN_SELECTBOX) {
		sx += 20;
		/* state is set when the selectbox is hovered */
		if (node->state) {
			int hoverOptionID;
			sy += (node->size[1] * node->height);
			/* hover a given entry in the list */
			hoverOptionID = (y - node->pos[1]);

			if (node->size[1])
				hoverOptionID = (hoverOptionID - node->size[1]) / node->size[1];
			else
				hoverOptionID = (hoverOptionID - SELECTBOX_DEFAULT_HEIGHT) / SELECTBOX_DEFAULT_HEIGHT;

			if (hoverOptionID >= 0 && hoverOptionID < node->height) {
				node->options[hoverOptionID].hovered = qtrue;
			}
		}
	}

	tx = x - node->pos[0];
	ty = y - node->pos[1];
	if (node->align > 0 && node->align < ALIGN_LAST) {
		switch (node->align % 3) {
		/* center */
		case 1:
			tx = x - node->pos[0] + sx / 2;
			break;
		/* right */
		case 2:
			tx = x - node->pos[0] + sx;
			break;
		}
	}

	if (tx < 0 || ty < 0 || tx > sx || ty > sy)
		return qfalse;

	for (i = 0; i < node->excludeNum; i++) {
		if (x >= node->exclude[i].pos[0] && x <= node->exclude[i].pos[0] + node->exclude[i].size[0]
		 && y >= node->exclude[i].pos[1] && y <= node->exclude[i].pos[1] + node->exclude[i].size[1])
			return qfalse;
	}

	/* on the node */
	if (node->type == MN_TEXT) {
		assert(node->texh[0]);
		if (node->textScroll)
			return (int) (ty / node->texh[0]) + node->textScroll + 1;
		else
			return (int) (ty / node->texh[0]) + 1;
	} else
		return qtrue;
}

/**
 * @sa IN_Parse
 */
qboolean MN_CursorOnMenu (int x, int y)
{
	menuNode_t *node;
	menu_t *menu;
	int sp;

	sp = menuStackPos;

	while (sp > 0) {
		menu = menuStack[--sp];
		for (node = menu->firstNode; node; node = node->next)
			if (MN_CheckNodeZone(node, x, y)) {
				/* found an element */
				/*MN_FocusSetNode(node);*/
				return qtrue;
			}

		if (menu->renderNode) {
			/* don't care about non-rendered windows */
			if (menu->renderNode->invis)
				return qtrue;
			else
				return qfalse;
		}
	}

	return qfalse;
}


/**
 * @note: node->mousefx is the container id
 */
static void MN_Drag (const menuNode_t* const node, int x, int y, qboolean rightClick)
{
	int px, py, sel;

	if (!menuInventory)
		return;

	/* don't allow this in tactical missions */
	if (selActor && rightClick)
		return;

	sel = cl_selected->integer;
	if (sel < 0)
		return;

	if (mouseSpace == MS_MENU) {
		invList_t *ic;

		/* normalize it */
		px = (int) (x - node->pos[0]) / C_UNIT;
		py = (int) (y - node->pos[1]) / C_UNIT;

		/* start drag (mousefx represents container number) */
		ic = Com_SearchInInventory(menuInventory, node->mousefx, px, py);
		if (ic) {
			if (!rightClick) {
				/* found item to drag */
				mouseSpace = MS_DRAG;
				dragItem = ic->item;
				/* mousefx is the container (see hover code) */
				dragFrom = node->mousefx;
				dragFromX = ic->x;
				dragFromY = ic->y;
			} else {
				if (node->mousefx != csi.idEquip) {
					/* back to idEquip (ground, floor) container */
					INV_MoveItem(baseCurrent, menuInventory, csi.idEquip, -1, -1, node->mousefx, ic->x, ic->y);
				} else {
					qboolean packed = qfalse;

					assert(ic->item.t != NONE);
					/* armour can only have one target */
					if (!Q_strncmp(csi.ods[ic->item.t].type, "armour", MAX_VAR)) {
						packed = INV_MoveItem(baseCurrent, menuInventory, csi.idArmour, 0, 0, node->mousefx, ic->x, ic->y);
					/* ammo or item */
					} else if (!Q_strncmp(csi.ods[ic->item.t].type, "ammo", MAX_VAR)) {
						Com_FindSpace(menuInventory, &ic->item, csi.idBelt, &px, &py);
						packed = INV_MoveItem(baseCurrent, menuInventory, csi.idBelt, px, py, node->mousefx, ic->x, ic->y);
						if (!packed) {
							Com_FindSpace(menuInventory, &ic->item, csi.idHolster, &px, &py);
							packed = INV_MoveItem(baseCurrent, menuInventory, csi.idHolster, px, py, node->mousefx, ic->x, ic->y);
						}
						if (!packed) {
							Com_FindSpace(menuInventory, &ic->item, csi.idBackpack, &px, &py);
							packed = INV_MoveItem(baseCurrent, menuInventory, csi.idBackpack, px, py, node->mousefx, ic->x, ic->y);
						}
					} else {
						if (csi.ods[ic->item.t].headgear) {
							Com_FindSpace(menuInventory, &ic->item, csi.idHeadgear, &px, &py);
							packed = INV_MoveItem(baseCurrent, menuInventory, csi.idHeadgear, px, py, node->mousefx, ic->x, ic->y);
						} else {
							/* left and right are single containers, but this might change - it's cleaner to check
							 * for available space here, too */
							Com_FindSpace(menuInventory, &ic->item, csi.idRight, &px, &py);
							packed = INV_MoveItem(baseCurrent, menuInventory, csi.idRight, px, py, node->mousefx, ic->x, ic->y);
							if (!packed) {
								Com_FindSpace(menuInventory, &ic->item, csi.idLeft, &px, &py);
								packed = INV_MoveItem(baseCurrent, menuInventory, csi.idLeft, px, py, node->mousefx, ic->x, ic->y);
							}
							if (!packed) {
								Com_FindSpace(menuInventory, &ic->item, csi.idHolster, &px, &py);
								packed = INV_MoveItem(baseCurrent, menuInventory, csi.idHolster, px, py, node->mousefx, ic->x, ic->y);
							}
							if (!packed) {
								Com_FindSpace(menuInventory, &ic->item, csi.idBelt, &px, &py);
								packed = INV_MoveItem(baseCurrent, menuInventory, csi.idBelt, px, py, node->mousefx, ic->x, ic->y);
							}
							if (!packed) {
								Com_FindSpace(menuInventory, &ic->item, csi.idBackpack, &px, &py);
								packed = INV_MoveItem(baseCurrent, menuInventory, csi.idBackpack, px, py, node->mousefx, ic->x, ic->y);
							}
						}
					}
					/* no need to continue here - placement wasn't successful at all */
					if (!packed)
						return;
				}
			}
			UP_ItemDescription(ic->item.t);
/*			MN_DrawTooltip("f_verysmall", csi.ods[dragItem.t].name, px, py, 0);*/
		}
	} else {
		/* end drag */
		mouseSpace = MS_NULL;
		px = (int) ((x - node->pos[0] - C_UNIT * ((csi.ods[dragItem.t].sx - 1) / 2.0)) / C_UNIT);
		py = (int) ((y - node->pos[1] - C_UNIT * ((csi.ods[dragItem.t].sy - 1) / 2.0)) / C_UNIT);

		/* tactical mission */
		if (selActor) {
			MSG_Write_PA(PA_INVMOVE, selActor->entnum, dragFrom, dragFromX, dragFromY, node->mousefx, px, py);
			return;
		/* menu */
		}

		INV_MoveItem(baseCurrent, menuInventory, node->mousefx, px, py, dragFrom, dragFromX, dragFromY);
	}

	/* We are in the base or multiplayer inventory */
	if (sel < chrDisplayList.num) {
		assert(chrDisplayList.chr[sel]);
		if (chrDisplayList.chr[sel]->empl_type == EMPL_ROBOT)
			CL_UGVCvars(chrDisplayList.chr[sel]);
		else
			CL_CharacterCvars(chrDisplayList.chr[sel]);
	}
}

/**
 * @brief Handles checkboxes clicks
 */
static void MN_CheckboxClick (menuNode_t * node)
{
	int value;

	assert(node->data[MN_DATA_MODEL_SKIN_OR_CVAR]);
	/* no cvar? */
	if (Q_strncmp(node->data[MN_DATA_MODEL_SKIN_OR_CVAR], "*cvar", 5))
		return;

	value = Cvar_VariableInteger(&((char*)node->data[MN_DATA_MODEL_SKIN_OR_CVAR])[6]) ^ 1;
	Cvar_SetValue(&((char*)node->data[MN_DATA_MODEL_SKIN_OR_CVAR])[6], value);
}

/**
 * @brief Handles selectboxes clicks
 * @sa MN_SELECTBOX
 */
static void MN_SelectboxClick (menu_t * menu, menuNode_t * node, int y)
{
	selectBoxOptions_t* selectBoxOption;
	int clickedAtOption;

	clickedAtOption = (y - node->pos[1]);

	if (node->size[1])
		clickedAtOption = (clickedAtOption - node->size[1]) / node->size[1];
	else
		clickedAtOption = (clickedAtOption - SELECTBOX_DEFAULT_HEIGHT) / SELECTBOX_DEFAULT_HEIGHT; /* default height - see selectbox.tga */

	if (clickedAtOption < 0 || clickedAtOption >= node->height)
		return;

	/* the cvar string is stored in data[MN_DATA_MODEL_SKIN_OR_CVAR] */
	/* no cvar given? */
	if (!node->data[MN_DATA_MODEL_SKIN_OR_CVAR] || !*(char*)node->data[MN_DATA_MODEL_SKIN_OR_CVAR]) {
		Com_Printf("MN_SelectboxClick: node '%s' doesn't have a valid cvar assigned (menu %s)\n", node->name, node->menu->name);
		return;
	}

	/* no cvar? */
	if (Q_strncmp((char*)node->data[MN_DATA_MODEL_SKIN_OR_CVAR], "*cvar", 5))
		return;

	/* only execute the click stuff if the selectbox is active */
	if (node->state) {
		selectBoxOption = node->options;
		for (; clickedAtOption > 0 && selectBoxOption; selectBoxOption = selectBoxOption->next) {
			clickedAtOption--;
		}
		if (selectBoxOption) {
			/* strip '*cvar ' from data[0] - length is already checked above */
			Cvar_Set(&((char*)node->data[MN_DATA_MODEL_SKIN_OR_CVAR])[6], selectBoxOption->value);
			if (*selectBoxOption->action) {
#ifdef DEBUG
				if (selectBoxOption->action[strlen(selectBoxOption->action)-1] != ';')
					Com_Printf("selectbox option with none terminated action command");
#endif
				Cbuf_AddText(selectBoxOption->action);
			}
		}
	}
}

/**
 * @brief Handles the bar cvar values
 * @sa Key_Message
 */
static void MN_BarClick (menu_t * menu, menuNode_t * node, int x)
{
	char var[MAX_VAR];
	float frac, min;

	if (!node->mousefx)
		return;

	Q_strncpyz(var, node->data[2], sizeof(var));
	/* no cvar? */
	if (Q_strncmp(var, "*cvar", 5))
		return;

	/* normalize it */
	frac = (float) (x - node->pos[0]) / node->size[0];
	/* in the case of MN_BAR the first three data array values are float values - see menuDataValues_t */
	min = MN_GetReferenceFloat(menu, node->data[1]);
	Cvar_SetValue(&var[6], min + frac * (MN_GetReferenceFloat(menu, node->data[0]) - min));
}

/**
 * @brief Left click on the basemap
 */
static void MN_BaseMapClick (menuNode_t * node, int x, int y)
{
	int row, col;
	building_t	*entry;

	assert(baseCurrent);

	if (baseCurrent->buildingCurrent && baseCurrent->buildingCurrent->buildingStatus == B_STATUS_NOT_SET) {
		for (row = 0; row < BASE_SIZE; row++)
			for (col = 0; col < BASE_SIZE; col++)
				if (baseCurrent->map[row][col] == BASE_FREESLOT && x >= baseCurrent->posX[row][col]
					&& x < baseCurrent->posX[row][col] + node->size[0] / BASE_SIZE && y >= baseCurrent->posY[row][col]
					&& y < baseCurrent->posY[row][col] + node->size[1] / BASE_SIZE) {
					/* Set position for a new building */
					B_SetBuildingByClick(row, col);
					return;
				}
	}

	for (row = 0; row < BASE_SIZE; row++)
		for (col = 0; col < BASE_SIZE; col++)
			if (baseCurrent->map[row][col] > BASE_FREESLOT && x >= baseCurrent->posX[row][col]
				&& x < baseCurrent->posX[row][col] + node->size[0] / BASE_SIZE && y >= baseCurrent->posY[row][col]
				&& y < baseCurrent->posY[row][col] + node->size[1] / BASE_SIZE) {
				entry = B_GetBuildingByIdx(baseCurrent, baseCurrent->map[row][col]);
				if (!entry)
					Sys_Error("MN_BaseMapClick: no entry at %i:%i\n", x, y);

				if (*entry->onClick) {
					baseCurrent->buildingCurrent = entry;
					Cmd_ExecuteString(va("%s %i", entry->onClick, baseCurrent->idx));
					baseCurrent->buildingCurrent = NULL;
					gd.baseAction = BA_NONE;
				}
#if 0
				else {
					/* Click on building : display its properties in the building menu */
					MN_PushMenu("buildings");
					baseCurrent->buildingCurrent = entry;
					B_BuildingStatus(baseCurrent, baseCurrent->buildingCurrent);
				}
#else
				else
					UP_OpenWith(entry->pedia);
#endif
				return;
			}
}

/**
 * @brief Right click on the basemap
 */
static void MN_BaseMapRightClick (menuNode_t * node, int x, int y)
{
	int row, col;
	building_t	*entry;

	assert(baseCurrent);

	for (row = 0; row < BASE_SIZE; row++)
		for (col = 0; col < BASE_SIZE; col++)
			if (baseCurrent->map[row][col] > BASE_FREESLOT && x >= baseCurrent->posX[row][col]
				&& x < baseCurrent->posX[row][col] + node->size[0] / BASE_SIZE && y >= baseCurrent->posY[row][col]
				&& y < baseCurrent->posY[row][col] + node->size[1] / BASE_SIZE) {
				entry = B_GetBuildingByIdx(baseCurrent, baseCurrent->map[row][col]);
				if (!entry)
					Sys_Error("MN_BaseMapClick: no entry at %i:%i\n", x, y);
				B_MarkBuildingDestroy(baseCurrent, entry);
				return;
			}
}

/**
 * @brief Activates the model rotation
 * @note set the mouse space to MS_ROTATE
 * @sa rotateAngles
 */
static void MN_ModelClick (menuNode_t * node)
{
	mouseSpace = MS_ROTATE;
	/* modify node->angles (vec3_t) if you rotate the model */
	rotateAngles = node->angles;
}


/**
 * @brief Calls the script command for a text node that is clickable
 * @note The node must have the click parameter
 * @sa MN_TextRightClick
 */
static void MN_TextClick (menuNode_t * node, int mouseOver)
{
	char cmd[MAX_VAR];
	Com_sprintf(cmd, sizeof(cmd), "%s_click", node->name);
	if (Cmd_Exists(cmd))
		Cbuf_AddText(va("%s %i\n", cmd, mouseOver - 1));
}

/**
 * @brief Calls the script command for a text node that is clickable via right mouse button
 * @note The node must have the rclick parameter
 * @sa MN_TextClick
 */
static void MN_TextRightClick (menuNode_t * node, int mouseOver)
{
	char cmd[MAX_VAR];
	Com_sprintf(cmd, sizeof(cmd), "%s_rclick", node->name);
	if (Cmd_Exists(cmd))
		Cbuf_AddText(va("%s %i\n", cmd, mouseOver - 1));
}

/**
 * @brief Is called everytime one clickes on a menu/screen. Then checks if anything needs to be executed in the earea of the click (e.g. button-commands, inventory-handling, geoscape-stuff, etc...)
 * @sa MN_ModelClick
 * @sa MN_TextRightClick
 * @sa MN_TextClick
 * @sa MN_Drag
 * @sa MN_BarClick
 * @sa MN_CheckboxClick
 * @sa MN_BaseMapClick
 * @sa MAP_3DMapClick
 * @sa MAP_MapClick
 * @sa MN_ExecuteActions
 * @sa MN_RightClick
 * @sa Key_Message
 * @sa CL_MessageMenu_f
 * @note inline editing of cvars (e.g. save dialog) is done in Key_Message
 */
void MN_Click (int x, int y)
{
	menuNode_t *node, *execute_node = NULL;
	menu_t *menu;
	int sp, mouseOver;
	qboolean clickedInside = qfalse;

	sp = menuStackPos;

	while (sp > 0) {
		menu = menuStack[--sp];
		execute_node = NULL;
		for (node = menu->firstNode; node; node = node->next) {
			if (node->type != MN_CONTAINER && node->type != MN_CHECKBOX
			 && node->type != MN_SELECTBOX && !node->click)
				continue;

			/* check whether mouse is over this node */
			mouseOver = MN_CheckNodeZone(node, x, y);

			if (!mouseOver)
				continue;

			/* check whether we clicked at least on one menu node */
			clickedInside = qtrue;

			/* found a node -> do actions */
			switch (node->type) {
			case MN_CONTAINER:
				MN_Drag(node, x, y, qfalse);
				break;
			case MN_BAR:
				MN_BarClick(menu, node, x);
				break;
			case MN_BASEMAP:
				MN_BaseMapClick(node, x, y);
				break;
			case MN_MAP:
				MAP_MapClick(node, x, y);
				break;
			case MN_CHECKBOX:
				MN_CheckboxClick(node);
				break;
			case MN_SELECTBOX:
				MN_SelectboxClick(menu, node, y);
				break;
			case MN_MODEL:
				MN_ModelClick(node);
				break;
			case MN_TEXT:
				MN_TextClick(node, mouseOver);
				break;
			default:
				/* Save the action for later execution. */
				if (node->click && (node->click->type != EA_NULL))
					execute_node = node;
				break;
			}
		}

		/* Only execute the last-found (i.e. from the top-most displayed node) action found.
		 * Especially needed for button-nodes that are (partially) overlayed and all have actions defined.
		 * e.g. the firemode checkboxes.
		 */
		if (execute_node) {
			MN_ExecuteActions(menu, execute_node->click);
			if (execute_node->repeat) {
				mouseSpace = MS_LHOLD;
				mouseRepeat.menu = menu;
				mouseRepeat.action = execute_node->click;
				mouseRepeat.nexttime = cls.realtime + 500;	/* second "event" after 0.5 sec */
			}
		}

		/* @todo: maybe we should also check sp == menuStackPos here */
		if (!clickedInside && menu->leaveNode)
			MN_ExecuteActions(menu, menu->leaveNode->click);

		/* don't care about non-rendered windows */
		if (menu->renderNode || menu->popupNode)
			return;
	}
}

/**
 * @brief Calls script function on cvar change
 *
 * This is for inline editing of cvar values
 * The cvarname_changed function are called,
 * the editing is activated and ended here
 *
 * Done by the script command msgmenu [?|!|:][cvarname]
 * @sa Key_Message
 */
static void CL_MessageMenu_f (void)
{
	static char nameBackup[MAX_CVAR_EDITING_LENGTH];
	static char cvarName[MAX_VAR];
	const char *msg;

	if (Cmd_Argc() < 2) {
		Com_Printf("Usage: %s <msg>\n", Cmd_Argv(0));
		return;
	}

	msg = Cmd_Argv(1);
	switch (msg[0]) {
	case '?':
		/* start */
		Cbuf_AddText("messagemenu\n");
		Q_strncpyz(cvarName, msg + 1, sizeof(cvarName));
		Q_strncpyz(nameBackup, Cvar_VariableString(cvarName), mn_inputlength->integer);
		Q_strncpyz(msg_buffer, nameBackup, sizeof(msg_buffer));
		msg_bufferlen = strlen(nameBackup);
		break;
	case '\'':
		if (!*cvarName)
			break;
		/* abort without doing anything */
		nameBackup[0] = cvarName[0] = '\0';
		break;
	case '!':
		if (!*cvarName)
			break;
		/* cancel */
		Cvar_ForceSet(cvarName, nameBackup);
		/* hack for actor renaming */
		/* FIXME: this will produce a lot of cvars */
		Cvar_ForceSet(va("%s%i", cvarName, cl_selected->integer), nameBackup);
		/* call trigger function */
		Cbuf_AddText(va("%s_changed\n", cvarName));
		/* don't restore this the next time */
		nameBackup[0] = cvarName[0] = '\0';
		break;
	case ':':
		if (!*cvarName)
			break;
		/* end */
		Cvar_ForceSet(cvarName, msg + 1);
		/* hack for employee name */
		/* FIXME: this will produce a lot of cvars */
		Cvar_ForceSet(va("%s%i", cvarName, cl_selected->integer), msg + 1);
		/* call trigger function */
		Cbuf_AddText(va("%s_changed\n", cvarName));
		nameBackup[0] = cvarName[0] = '\0';
		break;
	default:
		if (!*cvarName)
			break;
		/* continue */
		Cvar_ForceSet(cvarName, msg);
		Cvar_ForceSet(va("%s%i", cvarName, cl_selected->integer), msg);
		break;
	}
}

/**
 * @brief Scrolls the text in a textbox up/down.
 * @param[in] node The node of the text to be scrolled.
 * @param[in] offset Number of lines to scroll. Positive values scroll down, negative up.
 * @return Returns qtrue if scrolling was possible otherwise qfalse.
 */
static qboolean MN_TextScroll (menuNode_t *node, int offset)
{
	int textScroll_new;

	if (!node || !node->height)
		return qfalse;

	if (abs(offset) >= node->height) {
		/* Offset value is bigger than textbox height. */
		return qfalse;
	}

	if (node->textLines <= node->height) {
		/* Number of lines are less than the height of the textbox. */
		node->textScroll = 0;
		return qfalse;
	}

	textScroll_new = node->textScroll + offset;

	if (textScroll_new <= 0) {
		/* Goto top line, no matter how big the offset was. */
		node->textScroll = 0;
		return qtrue;

	} else if (textScroll_new >= (node->textLines + 1 - node->height)) {
		/* Goto last possible line, no matter how big the offset was. */
		node->textScroll = node->textLines - node->height;
		return qtrue;

	} else {
		node->textScroll = textScroll_new;
		return qtrue;
	}
}

/**
 * @brief Scriptfunction that gets the wanted text node and scrolls the text.
 */
static void MN_TextScroll_f (void)
{
	int offset = 0;
	menuNode_t *node = NULL;

	if (Cmd_Argc() < 3) {
		Com_Printf("Usage: %s <nodename> <+/-offset>\n", Cmd_Argv(0));
		return;
	}

	node = MN_GetNodeFromCurrentMenu(Cmd_Argv(1));

	if (!node) {
		Com_DPrintf(DEBUG_CLIENT, "MN_TextScroll_f: Node '%s' not found.\n", Cmd_Argv(1));
		return;
	}

	if (!Q_strncmp(Cmd_Argv(2), "reset", 5)) {
		node->textScroll = 0;
		return;
	}

	offset = atoi(Cmd_Argv(2));

	if (offset == 0)
		return;

	MN_TextScroll(node, offset);
}

/**
 * @brief Scroll to the bottom
 */
void MN_TextScrollBottom (const char* nodeName)
{
	menuNode_t *node = NULL;

	node = MN_GetNodeFromCurrentMenu(nodeName);

	if (!node) {
		Com_DPrintf(DEBUG_CLIENT, "Node '%s' could not be found\n", nodeName);
		return;
	}

	if (node->textLines > node->height) {
		Com_DPrintf(DEBUG_CLIENT, "\nMN_TextScrollBottom: Scrolling to line %i\n", node->textLines - node->height + 1);
		node->textScroll = node->textLines - node->height + 1;
	}
}

/**
 * @sa MAP_ResetAction
 * @sa MN_TextRightClick
 * @sa MN_ExecuteActions
 * @sa MN_Click
 * @sa MN_MiddleClick
 * @sa MN_MouseWheel
 */
void MN_RightClick (int x, int y)
{
	menuNode_t *node;
	menu_t *menu;
	int sp, mouseOver;

	sp = menuStackPos;

	while (sp > 0) {
		menu = menuStack[--sp];
		for (node = menu->firstNode; node; node = node->next) {
			/* no right click for this node defined */
			if (node->type != MN_CONTAINER && !node->rclick)
				continue;

			/* check whether mouse if over this node */
			mouseOver = MN_CheckNodeZone(node, x, y);
			if (!mouseOver)
				continue;

			/* found a node -> do actions */
			switch (node->type) {
			case MN_CONTAINER:
				MN_Drag(node, x, y, qtrue);
				break;
			case MN_BASEMAP:
				MN_BaseMapRightClick(node, x, y);
				break;
			case MN_MAP:
				MAP_ResetAction();
				if (!cl_3dmap->integer)
					mouseSpace = MS_SHIFTMAP;
				else
					mouseSpace = MS_SHIFT3DMAP;
				break;
			case MN_TEXT:
				MN_TextRightClick(node, mouseOver);
				break;
			default:
				MN_ExecuteActions(menu, node->rclick);
				break;
			}
		}

		if (menu->renderNode || menu->popupNode)
			/* don't care about non-rendered windows */
			return;
	}
}

/**
 * @sa MN_Click
 * @sa MN_RightClick
 * @sa MN_MouseWheel
 */
void MN_MiddleClick (int x, int y)
{
	menuNode_t *node;
	menu_t *menu;
	int sp, mouseOver;

	sp = menuStackPos;

	while (sp > 0) {
		menu = menuStack[--sp];
		for (node = menu->firstNode; node; node = node->next) {
			/* no middle click for this node defined */
			if (!node->mclick)
				continue;

			/* check whether mouse if over this node */
			mouseOver = MN_CheckNodeZone(node, x, y);
			if (!mouseOver)
				continue;

			/* found a node -> do actions */
			switch (node->type) {
			case MN_MAP:
				mouseSpace = MS_ZOOMMAP;
				break;
			default:
				MN_ExecuteActions(menu, node->mclick);
				break;
			}
		}

		if (menu->renderNode || menu->popupNode)
			/* don't care about non-rendered windows */
			return;
	}
}

/**
 * @brief Called when we are in menu mode and scroll via mousewheel
 * @note The geoscape zooming code is here, too (also in CL_ParseInput)
 * @sa MN_Click
 * @sa MN_RightClick
 * @sa MN_MiddleClick
 * @sa CL_ZoomInQuant
 * @sa CL_ZoomOutQuant
 * @sa MN_Click
 * @sa MN_RightClick
 * @sa MN_MiddleClick
 * @sa CL_ZoomInQuant
 * @sa CL_ZoomOutQuant
 */
void MN_MouseWheel (qboolean down, int x, int y)
{
	menuNode_t *node;
	menu_t *menu;
	int sp, mouseOver;

	sp = menuStackPos;

	while (sp > 0) {
		menu = menuStack[--sp];
		for (node = menu->firstNode; node; node = node->next) {
			/* no middle click for this node defined */
			/* both wheelUp & wheelDown required */
			if (!node->wheel && !(node->wheelUp && node->wheelDown))
				continue;

			/* check whether mouse if over this node */
			mouseOver = MN_CheckNodeZone(node, x, y);
			if (!mouseOver)
				continue;

			/* found a node -> do actions */
			switch (node->type) {
			case MN_MAP:
				ccs.zoom *= pow(0.995, (down ? 10: -10));
				if (ccs.zoom < cl_mapzoommin->value)
					ccs.zoom = cl_mapzoommin->value;
				else if (ccs.zoom > cl_mapzoommax->value)
					ccs.zoom = cl_mapzoommax->value;

				if (!cl_3dmap->integer) {
					if (ccs.center[1] < 0.5 / ccs.zoom)
						ccs.center[1] = 0.5 / ccs.zoom;
					if (ccs.center[1] > 1.0 - 0.5 / ccs.zoom)
						ccs.center[1] = 1.0 - 0.5 / ccs.zoom;
				}
				break;
			case MN_TEXT:
				if (node->wheelUp && node->wheelDown) {
					MN_ExecuteActions(menu, (down ? node->wheelDown : node->wheelUp));
				} else {
					MN_TextScroll(node, (down ? 1 : -1));
					/* they can also have script commands assigned */
					MN_ExecuteActions(menu, node->wheel);
				}
				break;
			default:
				if (node->wheelUp && node->wheelDown)
					MN_ExecuteActions(menu, (down ? node->wheelDown : node->wheelUp));
				else
					MN_ExecuteActions(menu, node->wheel);
				break;
			}
		}

		if (menu->renderNode || menu->popupNode)
			/* don't care about non-rendered windows */
			return;
	}
}


/**
 * @brief Determine the position and size of render
 * @param[in] menu : use its position and size properties
 */
void MN_SetViewRect (const menu_t* menu)
{
	menuNode_t* menuNode = menu ? (menu->renderNode ? menu->renderNode : (menu->popupNode ? menu->popupNode : NULL)): NULL;

	if (!menuNode) {
		/* render the full screen */
		scr_vrect.x = scr_vrect.y = 0;
		scr_vrect.width = viddef.width;
		scr_vrect.height = viddef.height;
	} else if (menuNode->invis) {
		/* don't draw the scene */
		memset(&scr_vrect, 0, sizeof(scr_vrect));
	} else {
		/* the menu has a view size specified */
		scr_vrect.x = menuNode->pos[0] * viddef.rx;
		scr_vrect.y = menuNode->pos[1] * viddef.ry;
		scr_vrect.width = menuNode->size[0] * viddef.rx;
		scr_vrect.height = menuNode->size[1] * viddef.ry;
	}
}


/*
==============================================================
MENU DRAWING
==============================================================
*/

/**
 * @brief Draws an item to the screen
 *
 * @param[in] org Node position on the screen (pixel)
 * @param[in] item The item to draw
 * @param[in] sx Size in x direction (no pixel but container space)
 * @param[in] sy Size in y direction (no pixel but container space)
 * @param[in] x Position in container
 * @param[in] y Position in container
 * @param[in] scale
 * @param[in] color
 * @sa SCR_DrawCursor
 * Used to draw an item to the equipment containers. First look whether the objDef_t
 * includes an image - if there is none then draw the model
 */
void MN_DrawItem (vec3_t org, item_t item, int sx, int sy, int x, int y, vec3_t scale, vec4_t color)
{
	modelInfo_t mi;
	objDef_t *od;
	vec3_t angles = { -10, 160, 70 };
	vec3_t origin;
	vec3_t size;
	vec4_t col;

	assert(item.t != NONE);
	od = &csi.ods[item.t];

	if (od->image[0]) {
		/* draw the image */
		R_DrawNormPic(
			org[0] + C_UNIT / 2.0 * sx + C_UNIT * x,
			org[1] + C_UNIT / 2.0 * sy + C_UNIT * y,
			C_UNIT * sx, C_UNIT * sy, 0, 0, 0, 0, ALIGN_CC, qtrue, od->image);
	} else if (od->model[0]) {
		if (item.rotated) {
			angles[0] = angles[0] - 90;
		}
		/* draw model, if there is no image */
		mi.name = od->model;
		mi.origin = origin;
		mi.angles = angles;
		mi.center = od->center;
		mi.scale = size;

		if (od->scale) {
			VectorScale(scale, od->scale, size);
		} else {
			VectorCopy(scale, size);
		}

		VectorCopy(org, origin);
		if (x || y || sx || sy) {
			origin[0] += C_UNIT / 2.0 * sx;
			origin[1] += C_UNIT / 2.0 * sy;
			origin[0] += C_UNIT * x;
			origin[1] += C_UNIT * y;
		}

		mi.frame = 0;
		mi.oldframe = 0;
		mi.backlerp = 0;
		mi.skin = 0;

		Vector4Copy(color, col);
		/* no ammo in this weapon - hightlight this item */
		if (od->weapon && od->reload && !item.a) {
			col[1] *= 0.5;
			col[2] *= 0.5;
		}

		mi.color = col;

		/* draw the model */
		R_DrawModelDirect(&mi, NULL, NULL);
	}
}



/**
 * @brief Generic tooltip function
 */
static int MN_DrawTooltip (const char *font, char *string, int x, int y, int maxWidth, int maxHeight)
{
	int height = 0, width = 0;
	int lines = 5;

	R_FontLength(font, string, &width, &height);
	if (!width)
		return 0;

	/* maybe there is no maxWidth given */
	if (maxWidth < width)
		maxWidth = width;

	x += 5;
	y += 5;
	if (x + maxWidth +3 > VID_NORM_WIDTH)
		x -= (maxWidth + 10);
	R_DrawFill(x - 1, y - 1, maxWidth + 4, height, 0, tooltipBG);
	R_ColorBlend(tooltipColor);
	R_FontDrawString(font, 0, x + 1, y + 1, x + 1, y + 1, maxWidth, maxHeight, height, string, lines, 0, NULL, qfalse);
	R_ColorBlend(NULL);
	return width;
}

/**
 * @brief Wrapper for menu tooltips
 */
static void MN_Tooltip (menu_t *menu, menuNode_t *node, int x, int y)
{
	char *tooltip;
	int width = 0;

	/* tooltips
	 * data[MN_DATA_NODE_TOOLTIP] is a char pointer to the tooltip text
	 * see value_t nps for more info */

	/* maybe not tooltip but a key entity? */
	if (node->data[MN_DATA_NODE_TOOLTIP]) {
		tooltip = (char *) node->data[MN_DATA_NODE_TOOLTIP];
		assert(tooltip);
		if (*tooltip == '_')
			tooltip++;

		width = MN_DrawTooltip("f_small", _(MN_GetReferenceString(menu, node->data[MN_DATA_NODE_TOOLTIP])), x, y, width, 0);
		y += 20;
	}
	if (node->key[0]) {
		if (node->key[0] == '*')
			Com_sprintf(node->key, sizeof(node->key), _("Key: %s"), MN_GetReferenceString(menu, node->key));
		MN_DrawTooltip("f_verysmall", node->key, x, y, width,0);
	}
}

/**
 * @brief Returns pointer to menu model
 * @param[in] menuModel menu model id from script files
 * @return menuModel_t pointer
 */
static menuModel_t *MN_GetMenuModel (const char *menuModel)
{
	int i;
	menuModel_t *m;

	for (i = 0; i < numMenuModels; i++) {
		m = &menuModels[i];
		if (!Q_strncmp(m->id, menuModel, MAX_VAR))
			return m;
	}
	return NULL;
}

/**
 * @brief Return the font for a specific node or default font
 * @param[in] m The current menu pointer - if NULL we will use the current menuStack
 * @param[in] n The node to get the font for - if NULL f_small is returned
 * @return char pointer with font name (default is f_small)
 */
const char *MN_GetFont (const menu_t *m, const menuNode_t *const n)
{
	if (!n || n->data[MN_DATA_ANIM_OR_FONT]) {
		if (!m)
			m = MN_GetCurrentMenu();

		return MN_GetReferenceString(m, n->data[MN_DATA_ANIM_OR_FONT]);
	}
	return "f_small";
}

/**
 * @brief Generate tooltip text for an item.
 * @param[in] item The item we want to generate the tooltip text for.
 * @param[in|out] tooltiptext Pointer to a string the information should be written into.
 * @param[in] Max. string size of tooltiptext.
 * @return Number of lines
 * @todo return maximum line-width as well?
 */
static int INV_GetItemTooltip (item_t item, char *tooltiptext, size_t string_maxlength)
{
	int i;
	int weapon_idx;
	int linenum = 0;
	if (item.amount > 1)
		Q_strncpyz(tooltiptext, va("%i x %s\n", item.amount, csi.ods[item.t].name), string_maxlength);
	else
		Q_strncpyz(tooltiptext, va("%s\n", csi.ods[item.t].name), string_maxlength);
	linenum++;
	/* Only display further info if item.t is researched */
	if (RS_ItemIsResearched(csi.ods[item.t].id)) {
		if (csi.ods[item.t].weapon) {
			/* Get info about used ammo (if there is any) */
			if (item.t == item.m) {
				/* Item has no ammo but might have shot-count */
				if (item.a) {
					Q_strcat(tooltiptext, va(_("Ammo: %i\n"), item.a), string_maxlength);
					linenum++;
				}
			} else if (item.m != NONE) {
				/* Search for used ammo and display name + ammo count */
				Q_strcat(tooltiptext, va(_("%s loaded\n"), csi.ods[item.m].name), string_maxlength);
				linenum++;
				Q_strcat(tooltiptext, va(_("Ammo: %i\n"),  item.a), string_maxlength);
				linenum++;
			}
		} else if (csi.ods[item.t].numWeapons) {
			/* Check if this is a non-weapon and non-ammo item */
			if (!(csi.ods[item.t].numWeapons == 1 && csi.ods[item.t].weap_idx[0] == item.t)) {
				/* If it's ammo get the weapon names it can be used in */
				Q_strcat(tooltiptext, _("Usable in:\n"), string_maxlength);
				linenum++;
				for (i = 0; i < csi.ods[item.t].numWeapons; i++) {
					weapon_idx = csi.ods[item.t].weap_idx[i];
					if (RS_ItemIsResearched(csi.ods[weapon_idx].id)) {
						Q_strcat(tooltiptext, va("* %s\n", csi.ods[weapon_idx].name), string_maxlength);
						linenum++;
					}
				}
			}
		}
	}
	return linenum;
}


#define SCROLLBAR_WIDTH 10
/**
 * @brief Handles line breaks and drawing for MN_TEXT menu nodes
 * @sa MN_DrawMenus
 * @param[in] text Text to draw
 * @param[in] font Font string to use
 * @param[in] node The current menu node
 * @param[in] x The fixed x position every new line starts
 * @param[in] y The fixed y position the text node starts
 * @todo The node pointer can be NULL
 */
static void MN_DrawTextNode (const char *text, const linkedList_t* list, const char* font, menuNode_t* node, int x, int y, int width, int height)
{
	char textCopy[MAX_MENUTEXTLEN];
	int lineHeight = 0;
	char newFont[MAX_VAR];
	const char* oldFont = NULL;
	vec4_t color;
	char *cur, *tab, *end;
	int x1, y1; /* variable x and y position */
	static const vec4_t scrollbarColorBG = {0.03, 0.41, 0.05, 0.5};
	static const vec4_t scrollbarColorBar = {0.03, 0.41, 0.05, 1.0};

	if (text) {
		Q_strncpyz(textCopy, text, sizeof(textCopy));
	} else if (list) {
		Q_strncpyz(textCopy, (char*)list->data, sizeof(textCopy));
	} else
		Sys_Error("MN_DrawTextNode: Called without text or linkedList pointer");
	cur = textCopy;

	/* hover darkening effect for text lines */
	VectorScale(node->color, 0.8, color);
	color[3] = node->color[3];

	if (node->scrollbar) {
		width -= SCROLLBAR_WIDTH; /* scrollbar space */
		if (node->scrollbarLeft)
			x += SCROLLBAR_WIDTH;
	}

	y1 = y;
	/*Com_Printf("\n\n\nnode->textLines: %i \n", node->textLines);*/
	if (node)
		node->textLines = 0; /* these are lines only in one-line texts! */
	/* but it's easy to fix, just change FontDrawString
	 * so that it returns a pair, #lines and height
	 * and add that to variable line; the only other file
	 * using FontDrawString result is client/cl_sequence.c
	 * and there just ignore #lines */
	do {
		/* new line starts from node x position */
		x1 = x;
		if (oldFont) {
			font = oldFont;
			oldFont = NULL;
		}

		if (cur[0] == '^') {
			switch (toupper(cur[1])) {
			case 'B':
				Com_sprintf(newFont, sizeof(newFont), "%s_bold", font);
				oldFont = font;
				font = newFont;
				cur += 2; /* don't print the format string */
				break;
			}
		}

		/* get the position of the next newline - otherwise end will be null */
		end = strchr(cur, '\n');
		if (end)
			/* set the \n to \0 to draw only this part (before the \n) with our font renderer */
			/* let end point to the next char after the \n (or \0 now) */
			*end++ = '\0';

		/* hightlight if mousefx is true */
		/* node->state is the line number to highlight */
		/* FIXME: what about multiline text that should be highlighted completly? */
		if (node && node->mousefx && node->textLines + 1 == node->state)
			R_ColorBlend(color);

		/* we assume all the tabs fit on a single line */
		do {
			tab = strchr(cur, '\t');
			/* if this string does not contain any tabstops break this do while ... */
			if (!tab)
				break;
			/* ... otherwise set the \t to \0 and increase the tab pointer to the next char */
			/* after the tab (needed for the next loop in this (the inner) do while) */
			*tab++ = '\0';
			/* now check whether we should draw this string */
			/*Com_Printf("tab - first part - node->textLines: %i \n", node->textLines);*/
			if (node)
				node->textLines++;
			R_FontDrawString(font, node->align, x1, y1, x, y, width, height, node->texh[0], cur, node->height, node->textScroll, &node->textLines, qfalse);
			if (node)
				node->textLines--;
			/* increase the x value as given via menu definition format string */
			/* or use 1/3 of the node size (width) */
			if (!node || !node->texh[1])
				x1 += (width / 3);
			else
				x1 += node->texh[1];
			/* now set cur to the first char after the \t */
			cur = tab;
		} while (1);

		/*Com_Printf("until newline - node->textLines: %i\n", node->textLines);*/
		/* the conditional expression at the end is a hack to draw "/n/n" as a blank line */
		lineHeight = R_FontDrawString(font, node->align, x1, y1, x, y, width, height, node->texh[0], (*cur ? cur : " "), node->height, node->textScroll, &node->textLines, qtrue);
		if (lineHeight > 0)
			y1 += lineHeight;

		if (node && node->mousefx)
			R_ColorBlend(node->color); /* restore original color */

		/* now set cur to the next char after the \n (see above) */
		cur = end;
		if (!cur && list) {
			list = list->next;
			if (list) {
				Q_strncpyz(textCopy, (char*)list->data, sizeof(textCopy));
				cur = textCopy;
			}
		}
	} while (cur);

	/* draw scrollbars */
	if (node->scrollbar && node->height  && node->textLines > node->height) {
		int scrollBarX, nodePixelHeight;
		float scrollBarHeight, scrollBarY;

		if (!node->texh[0])
			Sys_Error("MN_DrawTextNode: no format height for node '%s'\n", node->name);

		nodePixelHeight = node->height * node->texh[0];
		if (!node->scrollbarLeft)
			scrollBarX = node->pos[0] + node->size[0] - SCROLLBAR_WIDTH;
		else
			scrollBarX = node->pos[0];
		scrollBarY = node->pos[1];

		/* draw background of scrollbar */
		R_DrawFill(scrollBarX, scrollBarY, SCROLLBAR_WIDTH, nodePixelHeight, 0, scrollbarColorBG);

		/* draw scroll bar */
		scrollBarHeight = nodePixelHeight * (nodePixelHeight / (node->textLines * node->texh[0]));
		scrollBarY += node->textScroll * (scrollBarHeight / node->height);
		R_DrawFill(scrollBarX, scrollBarY, SCROLLBAR_WIDTH, scrollBarHeight, 0, scrollbarColorBar);
	}
}

/**
 * @brief Draws the menu stack
 * @sa SCR_UpdateScreen
 */
void MN_DrawMenus (void)
{
	modelInfo_t mi;
	modelInfo_t pmi;
	menuNode_t *node;
	menu_t *menu;
	animState_t *as;
	const char *ref;
	const char *font;
	char *anim;					/* model anim state */
	char source[MAX_VAR] = "";
	int sp, pp;
	item_t item = {1, NONE, NONE, 0, 0}; /* 1 so it's not reddish; fake item anyway */
	vec4_t color;
	int mouseOver = 0;
	int y, i;
	message_t *message;
	menuModel_t *menuModel = NULL, *menuModelParent = NULL;
	int width, height;
	invList_t *itemHover = NULL;
	char *tab, *end;
	static menuNode_t *selectBoxNode = NULL;

	/* render every menu on top of a menu with a render node */
	pp = 0;
	sp = menuStackPos;
	while (sp > 0) {
		if (menuStack[--sp]->renderNode)
			break;
		if (menuStack[sp]->popupNode)
			pp = sp;
	}
	if (pp < sp)
		pp = sp;

	while (sp < menuStackPos) {
		menu = menuStack[sp++];
		/* event node */
		if (menu->eventNode) {
			if (menu->eventNode->timeOut > 0 && (menu->eventNode->timeOut == 1 || (!menu->eventTime || (menu->eventTime + menu->eventNode->timeOut < cls.realtime)))) {
				menu->eventTime = cls.realtime;
				MN_ExecuteActions(menu, menu->eventNode->click);
#ifdef DEBUG
				Com_DPrintf(DEBUG_CLIENT, "Event node '%s' '%i\n", menu->eventNode->name, menu->eventNode->timeOut);
#endif
			}
		}
		for (node = menu->firstNode; node; node = node->next) {
			if (!node->invis && ((node->data[MN_DATA_STRING_OR_IMAGE_OR_MODEL] /* 0 are images, models and strings e.g. */
					|| node->type == MN_CONTAINER || node->type == MN_TEXT || node->type == MN_BASEMAP || node->type == MN_MAP)
					|| node->type == MN_CHECKBOX || node->type == MN_SELECTBOX || node->type == MN_LINESTRIP
					|| ((node->type == MN_ZONE || node->type == MN_CONTAINER) && node->bgcolor[3]))) {
				/* if construct */
				if (!MN_CheckCondition(node))
					continue;

				/* timeout? */
				if (node->timePushed) {
					if (node->timePushed + node->timeOut < cls.realtime) {
						node->timePushed = 0;
						node->invis = qtrue;
						/* only timeout this once, otherwise there is a new timeout after every new stack push */
						if (node->timeOutOnce)
							node->timeOut = 0;
						Com_DPrintf(DEBUG_CLIENT, "MN_DrawMenus: timeout for node '%s'\n", node->name);
						continue;
					}
				}

				/* mouse effects */
				/* don't allow to open more than one selectbox */
				if (selectBoxNode && selectBoxNode != node)
					node->state = qfalse;
				else if (sp > pp) {
					/* in and out events */
					mouseOver = MN_CheckNodeZone(node, mousePosX, mousePosY);
					if (mouseOver != node->state) {
						/* maybe we are leaving to another menu */
						menu->hoverNode = NULL;
						if (mouseOver)
							MN_ExecuteActions(menu, node->mouseIn);
						else
							MN_ExecuteActions(menu, node->mouseOut);
						node->state = mouseOver;
						selectBoxNode = NULL;
					}
				}

				/* check node size x and y value to check whether they are zero */
				if (node->bgcolor && node->size[0] && node->size[1] && node->pos) {
					R_DrawFill(node->pos[0] - node->padding, node->pos[1] - node->padding, node->size[0] + (node->padding*2), node->size[1] + (node->padding*2), 0, node->bgcolor);
				}

				if (node->border && node->bordercolor && node->size[0] && node->size[1] && node->pos) {
					/* left */
					R_DrawFill(node->pos[0] - node->padding - node->border, node->pos[1] - node->padding - node->border,
						node->border, node->size[1] + (node->padding*2) + (node->border*2), 0, node->bordercolor);
					/* right */
					R_DrawFill(node->pos[0] + node->size[0] + node->padding, node->pos[1] - node->padding - node->border,
						node->border, node->size[1] + (node->padding*2) + (node->border*2), 0, node->bordercolor);
					/* top */
					R_DrawFill(node->pos[0] - node->padding, node->pos[1] - node->padding - node->border,
						node->size[0] + (node->padding*2), node->border, 0, node->bordercolor);
					/* down */
					R_DrawFill(node->pos[0] - node->padding, node->pos[1] + node->size[1] + node->padding,
						node->size[0] + (node->padding*2), node->border, 0, node->bordercolor);
				}

				/* mouse darken effect */
				VectorScale(node->color, 0.8, color);
				color[3] = node->color[3];
				if (node->mousefx && node->type == MN_PIC && mouseOver && sp > pp)
					R_ColorBlend(color);
				else if (node->type != MN_SELECTBOX)
					R_ColorBlend(node->color);

				/* get the reference */
				if (node->type != MN_BAR && node->type != MN_CONTAINER && node->type != MN_BASEMAP
					&& node->type != MN_TEXT && node->type != MN_MAP && node->type != MN_ZONE && node->type != MN_LINESTRIP) {
					ref = MN_GetReferenceString(menu, node->data[MN_DATA_STRING_OR_IMAGE_OR_MODEL]);
					if (!ref) {
						/* these have default values for their image */
						if (node->type != MN_SELECTBOX && node->type != MN_CHECKBOX) {
							/* bad reference */
							node->invis = qtrue;
							Com_Printf("MN_DrawActiveMenus: node \"%s\" bad reference \"%s\" (menu %s)\n", node->name, (char*)node->data, node->menu->name);
							continue;
						}
					} else
						Q_strncpyz(source, ref, MAX_VAR);
				} else {
					ref = NULL;
					*source = '\0';
				}

				switch (node->type) {
				case MN_PIC:
					/* hack for ekg pics */
					if (!Q_strncmp(node->name, "ekg_", 4)) {
						int pt;

						if (node->name[4] == 'm')
							pt = Cvar_VariableInteger("mn_morale") / 2;
						else
							pt = Cvar_VariableInteger("mn_hp");
						node->texl[1] = (3 - (int) (pt < 60 ? pt : 60) / 20) * 32;
						node->texh[1] = node->texl[1] + 32;
						node->texl[0] = -(int) (0.01 * (node->name[4] - 'a') * cl.time) % 64;
						node->texh[0] = node->texl[0] + node->size[0];
					}
					if (ref && *ref)
						R_DrawNormPic(node->pos[0], node->pos[1], node->size[0], node->size[1],
								node->texh[0], node->texh[1], node->texl[0], node->texl[1], node->align, node->blend, ref);
					break;

				case MN_CHECKBOX:
					{
						const char *image;
						/* image set? */
						if (ref && *ref)
							image = ref;
						else
							image = "menu/checkbox";
						ref = MN_GetReferenceString(menu, node->data[MN_DATA_MODEL_SKIN_OR_CVAR]);
						switch (*ref) {
						case '0':
							R_DrawNormPic(node->pos[0], node->pos[1], node->size[0], node->size[1],
								node->texh[0], node->texh[1], node->texl[0], node->texl[1], node->align, node->blend, va("%s_unchecked", image));
							break;
						case '1':
							R_DrawNormPic(node->pos[0], node->pos[1], node->size[0], node->size[1],
								node->texh[0], node->texh[1], node->texl[0], node->texl[1], node->align, node->blend, va("%s_checked", image));
							break;
						default:
							Com_Printf("Error - invalid value for MN_CHECKBOX node - only 0/1 allowed\n");
						}
						break;
					}

				case MN_SELECTBOX:
					{
						selectBoxOptions_t* selectBoxOption;
						int selBoxX, selBoxY;
						const char *image = ref;
						if (!image)
							image = "menu/selectbox";
						if (!node->data[MN_DATA_MODEL_SKIN_OR_CVAR]) {
							Com_Printf("MN_DrawMenus: skip node '%s' (MN_SELECTBOX) - no cvar given (menu %s)\n", node->name, node->menu->name);
							node->invis = qtrue;
							break;
						}
						ref = MN_GetReferenceString(menu, node->data[MN_DATA_MODEL_SKIN_OR_CVAR]);

						font = MN_GetFont(menu, node);
						selBoxX = node->pos[0] + SELECTBOX_SIDE_WIDTH;
						selBoxY = node->pos[1] + SELECTBOX_SPACER;

						/* left border */
						R_DrawNormPic(node->pos[0], node->pos[1], SELECTBOX_SIDE_WIDTH, node->size[1],
							SELECTBOX_SIDE_WIDTH, 20.0f, 0.0f, 0.0f, node->align, node->blend, image);
						/* stretched middle bar */
						R_DrawNormPic(node->pos[0] + SELECTBOX_SIDE_WIDTH, node->pos[1], node->size[0], node->size[1],
							12.0f, 20.0f, 7.0f, 0.0f, node->align, node->blend, image);
						/* right border (arrow) */
						R_DrawNormPic(node->pos[0] + SELECTBOX_SIDE_WIDTH + node->size[0], node->pos[1], SELECTBOX_DEFAULT_HEIGHT, node->size[1],
							32.0f, 20.0f, 12.0f, 0.0f, node->align, node->blend, image);
						/* draw the label for the current selected option */
						for (selectBoxOption = node->options; selectBoxOption; selectBoxOption = selectBoxOption->next) {
							if (!Q_strcmp(selectBoxOption->value, ref)) {
								R_FontDrawString(font, node->align, selBoxX, selBoxY,
									selBoxX, selBoxY, node->size[0] - 4, 0,
									node->texh[0], _(selectBoxOption->label), 0, 0, NULL, qfalse);
							}
						}

						/* active? */
						if (node->state) {
							selBoxY += node->size[1];
							selectBoxNode = node;

							/* drop down menu */
							/* left side */
							R_DrawNormPic(node->pos[0], node->pos[1] + node->size[1], SELECTBOX_SIDE_WIDTH, node->size[1] * node->height,
								7.0f, 28.0f, 0.0f, 21.0f, node->align, node->blend, image);

							/* stretched middle bar */
							R_DrawNormPic(node->pos[0] + SELECTBOX_SIDE_WIDTH, node->pos[1] + node->size[1], node->size[0], node->size[1] * node->height,
								16.0f, 28.0f, 7.0f, 21.0f, node->align, node->blend, image);

							/* right side */
							R_DrawNormPic(node->pos[0] + SELECTBOX_SIDE_WIDTH + node->size[0], node->pos[1] + node->size[1], SELECTBOX_SIDE_WIDTH, node->size[1] * node->height,
								23.0f, 28.0f, 16.0f, 21.0f, node->align, node->blend, image);

							/* now draw all available options for this selectbox */
							for (selectBoxOption = node->options; selectBoxOption; selectBoxOption = selectBoxOption->next) {
								/* draw the hover effect */
								if (selectBoxOption->hovered)
									R_DrawFill(selBoxX, selBoxY, node->size[0], SELECTBOX_DEFAULT_HEIGHT, ALIGN_UL, node->color);
								/* print the option label */
								R_FontDrawString(font, node->align, selBoxX, selBoxY,
									selBoxX, node->pos[1] + node->size[1], node->size[0] - 4, 0,
									node->texh[0], _(selectBoxOption->label), 0, 0, NULL, qfalse);
								/* next entries' position */
								selBoxY += node->size[1];
								/* reset the hovering - will be recalculated next frame */
								selectBoxOption->hovered = qfalse;
							}
							/* left side */
							R_DrawNormPic(node->pos[0], selBoxY - SELECTBOX_SPACER, SELECTBOX_SIDE_WIDTH, SELECTBOX_BOTTOM_HEIGHT,
								7.0f, 32.0f, 0.0f, 28.0f, node->align, node->blend, image);

							/* stretched middle bar */
							R_DrawNormPic(node->pos[0] + SELECTBOX_SIDE_WIDTH, selBoxY - SELECTBOX_SPACER, node->size[0], SELECTBOX_BOTTOM_HEIGHT,
								16.0f, 32.0f, 7.0f, 28.0f, node->align, node->blend, image);

							/* right bottom side */
							R_DrawNormPic(node->pos[0] + SELECTBOX_SIDE_WIDTH + node->size[0], selBoxY - SELECTBOX_SPACER,
								SELECTBOX_SIDE_WIDTH, SELECTBOX_BOTTOM_HEIGHT,
								23.0f, 32.0f, 16.0f, 28.0f, node->align, node->blend, image);
						}
						break;
					}

				case MN_STRING:
					font = MN_GetFont(menu, node);
					ref += node->horizontalScroll;
					/* blinking */
					if (!node->mousefx || cl.time % 1000 < 500)
						R_FontDrawString(font, node->align, node->pos[0], node->pos[1], node->pos[0], node->pos[1], node->size[0], 0, node->texh[0], ref, 0, 0, NULL, qfalse);
					else
						R_FontDrawString(font, node->align, node->pos[0], node->pos[1], node->pos[0], node->pos[1], node->size[0], node->size[1], node->texh[0], va("%s*\n", ref), 0, 0, NULL, qfalse);
					break;

				case MN_TEXT:
					if (menuText[node->num]) {
						font = MN_GetFont(menu, node);
						MN_DrawTextNode(menuText[node->num], NULL, font, node, node->pos[0], node->pos[1], node->size[0], node->size[1]);
					} else if (menuTextLinkedList[node->num]) {
						font = MN_GetFont(menu, node);
						MN_DrawTextNode(NULL, menuTextLinkedList[node->num], font, node, node->pos[0], node->pos[1], node->size[0], node->size[1]);
					} else if (node->num == TEXT_MESSAGESYSTEM) {
						if (node->data[MN_DATA_ANIM_OR_FONT])
							font = MN_GetReferenceString(menu, node->data[MN_DATA_ANIM_OR_FONT]);
						else
							font = "f_small";

						y = node->pos[1];
						node->textLines = 0;
						message = messageStack;
						while (message) {
							if (node->textLines >= node->height) {
								/* @todo: Draw the scrollbars */
								break;
							}
							node->textLines++;

							/* maybe due to scrolling this line is not visible */
							if (node->textLines > node->textScroll) {
								int offset = 0;
								char text[TIMESTAMP_TEXT + MAX_MESSAGE_TEXT];
								/* get formatted date text and pixel width of text */
								MN_TimestampedText(text, message, sizeof(text));
								R_FontLength(font, text, &offset, &height);
								/* append remainder of message */
								Q_strcat(text, message->text, sizeof(text));
								R_FontLength(font, text, &width, &height);
								if (!width)
									break;
								if (width > node->pos[0] + node->size[0]) {
									int indent = node->pos[0];
									tab = text;
									while (qtrue) {
										y += R_FontDrawString(font, ALIGN_UL, indent, y, node->pos[0], node->pos[1], node->size[0], node->size[1], node->texh[0], tab, 0, 0, NULL, qfalse);
										/* we use a backslash to determine where to break the line */
										end = strstr(tab, "\\");
										if (!end)
											break;
										*end++ = '\0';
										tab = end;
										node->textLines++;
										if (node->textLines >= node->height)
											break;
										indent = offset;
									}
								} else {
									/* newline to space - we don't need this */
									while ((end = strstr(text, "\\")) != NULL)
										*end = ' ';

									y += R_FontDrawString(font, ALIGN_UL, node->pos[0], y, node->pos[0], node->pos[1], node->size[0], node->size[1], node->texh[0], text, 0, 0, NULL, qfalse);
								}
							}

							message = message->next;
						}
					}
					break;

				case MN_BAR:
					{
						float fac, bar_width;

						/* in the case of MN_BAR the first three data array values are float values - see menuDataValues_t */
						fac = node->size[0] / (MN_GetReferenceFloat(menu, node->data[0]) - MN_GetReferenceFloat(menu, node->data[1]));
						bar_width = (MN_GetReferenceFloat(menu, node->data[2]) - MN_GetReferenceFloat(menu, node->data[1])) * fac;
						R_DrawFill(node->pos[0], node->pos[1], bar_width, node->size[1], node->align, mouseOver ? color : node->color);
					}
					break;

				case MN_LINESTRIP:
					if (node->linestrips.numStrips > 0) {
						/* Draw all linestrips. */
						for (i = 0; i < node->linestrips.numStrips; i++) {
							/* Draw this line if it's valid. */
							if (node->linestrips.pointList[i] && (node->linestrips.numPoints[i] > 0)) {
								R_ColorBlend(node->linestrips.color[i]);
								R_DrawLineStrip(node->linestrips.numPoints[i], node->linestrips.pointList[i]);
							}
						}
					}
					break;

				case MN_CONTAINER:
					if (menuInventory) {
						vec3_t scale = {3.5, 3.5, 3.5};
						invList_t *ic;

						color[0] = color[1] = color[2] = 0.5;
						color[3] = 1;

						if (node->mousefx == C_UNDEFINED)
							MN_FindContainer(node);
						if (node->mousefx == NONE)
							break;

						if (csi.ids[node->mousefx].single) {
							/* single item container (special case for left hand) */
							if (node->mousefx == csi.idLeft && !menuInventory->c[csi.idLeft]) {
								color[3] = 0.5;
								if (menuInventory->c[csi.idRight] && csi.ods[menuInventory->c[csi.idRight]->item.t].holdTwoHanded)
									MN_DrawItem(node->pos, menuInventory->c[csi.idRight]->item, node->size[0] / C_UNIT,
												node->size[1] / C_UNIT, 0, 0, scale, color);
							} else if (menuInventory->c[node->mousefx]) {
								if (node->mousefx == csi.idRight &&
										csi.ods[menuInventory->c[csi.idRight]->item.t].fireTwoHanded &&
										menuInventory->c[csi.idLeft]) {
									color[0] = color[1] = color[2] = color[3] = 0.5;
									MN_DrawDisabled(node);
								}
								MN_DrawItem(node->pos, menuInventory->c[node->mousefx]->item, node->size[0] / C_UNIT,
											node->size[1] / C_UNIT, 0, 0, scale, color);
							}
						} else {
							/* standard container */
							for (ic = menuInventory->c[node->mousefx]; ic; ic = ic->next) {
								MN_DrawItem(node->pos, ic->item, csi.ods[ic->item.t].sx, csi.ods[ic->item.t].sy, ic->x, ic->y, scale, color);
							}
						}
						/* draw free space if dragging - but not for idEquip */
						if (mouseSpace == MS_DRAG && node->mousefx != csi.idEquip)
							MN_InvDrawFree(menuInventory, node);

						/* Draw tooltip for weapon or ammo */
						if (mouseSpace != MS_DRAG && node->state && cl_show_tooltips->integer) {
							/* Find out where the mouse is */
							itemHover = Com_SearchInInventory(menuInventory,
								node->mousefx, (mousePosX - node->pos[0]) / C_UNIT, (mousePosY - node->pos[1]) / C_UNIT);
						}
					}
					break;

				case MN_ITEM:
					color[0] = color[1] = color[2] = 0.5;
					color[3] = 1;

					if (node->mousefx == C_UNDEFINED)
						MN_FindContainer(node);
					if (node->mousefx == NONE) {
						Com_Printf("no container for drawing this item (%s)...\n", ref);
						break;
					}

					for (item.t = 0; item.t < csi.numODs; item.t++)
						if (!Q_strncmp(ref, csi.ods[item.t].id, MAX_VAR))
							break;
					if (item.t == csi.numODs)
						break;

					MN_DrawItem(node->pos, item, 0, 0, 0, 0, node->scale, color);
					break;

				case MN_MODEL:
				{
					/* if true we have to reset the anim state and make sure the correct model is shown */
					qboolean updateModel = qfalse;
					/* set model properties */
					if (!*source)
						break;
					node->menuModel = MN_GetMenuModel(source);

					/* direct model name - no menumodel definition */
					if (!node->menuModel) {
						/* prevent the searching for a menumodel def in the next frame */
						menuModel = NULL;
						mi.model = R_RegisterModelShort(source);
						mi.name = source;
						if (!mi.model) {
							Com_Printf("Could not find model '%s'\n", source);
							break;
						}
					} else
						menuModel = node->menuModel;

					/* check whether the cvar value changed */
					if (Q_strncmp(node->oldRefValue, source, sizeof(node->oldRefValue))) {
						Q_strncpyz(node->oldRefValue, source, sizeof(node->oldRefValue));
						updateModel = qtrue;
					}

					mi.origin = node->origin;
					mi.angles = node->angles;
					mi.scale = node->scale;
					mi.center = node->center;
					mi.color = node->color;

					/* autoscale? */
					if (!node->scale[0]) {
						mi.scale = NULL;
						mi.center = node->size;
					}

					do {
						/* no animation */
						mi.frame = 0;
						mi.oldframe = 0;
						mi.backlerp = 0;
						if (menuModel) {
							assert(menuModel->model);
							mi.model = R_RegisterModelShort(menuModel->model);
							if (!mi.model) {
								menuModel = menuModel->next;
								/* list end */
								if (!menuModel)
									break;
								continue;
							}

							mi.skin = menuModel->skin;
							mi.name = menuModel->model;

							/* set mi pointers to menuModel */
							mi.origin = menuModel->origin;
							mi.angles = menuModel->angles;
							mi.center = menuModel->center;
							mi.color = menuModel->color;
							mi.scale = menuModel->scale;

							/* no tag and no parent means - base model or single model */
							if (!menuModel->tag && !menuModel->parent) {
								if (menuModel->menuTransformCnt) {
									for (i = 0; i < menuModel->menuTransformCnt; i++) {
										if (menu == menuModel->menuTransform[i].menuPtr) {
											/* Use menu scale if defined. */
											if (menuModel->menuTransform[i].useScale) {
												VectorCopy(menuModel->menuTransform[i].scale, mi.scale);
											} else {
												VectorCopy(node->scale, mi.scale);
											}

											/* Use menu angles if defined. */
											if (menuModel->menuTransform[i].useAngles) {
												VectorCopy(menuModel->menuTransform[i].angles, mi.angles);
											} else {
												VectorCopy(node->angles, mi.angles);
											}

											/* Use menu origin if defined. */
											if (menuModel->menuTransform[i].useOrigin) {
												VectorAdd(node->origin, menuModel->menuTransform[i].origin, mi.origin);
											} else {
												VectorCopy(node->origin, mi.origin);
											}
											break;
										}
									}
									/* not for this menu */
									if (i == menuModel->menuTransformCnt) {
										VectorCopy(node->scale, mi.scale);
										VectorCopy(node->angles, mi.angles);
										VectorCopy(node->origin, mi.origin);
									}
								} else {
									VectorCopy(node->scale, mi.scale);
									VectorCopy(node->angles, mi.angles);
									VectorCopy(node->origin, mi.origin);
								}
								Vector4Copy(node->color, mi.color);
								VectorCopy(node->center, mi.center);

								/* get the animation given by menu node properties */
								if (node->data[MN_DATA_ANIM_OR_FONT] && *(char *) node->data[MN_DATA_ANIM_OR_FONT]) {
									ref = MN_GetReferenceString(menu, node->data[MN_DATA_ANIM_OR_FONT]);
								/* otherwise use the standard animation from modelmenu defintion */
								} else
									ref = menuModel->anim;

								/* only base models have animations */
								if (ref && *ref) {
									as = &menuModel->animState;
									anim = R_AnimGetName(as, mi.model);
									/* initial animation or animation change */
									if (!anim || (anim && Q_strncmp(anim, ref, MAX_VAR)))
										R_AnimChange(as, mi.model, ref);
									else
										R_AnimRun(as, mi.model, cls.frametime * 1000);

									mi.frame = as->frame;
									mi.oldframe = as->oldframe;
									mi.backlerp = as->backlerp;
								}
								R_DrawModelDirect(&mi, NULL, NULL);
							/* tag and parent defined */
							} else {
								/* place this menumodel part on an already existing menumodel tag */
								assert(menuModel->parent);
								assert(menuModel->tag);
								menuModelParent = MN_GetMenuModel(menuModel->parent);
								if (!menuModelParent) {
									Com_Printf("Menumodel: Could not get the menuModel '%s'\n", menuModel->parent);
									break;
								}
								pmi.model = R_RegisterModelShort(menuModelParent->model);
								if (!pmi.model) {
									Com_Printf("Menumodel: Could not get the model '%s'\n", menuModelParent->model);
									break;
								}

								pmi.name = menuModelParent->model;
								pmi.origin = menuModelParent->origin;
								pmi.angles = menuModelParent->angles;
								pmi.scale = menuModelParent->scale;
								pmi.center = menuModelParent->center;
								pmi.color = menuModelParent->color;

								/* autoscale? */
								if (!mi.scale[0]) {
									mi.scale = NULL;
									mi.center = node->size;
								}

								as = &menuModelParent->animState;
								pmi.frame = as->frame;
								pmi.oldframe = as->oldframe;
								pmi.backlerp = as->backlerp;

								R_DrawModelDirect(&mi, &pmi, menuModel->tag);
							}
							menuModel = menuModel->next;
						} else {
							/* get skin */
							if (node->data[MN_DATA_MODEL_SKIN_OR_CVAR] && *(char *) node->data[MN_DATA_MODEL_SKIN_OR_CVAR])
								mi.skin = atoi(MN_GetReferenceString(menu, node->data[MN_DATA_MODEL_SKIN_OR_CVAR]));
							else
								mi.skin = 0;

							/* do animations */
							if (node->data[MN_DATA_ANIM_OR_FONT] && *(char *) node->data[MN_DATA_ANIM_OR_FONT]) {
								ref = MN_GetReferenceString(menu, node->data[MN_DATA_ANIM_OR_FONT]);
								if (updateModel) {
									/* model has changed but mem is already reserved in pool */
									if (node->data[MN_DATA_MODEL_ANIMATION_STATE]) {
										Mem_Free(node->data[MN_DATA_MODEL_ANIMATION_STATE]);
										node->data[MN_DATA_MODEL_ANIMATION_STATE] = NULL;
									}
								}
								if (!node->data[MN_DATA_MODEL_ANIMATION_STATE]) {
									as = (animState_t *) Mem_PoolAlloc(sizeof(animState_t), cl_genericPool, CL_TAG_NONE);
									R_AnimChange(as, mi.model, ref);
									node->data[MN_DATA_MODEL_ANIMATION_STATE] = as;
								} else {
									/* change anim if needed */
									as = node->data[MN_DATA_MODEL_ANIMATION_STATE];
									anim = R_AnimGetName(as, mi.model);
									if (anim && Q_strncmp(anim, ref, MAX_VAR))
										R_AnimChange(as, mi.model, ref);
									R_AnimRun(as, mi.model, cls.frametime * 1000);
								}

								mi.frame = as->frame;
								mi.oldframe = as->oldframe;
								mi.backlerp = as->backlerp;
							}

							/* place on tag */
							if (node->data[MN_DATA_MODEL_TAG]) {
								menuNode_t *search;
								char parent[MAX_VAR];
								char *tag;

								Q_strncpyz(parent, MN_GetReferenceString(menu, node->data[MN_DATA_MODEL_TAG]), MAX_VAR);
								tag = parent;
								/* tag "menuNodeName modelTag" */
								while (*tag && *tag != ' ')
									tag++;
								/* split node name and tag */
								*tag++ = 0;

								for (search = menu->firstNode; search != node && search; search = search->next)
									if (search->type == MN_MODEL && !Q_strncmp(search->name, parent, MAX_VAR)) {
										char model[MAX_VAR];

										Q_strncpyz(model, MN_GetReferenceString(menu, search->data[MN_DATA_STRING_OR_IMAGE_OR_MODEL]), MAX_VAR);

										pmi.model = R_RegisterModelShort(model);
										if (!pmi.model)
											break;

										pmi.name = model;
										pmi.origin = search->origin;
										pmi.angles = search->angles;
										pmi.scale = search->scale;
										pmi.center = search->center;
										pmi.color = search->color;

										/* autoscale? */
										if (!node->scale[0]) {
											mi.scale = NULL;
											mi.center = node->size;
										}

										as = search->data[MN_DATA_MODEL_ANIMATION_STATE];
										pmi.frame = as->frame;
										pmi.oldframe = as->oldframe;
										pmi.backlerp = as->backlerp;
										R_DrawModelDirect(&mi, &pmi, tag);
										break;
									}
							} else
								R_DrawModelDirect(&mi, NULL, NULL);
						}
					/* for normal models (normal = no menumodel definition) this
					 * menuModel pointer is null - the do-while loop is only
					 * ran once */
					} while (menuModel != NULL);
					break;
				}

				case MN_MAP:
					if (curCampaign) {
						CL_CampaignRun();	/* advance time */
						MAP_DrawMap(node); /* Draw geoscape */
					}
					break;

				case MN_BASEMAP:
					B_DrawBase(node);
					break;
				}	/* switch */

				/* mouseover? */
				if (node->state == qtrue)
					menu->hoverNode = node;

				if (mn_debugmenu->integer) {
					MN_DrawTooltip("f_small", node->name, node->pos[0], node->pos[1], node->size[1], 0);
/*					R_FontDrawString("f_verysmall", ALIGN_UL, node->pos[0], node->pos[1], node->pos[0], node->pos[1], node->size[0], 0, node->texh[0], node->name, 0, 0, NULL, qfalse);*/
				}
			}	/* if */

		}	/* for */
		if (sp == menuStackPos && menu->hoverNode && cl_show_tooltips->integer) {
			/* we are hovering an item and also want to display it
	 		 * make sure that we draw this on top of every other node */
			if (itemHover) {
				char tooltiptext[MAX_VAR*2] = "";
				int x = mousePosX, y = mousePosY;
				const int itemToolTipWidth = 250;
				int linenum;
				int itemToolTipHeight;
				/* Get name and info about item */
				linenum = INV_GetItemTooltip(itemHover->item, tooltiptext, sizeof(tooltiptext));
				itemToolTipHeight = linenum * 25; /** @todo make this a constant/define? */
#if 1
				if (x + itemToolTipWidth > VID_NORM_WIDTH)
					x = VID_NORM_WIDTH - itemToolTipWidth;
				if (y + itemToolTipHeight > VID_NORM_HEIGHT)
					y = VID_NORM_HEIGHT - itemToolTipHeight;
				MN_DrawTextNode(tooltiptext, NULL, "f_small", menu->hoverNode, x, y, itemToolTipWidth, itemToolTipHeight);
#else
				MN_DrawTooltip("f_small", tooltiptext, x, y, itemToolTipWidth, itemToolTipHeight);
#endif
			} else {
				MN_Tooltip(menu, menu->hoverNode, mousePosX, mousePosY);
				menu->hoverNode = NULL;
			}
		}
	}

	R_ColorBlend(NULL);
}

/*
==============================================================
GENERIC MENU FUNCTIONS
==============================================================
*/

/**
 * @brief Remove the menu from the menu stack
 * @param[in] menu The menu to remove from the stack
 * @sa MN_PushMenuDelete
 */
static void MN_DeleteMenuFromStack (menu_t * menu)
{
	int i;

	for (i = 0; i < menuStackPos; i++)
		if (menuStack[i] == menu) {
			/* @todo don't leave the loop even if we found it - there still
			 * may be other copies around in the stack of the same menu
			 * @sa MN_PushCopyMenu_f */
			for (menuStackPos--; i < menuStackPos; i++)
				menuStack[i] = menuStack[i + 1];
			return;
		}
}

/**
 * @brief Push a menu onto the menu stack
 * @param[in] name Name of the menu to push onto menu stack
 * @param[in] delete Delete the menu from the menu stack before readd it
 * @return pointer to menu_t
 */
static menu_t* MN_PushMenuDelete (const char *name, qboolean delete)
{
	int i;
	menuNode_t *node;

	MN_FocusRemove();

	for (i = 0; i < numMenus; i++)
		if (!Q_strncmp(menus[i].name, name, MAX_VAR)) {
			/* found the correct add it to stack or bring it on top */
			if (delete)
				MN_DeleteMenuFromStack(&menus[i]);

			if (menuStackPos < MAX_MENUSTACK)
				menuStack[menuStackPos++] = &menus[i];
			else
				Com_Printf("Menu stack overflow\n");

			/* initialize it */
			if (menus[i].initNode)
				MN_ExecuteActions(&menus[i], menus[i].initNode->click);

			if (cls.key_dest == key_input && msg_mode == MSG_MENU)
				Key_Event(K_ENTER, qtrue, cls.realtime);
			Key_SetDest(key_game);

			for (node = menus[i].firstNode; node; node = node->next) {
				/* if there is a timeout value set, init the menu with current
				 * client time */
				if (node->timeOut)
					node->timePushed = cl.time;
			}

			return menus + i;
		}

	Com_Printf("Didn't find menu \"%s\"\n", name);
	return NULL;
}

/**
 * @brief Complete function for mn_push
 * @sa Cmd_AddParamCompleteFunction
 * @sa MN_PushMenu
 * @note Does not really complete the input - but shows at least all parsed menus
 */
static int MN_CompletePushMenu (const char *partial, const char **match)
{
	int i, matches = 0;
	const char *localMatch[MAX_COMPLETE];
	size_t len;

	len = strlen(partial);
	if (!len) {
		for (i = 0; i < numMenus; i++)
			Com_Printf("%s\n", menus[i].name);
		return 0;
	}

	localMatch[matches] = NULL;

	/* check for partial matches */
	for (i = 0; i < numMenus; i++)
		if (!Q_strncmp(partial, menus[i].name, len)) {
			Com_Printf("%s\n", menus[i].name);
			localMatch[matches++] = menus[i].name;
			if (matches >= MAX_COMPLETE)
				break;
		}

	return Cmd_GenericCompleteFunction(len, match, matches, localMatch);
}

/**
 * @brief Push a menu onto the menu stack
 * @param[in] name Name of the menu to push onto menu stack
 * @return pointer to menu_t
 */
menu_t* MN_PushMenu (const char *name)
{
	return MN_PushMenuDelete(name, qtrue);
}

/**
 * @brief Console function to push a menu onto the menu stack
 * @sa MN_PushMenu
 */
static void MN_PushMenu_f (void)
{
	if (Cmd_Argc() > 1)
		MN_PushMenu(Cmd_Argv(1));
	else
		Com_Printf("Usage: %s <name>\n", Cmd_Argv(0));
}

/**
 * @brief Console function to hide the HUD in battlescape mode
 * Note: relies on a "nohud" menu existing
 * @sa MN_PushMenu
 */
static void MN_PushNoHud_f (void)
{
	/* can't hide hud if we are not in battlescape */
	if (!CL_OnBattlescape())
		return;

	MN_PushMenu("nohud");
}

/**
 * @brief Console function to push a menu, without deleting its copies
 * @sa MN_PushMenu
 */
static void MN_PushCopyMenu_f (void)
{
	if (Cmd_Argc() > 1) {
		Cvar_SetValue("mn_escpop", mn_escpop->value + 1);
		MN_PushMenuDelete(Cmd_Argv(1), qfalse);
	} else {
		Com_Printf("Usage: %s <name>\n", Cmd_Argv(0));
	}
}


/**
 * @brief Pops a menu from the menu stack
 * @param[in] all If true pop all menus from stack
 * @sa MN_PopMenu_f
 */
void MN_PopMenu (qboolean all)
{
	/* make sure that we end all input buffers */
	if (cls.key_dest == key_input && msg_mode == MSG_MENU)
		Key_Event(K_ENTER, qtrue, cls.realtime);

	MN_FocusRemove();

	if (all)
		while (menuStackPos > 0) {
			menuStackPos--;
			if (menuStack[menuStackPos]->closeNode)
				MN_ExecuteActions(menuStack[menuStackPos], menuStack[menuStackPos]->closeNode->click);
		}

	if (menuStackPos > 0) {
		menuStackPos--;
		if (menuStack[menuStackPos]->closeNode)
			MN_ExecuteActions(menuStack[menuStackPos], menuStack[menuStackPos]->closeNode->click);
	}

	if (!all && menuStackPos == 0) {
		if (!Q_strncmp(menuStack[0]->name, mn_main->string, MAX_VAR)) {
			if (*mn_active->string)
				MN_PushMenu(mn_active->string);
			if (!menuStackPos)
				MN_PushMenu(mn_main->string);
		} else {
			if (*mn_main->string)
				MN_PushMenu(mn_main->string);
			if (!menuStackPos)
				MN_PushMenu(mn_active->string);
		}
	}

	Key_SetDest(key_game);
}

/**
 * @brief Console function to pop a menu from the menu stack
 * @sa MN_PopMenu
 * @note The cvar mn_escpop defined how often the MN_PopMenu function is called.
 * This is useful for e.g. nodes that doesn't have a render node (for example: video options)
 */
static void MN_PopMenu_f (void)
{
	if (Cmd_Argc() < 2 || Q_strncmp(Cmd_Argv(1), "esc", 3)) {
		MN_PopMenu(qfalse);
	} else {
		int i;

		for (i = 0; i < mn_escpop->integer; i++)
			MN_PopMenu(qfalse);

		Cvar_Set("mn_escpop", "1");
	}
}


static void MN_Modify_f (void)
{
	float value;

	if (Cmd_Argc() < 5)
		Com_Printf("Usage: %s <name> <amount> <min> <max>\n", Cmd_Argv(0));

	value = Cvar_VariableValue(Cmd_Argv(1));
	value += atof(Cmd_Argv(2));
	if (value < atof(Cmd_Argv(3)))
		value = atof(Cmd_Argv(3));
	else if (value > atof(Cmd_Argv(4)))
		value = atof(Cmd_Argv(4));

	Cvar_SetValue(Cmd_Argv(1), value);
}


static void MN_ModifyWrap_f (void)
{
	float value;

	if (Cmd_Argc() < 5)
		Com_Printf("Usage: %s <name> <amount> <min> <max>\n", Cmd_Argv(0));

	value = Cvar_VariableValue(Cmd_Argv(1));
	value += atof(Cmd_Argv(2));
	if (value < atof(Cmd_Argv(3)))
		value = atof(Cmd_Argv(4));
	else if (value > atof(Cmd_Argv(4)))
		value = atof(Cmd_Argv(3));

	Cvar_SetValue(Cmd_Argv(1), value);
}


static void MN_ModifyString_f (void)
{
	qboolean next;
	const char *current, *list;
	char *tp;
	char token[MAX_VAR], last[MAX_VAR], first[MAX_VAR];
	int add;

	if (Cmd_Argc() < 4)
		Com_Printf("Usage: %s <name> <amount> <list>\n", Cmd_Argv(0));

	current = Cvar_VariableString(Cmd_Argv(1));
	add = atoi(Cmd_Argv(2));
	list = Cmd_Argv(3);
	last[0] = 0;
	first[0] = 0;
	next = qfalse;

	while (add) {
		tp = token;
		while (*list && *list != ':')
			*tp++ = *list++;
		if (*list)
			list++;
		*tp = 0;

		if (*token && !first[0])
			Q_strncpyz(first, token, MAX_VAR);

		if (!*token) {
			if (add < 0 || next)
				Cvar_Set(Cmd_Argv(1), last);
			else
				Cvar_Set(Cmd_Argv(1), first);
			return;
		}

		if (next) {
			Cvar_Set(Cmd_Argv(1), token);
			return;
		}

		if (!Q_strncmp(token, current, MAX_VAR)) {
			if (add < 0) {
				if (last[0])
					Cvar_Set(Cmd_Argv(1), last);
				else
					Cvar_Set(Cmd_Argv(1), first);
				return;
			} else
				next = qtrue;
		}
		Q_strncpyz(last, token, MAX_VAR);
	}
}


/**
 * @brief Shows the corresponding strings in menu
 * Example: Optionsmenu - fullscreen: yes
 */
static void MN_Translate_f (void)
{
	qboolean next;
	const char *current, *list;
	char *orig, *trans;
	char original[MAX_VAR], translation[MAX_VAR];

	if (Cmd_Argc() < 4) {
		Com_Printf("Usage: %s <source> <dest> <list>\n", Cmd_Argv(0));
		return;
	}

	current = Cvar_VariableString(Cmd_Argv(1));
	list = Cmd_Argv(3);
	next = qfalse;

	while (*list) {
		orig = original;
		while (*list && *list != ':')
			*orig++ = *list++;
		*orig = 0;
		list++;

		trans = translation;
		while (*list && *list != ',')
			*trans++ = *list++;
		*trans = 0;
		list++;

		if (!Q_strncmp(current, original, MAX_VAR)) {
			Cvar_Set(Cmd_Argv(2), _(translation));
			return;
		}
	}

	/* nothing found, copy value */
	Cvar_Set(Cmd_Argv(2), _(current));
}

#define MAX_TUTORIALLIST 512
static char tutorialList[MAX_TUTORIALLIST];

static void MN_GetTutorials_f (void)
{
	int i;
	tutorial_t *t;

	menuText[TEXT_LIST] = tutorialList;
	tutorialList[0] = 0;
	for (i = 0; i < numTutorials; i++) {
		t = &tutorials[i];
		Q_strcat(tutorialList, va("%s\n", t->name), sizeof(tutorialList));
	}
}

static void MN_ListTutorials_f (void)
{
	int i;

	Com_Printf("Tutorials: %i\n", numTutorials);
	for (i = 0; i < numTutorials; i++) {
		Com_Printf("tutorial: %s\n", tutorials[i].name);
		Com_Printf("..sequence: %s\n", tutorials[i].sequence);
	}
}

/**
 * @brief click function for text tutoriallist in menu_tutorials.ufo
 */
static void MN_TutorialListClick_f (void)
{
	int num;

	if (Cmd_Argc() < 2) {
		Com_Printf("Usage: %s <num>\n", Cmd_Argv(0));
		return;
	}

	num = atoi(Cmd_Argv(1));
	if (num < 0 || num >= numTutorials)
		return;

	Cmd_ExecuteString(va("seq_start %s", tutorials[num].sequence));
}

static void MN_ListMenuModels_f (void)
{
	int i;

	/* search for menumodels with same name */
	Com_Printf("menu models: %i\n", numMenuModels);
	for (i = 0; i < numMenuModels; i++)
		Com_Printf("id: %s\n...model: %s\n...need: %s\n\n", menuModels[i].id, menuModels[i].model, menuModels[i].need);
}

/**
 * @brief Prints a list of tab and newline seperated string to keylist char array that hold the key and the command desc
 */
static void MN_InitKeyList_f (void)
{
	static char keylist[2048];
	int i;

	*keylist = '\0';

	for (i = 0; i < K_LAST_KEY; i++)
		if (keybindings[i] && keybindings[i][0]) {
			Com_Printf("%s - %s\n", Key_KeynumToString(i), keybindings[i]);
			Q_strcat(keylist, va("%s\t%s\n", Key_KeynumToString(i), Cmd_GetCommandDesc(keybindings[i])), sizeof(keylist));
		}

	menuText[TEXT_LIST] = keylist;
}

#ifdef DEBUG
/**
 * @brief This function allows you inline editing of menus
 * @note Changes you made are lost on quit
 * @sa MN_PrintMenu_f
 */
static void MN_EditMenuNode_f (void)
{
	menuNode_t *node;
	const value_t *val;
	const char *nodeID, *var, *value;
	int np = -1, cnt = 0;

	/* not initialized yet - commandline? */
	if (menuStackPos <= 0)
		return;

	if (Cmd_Argc() < 4) {
		Com_Printf("Usage: %s <node> <var> <value>\n", Cmd_Argv(0));
		return;
	}
	Com_Printf("!!WARNING!! This function may be dangerous and should only be used if you know what you are doing\n");

	nodeID = Cmd_Argv(1);
	var = Cmd_Argv(2);
	value = Cmd_Argv(3);
	/* search the node */
	node = MN_GetNodeFromCurrentMenu(nodeID);

	if (!node) {
		/* didn't find node -> "kill" action and print error */
		Com_Printf("MN_EditMenuNode_f: node \"%s\" doesn't exist\n", nodeID);
		return;
	}

	for (val = nps; val->type; val++) {
		if (!Q_strcmp(val->string, var)) {
			Com_Printf("Found %s at offset %Zx\n", var, val->ofs);
			np = cnt;
			break;
		}
		cnt++;
	}

	if (np == -1) {
		Com_Printf("%s is not in the list of valid node paramters\n", var);
		return;
	}

	/* 0, -1, -2, -3, -4, -5 fills the data array in menuNode_t */
	if ((nps[np].ofs > 0) && (nps[np].ofs < (size_t)-5))
		Com_SetValue(node, value, nps[np].type, nps[np].ofs, nps[np].size);
	else
		Com_Printf("This var is not supported by inline editing\n");
}

/**
 * @brief This function allows you inline transforming of mdoels.
 * @note Changes you made are lost on quit
 * @sa MN_EditMenuNode_f
 */
static void MN_SetModelTransform_f (void)
{
	menuNode_t *node;
	const char *command, *nodeID;
	float x, y ,z;
	vec3_t value;


	/* not initialized yet - commandline? */
	if (menuStackPos <= 0)
		return;

	if (Cmd_Argc() < 5) {
		Com_Printf("Usage: %s <node> <x> <y> <z>\n", Cmd_Argv(0));
		return;
	}

	command = Cmd_Argv(0);
	nodeID = Cmd_Argv(1);
	x = atof(Cmd_Argv(2));
	y = atof(Cmd_Argv(3));
	z = atof(Cmd_Argv(4));

	VectorSet(value, x, y, z);

	/* search the node */
	node = MN_GetNode(MN_GetCurrentMenu(), nodeID);
	if (!node) {
		/* didn't find node -> "kill" action and print error */
		Com_Printf("MN_SetModelTransform_f: node \"%s\" doesn't exist\n", nodeID);
		return;
	} else if (node->type != MN_MODEL) {
		Com_Printf("MN_SetModelTransform_f: node \"%s\" isn't a model node\n", nodeID);
		return;
	}

	if (!Q_strcmp(command, "debug_mnscale")) {
		VectorCopy(value, node->scale);
	} else if (!Q_strcmp(command, "debug_mnangles")) {
		VectorCopy(value, node->angles);
	}
}
#endif /* DEBUG */

#ifdef DEBUG
/**
 * @brief Callback function that reloads the menus from file
 */
static void MN_ReloadMenus_f (void)
{
	const char *type, *name, *text;

	/* not initialized yet - commandline? */
	if (menuStackPos <= 0)
		return;

	assert(adataize);
	Mem_FreePool(cl_menuSysPool);

	/* pre-stage parsing */
	FS_BuildFileList("ufos/*.ufo");
	FS_NextScriptHeader(NULL, NULL, NULL);
	text = NULL;

	/* reset menu structures */
	numActions = 0;
	numNodes = 0;
	numMenus = 0;
	numMenuModels = 0;

	adata = (byte*)Mem_PoolAlloc(MENU_HUNK_SIZE, cl_menuSysPool, CL_TAG_MENU);
	adataize = MENU_HUNK_SIZE;
	curadata = adata;

	while ((type = FS_NextScriptHeader("ufos/*.ufo", &name, &text)) != 0 )
		if (!Q_strncmp(type, "menu", 4))
			MN_ParseMenu(name, &text);
}
#endif /* DEBUG */

#ifdef DEBUG
/**
 * @brief Callback function that prints the current menu from stack to game console
 */
static void MN_PrintMenu_f (void)
{
	menuNode_t *node;
	const value_t *val;
	menu_t *current;

	/* not initialized yet - commandline? */
	if (menuStackPos <= 0)
		return;

	current = MN_GetCurrentMenu();
	assert(current);
	Com_Printf("menu %s {\n", current->name);
	for (node = current->firstNode; node; node = node->next) {
		Com_Printf("  %s %s {\n", nt_strings[node->type], node->name);
		for (val = nps; val->type; val++) {
			if ((val->ofs > 0) && (val->ofs < (size_t)-5))
				Com_Printf("    %s  \"%s\"\n", val->string, Com_ValueToStr(node, val->type, val->ofs));
		}
		Com_Printf("  }\n");
	}
	Com_Printf("}\n");
}
#endif /* DEBUG */

/**
 * @brief Hides a given menu node
 * @note Sanity check whether node is null included
 */
void MN_HideNode (menuNode_t* node)
{
	if (node)
		node->invis = qtrue;
	if (!node)
		Com_Printf("MN_HideNode: No node given\n");
}

/**
 * @brief Script command to hide a given menu node
 */
static void MN_HideNode_f (void)
{
	if (Cmd_Argc() == 2)
		MN_HideNode(MN_GetNodeFromCurrentMenu(Cmd_Argv(1)));
	else
		Com_Printf("Usage: %s <node>\n", Cmd_Argv(0));
}

/**
 * @brief Unhides a given menu node
 * @note Sanity check whether node is null included
 */
void MN_UnHideNode (menuNode_t* node)
{
	if (node)
		node->invis = qfalse;
	if (!node)
		Com_Printf("MN_UnHideNode: No node given\n");
}

/**
 * @brief Resets the menuText pointers and the menuTextLinkedList lists
 */
void MN_MenuTextReset (int menuTextID)
{
	assert(menuTextID < MAX_MENUTEXTS);
	assert(menuTextID >= 0);

	menuText[menuTextID] = NULL;

	if (menuTextLinkedList[menuTextID]) {
		LIST_Delete(menuTextLinkedList[menuTextID]);
		menuTextLinkedList[menuTextID] = NULL;
	}
}

/**
 * @brief Resets the menuText pointers from a func node
 * @note You can give this function a parameter to only delete a specific list
 */
static void MN_MenuTextReset_f (void)
{
	int i;

	if (Cmd_Argc() == 2) {
		i = atoi(Cmd_Argv(1));
		if (i >= 0 && i < MAX_MENUTEXTS)
			MN_MenuTextReset(i);
		else
			Com_Printf("%s: invalid menuText ID: %i\n", Cmd_Argv(0), i);
	} else {
		for (i = 0; i < MAX_MENUTEXTS; i++)
			MN_MenuTextReset(i);
	}
}

/**
 * @brief Script command to unhide a given menu node
 */
static void MN_UnHideNode_f (void)
{
	if (Cmd_Argc() == 2)
		MN_UnHideNode(MN_GetNodeFromCurrentMenu(Cmd_Argv(1)));
	else
		Com_Printf("Usage: %s <node>\n", Cmd_Argv(0));
}

/**
 * @brief Initialize the menu data hunk, add cvars and commands
 * @note Also calls the 'reset' functions for production, basemanagement,
 * aliencontainmenu, employee, hospital and a lot more subfunctions
 * @note This function is called once
 * @sa MN_Shutdown
 * @sa B_ResetBaseManagement
 * @sa CL_InitLocal
 */
void MN_ResetMenus (void)
{
	int i;

	/* reset menu structures */
	numActions = 0;
	numNodes = 0;
	numMenus = 0;
	numMenuModels = 0;
	menuStackPos = 0;
	numTutorials = 0;
	numSelectBoxes = 0;

	messageStack = NULL;

	/* add commands */
	mn_escpop = Cvar_Get("mn_escpop", "1", 0, NULL);
	mn_debugmenu = Cvar_Get("mn_debugmenu", "0", 0, "Prints node names for debugging purposes");
	Cvar_Set("mn_main", "main");
	Cvar_Set("mn_sequence", "sequence");

	/* textbox */
	Cmd_AddCommand("mn_textscroll", MN_TextScroll_f, NULL);
	Cmd_AddCommand("msgmenu", CL_MessageMenu_f, "Activates the inline cvar editing");

	Cmd_AddCommand("mn_hidenode", MN_HideNode_f, "Hides a given menu node");
	Cmd_AddCommand("mn_unhidenode", MN_UnHideNode_f, "Unhides a given menu node");

	Cmd_AddCommand("mn_textreset", MN_MenuTextReset_f, "Resets the menuText pointers");

	/* print the keybindings to menuText */
	Cmd_AddCommand("mn_init_keylist", MN_InitKeyList_f, NULL);

	/* tutorial stuff */
	Cmd_AddCommand("listtutorials", MN_ListTutorials_f, "Show all tutorials");
	Cmd_AddCommand("gettutorials", MN_GetTutorials_f, NULL);
	Cmd_AddCommand("tutoriallist_click", MN_TutorialListClick_f, NULL);
	Cmd_AddCommand("chatlist", CL_ShowChatMessagesOnStack_f, "Print all chat messages to the game console");
	Cmd_AddCommand("messagelist", CL_ShowMessagesOnStack_f, "Print all messages to the game console");
	Cmd_AddCommand("mn_startserver", MN_StartServer_f, NULL);
	Cmd_AddCommand("mn_updategametype", MN_UpdateGametype_f, "Update the menu values with current gametype values");
	Cmd_AddCommand("mn_nextgametype", MN_ChangeGametype_f, "Switch to the next multiplayer game type");
	Cmd_AddCommand("mn_prevgametype", MN_ChangeGametype_f, "Switch to the previous multiplayer game type");
	Cmd_AddCommand("mn_getmaps", MN_GetMaps_f, "The initial map to show");
	Cmd_AddCommand("mn_nextmap", MN_NextMap_f, "Switch to the next multiplayer map");
	Cmd_AddCommand("mn_prevmap", MN_PrevMap_f, "Switch to the previous multiplayer map");
	Cmd_AddCommand("mn_push", MN_PushMenu_f, "Push a menu to the menustack");
	Cmd_AddParamCompleteFunction("mn_push", MN_CompletePushMenu);
	Cmd_AddCommand("mn_push_copy", MN_PushCopyMenu_f, NULL);
	Cmd_AddCommand("mn_pop", MN_PopMenu_f, "Pops the current menu from the stack");
	Cmd_AddCommand("mn_reinit", MN_ReinitCurrentMenu_f, "This will reinit the current menu (recall the init function)");
	Cmd_AddCommand("mn_modify", MN_Modify_f, NULL);
	Cmd_AddCommand("mn_modifywrap", MN_ModifyWrap_f, NULL);
	Cmd_AddCommand("mn_modifystring", MN_ModifyString_f, NULL);
	Cmd_AddCommand("mn_translate", MN_Translate_f, NULL);
	Cmd_AddCommand("menumodelslist", MN_ListMenuModels_f, NULL);
#ifdef DEBUG
	Cmd_AddCommand("debug_menuprint", MN_PrintMenu_f, "Shows the current menu as text on the game console");
	Cmd_AddCommand("debug_menueditnode", MN_EditMenuNode_f, "This function is for inline editing of nodes - dangerous!!");
	Cmd_AddCommand("debug_menureload", MN_ReloadMenus_f, "Reloads the menus to show updates without the need to restart");
	Cmd_AddCommand("debug_mnscale", MN_SetModelTransform_f, "Transform model from command line.");
	Cmd_AddCommand("debug_mnangles", MN_SetModelTransform_f, "Transform model from command line.");
#endif
	Cmd_AddCommand("hidehud", MN_PushNoHud_f, _("Hide the HUD (press ESC to reactivate HUD)"));
	/* 256kb - FIXME: Get rid of adata, curadata and adataize */
	adata = (byte*)Mem_PoolAlloc(MENU_HUNK_SIZE, cl_menuSysPool, CL_TAG_MENU);
	adataize = MENU_HUNK_SIZE;
	curadata = adata;

	/* reset menu texts and lists */
	for (i = 0; i < MAX_MENUTEXTS; i++) {
		menuText[i] = NULL;
		menuTextLinkedList[i] = NULL;
	}

	/* reset ufopedia, basemanagement and other subsystems */
	UP_ResetUfopedia();
	B_ResetBaseManagement();
	RS_ResetResearch();
	PR_ResetProduction();
	E_Reset();
	HOS_Reset();
	AC_Reset();
	MAP_ResetAction();
	UFO_Reset();
	TR_Reset();
	BaseSummary_Reset();
}

/**
 * @brief Reset and free the menu data hunk
 * @note Even called in case of an error when CL_Shutdown was called - maybe even
 * before CL_InitLocal (and thus MN_ResetMenus) was called
 * @sa CL_Shutdown
 * @sa MN_ResetMenus
 */
void MN_Shutdown (void)
{
	if (adataize)
		Mem_Free(adata);
	adata = NULL;
	adataize = 0;
}

/*
==============================================================
MENU PARSING
==============================================================
*/

/**
 * @brief
 * @sa MN_ExecuteActions
 */
static qboolean MN_ParseAction (menuNode_t * menuNode, menuAction_t * action, const char **text, const const char **token)
{
	const char *errhead = "MN_ParseAction: unexpected end of file (in event)";
	menuAction_t *lastAction;
	menuNode_t *node;
	qboolean found;
	const value_t *val;
	int i;

	lastAction = NULL;

	do {
		/* get new token */
		*token = COM_EParse(text, errhead, NULL);
		if (!*token)
			return qfalse;

		/* get actions */
		do {
			found = qfalse;

			/* standard function execution */
			for (i = 0; i < EA_CALL; i++)
				if (!Q_stricmp(*token, ea_strings[i])) {
/*					Com_Printf("   %s", *token); */

					/* add the action */
					if (lastAction) {
						if (numActions >= MAX_MENUACTIONS)
							Sys_Error("MN_ParseAction: MAX_MENUACTIONS exceeded (%i)\n", numActions);
						action = &menuActions[numActions++];
						memset(action, 0, sizeof(menuAction_t));
						lastAction->next = action;
					}
					action->type = i;

					if (ea_values[i] != V_NULL) {
						/* get parameter values */
						*token = COM_EParse(text, errhead, NULL);
						if (!*text)
							return qfalse;

/*						Com_Printf(" %s", *token); */

						/* get the value */
						action->data = curadata;
						curadata += Com_ParseValue(curadata, *token, ea_values[i], 0, 0);
					}

/*					Com_Printf("\n"); */

					/* get next token */
					*token = COM_EParse(text, errhead, NULL);
					if (!*text)
						return qfalse;

					lastAction = action;
					found = qtrue;
					break;
				}

			/* node property setting */
			if (**token == '*') {
/*				Com_Printf("   %s", *token); */

				/* add the action */
				if (lastAction) {
					if (numActions >= MAX_MENUACTIONS)
						Sys_Error("MN_ParseAction: MAX_MENUACTIONS exceeded (%i)\n", numActions);
					action = &menuActions[numActions++];
					memset(action, 0, sizeof(menuAction_t));
					lastAction->next = action;
				}
				action->type = EA_NODE;

				/* get the node name */
				action->data = curadata;

				strcpy((char *) curadata, &(*token)[1]);
				curadata += ALIGN(strlen((char *) curadata) + 1);

				/* get the node property */
				*token = COM_EParse(text, errhead, NULL);
				if (!*text)
					return qfalse;

/*				Com_Printf(" %s", *token); */

				for (val = nps, i = 0; val->type; val++, i++)
					if (!Q_stricmp(*token, val->string)) {
						*(int *) curadata = i;
						break;
					}

				if (!val->type) {
					Com_Printf("MN_ParseAction: token \"%s\" isn't a node property (in event)\n", *token);
					curadata = action->data;
					if (lastAction) {
						lastAction->next = NULL;
						numActions--;
					}
					break;
				}

				/* get the value */
				*token = COM_EParse(text, errhead, NULL);
				if (!*text)
					return qfalse;

/*				Com_Printf(" %s\n", *token); */

				curadata += ALIGN(sizeof(int));
				curadata += Com_ParseValue(curadata, *token, val->type, 0, val->size);

				/* get next token */
				*token = COM_EParse(text, errhead, NULL);
				if (!*text)
					return qfalse;

				lastAction = action;
				found = qtrue;
			}

			/* function calls */
			for (node = menus[numMenus - 1].firstNode; node; node = node->next)
				if ((node->type == MN_FUNC || node->type == MN_CONFUNC || node->type == MN_CVARFUNC)
					&& !Q_strncmp(node->name, *token, sizeof(node->name))) {
/*					Com_Printf("   %s\n", node->name); */

					/* add the action */
					if (lastAction) {
						if (numActions >= MAX_MENUACTIONS)
							Sys_Error("MN_ParseAction: MAX_MENUACTIONS exceeded (%i)\n", numActions);
						action = &menuActions[numActions++];
						memset(action, 0, sizeof(menuAction_t));
						lastAction->next = action;
					}
					action->type = EA_CALL;

					action->data = curadata;
					*(menuAction_t ***) curadata = &node->click;
					curadata += ALIGN(sizeof(menuAction_t *));

					/* get next token */
					*token = COM_EParse(text, errhead, NULL);
					if (!*text)
						return qfalse;

					lastAction = action;
					found = qtrue;
					break;
				}
		} while (found);

		/* test for end or unknown token */
		if (**token == '}') {
			/* finished */
			return qtrue;
		} else {
			if (!Q_strcmp(*token, "timeout")) {
				/* get new token */
				*token = COM_EParse(text, errhead, NULL);
				if (!*token || **token == '}') {
					Com_Printf("MN_ParseAction: timeout with no value (in event) (node: %s)\n", menuNode->name);
					return qfalse;
				}
				menuNode->timeOut = atoi(*token);
			} else {
				/* unknown token, print message and continue */
				Com_Printf("MN_ParseAction: unknown token \"%s\" ignored (in event) (node: %s, menu %s)\n", *token, menuNode->name, menuNode->menu->name);
			}
		}
	} while (*text);

	return qfalse;
}

/**
 * @sa MN_ParseMenuBody
 */
static qboolean MN_ParseNodeBody (menuNode_t * node, const char **text, const char **token)
{
	const char *errhead = "MN_ParseNodeBody: unexpected end of file (node";
	qboolean found;
	const value_t *val;
	int i;

	/* functions are a special case */
	if (node->type == MN_CONFUNC || node->type == MN_FUNC || node->type == MN_CVARFUNC) {
		menuAction_t **action;

		/* add new actions to end of list */
		action = &node->click;
		for (; *action; action = &(*action)->next);

		if (numActions >= MAX_MENUACTIONS)
			Sys_Error("MN_ParseNodeBody: MAX_MENUACTIONS exceeded (%i)\n", numActions);
		*action = &menuActions[numActions++];
		memset(*action, 0, sizeof(menuAction_t));

		if (node->type == MN_CONFUNC) {
			/* don't add a callback twice */
			if (!Cmd_Exists(node->name))
				Cmd_AddCommand(node->name, MN_Command_f, "Confunc callback");
			else
				Com_DPrintf(DEBUG_CLIENT, "MN_ParseNodeBody: skip confunc '%s' - already added (menu %s)\n", node->name, node->menu->name);
		}

		return MN_ParseAction(node, *action, text, token);
	}

	do {
		/* get new token */
		*token = COM_EParse(text, errhead, node->name);
		if (!*text)
			return qfalse;

		/* get properties, events and actions */
		do {
			found = qfalse;

			for (val = nps; val->type; val++)
				if (!Q_strcmp(*token, val->string)) {
/*					Com_Printf("  %s", *token); */

					if (val->type != V_NULL) {
						/* get parameter values */
						*token = COM_EParse(text, errhead, node->name);
						if (!*text)
							return qfalse;

/*						Com_Printf(" %s", *token); */

						/* get the value */
						/* 0, -1, -2, -3, -4, -5 fills the data array in menuNode_t */
						if ((val->ofs > 0) && (val->ofs < (size_t)-5)) {
							if (Com_ParseValue(node, *token, val->type, val->ofs, val->size) == -1)
								Com_Printf("MN_ParseNodeBody: Wrong size for value %s\n", val->string);
						} else {
							/* a reference to data is handled like this */
/* 							Com_Printf("%i %s\n", val->ofs, *token); */
							node->data[-((int)val->ofs)] = curadata;
							/* references are parsed as string */
							if (**token == '*')
								curadata += Com_ParseValue(curadata, *token, V_STRING, 0, 0);
							else
								curadata += Com_ParseValue(curadata, *token, val->type, 0, val->size);
						}
					}

/*					Com_Printf("\n"); */

					/* get next token */
					*token = COM_EParse(text, errhead, node->name);
					if (!*text)
						return qfalse;

					found = qtrue;
					break;
				}

			for (i = 0; i < NE_NUM_NODEEVENT; i++)
				if (!Q_strcmp(*token, ne_strings[i])) {
					menuAction_t **action;

/*					Com_Printf("  %s\n", *token); */

					/* add new actions to end of list */
					action = (menuAction_t **) ((byte *) node + ne_values[i]);
					for (; *action; action = &(*action)->next);

					if (numActions >= MAX_MENUACTIONS)
						Sys_Error("MN_ParseNodeBody: MAX_MENUACTIONS exceeded (%i)\n", numActions);
					*action = &menuActions[numActions++];
					memset(*action, 0, sizeof(menuAction_t));

					/* get the action body */
					*token = COM_EParse(text, errhead, node->name);
					if (!*text)
						return qfalse;

					if (**token == '{') {
						MN_ParseAction(node, *action, text, token);

						/* get next token */
						*token = COM_EParse(text, errhead, node->name);
						if (!*text)
							return qfalse;
					}

					found = qtrue;
					break;
				}
		} while (found);

		/* test for end or unknown token */
		if (**token == '}') {
			/* finished */
			return qtrue;
		} else if (!Q_strncmp(*token, "excluderect", 12)) {
			/* get parameters */
			*token = COM_EParse(text, errhead, node->name);
			if (!*text)
				return qfalse;
			if (**token != '{') {
				Com_Printf("MN_ParseNodeBody: node with bad excluderect ignored (node \"%s\", menu %s)\n", node->name, node->menu->name);
				continue;
			}

			do {
				*token = COM_EParse(text, errhead, node->name);
				if (!*text)
					return qfalse;
				if (!Q_strcmp(*token, "pos")) {
					*token = COM_EParse(text, errhead, node->name);
					if (!*text)
						return qfalse;
					Com_ParseValue(&node->exclude[node->excludeNum], *token, V_POS, offsetof(excludeRect_t, pos), sizeof(vec2_t));
				} else if (!Q_strcmp(*token, "size")) {
					*token = COM_EParse(text, errhead, node->name);
					if (!*text)
						return qfalse;
					Com_ParseValue(&node->exclude[node->excludeNum], *token, V_POS, offsetof(excludeRect_t, size), sizeof(vec2_t));
				}
			} while (**token != '}');
			if (node->excludeNum < MAX_EXLUDERECTS - 1)
				node->excludeNum++;
			else
				Com_Printf("MN_ParseNodeBody: exluderect limit exceeded (max: %i)\n", MAX_EXLUDERECTS);
		/* for MN_SELECTBOX */
		} else if (!Q_strncmp(*token, "option", 7)) {
			/* get parameters */
			*token = COM_EParse(text, errhead, node->name);
			if (!*text)
				return qfalse;
			Q_strncpyz(menuSelectBoxes[numSelectBoxes].id, *token, sizeof(menuSelectBoxes[numSelectBoxes].id));
			Com_DPrintf(DEBUG_CLIENT, "...found selectbox: '%s'\n", *token);

			*token = COM_EParse(text, errhead, node->name);
			if (!*text)
				return qfalse;
			if (**token != '{') {
				Com_Printf("MN_ParseNodeBody: node with bad option definition ignored (node \"%s\", menu %s)\n", node->name, node->menu->name);
				continue;
			}

			if (numSelectBoxes >= MAX_SELECT_BOX_OPTIONS) {
				FS_SkipBlock(text);
				Com_Printf("MN_ParseNodeBody: Too many option entries for node %s (menu %s)\n", node->name, node->menu->name);
				return qfalse;
			}

			/**
			 * the options data can be defined like this
			 * @code
			 * option string {
			 *  value "value"
			 *  action "command string"
			 * }
			 * @endcode
			 * The strings will appear in the drop down list of the select box
			 * if you select the 'string', the 'cvarname' will be set to 'value'
			 * and if action is defined (which is a console/script command string)
			 * this one is executed on each selection
			 */

			do {
				*token = COM_EParse(text, errhead, node->name);
				if (!*text)
					return qfalse;
				if (**token == '}')
					break;
				for (val = selectBoxValues; val->string; val++)
					if (!Q_strncmp(*token, val->string, sizeof(val->string))) {
						/* get parameter values */
						*token = COM_EParse(text, errhead, node->name);
						if (!*text)
							return qfalse;
						Com_ParseValue(&menuSelectBoxes[numSelectBoxes], *token, val->type, val->ofs, val->size);
						break;
					}
				if (!val->string)
					Com_Printf("MN_ParseNodeBody: unknown options value: '%s' - ignore it\n", *token);
			} while (**token != '}');
			MN_AddSelectboxOption(node);
		} else {
			/* unknown token, print message and continue */
			Com_Printf("MN_ParseNodeBody: unknown token \"%s\" ignored (node \"%s\", menu %s)\n", *token, node->name, node->menu->name);
		}
	} while (*text);

	return qfalse;
}

/**
 * @sa MN_ParseNodeBody
 */
static qboolean MN_ParseMenuBody (menu_t * menu, const char **text)
{
	const char *errhead = "MN_ParseMenuBody: unexpected end of file (menu";
	const char *token;
	qboolean found;
	menuNode_t *node, *lastNode, *iNode;
	int i;

	lastNode = NULL;

	/* if inheriting another menu, link in the super menu's nodes */
	for (node = menu->firstNode; node; node = node->next) {
		if (numNodes >= MAX_MENUNODES)
			Sys_Error("MAX_MENUNODES exceeded\n");
		iNode = &menuNodes[numNodes++];
		*iNode = *node;
		/* link it in */
		if (lastNode)
			lastNode->next = iNode;
		else
			menu->firstNode = iNode;
		lastNode = iNode;
	}

	lastNode = NULL;

	do {
		/* get new token */
		token = COM_EParse(text, errhead, menu->name);
		if (!*text)
			return qfalse;

		/* get node type */
		do {
			found = qfalse;

			for (i = 0; i < MN_NUM_NODETYPE; i++)
				if (!Q_strcmp(token, nt_strings[i])) {
					/* found node */
					/* get name */
					token = COM_EParse(text, errhead, menu->name);
					if (!*text)
						return qfalse;

					/* test if node already exists */
					for (node = menu->firstNode; node; node = node->next) {
						if (!Q_strncmp(token, node->name, sizeof(node->name))) {
							if (node->type != i)
								Com_Printf("MN_ParseMenuBody: node prototype type change (menu \"%s\")\n", menu->name);
							Com_DPrintf(DEBUG_CLIENT, "... over-riding node %s in menu %s\n", node->name, menu->name);
							/* reset action list of node */
							node->click = NULL;
							break;
						}
						lastNode = node;
					}

					/* initialize node */
					if (!node) {
						if (numNodes >= MAX_MENUNODES)
							Sys_Error("MAX_MENUNODES exceeded\n");
						node = &menuNodes[numNodes++];
						memset(node, 0, sizeof(menuNode_t));
						node->menu = menu;
						Q_strncpyz(node->name, token, sizeof(node->name));

						/* link it in */
						if (lastNode)
							lastNode->next = node;
						else
							menu->firstNode = node;

						lastNode = node;
					}

					node->type = i;
					/* node default values */
					node->padding = 3;

/*					Com_Printf(" %s %s\n", nt_strings[i], *token); */

					/* check for special nodes */
					switch (node->type) {
					case MN_FUNC:
						if (!Q_strncmp(node->name, "init", 4)) {
							if (!menu->initNode)
								menu->initNode = node;
							else
								Com_Printf("MN_ParseMenuBody: second init function ignored (menu \"%s\")\n", menu->name);
						} else if (!Q_strncmp(node->name, "close", 5)) {
							if (!menu->closeNode)
								menu->closeNode = node;
							else
								Com_Printf("MN_ParseMenuBody: second close function ignored (menu \"%s\")\n", menu->name);
						} else if (!Q_strncmp(node->name, "event", 5)) {
							if (!menu->eventNode) {
								menu->eventNode = node;
								menu->eventNode->timeOut = 2000; /* default value */
							} else
								Com_Printf("MN_ParseMenuBody: second event function ignored (menu \"%s\")\n", menu->name);
						} else if (!Q_strncmp(node->name, "leave", 5)) {
							if (!menu->leaveNode) {
								menu->leaveNode = node;
							} else
								Com_Printf("MN_ParseMenuBody: second leave function ignored (menu \"%s\")\n", menu->name);
						}
						break;
					case MN_ZONE:
						if (!Q_strncmp(node->name, "render", 6)) {
							if (!menu->renderNode)
								menu->renderNode = node;
							else
								Com_Printf("MN_ParseMenuBody: second render node ignored (menu \"%s\")\n", menu->name);
						} else if (!Q_strncmp(node->name, "popup", 5)) {
							if (!menu->popupNode)
								menu->popupNode = node;
							else
								Com_Printf("MN_ParseMenuBody: second popup node ignored (menu \"%s\")\n", menu->name);
						}
						break;
					case MN_CONTAINER:
						node->mousefx = C_UNDEFINED;
						break;
					}

					/* set standard texture coordinates */
/*					node->texl[0] = 0; node->texl[1] = 0; */
/*					node->texh[0] = 1; node->texh[1] = 1; */

					/* get parameters */
					token = COM_EParse(text, errhead, menu->name);
					if (!*text)
						return qfalse;

					if (*token == '{') {
						if (!MN_ParseNodeBody(node, text, &token)) {
							Com_Printf("MN_ParseMenuBody: node with bad body ignored (menu \"%s\")\n", menu->name);
							numNodes--;
							continue;
						}

						token = COM_EParse(text, errhead, menu->name);
						if (!*text)
							return qfalse;
					}

					/* set standard color */
					if (!node->color[3])
						node->color[0] = node->color[1] = node->color[2] = node->color[3] = 1.0f;

					found = qtrue;
					break;
				}
		} while (found);

		/* test for end or unknown token */
		if (*token == '}') {
			/* finished */
			return qtrue;
		} else {
			/* unknown token, print message and continue */
			Com_Printf("MN_ParseMenuBody: unknown token \"%s\" ignored (menu \"%s\")\n", token, menu->name);
		}

	} while (*text);

	return qfalse;
}

/**
 * @brief Add a menu link to menumodel definition for faster access
 * @note Called after all menus are parsed of course
 */
void MN_LinkMenuModels (void)
{
	int i, j;
	for (i = 0; i < numMenuModels; i++) {
		for (j = 0; j < menuModels[i].menuTransformCnt; j++) {
			menuModels[i].menuTransform[j].menuPtr = MN_GetMenu(menuModels[i].menuTransform[j].menuID);
			if (menuModels[i].menuTransform[j].menuPtr == NULL)
				Com_Printf("Could not find menu '%s' as requested by menumodel '%s'", menuModels[i].menuTransform[j].menuID, menuModels[i].id);
		}
	}
}

/**
 * @brief parses the models.ufo and all files where menu_models are defined
 * @sa CL_ParseClientData
 */
void MN_ParseMenuModel (const char *name, const char **text)
{
	menuModel_t *menuModel;
	const char *token;
	int i;
	const value_t *v = NULL;
	const char *errhead = "MN_ParseMenuModel: unexpected end of file (names ";

	/* search for menumodels with same name */
	for (i = 0; i < numMenuModels; i++)
		if (!Q_strcmp(menuModels[i].id, name)) {
			Com_Printf("MN_ParseMenuModel: menu_model \"%s\" with same name found, second ignored\n", name);
			return;
		}

	if (numMenuModels >= MAX_MENUMODELS) {
		Com_Printf("MN_ParseMenuModel: Max menu models reached\n");
		return;
	}

	/* initialize the menu */
	menuModel = &menuModels[numMenuModels];
	memset(menuModel, 0, sizeof(menuModel_t));

	Vector4Set(menuModel->color, 0.5, 0.5, 0.5, 1.0);

	menuModel->id = Mem_PoolStrDup(name, cl_menuSysPool, CL_TAG_MENU);
	Com_DPrintf(DEBUG_CLIENT, "Found menu model %s (%i)\n", menuModel->id, numMenuModels);

	/* get it's body */
	token = COM_Parse(text);

	if (!*text || *token != '{') {
		Com_Printf("MN_ParseMenuModel: menu \"%s\" without body ignored\n", menuModel->id);
		return;
	}

	numMenuModels++;

	do {
		/* get the name type */
		token = COM_EParse(text, errhead, name);
		if (!*text)
			break;
		if (*token == '}')
			break;

		for (v = menuModelValues; v->string; v++)
			if (!Q_strncmp(token, v->string, sizeof(v->string))) {
				/* found a definition */
				if (!Q_strncmp(v->string, "need", 4)) {
					token = COM_EParse(text, errhead, name);
					if (!*text)
						return;
					menuModel->next = MN_GetMenuModel(token);
					if (!menuModel->next)
						Com_Printf("Could not find menumodel %s", token);
					menuModel->need = Mem_PoolStrDup(token, cl_menuSysPool, CL_TAG_MENU);
				} else if (!Q_strncmp(v->string, "menutransform", 13)) {
					token = COM_EParse(text, errhead, name);
					if (!*text)
						return;
					if (*token != '{') {
						Com_Printf("Error in menumodel '%s' menutransform definition\n", name);
						break;
					}
					do {
						token = COM_EParse(text, errhead, name);
						if (!*text)
							return;
						if (*token == '}')
							break;
						menuModel->menuTransform[menuModel->menuTransformCnt].menuID = Mem_PoolStrDup(token, cl_menuSysPool, CL_TAG_MENU);

						token = COM_EParse(text, errhead, name);
						if (!*text)
							return;
						if (*token == '}') {
							Com_Printf("Error in menumodel '%s' menutransform definition - missing scale float value\n", name);
							break;
						}
						if (*token == '#') {
							menuModel->menuTransform[menuModel->menuTransformCnt].useScale = qfalse;
						} else {
							Com_ParseValue(&menuModel->menuTransform[menuModel->menuTransformCnt].scale, token, V_VECTOR, 0, sizeof(vec3_t));
							menuModel->menuTransform[menuModel->menuTransformCnt].useScale = qtrue;
						}

						token = COM_EParse(text, errhead, name);
						if (!*text)
							return;
						if (*token == '}') {
							Com_Printf("Error in menumodel '%s' menutransform definition - missing angles float value\n", name);
							break;
						}
						if (*token == '#') {
							menuModel->menuTransform[menuModel->menuTransformCnt].useAngles = qfalse;
						} else {
							Com_ParseValue(&menuModel->menuTransform[menuModel->menuTransformCnt].angles, token, V_VECTOR, 0, sizeof(vec3_t));
							menuModel->menuTransform[menuModel->menuTransformCnt].useAngles = qtrue;
						}

						token = COM_EParse(text, errhead, name);
						if (!*text)
							return;
						if (*token == '}') {
							Com_Printf("Error in menumodel '%s' menutransform definition - missing origin float value\n", name);
							break;
						}
						if (*token == '#') {
							menuModel->menuTransform[menuModel->menuTransformCnt].useOrigin = qfalse;
						} else {
							Com_ParseValue(&menuModel->menuTransform[menuModel->menuTransformCnt].origin, token, V_VECTOR, 0, sizeof(vec3_t));
							menuModel->menuTransform[menuModel->menuTransformCnt].useOrigin = qtrue;
						}

						menuModel->menuTransformCnt++;
					} while (*token != '}'); /* dummy condition - break is earlier here */
				} else {
					token = COM_EParse(text, errhead, name);
					if (!*text)
						return;

					switch (v->type) {
					case V_CLIENT_HUNK_STRING:
						Mem_PoolStrDupTo(token, (char**) ((char*)menuModel + (int)v->ofs), cl_menuSysPool, CL_TAG_MENU);
						break;
					default:
						Com_ParseValue(menuModel, token, v->type, v->ofs, v->size);
					}
				}
				break;
			}

		if (!v->string)
			Com_Printf("MN_ParseMenuModel: unknown token \"%s\" ignored (menu_model %s)\n", token, name);

	} while (*text);
}

/**
 * @sa CL_ParseClientData
 */
void MN_ParseMenu (const char *name, const char **text)
{
	menu_t *menu;
	menuNode_t *node;
	const char *token;
	int i;

	/* search for menus with same name */
	for (i = 0; i < numMenus; i++)
		if (!Q_strncmp(name, menus[i].name, MAX_VAR))
			break;

	if (i < numMenus) {
		Com_Printf("MN_ParseMenus: menu \"%s\" with same name found, second ignored\n", name);
		return;
	}

	if (numMenus >= MAX_MENUS) {
		Sys_Error("MN_ParseMenu: max menus exceeded (%i) - ignore '%s'\n", MAX_MENUS, name);
		return;	/* never reached */
	}

	/* initialize the menu */
	menu = &menus[numMenus++];
	memset(menu, 0, sizeof(menu_t));

	Q_strncpyz(menu->name, name, sizeof(menu->name));

	/* get it's body */
	token = COM_Parse(text);

	/* does this menu inherit data from another menu? */
	if (!Q_strncmp(token, "extends", 7)) {
		menu_t *superMenu;
		token = COM_Parse(text);
		Com_DPrintf(DEBUG_CLIENT, "MN_ParseMenus: menu \"%s\" inheriting menu \"%s\"\n", name, token);
		superMenu = MN_GetMenu(token);
		if (!superMenu)
			Sys_Error("MN_ParseMenu: menu '%s' can't inherit from menu '%s' - because '%s' was not found\n", name, token, token);
		memcpy(menu, superMenu, sizeof(menu_t));
		Q_strncpyz(menu->name, name, sizeof(menu->name));
		token = COM_Parse(text);
	}

	if (!*text || *token != '{') {
		Com_Printf("MN_ParseMenus: menu \"%s\" without body ignored\n", menu->name);
		numMenus--;
		return;
	}

	/* parse it's body */
	if (!MN_ParseMenuBody(menu, text)) {
		Com_Printf("MN_ParseMenus: menu \"%s\" with bad body ignored\n", menu->name);
		numMenus--;
		return;
	}

	for (node = menu->firstNode; node; node = node->next)
		if (node->num >= MAX_MENUTEXTS)
			Sys_Error("Error in menu %s - max menu num exeeded (num: %i, max: %i) in node '%s'", menu->name, node->num, MAX_MENUTEXTS, node->name);

}

/**
 * @brief Prints the map info
 */
static void MN_MapInfo (int step)
{
	int i = 0;
	mapDef_t *md;

	if (!csi.numMDs)
		return;

	ccs.multiplayerMapDefinitionIndex += step;

	if (ccs.multiplayerMapDefinitionIndex < 0)
		ccs.multiplayerMapDefinitionIndex = csi.numMDs - 1;

	ccs.multiplayerMapDefinitionIndex %= csi.numMDs;

	if (!ccs.singleplayer) {
		while (!csi.mds[ccs.multiplayerMapDefinitionIndex].multiplayer) {
			i++;
			ccs.multiplayerMapDefinitionIndex += (step ? step : 1);
			if (ccs.multiplayerMapDefinitionIndex < 0)
				ccs.multiplayerMapDefinitionIndex = csi.numMDs - 1;
			ccs.multiplayerMapDefinitionIndex %= csi.numMDs;
			if (i >= csi.numMDs)
				Sys_Error("MN_MapInfo: There is no multiplayer map in any mapdef\n");
		}
	}

	md = &csi.mds[ccs.multiplayerMapDefinitionIndex];

	Cvar_Set("mn_svmapname", md->map);
	if (FS_CheckFile(va("pics/maps/shots/%s.jpg", md->map)) != -1)
		Cvar_Set("mn_mappic", va("maps/shots/%s.jpg", md->map));
	else
		Cvar_Set("mn_mappic", va("maps/shots/na.jpg"));

	if (FS_CheckFile(va("pics/maps/shots/%s_2.jpg", md->map)) != -1)
		Cvar_Set("mn_mappic2", va("maps/shots/%s_2.jpg", md->map));
	else
		Cvar_Set("mn_mappic2", va("maps/shots/na.jpg"));

	if (FS_CheckFile(va("pics/maps/shots/%s_3.jpg", md->map)) != -1)
		Cvar_Set("mn_mappic3", va("maps/shots/%s_3.jpg", md->map));
	else
		Cvar_Set("mn_mappic3", va("maps/shots/na.jpg"));

	if (md->gameTypes) {
		linkedList_t *list = md->gameTypes;
		char buf[256] = "";
		while (list) {
			Q_strcat(buf, va("%s ", (const char *)list->data), sizeof(buf));
			list = list->next;
		}
		Cvar_Set("mn_mapgametypes", buf);
		list = md->gameTypes;
		while (list) {
			/* check whether current selected gametype is a valid one */
			if (!Q_strcmp((const char*)list->data, gametype->string)) {
				break;
			}
			list = list->next;
		}
		if (!list)
			MN_ChangeGametype_f();
	} else {
		Cvar_Set("mn_mapgametypes", _("all"));
	}
}

static void MN_GetMaps_f (void)
{
	MN_MapInfo(0);
}

static void MN_NextMap_f (void)
{
	MN_MapInfo(1);
}

static void MN_PrevMap_f (void)
{
	MN_MapInfo(-1);
}

/**
 * @brief Script command to show all messages on the stack
 */
static void CL_ShowMessagesOnStack_f (void)
{
	message_t *m = messageStack;

	while (m) {
		Com_Printf("%s: %s\n", m->title, m->text);
		m = m->next;
	}
}

/**
 * @brief Adds a new message to message stack
 * @note These are the messages that are displayed at geoscape
 * @param[in] title Already translated message/mail title
 * @param[in] text Already translated message/mail body
 * @param[in] popup Show this as a popup, too?
 * @param[in] type The message type
 * @param[in] pedia Pointer to technology (only if needed)
 * @return message_t pointer
 * @sa UP_OpenMail_f
 * @sa CL_EventAddMail_f
 */
message_t *MN_AddNewMessage (const char *title, const char *text, qboolean popup, messagetype_t type, technology_t *pedia)
{
	message_t *mess;

	assert(type < MSG_MAX);

	/* allocate memory for new message - delete this with every new game */
	mess = (message_t *) Mem_PoolAlloc(sizeof(message_t), cl_localPool, CL_TAG_NONE);

	/* push the new message at the beginning of the stack */
	mess->next = messageStack;
	messageStack = mess;

	mess->type = type;
	mess->pedia = pedia;		/* pointer to ufopedia */

	CL_DateConvert(&ccs.date, &mess->d, &mess->m);
	mess->y = ccs.date.day / 365;
	mess->h = ccs.date.sec / 3600;
	mess->min = ((ccs.date.sec % 3600) / 60);
	mess->s = (ccs.date.sec % 3600) / 60 / 60;

	Q_strncpyz(mess->title, title, sizeof(mess->title));
	mess->text = Mem_PoolStrDup(text, cl_localPool, CL_TAG_NONE);

	/* they need to be translated already */
	if (popup)
		MN_Popup(mess->title, mess->text);

	switch (type) {
	case MSG_DEBUG:
	case MSG_INFO:
	case MSG_STANDARD:
	case MSG_TRANSFERFINISHED:
	case MSG_PROMOTION:
	case MSG_DEATH:
		break;
	case MSG_RESEARCH_PROPOSAL:
	case MSG_RESEARCH_FINISHED:
		assert(pedia);
	case MSG_EVENT:
	case MSG_NEWS:
		/* reread the new mails in UP_GetUnreadMails */
		gd.numUnreadMails = -1;
		/*S_StartLocalSound("misc/mail");*/
		break;
	case MSG_UFOSPOTTED:
	case MSG_TERRORSITE:
	case MSG_BASEATTACK:
	case MSG_CRASHSITE:
		S_StartLocalSound("misc/newmission");
		break;
	case MSG_CONSTRUCTION:
		break;
	case MSG_PRODUCTION:
		break;
	case MSG_MAX:
		break;
	}

	return mess;
}

/**
 * @brief Returns formatted text of a message timestamp
 * @param[in] text Buffer to hold the final result
 * @param[in] message The message to convert into text
 */
static void MN_TimestampedText (char *text, message_t *message, size_t textsize)
{
	Com_sprintf(text, textsize, _("%i %s %02i, %02i:%02i: "), message->y, CL_DateGetMonthName(message->m), message->d, message->h, message->min);
}

void MN_RemoveMessage (char *title)
{
	message_t *m = messageStack;
	message_t *prev = NULL;

	while (m) {
		if (!Q_strncmp(m->title, title, MAX_VAR)) {
			if (prev)
				prev->next = m->next;
			Mem_Free(m->text);
			Mem_Free(m);
			return;
		}
		prev = m;
		m = m->next;
	}
	Com_Printf("Could not remove message from stack - %s was not found\n", title);
}

/**< @brief buffer that hold all printed chat messages for menu display */
static char *chatBuffer = NULL;
static menuNode_t* chatBufferNode = NULL;

/**
 * @brief Displays a chat on the hud and add it to the chat buffer
 */
void MN_AddChatMessage (const char *text)
{
	/* allocate memory for new chat message */
	chatMessage_t *chat = (chatMessage_t *) Mem_PoolAlloc(sizeof(chatMessage_t), cl_genericPool, CL_TAG_NONE);

	/* push the new chat message at the beginning of the stack */
	chat->next = chatMessageStack;
	chatMessageStack = chat;
	chat->text = Mem_PoolStrDup(text, cl_genericPool, CL_TAG_NONE);

	if (!chatBuffer) {
		chatBuffer = (char*)Mem_PoolAlloc(sizeof(char) * MAX_MESSAGE_TEXT, cl_genericPool, CL_TAG_NONE);
		if (!chatBuffer) {
			Com_Printf("Could not allocate chat buffer\n");
			return;
		}
		/* only link this once */
		menuText[TEXT_CHAT_WINDOW] = chatBuffer;
	}
	if (!chatBufferNode)
		chatBufferNode = MN_GetNodeFromCurrentMenu("chatscreen");

	*chatBuffer = '\0'; /* clear buffer */
	do {
		if (strlen(chatBuffer) + strlen(chat->text) >= MAX_MESSAGE_TEXT)
			break;
		Q_strcat(chatBuffer, chat->text, MAX_MESSAGE_TEXT); /* fill buffer */
		chat = chat->next;
	} while (chat);

	/* maybe the hud doesn't have a chatscreen node - or we don't have a hud */
	if (chatBufferNode) {
		Cmd_ExecuteString("unhide_chatscreen");
		chatBufferNode->menu->eventTime = cls.realtime;
	}
}

/**
 * @brief Script command to show all chat messages on the stack
 */
static void CL_ShowChatMessagesOnStack_f (void)
{
	chatMessage_t *m = chatMessageStack;

	while (m) {
		Com_Printf("%s", m->text);
		m = m->next;
	}
}

/**
 * @brief Displays a message on the hud.
 *
 * @param[in] time is a ms values
 * @param[in] text text is already translated here
 */
void CL_DisplayHudMessage (const char *text, int time)
{
	cl.msgTime = cl.time + time;
	Q_strncpyz(cl.msgText, text, sizeof(cl.msgText));
}

/* ==================== USE_SDL_TTF stuff ===================== */

#define MAX_FONTS 16
static int numFonts = 0;
font_t fonts[MAX_FONTS];

font_t *fontBig;
font_t *fontSmall;

static const value_t fontValues[] = {
	{"font", V_TRANSLATION_MANUAL_STRING, offsetof(font_t, path), 0},
	{"size", V_INT, offsetof(font_t, size), MEMBER_SIZEOF(font_t, size)},
	{"style", V_CLIENT_HUNK_STRING, offsetof(font_t, style), 0},

	{NULL, V_NULL, 0},
};

void CL_GetFontData (const char *name, int *size, const char *path)
{
	int i;

	/* search for font with this name */
	for (i = 0; i < numFonts; i++)
		if (!Q_strncmp(fonts[i].name, name, MAX_VAR)) {
			*size = fonts[i].size;
			path = fonts[i].path;
			return;
		}
	Com_Printf("Font: %s not found\n", name);
}

/**
 * @sa CL_ParseClientData
 */
void CL_ParseFont (const char *name, const char **text)
{
	font_t *font;
	const char *errhead = "CL_ParseFont: unexpected end of file (font";
	const char *token;
	int i;
	const value_t *v = NULL;

	/* search for font with same name */
	for (i = 0; i < numFonts; i++)
		if (!Q_strncmp(fonts[i].name, name, MAX_VAR)) {
			Com_Printf("CL_ParseFont: font \"%s\" with same name found, second ignored\n", name);
			return;
		}

	if (numFonts >= MAX_FONTS) {
		Com_Printf("CL_ParseFont: Max fonts reached\n");
		return;
	}

	/* initialize the menu */
	font = &fonts[numFonts];
	memset(font, 0, sizeof(font_t));

	font->name = Mem_PoolStrDup(name, cl_menuSysPool, CL_TAG_MENU);

	if (!Q_strcmp(font->name, "f_small"))
		fontSmall = font;
	else if (!Q_strcmp(font->name, "f_big"))
		fontBig = font;

	Com_DPrintf(DEBUG_CLIENT, "...found font %s (%i)\n", font->name, numFonts);

	/* get it's body */
	token = COM_Parse(text);

	if (!*text || *token != '{') {
		Com_Printf("CL_ParseFont: font \"%s\" without body ignored\n", name);
		return;
	}

	numFonts++;

	do {
		/* get the name type */
		token = COM_EParse(text, errhead, name);
		if (!*text)
			break;
		if (*token == '}')
			break;

		for (v = fontValues; v->string; v++)
			if (!Q_strncmp(token, v->string, sizeof(v->string))) {
				/* found a definition */
				token = COM_EParse(text, errhead, name);
				if (!*text)
					return;

				switch (v->type) {
				case V_TRANSLATION_MANUAL_STRING:
					token++;
				case V_CLIENT_HUNK_STRING:
					Mem_PoolStrDupTo(token, (char**) ((char*)font + (int)v->ofs), cl_menuSysPool, CL_TAG_MENU);
					break;
				default:
					Com_ParseValue(font, token, v->type, v->ofs, v->size);
					break;
				}
				break;
			}

		if (!v->string)
			Com_Printf("CL_ParseFont: unknown token \"%s\" ignored (font %s)\n", token, name);
	} while (*text);

	if (!font->path)
		Sys_Error("...font without path (font %s)\n", font->name);

	if (FS_CheckFile(font->path) == -1)
		Sys_Error("...font file %s does not exist (font %s)\n", font->path, font->name);

	R_FontRegister(font->name, font->size, _(font->path), font->style);
}

/**
 * @brief after a vid restart we have to reinit the fonts
 */
void CL_InitFonts (void)
{
	int i;

	Com_Printf("...registering %i fonts\n", numFonts);
	for (i = 0; i < numFonts; i++)
		R_FontRegister(fonts[i].name, fonts[i].size, fonts[i].path, fonts[i].style);
}

/* ===================== USE_SDL_TTF stuff end ====================== */

static const value_t tutValues[] = {
	{"name", V_TRANSLATION_STRING, offsetof(tutorial_t, name), 0},
	{"sequence", V_CLIENT_HUNK_STRING, offsetof(tutorial_t, sequence), 0},
	{NULL, 0, 0, 0}
};

/**
 * @sa CL_ParseClientData
 */
void MN_ParseTutorials (const char *name, const char **text)
{
	tutorial_t *t = NULL;
	const char *errhead = "MN_ParseTutorials: unexpected end of file (tutorial ";
	const char *token;
	const value_t *v;

	/* get name list body body */
	token = COM_Parse(text);

	if (!*text || *token != '{') {
		Com_Printf("MN_ParseTutorials: tutorial \"%s\" without body ignored\n", name);
		return;
	}

	/* parse tutorials */
	if (numTutorials >= MAX_TUTORIALS) {
		Com_Printf("Too many tutorials, '%s' ignored.\n", name);
		numTutorials = MAX_TUTORIALS;
		return;
	}

	t = &tutorials[numTutorials++];
	memset(t, 0, sizeof(tutorial_t));
	do {
		/* get the name type */
		token = COM_EParse(text, errhead, name);
		if (!*text)
			break;
		if (*token == '}')
			break;
		for (v = tutValues; v->string; v++)
			if (!Q_strncmp(token, v->string, sizeof(v->string))) {
				/* found a definition */
				token = COM_EParse(text, errhead, name);
				if (!*text)
					return;

				switch (v->type) {
				case V_CLIENT_HUNK_STRING:
					Mem_PoolStrDupTo(token, (char**) ((char*)t + (int)v->ofs), cl_menuSysPool, CL_TAG_MENU);
					break;
				default:
					Com_ParseValue(t, token, v->type, v->ofs, v->size);
				}
				break;
			}
		if (!v->string)
			Com_Printf("MN_ParseTutorials: unknown token \"%s\" ignored (tutorial %s)\n", token, name);
	} while (*text);
}

/**
 * @brief Saved the complete message stack
 * @sa SAV_GameSave
 * @sa MN_AddNewMessage
 */
static void MS_MessageSave (sizebuf_t * sb, message_t * message)
{
	int idx = -1;

	if (!message)
		return;
	/* bottom up */
	MS_MessageSave(sb, message->next);

	/* don't save these message types */
	if (message->type == MSG_INFO)
		return;

	if (message->pedia)
		idx = message->pedia->idx;

	Com_DPrintf(DEBUG_CLIENT, "MS_MessageSave: Save '%s' - '%s'; type = %i; idx = %i\n", message->title, message->text, message->type, idx);
	MSG_WriteString(sb, message->title);
	MSG_WriteString(sb, message->text);
	MSG_WriteByte(sb, message->type);
	/* store script id of event mail */
	if (message->type == MSG_EVENT)
		MSG_WriteString(sb, message->eventMail->id);
	MSG_WriteLong(sb, idx);
	MSG_WriteLong(sb, message->d);
	MSG_WriteLong(sb, message->m);
	MSG_WriteLong(sb, message->y);
	MSG_WriteLong(sb, message->h);
	MSG_WriteLong(sb, message->min);
	MSG_WriteLong(sb, message->s);
}

/**
 * @sa MS_Load
 * @sa MN_AddNewMessage
 * @sa MS_MessageSave
 */
qboolean MS_Save (sizebuf_t* sb, void* data)
{
	int i = 0;
	message_t* message;

	/* store message system items */
	for (message = messageStack; message; message = message->next) {
		if (message->type == MSG_INFO)
			continue;
		i++;
	}
	MSG_WriteLong(sb, i);
	MS_MessageSave(sb, messageStack);
	return qtrue;
}

/**
 * @sa MS_Save
 * @sa MN_AddNewMessage
 */
qboolean MS_Load (sizebuf_t* sb, void* data)
{
	int i, mtype, idx;
	char title[MAX_VAR], text[MAX_MESSAGE_TEXT];
	message_t *mess;
	const char *s = NULL;

	/* how many message items */
	i = MSG_ReadLong(sb);
	for (; i--;) {
		/* can contain high bits due to utf8 */
		Q_strncpyz(title, MSG_ReadStringRaw(sb), sizeof(title));
		Q_strncpyz(text, MSG_ReadStringRaw(sb), sizeof(text));
		mtype = MSG_ReadByte(sb);
		if (mtype == MSG_EVENT)
			s = MSG_ReadString(sb);
		else
			s = NULL;
		idx = MSG_ReadLong(sb);
		if (mtype != MSG_DEBUG || developer->integer == 1) {
			mess = MN_AddNewMessage(title, text, qfalse, mtype, RS_GetTechByIDX(idx));
			if (s) {
				mess->eventMail = CL_GetEventMail(s);
				if (!mess->eventMail) {
					Com_Printf("Could not find eventMail with id: %s\n", s);
					return qfalse;
				}
			}
			mess->d = MSG_ReadLong(sb);
			mess->m = MSG_ReadLong(sb);
			mess->y = MSG_ReadLong(sb);
			mess->h = MSG_ReadLong(sb);
			mess->min = MSG_ReadLong(sb);
			mess->s = MSG_ReadLong(sb);
		} else {
			MSG_ReadLong(sb);
			MSG_ReadLong(sb);
			MSG_ReadLong(sb);
			MSG_ReadLong(sb);
			MSG_ReadLong(sb);
			MSG_ReadLong(sb);
		}
	}
	return qtrue;
}

/**
 * @brief Checks the parsed menus for errors
 * @return false if there are errors - true otherwise
 */
qboolean MN_ScriptSanityCheck (void)
{
	int i, error = 0;
	menuNode_t* node;

	for (i = 0, node = menuNodes; i < numNodes; i++, node++) {
		switch (node->type) {
		case MN_TEXT:
			if (!node->height) {
				Com_Printf("MN_ParseNodeBody: node '%s' (menu: %s) has no height value but is a text node\n", node->name, node->menu->name);
				error++;
			} else if (node->height != (int)(node->size[1] / node->texh[0]) && node->texh[0]) {
				/* if node->texh[0] == 0, the height of the font is used */
				Com_Printf("MN_ParseNodeBody: height value (%i) of node '%s' (menu: %s) differs from size (%.0f) and format (%.0f) values\n",
					node->height, node->name, node->menu->name, node->size[1], node->texh[0]);
				error++;
			}
			break;
		default:
			break;
		}
	}

	if (!error)
		return qtrue;
	else
		return qfalse;
}
