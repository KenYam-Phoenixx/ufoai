/**
 * @file q_shared.c
 * @brief Common functions.
 */

/*
All original materal Copyright (C) 2002-2007 UFO: Alien Invasion team.

Original file from Quake 2 v3.21: quake2-2.31/game/q_shared.c
Copyright (C) 1997-2001 Id Software, Inc.

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

#include "q_shared.h"

#ifdef UFO_MMX_ENABLED
/* used for mmx optimizations */
#include <xmmintrin.h>
#endif

#define DEG2RAD( a ) (( a * M_PI ) / 180.0F)

const vec3_t vec3_origin = { 0, 0, 0 };
const vec4_t vec4_origin = { 0, 0, 0, 0 };


#define RT2	0.707107

/* DIRECTIONS
 *  straight
 * 0 = x+1, y
 * 1 = x-1, y
 * 2 = x  , y+1
 * 3 = x  , y-1
 *  diagonal
 * 4 = x+1, y+1
 * 5 = x-1, y-1
 * 6 = x-1, y+1
 * 7 = x+1, y-1
 */
const int dvecs[DIRECTIONS][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, -1}, {-1, 1}, {1, -1} };
const float dvecsn[DIRECTIONS][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {RT2, RT2}, {-RT2, -RT2}, {-RT2, RT2}, {RT2, -RT2} };
/* if you change dangle[DIRECTIONS], you must also change function AngleToDV */
const float dangle[DIRECTIONS] = { 0, 180.0f, 90.0f, 270.0f, 45.0f, 225.0f, 135.0f, 315.0f };

const byte dvright[DIRECTIONS] = { 7, 6, 4, 5, 0, 1, 2, 3 };
const byte dvleft[DIRECTIONS] = { 4, 5, 6, 7, 2, 3, 1, 0 };

/*===========================================================================*/

/** @brief Player action format strings for netchannel transfer */
const char *pa_format[] =
{
	"",					/**< PA_NULL */
	"b",				/**< PA_TURN */
	"g",				/**< PA_MOVE */
	"s",				/**< PA_STATE - don't use a bitmask here - only one value
						 * @sa G_ClientStateChange */
	"gbbl",				/**< PA_SHOOT */
	"bbbbbb",			/**< PA_INVMOVE */
	"ll"				/**< PA_REACT_SELECT */
};

/*===========================================================================*/

/**
 * @brief Compare two strings
 * @param[in] string1 The first string
 * @param[in] string2 The second string
 * @return An integer less than, equal to, or greater than zero if string1 is
 * found, respectively, to be less than,  to match, or be greater than string2
 * @note sort function pointer for qsort
 */
int Q_StringSort (const void *string1, const void *string2)
{
	const char *s1, *s2;
	s1 = string1;
	s2 = string2;
	if (*s1 < *s2)
		return -1;
	else if (*s1 == *s2) {
		while (*s1) {
			s1++;
			s2++;
			if (*s1 < *s2)
				return -1;
			else if (*s1 == *s2) {
				;
			} else
				return 1;
		}
		return 0;
	} else
		return 1;
}

/**
 * @brief Returns the indice of array dangle[DIRECTIONS] whose value is the closest to angle
 * @note This function allows to know the closest multiple of 45 degree of angle.
 * @param[in] angle The angle (in degrees) which is tested.
 * @return Corresponding indice of array dangle[DIRECTIONS].
 */
int AngleToDV (int angle)
{
#if 0
	int i, mini;
	int div, minDiv;

	/* set angle between -22 <= angle <= 338 */
	/* The first line is just to minimize the number of loops in the following while */
	angle %= 360;
	while (angle > 360 - 22)
		angle -= 360;
	while (angle < -22)
		angle += 360;
	minDiv = 360;
	mini = 0;

	for (i = 0; i < 8; i++) {
		div = angle - dangle[i];
		div = (div < 0) ? -div : div;
		if (div < minDiv) {
			mini = i;
			minDiv = div;
		}
	}

	return mini;
#endif

	angle += 22;
	/* set angle between 0 <= angle < 360 */
	angle %= 360;
	/* next step is because the result of angle %= 360 when angle is negative depends of the compiler
	 *  (it can be between -360 < angle <= 0 or 0 <= angle < 360) */
	if (angle < 0)
		angle += 360;

	/* get an integer quotient */
	angle /= 45;

	/* return the corresponding indice in dangle[DIRECTIONS] */
	switch(angle) {
	case 0:
		return 0;
	case 1:
		return 4;
	case 2:
		return 2;
	case 3:
		return 6;
	case 4:
		return 1;
	case 5:
		return 5;
	case 6:
		return 3;
	case 7:
		return 7;
	default:
		Com_Printf("Error in AngleToDV: shouldn't have reached this line\n");
		return 0;
	}
}

