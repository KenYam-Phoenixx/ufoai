/**
 * @file cl_map.c
 * @brief Geoscape/Map management
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

/*
==============================================================
MULTI SELECTION DEFINITION
==============================================================
*/

#define MULTISELECT_MAXSELECT 6	/**< Maximal count of elements that can be selected at once */

/**
 * @brief Types of elements that can be selected in map
 */
typedef enum {
	MULTISELECT_TYPE_BASE,
	MULTISELECT_TYPE_MISSION,
	MULTISELECT_TYPE_AIRCRAFT,
	MULTISELECT_TYPE_UFO,
	MULTISELECT_TYPE_NONE
} multiSelectType_t;


/**
 * @brief Structure to manage the multi selection
 */
typedef struct multiSelect_s {
	int nbSelect;						/**< Count of currently selected elements */
	multiSelectType_t selectType[MULTISELECT_MAXSELECT];	/**< Type of currently selected elements */
	int selectId[MULTISELECT_MAXSELECT];	/**< Identifier of currently selected element */
	char popupText[2048];				/**< Text to display in popup_multi_selection menu */
} multiSelect_t;

static multiSelect_t multiSelect;	/**< Data to manage the multi selection */


/*
==============================================================
STATIC DEFINITION
==============================================================
*/

/* Functions */
static qboolean MAP_IsMapPositionSelected(const menuNode_t* node, vec2_t pos, int x, int y);
static void MAP3D_ScreenToMap(const menuNode_t* node, int x, int y, vec2_t pos);
static void MAP_ScreenToMap(const menuNode_t* node, int x, int y, vec2_t pos);

/* static variables */
static cvar_t* cl_showCoords;
static aircraft_t *selectedAircraft;	/**< Currently selected aircraft */
static aircraft_t *selectedUfo;			/**< Currently selected UFO */
static char text_standard[2048];		/**< Buffer to display standard text in geoscape */
static int centerOnEventIdx = 0;		/**< Current Event centered on 3D geoscape */
static vec3_t finalGlobeAngle = {0, GLOBE_ROTATE, 0};	/**< value of finale ccs.angles for a smooth change of angle (see MAP_CenterOnPoint)*/
static vec2_t final2DGeoscapeCenter = {0.5, 0.5};		/**< value of ccs.center for a smooth change of position (see MAP_CenterOnPoint) */
static qboolean smoothRotation = qfalse;				/**< qtrue if the rotation of 3D geoscape must me smooth */
static byte *terrainPic = NULL;			/**< this is the terrain mask for separating the clima
											zone and water by different color values */
static int terrainWidth, terrainHeight;		/**< the width and height for the terrain pic. */

static byte *culturePic = NULL;			/**< this is the mask for separating the culture
											zone and water by different color values */
static int cultureWidth, cultureHeight;		/**< the width and height for the culture pic. */

static byte *populationPic = NULL;			/**< this is the mask for separating the population rate
											zone and water by different color values */
static int populationWidth, populationHeight;		/**< the width and height for the population pic. */

static byte *nationsPic = NULL;			/**< this is the nation mask - separated
											by colors given in nations.ufo. */
static int nationsWidth, nationsHeight;	/**< the width and height for the nation pic. */


/*
==============================================================
CLICK ON MAP and MULTI SELECTION FUNCTIONS
==============================================================
*/

/**
 * @brief Add an element in the multiselection list
 */
static void MAP_MultiSelectListAddItem (multiSelectType_t item_type, int item_id,
	const char* item_description, const char* item_name)
{
	Q_strcat(multiSelect.popupText, va("%s\t%s\n", item_description, item_name), sizeof(multiSelect.popupText));
	multiSelect.selectType[multiSelect.nbSelect] = item_type;
	multiSelect.selectId[multiSelect.nbSelect] = item_id;
	multiSelect.nbSelect++;
}

/**
 * @brief Execute action for 1 element of the multi selection
 * Param Cmd_Argv(1) is the element selected in the popup_multi_selection menu
 */
static void MAP_MultiSelectExecuteAction_f (void)
{
	int selected, id;
	aircraft_t* aircraft;

	if (Cmd_Argc() < 2) {
		/* Direct call from code, not from a popup menu */
		selected = 0;
	} else {
		/* Call from a geoscape popup menu (popup_multi_selection) */
		MN_PopMenu(qfalse);
		selected = atoi(Cmd_Argv(1));
	}

	if (selected < 0 || selected >= multiSelect.nbSelect)
		return;
	id = multiSelect.selectId[selected];

	/* Execute action on element */
	switch (multiSelect.selectType[selected]) {
	case MULTISELECT_TYPE_BASE:	/* Select a base */
		if (id >= gd.numBases)
			break;
		MAP_ResetAction();
		Cmd_ExecuteString(va("mn_select_base %i", id));
		MN_PushMenu("bases");
		break;

	case MULTISELECT_TYPE_MISSION: /* Select a mission */
		if (id >= ccs.numMissions)
			break;

		if (gd.mapAction == MA_INTERCEPT && selMis && selMis == ccs.mission + id) {
			CL_DisplayPopupIntercept(selMis, NULL);
			return;
		}

		MAP_ResetAction();
		selMis = ccs.mission + id;

		if (selMis->def->missionType == MIS_BASEATTACK) {
			base_t* base;
			/* we need no dropship in our base */
			selMis->def->active = qtrue;
			gd.mapAction = MA_BASEATTACK;
			Com_DPrintf(DEBUG_CLIENT, "Base attack: %s at %.0f:%.0f\n", selMis->def->name, selMis->realPos[0], selMis->realPos[1]);
			base = (base_t*)selMis->def->data;
			assert(base);
			cls.missionaircraft = &base->aircraft[0]; /* FIXME */
			assert(cls.missionaircraft);
			assert(cls.missionaircraft->homebase == base);
		} else {
			Com_DPrintf(DEBUG_CLIENT, "Select mission: %s at %.0f:%.0f\n", selMis->def->name, selMis->realPos[0], selMis->realPos[1]);
			gd.mapAction = MA_INTERCEPT;
		}
		break;

	case MULTISELECT_TYPE_AIRCRAFT: /* Selection of an aircraft */
		aircraft = AIR_AircraftGetFromIdx(id);
		if (aircraft == NULL) {
			Com_DPrintf(DEBUG_CLIENT, "MAP_MultiSelectExecuteAction: selection of an unknow aircraft idx %i\n", id);
			return;
		}

		if (aircraft == selectedAircraft) {
			/* Selection of an already selected aircraft */
			CL_DisplayPopupAircraft(aircraft);	/* Display popup_aircraft */
		} else {
			/* Selection of an unselected aircraft */
			MAP_ResetAction();
			selectedAircraft = aircraft;
		}
		break;

	case MULTISELECT_TYPE_UFO : /* Selection of an UFO */
		/* Get the ufo selected */
		if (id < 0 || id >= gd.numUfos)
			return;
		aircraft = gd.ufos + id;

		if (aircraft == selectedUfo) {
			/* Selection of an already selected ufo */
			CL_DisplayPopupIntercept(NULL, selectedUfo);
		} else {
			/* Selection of an unselected ufo */
			MAP_ResetAction();
			selectedUfo = aircraft;
		}
		break;

	case MULTISELECT_TYPE_NONE :	/* Selection of an element that has been removed */
		break;
	default:
		Com_DPrintf(DEBUG_CLIENT, "MAP_MultiSelectExecuteAction: selection of an unknow element type %i\n", multiSelect.selectType[selected]);
	}
}

/**
 * @brief Notify the multi select system that a mission has been removed
 */
static void MAP_MultiSelectNotifyMissionRemoved (const actMis_t* mission)
{
	int num = mission - ccs.mission, i;

	for (i = 0; i < multiSelect.nbSelect; i++)
		if (multiSelect.selectType[i] == MULTISELECT_TYPE_MISSION) {
			if (multiSelect.selectId[i] == num)
				multiSelect.selectType[i] = MULTISELECT_TYPE_NONE;
			else if (multiSelect.selectId[i] > num)
				multiSelect.selectId[i]--;
		}
}

/**
 * @brief Notify the multi select system that an ufo has been removed
 */
static void MAP_MultiSelectNotifyUfoRemoved (const aircraft_t* ufo)
{
	int i;

	/* Deactive all ufos */
	/* @todo : Determine THE corresponding ufo in the multi select list */
	for (i = 0; i < multiSelect.nbSelect; i++)
		if (multiSelect.selectType[i] == MULTISELECT_TYPE_UFO)
			multiSelect.selectType[i] = MULTISELECT_TYPE_NONE;
}

/**
 * @brief Notify the multi select system that an ufo disapeard on radars
 */
static void MAP_MultiSelectNotifyUfoDisappeared (const aircraft_t* ufo)
{
	MAP_MultiSelectNotifyUfoRemoved(ufo);
}

#define MN_MAP_DIST_SELECTION 15

/**
 * @brief Click on the map/geoscape
 */
