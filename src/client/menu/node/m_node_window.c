/**
 * @file m_node_window.c
 * @note this file is about menu function. Its not yet a real node,
 * but it may become one. Think the code like that will help to merge menu and node.
 * @note It used 'window' instead of 'menu', because a menu is not this king of widget
 * @todo move it as an inheritance of panel bahaviour
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

#include "../m_main.h"
#include "../m_parse.h"
#include "../m_font.h"
#include "../m_nodes.h"
#include "../m_internal.h"
#include "../m_render.h"
#include "m_node_window.h"
#include "m_node_panel.h"
#include "m_node_abstractnode.h"

#include "../../client.h" /* gettext _() */

#define EXTRADATA_TYPE windowExtraData_t
#define EXTRADATA(node) MN_EXTRADATA(node, EXTRADATA_TYPE)
#define EXTRADATACONST(node) MN_EXTRADATACONST(node, EXTRADATA_TYPE)

/* constants defining all tile of the texture */

#define LEFT_WIDTH 20
#define MID_WIDTH 1
#define RIGHT_WIDTH 19

#define TOP_HEIGHT 46
#define MID_HEIGHT 1
#define BOTTOM_HEIGHT 19

#define MARGE 3

static const nodeBehaviour_t const *localBehaviour;

static const int CONTROLS_IMAGE_DIMENSIONS = 17;
static const int CONTROLS_PADDING = 22;
static const int CONTROLS_SPACING = 5;

static const int windowTemplate[] = {
	LEFT_WIDTH, MID_WIDTH, RIGHT_WIDTH,
	TOP_HEIGHT, MID_HEIGHT, BOTTOM_HEIGHT,
	MARGE
};

static const vec4_t modalBackground = {0, 0, 0, 0.6};
static const vec4_t anamorphicBorder = {0, 0, 0, 1};

/**
 * @brief Check if a window is fullscreen or not
 */
qboolean MN_WindowIsFullScreen (const menuNode_t* const node)
{
	assert(MN_NodeInstanceOf(node, "window"));
	return EXTRADATACONST(node).isFullScreen;
}

static void MN_WindowNodeDraw (menuNode_t *node)
{
	const char* image;
	const char* text;
	vec2_t pos;
	const char *font = MN_GetFontFromNode(node);

	MN_GetNodeAbsPos(node, pos);

	/* black border for anamorphic mode */
	/** @todo it should be over the window */
	/** @todo why not using glClear here with glClearColor set to black here? */
	if (MN_WindowIsFullScreen(node)) {
		/* top */
		if (pos[1] != 0)
			MN_DrawFill(0, 0, viddef.virtualWidth, pos[1], anamorphicBorder);
		/* left-right */
		if (pos[0] != 0)
			MN_DrawFill(0, pos[1], pos[0], node->size[1], anamorphicBorder);
		if (pos[0] + node->size[0] < viddef.virtualWidth) {
			const int width = viddef.virtualWidth - (pos[0] + node->size[0]);
			MN_DrawFill(viddef.virtualWidth - width, pos[1], width, node->size[1], anamorphicBorder);
		}
		/* bottom */
		if (pos[1] + node->size[1] < viddef.virtualHeight) {
			const int height = viddef.virtualHeight - (pos[1] + node->size[1]);
			MN_DrawFill(0, viddef.virtualHeight - height, viddef.virtualWidth, height, anamorphicBorder);
		}
	}

	/* darker background if last window is a modal */
	if (EXTRADATA(node).modal && mn.windowStack[mn.windowStackPos - 1] == node)
		MN_DrawFill(0, 0, viddef.virtualWidth, viddef.virtualHeight, modalBackground);

	/* draw the background */
	image = MN_GetReferenceString(node, node->image);
	if (image)
		MN_DrawPanel(pos, node->size, image, 0, 0, windowTemplate);

	/* draw the title */
	text = MN_GetReferenceString(node, node->text);
	if (text)
		MN_DrawStringInBox(font, ALIGN_CC, pos[0] + node->padding, pos[1] + node->padding, node->size[0] - node->padding - node->padding, TOP_HEIGHT + 10 - node->padding - node->padding, text, LONGLINES_PRETTYCHOP);

	/* embedded timer */
	if (EXTRADATA(node).onTimeOut && EXTRADATA(node).timeOut) {
		if (EXTRADATA(node).lastTime == 0)
			EXTRADATA(node).lastTime = cls.realtime;
		if (EXTRADATA(node).lastTime + EXTRADATA(node).timeOut < cls.realtime) {
			EXTRADATA(node).lastTime = 0;	/**< allow to reset timeOut on the event, and restart it, with an uptodate lastTime */
			Com_DPrintf(DEBUG_CLIENT, "MN_DrawMenus: timeout for node '%s'\n", node->name);
			MN_ExecuteEventActions(node, EXTRADATA(node).onTimeOut);
		}
	}
}