/*============================================================================ */

/**
 * @brief Rotate a point around static (idle ?) frame {0, 1, 0}, {0, 0, 1} ,{1, 0, 0}
 * @param[in] angles Contains the three angles of rotation around idle frame ({0, 1, 0}, {0, 0, 1} ,{1, 0, 0}) (in this order)
 * @param[out] forward result of previous rotation for point {1, 0, 0} (can be NULL if not needed)
 * @param[out] right result of previous rotation for point {0, -1, 0} (!) (can be NULL if not needed)
 * @param[out] up result of previous rotation for point {0, 0, 1} (can be NULL if not needed)
 */
void AngleVectors (const vec3_t angles, vec3_t forward, vec3_t right, vec3_t up)
{
	float angle;
	static float sr, sp, sy, cr, cp, cy;

	/* static to help MS compiler fp bugs */

	angle = angles[YAW] * (M_PI * 2 / 360);
	sy = sin(angle);
	cy = cos(angle);
	angle = angles[PITCH] * (M_PI * 2 / 360);
	sp = sin(angle);
	cp = cos(angle);
	angle = angles[ROLL] * (M_PI * 2 / 360);
	sr = sin(angle);
	cr = cos(angle);

	if (forward) {
		forward[0] = cp * cy;
		forward[1] = cp * sy;
		forward[2] = -sp;
	}
	if (right) {
		right[0] = (-1 * sr * sp * cy + -1 * cr * -sy);
		right[1] = (-1 * sr * sp * sy + -1 * cr * cy);
		right[2] = -1 * sr * cp;
	}
	if (up) {
		up[0] = (cr * sp * cy + -sr * -sy);
		up[1] = (cr * sp * sy + -sr * cy);
		up[2] = cr * cp;
	}
}

/**
 * @brief Checks whether a point is visible from a given position
 * @param[in] origin Origin to test from
 * @param[in] dir Direction to test into
 * @param[in] point This is the point we want to check the visibility for
 */
qboolean FrustomVis (vec3_t origin, int dir, vec3_t point)
{
	/* view frustum check */
	vec3_t delta;
	byte dv;

	delta[0] = point[0] - origin[0];
	delta[1] = point[1] - origin[1];
	delta[2] = 0;
	VectorNormalize(delta);
	dv = dir & (DIRECTIONS-1);

	/* test 120 frustum (cos 60 = 0.5) */
	if ((delta[0] * dvecsn[dv][0] + delta[1] * dvecsn[dv][1]) < 0.5)
		return qfalse;
	else
		return qtrue;
}

/**
 * @brief Projects a point on a plane passing through the origin
 * @param[in] point Coordinates of the point to project
 * @param[in] normal The normal vector of the plane
 * @param[out] dst Coordinates of the projection on the plane
 * @pre @c Non-null pointers and a normalized normal vector.
 */
static inline void ProjectPointOnPlane (vec3_t dst, const vec3_t point, const vec3_t normal)
{
	float distance; /**< closest distance from the point to the plane */

#if 0
	vec3_t n;
	float inv_denom;
	/* I added a sqrt there, otherwise this function does not work for unnormalized vector (13052007 Kracken) */
	/* old line was inv_denom = 1.0F / DotProduct(normal, normal); */
	inv_denom = 1.0F / sqrt(DotProduct(normal, normal));
#endif

	distance = DotProduct(normal, point);
#if 0
	n[0] = normal[0] * inv_denom;
	n[1] = normal[1] * inv_denom;
	n[2] = normal[2] * inv_denom;
#endif

	dst[0] = point[0] - distance * normal[0];
	dst[1] = point[1] - distance * normal[1];
	dst[2] = point[2] - distance * normal[2];
}

/**
 * @brief Calculated the normal vector for a given vec3_t
 * @param[in] v Vector to normalize
 * @sa VectorNormalize2
 * @return vector length as vec_t
 */
vec_t VectorNormalize (vec3_t v)
{
	float length, ilength;

	length = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
	length = sqrt(length);		/* FIXME */

	if (length) {
		ilength = 1 / length;
		v[0] *= ilength;
		v[1] *= ilength;
		v[2] *= ilength;
	}

	return length;
}

