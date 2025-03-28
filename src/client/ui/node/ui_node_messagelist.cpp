/**
 * @file
 * @todo add getter/setter to cleanup access to extradata from cl_*.c files (check "u.text.")
 */

/*
Copyright (C) 2002-2025 UFO: Alien Invasion.

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

#include "../../DateTime.h"
#include "../ui_main.h"
#include "../ui_internal.h"
#include "../ui_font.h"
#include "../ui_actions.h"
#include "../ui_parse.h"
#include "../ui_render.h"
#include "../ui_sprite.h"
#include "ui_node_text.h"
#include "ui_node_messagelist.h"
#include "ui_node_abstractnode.h"

#include "../../client.h"
#include "../../../shared/parse.h"

#include "../../../common/scripts_lua.h"

#define EXTRADATA(node) UI_EXTRADATA(node, abstractScrollableExtraData_t)
#define EXTRADATACONST(node) UI_EXTRADATACONST(node, abstractScrollableExtraData_t)

/** @todo use the font height? */
static const int LINEHEIGHT	= 20;

static const int DATETIME_COLUUI_SIZE = 200;

/* Used for drag&drop-like scrolling */
static int mouseScrollX;
static int mouseScrollY;

/* Russian timestamp (with UTF-8) is 23 bytes long */
#define TIMESTAMP_TEXT 24
typedef struct uiMessageListNodeMessage_s {
	char title[MAX_VAR];
	char timestamp[TIMESTAMP_TEXT];
	char* text;
	class DateTime date;
	const char* iconName;
	int lineUsed;		/**< used by the node to cache the number of lines need (often =1) */
	struct uiMessageListNodeMessage_s* next;
} uiMessageListNodeMessage_t;

/** @todo implement this on a per-node basis */
static uiMessageListNodeMessage_t* messageStack;

struct uiMessageListNodeMessage_s* UI_MessageGetStack (void)
{
	return messageStack;
}

void UI_MessageResetStack (void)
{
	messageStack = nullptr;
}

void UI_MessageAddStack (struct uiMessageListNodeMessage_s* message)
{
	message->next = messageStack;
	messageStack = message;
}

/**
 * @return Number of lines need to display this message
 */
static int UI_MessageGetLines (const uiNode_t* node, uiMessageListNodeMessage_t* message, const char* fontID, int width)
{
	const int column1 = DATETIME_COLUUI_SIZE;
	const int column2 = width - DATETIME_COLUUI_SIZE - node->padding;
	int lines1;
	int lines2;
	R_FontTextSize(fontID, message->timestamp, column1, LONGLINES_WRAP, nullptr, nullptr, &lines1, nullptr);
	R_FontTextSize(fontID, message->text, column2, LONGLINES_WRAP, nullptr, nullptr, &lines2, nullptr);
	return std::max(lines1, lines2);
}

static char* lastDate;

/**
 * @todo do not hard code icons
 * @todo cache icon result
 */
static uiSprite_t* UI_MessageGetIcon (const uiMessageListNodeMessage_t* message)
{
	const char* iconName = message->iconName;
	if (Q_strnull(iconName))
		iconName = "icons/message_info";

	return UI_GetSpriteByName(iconName);
}

static void UI_MessageDraw (const uiNode_t* node, uiMessageListNodeMessage_t* message, const char* fontID, int x, int y, int width, int* screenLines)
{
	const int column1 = DATETIME_COLUUI_SIZE;
	const int column2 = width - DATETIME_COLUUI_SIZE - node->padding;
	int lines1 = *screenLines;
	int lines2 = *screenLines;

	/* also display the first date on wraped message we only see the end */
	if (lines1 < 0)
		lines1 = 0;

	/* display the date */
	if (lastDate == nullptr || !Q_streq(lastDate, message->timestamp)) {
		R_Color(node->color);
		UI_DrawString(fontID, ALIGN_UL, x, y, x, column1, LINEHEIGHT, message->timestamp, EXTRADATACONST(node).scrollY.viewSize, 0, &lines1, true, LONGLINES_WRAP);
		R_Color(nullptr);
	}

	x += DATETIME_COLUUI_SIZE + node->padding;

	/* identify the begin of a message with a mark */
	if (lines2 >= 0) {
		const uiSprite_t* icon = UI_MessageGetIcon(message);
		R_Color(nullptr);
		UI_DrawSpriteInBox(false, icon, SPRITE_STATUS_NORMAL, x - 25, y + LINEHEIGHT * lines2 - 1, 19, 19);
	}

	/* draw the message */
	R_Color(node->color);
	UI_DrawString(fontID, ALIGN_UL, x, y, x, column2, LINEHEIGHT, message->text, EXTRADATACONST(node).scrollY.viewSize, 0, &lines2, true, LONGLINES_WRAP);
	R_Color(nullptr);
	*screenLines = std::max(lines1, lines2);
	lastDate = message->timestamp;
}

/**
 * @brief Draws the messagesystem node
 * @param[in] node The context node
 */