void MAP_MapClick (const menuNode_t* node, int x, int y)
{
	aircraft_t *aircraft = NULL;
	actMis_t *ms;
	int i;
	vec2_t pos;
	nation_t* nation;
	char clickBuffer[30];

	/* get map position */
	if (cl_3dmap->integer) {
		MAP3D_ScreenToMap(node, x, y, pos);
	} else {
		MAP_ScreenToMap(node, x, y, pos);
	}
	if (cl_showCoords->integer) {
		Com_sprintf(clickBuffer, sizeof(clickBuffer), "Long: %.1f Lat: %.1f", pos[0], pos[1]);
		MN_AddNewMessage("Click", clickBuffer, qfalse, MSG_DEBUG, NULL);
		Com_Printf("Clicked at %.1f %.1f\n", pos[0], pos[1]);
	}

	/* new base construction */
	switch (gd.mapAction) {
	case MA_NEWBASE:
		if (!MapIsWater(MAP_GetColor(pos, MAPTYPE_TERRAIN))) {
			newBasePos[0] = pos[0];
			newBasePos[1] = pos[1];
			Com_DPrintf(DEBUG_CLIENT, "MAP_MapClick: Build base at: %.0f:%.0f\n", pos[0], pos[1]);
			nation = MAP_GetNation(pos);
			if (nation)
				Com_DPrintf(DEBUG_CLIENT, "MAP_MapClick: Build base in nation '%s'\n", nation->id);
			MN_PushMenu("popup_newbase");
			return;
		} else {
			MN_AddNewMessage(_("Notice"), _("Could not set up your base at this location"), qfalse, MSG_INFO, NULL);
		}
		break;
	case MA_UFORADAR:
		MN_PushMenu("popup_intercept_ufo");
		/* if shoot down - we have a new crashsite mission if color != water */
		break;
	case MA_BASEATTACK:
		MN_PushMenu("popup_base_attack");
		break;
	default:
		break;
	}

	/* Init datas for multi selection */
	multiSelect.nbSelect = 0;
	memset(multiSelect.popupText, 0, sizeof(multiSelect.popupText));

	/* Get selected missions */
	for (i = 0, ms = ccs.mission; i < ccs.numMissions && multiSelect.nbSelect < MULTISELECT_MAXSELECT; i++, ms++)
		if (MAP_IsMapPositionSelected(node, ms->realPos, x, y))
			MAP_MultiSelectListAddItem(MULTISELECT_TYPE_MISSION, i, _(ms->def->type), _(ms->def->location));

	/* Get selected bases */
	for (i = 0; i < gd.numBases && multiSelect.nbSelect < MULTISELECT_MAXSELECT; i++) {
		if (MAP_IsMapPositionSelected(node, gd.bases[i].pos, x, y))
			MAP_MultiSelectListAddItem(MULTISELECT_TYPE_BASE, i, _("Base"), gd.bases[i].name);

		/* Get selected aircrafts wich belong to the base */
		aircraft = gd.bases[i].aircraft + gd.bases[i].numAircraftInBase - 1;
		for (; aircraft >= gd.bases[i].aircraft; aircraft--)
			if (aircraft->status > AIR_HOME && aircraft->fuel > 0 && MAP_IsMapPositionSelected(node, aircraft->pos, x, y))
				MAP_MultiSelectListAddItem(MULTISELECT_TYPE_AIRCRAFT, aircraft->idx, _("Aircraft"), aircraft->name);
	}

	/* Get selected ufos */
	for (aircraft = gd.ufos + gd.numUfos - 1; aircraft >= gd.ufos; aircraft--)
		if (aircraft->visible
#if DEBUG
		|| Cvar_VariableInteger("debug_showufos")
#endif
		)
			if (aircraft->status > AIR_HOME && MAP_IsMapPositionSelected(node, aircraft->pos, x, y))
				MAP_MultiSelectListAddItem(MULTISELECT_TYPE_UFO, aircraft - gd.ufos, _("UFO"), aircraft->name);

	if (multiSelect.nbSelect == 1) {
		/* Execute directly action for the only one element selected */
		Cmd_ExecuteString("multi_select_click");
 	} else if (multiSelect.nbSelect > 1) {
		/* Display popup for multi selection */
		menuText[TEXT_MULTISELECTION] = multiSelect.popupText;
		MN_PushMenu("popup_multi_selection");
	} else {
		/* Nothing selected */
		if (!selectedAircraft)
			MAP_ResetAction();
		else if (selectedAircraft->status > AIR_HOME && AIR_AircraftHasEnoughFuel(selectedAircraft, pos)) {
			/* Move the selected aircraft to the position clicked */
			MAP_MapCalcLine(selectedAircraft->pos, pos, &(selectedAircraft->route));
			selectedAircraft->status = AIR_TRANSIT;
			selectedAircraft->time = aircraft->point = 0;
		}
	}
}


/*
==============================================================
GEOSCAPE DRAWING AND COORDINATES
==============================================================
*/

#define MN_MAP_DIST_SELECTION 15
/**
 * @brief Tell if the specified position is considered clicked
 */
static qboolean MAP_IsMapPositionSelected (const menuNode_t* node, vec2_t pos, int x, int y)
{
	int msx, msy;

	if (MAP_AllMapToScreen(node, pos, &msx, &msy, NULL))
		if (x >= msx - MN_MAP_DIST_SELECTION && x <= msx + MN_MAP_DIST_SELECTION
		 && y >= msy - MN_MAP_DIST_SELECTION && y <= msy + MN_MAP_DIST_SELECTION)
			return qtrue;

	return qfalse;
}

#define GLOBE_RADIUS r_3dmapradius->value * (ccs.zoom / 4.0f) * 0.1
/**
 * @brief Transform a 2D position on the map to screen coordinates.
 * @param[in] pos vector that holds longitude and latitude
 * @param[out] x normalized (rotated and scaled) x value of mouseclick
 * @param[out] y normalized (rotated and scaled) y value of mouseclick
 * @param[out] z z value of the given latitude and longitude - might also be NULL if not needed
 * @sa MAP_MapToScreen
 * @sa MAP3D_ScreenToMap
 * @return qtrue if the point is visible, qfalse else (if it's outside the node or on the wrong side of earth).
 * @note In the function, we do the opposite of MAP3D_ScreenToMap
 */
static qboolean MAP_3DMapToScreen (const menuNode_t* node, const vec2_t pos, int *x, int *y, int *z)
{
	vec2_t mid;
	vec3_t v, v1, rotationAxis;
	const float radius = GLOBE_RADIUS;

	PolarToVec(pos, v);

	/* rotate the vector to switch of reference frame.
	 * We switch from the static frame of earth to the local frame of the player (opposite rotation of MAP3D_ScreenToMap) */
	VectorSet(rotationAxis, 0, 0, 1);
	RotatePointAroundVector(v1, rotationAxis, v, - ccs.angles[PITCH]);

	VectorSet(rotationAxis, 0, 1, 0);
	RotatePointAroundVector(v, rotationAxis, v1, - ccs.angles[YAW]);

	/* set mid to the coordinates of the center of the globe */
	Vector2Set(mid, (node->pos[0] + node->size[0]) / 2.0f, (node->pos[1] + node->size[1]) / 2.0f);

	/* We now convert those coordinates relative to the center of the globe to coordinates of the screen
	 * (which are relative to the upper left side of the screen) */
	*x = (int) (mid[0] - radius * v[1]);
	*y = (int) (mid[1] - radius * v[0]);
	/* FIXME: I'm not sure of what z should contain (normalized value ? sign ?) */
	if (z) {
		*z = (int) (- radius * v[2]);
	}

	/* if the point is on the wrong side of earth, the player cannot see it */
	if (v[2] > 0)
		return qfalse;

	/* if the point is outside the screen, the player cannot see it */
	if (*x < node->pos[0] && *y < node->pos[1] && *x > node->pos[0] + node->size[0] && *y > node->pos[1] + node->size[1])
		return qfalse;

	return qtrue;
}

/**
 * @brief Transform a 2D position on the map to screen coordinates.
 * @param[in] node Menu node
 * @param[in] pos Position on the map described by longitude and latitude
 * @param[out] x X coordinate on the screen
 * @param[out] y Y coordinate on the screen
 * @returns qtrue if the screen position is within the boundaries of the menu
 * node. Otherwise returns qfalse.
 * @sa MAP_3DMapToScreen
 */
static qboolean MAP_MapToScreen (const menuNode_t* node, const vec2_t pos,
		int *x, int *y)
{
	float sx;

	/* get "raw" position */
	sx = pos[0] / 360 + ccs.center[0] - 0.5;

	/* shift it on screen */
	if (sx < -0.5)
		sx += 1.0;
	else if (sx > +0.5)
		sx -= 1.0;

	*x = node->pos[0] + 0.5 * node->size[0] - sx * node->size[0] * ccs.zoom;
	*y = node->pos[1] + 0.5 * node->size[1] -
		(pos[1] / 180 + ccs.center[1] - 0.5) * node->size[1] * ccs.zoom;

	if (*x < node->pos[0] && *y < node->pos[1] &&
		*x > node->pos[0] + node->size[0] && *y > node->pos[1] + node->size[1])
		return qfalse;
	return qtrue;
}

/**
 * @brief Call either MAP_MapToScreen or MAP_3DMapToScreen depending on the geoscape you're using.
 * @param[in] node Menu node
 * @param[in] pos Position on the map described by longitude and latitude
 * @param[out] x Pointer to the X coordinate on the screen
 * @param[out] y Pointer to the Y coordinate on the screen
 * @param[out] z Pointer to the Z coordinate on the screen (may be NULL if not needed)
 * @returns qtrue if pos corresponds to a point which is visible on the screen. Otherwise returns qfalse.
 * @sa MAP_MapToScreen
 * @sa MAP_3DMapToScreen
 */
qboolean MAP_AllMapToScreen (const menuNode_t* node, const vec2_t pos, int *x, int *y, int *z)
{
	if (cl_3dmap->integer)
		return MAP_3DMapToScreen(node, pos, x, y, z);
	else {
		if (z)
			*z = 10;
		return MAP_MapToScreen(node, pos, x, y);
	}
}