/**
 * @brief Finds a vector perpendicular to the source vector
 * @param[in] src The source vector
 * @param[out] dst A vector perpendicular to @c src
 * @note @c dst is a perpendicular vector to @c src such that it is the closest
 * to one of the three axis: {1,0,0}, {0,1,0} and {0,0,1} (chosen in that order
 * in case of equality)
 * @pre non-NULL pointers and @c src is normalized
 * @sa ProjectPointOnPlane
 */
void PerpendicularVector (vec3_t dst, const vec3_t src)
{
	int pos;
	int i;
	float minelem = 1.0F;
	vec3_t tempvec;

	/* find the smallest magnitude axially aligned vector */
	for (pos = 0, i = 0; i < 3; i++) {
		if (fabs(src[i]) < minelem) {
			pos = i;
			minelem = fabs(src[i]);
		}
	}
	tempvec[0] = tempvec[1] = tempvec[2] = 0.0F;
	tempvec[pos] = 1.0F;

	/* project the point onto the plane defined by src */
	ProjectPointOnPlane(dst, tempvec, src);

	/* normalize the result */
	VectorNormalize(dst);
}

/**
 * @brief binary operation on vectors in a three-dimensional space
 * @note also known as the vector product or outer product
 * @note It differs from the dot product in that it results in a vector
 * rather than in a scalar
 * @note Its main use lies in the fact that the cross product of two vectors
 * is orthogonal to both of them.
 * @param[in] v1 directional vector
 * @param[in] v2 directional vector
 * @param[out] cross output
 * @example
 * you have the right and forward values of an axis, their cross product will
 * be a properly oriented "up" direction
 * @sa DotProduct
 * @sa VectorNormalize2
 */
void CrossProduct (const vec3_t v1, const vec3_t v2, vec3_t cross)
{
	cross[0] = v1[1] * v2[2] - v1[2] * v2[1];
	cross[1] = v1[2] * v2[0] - v1[0] * v2[2];
	cross[2] = v1[0] * v2[1] - v1[1] * v2[0];
}

static inline void R_ConcatRotations (float in1[3][3], float in2[3][3], float out[3][3])
{
	out[0][0] = in1[0][0] * in2[0][0] + in1[0][1] * in2[1][0] + in1[0][2] * in2[2][0];
	out[0][1] = in1[0][0] * in2[0][1] + in1[0][1] * in2[1][1] + in1[0][2] * in2[2][1];
	out[0][2] = in1[0][0] * in2[0][2] + in1[0][1] * in2[1][2] + in1[0][2] * in2[2][2];
	out[1][0] = in1[1][0] * in2[0][0] + in1[1][1] * in2[1][0] + in1[1][2] * in2[2][0];
	out[1][1] = in1[1][0] * in2[0][1] + in1[1][1] * in2[1][1] + in1[1][2] * in2[2][1];
	out[1][2] = in1[1][0] * in2[0][2] + in1[1][1] * in2[1][2] + in1[1][2] * in2[2][2];
	out[2][0] = in1[2][0] * in2[0][0] + in1[2][1] * in2[1][0] + in1[2][2] * in2[2][0];
	out[2][1] = in1[2][0] * in2[0][1] + in1[2][1] * in2[1][1] + in1[2][2] * in2[2][1];
	out[2][2] = in1[2][0] * in2[0][2] + in1[2][1] * in2[1][2] + in1[2][2] * in2[2][2];
}

/**
 * @brief Rotate a point around a given vector
 * @param[in] dir The vector around which to rotate
 * @param[in] point The point to be rotated
 * @param[in] degrees How many degrees to rotate the point by
 * @param[out] dst The point after rotation
 * @note Warning: dst must be different from point (otherwise the result has no meaning)
 * @pre @c dir must be normalized
 */
void RotatePointAroundVector (vec3_t dst, const vec3_t dir, const vec3_t point, float degrees)
{
	float m[3][3];
	float im[3][3];
	float zrot[3][3];
	float tmpmat[3][3];
	float rot[3][3];
	int i;
	vec3_t vr, vup, vf;

	vf[0] = dir[0];
	vf[1] = dir[1];
	vf[2] = dir[2];

	PerpendicularVector(vr, dir);
	CrossProduct(vr, vf, vup);

	m[0][0] = vr[0];
	m[1][0] = vr[1];
	m[2][0] = vr[2];

	m[0][1] = vup[0];
	m[1][1] = vup[1];
	m[2][1] = vup[2];

	m[0][2] = vf[0];
	m[1][2] = vf[1];
	m[2][2] = vf[2];

	memcpy(im, m, sizeof(im));

	im[0][1] = m[1][0];
	im[0][2] = m[2][0];
	im[1][0] = m[0][1];
	im[1][2] = m[2][1];
	im[2][0] = m[0][2];
	im[2][1] = m[1][2];

	memset(zrot, 0, sizeof(zrot));
	zrot[0][0] = zrot[1][1] = zrot[2][2] = 1.0F;

	zrot[0][0] = cos(DEG2RAD(degrees));
	zrot[0][1] = sin(DEG2RAD(degrees));
	zrot[1][0] = -sin(DEG2RAD(degrees));
	zrot[1][1] = cos(DEG2RAD(degrees));

	R_ConcatRotations(m, zrot, tmpmat);
	R_ConcatRotations(tmpmat, im, rot);

	for (i = 0; i < 3; i++) {
		dst[i] = rot[i][0] * point[0] + rot[i][1] * point[1] + rot[i][2] * point[2];
	}
}

