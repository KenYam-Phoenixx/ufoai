/**
 * @file
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

#include "../../../../client.h"
#include "../../../../ui/ui_main.h"
#include "../../../cl_localentity.h"
#include "../../../cl_actor.h"
#include "../../../cl_hud.h"
#include "e_event_actorstatechange.h"

void CL_ActorStateChange (const eventRegister_t* self, dbuffer* msg)
{
	int entnum, state;
	NET_ReadFormat(msg, self->formatString, &entnum, &state);

	le_t* le = LE_Get(entnum);
	if (!le)
		LE_NotFoundError(entnum);

	if (!LE_IsActor(le)) {
		Com_Printf("StateChange message ignored... LE is no actor (number: %i, state: %i, type: %i)\n",
			entnum, state, le->type);
		return;
	}

	/* If standing up or crouching down remove the reserved-state for crouching. */
	if (((state & STATE_CROUCHED) && !LE_IsCrouched(le)) ||
		 (!(state & STATE_CROUCHED) && LE_IsCrouched(le))) {
		if (CL_ActorUsableTUs(le) < TU_CROUCH && CL_ActorReservedTUs(le, RES_CROUCH) >= TU_CROUCH) {
			/* We have not enough non-reserved TUs,
			 * but some reserved for crouching/standing up.
			 * i.e. we only reset the reservation for crouching if it's the very last attempt. */
			CL_ActorReserveTUs(le, RES_CROUCH, 0); /* Reset reserved TUs (0 TUs) */
		}
	}

	/* killed by the server: no animation is played, etc. */
	if ((state & STATE_DEAD) && LE_IsLivingActor(le)) {
		if ((state & STATE_STUN) != (le->state & STATE_STUN))
			CL_ActorPlaySound(le, SND_DEATH);
		le->state = state;
		le->resetFloor();
		LE_SetThink(le, nullptr);
		le->aabb.setMaxs(player_dead_maxs);
		CL_ActorRemoveFromTeamList(le);
		return;
	}

	character_t* chr = CL_ActorGetChr(le);
	if (chr) { /* Print some informative messages */
		if (le->team == cls.team) {
			if ((state & STATE_DAZED) && !LE_IsDazed(le))
				HUD_DisplayMessage(va(_("%s is dazed!\nEnemy used flashbang!"), chr->name));

			if ((state & STATE_PANIC) && !LE_IsPanicked(le))
				HUD_DisplayMessage(va(_("%s panics!"), chr->name));

			if ((state & STATE_RAGE) && !LE_IsRaged(le)) {
				if ((state & STATE_INSANE)) {
					HUD_DisplayMessage(va(_("%s is consumed by mad rage!"), chr->name));
				} else {
					HUD_DisplayMessage(va(_("%s is on a rampage!"), chr->name));
				}
			}

			if ((state & STATE_SHAKEN) && !LE_IsShaken(le))
				HUD_DisplayMessage(va(_("%s is currently shaken."), chr->name));
		}
	}

	le->state = state;
	LE_SetThink(le, LET_StartIdle);

	/* save those states that the actor should also carry over to other missions */
	if (chr)
		chr->state = (le->state & STATE_REACTION);

	/* state change may have affected move length */
	CL_ActorConditionalMoveCalc(le);
}
