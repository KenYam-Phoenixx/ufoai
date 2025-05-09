/**
 * @file
 * @brief XML tag constants for savegame.
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

#define SAVE_TRANSFER_TRANSFERS "transfers"
#define SAVE_TRANSFER_TRANSFER "transfer"
#define SAVE_TRANSFER_DESTBASE "destBase"
#define SAVE_TRANSFER_SRCBASE "srcBase"
#define SAVE_TRANSFER_DAY "day"
#define SAVE_TRANSFER_SEC "sec"

/** @todo Remove: Fallback records for compatibility. */
#define SAVE_TRANSFER_ITEM "item"
#define SAVE_TRANSFER_ITEMID "itemid"
#define SAVE_TRANSFER_AMOUNT "amount"

#define SAVE_TRANSFER_ALIENCARGO "alienCargo"
#define SAVE_TRANSFER_ITEMCARGO "itemCargo"

#define SAVE_TRANSFER_EMPLOYEE "employee"
#define SAVE_TRANSFER_UCN "UCN"

#define SAVE_TRANSFER_AIRCRAFT "aircraft"
#define SAVE_TRANSFER_ID "id"

#define SAVE_TRANSFER_ANTIMATTER "antimatter"
#define SAVE_TRANSFER_ANTIMATTER_AMOUNT "amount"

/*
DTD:

<!ELEMENT transfers transfer*>

<!ELEMENT transfer item* alien* employee* aircraft* antimatter*>
<!ATTLIST transfer
	destBase	CDATA		#REQUIRED
	srcBase		CDATA		#REQUIRED
	day			CDATA		'0'
	sec			CDATA		'0'
>

<!ELEMENT item EMPTY>
<!ATTLIST item
	itemid		CDATA		#REQUIRED
	amount		CDATA		'1'
>

<!ELEMENT alien EMPTY>
<!ATTLIST alien
	alienid		CDATA		#REQUIRED
	aliveAmount	CDATA		'0'
	deadAmount	CDATA		'0'
>

<!ELEMENT employee EMPTY>
<!ATTLIST employee
	UCN			CDATA		#REQUIRED
>

<!ELEMENT aircraft EMPTY>
<!ATTLIST aircraft
	id			CDATA		#REQUIRED
>

<!ATTLIST antimatter
	amount	CDATA		#REQUIRED
>

*/