/**
 * @brief Print a 3D vector
 * @param[in] v The vector to be printed
 */
void Print3Vector (const vec3_t v)
{
	Com_Printf("(%f, %f, %f)\n", v[0], v[1], v[2]);
}

/**
 * @brief Print a 2D vector
 * @param[in] v The vector to be printed
 */
void Print2Vector (const vec2_t v)
{
	Com_Printf("(%f, %f)\n", v[0], v[1]);
}

/**
 * @brief Converts longitude and latitude to a 3D vector in Euclidean
 * coordinates
 * @param[in] a The longitude and latitude in a 2D vector
 * @param[out] v The resulting normal 3D vector
 * @sa VecToPolar
 */
void PolarToVec (const vec2_t a, vec3_t v)
{
	float p, t;

	p = a[0] * torad;	/* long */
	t = a[1] * torad;	/* lat */
	/* v[0] = z, v[1] = x, v[2] = y - wtf? */
	VectorSet(v, cos(p) * cos(t), sin(p) * cos(t), sin(t));
}

/**
 * @brief Converts vector coordinates into polar coordinates
 * @sa PolarToVec
 */
void VecToPolar (const vec3_t v, vec2_t a)
{
	a[0] = todeg * atan2(v[1], v[0]);	/* long */
	a[1] = 90 - todeg * acos(v[2]);	/* lat */
}

/**
 * @brief Converts a vector to an angle vector
 * @param[in] value1
 * @param[in] angles Target vector for pitch, yaw, roll
 * @sa anglemod
 */
void VecToAngles (const vec3_t value1, vec3_t angles)
{
	float forward;
	float yaw, pitch;

	if (value1[1] == 0 && value1[0] == 0) {
		yaw = 0;
		if (value1[2] > 0)
			pitch = 90;
		else
			pitch = 270;
	} else {
		if (value1[0])
			yaw = (int) (atan2(value1[1], value1[0]) * todeg);
		else if (value1[1] > 0)
			yaw = 90;
		else
			yaw = -90;
		if (yaw < 0)
			yaw += 360;

		forward = sqrt(value1[0] * value1[0] + value1[1] * value1[1]);
		pitch = (int) (atan2(value1[2], forward) * todeg);
		if (pitch < 0)
			pitch += 360;
	}

	/* up and down */
	angles[PITCH] = -pitch;
	/* left and right */
	angles[YAW] = yaw;
	/* tilt left and right */
	angles[ROLL] = 0;
}


/**
 * @brief Checks whether i is power of two value
 */
qboolean Q_IsPowerOfTwo (int i)
{
	return (i > 0 && !(i & (i - 1)));
}

/**
 * @brief Returns the angle resulting from turning fraction * angle
 * from angle1 to angle2
 */
float LerpAngle (float a2, float a1, float frac)
{
	if (a1 - a2 > 180)
		a1 -= 360;
	if (a1 - a2 < -180)
		a1 += 360;
	return a2 + frac * (a1 - a2);
}

/**
 * @brief returns angle normalized to the range [0 <= angle < 360]
 * @param[in] angle Angle
 * @sa VecToAngles
 */
float AngleNormalize360 (float angle)
{
	return (360.0 / 65536) * ((int)(angle * (65536 / 360.0)) & 65535);
}

/**
 * @brief returns angle normalized to the range [-180 < angle <= 180]
 * @param[in] angle Angle
 */
float AngleNormalize180 (float angle)
{
	angle = AngleNormalize360(angle);
	if (angle > 180.0)
		angle -= 360.0;
	return angle;
}

