/**
 * @file cl_sequence.c
 * @brief Non-interactive sequence rendering and AVI recording.
 * @note Sequences are rendered on top of a menu node - the default menu
 * is stored in mn_sequence cvar
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

#define MAX_DATA_LENGTH 2048

typedef struct seqCmd_s {
	int (*handler) (const char *name, char *data);
	char name[MAX_VAR];
	char data[MAX_DATA_LENGTH];
} seqCmd_t;

typedef struct sequence_s {
	char name[MAX_VAR];
	int start;
	int length;
} sequence_t;

typedef enum {
	SEQ_END,
	SEQ_WAIT,
	SEQ_CLICK,
	SEQ_PRECACHE,
	SEQ_CAMERA,
	SEQ_MODEL,
	SEQ_2DOBJ,
	SEQ_REM,
	SEQ_CMD,

	SEQ_NUMCMDS
} seqCmdEnum_t;

static const char *seqCmdName[SEQ_NUMCMDS] = {
	"end",
	"wait",
	"click",
	"precache",
	"camera",
	"model",
	"2dobj",
	"rem",
	"cmd"
};

typedef struct seqCamera_s {
	vec3_t origin, speed;
	vec3_t angles, omega;
	float dist, ddist;
	float zoom, dzoom;
} seqCamera_t;

typedef struct seqEnt_s {
	qboolean inuse;
	char name[MAX_VAR];
	struct model_s *model;
	int skin;
	vec3_t origin, speed;
	vec3_t angles, omega;
	float alpha;
	char parent[MAX_VAR];
	char tag[MAX_VAR];
	animState_t as;
	entity_t *ep;
} seqEnt_t;

typedef struct seq2D_s {
	qboolean inuse;
	char name[MAX_VAR];
	char text[MAX_VAR];	/**< a placeholder for gettext (V_TRANSLATION_MANUAL_STRING) */
	char font[MAX_VAR];
	char image[MAX_VAR];
	vec2_t pos, speed;
	vec2_t size, enlarge;
	vec4_t color, fade, bgcolor;
	byte align;
	qboolean relativePos;	/**< useful for translations when sentence length may differ */
} seq2D_t;

int SEQ_Click(const char *name, char *data);
int SEQ_Wait(const char *name, char *data);
int SEQ_Precache(const char *name, char *data);
int SEQ_Camera(const char *name, char *data);
int SEQ_Model(const char *name, char *data);
int SEQ_2Dobj(const char *name, char *data);
int SEQ_Remove(const char *name, char *data);
int SEQ_Command(const char *name, char *data);

static int (*seqCmdFunc[SEQ_NUMCMDS]) (const char *name, char *data) = {
	NULL,
	SEQ_Wait,
	SEQ_Click,
	SEQ_Precache,
	SEQ_Camera,
	SEQ_Model,
	SEQ_2Dobj,
	SEQ_Remove,
	SEQ_Command
};

#define MAX_SEQCMDS		8192
#define MAX_SEQUENCES	32
#define MAX_SEQENTS		128
#define MAX_SEQ2DS		128

static seqCmd_t seqCmds[MAX_SEQCMDS];
static int numSeqCmds;

static sequence_t sequences[MAX_SEQUENCES];
static int numSequences;

static int seqTime;	/**< miliseconds the sequence is already running */
static qboolean seqLocked = qfalse; /**< if a click event is triggered this is true */
static qboolean seqEndClickLoop = qfalse; /**< if the menu node the sequence is rendered in fetches a click this is true */
static int seqCmd, seqEndCmd;

static seqCamera_t seqCamera;

static seqEnt_t seqEnts[MAX_SEQENTS];
static int numSeqEnts;

static seq2D_t seq2Ds[MAX_SEQ2DS];
static int numSeq2Ds;

static cvar_t *seq_animspeed;


/**
 * @brief Sets the client state to ca_disconnected
 * @sa CL_SequenceStart_f
 */
void CL_SequenceEnd_f (void)
{
	CL_SetClientState(ca_disconnected);
}


/**
 * @brief Set the camera values for a sequence
 * @sa CL_SequenceRender
 */
