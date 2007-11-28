/**
 * @file cl_event.c
 * @brief Geoscape event implementation
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

static linkedList_t *eventMails = NULL;

/**
 * @brief Searches all event mails for a given id
 * @note Might also return NULL - always check the return value
 * @note If you want to create mails that base on a script defintion but have differnet
 * body messages, set createCopy to true
 * @param[in] id The id from the script files
 * @param[in] createCopy Don't return the link to gd.eventMails but allocate memory
 * and copy the gd.eventMail memory over to the newly allocated. Don't use createCopy on
 * dynamic mails
 * @sa UP_SetMailHeader
 * @sa CL_NewEventMail
 */
eventMail_t* CL_GetEventMail (const char *id, qboolean createCopy)
{
	int i;
	linkedList_t* list;
	eventMail_t* listMail;

	if (!createCopy) {
		for (i = 0; i < gd.numEventMails; i++)
			if (!Q_strcmp(gd.eventMails[i].id, id))
				return &gd.eventMails[i];

		list = eventMails;
		while (list) {
			listMail = (eventMail_t *)list->data;
			if (!Q_strcmp(listMail->id, id))
				return listMail;
			list = list->next;
		}

		return NULL;
	} else {
		/* create a copy of the static eventmails */
		eventMail_t *eventMail = NULL, *newEventMail;

		/* search the static mails - and only the static ones! */
		for (i = 0; i < gd.numEventMails; i++)
			if (!Q_strcmp(gd.eventMails[i].id, id)) {
				eventMail = &gd.eventMails[i];
				break;
			}

		if (!eventMail)
			return NULL;

		newEventMail = Mem_PoolAlloc(sizeof(eventMail_t), cl_localPool, CL_TAG_NONE);
		if (!newEventMail)
			return NULL;

		/* don't !! free the original pointers here */
		memcpy(newEventMail, eventMail, sizeof(eventMail_t));
		LIST_AddPointer(&eventMails, newEventMail);
		/* make sure, that you set a unique eventmail->id and eventmail->body now */
		return newEventMail;
	}
}

/**
 * @brief Make sure, that the linked list is freed with every new game
 * @sa CL_ResetSinglePlayerData
 */
void CL_FreeDynamicEventMail (void)
{
	/* the pointers are not freed, this is done with the
	 * pool clear in CL_ResetSinglePlayerData */
	LIST_Delete(eventMails);
	eventMails = NULL;
}

/**
 * @brief Use this function to create new eventmails with dynamic body content
 * @sa CL_GetEventMail
 * @note The pointers in the original eventmail are not freed - we still need this memory later!!
 * @sa CL_ResetSinglePlayerData
 * @sa CP_UFOSendMail
 * @param[in] id eventmail id of the source mail parsed from events.ufo
 * @param[in] newID the new id for the dynamic mail (needed to seperate the new mail
 * from the source mail to let CL_GetEventMail be able to find it afterwards)
 * @param[in] The body of the new mail - this may also be NULL if you need the original body of
 * the source mail that was parsed from events.ufo
 */
eventMail_t* CL_NewEventMail (const char *id, const char *newID, const char *body)
{
	eventMail_t* mail;

	assert(id);
	assert(newID);

	mail = CL_GetEventMail(id, qtrue);
	if (!mail)
		return NULL;

	/* cl_localPool is freed with every new game in CL_ResetSinglePlayerData */
	mail->id = Mem_PoolStrDup(newID, cl_localPool, CL_TAG_NONE);

	/* maybe we want to use the old body */
	if (body)
		mail->body = Mem_PoolStrDup(body, cl_localPool, CL_TAG_NONE);

	return mail;
}

/** @brief Valid event mail parameters */
static const value_t eventMail_vals[] = {
	{"subject", V_TRANSLATION_MANUAL_STRING, offsetof(eventMail_t, subject), 0},
	{"from", V_TRANSLATION_MANUAL_STRING, offsetof(eventMail_t, from), 0},
	{"to", V_TRANSLATION_MANUAL_STRING, offsetof(eventMail_t, to), 0},
	{"cc", V_TRANSLATION_MANUAL_STRING, offsetof(eventMail_t, cc), 0},
	{"date", V_TRANSLATION_MANUAL_STRING, offsetof(eventMail_t, date), 0},
	{"body", V_TRANSLATION_MANUAL_STRING, offsetof(eventMail_t, body), 0},
	{"icon", V_CLIENT_HUNK_STRING, offsetof(eventMail_t, icon), 0},

	{NULL, 0, 0, 0}
};