int BoxOnPlaneSide (vec3_t emins, vec3_t emaxs, struct cBspPlane_s *p)
{
	float dist1, dist2;
	int sides;

	/* fast axial cases */
	if (p->type < 3) {
		if (p->dist <= emins[p->type])
			return 1;
		if (p->dist >= emaxs[p->type])
			return 2;
		return 3;
	}

	/* general case */
	switch (p->signbits) {
	case 0:
		dist1 = p->normal[0] * emaxs[0] + p->normal[1] * emaxs[1] + p->normal[2] * emaxs[2];
		dist2 = p->normal[0] * emins[0] + p->normal[1] * emins[1] + p->normal[2] * emins[2];
		break;
	case 1:
		dist1 = p->normal[0] * emins[0] + p->normal[1] * emaxs[1] + p->normal[2] * emaxs[2];
		dist2 = p->normal[0] * emaxs[0] + p->normal[1] * emins[1] + p->normal[2] * emins[2];
		break;
	case 2:
		dist1 = p->normal[0] * emaxs[0] + p->normal[1] * emins[1] + p->normal[2] * emaxs[2];
		dist2 = p->normal[0] * emins[0] + p->normal[1] * emaxs[1] + p->normal[2] * emins[2];
		break;
	case 3:
		dist1 = p->normal[0] * emins[0] + p->normal[1] * emins[1] + p->normal[2] * emaxs[2];
		dist2 = p->normal[0] * emaxs[0] + p->normal[1] * emaxs[1] + p->normal[2] * emins[2];
		break;
	case 4:
		dist1 = p->normal[0] * emaxs[0] + p->normal[1] * emaxs[1] + p->normal[2] * emins[2];
		dist2 = p->normal[0] * emins[0] + p->normal[1] * emins[1] + p->normal[2] * emaxs[2];
		break;
	case 5:
		dist1 = p->normal[0] * emins[0] + p->normal[1] * emaxs[1] + p->normal[2] * emins[2];
		dist2 = p->normal[0] * emaxs[0] + p->normal[1] * emins[1] + p->normal[2] * emaxs[2];
		break;
	case 6:
		dist1 = p->normal[0] * emaxs[0] + p->normal[1] * emins[1] + p->normal[2] * emins[2];
		dist2 = p->normal[0] * emins[0] + p->normal[1] * emaxs[1] + p->normal[2] * emaxs[2];
		break;
	case 7:
		dist1 = p->normal[0] * emins[0] + p->normal[1] * emins[1] + p->normal[2] * emins[2];
		dist2 = p->normal[0] * emaxs[0] + p->normal[1] * emaxs[1] + p->normal[2] * emaxs[2];
		break;
	default:
		dist1 = dist2 = 0;		/* shut up compiler */
		assert(0);
		break;
	}

	sides = 0;
	if (dist1 >= p->dist)
		sides = 1;
	if (dist2 < p->dist)
		sides |= 2;

	assert(sides != 0);

	return sides;
}

void ClearBounds (vec3_t mins, vec3_t maxs)
{
	mins[0] = mins[1] = mins[2] = 99999;
	maxs[0] = maxs[1] = maxs[2] = -99999;
}

/**
 * @brief If the point is outside the box defined by mins and maxs, expand
 */
void AddPointToBounds (const vec3_t v, vec3_t mins, vec3_t maxs)
{
	int i;
	vec_t val;

	for (i = 0; i < 3; i++) {
		val = v[i];
		if (val < mins[i])
			mins[i] = val;
		if (val > maxs[i])
			maxs[i] = val;
	}
}

qboolean VectorNearer (const vec3_t v1, const vec3_t v2, const vec3_t comp)
{
	vec3_t d1, d2;

	VectorSubtract(comp, v1, d1);
	VectorSubtract(comp, v2, d2);

	return VectorLength(d1) < VectorLength(d2);
}

/**
 * @brief Calculated the normal vector for a given vec3_t
 * @param[in] v Vector to normalize
 * @param[out] out The normalized vector
 * @sa VectorNormalize
 * @return vector length as vec_t
 * @sa CrossProduct
 */
vec_t VectorNormalize2 (const vec3_t v, vec3_t out)
{
	float length, ilength;

	length = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
	length = sqrt(length);		/* FIXME */

	if (length) {
		ilength = 1 / length;
		out[0] = v[0] * ilength;
		out[1] = v[1] * ilength;
		out[2] = v[2] * ilength;
	}

	return length;
}

/**
 * @brief Sets vector_out (vc) to vevtor1 (va) + scale * vector2 (vb)
 * @param[in] veca Position to start from
 * @param[in] scale Speed of the movement
 * @param[in] vecb Movement direction
 * @param[out] vecc Target vector
 */