void uiMessageListNode::draw (uiNode_t* node)
{
	uiMessageListNodeMessage_t* message;
	int screenLines;
	const char* font = UI_GetFontFromNode(node);
	vec2_t pos;
	int x, y, width;
	int defaultHeight;
	int lineNumber = 0;
	int posY;

/* #define AUTOSCROLL */		/**< if newer messages are on top, autoscroll is not need */
#ifdef AUTOSCROLL
	bool autoscroll;
#endif
	UI_GetNodeAbsPos(node, pos);

	defaultHeight = LINEHEIGHT;

#ifdef AUTOSCROLL
	autoscroll = (EXTRADATA(node).scrollY.viewPos + EXTRADATA(node).scrollY.viewSize == EXTRADATA(node).scrollY.fullSize)
		|| (EXTRADATA(node).scrollY.fullSize < EXTRADATA(node).scrollY.viewSize);
#endif

	/* text box */
	x = pos[0] + node->padding;
	y = pos[1] + node->padding;
	width = node->box.size[0] - node->padding - node->padding;

	/* update message cache */
	if (isSizeChange(node)) {
		/* recompute all line size */
		message = messageStack;
		while (message) {
			message->lineUsed = UI_MessageGetLines(node, message, font, width);
			lineNumber += message->lineUsed;
			message = message->next;
		}
	} else {
		/* only check unvalidated messages */
		message = messageStack;
		while (message) {
			if (message->lineUsed == 0)
				message->lineUsed = UI_MessageGetLines(node, message, font, width);
			lineNumber += message->lineUsed;
			message = message->next;
		}
	}

	/* update scroll status */
#ifdef AUTOSCROLL
	if (autoscroll)
		setScrollY(node, lineNumber, node->box.size[1] / defaultHeight, lineNumber);
	else
		setScrollY(node, -1, node->box.size[1] / defaultHeight, lineNumber);
#else
	setScrollY(node, -1, node->box.size[1] / defaultHeight, lineNumber);
#endif

	/* found the first message we must display */
	message = messageStack;
	posY = EXTRADATA(node).scrollY.viewPos;
	while (message && posY > 0) {
		posY -= message->lineUsed;
		if (posY < 0)
			break;
		message = message->next;
	}

	/* draw */
	/** @note posY can be negative (if we must display last line of the first message) */
	lastDate = nullptr;
	screenLines = posY;
	while (message) {
		UI_MessageDraw(node, message, font, x, y, width, &screenLines);
		if (screenLines >= EXTRADATA(node).scrollY.viewSize)
			break;
		message = message->next;
	}
}

bool uiMessageListNode::onScroll (uiNode_t* node, int deltaX, int deltaY)
{
	bool down = deltaY > 0;
	bool updated;
	if (deltaY == 0)
		return false;
	updated = scrollY(node, (down ? 1 : -1));
	/* @todo use super behaviour */
	if (node->onWheelUp && !down) {
		UI_ExecuteEventActions(node, node->onWheelUp);
		updated = true;
	}
	if (node->onWheelDown && down) {
		UI_ExecuteEventActions(node, node->onWheelDown);
		updated = true;
	}
	if (node->onWheel) {
		UI_ExecuteEventActions(node, node->onWheel);
		updated = true;
	}
	return updated;
}

void uiMessageListNode::onLoading (uiNode_t* node)
{
	Vector4Set(node->color, 1.0, 1.0, 1.0, 1.0);
}

/**
 * @brief Track mouse down/up events to implement drag&drop-like scrolling, for touchscreen devices
 * @sa UI_TextNodeMouseUp, UI_TextNodeCapturedMouseMove
*/
void uiMessageListNode::onMouseDown (uiNode_t* node, int x, int y, int button)
{
	if (!UI_GetMouseCapture() && button == K_MOUSE1 &&
		EXTRADATA(node).scrollY.fullSize > EXTRADATA(node).scrollY.viewSize) {
		UI_SetMouseCapture(node);
		mouseScrollX = x;
		mouseScrollY = y;
	}
}

void uiMessageListNode::onMouseUp (uiNode_t* node, int x, int y, int button)
{
	if (UI_GetMouseCapture() == node)  /* More checks can never hurt */
		UI_MouseRelease();
}

void uiMessageListNode::onCapturedMouseMove (uiNode_t* node, int x, int y)
{
	const int lineHeight = getCellHeight(node);
	const int deltaY = (mouseScrollY - y) / lineHeight;
	/* We're doing only vertical scroll, that's enough for the most instances */
	if (abs(mouseScrollY - y) >= lineHeight) {
		scrollY(node, deltaY);
		/* @todo not accurate */
		mouseScrollX = x;
		mouseScrollY = y;
	}
	onMouseMove(node, x, y);
}

/**
 * @brief Return size of the cell, which is the size (in virtual "pixel") which represent 1 in the scroll values.
 * Here we guess the widget can scroll pixel per pixel.
 * @return Size in pixel.
 */
int uiMessageListNode::getCellHeight (uiNode_t* node)
{
	return LINEHEIGHT;
}

void UI_RegisterMessageListNode (uiBehaviour_t* behaviour)
{
	behaviour->name = "messagelist";
	behaviour->extends = "abstractscrollable";
	behaviour->manager = UINodePtr(new uiMessageListNode());
	behaviour->lua_SWIG_typeinfo = UI_SWIG_TypeQuery("uiMessageListNode_t *");
}