/**
 * @brief Draws a 3D marker on geoscape if the player can see it.
 * @param[in] node Menu node.
 * @param[in] pos Longitude and latitude of the marker to draw.
 * @param[in] theta Angle (degree) of the model to the horizontal.
 * @param[in] model The name of the model of the marker.
 * @param[in] globe qtrue if the 3D marker is to be drawn on 3D geoscape, qfalse else.
 */
qboolean MAP_Draw3DMarkerIfVisible (const menuNode_t* node, const vec2_t pos, float theta, const char *model)
{
	int x, y, z;
	vec3_t screenPos, angles, v;
	float zoom;
	float costheta, sintheta;
	const float radius = GLOBE_RADIUS;
	qboolean test;

	test = MAP_AllMapToScreen(node, pos, &x, &y, &z);

	if (test) {
		/* Set position of the model on the screen */
		VectorSet(screenPos, x, y, z);

		if (cl_3dmap->integer) {
			/* Set angles of the model */
			VectorCopy(screenPos, v);
			v[0] -= (node->pos[0] + node->size[0]) / 2.0f;
			v[1] -= (node->pos[1] + node->size[1]) / 2.0f;

			angles[0] = theta;
			costheta = cos(angles[0] * torad);
			sintheta = sin(angles[0] * torad);

			angles[1] = 180 - asin((v[0] * costheta + v[1] * sintheta) / radius) * todeg;
			angles[2] = + asin((v[0] * sintheta - v[1] * costheta) / radius) * todeg;

			/* Set zoom */
			zoom = 0.2f + ccs.zoom * (float) z / radius / 6.0f;
		} else {
			VectorSet(angles, theta, 180, 0);
			zoom = 0.2f + ccs.zoom / 6.0f;
		}

		/* Draw */
		R_Draw3DMapMarkers(angles, zoom, screenPos, model);
		return qtrue;
	}
	return qfalse;
}

/**
 * @brief Return longitude and latitude of a point of the screen for 2D geoscape
 * @return pos vec2_t was filled with longitude and latitude
 * @param[in] x X coordinate on the screen that was clicked to
 * @param[in] y Y coordinate on the screen that was clicked to
 * @param[in] node The current menuNode we was clicking into (3dmap or map)
 */
static void MAP_ScreenToMap (const menuNode_t* node, int x, int y, vec2_t pos)
{
	pos[0] = (((node->pos[0] - x) / node->size[0] + 0.5) / ccs.zoom - (ccs.center[0] - 0.5)) * 360.0;
	pos[1] = (((node->pos[1] - y) / node->size[1] + 0.5) / ccs.zoom - (ccs.center[1] - 0.5)) * 180.0;

	while (pos[0] > 180.0)
		pos[0] -= 360.0;
	while (pos[0] < -180.0)
		pos[0] += 360.0;
}

/**
 * @brief Return longitude and latitude of a point of the screen for 3D geoscape (globe)
 * @return pos vec2_t was filled with longitude and latitude
 * @param[in] x X coordinate on the screen that was clicked to
 * @param[in] y Y coordinate on the screen that was clicked to
 * @param[in] node The current menuNode we was clicking into (3dmap or map)
 * @sa MAP_3DMapToScreen
 */
static void MAP3D_ScreenToMap (const menuNode_t* node, int x, int y, vec2_t pos)
{
	vec2_t mid;
	vec3_t v, v1, rotationAxis;
	float dist;
	const float radius = GLOBE_RADIUS;

	/* set mid to the coordinates of the center of the globe */
	Vector2Set(mid, (node->pos[0] + node->size[0]) / 2.0f, (node->pos[1] + node->size[1]) / 2.0f);

	/* stop if we click outside the globe (distance is the distance of the point to the center of the globe) */
	dist = sqrt((x - mid[0]) * (x - mid[0]) + (y - mid[1]) * (y - mid[1]));
	if (dist > radius) {
		Vector2Set(pos, -1.0, -1.0);
		return;
	}

	/* calculate the coordinates in the local frame
	 * this frame is the frame of the screen.
	 * v[0] is the vertical axis of the screen
	 * v[1] is the horizontal axis of the screen
	 * v[2] is the axis perpendicular to the screen - we get its value knowing that norm of v is egal to radius
	 * 	(because the point is on the globe) */
	v[0] = - (y - mid[1]);
	v[1] = - (x - mid[0]);
	v[2] = - sqrt(radius * radius - (x - mid[0]) * (x - mid[0]) - (y - mid[1]) * (y - mid[1]));
	VectorNormalize(v);

	/* rotate the vector to switch of reference frame */
	/* note the ccs.angles[ROLL] is always 0, so there is only 2 rotations and not 3 */
	/*	and that GLOBE_ROTATE is already included in ccs.angles[YAW] */
	/* first rotation is along the horizontal axis of the screen, to put north-south axis of the earth
	 *	perpendicular to the screen */
	VectorSet(rotationAxis, 0, 1, 0);
	RotatePointAroundVector(v1, rotationAxis, v, ccs.angles[YAW]);

	/* second rotation is to rotate the earth around its north-south axis
	 *	so that Greenwich meridian is along the vertical axis of the screen */
	VectorSet(rotationAxis, 0, 0, 1);
	RotatePointAroundVector(v, rotationAxis, v1, ccs.angles[PITCH]);

	/* we therefore got in v the coordinates of the point in the static frame of the earth
	 *	that we can convert in polar coordinates to get its latitude and longitude */
	VecToPolar(v, pos);
}

/**
 * @brief Calculate the shortest way to go from start to end on a sphere
 * @param[in] start The point you start from
 * @param[in] end The point you go to
 * @param[out] line Contains the shortest path to go from start to end
 * @sa MAP_MapDrawLine
 */
void MAP_MapCalcLine (const vec2_t start, const vec2_t end, mapline_t* line)
{
	vec3_t s, e, v;
	vec3_t normal;
	vec2_t trafo, sa, ea;
	float cosTrafo, sinTrafo;
	float phiStart, phiEnd, dPhi, phi;
	float *p, *last;
	int i, n;

	/* get plane normal */
	PolarToVec(start, s);
	PolarToVec(end, e);
	CrossProduct(s, e, normal);
	VectorNormalize(normal);

	/* get transformation */
	VecToPolar(normal, trafo);
	cosTrafo = cos(trafo[1] * torad);
	sinTrafo = sin(trafo[1] * torad);

	sa[0] = start[0] - trafo[0];
	sa[1] = start[1];
	PolarToVec(sa, s);
	ea[0] = end[0] - trafo[0];
	ea[1] = end[1];
	PolarToVec(ea, e);

	phiStart = atan2(s[1], cosTrafo * s[2] - sinTrafo * s[0]);
	phiEnd = atan2(e[1], cosTrafo * e[2] - sinTrafo * e[0]);

	/* get waypoints */
	if (phiEnd < phiStart - M_PI)
		phiEnd += 2 * M_PI;
	if (phiEnd > phiStart + M_PI)
		phiEnd -= 2 * M_PI;

	n = (phiEnd - phiStart) / M_PI * LINE_MAXSEG;
	if (n > 0)
		n = n + 1;
	else
		n = -n + 1;

	Com_DPrintf(DEBUG_CLIENT, "MAP_MapCalcLine: #(%3.1f %3.1f) -> (%3.1f %3.1f)\n", start[0], start[1], end[0], end[1]);

	line->distance = fabs(phiEnd - phiStart) / n * todeg;
	line->numPoints = n + 1;
	dPhi = (phiEnd - phiStart) / n;
	p = NULL;
	for (phi = phiStart, i = 0; i <= n; phi += dPhi, i++) {
		last = p;
		p = line->point[i];
		VectorSet(v, -sinTrafo * cos(phi), sin(phi), cosTrafo * cos(phi));
		VecToPolar(v, p);
		p[0] += trafo[0];

		if (!last) {
			while (p[0] < -180.0)
				p[0] += 360.0;
			while (p[0] > +180.0)
				p[0] -= 360.0;
		} else {
			while (p[0] - last[0] > +180.0)
				p[0] -= 360.0;
			while (p[0] - last[0] < -180.0)
				p[0] += 360.0;
		}
	}
}

/**
 * @brief Draw a path on a menu node (usually the geoscape map)
 * @param[in] node The menu node which will be used for drawing dimensions.
 * This is usually the geoscape menu node.
 * @param[in] line The path which is to be drawn
 * @sa MAP_MapCalcLine
 */
static void MAP_MapDrawLine (const menuNode_t* node, const mapline_t* line)
{
	const vec4_t color = {1, 0.5, 0.5, 1};
	screenPoint_t pts[LINE_MAXPTS];
	screenPoint_t *p;
	int i, start, old;

	/* draw */
	R_Color(color);
	start = 0;
	old = node->size[0] / 2;
	for (i = 0, p = pts; i < line->numPoints; i++, p++) {
		MAP_MapToScreen(node, line->point[i], &p->x, &p->y);

		/* If we cross longitude 180 degree (right/left edge of the screen), draw the first part of the path */
		if (i > start && abs(p->x - old) > node->size[0] / 2) {
			/* shift last point */
			int diff;

			if (p->x - old > node->size[0] / 2)
				diff = -node->size[0] * ccs.zoom;
			else
				diff = node->size[0] * ccs.zoom;
			p->x += diff;

			/* wrap around screen border */
			R_DrawLineStrip(i - start, (int*)(&pts));

			/* first path of the path is drawn, now we begin the second part of the path */
			/* shift first point, continue drawing */
			start = i - 1;
			pts[0].x = p[-1].x - diff;
			pts[0].y = p[-1].y;
			p = pts;
		}
		old = p->x;
	}

	R_DrawLineStrip(i - start, (int*)(&pts));
	R_Color(NULL);
}

