/**
 * @file cl_transfer.h
 * @brief Header file for Transfer stuff.
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

#ifndef CLIENT_CL_TRANSFER_H
#define CLIENT_CL_TRANSFER_H

#define MAX_TRANSFERS	16
#define TRANS_LIST_EMPTY_SLOT -1

enum {
	CARGO_TYPE_INVALID = 0,
	CARGO_TYPE_ITEM,
	CARGO_TYPE_EMPLOYEE,
	CARGO_TYPE_ALIEN_DEAD,
	CARGO_TYPE_ALIEN_ALIVE,
	CARGO_TYPE_AIRCRAFT,

	CARGO_TYPE_MAX
};

enum {
	TRANS_TYPE_INVALID = -1,
	TRANS_TYPE_ITEM,
	TRANS_TYPE_EMPLOYEE,
	TRANS_TYPE_ALIEN,
	TRANS_TYPE_AIRCRAFT,

	TRANS_TYPE_MAX
};

enum {
	TRANS_ALIEN_ALIVE,
	TRANS_ALIEN_DEAD,

	TRANS_ALIEN_MAX
};

/** @brief Transfer informations (they are being stored in gd.alltransfers[MAX_TRANSFERS]. */
typedef struct transfer_s {
	int itemAmount[MAX_OBJDEFS];			/**< Amount of given item [csi.ods[idx]]. */
	int alienAmount[MAX_TEAMDEFS][TRANS_ALIEN_MAX];		/**< Alien cargo, [0] alive, [1] dead. */
	int employeesArray[MAX_EMPL][MAX_EMPLOYEES];	/**< Array of indexes of personel transfering. */
	int aircraftArray[MAX_AIRCRAFT];		/**< Aircraft being transferred. */
	int destBase;					/**< Index of destination base. */
	int srcBase;					/**< Intex of source base. */
	date_t event;					/**< When the transfer finish process should start. */
	qboolean active;				/**< True if this transfer is under processing. */
	qboolean hasItems;				/**< Transfer of items. */
	qboolean hasEmployees;			/**< Transfer of employees. */
	qboolean hasAliens;				/**< Transfer of Aliens. */
	qboolean hasAircraft;			/**< Transfer of aircraft. */
} transfer_t;

/** @brief Array of current cargo onboard. */
typedef struct transferCargo_s {
	int type;			/**< Type of cargo (1 - items, 2 - employees, 3 - alien bodies, 4 - alive aliens). */
	int itemidx;			/**< Index of item in cargo. */
} transferCargo_t;

void TR_TransferAircraftMenu(aircraft_t* aircraft);
void TR_TransferEnd(transfer_t *transfer);
void TR_EmptyTransferCargo(transfer_t *transfer, qboolean success);
void TR_TransferCheck(void);

void TR_Reset(void);

#endif /* CLIENT_CL_TRANSFER_H */