static void CL_SequenceCamera (void)
{
	if (!scr_vrect.width || !scr_vrect.height)
		return;

	/* advance time */
	VectorMA(seqCamera.origin, cls.frametime, seqCamera.speed, seqCamera.origin);
	VectorMA(seqCamera.angles, cls.frametime, seqCamera.omega, seqCamera.angles);
	seqCamera.zoom += cls.frametime * seqCamera.dzoom;
	seqCamera.dist += cls.frametime * seqCamera.ddist;

	/* set camera */
	VectorCopy(seqCamera.origin, cl.cam.reforg);
	VectorCopy(seqCamera.angles, cl.cam.angles);

	AngleVectors(cl.cam.angles, cl.cam.axis[0], cl.cam.axis[1], cl.cam.axis[2]);
	VectorMA(cl.cam.reforg, -seqCamera.dist, cl.cam.axis[0], cl.cam.camorg);
	cl.cam.zoom = max(seqCamera.zoom, MIN_ZOOM);
	/* fudge to get isometric and perspective modes looking similar */
	if (cl_isometric->integer)
		cl.cam.zoom /= 1.35;
	V_CalcFovX();
}


/**
 * @brief Finds a given entity in all sequence entities
 * @sa CL_SequenceFind2D
 */
static seqEnt_t *CL_SequenceFindEnt (const char *name)
{
	seqEnt_t *se;
	int i;

	for (i = 0, se = seqEnts; i < numSeqEnts; i++, se++)
		if (se->inuse && !Q_strncmp(se->name, name, MAX_VAR))
			break;
	if (i < numSeqEnts)
		return se;
	else
		return NULL;
}


/**
 * @brief Finds a given 2d object in the current sequence data
 * @sa CL_SequenceFindEnt
 */
static seq2D_t *CL_SequenceFind2D (const char *name)
{
	seq2D_t *s2d;
	int i;

	for (i = 0, s2d = seq2Ds; i < numSeq2Ds; i++, s2d++)
		if (s2d->inuse && !Q_strncmp(s2d->name, name, MAX_VAR))
			break;
	if (i < numSeq2Ds)
		return s2d;
	else
		return NULL;
}


/**
 * @sa CL_Sequence2D
 * @sa V_RenderView
 * @sa CL_SequenceEnd_f
 * @sa MN_PopMenu
 * @sa CL_SequenceFindEnt
 */
void CL_SequenceRender (void)
{
	entity_t ent;
	seqCmd_t *sc;
	seqEnt_t *se;
	float sunfrac;
	int i;

	/* run script */
	while (seqTime <= cl.time) {
		/* test for finish */
		if (seqCmd >= seqEndCmd) {
			CL_SequenceEnd_f();
			MN_PopMenu(qfalse);
			return;
		}

		/* call handler */
		sc = &seqCmds[seqCmd];
		seqCmd += sc->handler(sc->name, sc->data);
	}

	/* set camera */
	CL_SequenceCamera();

	/* render sequence */
	for (i = 0, se = seqEnts; i < numSeqEnts; i++, se++)
		if (se->inuse) {
			/* advance in time */
			VectorMA(se->origin, cls.frametime, se->speed, se->origin);
			VectorMA(se->angles, cls.frametime, se->omega, se->angles);
			R_AnimRun(&se->as, se->model, seq_animspeed->value * cls.frametime);

			/* add to scene */
			memset(&ent, 0, sizeof(ent));
			ent.model = se->model;
			ent.skinnum = se->skin;
			ent.as = se->as;
			ent.alpha = se->alpha;

			sunfrac = 1.0;
			ent.lightparam = &sunfrac;

			VectorCopy(se->origin, ent.origin);
			VectorCopy(se->origin, ent.oldorigin);
			VectorCopy(se->angles, ent.angles);

			if (se->parent && se->tag) {
				seqEnt_t *parent;

				parent = CL_SequenceFindEnt(se->parent);
				if (parent)
					ent.tagent = parent->ep;
				ent.tagname = se->tag;
			}

			/* add to render list */
			se->ep = V_GetEntity();
			V_AddEntity(&ent);
		}
}


/**
 * @brief Renders text and images
 * @sa CL_ResetSequences
 */