void VectorMA (const vec3_t veca, const float scale, const vec3_t vecb, vec3_t vecc)
{
	vecc[0] = veca[0] + scale * vecb[0];
	vecc[1] = veca[1] + scale * vecb[1];
	vecc[2] = veca[2] + scale * vecb[2];
}

void VectorClampMA (vec3_t veca, float scale, const vec3_t vecb, vec3_t vecc)
{
	float test, newScale;
	int i;

	newScale = scale;

	/* clamp veca to bounds */
	for (i = 0; i < 3; i++)
		if (veca[i] > 4094.0)
			veca[i] = 4094.0;
		else if (veca[i] < -4094.0)
			veca[i] = -4094.0;

	/* rescale to fit */
	for (i = 0; i < 3; i++) {
		test = veca[i] + scale * vecb[i];
		if (test < -4095.0f) {
			newScale = (-4094.0 - veca[i]) / vecb[i];
			if (fabs(newScale) < fabs(scale))
				scale = newScale;
		} else if (test > 4095.0f) {
			newScale = (4094.0 - veca[i]) / vecb[i];
			if (fabs(newScale) < fabs(scale))
				scale = newScale;
		}
	}

	/* use rescaled scale */
	for (i = 0; i < 3; i++)
		vecc[i] = veca[i] + scale * vecb[i];
}

/**
 * @sa GLMatrixMultiply
 */
void MatrixMultiply (const vec3_t a[3], const vec3_t b[3], vec3_t c[3])
{
	c[0][0] = a[0][0] * b[0][0] + a[1][0] * b[0][1] + a[2][0] * b[0][2];
	c[0][1] = a[0][1] * b[0][0] + a[1][1] * b[0][1] + a[2][1] * b[0][2];
	c[0][2] = a[0][2] * b[0][0] + a[1][2] * b[0][1] + a[2][2] * b[0][2];

	c[1][0] = a[0][0] * b[1][0] + a[1][0] * b[1][1] + a[2][0] * b[1][2];
	c[1][1] = a[0][1] * b[1][0] + a[1][1] * b[1][1] + a[2][1] * b[1][2];
	c[1][2] = a[0][2] * b[1][0] + a[1][2] * b[1][1] + a[2][2] * b[1][2];

	c[2][0] = a[0][0] * b[2][0] + a[1][0] * b[2][1] + a[2][0] * b[2][2];
	c[2][1] = a[0][1] * b[2][0] + a[1][1] * b[2][1] + a[2][1] * b[2][2];
	c[2][2] = a[0][2] * b[2][0] + a[1][2] * b[2][1] + a[2][2] * b[2][2];
}


/**
 * @sa MatrixMultiply
 */
void GLMatrixMultiply (const float a[16], const float b[16], float c[16])
{
	int i, j, k;

	for (j = 0; j < 4; j++) {
		k = j * 4;
		for (i = 0; i < 4; i++)
			c[i + k] = a[i] * b[k] + a[i + 4] * b[k + 1] + a[i + 8] * b[k + 2] + a[i + 12] * b[k + 3];
	}
}


void GLVectorTransform (const float m[16], const vec4_t in, vec4_t out)
{
	int i;

	for (i = 0; i < 4; i++)
		out[i] = m[i] * in[0] + m[i + 4] * in[1] + m[i + 8] * in[2] + m[i + 12] * in[3];
}


void VectorRotate (const vec3_t m[3], const vec3_t va, vec3_t vb)
{
	vb[0] = m[0][0] * va[0] + m[1][0] * va[1] + m[2][0] * va[2];
	vb[1] = m[0][1] * va[0] + m[1][1] * va[1] + m[2][1] * va[2];
	vb[2] = m[0][2] * va[0] + m[1][2] * va[1] + m[2][2] * va[2];
}

/**
 * @brief Compare two vectors that may have an epsilon difference but still be
 * the same vectors
 * @param[in] v1 vector to compare with v2
 * @param[in] v2 vector to compare with v1
 * @param[in] epsilon The epsilon the vectors may differ to still be the same
 */
int VectorCompareEps (const vec3_t v1, const vec3_t v2, float epsilon)
{
	vec3_t d;

	VectorSubtract(v1, v2, d);
	d[0] = fabs(d[0]);
	d[1] = fabs(d[1]);
	d[2] = fabs(d[2]);

	if (d[0] > epsilon || d[1] > epsilon || d[2] > epsilon)
		return 0;

	return 1;
}

double sqrt (double x);

/**
 * @brief Calculate the length of a vector
 * @param[in] v Vector to calculate the length of
 * @sa VectorNormalize
 * @return Vector length as vec_t
 */