static void MN_WindowNodeDoLayout (menuNode_t *node)
{
	qboolean resized = qfalse;

	if (!node->invalidated)
		return;

	/* use a the space */
	if (EXTRADATA(node).fill) {
		if (node->size[0] != viddef.virtualWidth) {
			node->size[0] = viddef.virtualWidth;
			resized = qtrue;
		}
		if (node->size[1] != viddef.virtualHeight) {
			node->size[1] = viddef.virtualHeight;
			resized = qtrue;
		}
	}

	/* move fullscreen menu on the center of the screen */
	if (MN_WindowIsFullScreen(node)) {
		node->pos[0] = (int) ((viddef.virtualWidth - node->size[0]) / 2);
		node->pos[1] = (int) ((viddef.virtualHeight - node->size[1]) / 2);
	}

	/** @todo check and fix here window outside the screen */

	if (resized) {
		if (EXTRADATA(node).starLayout) {
			MN_StarLayout(node);
		}
	}

	/* super */
	localBehaviour->super->doLayout(node);
}

/**
 * @brief Called when we init the node on the screen
 * @todo we can move generic code into abstract node
 */
static void MN_WindowNodeInit (menuNode_t *node)
{
	menuNode_t *child;

	/* init the embeded timer */
	EXTRADATA(node).lastTime = cls.realtime;

	/* init child */
	for (child = node->firstChild; child; child = child->next) {
		if (child->behaviour->init) {
			child->behaviour->init(child);
		}
	}

	/* script callback */
	if (EXTRADATA(node).onInit)
		MN_ExecuteEventActions(node, EXTRADATA(node).onInit);

	MN_Invalidate(node);
}

/**
 * @brief Called when we close the node on the screen
 * @todo we can move generic code into abstract node
 */
static void MN_WindowNodeClose (menuNode_t *node)
{
	menuNode_t *child;

	/* close child */
	for (child = node->firstChild; child; child = child->next) {
		if (child->behaviour->close) {
			child->behaviour->close(child);
		}
	}

	/* script callback */
	if (EXTRADATA(node).onClose)
		MN_ExecuteEventActions(node, EXTRADATA(node).onClose);
}

/**
 * @brief Called at the begin of the load from script
 */
static void MN_WindowNodeLoading (menuNode_t *node)
{
	node->size[0] = VID_NORM_WIDTH;
	node->size[1] = VID_NORM_HEIGHT;
	node->font = "f_big";
	node->padding = 5;
}

void MN_WindowNodeSetRenderNode (menuNode_t *node, menuNode_t *renderNode)
{
	if (!MN_NodeInstanceOf(node, "window")) {
		Com_Printf("MN_WindowNodeSetRenderNode: '%s' node is not an 'window'.\n", MN_GetPath(node));
		return;
	}

	if (EXTRADATA(node).renderNode) {
		Com_Printf("MN_WindowNodeSetRenderNode: second render node ignored (\"%s\")\n", MN_GetPath(renderNode));
		return;
	}

	EXTRADATA(node).renderNode = renderNode;
}

/**
 * @brief Called at the end of the load from script
 */