/**
 * @brief Draw a path on a menu node (usually the 3Dgeoscape map)
 * @param[in] node The menu node which will be used for drawing dimensions.
 * This is usually the 3Dgeoscape menu node.
 * @param[in] line The path which is to be drawn
 * @sa MAP_MapCalcLine
 */
static void MAP_3DMapDrawLine (const menuNode_t* node, const mapline_t* line)
{
	const vec4_t color = {1, 0.5, 0.5, 1};
	screenPoint_t pts[LINE_MAXPTS];
	int i, numPoints;

	/* draw only when the point of the path is visible*/
	R_Color(color);
	for (i = 0, numPoints = 0; i < line->numPoints; i++) {
		if (MAP_3DMapToScreen(node, line->point[i], &pts[i].x, &pts[i].y, NULL)) {
			numPoints++;
		}
	}

	R_DrawLineStrip(numPoints, (int*)(&pts));
	R_Color(NULL);
}

#define CIRCLE_DRAW_POINTS	60
/**
 * @brief Draw equidistant points from a given point on a menu node
 * @param[in] node The menu node which will be used for drawing dimensions.
 * This is usually the geoscape menu node.
 * @param[in] center The latitude and longitude of center point
 * @param[in] angle The angle defining the distance of the equidistant points to center
 * @param[in] color The color for drawing
 * @param[in] globe qtrue is this is 3D geoscape, qfalse if this is 2D geoscape
 * @sa RADAR_DrawCoverage
 */
void MAP_MapDrawEquidistantPoints (const menuNode_t* node, vec2_t center, const float angle, const vec4_t color)
{
	int i, xCircle, yCircle;
	screenPoint_t pts[CIRCLE_DRAW_POINTS + 1];
	vec2_t posCircle;
	qboolean draw, oldDraw = qfalse;
	int numPoints = 0;
	vec3_t initialVector, rotationAxis, currentPoint, centerPos;

	/* Set color */
	R_Color(color);

	/* Set centerPos corresponding to cartesian coordinates of the center point */
	PolarToVec(center, centerPos);

	/* Find a perpendicular vector to centerPos, and rotate centerPos around him to obtain one point distant of angle from centerPos */
	PerpendicularVector(rotationAxis, centerPos);
	RotatePointAroundVector(initialVector, rotationAxis, centerPos, angle);

	/* Now, each equidistant point is given by a rotation around centerPos */
	for (i = 0; i <= CIRCLE_DRAW_POINTS; i++) {
		RotatePointAroundVector(currentPoint, centerPos, initialVector, i * 360 / CIRCLE_DRAW_POINTS);
		VecToPolar(currentPoint, posCircle);
		draw = qfalse;
		if (MAP_AllMapToScreen(node, posCircle, &xCircle, &yCircle, NULL)) {
			draw = qtrue;
			if (!cl_3dmap->integer && numPoints != 0 && abs(pts[numPoints - 1].x - xCircle) > 512)
				oldDraw = qfalse;
		}

		/* if moving from a point of the screen to a distant one, draw the path we already calculated, and begin a new path
		 * (to avoid unwanted lines) */
		if (draw != oldDraw && i != 0) {
			R_DrawLineStrip(numPoints, (int*)(&pts));
			numPoints = 0;
		}
		/* if the current point is to be drawn, add it to the path */
		if (draw) {
			pts[numPoints].x = xCircle;
			pts[numPoints].y = yCircle;
			numPoints++;
		}
		/* update value of oldDraw */
		oldDraw = draw;
	}

	/* Draw the last path */
	R_DrawLineStrip(numPoints, (int*)(&pts));
	R_Color(NULL);
}

/**
 * @brief Return the angle of a model given its position and destination.
 * @param[in] start Latitude and longitude of the position of the model.
 * @param[in] end Latitude and longitude of aimed point.
 * @param[in] direction vec3_t giving current direction of the model (NULL if the model is idle).
 * @param[out] ortVector If not NULL, this will be filled with the normalized vector around which rotation allows to go toward @c direction.
 * @return Angle (degrees) of rotation around the axis perpendicular to the screen for a model in @c start going toward @c end.
 */
float MAP_AngleOfPath (const vec3_t start, const vec2_t end, vec3_t direction, vec3_t ortVector)
{
	float angle = 0.0f;
	vec3_t start3D, end3D, tangentVector, v, rotationAxis;
	float dist;

	/* calculate the vector tangent to movement */
	PolarToVec(start, start3D);
	PolarToVec(end, end3D);
	if (ortVector) {
		CrossProduct(start3D, end3D, ortVector);
		VectorNormalize(ortVector);
		CrossProduct(ortVector, start3D, tangentVector);
	} else {
		CrossProduct(start3D, end3D, v);
		CrossProduct(v, start3D, tangentVector);
	}
	VectorNormalize(tangentVector);

	/* smooth change of direction if the model is not idle */
	if (direction) {
		VectorSubtract(tangentVector, direction, v);
		dist = VectorLength(v);
		if (dist > 0.01) {
			CrossProduct(direction, tangentVector, rotationAxis);
			VectorNormalize(rotationAxis);
			RotatePointAroundVector(v, rotationAxis, direction, 5.0);
			VectorCopy(v, direction);
			VectorSubtract(tangentVector, direction, v);
			if (VectorLength(v) < dist) {
				VectorCopy(direction, tangentVector);
			} else {
				VectorCopy(tangentVector, direction);
			}
		}
	}

	if (cl_3dmap->integer) {
		/* rotate vector tangent to movement in the frame of the screen */
		VectorSet(rotationAxis, 0, 0, 1);
		RotatePointAroundVector(v, rotationAxis, tangentVector, - ccs.angles[PITCH]);
		VectorSet(rotationAxis, 0, 1, 0);
		RotatePointAroundVector(tangentVector, rotationAxis, v, - ccs.angles[YAW]);
	} else {
		VectorSet(rotationAxis, 0, 0, 1);
		RotatePointAroundVector(v, rotationAxis, tangentVector, - start[0]);
		VectorSet(rotationAxis, 0, 1, 0);
		RotatePointAroundVector(tangentVector, rotationAxis, v, start[1] + 90.0f);
	}

	/* calculate the orientation angle of the model around axis perpendicular to the screen */
	angle = todeg * atan(tangentVector[0] / tangentVector[1]);
	if (tangentVector[1] > 0)
		angle += 180.0f;

	return angle;
}

/**
 * @brief Returns position of the model corresponding to centerOnEventIdx.
 * @param[out] Vector Latitude and longitude of the model (finalAngle[2] is always 0).
 * @note Vector is a vec3_t if cl_3dmap is true, and a vec2_t if cl_3dmap is false.
 * @sa MAP_CenterOnPoint
 */
static void MAP_GetGeoscapeAngle (float *Vector)
{
	int i;
	int counter = 0;
	int maxEventIdx;
	base_t *base;
	aircraft_t *aircraft;

	/* If the value of maxEventIdx is too big or to low, restart from begining */
	maxEventIdx = ccs.numMissions + gd.numBases - 1;
	for (base = gd.bases + gd.numBases - 1; base >= gd.bases ; base--) {
		for (i = 0, aircraft = (aircraft_t *) base->aircraft; i < base->numAircraftInBase; i++, aircraft++) {
			if (aircraft->status > AIR_HOME)
				maxEventIdx++;
		}
	}
	for (aircraft = gd.ufos + gd.numUfos - 1; aircraft >= gd.ufos; aircraft --) {
		if (aircraft->visible) {
			maxEventIdx++;
		}
	}
	/* check centerOnEventIdx is within the bounds */
	if (centerOnEventIdx < 0)
		centerOnEventIdx = maxEventIdx;
	if (centerOnEventIdx > maxEventIdx)
		centerOnEventIdx = 0;

	/* Cycle through missions */
	if (centerOnEventIdx < ccs.numMissions) {
		if (cl_3dmap->integer)
			VectorSet(Vector, ccs.mission[centerOnEventIdx - counter].realPos[0], -ccs.mission[centerOnEventIdx - counter].realPos[1], 0);
		else
			Vector2Set(Vector, ccs.mission[centerOnEventIdx - counter].realPos[0], ccs.mission[centerOnEventIdx - counter].realPos[1]);
		MAP_ResetAction();
		selMis = ccs.mission + centerOnEventIdx - counter;
		return;
	}
	counter += ccs.numMissions;

	/* Cycle through bases */
	if (centerOnEventIdx < gd.numBases + counter) {
		if (cl_3dmap->integer)
			VectorSet(Vector, gd.bases[centerOnEventIdx - counter].pos[0], -gd.bases[centerOnEventIdx - counter].pos[1], 0);
		else
			Vector2Set(Vector, gd.bases[centerOnEventIdx - counter].pos[0], gd.bases[centerOnEventIdx - counter].pos[1]);
		return;
	}
	counter += gd.numBases;

	/* Cycle through aircrafts (only those present on geoscape) */
	for (base = gd.bases + gd.numBases - 1; base >= gd.bases ; base--) {
		for (i = 0, aircraft = (aircraft_t *) base->aircraft; i < base->numAircraftInBase; i++, aircraft++) {
			if (aircraft->status > AIR_HOME) {
				if (centerOnEventIdx == counter) {
					if (cl_3dmap->integer)
						VectorSet(Vector, aircraft->pos[0], -aircraft->pos[1], 0);
					else
						Vector2Set(Vector, aircraft->pos[0], aircraft->pos[1]);
					MAP_ResetAction();
					selectedAircraft = aircraft;
					return;
				}
				counter++;
			}
		}
	}

	/* Cycle through UFO (only those visible on geoscape) */
	for (aircraft = gd.ufos + gd.numUfos - 1; aircraft >= gd.ufos; aircraft --) {
		if (aircraft->visible) {
			if (centerOnEventIdx == counter) {
				if (cl_3dmap->integer)
					VectorSet(Vector, aircraft->pos[0], -aircraft->pos[1], 0);
				else
					Vector2Set(Vector, aircraft->pos[0], aircraft->pos[1]);
				MAP_ResetAction();
				selectedUfo = aircraft;
				return;
			}
			counter++;
		}
	}
}