vec_t VectorLength (const vec3_t v)
{
	int i;
	float length;

	length = 0;
	for (i = 0; i < 3; i++)
		length += v[i] * v[i];
	length = sqrt(length);		/* FIXME */

	return length;
}

void VectorInverse (vec3_t v)
{
	v[0] = -v[0];
	v[1] = -v[1];
	v[2] = -v[2];
}

void VectorScale (const vec3_t in, const vec_t scale, vec3_t out)
{
#ifdef UFO_MMX_ENABLED
	/* raynorpat: msvc sse optimization */
	__m128 xmm_in, xmm_scale, xmm_out;

	xmm_in = _mm_load_ss(&in[0]);
	xmm_scale = _mm_load_ss(&scale);
	xmm_out = _mm_mul_ss(xmm_in, xmm_scale);
	_mm_store_ss(&out[0], xmm_out);

	xmm_in = _mm_load_ss(&in[1]);
	xmm_scale = _mm_load_ss(&scale);
	xmm_out = _mm_mul_ss(xmm_in, xmm_scale);
	_mm_store_ss(&out[1], xmm_out);

	xmm_in = _mm_load_ss(&in[2]);
	xmm_scale = _mm_load_ss(&scale);
	xmm_out = _mm_mul_ss(xmm_in, xmm_scale);
	_mm_store_ss(&out[2], xmm_out);
#else
	out[0] = in[0] * scale;
	out[1] = in[1] * scale;
	out[2] = in[2] * scale;
#endif
}


int Q_log2 (int val)
{
	int answer = 0;

	while (val >>= 1)
		answer++;
	return answer;
}

/**
 * @brief Return random values between 0 and 1
 * @sa crand
 * @sa gaussrand
 */
float frand (void)
{
	return (rand() & 32767) * (1.0 / 32767);
}


/**
 * @brief Return random values between -1 and 1
 * @sa frand
 * @sa gaussrand
 */
float crand (void)
{
	return (rand() & 32767) * (2.0 / 32767) - 1;
}

/**
 * @brief generate two gaussian distributed random numbers with median at 0 and stdev of 1
 * @param pointers to two floats that need to be set. both have to be provided.
 * @sa crand
 * @sa frand
 */
void gaussrand (float *gauss1, float *gauss2)
{
	float x1,x2,w,tmp;

	do {
		x1 = crand();
		x2 = crand();
		w = x1 * x1 + x2 * x2;
	} while (w >= 1.0);

	tmp = -2 * logf(w);
	w = sqrt(tmp / w);
	*gauss1 = x1 * w;
	*gauss2 = x2 * w;
}

#define VA_BUFSIZE 4096
/**
 * @brief does a varargs printf into a temp buffer, so I don't need to have
 * varargs versions of all text functions.
 */
char *va (const char *format, ...)
{
	va_list argptr;
	/* in case va is called by nested functions */
	static char string[2][VA_BUFSIZE];
	static int index = 0;
	char *buf;

	buf = string[index & 1];
	index++;

	va_start(argptr, format);
	Q_vsnprintf(buf, VA_BUFSIZE, format, argptr);
	va_end(argptr);

	buf[VA_BUFSIZE-1] = 0;

	return buf;
}

/**
 * @sa Com_Parse
 */
const char *COM_EParse (const char **text, const char *errhead, const char *errinfo)
{
	const char *token;

	token = COM_Parse(text);
	if (!*text) {
		if (errinfo)
			Com_Printf("%s \"%s\")\n", errhead, errinfo);
		else
			Com_Printf("%s\n", errhead);

		return NULL;
	}

	return token;
}

/*
============================================================================
LIBRARY REPLACEMENT FUNCTIONS
============================================================================
*/

char *Q_strlwr (char *str)
{
	char* origs = str;
	while (*str) {
		*str = tolower(*str);
		str++;
	}
	return origs;
}

int Q_putenv (const char *var, const char *value)
{
#ifdef __APPLE__
	return setenv(var, value, 1);
#else
	char str[32];

	Com_sprintf(str, sizeof(str), "%s=%s", var, value);

	return putenv((char *) str);
#endif /* __APPLE__ */
}

/**
 * @sa Q_strncmp
 */
int Q_strcmp (const char *s1, const char *s2)
{
	return strncmp(s1, s2, 99999);
}

/**
 * @sa Q_strcmp
 */
int Q_strncmp (const char *s1, const char *s2, size_t n)
{
	return strncmp(s1, s2, n);
}

/* PATCH: matt */
/* use our own strncasecmp instead of this implementation */
#ifdef sun