static void MN_WindowNodeLoaded (menuNode_t *node)
{
	static char* closeCommand = "mn_close <path:root>;";

	/* if it need, construct the drag button */
	if (EXTRADATA(node).dragButton) {
		menuNode_t *control = MN_AllocNode("move_window_button", "controls", node->dynamic);
		control->root = node;
		control->image = NULL;
		/** @todo Once @c image_t is known on the client, use @c image->width resp. @c image->height here */
		control->size[0] = node->size[0];
		control->size[1] = TOP_HEIGHT;
		control->pos[0] = 0;
		control->pos[1] = 0;
		control->tooltip = _("Drag to move window");
		MN_AppendNode(node, control);
	}

	/* if the menu should have a close button, add it here */
	if (EXTRADATA(node).closeButton) {
		menuNode_t *button = MN_AllocNode("close_window_button", "pic", node->dynamic);
		const int positionFromRight = CONTROLS_PADDING;
		button->root = node;
		button->image = "icons/system_close";
		/** @todo Once @c image_t is known on the client, use @c image->width resp. @c image->height here */
		button->size[0] = CONTROLS_IMAGE_DIMENSIONS;
		button->size[1] = CONTROLS_IMAGE_DIMENSIONS;
		button->pos[0] = node->size[0] - positionFromRight - button->size[0];
		button->pos[1] = CONTROLS_PADDING;
		button->tooltip = _("Close the window");
		button->onClick = MN_AllocStaticCommandAction(closeCommand);
		MN_AppendNode(node, button);
	}

	EXTRADATA(node).isFullScreen = node->size[0] == VID_NORM_WIDTH
			&& node->size[1] == VID_NORM_HEIGHT;

#ifdef DEBUG
	if (node->size[0] < LEFT_WIDTH + MID_WIDTH + RIGHT_WIDTH || node->size[1] < TOP_HEIGHT + MID_HEIGHT + BOTTOM_HEIGHT)
		Com_DPrintf(DEBUG_CLIENT, "Node '%s' too small. It can create graphical bugs\n", node->name);
#endif
}

static void MN_WindowNodeClone (const menuNode_t *source, menuNode_t *clone)
{
	/** @todo anyway we should remove soon renderNode */
	if (EXTRADATACONST(source).renderNode != NULL) {
		Com_Printf("MN_WindowNodeClone: Do not inherite window using a render node. Render node ignored (\"%s\")\n", MN_GetPath(clone));
		EXTRADATA(clone).renderNode = NULL;
	}
}

/**
 * @brief Valid properties for a window node (called yet 'menu')
 */
static const value_t windowNodeProperties[] = {
	/* @override image
	 * Texture to use. The texture is a cut of 9 portions
	 * (left, middle, right � top, middle, bottom). Between all this elements,
	 * we use a margin of 3 pixels (purple mark into the sample).
	 * Graphically we see only a 1 pixel margin, because, for a problem of
	 * lossy compression of texture it's not nice to have a pure transparent
	 * pixel near the last colored one, when we cut or stretch textures.
	 * @image html http://ufoai.ninex.info/wiki/images/Popup_alpha_tile.png
	 */

	/* In windows where notify messages appear (like e.g. the video options window when you have to restart the game until the settings take effects) you can define the position of those messages with this option. */
	{"noticepos", V_POS, MN_EXTRADATA_OFFSETOF(windowExtraData_t, noticePos), MEMBER_SIZEOF(windowExtraData_t, noticePos)},
	/* Create subnode allowing to move the window when we click on the header. Updating this attribute at the runtime will change nothing. */
	{"dragbutton", V_BOOL, MN_EXTRADATA_OFFSETOF(windowExtraData_t, dragButton), MEMBER_SIZEOF(windowExtraData_t, dragButton)},
	/* Add a button on the top right the window to close it. Updating this attribute at the runtime will change nothing. */
	{"closebutton", V_BOOL, MN_EXTRADATA_OFFSETOF(windowExtraData_t, closeButton), MEMBER_SIZEOF(windowExtraData_t, closeButton)},
	/* If true, the user can't select something outside the modal window. He must first close the window. */
	{"modal", V_BOOL, MN_EXTRADATA_OFFSETOF(windowExtraData_t, modal), MEMBER_SIZEOF(windowExtraData_t, modal)},
	/* If true, if the user click outside the window, it will close it. */
	{"dropdown", V_BOOL, MN_EXTRADATA_OFFSETOF(windowExtraData_t, dropdown), MEMBER_SIZEOF(windowExtraData_t, dropdown)},
	/* If true, the user can't use ''ESC'' key to close the window. */
	{"preventtypingescape", V_BOOL, MN_EXTRADATA_OFFSETOF(windowExtraData_t, preventTypingEscape), MEMBER_SIZEOF(windowExtraData_t, preventTypingEscape)},
	/* If true, the window is filled according to the widescreen. */
	{"fill", V_BOOL, MN_EXTRADATA_OFFSETOF(windowExtraData_t, fill), MEMBER_SIZEOF(windowExtraData_t, fill)},
	/* If true, when the window size change, the window content position is updated according to the "star" layout.
	 * @todo Need more documentation.
	 */
	{"starlayout", V_BOOL, MN_EXTRADATA_OFFSETOF(windowExtraData_t, starLayout), MEMBER_SIZEOF(windowExtraData_t, starLayout)},

	/* This property control milliseconds between each calls of <code>onEvent</code>.
	 * If value is 0 (the default value) nothing is called. We can change the
	 * value at the runtime.
	 */
	{"timeout", V_INT,MN_EXTRADATA_OFFSETOF(windowExtraData_t, timeOut), MEMBER_SIZEOF(windowExtraData_t, timeOut)},

	/* Called when the window is puched into the active window stack. */
	{"oninit", V_UI_ACTION, MN_EXTRADATA_OFFSETOF(windowExtraData_t, onInit), MEMBER_SIZEOF(windowExtraData_t, onInit)},
	/* Called when the window is removed from the active window stack. */
	{"onclose", V_UI_ACTION, MN_EXTRADATA_OFFSETOF(windowExtraData_t, onClose), MEMBER_SIZEOF(windowExtraData_t, onClose)},
	/* Called periodically. See <code>timeout</code>. */
	{"onevent", V_UI_ACTION, MN_EXTRADATA_OFFSETOF(windowExtraData_t, onTimeOut), MEMBER_SIZEOF(windowExtraData_t, onTimeOut)},

	{NULL, V_NULL, 0, 0}
};