void CL_Sequence2D (void)
{
	seq2D_t *s2d;
	int i, j;
	int height = 0;

	/* add texts */
	for (i = 0, s2d = seq2Ds; i < numSeq2Ds; i++, s2d++)
		if (s2d->inuse) {
			if (s2d->relativePos && height > 0) {
				s2d->pos[1] += height;
				s2d->relativePos = qfalse;
			}
			/* advance in time */
			for (j = 0; j < 4; j++) {
				s2d->color[j] += cls.frametime * s2d->fade[j];
				if (s2d->color[j] < 0.0)
					s2d->color[j] = 0.0;
				else if (s2d->color[j] > 1.0)
					s2d->color[j] = 1.0;
			}
			for (j = 0; j < 2; j++) {
				s2d->pos[j] += cls.frametime * s2d->speed[j];
				s2d->size[j] += cls.frametime * s2d->enlarge[j];
			}

			/* outside the screen? */
			/* FIXME: We need this check - but this does not work */
			/*if (s2d->pos[1] >= VID_NORM_HEIGHT || s2d->pos[0] >= VID_NORM_WIDTH)
				continue;*/

			/* render */
			R_Color(s2d->color);

			/* image can be background */
			if (*s2d->image)
				R_DrawNormPic(s2d->pos[0], s2d->pos[1], s2d->size[0], s2d->size[1], 0, 0, 0, 0, s2d->align, qtrue, s2d->image);

			/* bgcolor can be overlay */
			if (s2d->bgcolor[3] > 0.0)
				R_DrawFill(s2d->pos[0], s2d->pos[1], s2d->size[0], s2d->size[1], s2d->align, s2d->bgcolor);

			/* render */
			R_Color(s2d->color);

			/* gettext placeholder */
			if (*s2d->text)
				height += R_FontDrawString(s2d->font, s2d->align, s2d->pos[0], s2d->pos[1], s2d->pos[0], s2d->pos[1], (int) s2d->size[0], (int) s2d->size[1], -1 /* @todo: use this for some nice line spacing */, _(s2d->text), 0, 0, NULL, qfalse);
		}
	R_Color(NULL);
}

/**
 * @brief Unlock a click event for the current sequence or ends the current sequence if not locked
 * @note Script binding for seq_click
 * @sa menu sequence in menu_main.ufo
 */
void CL_SequenceClick_f (void)
{
	if (seqLocked) {
		seqEndClickLoop = qtrue;
		seqLocked = qfalse;
	} else
		MN_PopMenu(qfalse);
}

/**
 * @brief Start a sequence
 * @sa CL_SequenceEnd_f
 */
void CL_SequenceStart_f (void)
{
	sequence_t *sp;
	const char *name, *menuName;
	int i;
	menu_t* menu;

	if (Cmd_Argc() < 2) {
		Com_Printf("Usage: %s <name> [<menu>]\n", Cmd_Argv(0));
		return;
	}
	name = Cmd_Argv(1);

	/* find sequence */
	for (i = 0, sp = sequences; i < numSequences; i++, sp++)
		if (!Q_strncmp(name, sp->name, MAX_VAR))
			break;
	if (i >= numSequences) {
		Com_Printf("Couldn't find sequence '%s'\n", name);
		return;
	}

	/* display the sequence menu */
	/* the default is in menu_main.ufo - menu sequence */
	menuName = Cmd_Argc() < 3 ? mn_sequence->string : Cmd_Argv(2);
	menu = MN_PushMenu(menuName);
	if (! menu) {
		Com_Printf("CL_SequenceStart_f: can't display menu '%s'\n", menuName);
		return;
	}

	/* init script parsing */
	numSeqEnts = 0;
	numSeq2Ds = 0;
	memset(&seqCamera, 0, sizeof(seqCamera_t));
	seqTime = cl.time;
	seqCmd = sp->start;
	seqEndCmd = sp->start + sp->length;

	/* init sequence state */
	CL_SetClientState(ca_sequence);

	/* init sun */
	VectorSet(map_sun.dir, 2, 2, 3);
	VectorSet(map_sun.ambient, 1.6, 1.6, 1.6);
	map_sun.ambient[3] = 5.4;
	VectorSet(map_sun.color, 1.2, 1.2, 1.2);
	map_sun.color[3] = 1.0;
}


void CL_ResetSequences (void)
{
	/* reset counters */
	seq_animspeed = Cvar_Get("seq_animspeed", "1000", 0, NULL);
	numSequences = 0;
	numSeqCmds = 0;
	numSeqEnts = 0;
	numSeq2Ds = 0;
	seqLocked = qfalse;
}


/* =========================================================== */

