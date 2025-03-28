/**
 * @file
 * @brief Campaign statistic headers
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

typedef struct stats_s {
	int missions;				/**< number of all missions ever (used for unique idx generation) */
	int missionsWon;
	int missionsLost;
	int basesBuilt;
	int basesAttacked;
	int installationsBuilt;
	int interceptions;
	int soldiersLost;
	int soldiersNew;			/**< new recruits */
	int killedAliens;
	int capturedAliens;			/**< 'alive' aliens captured - the other ones count as killed */
	int rescuedCivilians;
	int researchedTechnologies;
	int moneyInterceptions;
	int moneyBases;
	int moneyResearch;
	int moneyWeapons;
	int ufosDetected;
	int alienBasesBuilt;		/**< number of all alien bases ever built (used for unique idx generation) */
	int ufosStored;				/**< number of UFOS ever stored in UFO Yards (used for unique idx generation) */
	int aircraftHad;			/**< number of PHALANX aircraft ever bought/produced (used for unique idx generation) */
} stats_t;

void STATS_InitStartup(void);
void STATS_ShutDown(void);