/**
 * @sa CL_ParseScriptFirst
 * @note write into cl_localPool - free on every game restart and reparse
 */
void CL_ParseEventMails (const char *name, const char **text)
{
	const char *errhead = "CL_ParseEventMails: unexpected end of file (mail ";
	eventMail_t *eventMail;
	const value_t *vp;
	const char *token;

	if (gd.numEventMails >= MAX_EVENTMAILS) {
		Com_Printf("CL_ParseEventMails: mail def \"%s\" with same name found, second ignored\n", name);
		return;
	}

	/* initialize the eventMail */
	eventMail = &gd.eventMails[gd.numEventMails++];
	memset(eventMail, 0, sizeof(eventMail_t));

	Com_DPrintf(DEBUG_CLIENT, "...found eventMail %s\n", name);
	eventMail->id = Mem_PoolStrDup(name, cl_localPool, CL_TAG_REPARSE_ON_NEW_GAME);

	/* get it's body */
	token = COM_Parse(text);

	if (!*text || *token != '{') {
		Com_Printf("CL_ParseEventMails: eventMail def \"%s\" without body ignored\n", name);
		gd.numEventMails--;
		return;
	}

	do {
		token = COM_EParse(text, errhead, name);
		if (!*text)
			break;
		if (*token == '}')
			break;

		/* check for some standard values */
		for (vp = eventMail_vals; vp->string; vp++)
			if (!Q_strcmp(token, vp->string)) {
				/* found a definition */
				token = COM_EParse(text, errhead, name);
				if (!*text)
					return;

				switch (vp->type) {
				case V_TRANSLATION_MANUAL_STRING:
					token++;
				case V_CLIENT_HUNK_STRING:
					Mem_PoolStrDupTo(token, (char**) ((char*)eventMail + (int)vp->ofs), cl_localPool, CL_TAG_REPARSE_ON_NEW_GAME);
					break;
				default:
					Com_ParseValue(eventMail, token, vp->type, vp->ofs, vp->size);
					break;
				}
				break;
			}

		if (!vp->string) {
			Com_Printf("CL_ParseEventMails: unknown token \"%s\" ignored (mail %s)\n", token, name);
			COM_EParse(text, errhead, name);
		}
	} while (*text);
}

/**
 * @sa UP_OpenMail_f
 * @sa MN_AddNewMessage
 * @sa UP_SetMailHeader
 * @sa UP_OpenEventMail
 */
void CL_EventAddMail_f (void)
{
	const char *eventMailId;
	eventMail_t* eventMail;
	message_t *m;
	char dateBuf[MAX_VAR] = "";

	if (Cmd_Argc() < 2) {
		Com_Printf("Usage: %s <event_mail_id>\n", Cmd_Argv(0));
		return;
	}

	eventMailId = Cmd_Argv(1);

	eventMail = CL_GetEventMail(eventMailId, qfalse);
	if (!eventMail) {
		Com_Printf("CL_EventAddMail_f: Could not find eventmail with id '%s'\n", eventMailId);
		return;
	}

	if (!eventMail->from || !eventMail->to || !eventMail->subject || !eventMail->body) {
		Com_Printf("CL_EventAddMail_f: mail with id '%s' has incomplete data\n", eventMailId);
		return;
	}

	if (!eventMail->date) {
		int day, month;
		CL_DateConvert(&ccs.date, &day, &month);
		Com_sprintf(dateBuf, sizeof(dateBuf), _("%i %s %02i"),
			ccs.date.day / DAYS_PER_YEAR, CL_DateGetMonthName(month), day);
		eventMail->date = Mem_PoolStrDup(dateBuf, cl_localPool, 0);
	}

	/* the subject double %s: see UP_SetMailHeader */
	m = MN_AddNewMessage("", va(_("You've got a new mail: %s"), _(eventMail->subject)), qfalse, MSG_EVENT, NULL);
	if (m)
		m->eventMail = eventMail;
	else
		Com_Printf("Could not add message with id: %s\n", eventMailId);
}