/** @brief valid id names for camera */
static const value_t seqCamera_vals[] = {
	{"origin", V_VECTOR, offsetof(seqCamera_t, origin), MEMBER_SIZEOF(seqCamera_t, origin)},
	{"speed", V_VECTOR, offsetof(seqCamera_t, speed), MEMBER_SIZEOF(seqCamera_t, speed)},
	{"angles", V_VECTOR, offsetof(seqCamera_t, angles), MEMBER_SIZEOF(seqCamera_t, angles)},
	{"omega", V_VECTOR, offsetof(seqCamera_t, omega), MEMBER_SIZEOF(seqCamera_t, omega)},
	{"dist", V_FLOAT, offsetof(seqCamera_t, dist), MEMBER_SIZEOF(seqCamera_t, dist)},
	{"ddist", V_FLOAT, offsetof(seqCamera_t, dist), MEMBER_SIZEOF(seqCamera_t, dist)},
	{"zoom", V_FLOAT, offsetof(seqCamera_t, zoom), MEMBER_SIZEOF(seqCamera_t, zoom)},
	{"dzoom", V_FLOAT, offsetof(seqCamera_t, dist), MEMBER_SIZEOF(seqCamera_t, dist)},
	{NULL, 0, 0, 0}
};

/** @brief valid entity names for a sequence */
static const value_t seqEnt_vals[] = {
	{"name", V_STRING, offsetof(seqEnt_t, name), 0},
	{"skin", V_INT, offsetof(seqEnt_t, skin), MEMBER_SIZEOF(seqEnt_t, skin)},
	{"alpha", V_FLOAT, offsetof(seqEnt_t, alpha), MEMBER_SIZEOF(seqEnt_t, alpha)},
	{"origin", V_VECTOR, offsetof(seqEnt_t, origin), MEMBER_SIZEOF(seqEnt_t, origin)},
	{"speed", V_VECTOR, offsetof(seqEnt_t, speed), MEMBER_SIZEOF(seqEnt_t, speed)},
	{"angles", V_VECTOR, offsetof(seqEnt_t, angles), MEMBER_SIZEOF(seqEnt_t, angles)},
	{"omega", V_VECTOR, offsetof(seqEnt_t, omega), MEMBER_SIZEOF(seqEnt_t, omega)},
	{"parent", V_STRING, offsetof(seqEnt_t, parent), 0},
	{"tag", V_STRING, offsetof(seqEnt_t, tag), 0},
	{NULL, 0, 0, 0}
};

/** @brief valid id names for 2d entity */
static const value_t seq2D_vals[] = {
	{"name", V_STRING, offsetof(seq2D_t, name), 0},
	{"text", V_TRANSLATION_MANUAL_STRING, offsetof(seq2D_t, text), 0},
	{"font", V_STRING, offsetof(seq2D_t, font), 0},
	{"image", V_STRING, offsetof(seq2D_t, image), 0},
	{"pos", V_POS, offsetof(seq2D_t, pos), MEMBER_SIZEOF(seq2D_t, pos)},
	{"speed", V_POS, offsetof(seq2D_t, speed), MEMBER_SIZEOF(seq2D_t, speed)},
	{"size", V_POS, offsetof(seq2D_t, size), MEMBER_SIZEOF(seq2D_t, size)},
	{"enlarge", V_POS, offsetof(seq2D_t, enlarge), MEMBER_SIZEOF(seq2D_t, enlarge)},
	{"bgcolor", V_COLOR, offsetof(seq2D_t, bgcolor), MEMBER_SIZEOF(seq2D_t, bgcolor)},
	{"color", V_COLOR, offsetof(seq2D_t, color), MEMBER_SIZEOF(seq2D_t, color)},
	{"fade", V_COLOR, offsetof(seq2D_t, fade), MEMBER_SIZEOF(seq2D_t, fade)},
	{"align", V_ALIGN, offsetof(seq2D_t, align), MEMBER_SIZEOF(seq2D_t, align)},
	{"relative", V_BOOL, offsetof(seq2D_t, relativePos), MEMBER_SIZEOF(seq2D_t, relativePos)},
	{NULL, 0, 0, 0},
};

/**
 * @brief Wait until someone clicks with the mouse
 * @return 0 if you wait for the click
 * @return 1 if the click occured
 */
int SEQ_Click (const char *name, char *data)
{
	/* if a CL_SequenceClick_f event was called */
	if (seqEndClickLoop) {
		seqEndClickLoop = qfalse;
		seqLocked = qfalse;
		/* increase the command counter by 1 */
		return 1;
	}
	seqTime += 1000;
	seqLocked = qtrue;
	/* don't increase the command counter - stay at click command */
	return 0;
}

/**
 * @brief Increase the sequence time
 * @return 1 - increase the command position of the sequence by one
 */
int SEQ_Wait (const char *name, char *data)
{
	seqTime += 1000 * atof(name);
	return 1;
}

