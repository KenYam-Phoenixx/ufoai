/**
 * @file
 * @brief Campaign mission header
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

#pragma once

extern const int STARTING_BASEBUILD_INTEREST;

bool CP_BasemissionIsSubvertingGovernmentMission(const struct mission_s* mission);
void CP_BuildBaseMissionNextStage(const campaign_t* campaign, struct mission_s* mission);
void CP_BuildBaseMissionIsFailure(struct mission_s* mission);
void CP_BuildBaseMissionBaseDestroyed(struct mission_s* mission);
void CP_BuildBaseMissionIsSuccess(struct mission_s* mission);
void CP_BuildBaseMissionOnSpawn(void);