#define ZOOM_LIMIT	2.5f
/**
 * @brief Switch to next model on 2D and 3D geoscape.
 * @note Set @c smoothRotation to @c qtrue to allow a smooth rotation in MAP_DrawMap.
 * @note This function sets the value of finalGlobeAngle (for 3D) or final2DGeoscapeCenter (for 2D),
 *  which contains the finale value that ccs.angles or ccs.centre must respectively take.
 * @sa MAP_GetGeoscapeAngle
 * @sa MAP_DrawMap
 * @sa MAP3D_SmoothRotate
 * @sa MAP_SmoothTranslate
 */
void MAP_CenterOnPoint_f (void)
{
	menu_t *activeMenu = NULL;

	/* this function only concerns maps */
	activeMenu = MN_ActiveMenu();
	if (Q_strncmp(activeMenu->name, "map", 3))
		return;

	centerOnEventIdx++;

	if (cl_3dmap->integer) {
		/* case 3D geoscape */
		MAP_GetGeoscapeAngle(finalGlobeAngle);
		finalGlobeAngle[1] += GLOBE_ROTATE;
	} else {
		/* case 2D geoscape */
		MAP_GetGeoscapeAngle(final2DGeoscapeCenter);
		Vector2Set(final2DGeoscapeCenter, 0.5f - final2DGeoscapeCenter[0] / 360.0f, 0.5f - final2DGeoscapeCenter[1] / 180.0f);
		if (final2DGeoscapeCenter[1] < 0.5 / ZOOM_LIMIT)
			final2DGeoscapeCenter[1] = 0.5 / ZOOM_LIMIT;
		if (final2DGeoscapeCenter[1] > 1.0 - 0.5 / ZOOM_LIMIT)
			final2DGeoscapeCenter[1] = 1.0 - 0.5 / ZOOM_LIMIT;
	}

	smoothRotation = qtrue;
}

#define SMOOTHING_STEP	5.0f
/**
 * @brief smooth rotation of the 3D geoscape
 * @note updates slowly values of ccs.angles so that its value goes to finalGlobeAngle
 * @sa MAP_DrawMap
 * @sa MAP_CenterOnPoint
 */
static void MAP3D_SmoothRotate (void)
{
	vec3_t diff;
	float length, diff_zoom;

	diff_zoom = ZOOM_LIMIT - ccs.zoom;

	VectorSubtract(finalGlobeAngle, ccs.angles, diff);
	length = VectorLength(diff);
	if (length < SMOOTHING_STEP) {
		VectorCopy(finalGlobeAngle, ccs.angles);
		smoothRotation = qfalse;
		ccs.zoom = ZOOM_LIMIT;
	} else {
		VectorScale(diff, SMOOTHING_STEP / length, diff);
		VectorAdd(ccs.angles, diff, ccs.angles);
		ccs.zoom = ccs.zoom + SMOOTHING_STEP * diff_zoom / length;
	}
}

#define SMOOTHING_STEP_2D	0.02f
/**
 * @brief smooth translation of the 2D geoscape
 * @note updates slowly values of ccs.center so that its value goes to final2DGeoscapeCenter
 * @note updates slowly values of ccs.zoom so that its value goes to ZOOM_LIMIT
 * @sa MAP_DrawMap
 * @sa MAP_CenterOnPoint
 */
static void MAP_SmoothTranslate (void)
{
	vec2_t diff;
	float length, diff_zoom;

	diff[0] = final2DGeoscapeCenter[0] - ccs.center[0];
	diff[1] = final2DGeoscapeCenter[1] - ccs.center[1];
	diff_zoom = ZOOM_LIMIT - ccs.zoom;

	length = sqrt(diff[0] * diff[0] + diff[1] * diff[1]);
	if (length < SMOOTHING_STEP_2D) {
		ccs.center[0] = final2DGeoscapeCenter[0];
		ccs.center[1] = final2DGeoscapeCenter[1];
		ccs.zoom = ZOOM_LIMIT;
		smoothRotation = qfalse;
	} else {
		ccs.center[0] = ccs.center[0] + SMOOTHING_STEP_2D * diff[0] / length;
		ccs.center[1] = ccs.center[1] + SMOOTHING_STEP_2D * diff[1] / length;
		ccs.zoom = ccs.zoom + SMOOTHING_STEP_2D * diff_zoom / length;
	}
}

#define BULLET_SIZE	1
/**
 * @brief Draws on bunch of bullets on the geoscape map
 * @param[in] node Pointer to the node in which you want to draw the bullets.
 * @param[in] bulletIdx idx of the bullet in bulletPos array.
 * @sa MAP_DrawMap
 */
static void MAP_DrawBullets (const menuNode_t* node, int bulletIdx)
{
	int k;
	int x, y;
	const vec4_t yellow = {1.0f, 0.874f, 0.294f, 1.0f};

	for (k = 0; k < BULLETS_PER_SHOT; k++) {
		if (MAP_AllMapToScreen(node, bulletPos[bulletIdx][k], &x, &y, NULL))
			R_DrawFill(x, y, BULLET_SIZE, BULLET_SIZE, ALIGN_CC, yellow);
	}
}

#define SELECT_CIRCLE_RADIUS	1.5f + 3.0f / ccs.zoom

/**
 * @brief Draws all ufos, aircraft, bases and so on to the geoscape map (2D and 3D)
 * @param[in] node The menu node which will be used for drawing markers.
 * @sa MAP_DrawMap
 */