/**
 * @brief Precaches the models and images for a sequence
 * @return 1 - increase the command position of the sequence by one
 * @sa R_RegisterModelShort
 * @sa R_RegisterPic
 */
int SEQ_Precache (const char *name, char *data)
{
	if (!Q_strncmp(name, "models", 6)) {
		while (*data) {
			Com_DPrintf(DEBUG_CLIENT, "Precaching model: %s\n", data);
			R_RegisterModelShort(data);
			data += strlen(data) + 1;
		}
	} else if (!Q_strncmp(name, "pics", 4)) {
		while (*data) {
			Com_DPrintf(DEBUG_CLIENT, "Precaching image: %s\n", data);
			R_RegisterPic(data);
			data += strlen(data) + 1;
		}
	} else
		Com_Printf("SEQ_Precache: unknown format '%s'\n", name);
	return 1;
}

/**
 * @brief Parse the values for the camera like given in seqCamera
 */
int SEQ_Camera (const char *name, char *data)
{
	const value_t *vp;

	/* get values */
	while (*data) {
		for (vp = seqCamera_vals; vp->string; vp++)
			if (!Q_strcmp(data, vp->string)) {
				data += strlen(data) + 1;
				Com_ParseValue(&seqCamera, data, vp->type, vp->ofs, vp->size);
				break;
			}
		if (!vp->string)
			Com_Printf("SEQ_Camera: unknown token '%s'\n", data);

		data += strlen(data) + 1;
	}
	return 1;
}

/**
 * @brief Parse values for a sequence model
 * @return 1 - increase the command position of the sequence by one
 * @sa seqEnt_vals
 * @sa CL_SequenceFindEnt
 */
int SEQ_Model (const char *name, char *data)
{
	seqEnt_t *se;
	const value_t *vp;
	int i;

	/* get sequence entity */
	se = CL_SequenceFindEnt(name);
	if (!se) {
		/* create new sequence entity */
		for (i = 0, se = seqEnts; i < numSeqEnts; i++, se++)
			if (!se->inuse)
				break;
		if (i >= numSeqEnts) {
			if (numSeqEnts >= MAX_SEQENTS)
				Com_Error(ERR_FATAL, "Too many sequence entities");
			se = &seqEnts[numSeqEnts++];
		}
		/* allocate */
		memset(se, 0, sizeof(seqEnt_t));
		se->inuse = qtrue;
		Com_sprintf(se->name, MAX_VAR, name);
	}

	/* get values */
	while (*data) {
		for (vp = seqEnt_vals; vp->string; vp++)
			if (!Q_strcmp(data, vp->string)) {
				data += strlen(data) + 1;
				Com_ParseValue(se, data, vp->type, vp->ofs, vp->size);
				break;
			}
		if (!vp->string) {
			if (!Q_strncmp(data, "model", 5)) {
				data += strlen(data) + 1;
				Com_DPrintf(DEBUG_CLIENT, "Registering model: %s\n", data);
				se->model = R_RegisterModelShort(data);
			} else if (!Q_strncmp(data, "anim", 4)) {
				data += strlen(data) + 1;
				Com_DPrintf(DEBUG_CLIENT, "Change anim to: %s\n", data);
				R_AnimChange(&se->as, se->model, data);
			} else
				Com_Printf("SEQ_Model: unknown token '%s'\n", data);
		}

		data += strlen(data) + 1;
	}
	return 1;
}

/**
 * @brief Parse 2D objects like text and images
 * @return 1 - increase the command position of the sequence by one
 * @sa seq2D_vals
 * @sa CL_SequenceFind2D
 */
int SEQ_2Dobj (const char *name, char *data)
{
	seq2D_t *s2d;
	const value_t *vp;
	int i;

	/* get sequence text */
	s2d = CL_SequenceFind2D(name);
	if (!s2d) {
		/* create new sequence text */
		for (i = 0, s2d = seq2Ds; i < numSeq2Ds; i++, s2d++)
			if (!s2d->inuse)
				break;
		if (i >= numSeq2Ds) {
			if (numSeq2Ds >= MAX_SEQ2DS)
				Com_Error(ERR_FATAL, "Too many sequence 2d objects");
			s2d = &seq2Ds[numSeq2Ds++];
		}
		/* allocate */
		memset(s2d, 0, sizeof(seq2D_t));
		for (i = 0; i < 4; i++)
			s2d->color[i] = 1.0f;
		s2d->inuse = qtrue;
		Q_strncpyz(s2d->font, "f_big", sizeof(s2d->font));	/* default font */
		Q_strncpyz(s2d->name, name, sizeof(s2d->name));
	}

	/* get values */
	while (*data) {
		for (vp = seq2D_vals; vp->string; vp++)
			if (!Q_strcmp(data, vp->string)) {
				data += strlen(data) + 1;
				Com_ParseValue(s2d, data, vp->type, vp->ofs, vp->size);
				break;
			}
		if (!vp->string)
			Com_Printf("SEQ_Text: unknown token '%s'\n", data);

		data += strlen(data) + 1;
	}
	return 1;
}