/**
 * @brief Get the noticePosition from a window node.
 * @param node A window node
 * @return A position, else NULL if no notice position
 */
vec_t *MN_WindowNodeGetNoticePosition(struct menuNode_s *node)
{
	if (EXTRADATA(node).noticePos[0] == 0 && EXTRADATA(node).noticePos[1] == 0)
		return NULL;
	return EXTRADATA(node).noticePos;
}

/**
 * @brief True if the window is a drop down.
 * @param node A window node
 * @return True if the window is a drop down.
 */
qboolean MN_WindowIsDropDown(const struct menuNode_s* const node)
{
	return EXTRADATACONST(node).dropdown;
}

/**
 * @brief True if the window is a modal.
 * @param node A window node
 * @return True if the window is a modal.
 */
qboolean MN_WindowIsModal(const struct menuNode_s* const node)
{
	return EXTRADATACONST(node).modal;
}

/**
 * @brief Add a key binding to a window node.
 * Window node store key bindings for his node child.
 * @param node A window node
 */
void MN_WindowNodeRegisterKeyBinding (menuNode_t* node, menuKeyBinding_t *binding)
{
	assert(MN_NodeInstanceOf(node, "window"));
	binding->next = EXTRADATA(node).keyList;
	EXTRADATA(node).keyList = binding;
}

const menuKeyBinding_t *binding;

/**
 * @brief Search a a key binding fromp a window node.
 * Window node store key bindings for his node child.
 * @param node A window node
 */
menuKeyBinding_t *MN_WindowNodeGetKeyBinding (const struct menuNode_s* const node, unsigned int key)
{
	menuKeyBinding_t *binding = EXTRADATACONST(node).keyList;
	assert(MN_NodeInstanceOf(node, "window"));
	while (binding) {
		if (binding->key == key)
			break;
		binding = binding->next;
	}
	return binding;
}

void MN_RegisterWindowNode (nodeBehaviour_t *behaviour)
{
	localBehaviour = behaviour;
	behaviour->name = "window";
	behaviour->loading = MN_WindowNodeLoading;
	behaviour->loaded = MN_WindowNodeLoaded;
	behaviour->init = MN_WindowNodeInit;
	behaviour->close = MN_WindowNodeClose;
	behaviour->draw = MN_WindowNodeDraw;
	behaviour->doLayout = MN_WindowNodeDoLayout;
	behaviour->clone = MN_WindowNodeClone;
	behaviour->properties = windowNodeProperties;
	behaviour->extraDataSize = sizeof(EXTRADATA_TYPE);
}