static void MAP_DrawMapMarkers (const menuNode_t* node)
{
	aircraft_t *aircraft;
	actMis_t *ms;
	aircraftProjectile_t *projectile;
	int x, y, i, j;
	base_t* base;
	const char* font = NULL;
	int borders[MAX_NATION_BORDERS * 2];	/**< GL_LINE_LOOP coordinates for nation borders */
	const vec2_t northPole = {0.0f, 90.0f};
	float angle = 0.0f;
	const vec4_t yellow = {1.0f, 0.874f, 0.294f, 1.0f};
	qboolean showXVI = qfalse;
	char buffer[512] = "";

	assert(node);

	/* font color on geoscape */
	R_Color(node->color);
	/* default font */
	font = MN_GetFont(NULL, node);

	/* draw mission pics */
	Cvar_Set("mn_mapdaytime", "");
	for (ms = ccs.mission + ccs.numMissions - 1; ms >= ccs.mission; ms--)
		if (MAP_AllMapToScreen(node, ms->realPos, &x, &y, NULL)) {
			if (ms == selMis) {
				Cvar_Set("mn_mapdaytime", MAP_IsNight(ms->realPos) ? _("Night") : _("Day"));

				/* Draw circle around the mission */
				if (cl_3dmap->integer) {
					if (!selMis->def->active)
						MAP_MapDrawEquidistantPoints(node,  ms->realPos, SELECT_CIRCLE_RADIUS, yellow);
				} else
					R_DrawNormPic(x, y, 0, 0, 0, 0, 0, 0, ALIGN_CC, qtrue, selMis->def->active ? "circleactive" : "circle");
			}

			/* Draw mission model (this must be after drawing 'selected circle' so that the model looks above it)*/
			if (cl_3dmap->integer) {
				angle = MAP_AngleOfPath(ms->realPos, northPole, NULL, NULL) + 90.0f;
				MAP_Draw3DMarkerIfVisible(node, ms->realPos, angle, "mission");
			} else
				R_DrawNormPic(x, y, 0, 0, 0, 0, 0, 0, ALIGN_CC, qfalse, "cross");

			R_FontDrawString("f_verysmall", ALIGN_UL, x + 10, y, node->pos[0], node->pos[1], node->size[0], node->size[1], node->size[1], _(ms->def->location), 0, 0, NULL, qfalse);
		}

	/* draws projectiles */
	for (projectile = gd.projectiles + gd.numProjectiles - 1; projectile >= gd.projectiles; projectile --) {
		if (projectile->bulletIdx > -1)
			MAP_DrawBullets(node, projectile->bulletIdx);
		else
			MAP_Draw3DMarkerIfVisible(node, projectile->pos, projectile->angle, "missile");
	}

	/* draw bases */
	for (base = gd.bases + gd.numBases - 1; base >= gd.bases; base--) {
		if (!base->founded)
			continue;

		/* Draw base radar info */
		RADAR_DrawInMap(node, &(base->radar), base->pos);

		/* Draw base */
		if (cl_3dmap->integer) {
			angle = MAP_AngleOfPath(base->pos, northPole, NULL, NULL) + 90.0f;
			if (base->baseStatus == BASE_UNDER_ATTACK)
				MAP_Draw3DMarkerIfVisible(node, base->pos, angle, "baseattack");
			else
				MAP_Draw3DMarkerIfVisible(node, base->pos, angle, "base");
		} else if (MAP_MapToScreen(node, base->pos, &x, &y)) {
			if (base->baseStatus == BASE_UNDER_ATTACK)
				R_DrawNormPic(x, y, 0, 0, 0, 0, 0, 0, ALIGN_CC, qtrue, "baseattack");
			else
				R_DrawNormPic(x, y, 0, 0, 0, 0, 0, 0, ALIGN_CC, qfalse, "base");
		}

		/* Draw base names */
		if (MAP_AllMapToScreen(node, base->pos, &x, &y, NULL))
			R_FontDrawString(font, ALIGN_UL, x, y + 10, node->pos[0], node->pos[1], node->size[0], node->size[1], node->size[1], base->name, 0, 0, NULL, qfalse);

		/* draw aircrafts of base */
		for (aircraft = base->aircraft + base->numAircraftInBase - 1; aircraft >= base->aircraft; aircraft--)
			if (aircraft->status > AIR_HOME) {

				/* Draw aircraft radar */
				RADAR_DrawInMap(node, &(aircraft->radar), aircraft->pos);

				/* Draw aircraft route */
				if (aircraft->status >= AIR_TRANSIT) {
					mapline_t path;

					path.numPoints = aircraft->route.numPoints - aircraft->point;
					/* @todo : check why path.numPoints can be sometime equal to -1 */
					if (path.numPoints > 1) {
						memcpy(path.point, aircraft->pos, sizeof(vec2_t));
						memcpy(path.point + 1, aircraft->route.point + aircraft->point + 1, (path.numPoints - 1) * sizeof(vec2_t));
						if (cl_3dmap->integer)
							MAP_3DMapDrawLine(node, &path);
						else
							MAP_MapDrawLine(node, &path);
					}
					angle = MAP_AngleOfPath(aircraft->pos, aircraft->route.point[aircraft->route.numPoints - 1], aircraft->direction, NULL);
				} else {
					angle = MAP_AngleOfPath(aircraft->pos, northPole, aircraft->direction, NULL);
				}

				/* Draw a circle around selected aircraft */
				if (aircraft == selectedAircraft) {
					if (cl_3dmap->integer)
						MAP_MapDrawEquidistantPoints(node, aircraft->pos, SELECT_CIRCLE_RADIUS, yellow);
					else
						R_DrawNormPic(x, y, 0, 0, 0, 0, 0, 0, ALIGN_CC, qtrue, "circle");

					/* Draw a circle around ufo purchased by selected aircraft */
					if (aircraft->status == AIR_UFO && MAP_AllMapToScreen(node, aircraft->aircraftTarget->pos, &x, &y, NULL)) {
						if (cl_3dmap->integer)
							MAP_MapDrawEquidistantPoints(node, aircraft->pos, SELECT_CIRCLE_RADIUS, yellow);
						else
							R_DrawNormPic(x, y, 0, 0, 0, 0, 0, 0, ALIGN_CC, qtrue, "circle");
					}
				}

				/* Draw aircraft (this must be after drawing 'selected circle' so that the aircraft looks above it)*/
				MAP_Draw3DMarkerIfVisible(node, aircraft->pos, angle, aircraft->model);
			}
	}

	/* draws ufos */
	for (aircraft = gd.ufos + gd.numUfos - 1; aircraft >= gd.ufos; aircraft --) {
#ifdef DEBUG
		/* in debug mode you execute set showufos 1 to see the ufos on geoscape */
		if (Cvar_VariableInteger("debug_showufos")) {
			/* Draw ufo route */
			if (cl_3dmap->integer)
				MAP_3DMapDrawLine(node, &(aircraft->route));
			else
				MAP_MapDrawLine(node, &(aircraft->route));
		} else
#endif
		if (!aircraft->visible || !MAP_AllMapToScreen(node, aircraft->pos, &x, &y, NULL))
			continue;
		if (aircraft == selectedUfo) {
			if (cl_3dmap->integer)
				MAP_MapDrawEquidistantPoints(node, aircraft->pos, SELECT_CIRCLE_RADIUS, yellow);
			else
				R_DrawNormPic(x, y, 0, 0, 0, 0, 0, 0, ALIGN_CC, qfalse, "circle");
		}
		angle = MAP_AngleOfPath(aircraft->pos, aircraft->route.point[aircraft->route.numPoints - 1], aircraft->direction, NULL);
		MAP_Draw3DMarkerIfVisible(node, aircraft->pos, angle, aircraft->model);
	}

	/* FIXME */
	/* use the latitude and longitude values from nation border definition to draw a polygon */
	for (i = 0; i < gd.numNations; i++) {
		if (gd.nations[i].numBorders) {
			for (j = 0; j < gd.nations[i].numBorders; j++) {
				/* FIXME: doesn't work for scrolling the map */
				MAP_AllMapToScreen(node, gd.nations[i].borders[j], &x, &y, NULL);
				borders[j * 2] = x;
				borders[j * 2 + 1] = y;
			}
			R_DrawPolygon(gd.nations[i].numBorders, borders);
			R_DrawLineLoop(gd.nations[i].numBorders, borders);
		}
	}

	showXVI = CP_IsXVIResearched() ? qtrue : qfalse;


	/* Draw nation names */
	for (i = 0; i < gd.numNations; i++) {
		if (MAP_AllMapToScreen(node, gd.nations[i].pos, &x, &y, NULL))
			R_FontDrawString("f_verysmall", ALIGN_UC, x , y, node->pos[0], node->pos[1], node->size[0], node->size[1], node->size[1], _(gd.nations[i].name), 0, 0, NULL, qfalse);
		if (showXVI) {
			Q_strcat(buffer, va(_("%s\t%i%%\n"), _(gd.nations[i].name), gd.nations[i].XVIRate), sizeof(buffer));
		}
	}
	if (showXVI)
		menuText[TEXT_XVI] = buffer;
	else
		menuText[TEXT_XVI] = NULL;
}

/** @brief geobackground image for 3d globe */
static menuNode_t* geobackground = NULL;

/**
 * @brief Draw the geoscape
 * @param[in] node The map menu node
 * @sa MAP_DrawMapMarkers
 */
void MAP_DrawMap (const menuNode_t* node)
{
	float q;
	base_t* base;

	/* store these values in ccs struct to be able to handle this even in the input code */
	Vector2Copy(node->pos, ccs.mapPos);
	Vector2Copy(node->size, ccs.mapSize);

	/* Draw the map and markers */
	if (cl_3dmap->integer) {
		if (!geobackground)
			geobackground = MN_GetNodeFromCurrentMenu("geobackground");
		Vector2Set(geobackground->texl, 511 - 512 / ccs.zoom, 387 - 384 / ccs.zoom);
		Vector2Set(geobackground->texh, 512 + 512 / ccs.zoom, 387 + 384 / ccs.zoom);
		if (smoothRotation)
			MAP3D_SmoothRotate();
		q = (ccs.date.day % 365 + (float) (ccs.date.sec / (3600 * 24))) * 2 * M_PI / 365;
		R_Draw3DGlobe(node->pos[0], node->pos[1], node->size[0], node->size[1],
			(float) (ccs.date.sec / (24 * 3600.0f)) * 2 * M_PI - 0.5f * M_PI, q, ccs.angles, ccs.zoom / 10, curCampaign->map);
	} else {
		if (smoothRotation)
			MAP_SmoothTranslate();
		q = (ccs.date.day % 365 + (float) (ccs.date.sec / (3600 * 6)) / 4) * 2 * M_PI / 365 - M_PI;
		R_DrawDayAndNight(node->pos[0], node->pos[1], node->size[0], node->size[1], (float) ccs.date.sec / (3600 * 24), q,
			ccs.center[0], ccs.center[1], 0.5 / ccs.zoom, curCampaign->map);
	}
	MAP_DrawMapMarkers(node);

	/* display text */
	menuText[TEXT_STANDARD] = NULL;
	switch (gd.mapAction) {
	case MA_NEWBASE:
		for (base = gd.bases + gd.numBases - 1; base >= gd.bases; base--)
			/* Draw base radar info */
			RADAR_DrawCoverage(node, &(base->radar),base->pos);

		menuText[TEXT_STANDARD] = _("Select the desired location of the new base on the map.\n");
		return;
	case MA_BASEATTACK:
		if (selMis)
			break;
		menuText[TEXT_STANDARD] = _("Aliens are attacking our base at this very moment.\n");
		return;
	case MA_INTERCEPT:
		if (selMis)
			break;
		menuText[TEXT_STANDARD] = _("Select ufo or mission on map\n");
		return;
	case MA_UFORADAR:
		if (selMis)
			break;
		menuText[TEXT_STANDARD] = _("UFO in radar range\n");
		return;
	case MA_NONE:
		break;
	}

	/* Nothing is displayed yet */
	if (selMis) {
		const char *txt;
		if (!selMis->def)
			Sys_Error("the selected mission has no def pointer set\n");

		if (!selMis->def->played && selMis->def->onGeoscape && selMis->def->missionTextAlternate)
			txt = selMis->def->missionTextAlternate;
		else
			txt = selMis->def->missionText;

		menuText[TEXT_STANDARD] = va(_("Location: %s\nType: %s\nObjective: %s\n"), selMis->def->location, selMis->def->type, _(txt));
	} else if (selectedAircraft) {
		if (selectedAircraft->status <= AIR_HOME)
			MAP_ResetAction();
		else {
			Com_sprintf(text_standard, sizeof(text_standard), va(_("Name:\t%s (%i/%i)\n"), selectedAircraft->name, selectedAircraft->teamSize, selectedAircraft->maxTeamSize));
			Q_strcat(text_standard, va(_("Status:\t%s\n"), AIR_AircraftStatusToName(selectedAircraft)), sizeof(text_standard));
			Q_strcat(text_standard, va(_("Speed:\t%i\n"), selectedAircraft->stats[AIR_STATS_SPEED]), sizeof(text_standard));
			Q_strcat(text_standard, va(_("Fuel:\t%i/%i\n"), selectedAircraft->fuel / 1000, selectedAircraft->stats[AIR_STATS_FUELSIZE] / 1000), sizeof(text_standard));
			menuText[TEXT_STANDARD] = text_standard;
		}
	} else if (selectedUfo)
		menuText[TEXT_STANDARD] = va("%s\n", selectedUfo->name);
}