/**
 * @brief Removed a sequence entity from the current sequence
 * @return 1 - increase the command position of the sequence by one
 * @sa CL_SequenceFind2D
 * @sa CL_SequenceFindEnt
 */
int SEQ_Remove (const char *name, char *data)
{
	seqEnt_t *se;
	seq2D_t *s2d;

	se = CL_SequenceFindEnt(name);
	if (se)
		se->inuse = qfalse;

	s2d = CL_SequenceFind2D(name);
	if (s2d)
		s2d->inuse = qfalse;

	if (!se && !s2d)
		Com_Printf("SEQ_Remove: couldn't find '%s'\n", name);
	return 1;
}

/**
 * @brief Executes a sequence command
 * @return 1 - increase the command position of the sequence by one
 * @sa Cbuf_AddText
 */
int SEQ_Command (const char *name, char *data)
{
	/* add the command */
	Cbuf_AddText(name);
	return 1;
}

/**
 * @brief Reads the sequence values from given text-pointer
 * @sa CL_ParseClientData
 */
void CL_ParseSequence (const char *name, const char **text)
{
	const char *errhead = "CL_ParseSequence: unexpected end of file (sequence ";
	sequence_t *sp;
	seqCmd_t *sc;
	const char *token;
	char *data;
	int i, depth, maxLength;

	/* search for sequences with same name */
	for (i = 0; i < numSequences; i++)
		if (!Q_strncmp(name, sequences[i].name, MAX_VAR))
			break;

	if (i < numSequences) {
		Com_Printf("CL_ParseSequence: sequence def \"%s\" with same name found, second ignored\n", name);
		return;
	}

	/* initialize the sequence */
	if (numSequences >= MAX_SEQUENCES)
		Com_Error(ERR_FATAL, "Too many sequences");

	sp = &sequences[numSequences++];
	memset(sp, 0, sizeof(sequence_t));
	Com_sprintf(sp->name, MAX_VAR, name);
	sp->start = numSeqCmds;

	/* get it's body */
	token = COM_Parse(text);

	if (!*text || *token != '{') {
		Com_Printf("CL_ParseSequence: sequence def \"%s\" without body ignored\n", name);
		numSequences--;
		return;
	}

	do {
		token = COM_EParse(text, errhead, name);
		if (!*text)
			break;
	next_cmd:
		if (*token == '}')
			break;

		/* check for commands */
		for (i = 0; i < SEQ_NUMCMDS; i++)
			if (!Q_strcmp(token, seqCmdName[i])) {
				maxLength = MAX_DATA_LENGTH;
				/* found a command */
				token = COM_EParse(text, errhead, name);
				if (!*text)
					return;

				if (numSeqCmds >= MAX_SEQCMDS)
					Com_Error(ERR_FATAL, "Too many sequence commands");

				/* init seqCmd */
				sc = &seqCmds[numSeqCmds++];
				memset(sc, 0, sizeof(seqCmd_t));
				sc->handler = seqCmdFunc[i];
				sp->length++;

				/* copy name */
				Com_sprintf(sc->name, MAX_VAR, token);

				/* read data */
				token = COM_EParse(text, errhead, name);
				if (!*text)
					return;
				if (*token != '{')
					goto next_cmd;

				depth = 1;
				data = &sc->data[0];
				while (depth) {
					if (maxLength <= 0) {
						Com_Printf("Too much data for sequence %s", sc->name);
						break;
					}
					token = COM_EParse(text, errhead, name);
					if (!*text)
						return;

					if (*token == '{')
						depth++;
					else if (*token == '}')
						depth--;
					if (depth) {
						Com_sprintf(data, maxLength, token);
						data += strlen(token) + 1;
						maxLength -= (strlen(token) + 1);
					}
				}
				break;
			}

		if (i == SEQ_NUMCMDS) {
			Com_Printf("CL_ParseSequence: unknown command \"%s\" ignored (sequence %s)\n", token, name);
			COM_EParse(text, errhead, name);
		}
	} while (*text);
}
