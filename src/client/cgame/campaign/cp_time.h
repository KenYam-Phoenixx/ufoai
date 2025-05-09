/**
 * @file
 * @brief Campaign geoscape time header
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

#include "../../DateTime.h"
#include "../../cl_shared.h"

/**
 * @brief Human readable time information in the game.
 * @note Use this on runtime - please avoid for structs that get saved.
 * @sa CP_DateConvertLong
 */
typedef struct dateLong_s {
	short year;	/**< Year in yyyy notation. */
	byte month;	/**< Number of month (starting with 1). */
	byte day;	/**< Number of day (starting with 1). */
	byte hour;	/**< Hour of the day. @todo check what number-range this gives) */
	byte min;	/**< Minute of the hour. */
	byte sec;	/**< Second of the minute. */
} dateLong_t;

void CP_DateConvertLong(const DateTime& date, dateLong_t* dateLong);
const char* CP_SecondConvert(int second);

void CP_UpdateTime(void);
void CP_GameTimeStop(void);
bool CP_IsTimeStopped(void);
void CP_GameTimeFast(void);
void CP_GameTimeSlow(void);
void CP_SetGameTime_f(void);

int Date_DateToSeconds(const DateTime& date);
DateTime Date_Random(const DateTime& minFrame, const DateTime& maxFrame);
const char* Date_GetMonthName(int month);