/**
 * @brief No more special action in geoscape
 */
void MAP_ResetAction (void)
{
	/* only if there is a running single player campaign */
	if (!curCampaign)
		return;

	/* don't allow a reset when no base is set up */
	if (gd.numBases)
		gd.mapAction = MA_NONE;

	/* FIXME: Don't reset the selMis when we are in tactical mission and enter the geoscape via mn_push map */
	/* @todo: I think this doesn't matter anymore, don't it? */
	gd.interceptAircraft = -1;
	if (selMis) {
		selMis->def->active = qfalse;
		selMis = NULL;				/* reset selected mission */
	}
	selectedAircraft = NULL;
	selectedUfo = NULL;
}

/**
 * @brief Select the specified aircraft in geoscape
 */
void MAP_SelectAircraft (aircraft_t* aircraft)
{
	MAP_ResetAction();
	selectedAircraft = aircraft;
}

/**
 * @brief Selected the specified mission
 */
void MAP_SelectMission (actMis_t* mission)
{
	if (!mission || mission == selMis)
		return;
	MAP_ResetAction();
	gd.mapAction = MA_INTERCEPT;
	selMis = mission;
}

/**
 * @brief Notify that a mission has been removed
 */
void MAP_NotifyMissionRemoved (const actMis_t* mission)
{
	/* Unselect the current selected mission if its the same */
	if (selMis == mission && (gd.mapAction == MA_BASEATTACK || gd.mapAction == MA_INTERCEPT))
		MAP_ResetAction();
	else if (selMis > mission)
		selMis--;

	/* Notify the multi selection popup */
	MAP_MultiSelectNotifyMissionRemoved(mission);
}

/**
 * @brief Notify that an ufo has been removed
 */
void MAP_NotifyUfoRemoved (const aircraft_t* ufo)
{
	/* Unselect the current selected ufo if its the same */
	if (selectedUfo == ufo)
		MAP_ResetAction();
	else if (selectedUfo > ufo)
		selectedUfo--;

	/* Notify the multi selection popup */
	MAP_MultiSelectNotifyUfoRemoved(ufo);
}

/**
 * @brief Translate nation map color to nation
 * @sa MAP_GetColor
 * @param[in] pos Map Coordinates to get the nation from
 * @return returns the nation pointer with the given color on nationPic at given pos
 * @return NULL if no nation with the given color value was found
 * @note The coodinates already have to be transfored to map coordinates via MAP_ScreenToMap
 */
nation_t* MAP_GetNation (const vec2_t pos)
{
	int i;
	nation_t* nation;
	byte* color = MAP_GetColor(pos, MAPTYPE_NATIONS);
#ifdef PARANOID
	Com_DPrintf(DEBUG_CLIENT, "MAP_GetNation: color value for %.0f:%.0f is r:%i, g:%i, b: %i\n", pos[0], pos[1], color[0], color[1], color[2]);
#endif
	for (i = 0; i < gd.numNations; i++) {
		nation = &gd.nations[i];
		/* compare the first three color values with color value at pos */
		if (VectorCompare(nation->color, color))
			return nation;
	}
	Com_DPrintf(DEBUG_CLIENT, "MAP_GetNation: No nation found at %.0f:%.0f - color: %i:%i:%i\n", pos[0], pos[1], color[0], color[1], color[2]);
	return NULL;
}

/**
 * @brief Determine the terrain type under a given position
 * @sa MAP_GetColor
 * @param[in] pos Map Coordinates to get the terrain type from
 * @return returns the zone name
 */
const char* MAP_GetTerrainTypeByPos (const vec2_t pos)
{
    byte* color = MAP_GetColor(pos, MAPTYPE_TERRAIN);
    return MAP_GetTerrainType(color);
}

/**
 * @brief Determine the culture type under a given position
 * @sa MAP_GetColor
 * @param[in] pos Map Coordinates to get the culture type from
 * @return returns the zone name
 */
const char* MAP_GetCultureTypeByPos (const vec2_t pos)
{
    byte* color = MAP_GetColor(pos, MAPTYPE_CULTURE);
    return MAP_GetCultureType(color);
}

/**
 * @brief Determine the population type under a given position
 * @sa MAP_GetColor
 * @param[in] pos Map Coordinates to get the population type from
 * @return returns the zone name
 */
const char* MAP_GetPopulationTypeByPos (const vec2_t pos)
{
    byte* color = MAP_GetColor(pos, MAPTYPE_POPULATION);
    return MAP_GetPopulationType(color);
}

/**
 * @brief Translate color value to terrain type
 * @sa MAP_GetColor
 * @param[in] byte the color value from the terrain mask
 * @return returns the zone name
 * @note never may return a null pointer or an empty string
 * @note Make sure, that there are textures with the same name in base/textures/tex_terrain
 */
const char* MAP_GetTerrainType (byte* color)
{
	if (MapIsDesert(color))
		return "desert";
	else if (MapIsArctic(color))
		return "arctic";
	else if (MapIsWater(color))
		return "water";
	else if (MapIsMountain(color))
		return "mountain";
	else if (MapIsTropical(color))
		return "tropical";
	else if (MapIsCold(color))
		return "cold";
	else if (MapIsWasted(color))
		return "wasted";
	else
		return "grass";
}

/**
 * @brief Translate color value to culture type
 * @sa MAP_GetColor
 * @param[in] byte the color value from the culture mask
 * @return returns the zone name
 * @note never may return a null pointer or an empty string
 */
const char* MAP_GetCultureType (byte* color)
{
	if (MapIsWater(color))
		return "water";
	else if (MapIsEastern(color))
		return "eastern";
	else if (MapIsWestern(color))
		return "western";
	else if (MapIsOriental(color))
		return "oriental";
	else if (MapIsAfrican(color))
		return "african";
	else
		return "western";
}

/**
 * @brief Translate color value to population type
 * @sa MAP_GetColor
 * @param[in] byte the color value from the population mask
 * @return returns the zone name
 * @note never may return a null pointer or an empty string
 */
const char* MAP_GetPopulationType (byte* color)
{
	if (MapIsWater(color))
		return "water";
	else if (MapIsUrban(color))
		return "urban";
	else if (MapIsSuburban(color))
		return "suburban";
	else if (MapIsVillage(color))
		return "village";
	else if (MapIsRural(color))
		return "rural";
	else if (MapIsNopopulation(color))
		return "nopopulation";
	else
		return "nopopulation";
}

/**
 * @brief Calculate distance on the geoscape.
 * @param[in] pos1 Position at the start.
 * @param[in] pos2 Position at the end.
 * @return Distance from pos1 to pos2.
 * @note distance is an angle! This is the angle (in degrees) between the 2 vectors starting at earth's center and ending at start or end.
 */
float MAP_GetDistance (const vec2_t pos1, const vec2_t pos2)
{
	float distance;
	vec3_t start, end;

	PolarToVec(pos1, start);
	PolarToVec(pos2, end);

	distance = DotProduct(start, end);
	distance = acos(distance) * todeg;
	return distance;
}

/**
 * @brief Check whether given position is Day or Night.
 * @param[in] pos Given position.
 * @return True if given position is Night.
 */
qboolean MAP_IsNight (vec2_t pos)
{
	float p, q, a, root, x;

	/* set p to hours (we don't use ccs.day here because we need a float value) */
	p = (float) ccs.date.sec / (3600 * 24);
	/* convert current day to angle (-pi on 1st january, pi on 31 december) */
	q = (ccs.date.day + p) * 2 * M_PI / DAYS_PER_YEAR_AVG - M_PI;
	p = (0.5 + pos[0] / 360 - p) * 2 * M_PI - q;
	a = -sin(pos[1] * torad);
	root = sqrt(1.0 - a * a);
	x = sin(p) * root * sin(q) - (a * SIN_ALPHA + cos(p) * root * COS_ALPHA) * cos(q);
	return (x > 0);
}


qboolean MAP_MaskFind (byte * color, vec2_t polar)
{
	byte *c;
	int res, i, num;

	/* check color */
	if (!color[0] && !color[1] && !color[2])
		return qfalse;

	/* find possible positions */
	res = terrainWidth * terrainHeight;
	num = 0;
	for (i = 0, c = terrainPic; i < res; i++, c += 4)
		if (c[0] == color[0] && c[1] == color[1] && c[2] == color[2])
			num++;

	/* nothing found? */
	if (!num)
		return qfalse;

	/* get position */
	num = rand() % num;
	for (i = 0, c = terrainPic; i < num; c += 4)
		if (c[0] == color[0] && c[1] == color[1] && c[2] == color[2])
			i++;

	/* transform to polar coords */
	res = (c - terrainPic) / 4;
	polar[0] = 180.0 - 360.0 * ((float) (res % terrainWidth) + 0.5) / terrainWidth;
	polar[1] = 90.0 - 180.0 * ((float) (res / terrainWidth) + 0.5) / terrainHeight;
	Com_DPrintf(DEBUG_CLIENT, "Set new coords for mission to %.0f:%.0f\n", polar[0], polar[1]);
	return qtrue;
}


