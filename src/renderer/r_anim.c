/**
 * @file r_anim.c
 * @brief animation parsing and playing
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

#include "r_local.h"
#define LNEXT(x)	((x+1 < MAX_ANIMLIST) ? x+1 : 0)

/**
 * @sa R_AnimChange
 * @sa R_AnimAppend
 * @sa R_AnimRun
 * @sa R_AnimGetName
 */
static mAliasAnim_t *R_AnimGet (model_t * mod, const char *name)
{
	mAliasAnim_t *anim;
	int i;

	if (!mod || mod->type != mod_alias_md2)
		return NULL;

	for (i = 0, anim = mod->alias.animdata; i < mod->alias.numanims; i++, anim++)
		if (!Q_strncmp(name, anim->name, MAX_ANIMNAME))
			return anim;

	Com_Printf("model \"%s\" doesn't have animation \"%s\"\n", mod->name, name);
	return NULL;
}


/**
 * @sa R_AnimGet
 * @sa R_AnimChange
 * @sa R_AnimRun
 * @sa R_AnimGetName
 */
void R_AnimAppend (animState_t * as, model_t * mod, const char *name)
{
	mAliasAnim_t *anim;

	assert(as->ladd < MAX_ANIMLIST);

	if (!mod || mod->type != mod_alias_md2)
		return;

	/* get animation */
	anim = R_AnimGet(mod, name);
	if (!anim)
		return;

	if (as->lcur == as->ladd) {
		/* first animation */
		as->oldframe = anim->from;
		if (anim->to > anim->from)
			as->frame = anim->from + 1;
		else
			as->frame = anim->from;

		as->backlerp = 0.0;
		as->time = anim->time;
		as->dt = 0;

		as->list[as->ladd] = anim - mod->alias.animdata;
	} else {
		/* next animation */
		as->list[as->ladd] = anim - mod->alias.animdata;
	}

	/* advance in list (no overflow protection!) */
	as->ladd = LNEXT(as->ladd);
}


/**
 * @brief Changes the animation for md2 models
 * @sa R_AnimGet
 * @sa R_AnimAppend
 * @sa R_AnimRun
 * @sa R_AnimGetName
 * @param[in] as Client side animation state of the model
 * @param[in] mod Model structure to change the animation for (md2/mod_alias_md2)
 * @param[in] name Animation state name to switch to
 */
void R_AnimChange (animState_t * as, model_t * mod, const char *name)
{
	mAliasAnim_t *anim;

	assert(as->ladd < MAX_ANIMLIST);

	if (!mod || mod->type != mod_alias_md2) {
		Com_Printf("R_AnimChange: No md2 model - can't set animation (%s) (model: %s)\n", name, mod ? mod->name : "(null)");
		return;
	}

	/* get animation */
	anim = R_AnimGet(mod, name);
	if (!anim) {
		Com_Printf("R_AnimChange: No such animation: %s (model: %s)\n", name, mod->name);
		return;
	}

	if (as->lcur == as->ladd) {
		/* first animation */
		as->oldframe = anim->from;
		if (anim->to > anim->from)
			as->frame = anim->from + 1;
		else
			as->frame = anim->from;

		as->backlerp = 1.0;
		as->time = anim->time;
		as->dt = 0;

		as->list[as->ladd] = anim - mod->alias.animdata;
	} else {
		/* don't change to same animation */
/*		if (anim == mod->animdata + as->list[as->lcur])
			return; */

		/* next animation */
		as->ladd = LNEXT(as->lcur);
		as->list[as->ladd] = anim - mod->alias.animdata;

		if (anim->time < as->time)
			as->time = anim->time;
	}

	/* advance in list (no overflow protection!) */
	as->ladd = LNEXT(as->ladd);
	as->change = qtrue;
}


/**
 * @brief Run the animation of the given md2 model
 * @sa R_AnimGet
 * @sa R_AnimAppend
 * @sa R_AnimChange
 * @sa R_AnimGetName
 */
void R_AnimRun (animState_t * as, model_t * mod, int msec)
{
	mAliasAnim_t *anim;

	assert(as->lcur < MAX_ANIMLIST);

	if (!mod || mod->type != mod_alias_md2)
		return;

	if (as->lcur == as->ladd)
		return;

	as->dt += msec;

	while (as->dt > as->time) {
		as->dt -= as->time;
		anim = mod->alias.animdata + as->list[as->lcur];

		if (as->change || as->frame >= anim->to) {
			/* go to next animation if it isn't the last one */
			if (LNEXT(as->lcur) != as->ladd)
				as->lcur = LNEXT(as->lcur);

			anim = mod->alias.animdata + as->list[as->lcur];

			/* prepare next frame */
			as->dt = 0;
			as->time = anim->time;
			as->oldframe = as->frame;
			as->frame = anim->from;
			as->change = qfalse;
		} else {
			/* next frame of the same animation */
			as->time = anim->time;
			as->oldframe = as->frame;
			as->frame++;
		}
	}

	as->backlerp = 1.0 - (float) as->dt / as->time;
}


/**
 * @sa R_AnimGet
 * @sa R_AnimAppend
 * @sa R_AnimRun
 * @sa R_AnimChange
 */
char *R_AnimGetName (animState_t * as, model_t * mod)
{
	mAliasAnim_t *anim;

	assert(as->lcur < MAX_ANIMLIST);

	if (!mod || mod->type != mod_alias_md2)
		return NULL;

	if (as->lcur == as->ladd)
		return NULL;

	anim = mod->alias.animdata + as->list[as->lcur];
	return anim->name;
}