#define Q_strncasecmp(s1, s2, n) (strncasecmp(s1, s2, n))

int Q_stricmp (const char *s1, const char *s2)
{
	return strcasecmp(s1, s2);
}

#else							/* sun */
/* FIXME: replace all Q_stricmp with Q_strcasecmp */
int Q_stricmp (const char *s1, const char *s2)
{
#ifdef _WIN32
	return _stricmp(s1, s2);
#else
	return strcasecmp(s1, s2);
#endif
}

int Q_strncasecmp (const char *s1, const char *s2, size_t n)
{
	int c1, c2;

	do {
		c1 = *s1++;
		c2 = *s2++;

		if (!n--)
			return 0;			/* strings are equal until end point */

		if (c1 != c2) {
			if (c1 >= 'a' && c1 <= 'z')
				c1 -= ('a' - 'A');
			if (c2 >= 'a' && c2 <= 'z')
				c2 -= ('a' - 'A');
			if (c1 != c2)
				return -1;		/* strings not equal */
		}
	} while (c1);

	return 0;					/* strings are equal */
}
#endif							/* sun */

/**
 * @brief Safe strncpy that ensures a trailing zero
 * @param dest Destination pointer
 * @param src Source pointer
 * @param destsize Size of destination buffer (this should be a sizeof size due to portability)
 */
#ifdef DEBUG
void Q_strncpyzDebug (char *dest, const char *src, size_t destsize, const char *file, int line)
#else
void Q_strncpyz (char *dest, const char *src, size_t destsize)
#endif
{
#ifdef DEBUG
	if (!dest)
		Sys_Error("Q_strncpyz: NULL dest (%s, %i)", file, line);
	if (!src)
		Sys_Error("Q_strncpyz: NULL src (%s, %i)", file, line);
	if (destsize < 1)
		Sys_Error("Q_strncpyz: destsize < 1 (%s, %i)", file, line);
#endif

	/* space for \0 terminating */
	while (*src && destsize - 1) {
		*dest++ = *src++;
		destsize--;
	}
#ifdef PARANOID
	if (*src)
		Com_Printf("Buffer too small: %s: %i (%s)\n", file, line, src);
	/* the rest is filled with null */
	memset(dest, 0, destsize);
#else
	/* terminate the string */
	*dest = '\0';
#endif
}

/**
 * @brief Safely (without overflowing the destination buffer) concatenates two strings.
 * @param[in] dest the destination string.
 * @param[in] src the source string.
 * @param[in] destsize the total size of the destination buffer.
 * @return pointer destination string.
 * never goes past bounds or leaves without a terminating 0
 */
void Q_strcat (char *dest, const char *src, size_t destsize)
{
	size_t dest_length;
	dest_length = strlen(dest);
	if (dest_length >= destsize)
		Sys_Error("Q_strcat: already overflowed");
	Q_strncpyz(dest + dest_length, src, destsize - dest_length);
}

int Q_strcasecmp (const char *s1, const char *s2)
{
	return Q_strncasecmp(s1, s2, 99999);
}

/**
 * @return false if overflowed - true otherwise
 */
qboolean Com_sprintf (char *dest, size_t size, const char *fmt, ...)
{
	size_t len;
	va_list argptr;
	static char bigbuffer[0x10000];

	if (!fmt)
		return qfalse;

	va_start(argptr, fmt);
	len = Q_vsnprintf(bigbuffer, sizeof(bigbuffer), fmt, argptr);
	va_end(argptr);

	bigbuffer[sizeof(bigbuffer)-1] = 0;

	Q_strncpyz(dest, bigbuffer, size);

	if (len >= size) {
#ifdef PARANOID
		Com_Printf("Com_sprintf: overflow of "UFO_SIZE_T" in "UFO_SIZE_T"\n", len, size);
#endif
		return qfalse;
	}
	return qtrue;
}

/**
 * @brief Safe (null terminating) vsnprintf implementation
 */
int Q_vsnprintf (char *str, size_t size, const char *format, va_list ap)
{
	int len;

#if defined(_WIN32)
	len = _vsnprintf(str, size, format, ap);
	str[size-1] = '\0';
#ifdef DEBUG
	if (len == -1)
		Com_Printf("Q_vsnprintf: string was truncated - target buffer too small\n");
#endif
#else
	len = vsnprintf(str, size, format, ap);
#ifdef DEBUG
	if ((size_t)len >= size)
		Com_Printf("Q_vsnprintf: string was truncated - target buffer too small\n");
#endif
#endif

	return len;
}