/**
 * @brief Returns the color value from geoscape of a certain mask (terrain, culture or population) at a given position.
 * @param[in] pos vec2_t Value of position on map to get the color value from.
 * pos is longitude and latitude
 * @param[in] type determine the map to get the color from (there are different masks)
 * one for the climazone (bases made use of this - there are grass, ice and desert
 * base tiles available) and one for the nations
 * @return Returns the color value at given position.
 * @note terrainPic, culturePic and populationPic are pointers to an rgba image in memory
 * @sa MAP_GetTerrainType
 * @sa MAP_GetCultureType
 * @sa MAP_GetPopulationType
 */
byte *MAP_GetColor (const vec2_t pos, mapType_t type)
{
	int x, y;
	int width, height;
	byte *mask;

	switch (type) {
	case MAPTYPE_TERRAIN:
		mask = terrainPic;
		width = terrainWidth;
		height = terrainHeight;
		break;
	case MAPTYPE_CULTURE:
		mask = culturePic;
		width = cultureWidth;
		height = cultureHeight;
		break;
	case MAPTYPE_POPULATION:
		mask = populationPic;
		width = populationWidth;
		height = populationHeight;
		break;
	case MAPTYPE_NATIONS:
		mask = nationsPic;
		width = nationsWidth;
		height = nationsHeight;
		break;
	default:
		Sys_Error("Unknown maptype %i\n", type);
		return NULL;
	}

	/* get coordinates */
	x = (180 - pos[0]) / 360 * width;
	y = (90 - pos[1]) / 180 * height;
	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;

	/* 4 => RGBA */
	/* terrainWidth is the width of the image */
	/* this calulation returns the pixel in col x and in row y */
	return mask + 4 * (x + y * width);
}

/**
 * @brief Checks for a given location, if it fulfills all criteria given via parameters (terrain, culture, population, nation type)
 * @param[in] pos Location to be tested
 * @param[in] terrainTypes A linkedList_t containing a list of strings determining the terrain types to be tested for (e.g. "grass")
 * @param[in] cultureTypes A linkedList_t containing a list of strings determining the culture types to be tested for (e.g. "western")
 * @param[in] populationTypes A linkedList_t containing a list of strings determining the population types to be tested for (e.g. "suburban")
 * @param[in] nations A linkedList_t containing a list of strings determining the nations to be tested for (e.g. "asia")
 * @return true if a location was found, otherwise false
 * @note The name TCPNTypes comes from terrain, culture, population, nation types
 */
qboolean MAP_PositionFitsTCPNTypes (vec2_t pos, linkedList_t* terrainTypes, linkedList_t* cultureTypes, linkedList_t* populationTypes, linkedList_t* nations)
{
	if (LIST_ContainsString(terrainTypes,MAP_GetTerrainTypeByPos(pos)) || LIST_ContainsString(terrainTypes,"Any")) {
		if (LIST_ContainsString(cultureTypes,MAP_GetCultureTypeByPos(pos)) || LIST_ContainsString(cultureTypes,"Any")) {
			if (LIST_ContainsString(populationTypes,MAP_GetPopulationTypeByPos(pos)) || LIST_ContainsString(populationTypes,"Any")) {
				nation_t *nationAtPos = MAP_GetNation(pos);
				if ((nationAtPos && LIST_ContainsString(nations,nationAtPos->id)) || LIST_ContainsString(nations,"Any")) {
					return qtrue;
				}
			}
		}
	}

	return qfalse;
}

void MAP_Init (void)
{
	/* load terrain mask */
	if (terrainPic) {
		Mem_Free(terrainPic);
		terrainPic = NULL;
	}
	R_LoadTGA(va("pics/menu/%s_terrain.tga", curCampaign->map), &terrainPic, &terrainWidth, &terrainHeight);
	if (!terrainPic || !terrainWidth || !terrainHeight)
		Sys_Error("Couldn't load map mask %s_terrain.tga in pics/menu\n", curCampaign->map);

	/* load culture mask */
	if (culturePic) {
		Mem_Free(culturePic);
		culturePic = NULL;
	}
	R_LoadTGA(va("pics/menu/%s_culture.tga", curCampaign->map), &culturePic, &cultureWidth, &cultureHeight);
	if (!culturePic || !cultureWidth || !cultureHeight)
		Sys_Error("Couldn't load map mask %s_culture.tga in pics/menu\n", curCampaign->map);

	/* load population mask */
	if (populationPic) {
		Mem_Free(populationPic);
		populationPic = NULL;
	}
	R_LoadTGA(va("pics/menu/%s_population.tga", curCampaign->map), &populationPic, &populationWidth, &populationHeight);
	if (!populationPic || !populationWidth || !populationHeight)
		Sys_Error("Couldn't load map mask %s_population.tga in pics/menu\n", curCampaign->map);

	/* load nations mask */
	if (nationsPic) {
		Mem_Free(nationsPic);
		nationsPic = NULL;
	}
	R_LoadTGA(va("pics/menu/%s_nations.tga", curCampaign->map), &nationsPic, &nationsWidth, &nationsHeight);
	if (!nationsPic || !nationsWidth || !nationsHeight)
		Sys_Error("Couldn't load map mask %s_nations.tga in pics/menu\n", curCampaign->map);

	MAP_ResetAction();
}

/**
 * @brief Notify that an ufo disappears on radars
 */
void MAP_NotifyUfoDisappear (const aircraft_t* ufo)
{
	/* Unselect the current selected ufo if its the same */
	if (selectedUfo == ufo)
		MAP_ResetAction();

	/* Notify the multi selection popup */
	MAP_MultiSelectNotifyUfoDisappeared(ufo);
}

/**
 * @brief Command binding for map zooming
 */
void MAP_Zoom_f (void)
{
	const char *cmd;
	const float zoomAmount = 15.0f;

	if (Cmd_Argc() != 2) {
		Com_Printf("Usage: %s <in|out>\n", Cmd_Argv(0));
		return;
	}

	cmd = Cmd_Argv(1);
	switch (*cmd) {
	case 'i':
		ccs.zoom *= pow(0.995, -zoomAmount);
		break;
	case 'o':
		ccs.zoom *= pow(0.995, zoomAmount);
		break;
	default:
		Com_Printf("MAP_Zoom_f: Invalid parameter: %s\n", cmd);
		return;
	}

	if (ccs.zoom < cl_mapzoommin->value)
		ccs.zoom = cl_mapzoommin->value;
	else if (ccs.zoom > cl_mapzoommax->value)
		ccs.zoom = cl_mapzoommax->value;

	if (!cl_3dmap->integer) {
		if (ccs.center[1] < 0.5 / ccs.zoom)
			ccs.center[1] = 0.5 / ccs.zoom;
		if (ccs.center[1] > 1.0 - 0.5 / ccs.zoom)
			ccs.center[1] = 1.0 - 0.5 / ccs.zoom;
	}
}

/**
 * @brief Command binding for map scrolling
 */
void MAP_Scroll_f (void)
{
	const char *cmd;
	float scrollX = 0.0f, scrollY = 0.0f;
	const float scrollAmount = 30.0f;

	if (Cmd_Argc() != 2) {
		Com_Printf("Usage: %s <up|down|left|right>\n", Cmd_Argv(0));
		return;
	}

	cmd = Cmd_Argv(1);
	switch (*cmd) {
	case 'l':
		scrollX = scrollAmount;
		break;
	case 'r':
		scrollX = -scrollAmount;
		break;
	case 'u':
		scrollY = scrollAmount;
		break;
	case 'd':
		scrollY = -scrollAmount;
		break;
	default:
		Com_Printf("MAP_Scroll_f: Invalid parameter\n");
		return;
	}
	if (cl_3dmap->integer) {
		/* rotate a model */
		ccs.angles[PITCH] += ROTATE_SPEED * (scrollX) / ccs.zoom;
		ccs.angles[YAW] -= ROTATE_SPEED * (scrollY) / ccs.zoom;

		while (ccs.angles[YAW] > 180.0)
			ccs.angles[YAW] -= 360.0;
		while (ccs.angles[YAW] < -180.0)
			ccs.angles[YAW] += 360.0;

		while (ccs.angles[PITCH] > 180.0)
			ccs.angles[PITCH] -= 360.0;
		while (ccs.angles[PITCH] < -180.0)
			ccs.angles[PITCH] += 360.0;
	} else {
		int i;
		/* shift the map */
		ccs.center[0] -= (float) (scrollX) / (ccs.mapSize[0] * ccs.zoom);
		ccs.center[1] -= (float) (scrollY) / (ccs.mapSize[1] * ccs.zoom);
		for (i = 0; i < 2; i++) {
			while (ccs.center[i] < 0.0)
				ccs.center[i] += 1.0;
			while (ccs.center[i] > 1.0)
				ccs.center[i] -= 1.0;
		}
		if (ccs.center[1] < 0.5 / ccs.zoom)
			ccs.center[1] = 0.5 / ccs.zoom;
		if (ccs.center[1] > 1.0 - 0.5 / ccs.zoom)
			ccs.center[1] = 1.0 - 0.5 / ccs.zoom;
	}
}

/**
 * @brief Initialise MAP/Geoscape
 */
void MAP_GameInit (void)
{
	cl_showCoords = Cvar_Get("cl_showcoords", "0", 0, NULL);
	Cmd_AddCommand("multi_select_click", MAP_MultiSelectExecuteAction_f, NULL);
}
