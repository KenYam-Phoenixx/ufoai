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
#include "../../../cl_view.h"
#include "e_event_centerview.h"

void CL_CenterView (const eventRegister_t* self, dbuffer* msg)
{
	pos3_t pos;

	NET_ReadFormat(msg, self->formatString, &pos);
	CL_ViewCenterAtGridPosition(pos);
}

void CL_MoveView (const eventRegister_t* self, dbuffer* msg)
{
	pos3_t pos, from;

	NET_ReadFormat(msg, self->formatString, &pos);
	VecToPos(cl.cam.origin, from);
	CL_CheckCameraRoute(from, pos);
}
