/**
 * @file q_shared.c
 * @brief Common functions.
 */

/*
All original materal Copyright (C) 2002-2006 UFO: Alien Invasion team.

26/06/06, Eddy Cullen (ScreamingWithNoSound):
	Reformatted to agreed style.
	Added doxygen file comment.
	Updated copyright notice.

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

#ifdef Q2_MMX_ENABLED
/* used for mmx optimizations */
#include <xmmintrin.h>
#endif

#define DEG2RAD( a ) (( a * M_PI ) / 180.0F)

vec3_t vec3_origin = { 0, 0, 0 };
vec4_t vec4_origin = { 0, 0, 0, 0 };

/* this is used to access the skill and ability arrays for the current campaign */
static int globalCampaignID = -1;

#define RT2	0.707107

const int dvecs[DIRECTIONS][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, -1}, {-1, 1}, {1, -1} };
const float dvecsn[DIRECTIONS][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {RT2, RT2}, {-RT2, -RT2}, {-RT2, RT2}, {RT2, -RT2} };
const float dangle[DIRECTIONS] = { 0, 180.0f, 90.0f, 270.0f, 45.0f, 225.0f, 135.0f, 315.0f };

const byte dvright[DIRECTIONS] = { 7, 6, 4, 5, 0, 1, 2, 3 };
const byte dvleft[DIRECTIONS] = { 4, 5, 6, 7, 2, 3, 1, 0 };

/*===========================================================================*/

const char *pa_format[] =
{
	"",					/* PA_NULL */
	"b",				/* PA_TURN */
	"g",				/* PA_MOVE */
	"s",				/* PA_STATE */
	"gbb",				/* PA_SHOOT */
	"bbbbbb",			/* PA_INVMOVE */
	"lll"				/* PA_REACT_SELECT */
};

/*===========================================================================*/

/**
 * @brief Sorts some strings
 * @note sort function pointer for qsort
 */
int Q_StringSort (const void *string1, const void *string2)
{
 	char *s1, *s2;
 	s1 = (char*)string1;
 	s2 = (char*)string2;
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
 * @brief
 */
int AngleToDV (int angle)
{
	int i, mini;
	int div, minDiv;

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
}

/*============================================================================ */

#ifdef _MSC_VER
#pragma optimize( "", off )
#endif

/**
 * @brief
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

#ifdef _MSC_VER
#pragma optimize( "", on )
#endif

/**
 * @brief
 */
void AngleVectors (vec3_t angles, vec3_t forward, vec3_t right, vec3_t up)
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
 * @brief
 */
void ProjectPointOnPlane (vec3_t dst, const vec3_t p, const vec3_t normal)
{
	float d;
	vec3_t n;
	float inv_denom;

	inv_denom = 1.0F / DotProduct(normal, normal);

	d = DotProduct(normal, p) * inv_denom;

	n[0] = normal[0] * inv_denom;
	n[1] = normal[1] * inv_denom;
	n[2] = normal[2] * inv_denom;

	dst[0] = p[0] - d * n[0];
	dst[1] = p[1] - d * n[1];
	dst[2] = p[2] - d * n[2];
}

/**
 * @brief
 * @note assumes "src" is normalized
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
 * @brief
 * @param
 */
void R_ConcatRotations (float in1[3][3], float in2[3][3], float out[3][3])
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
 * @brief
 * @param
 * @sa
 */
void R_ConcatTransforms (float in1[3][4], float in2[3][4], float out[3][4])
{
	out[0][0] = in1[0][0] * in2[0][0] + in1[0][1] * in2[1][0] + in1[0][2] * in2[2][0];
	out[0][1] = in1[0][0] * in2[0][1] + in1[0][1] * in2[1][1] + in1[0][2] * in2[2][1];
	out[0][2] = in1[0][0] * in2[0][2] + in1[0][1] * in2[1][2] + in1[0][2] * in2[2][2];
	out[0][3] = in1[0][0] * in2[0][3] + in1[0][1] * in2[1][3] + in1[0][2] * in2[2][3] + in1[0][3];
	out[1][0] = in1[1][0] * in2[0][0] + in1[1][1] * in2[1][0] + in1[1][2] * in2[2][0];
	out[1][1] = in1[1][0] * in2[0][1] + in1[1][1] * in2[1][1] + in1[1][2] * in2[2][1];
	out[1][2] = in1[1][0] * in2[0][2] + in1[1][1] * in2[1][2] + in1[1][2] * in2[2][2];
	out[1][3] = in1[1][0] * in2[0][3] + in1[1][1] * in2[1][3] + in1[1][2] * in2[2][3] + in1[1][3];
	out[2][0] = in1[2][0] * in2[0][0] + in1[2][1] * in2[1][0] + in1[2][2] * in2[2][0];
	out[2][1] = in1[2][0] * in2[0][1] + in1[2][1] * in2[1][1] + in1[2][2] * in2[2][1];
	out[2][2] = in1[2][0] * in2[0][2] + in1[2][1] * in2[1][2] + in1[2][2] * in2[2][2];
	out[2][3] = in1[2][0] * in2[0][3] + in1[2][1] * in2[1][3] + in1[2][2] * in2[2][3] + in1[2][3];
}

/**
 * @brief
 */
void Print3Vector (const vec3_t v)
{
	Com_Printf("(%f, %f, %f)\n", v[0], v[1], v[2]);
}

/**
 * @brief
 */
void Print2Vector (const vec2_t v)
{
	Com_Printf("(%f, %f)\n", v[0], v[1]);
}

/**
 * @brief Converts longitude and latitude to vector coordinates
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


/*============================================================================ */


/**
 * @brief If the number is < 0, return -1 * number - otherwise return the number
 * @param[in] f
 */
float Q_fabs (float f)
{
#if 0
	if (f >= 0)
		return f;
	return -f;
#else
	int tmp = *(int *) &f;

	tmp &= 0x7FFFFFFF;
	return *(float *) &tmp;
#endif
}

#if defined _M_IX86 && !defined C_ONLY
#pragma warning (disable:4035)
__declspec(naked)
long Q_ftol (float f)
{
	static int tmp;
	__asm fld dword ptr[esp + 4]
	__asm fistp tmp
	__asm mov eax, tmp
	__asm ret
}
#pragma warning (default:4035)
#endif

/**
 * @brief Returns the angle resulting from turning fraction * angle
 * from angle1 to angle2
 * @param
 * @sa
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

/**
 * @brief
 * @param
 * @sa
 * @return 1, 2, or 1 + 2
 */
/* this is the slow, general version */
int BoxOnPlaneSide2 (vec3_t emins, vec3_t emaxs, struct cplane_s *p)
{
	int i;
	float dist1, dist2;
	int sides;
	vec3_t corners[2];

	for (i = 0; i < 3; i++) {
		if (p->normal[i] < 0) {
			corners[0][i] = emins[i];
			corners[1][i] = emaxs[i];
		} else {
			corners[1][i] = emins[i];
			corners[0][i] = emaxs[i];
		}
	}
	dist1 = DotProduct(p->normal, corners[0]) - p->dist;
	dist2 = DotProduct(p->normal, corners[1]) - p->dist;
	sides = 0;
	if (dist1 >= 0)
		sides = 1;
	if (dist2 < 0)
		sides |= 2;

	return sides;
}

#if !id386 || defined __linux__ || __MINGW32__ || defined __FreeBSD__ || defined __NetBSD__|| defined __sun || defined __sgi
int BoxOnPlaneSide (vec3_t emins, vec3_t emaxs, struct cplane_s *p)
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
#else
#pragma warning( disable: 4035 )

__declspec(naked)
int BoxOnPlaneSide (vec3_t emins, vec3_t emaxs, struct cplane_s *p)
{
	static int bops_initialized;
	static int Ljmptab[8];

	__asm {

		push ebx
		cmp bops_initialized, 1
		je initialized
		mov bops_initialized, 1
		mov Ljmptab[0 * 4], offset Lcase0
		mov Ljmptab[1 * 4], offset Lcase1
		mov Ljmptab[2 * 4], offset Lcase2
		mov Ljmptab[3 * 4], offset Lcase3
		mov Ljmptab[4 * 4], offset Lcase4
		mov Ljmptab[5 * 4], offset Lcase5
		mov Ljmptab[6 * 4], offset Lcase6
		mov Ljmptab[7 * 4], offset Lcase7
initialized:
		mov edx, ds:dword ptr[4 + 12 + esp]
		mov ecx, ds:dword ptr[4 + 4 + esp]
		xor eax, eax
		mov ebx, ds:dword ptr[4 + 8 + esp]
		mov al, ds:byte ptr[17 + edx]
		cmp al, 8
		jge Lerror
		fld ds:dword ptr[0 + edx]
		fld st(0)
		jmp dword ptr[Ljmptab + eax * 4]
Lcase0:
		fmul ds:dword ptr[ebx]
		fld ds:dword ptr[0 + 4 + edx]
		fxch st(2)
		fmul ds:dword ptr[ecx]
		fxch st(2)
		fld st(0)
		fmul ds:dword ptr[4 + ebx]
		fld ds:dword ptr[0 + 8 + edx]
		fxch st(2)
		fmul ds:dword ptr[4 + ecx]
		fxch st(2)
		fld st(0)
		fmul ds:dword ptr[8 + ebx]
		fxch st(5)
		faddp st(3), st(0)
		fmul ds:dword ptr[8 + ecx]
		fxch st(1)
		faddp st(3), st(0)
		fxch st(3)
		faddp st(2), st(0)
		jmp LSetSides
Lcase1:
		fmul ds:dword ptr[ecx]
		fld ds:dword ptr[0 + 4 + edx]
		fxch st(2)
		fmul ds:dword ptr[ebx]
		fxch st(2)
		fld st(0)
		fmul ds:dword ptr[4 + ebx]
		fld ds:dword ptr[0 + 8 + edx]
		fxch st(2)
		fmul ds:dword ptr[4 + ecx]
		fxch st(2)
		fld st(0)
		fmul ds:dword ptr[8 + ebx]
		fxch st(5)
		faddp st(3), st(0)
		fmul ds:dword ptr[8 + ecx]
		fxch st(1)
		faddp st(3), st(0)
		fxch st(3)
		faddp st(2), st(0)
		jmp LSetSides
Lcase2:
		fmul ds:dword ptr[ebx]
		fld ds:dword ptr[0 + 4 + edx]
		fxch st(2)
		fmul ds:dword ptr[ecx]
		fxch st(2)
		fld st(0)
		fmul ds:dword ptr[4 + ecx]
		fld ds:dword ptr[0 + 8 + edx]
		fxch st(2)
		fmul ds:dword ptr[4 + ebx]
		fxch st(2)
		fld st(0)
		fmul ds:dword ptr[8 + ebx]
		fxch st(5)
		faddp st(3), st(0)
		fmul ds:dword ptr[8 + ecx]
		fxch st(1)
		faddp st(3), st(0)
		fxch st(3)
		faddp st(2), st(0)
		jmp LSetSides
Lcase3:
		fmul ds:dword ptr[ecx]
		fld ds:dword ptr[0 + 4 + edx]
		fxch st(2)
		fmul ds:dword ptr[ebx]
		fxch st(2)
		fld st(0)
		fmul ds:dword ptr[4 + ecx]
		fld ds:dword ptr[0 + 8 + edx]
		fxch st(2)
		fmul ds:dword ptr[4 + ebx]
		fxch st(2)
		fld st(0)
		fmul ds:dword ptr[8 + ebx]
		fxch st(5)
		faddp st(3), st(0)
		fmul ds:dword ptr[8 + ecx]
		fxch st(1)
		faddp st(3), st(0)
		fxch st(3)
		faddp st(2), st(0)
		jmp LSetSides
Lcase4:
		fmul ds:dword ptr[ebx]
		fld ds:dword ptr[0 + 4 + edx]
		fxch st(2)
		fmul ds:dword ptr[ecx]
		fxch st(2)
		fld st(0)
		fmul ds:dword ptr[4 + ebx]
		fld ds:dword ptr[0 + 8 + edx]
		fxch st(2)
		fmul ds:dword ptr[4 + ecx]
		fxch st(2)
		fld st(0)
		fmul ds:dword ptr[8 + ecx]
		fxch st(5)
		faddp st(3), st(0)
		fmul ds:dword ptr[8 + ebx]
		fxch st(1)
		faddp st(3), st(0)
		fxch st(3)
		faddp st(2), st(0)
		jmp LSetSides
Lcase5:
		fmul ds:dword ptr[ecx]
		fld ds:dword ptr[0 + 4 + edx]
		fxch st(2)
		fmul ds:dword ptr[ebx]
		fxch st(2)
		fld st(0)
		fmul ds:dword ptr[4 + ebx]
		fld ds:dword ptr[0 + 8 + edx]
		fxch st(2)
		fmul ds:dword ptr[4 + ecx]
		fxch st(2)
		fld st(0)
		fmul ds:dword ptr[8 + ecx]
		fxch st(5)
		faddp st(3), st(0)
		fmul ds:dword ptr[8 + ebx]
		fxch st(1)
		faddp st(3), st(0)
		fxch st(3)
		faddp st(2), st(0)
		jmp LSetSides
Lcase6:
		fmul ds:dword ptr[ebx]
		fld ds:dword ptr[0 + 4 + edx]
		fxch st(2)
		fmul ds:dword ptr[ecx]
		fxch st(2)
		fld st(0)
		fmul ds:dword ptr[4 + ecx]
		fld ds:dword ptr[0 + 8 + edx]
		fxch st(2)
		fmul ds:dword ptr[4 + ebx]
		fxch st(2)
		fld st(0)
		fmul ds:dword ptr[8 + ecx]
		fxch st(5)
		faddp st(3), st(0)
		fmul ds:dword ptr[8 + ebx]
		fxch st(1)
		faddp st(3), st(0)
		fxch st(3)
		faddp st(2), st(0)
		jmp LSetSides
Lcase7:
		fmul ds:dword ptr[ecx]
		fld ds:dword ptr[0 + 4 + edx]
		fxch st(2)
		fmul ds:dword ptr[ebx]
		fxch st(2)
		fld st(0)
		fmul ds:dword ptr[4 + ecx]
		fld ds:dword ptr[0 + 8 + edx]
		fxch st(2)
		fmul ds:dword ptr[4 + ebx]
		fxch st(2)
		fld st(0)
		fmul ds:dword ptr[8 + ecx]
		fxch st(5)
		faddp st(3), st(0)
		fmul ds:dword ptr[8 + ebx]
		fxch st(1)
		faddp st(3), st(0)
		fxch st(3)
		faddp st(2), st(0)
LSetSides:
		faddp st(2), st(0)
		fcomp ds:dword ptr[12 + edx]
		xor ecx, ecx
		fnstsw ax
		fcomp ds:dword ptr[12 + edx]
		and ah, 1
		xor ah, 1
		add cl, ah
		fnstsw ax
		and ah, 1
		add ah, ah
		add cl, ah
		pop ebx
		mov eax, ecx
		ret
Lerror:
		int 3
	}
}

#pragma warning( default: 4035 )
#endif
/**
 * @brief
 * @param[in] mins
 * @param[in] maxs
 */
void ClearBounds (vec3_t mins, vec3_t maxs)
{
	mins[0] = mins[1] = mins[2] = 99999;
	maxs[0] = maxs[1] = maxs[2] = -99999;
}

/**
 * @brief If the point is outside the box defined by mins and maxs, expand
 * @param[in] v
 * @param[in] mins
 * @param[in] maxs
 */
void AddPointToBounds (vec3_t v, vec3_t mins, vec3_t maxs)
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

/**
 * @brief
 * @param[in] v1
 * @param[in] v2
 * @param[in] comp
 * @return qboolean
 */
qboolean VectorNearer (vec3_t v1, vec3_t v2, vec3_t comp)
{
	int i;

	for (i = 0; i < 3; i++)
		if (fabs(v1[i] - comp[i]) < fabs(v2[i] - comp[i]))
			return qtrue;

	return qfalse;
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
 * @brief Calculated the normal vector for a given vec3_t
 * @param[in] v Vector to normalize
 * @param[out] out The normalized vector
 * @sa VectorNormalize
 * @return vector length as vec_t
 * @sa CrossProduct
 */
vec_t VectorNormalize2 (vec3_t v, vec3_t out)
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
 * @param
 * @sa
 */
void VectorMA (vec3_t veca, float scale, vec3_t vecb, vec3_t vecc)
{
	vecc[0] = veca[0] + scale * vecb[0];
	vecc[1] = veca[1] + scale * vecb[1];
	vecc[2] = veca[2] + scale * vecb[2];
}

/**
 * @brief
 * @param
 * @sa
 */
void VectorClampMA (vec3_t veca, float scale, vec3_t vecb, vec3_t vecc)
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
 * @brief
 * @param
 * @sa VectorNormalize
 * @sa CrossProduct
 */
void MakeNormalVectors (vec3_t forward, vec3_t right, vec3_t up)
{
	float		d;

	/* this rotate and negat guarantees a vector */
	/* not colinear with the original */
	right[1] = -forward[0];
	right[2] = forward[1];
	right[0] = forward[2];

	d = DotProduct(right, forward);
	VectorMA(right, -d, forward, right);
	VectorNormalize(right);
	CrossProduct(right, forward, up);
}


/**
 * @brief
 * @param
 * @sa GLMatrixMultiply
 */
void MatrixMultiply (vec3_t a[3], vec3_t b[3], vec3_t c[3])
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
 * @brief
 * @param
 * @sa MatrixMultiply
 */
void GLMatrixMultiply (float a[16], float b[16], float c[16])
{
	int i, j, k;

	for (j = 0; j < 4; j++) {
		k = j * 4;
		for (i = 0; i < 4; i++)
			c[i + k] = a[i] * b[k] + a[i + 4] * b[k + 1] + a[i + 8] * b[k + 2] + a[i + 12] * b[k + 3];
	}
}


/**
 * @brief
 * @param
 * @sa
 */
void GLVectorTransform (float m[16], vec4_t in, vec4_t out)
{
	int i;

	for (i = 0; i < 4; i++)
		out[i] = m[i] * in[0] + m[i + 4] * in[1] + m[i + 8] * in[2] + m[i + 12] * in[3];
}


/**
 * @brief
 * @param
 * @sa
 */
void VectorRotate (vec3_t m[3], vec3_t va, vec3_t vb)
{
	vb[0] = m[0][0] * va[0] + m[1][0] * va[1] + m[2][0] * va[2];
	vb[1] = m[0][1] * va[0] + m[1][1] * va[1] + m[2][1] * va[2];
	vb[2] = m[0][2] * va[0] + m[1][2] * va[1] + m[2][2] * va[2];
}


/**
 * @brief binary operation which takes two vectors returns a scalar quantity
 * It is the standard inner product of the Euclidean space.
 * @note also known as the scalar product
 * @param[in] v1
 * @param[in] v2
 * @sa CrossProduct
 */
vec_t _DotProduct (vec3_t v1, vec3_t v2)
{
	return v1[0] * v2[0] + v1[1] * v2[1] + v1[2] * v2[2];
}

/**
 * @brief
 * @param
 * @sa _VectorAdd
 */
void _VectorSubtract (vec3_t veca, vec3_t vecb, vec3_t out)
{
#ifdef Q2_MMX_ENABLED
	/* raynorpat: msvc sse optimization */
	__m128 xmm_veca, xmm_vecb, xmm_out;

	xmm_veca = _mm_load_ss(&veca[0]);
	xmm_vecb = _mm_load_ss(&vecb[0]);
	xmm_out = _mm_sub_ss(xmm_veca, xmm_vecb);
	_mm_store_ss(&out[0], xmm_out);

	xmm_veca = _mm_load_ss(&veca[1]);
	xmm_vecb = _mm_load_ss(&vecb[1]);
	xmm_out = _mm_sub_ss(xmm_veca, xmm_vecb);
	_mm_store_ss(&out[1], xmm_out);

	xmm_veca = _mm_load_ss(&veca[2]);
	xmm_vecb = _mm_load_ss(&vecb[2]);
	xmm_out = _mm_sub_ss(xmm_veca, xmm_vecb);
	_mm_store_ss(&out[2], xmm_out);
#else
	out[0] = veca[0] - vecb[0];
	out[1] = veca[1] - vecb[1];
	out[2] = veca[2] - vecb[2];
#endif
}

/**
 * @brief
 * @param
 * @sa _VectorSubtract
 */
void _VectorAdd (vec3_t veca, vec3_t vecb, vec3_t out)
{
#ifdef Q2_MMX_ENABLED
	/* raynorpat: msvc sse optimization */
	__m128 xmm_veca, xmm_vecb, xmm_out;

	xmm_veca = _mm_load_ss(&veca[0]);
	xmm_vecb = _mm_load_ss(&vecb[0]);
	xmm_out = _mm_add_ss(xmm_veca, xmm_vecb);
	_mm_store_ss(&out[0], xmm_out);

	xmm_veca = _mm_load_ss(&veca[1]);
	xmm_vecb = _mm_load_ss(&vecb[1]);
	xmm_out = _mm_add_ss(xmm_veca, xmm_vecb);
	_mm_store_ss(&out[1], xmm_out);

	xmm_veca = _mm_load_ss(&veca[2]);
	xmm_vecb = _mm_load_ss(&vecb[2]);
	xmm_out = _mm_add_ss(xmm_veca, xmm_vecb);
	_mm_store_ss(&out[2], xmm_out);
#else
	out[0] = veca[0] + vecb[0];
	out[1] = veca[1] + vecb[1];
	out[2] = veca[2] + vecb[2];
#endif
}

/**
 * @brief
 * @param
 * @sa
 */
void _VectorCopy (vec3_t in, vec3_t out)
{
#ifdef Q2_MMX_ENABLED
	/* raynorpat: msvc sse optimization */
	__m128 xmm_in;

	xmm_in = _mm_load_ss(&in[0]);
	_mm_store_ss(&out[0], xmm_in);

	xmm_in = _mm_load_ss(&in[1]);
	_mm_store_ss(&out[1], xmm_in);

	xmm_in = _mm_load_ss(&in[2]);
	_mm_store_ss(&out[2], xmm_in);
#else
	out[0] = in[0];
	out[1] = in[1];
	out[2] = in[2];
#endif
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
 * @param[in] cross output
 * @example
 * you have the right and forward values of an axis, their cross product will
 * be a properly oriented "up" direction
 * @sa _DotProduct
 * @sa DotProduct
 * @sa VectorNormalize2
 */
void CrossProduct (vec3_t v1, vec3_t v2, vec3_t cross)
{
	cross[0] = v1[1] * v2[2] - v1[2] * v2[1];
	cross[1] = v1[2] * v2[0] - v1[0] * v2[2];
	cross[2] = v1[0] * v2[1] - v1[1] * v2[0];
}

double sqrt (double x);

/**
 * @brief
 * @param[in] v Vector to calculate the length of
 * @sa VectorNormalize
 * @return Vector length as vec_t
 */
vec_t VectorLength (vec3_t v)
{
	int i;
	float length;

	length = 0;
	for (i = 0; i < 3; i++)
		length += v[i] * v[i];
	length = sqrt(length);		/* FIXME */

	return length;
}

/**
 * @brief
 * @param
 * @sa
 */
void VectorInverse (vec3_t v)
{
	v[0] = -v[0];
	v[1] = -v[1];
	v[2] = -v[2];
}

/**
 * @brief
 * @param
 * @sa
 */
void VectorScale (vec3_t in, vec_t scale, vec3_t out)
{
#ifdef Q2_MMX_ENABLED
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


/**
 * @brief
 * @param
 * @sa
 */
int Q_log2 (int val)
{
	int answer = 0;

	while (val >>= 1)
		answer++;
	return answer;
}

/**
  * @brief Return random values between 0 and 1
  */
float frand (void)
{
	return (rand() & 32767) * (1.0 / 32767);
}


/**
  * @brief Return random values between -1 and 1
  */
float crand (void)
{
	return (rand() & 32767) * (2.0 / 32767) - 1;
}

/**
 * @brief generate two gaussian distributed random numbers with median at 0 and stdev of 1
 * @param pointers to two floats that need to be set. both have to be provided.
 */
void gaussrand (float *gauss1, float *gauss2)
{
	float x1,x2,w,tmp;

	do {
		x1 = crand();
		x2 = crand();
		w = x1 * x1 + x2 * x2;
	} while ( w >= 1.0 );

	tmp = -2 * logf(w) ;
	w = sqrt( tmp / w );
	*gauss1 = x1 * w;
	*gauss2 = x2 * w;
}

/**
 * @brief
 * @param
 * @sa Com_SkipTokens
 */
static qboolean Com_CharIsOneOfCharset (char c, char *set)
{
	unsigned int i;

	for (i = 0; i < strlen(set); i++) {
		if (set[i] == c)
			return qtrue;
	}

	return qfalse;
}

/**
 * @brief
 * @param
 * @sa Com_CharIsOneOfCharset
 */
char *Com_SkipCharset (char *s, char *sep)
{
	char *p = s;

	while (p) {
		if (Com_CharIsOneOfCharset(*p, sep))
			p++;
		else
			break;
	}

	return p;
}

/**
 * @brief
 * @param
 * @sa Com_CharIsOneOfCharset
 */
char *Com_SkipTokens (char *s, int numTokens, char *sep)
{
	int sepCount = 0;
	char *p = s;

	while (sepCount < numTokens) {
		if (Com_CharIsOneOfCharset(*p++, sep)) {
			sepCount++;
			while (Com_CharIsOneOfCharset(*p, sep))
				p++;
		} else if (*p == '\0')
			break;
	}

	if (sepCount == numTokens)
		return p;
	else
		return s;
}

/**
 * @brief Returns just the filename from a given path
 * @param
 * @sa COM_StripExtension
 */
char *COM_SkipPath (char *pathname)
{
	char *last;

	last = pathname;
	while (*pathname) {
		if (*pathname == '/')
			last = pathname + 1;
		pathname++;
	}
	return last;
}

/**
 * @brief Removed the file extension from a filename
 * @param
 * @sa COM_SkipPath
 */
void COM_StripExtension (const char *in, char *out)
{
	while (*in && *in != '.')
		*out++ = *in++;
	*out = 0;
}

/**
 * @brief Return the file extension from the first .
 * @param
 * @sa
 */
char *COM_FileExtension (const char *in)
{
	static char exten[8];
	int i;

	while (*in && *in != '.')
		in++;
	if (!*in)
		return "";
	in++;
	for (i = 0; i < 7 && *in; i++, in++)
		exten[i] = *in;
	exten[i] = 0;
	return exten;
}

/**
 * @brief
 * @param
 * @sa
 */
void COM_FileBase (const char *in, char *out)
{
	const char *s, *s2;

	s = in + strlen(in) - 1;

	while (s != in && *s != '.')
		s--;

	for (s2 = s; s2 != in && *s2 != '/'; s2--);

	if (s - s2 < 2)
		out[0] = 0;
	else {
		s--;
		strncpy(out, s2 + 1, s - s2);
		out[s - s2] = 0;
	}
}

/**
  * @brief Returns the path up to, but not including the last /
  */
void COM_FilePath (const char *in, char *out)
{
	const char *s;

	s = in + strlen(in) - 1;

	while (s != in && *s != '/')
		s--;

	/* FIXME: Q_strncpyz */
	strncpy(out, in, s - in);
	out[s - in] = 0;
}

/*
============================================================================
BYTE ORDER FUNCTIONS
============================================================================
*/

static qboolean bigendien;

/* can't just use function pointers, or dll linkage can */
/* mess up when qcommon is included in multiple places */
short (*_BigShort)(short l);
short (*_LittleShort)(short l);
int (*_BigLong)(int l);
int (*_LittleLong)(int l);
float (*_BigFloat)(float l);
float (*_LittleFloat)(float l);

short BigShort (short l)
{
	return _BigShort(l);
}
short LittleShort (short l)
{
	return _LittleShort(l);
}
int BigLong (int l)
{
	return _BigLong(l);
}
int LittleLong (int l)
{
	return _LittleLong(l);
}
float BigFloat (float l)
{
	return _BigFloat(l);
}
float LittleFloat (float l)
{
	return _LittleFloat(l);
}

/**
 * @brief
 * @param[in] l
 * @sa ShortNoSwap
 */
short ShortSwap (short l)
{
	byte b1, b2;

	b1 = l & 255;
	b2 = (l >> 8) & 255;

	return (b1 << 8) + b2;
}

/**
 * @brief
 * @param[in] l
 * @sa ShortSwap
 */
short ShortNoSwap (short l)
{
	return l;
}

/**
 * @brief
 * @param[in] l
 * @sa LongNoSwap
 */
int LongSwap (int l)
{
	byte b1, b2, b3, b4;

	b1 = l & UCHAR_MAX;
	b2 = (l >> 8) & UCHAR_MAX;
	b3 = (l >> 16) & UCHAR_MAX;
	b4 = (l >> 24) & UCHAR_MAX;

	return ((int) b1 << 24) + ((int) b2 << 16) + ((int) b3 << 8) + b4;
}

/**
 * @brief
 * @param[in] l
 * @sa LongSwap
 */
int LongNoSwap (int l)
{
	return l;
}

/**
 * @brief
 * @param[in] l
 * @sa FloatNoSwap
 */
float FloatSwap (float f)
{
	union float_u {
		float f;
		byte b[4];
	} dat1, dat2;


	dat1.f = f;
	dat2.b[0] = dat1.b[3];
	dat2.b[1] = dat1.b[2];
	dat2.b[2] = dat1.b[1];
	dat2.b[3] = dat1.b[0];
	return dat2.f;
}

/**
 * @brief
 * @param[in] l
 * @sa FloatSwap
 */
float FloatNoSwap (float f)
{
	return f;
}

/**
 * @brief
 * @param
 * @sa
 */
void Swap_Init (void)
{
	byte swaptest[2] = { 1, 0 };

	/* set the byte swapping variables in a portable manner */
	if (*(short *) swaptest == 1) {
		bigendien = qfalse;
		_BigShort = ShortSwap;
		_LittleShort = ShortNoSwap;
		_BigLong = LongSwap;
		_LittleLong = LongNoSwap;
		_BigFloat = FloatSwap;
		_LittleFloat = FloatNoSwap;
	} else {
		bigendien = qtrue;
		_BigShort = ShortNoSwap;
		_LittleShort = ShortSwap;
		_BigLong = LongNoSwap;
		_LittleLong = LongSwap;
		_BigFloat = FloatNoSwap;
		_LittleFloat = FloatSwap;
	}

}

#define VA_BUFSIZE 4096
/**
 * @brief does a varargs printf into a temp buffer, so I don't need to have
 * varargs versions of all text functions.
 */
char *va (char *format, ...)
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
 * @brief Parse a token out of a string
 * @param
 * @sa COM_EParse
 */
char *COM_Parse (char **data_p)
{
	static char com_token[4096];
	int c;
	size_t len;
	char *data;

	data = *data_p;
	len = 0;
	com_token[0] = 0;

	if (!data) {
		*data_p = NULL;
		return "";
	}

	/* skip whitespace */
skipwhite:
	while ((c = *data) <= ' ') {
		if (c == 0) {
			*data_p = NULL;
			return "";
		}
		data++;
	}

	if (c == '/' && data[1] == '*') {
		int clen = 0;
		data += 2;
		while (!((data[clen] && data[clen] == '*') && (data[clen+1] && data[clen+1] == '/'))) {
			clen++;
		}
		data[clen] = 0;
		Com_DPrintf("Com_Parse: multiline comment: %s\n", data);
		data += clen + 2; /* skip end of multiline comment */
		goto skipwhite;
	}

	/* skip // comments */
	if (c == '/' && data[1] == '/') {
		while (*data && *data != '\n')
			data++;
		goto skipwhite;
	}

	/* handle quoted strings specially */
	if (c == '\"') {
		data++;
		while (1) {
			c = *data++;
			if (c == '\"' || !c) {
				com_token[len] = 0;
				*data_p = data;
				return com_token;
			}
			if (len < sizeof(com_token)) {
				com_token[len] = c;
				len++;
			} else {
				Com_Printf("Com_Parse len exceeded: %Zu/MAX_TOKEN_CHARS\n", len);
			}
		}
	}

	/* parse a regular word */
	do {
		if (len < sizeof(com_token)) {
			com_token[len] = c;
			len++;
		}
		data++;
		c = *data;
	} while (c > 32);

	if (len == sizeof(com_token)) {
		Com_Printf("Token exceeded %i chars, discarded.\n", MAX_TOKEN_CHARS);
		len = 0;
	}
	com_token[len] = 0;

	*data_p = data;
	return com_token;
}


/**
 * @brief
 * @param
 * @sa Com_Parse
 */
char *COM_EParse (char **text, const char *errhead, char *errinfo)
{
	char *token;

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


static int paged_total;
/**
 * @brief
 * @param
 * @sa
 */
void Com_PageInMemory (byte * buffer, int size)
{
	int i;

	for (i = size - 1; i > 0; i -= 4096)
		paged_total += buffer[i];
}


/*
============================================================================
LIBRARY REPLACEMENT FUNCTIONS
============================================================================
*/

/**
 * @brief
 * @param
 * @sa
 */
char *Q_strlwr (char *str)
{
#ifdef _MSC_VER
	return _strlwr(str);
#else
	#if !defined (__APPLE__) && !defined (MACOSX)
		return strlwr(str);
	#else
		char* origs = str;
		while (*str) {
			*str = tolower(*str);
			str++;
		}
		return origs;
	#endif
#endif
}

/**
 * @brief Duplicates a string
 * @param[in] str String to duplicate
 * @note Don't forget to free it afterwards
 */
char *Q_strdup (const char *str)
{
	if (!str)
		return NULL;
#ifdef _MSC_VER
	return _strdup(str);
#else
	return strdup(str);
#endif
}

/**
 * @brief
 * @param
 * @sa
 */
int Q_putenv (const char *var, const char *value)
{
#ifdef __APPLE__
	return setenv(var, value, 1) == -1)
#else
	char str[32];

	Com_sprintf(str, sizeof(str), "%s=%s", var, value);
# ifdef _MSC_VER
	return _putenv(str);
# else
	return putenv((char *) str);
# endif
#endif /* __APPLE__ */
}

/**
 * @brief
 * @param
 * @sa
 */
char *Q_getcwd (char *dest, size_t size)
{
#ifdef _MSC_VER
	return _getcwd(dest, (int)size);
#else
	return getcwd(dest, size);
#endif
}

/**
 * @brief
 * @param
 * @sa Q_strncmp
 */
int Q_strcmp (const char *s1, const char *s2)
{
	return strncmp(s1, s2, 99999);
}

/**
 * @brief
 * @param
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

/**
 * @brief
 * @param
 * @sa
 */
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

/**
 * @brief
 * @param
 * @sa
 */
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
void Q_strncpyzDebug (char *dest, const char *src, size_t destsize, char *file, int line)
#else
void Q_strncpyz (char *dest, const char *src, size_t destsize)
#endif
{
#ifdef DEBUG
	if (!dest) {
		Sys_Error("Q_strncpyz: NULL dest (%s, %i)", file, line);
		return;	/* never reached. need for code analyst. */
	}
	if (!src) {
		Sys_Error("Q_strncpyz: NULL src (%s, %i)", file, line);
		return;	/* never reached. need for code analyst. */
	}
	if (destsize < 1)
		Sys_Error("Q_strncpyz: destsize < 1 (%s, %i)", file, line);
#endif

#if 0
	strncpy(dest, src, destsize - 1);
	dest[destsize - 1] = 0;
#else
	/* space for \0 terminating */
	while (*src && destsize - 1) {
		*dest++ = *src++;
		destsize--;
	}
	/* the rest is filled with null */
#if 1
	memset(dest, 0, destsize);
#else
	while (destsize--)
		*dest++ = 0;
#endif
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

/**
 * @brief
 * @param
 * @sa
 */
int Q_strcasecmp (const char *s1, const char *s2)
{
	return Q_strncasecmp(s1, s2, 99999);
}

/**
 * @brief
 * @param
 * @return false if overflowed - true otherwise
 * @sa
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
		Com_Printf("Com_sprintf: overflow of %Zu in %Zu\n", len, size);
#endif
		return qfalse;
	}
	return qtrue;
}

/*
=====================================================================
INFO STRINGS
=====================================================================
*/

/**
 * @brief Searches the string for the given key and returns the associated value, or an empty string.
 * @param
 * @sa
 */
char *Info_ValueForKey(char *s, char *key)
{
	char pkey[512];
	/* use two buffers so compares */
	static char value[2][512];

	/* work without stomping on each other */
	static int valueindex = 0;
	char *o;

	valueindex ^= 1;
	if (*s == '\\')
		s++;
	while (1) {
		o = pkey;
		while (*s != '\\') {
			if (!*s)
				return "";
			*o++ = *s++;
		}
		*o = 0;
		s++;

		o = value[valueindex];

		while (*s != '\\' && *s)
			*o++ = *s++;
		*o = 0;

		if (!Q_stricmp(key, pkey))
			return value[valueindex];

		if (!*s)
			return "";
		s++;
	}
}

/**
 * @brief Searches through s for key and remove is
 * @param[in] s String to search key in
 * @param[in] key String to search for in s
 * @sa Info_SetValueForKey
 */
void Info_RemoveKey(char *s, const char *key)
{
	char *start;
	char pkey[512];
	char value[512];
	char *o;

	if (strstr(key, "\\")) {
/*		Com_Printf("Can't use a key with a \\\n"); */
		return;
	}

	while (1) {
		start = s;
		if (*s == '\\')
			s++;
		o = pkey;
		while (*s != '\\') {
			if (!*s)
				return;
			*o++ = *s++;
		}
		*o = 0;
		s++;

		o = value;
		while (*s != '\\' && *s) {
			if (!*s)
				return;
			*o++ = *s++;
		}
		*o = 0;

		if (!Q_strncmp((char *) key, pkey, 512)) {
			strcpy(start, s);	/* remove this part */
			return;
		}

		if (!*s)
			return;
	}

}


/**
 * @brief Some characters are illegal in info strings because they can mess up the server's parsing
 * @param
 * @sa
 */
qboolean Info_Validate(char *s)
{
	if (strstr(s, "\""))
		return qfalse;
	if (strstr(s, ";"))
		return qfalse;
	return qtrue;
}

/**
 * @brief Adds a new entry into string with given value.
 * @note Removed any old version of the key
 * @param[in] s
 * @sa Info_RemoveKey
 */
void Info_SetValueForKey(char *s, const char *key, const char *value)
{
	char newi[MAX_INFO_STRING];

	if (strstr(key, "\\") || strstr(value, "\\")) {
		Com_Printf("Can't use keys or values with a \\\n");
		return;
	}

	if (strstr(key, ";")) {
		Com_Printf("Can't use keys or values with a semicolon\n");
		return;
	}

	if (strstr(key, "\"") || strstr(value, "\"")) {
		Com_Printf("Can't use keys or values with a \"\n");
		return;
	}

	if (strlen(key) > MAX_INFO_KEY - 1 || strlen(value) > MAX_INFO_KEY - 1) {
		Com_Printf("Keys and values must be < 64 characters.\n");
		return;
	}
	Info_RemoveKey(s, key);
	if (!value || !strlen(value))
		return;

	Com_sprintf(newi, sizeof(newi), "\\%s\\%s", key, value);

	Q_strcat(newi, s, sizeof(newi));
	Q_strncpyz(s, newi, MAX_INFO_STRING);
}


/*
==============================================================
INVENTORY MANAGEMENT
==============================================================
*/

static csi_t *CSI;
static invList_t *invUnused;
static item_t cacheItem = {NONE,NONE,NONE}; /* to crash as soon as possible */

/**
 * @brief
 * @param[in] import
 * @sa InitGame
 * @sa Com_ParseScripts
 */
void Com_InitCSI(csi_t * import)
{
	CSI = import;
}


/**
 * @brief
 * @param[in] invList
 * @sa InitGame
 * @sa CL_ResetSinglePlayerData
 * @sa CL_InitLocal
 */
void Com_InitInventory(invList_t * invList)
{
	invList_t *last;
	int i;

	assert(invList);
#ifdef DEBUG
	if (!invList)
		return;	/* never reached. need for code analyst. */
#endif

	invUnused = invList;
	invUnused->next = NULL;
	for (i = 0; i < MAX_INVLIST - 1; i++) {
		last = invUnused++;
		invUnused->next = last;
	}
}

static int cache_Com_CheckToInventory = 0;

/**
 * @brief
 * @param[in] i
 * @param[in] item
 * @param[in] container
 * @param[in] x
 * @param[in] y
 * @sa
 */
qboolean Com_CheckToInventory(const inventory_t * const i, const int item, const int container, int x, int y)
{
	invList_t *ic;
	static int mask[16];
	int j;

	assert(i);
#ifdef DEBUG
	if (!i)
		return qfalse;	/* never reached. need for code analyst. */
#endif

	assert((container >= 0) && (container < CSI->numIDs));
#ifdef DEBUG
	if ((container < 0) || (container >= CSI->numIDs))
		return qfalse;	/* never reached. need for code analyst. */
#endif

	/* armor vs item */
	if (!Q_strncmp(CSI->ods[item].type, "armor", MAX_VAR)) {
		if (!CSI->ids[container].armor && !CSI->ids[container].all) {
			return qfalse;
		}
	} else if (!Q_strncmp(CSI->ods[item].type, "extension", MAX_VAR)) {
		if (!CSI->ids[container].extension && !CSI->ids[container].all) {
			return qfalse;
		}
	} else if (!Q_strncmp(CSI->ods[item].type, "headgear", MAX_VAR)) {
		if (!CSI->ids[container].headgear && !CSI->ids[container].all) {
			return qfalse;
		}
	} else if (CSI->ids[container].armor) {
		return qfalse;
	} else if (CSI->ids[container].extension) {
		return qfalse;
	} else if (CSI->ids[container].headgear) {
		return qfalse;
	}

	/* twohanded item */
	if (CSI->ods[item].holdtwohanded) {
		if ( (container == CSI->idRight && i->c[CSI->idLeft])
			 || container == CSI->idLeft )
			return qfalse;
	}

	/* left hand is busy if right wields twohanded */
	if ( container == CSI->idLeft && i->c[CSI->idRight]
		 && CSI->ods[i->c[CSI->idRight]->item.t].holdtwohanded )
		return qfalse;

	/* can't put an item that is 'firetwohanded' into the left hand */
	if ( container == CSI->idLeft && CSI->ods[item].firetwohanded)
		return qfalse;

	/* single item containers, e.g. hands */
	if (CSI->ids[container].single) {
		/* there is already an item */
		if (i->c[container])
			return qfalse;
	}

	/* check bounds */
	if (x < 0 || y < 0 || x >= 32 || y >= 16)
		return qfalse;

	if (!cache_Com_CheckToInventory) {
		/* extract shape info */
		for (j = 0; j < 16; j++)
			mask[j] = ~CSI->ids[container].shape[j];

		/* add other items to mask */
		for (ic = i->c[container]; ic; ic = ic->next)
			for (j = 0; j < 4 && ic->y + j < 16; j++)
				mask[ic->y + j] |= ((CSI->ods[ic->item.t].shape >> (j * 8)) & 0xFF) << ic->x;
	}

	/* test for collisions with newly generated mask */
	for (j = 0; j < 4; j++)
		if ((((CSI->ods[item].shape >> (j * 8)) & 0xFF) << x) & mask[y + j])
			return qfalse;

	/* everything ok */
	return qtrue;
}


/**
 * @brief Searches a suitable place in container of a given inventory
 * @param[in] i
 * @param[in] container
 * @param[in] x
 * @param[in] y
 */
invList_t *Com_SearchInInventory(const inventory_t* const i, int container, int x, int y)
{
	invList_t *ic;

	/* only a single item */
	if (CSI->ids[container].single)
		return i->c[container];

	/* more than one item - search for a suitable place in this container */
	for (ic = i->c[container]; ic; ic = ic->next)
		if (x >= ic->x && y >= ic->y && x < ic->x + 8 && y < ic->y + 4 && ((CSI->ods[ic->item.t].shape >> (x - ic->x) >> (y - ic->y) * 8)) & 1)
			return ic;

	/* found nothing */
	return NULL;
}


/**
 * @brief Add an item to a specified container in a given inventory
 * @param[in] i
 * @param[in] item
 * @param[in] container
 * @param[in] x
 * @param[in] y
 */
invList_t *Com_AddToInventory(inventory_t * const i, item_t item, int container, int x, int y)
{
	invList_t *ic;

	if (item.t == NONE)
		return NULL;

	if (!invUnused) {
		Sys_Error("No free inventory space!\n");
		return NULL;	/* never reached. need for code analyst. */
	}

	assert(i);
#ifdef DEBUG
	if (!i)
		return NULL;	/* never reached. need for code analyst. */
#endif

	/* allocate space */
	ic = i->c[container];
	/* set container to next free invUnused slot */
	i->c[container] = invUnused;
	/* ensure, that invUnused will be the next empty slot */
	invUnused = invUnused->next;
	i->c[container]->next = ic;
	ic = i->c[container];
/*	Com_Printf("Add to container %i: %s\n", container, CSI->ods[item.t].id);*/

	/* set the data */
	ic->item = item;
	ic->x = x;
	ic->y = y;
	return ic;
}

/**
 * @brief
 * @param[in] i
 * @param[in] container
 * @param[in] x
 * @param[in] y
 * @sa
 */
qboolean Com_RemoveFromInventory (inventory_t* const i, int container, int x, int y)
{
	invList_t *ic, *old;

	assert(i);
#ifdef DEBUG
	if (!i)
		return qfalse;	/* never reached. need for code analyst. */
#endif

	assert(container < MAX_CONTAINERS);

	ic = i->c[container];
	if (!ic) {
#ifdef PARANOID
		Com_DPrintf("Com_RemoveFromInventory - empty container %s\n", CSI->ids[container].name);
#endif
		return qfalse;
	}

	if (CSI->ids[container].single || (ic->x == x && ic->y == y)) {
		old = invUnused;
		invUnused = ic;
		cacheItem = ic->item;
		i->c[container] = ic->next;

		if (CSI->ids[container].single && ic->next)
			Com_Printf("Com_RemoveFromInventory: Error: single container %s has many items.\n", CSI->ids[container].name);

		invUnused->next = old;
		return qtrue;
	}

	for (; ic->next; ic = ic->next)
		if (ic->next->x == x && ic->next->y == y) {
			old = invUnused;
			invUnused = ic->next;
			cacheItem = ic->next->item;
			ic->next = ic->next->next;
			invUnused->next = old;
			return qtrue;
		}

	return qfalse;
}

/**
 * @brief Conditions for moving items between containers.
 * @param[in] i an item
 * @param[in] from source container
 * @param[in] fx x for source container
 * @param[in] fy y for source container
 * @param[in] to destination container
 * @param[in] tx x for destination container
 * @param[in] ty y for destination container
 * @param[in] TU amount of TU needed to move an item
 * @param[in] icp
 * @return IA_NOTIME when not enough TU
 * @return IA_NONE if no action possible
 * @return IA_NORELOAD if you cannot reload a weapon
 * @return IA_RELOAD_SWAP in case of exchange of ammo in a weapon
 * @return IA_RELOAD when reloading
 * @return IA_ARMOR when placing an armour on the actor
 * @return IA_MOVE when just moving an item
 * @sa
 */
int Com_MoveInInventory (inventory_t* const i, int from, int fx, int fy, int to, int tx, int ty, int *TU, invList_t ** icp)
{
	invList_t *ic;
	int time;

	assert(to >= 0 && to < CSI->numIDs);
	assert(from >= 0 && from < CSI->numIDs);
#ifdef DEBUG
	if (from < 0 || from >= CSI->numIDs)
		return 0;	/* never reached. need for code analyst. */
#endif

	if (icp)
		*icp = NULL;

	if (from == to && fx == tx && fy == ty)
		return 0;

	time = CSI->ids[from].out + CSI->ids[to].in;
	if (from == to)
		time /= 2;
	if (TU && *TU < time)
		return IA_NOTIME;

	assert(i);

	/* break if source item is not removeable */
	if (!Com_RemoveFromInventory(i, from, fx, fy))
		return IA_NONE;

	if (cacheItem.t == NONE)
		return IA_NONE;

	assert(cacheItem.t < MAX_OBJDEFS);

	/* if weapon is twohanded and is moved from hand to hand do nothing. */
	/* twohanded weapon are only in CSI->idRight */
	if (CSI->ods[cacheItem.t].firetwohanded && to == CSI->idLeft && from == CSI->idRight) {
		Com_AddToInventory(i, cacheItem, from, fx, fy);
		return IA_NONE;
	}

	/* if non-armor moved to an armor slot then */
	/* move item back to source location and break */
	/* same for non extension items when moved to an extension slot */
	if ((CSI->ids[to].armor && Q_strcmp(CSI->ods[cacheItem.t].type, "armor"))
	 || (CSI->ids[to].extension && Q_strcmp(CSI->ods[cacheItem.t].type, "extension"))) {
		Com_AddToInventory(i, cacheItem, from, fx, fy);
		return IA_NONE;
	}

	/* check if the target is a blocked inv-armor and source!=dest */
	if (CSI->ids[to].armor && from != to && !Com_CheckToInventory(i, cacheItem.t, to, tx, ty)) {
		item_t cacheItem2;

		/* save/cache (source) item */
		cacheItem2 = cacheItem;
		/* move the destination item to the source */
		Com_MoveInInventory(i, to, tx, ty, from, fx, fy, TU, icp);
		/* reset the cached item (source) */
		cacheItem = cacheItem2;
	} else if (!Com_CheckToInventory(i, cacheItem.t, to, tx, ty)) {
		ic = Com_SearchInInventory(i, to, tx, ty);

		if (ic && INV_LoadableInWeapon(&CSI->ods[cacheItem.t], ic->item.t)) {
			/* TODO (or do this in two places in cl_menu.c):
			if ( !RS_ItemIsResearched(CSI->ods[ic->item.t].id)
				 || !RS_ItemIsResearched(CSI->ods[cacheItem.t].id) ) {
				return IA_NORELOAD;
			} */
			if (ic->item.a >= CSI->ods[ic->item.t].ammo
				&& ic->item.m == cacheItem.t) {
				/* weapon already fully loaded with the same ammunition
				   --- back to source location */
				Com_AddToInventory(i, cacheItem, from, fx, fy);
				return IA_NORELOAD;
			}
			time += CSI->ods[ic->item.t].reload;
			if (!TU || *TU >= time) {
				if (TU)
					*TU -= time;
				if (ic->item.a >= CSI->ods[ic->item.t].ammo) {
					/* exchange ammo */
					item_t item = {0, NONE, ic->item.m};

					Com_AddToInventory(i, item, from, fx, fy);

					ic->item.m = cacheItem.t;
					if (icp)
						*icp = ic;
					return IA_RELOAD_SWAP;
				} else {
					ic->item.m = cacheItem.t;
					/* loose ammo of type ic->item.m saved on server side */
					ic->item.a = CSI->ods[ic->item.t].ammo;
					if (icp)
						*icp = ic;
					return IA_RELOAD;
				}
			}
			/* not enough time - back to source location */
			Com_AddToInventory(i, cacheItem, from, fx, fy);
			return IA_NOTIME;
		}

		/* impossible move - back to source location */
		Com_AddToInventory(i, cacheItem, from, fx, fy);
		return IA_NONE;
	}

	/* twohanded exception - only CSI->idRight is allowed for firetwohanded weapons */
	if (CSI->ods[cacheItem.t].firetwohanded && to == CSI->idLeft) {
#ifdef DEBUG
		Com_DPrintf("Com_MoveInInventory - don't move the item to CSI->idLeft it's firetwohanded\n");
#endif
		to = CSI->idRight;
	}
#ifdef PARANOID
	else if (CSI->ods[cacheItem.t].firetwohanded)
		Com_DPrintf("Com_MoveInInventory: move firetwohanded item to container: %s\n", CSI->ids[to].name);
#endif

	/* successful */
	if (TU)
		*TU -= time;

	ic = Com_AddToInventory(i, cacheItem, to, tx, ty);

	/* return data */
	if (icp)
		*icp = ic;

	if (to == CSI->idArmor) {
		assert(!Q_strcmp(CSI->ods[cacheItem.t].type, "armor"));
		return IA_ARMOR;
	} else
		return IA_MOVE;
}

/**
 * @brief
 * @param[in] i
 * @param[in] container
 * @sa Com_DestroyInventory
 */
void Com_EmptyContainer (inventory_t* const i, const int container)
{
	invList_t *ic, *old;

#ifdef DEBUG
	int cnt = 0;
	if (CSI->ids[container].temp)
		Com_DPrintf("Com_EmptyContainer: Emptying temp container %s.\n", CSI->ids[container].name);
#endif

	assert(i);
#ifdef DEBUG
	if (!i)
		return;	/* never reached. need for code analyst. */
#endif

	ic = i->c[container];

	while (ic) {
		old = ic;
		ic = ic->next;
		old->next = invUnused;
		invUnused = old;
#ifdef DEBUG
		if (cnt >= MAX_INVLIST) {
			Com_Printf("Error: There are more than the allowed entries in container %s (cnt:%i, MAX_INVLIST:%i) (Com_EmptyContainer)\n", CSI->ids[container].name, cnt,
					   MAX_INVLIST);
			break;
		}
		cnt++;
#endif
	}
	i->c[container] = NULL;
}

/**
 * @brief
 * @param i The invetory which should be erased
 * @sa Com_EmptyContainer
 */
void Com_DestroyInventory(inventory_t* const i)
{
	int k;

	if (!i)
		return;

	for (k = 0; k < CSI->numIDs; k++)
		if (CSI->ids[k].temp)
			i->c[k] = NULL;
		else
			Com_EmptyContainer(i, k);
}

/**
 * @brief Finds space for item in inv at container
 * @param[in] inv
 * @param[in] time
 * @param[in] container
 * @param[in] px
 * @param[in] py
 * @sa Com_CheckToInventory
 */
void Com_FindSpace(const inventory_t* const inv, const int item, const int container, int* const px, int* const py)
{
	int x, y;

	assert(inv);
	assert(!cache_Com_CheckToInventory);

	for (y = 0; y < 16; y++)
		for (x = 0; x < 32; x++)
			if (Com_CheckToInventory(inv, item, container, x, y)) {
				cache_Com_CheckToInventory = 0;
				*px = x;
				*py = y;
				return;
			} else {
				cache_Com_CheckToInventory = 1;
			}
	cache_Com_CheckToInventory = 0;

#ifdef PARANOID
	Com_DPrintf("Com_FindSpace: no space for %s: %s in %s\n", CSI->ods[item].type, CSI->ods[item].id, CSI->ids[container].name);
#endif
	*px = *py = NONE;
}

/**
 * @brief Tries to add item to inventory inv at container
 * @param[in] inv Inventory pointer to add the item
 * @param[in] item Item to add to inventory
 * @param[in] container Container id
 * @sa Com_FindSpace
 * @sa Com_AddToInventory
 */
int Com_TryAddToInventory(inventory_t* const inv, item_t item, int container)
{
	int x, y;

	Com_FindSpace(inv, item.t, container, &x, &y);
	if (x == NONE) {
		assert (y == NONE);
		return 0;
	} else {
		Com_AddToInventory(inv, item, container, x, y);
		return 1;
	}
}

/**
 * @brief Tries to add item to buytype inventory inv at container
 * @param[in] inv Inventory pointer to add the item
 * @param[in] item Item to add to inventory
 * @param[in] container Container id
 * @sa Com_FindSpace
 * @sa Com_AddToInventory
 */
int Com_TryAddToBuyType (inventory_t* const inv, item_t item, int container)
{
	int x, y;
	inventory_t hackInv;

	/* link the temp container */
	hackInv.c[CSI->idEquip] = inv->c[container];

	Com_FindSpace(&hackInv, item.t, CSI->idEquip, &x, &y);
	if (x == NONE) {
		assert (y == NONE);
		return 0;
	} else {
		Com_AddToInventory(&hackInv, item, CSI->idEquip, x, y);
		inv->c[container] = hackInv.c[CSI->idEquip];
		return 1;
	}
}

/*
==============================================================================
CHARACTER GENERATION AND HANDLING
==============================================================================
*/

#define AKIMBO_CHANCE		0.3
#define WEAPONLESS_BONUS	0.4
#define PROB_COMPENSATION   3.0

/**
 * @brief Pack a weapon, possibly with some ammo
 * @param[in] inv The inventory that will get the weapon
 * @param[in] weapon The weapon type index in gi.csi->ods
 * @param[in] equip The equipment that shows how many clips to pack
 * @param[in] name The name of the equipment for debug messages
 * @sa INV_LoadableInWeapon
 */
int Com_PackAmmoAndWeapon (inventory_t* const inv, const int weapon, const int equip[MAX_OBJDEFS], int missed_primary, char *name)
{
	int ammo = -1; /* this variable is never used before being set */
	item_t item = {0,NONE,NONE};
	int i, max_price, prev_price;
	objDef_t obj;
	qboolean allowLeft;
	qboolean packed;
	int ammoMult = 1;

#ifdef PARANOID
	if (weapon < 0) {
		Com_Printf("Error in Com_PackAmmoAndWeapon - weapon is %i\n", weapon);
	}
#endif

	assert(Q_strcmp(CSI->ods[weapon].type, "armor"));
	item.t = weapon;

	/* are we going to allow trying the left hand */
	allowLeft = !(inv->c[CSI->idRight] && CSI->ods[inv->c[CSI->idRight]->item.t].firetwohanded);

	if (!CSI->ods[weapon].reload) {
		item.m = item.t; /* no ammo needed, so fire definitions are in t */
	} else {
		if (CSI->ods[weapon].oneshot) {
			/* The weapon provides its own ammo (i.e. it is charged or loaded in the base.) */
			item.a = CSI->ods[weapon].ammo;
			item.m = weapon;
			Com_DPrintf("Com_PackAmmoAndWeapon: oneshot weapon '%s' in equipment '%s'.\n", CSI->ods[weapon].id, name);
		} else {
			max_price = 0;
			/* find some suitable ammo for the weapon */
			for (i = CSI->numODs - 1; i >= 0; i--)
				if (equip[i]
				&& INV_LoadableInWeapon(&CSI->ods[i], weapon)
				&& (CSI->ods[i].price > max_price) ) {
					ammo = i;
					max_price = CSI->ods[i].price;
				}

			if (ammo < 0) {
				Com_DPrintf("Com_PackAmmoAndWeapon: no ammo for sidearm or primary weapon '%s' in equipment '%s'.\n", CSI->ods[weapon].id, name);
				return 0;
			}
			/* load ammo */
			item.a = CSI->ods[weapon].ammo;
			item.m = ammo;
		}
	}

	if (item.m == NONE) {
		Com_Printf("Com_PackAmmoAndWeapon: no ammo for sidearm or primary weapon '%s' in equipment '%s'.\n", CSI->ods[weapon].id, name);
		return 0;
	}

	/* now try to pack the weapon */
	packed = Com_TryAddToInventory(inv, item, CSI->idRight);
	if (packed)
		ammoMult = 3;
	if (!packed && allowLeft)
		packed = Com_TryAddToInventory(inv, item, CSI->idLeft);
	if (!packed)
		packed = Com_TryAddToInventory(inv, item, CSI->idBelt);
	if (!packed)
		packed = Com_TryAddToInventory(inv, item, CSI->idHolster);
	if (!packed)
		return 0;

	max_price = INT_MAX;
	do {
		/* search for the most expensive matching ammo in the equipment */
		prev_price = max_price;
		max_price = 0;
		for (i = 0; i < CSI->numODs; i++) {
			obj = CSI->ods[i];
			if (equip[i] && INV_LoadableInWeapon(&obj, weapon) ) {
				if (obj.price > max_price && obj.price < prev_price) {
					max_price = obj.price;
					ammo = i;
				}
			}
		}
		/* see if there is any */
		if (max_price) {
			int num;
			int numpacked = 0;

			/* how many clips? */
			num = min(
				equip[ammo] / equip[weapon]
				+ (equip[ammo] % equip[weapon] > rand() % equip[weapon])
				+ (PROB_COMPENSATION > 40 * frand())
				+ (float) missed_primary * (1 + frand() * PROB_COMPENSATION) / 40.0, 20);

			assert (num >= 0);
			/* pack some more ammo */
			while (num--) {
				item_t mun = {0,NONE,NONE};

				mun.t = ammo;
				/* ammo to backpack; belt is for knives and grenades */
				numpacked += Com_TryAddToInventory(inv, mun, CSI->idBackpack);
				/* no problem if no space left; one ammo already loaded */
				if (numpacked > ammoMult || numpacked*CSI->ods[weapon].ammo > 11)
					break;
			}
		}
	} while (max_price);

	return qtrue;
}


/**
 * @brief Fully equip one actor
 * @param[in] inv The inventory that will get the weapon
 * @param[in] equip The equipment that shows what is available
 * @param[in] name The name of the equipment for debug messages
 * @param[in] chr Pointer to character data - to get the weapon and armor bools
 * @note The code below is a complete implementation
 * of the scheme sketched at the beginning of equipment_missions.ufo.
 * Beware: if two weapons in the same category have the same price,
 * only one will be considered for inventory.
 */
void Com_EquipActor (inventory_t* const inv, const int equip[MAX_OBJDEFS], char *name, character_t* chr)
{
	int weapon = -1; /* this variable is never used before being set */
	int i, max_price, prev_price;
	int has_weapon = 0, has_armor = 0, repeat = 0, missed_primary = 0;
	int primary = 2; /* 0 particle or normal, 1 other, 2 no primary weapon */
	objDef_t obj;

	if (chr->weapons) {
		/* primary weapons */
		max_price = INT_MAX;
		do {
			int lastPos = CSI->numODs - 1;
			/* search for the most expensive primary weapon in the equipment */
			prev_price = max_price;
			max_price = 0;
			for (i = lastPos; i >= 0; i--) {
				obj = CSI->ods[i];
				if ( equip[i] && obj.weapon && obj.buytype == 0 && obj.firetwohanded ) {
					if (frand() < 0.15) { /* small chance to pick any weapon */
						weapon = i;
						max_price = obj.price;
						lastPos = i - 1;
						break;
					} else if ( obj.price > max_price && obj.price < prev_price ) {
						max_price = obj.price;
						weapon = i;
						lastPos = i - 1;
					}
				}
			}
			/* see if there is any */
			if (max_price) {
				/* see if the actor picks it */
				if ( equip[weapon] >= (28 - PROB_COMPENSATION) * frand() ) {
					/* not decrementing equip[weapon]
					* so that we get more possible squads */
					has_weapon += Com_PackAmmoAndWeapon(inv, weapon, equip, 0, name);
					if (has_weapon) {
						int ammo;

						/* find the first possible ammo to check damage type */
						for (ammo = 0; ammo < CSI->numODs; ammo++)
							if ( equip[ammo]
							&& INV_LoadableInWeapon(&CSI->ods[ammo], weapon) )
								break;
						if (ammo < CSI->numODs) {
							primary =
								/* to avoid two particle weapons */
								!(CSI->ods[ammo].fd[0][0].dmgtype
								== CSI->damParticle)
								/* to avoid SMG + Assault Rifle */
								&& !(CSI->ods[ammo].fd[0][0].dmgtype
									== CSI->damNormal); /* fd[0][0] Seems to be ok here since we just check the damage type and they are the same for all fds i've found. */
						}
						max_price = 0; /* one primary weapon is enough */
						missed_primary = 0;
					}
				} else {
					missed_primary += equip[weapon];
				}
			}
		} while (max_price);

		/* sidearms (secondary weapons with reload) */
		if (!has_weapon)
			repeat = WEAPONLESS_BONUS > frand ();
		else
			repeat = 0;
		do {
			max_price = primary ? INT_MAX : 0;
			do {
				prev_price = max_price;
				/* if primary is a particle or normal damage weapon,
				we pick cheapest sidearms first */
				max_price = primary ? 0 : INT_MAX;
				for (i = 0; i < CSI->numODs; i++) {
					obj = CSI->ods[i];
					if ( equip[i] && obj.weapon
						&& obj.buytype == 1 && obj.reload ) {
						if ( primary
							? obj.price > max_price && obj.price < prev_price
							: obj.price < max_price && obj.price > prev_price ) {
							max_price = obj.price;
							weapon = i;
						}
					}
				}
				if ( !(max_price == (primary ? 0 : INT_MAX)) ) {
					if ( equip[weapon] >= 40 * frand() ) {
						has_weapon += Com_PackAmmoAndWeapon(inv, weapon, equip, missed_primary, name);
						if (has_weapon) {
							/* try to get the second akimbo pistol */
							if ( primary == 2
								&& !CSI->ods[weapon].firetwohanded
								&& frand() < AKIMBO_CHANCE ) {
								Com_PackAmmoAndWeapon(inv, weapon, equip, 0, name);
							}
							/* enough sidearms */
							max_price = primary ? 0 : INT_MAX;
						}
					}
				}
			} while ( !(max_price == (primary ? 0 : INT_MAX)) );
		} while (!has_weapon && repeat--);

		/* misc items and secondary weapons without reload */
		if (!has_weapon)
			repeat = WEAPONLESS_BONUS > frand ();
		else
			repeat = 0;
		do {
			max_price = INT_MAX;
			do {
				prev_price = max_price;
				max_price = 0;
				for (i = 0; i < CSI->numODs; i++) {
					obj = CSI->ods[i];
					if ( equip[i]
						&& ((obj.weapon && obj.buytype == 1 && !obj.reload)
							|| obj.buytype == 2) ) {
						if ( obj.price > max_price && obj.price < prev_price ) {
							max_price = obj.price;
							weapon = i;
						}
					}
				}
				if (max_price) {
					int num;

					num =
						equip[weapon] / 40
						+ (equip[weapon] % 40 >= 40 * frand());
					while (num--)
						has_weapon += Com_PackAmmoAndWeapon(inv, weapon, equip, 0, name);
				}
			} while (max_price);
		} while (repeat--); /* gives more if no serious weapons */

		/* if no weapon at all, bad guys will always find a blade to wield */
		if (!has_weapon) {
			Com_DPrintf("Com_EquipActor: no weapon picked in equipment '%s', defaulting to the most expensive secondary weapon without reload.\n", name);
			max_price = 0;
			for (i = 0; i < CSI->numODs; i++) {
				obj = CSI->ods[i];
				if ( equip[i]
					&& obj.weapon && obj.buytype == 1 && !obj.reload ) {
					if ( obj.price > max_price && obj.price < prev_price ) {
						max_price = obj.price;
						weapon = i;
					}
				}
			}
			if (max_price)
				has_weapon += Com_PackAmmoAndWeapon(inv, weapon, equip, 0, name);
		}
		/* if still no weapon, something is broken, or no blades in equip */
		if (!has_weapon)
			Com_DPrintf("Com_EquipActor: cannot add any weapon; no secondary weapon without reload detected for equipment '%s'.\n", name);

		/* armor; especially for those without primary weapons */
		repeat = (float) missed_primary * (1 + frand() * PROB_COMPENSATION) / 40.0;
	} else {
		Com_DPrintf("Com_EquipActor: character '%s' may not carry weapons\n", chr->name);
		return;
	}

	if (!chr->armor) {
		Com_DPrintf("Com_EquipActor: character '%s' may not carry armor\n", chr->name);
		return;
	}

	do {
		max_price = INT_MAX;
		do {
			prev_price = max_price;
			max_price = 0;
			for (i = 0; i < CSI->numODs; i++) {
				obj = CSI->ods[i];
				if ( equip[i] && obj.buytype == 3 ) {
					if ( obj.price > max_price && obj.price < prev_price ) {
						max_price = obj.price;
						weapon = i;
					}
				}
			}
			if (max_price) {
				if ( equip[weapon] >= 40 * frand() ) {
					item_t item = {0,NONE,NONE};

					item.t = weapon;
					if (Com_TryAddToInventory(inv, item, CSI->idArmor)) {
						has_armor++;
						max_price = 0; /* one armor is enough */
					}
				}
			}
		} while (max_price);
	} while (!has_armor && repeat--);
}

/**
 * @brief Translate the team string to the team int value
 * @sa TEAM_ALIEN, TEAM_CIVILIAN, TEAM_PHALANX
 * @param[in] teamString
 */
int Com_StringToTeamNum (char* teamString)
{
	if (!Q_strncmp(teamString, "TEAM_PHALANX", MAX_VAR))
		return TEAM_PHALANX;
	if (!Q_strncmp(teamString, "TEAM_CIVILIAN", MAX_VAR))
		return TEAM_CIVILIAN;
	if (!Q_strncmp(teamString, "TEAM_ALIEN", MAX_VAR))
		return TEAM_ALIEN;
	/* there may be other ortnok teams - only check first 6 characters */
	Com_Printf("Com_StringToTeamNum: Unknown teamString: '%s'\n", teamString);
	return -1;
}

/* min and max values for all teams can be defined via campaign script */
int skillValues[MAX_CAMPAIGNS][MAX_TEAMS][MAX_EMPL][2];
int abilityValues[MAX_CAMPAIGNS][MAX_TEAMS][MAX_EMPL][2];

/**
 * @brief Fills the min and max values for abilities for the given character
 * @param[in] chr For which character - needed to check empl_type
 * @param[in] team TEAM_ALIEN, TEAM_CIVILIAN, ...
 * @param[in] minAbility Pointer to minAbility int value to use for this character
 * @param[in] maxAbility Pointer to maxAbility int value to use for this character
 * @sa Com_CharGenAbilitySkills
 */
void Com_GetAbility (character_t *chr, int team, int *minAbility, int *maxAbility, int campaignID)
{
	*minAbility = *maxAbility = 0;
	/* some default values */
	switch (chr->empl_type) {
	case EMPL_SOLDIER:
		*minAbility = 15;
		*maxAbility = 75;
		break;
	case EMPL_MEDIC:
		*minAbility = 15;
		*maxAbility = 75;
		break;
	case EMPL_SCIENTIST:
		*minAbility = 15;
		*maxAbility = 75;
		break;
	case EMPL_WORKER:
		*minAbility = 15;
		*maxAbility = 50;
		break;
	case EMPL_ROBOT:
		*minAbility = 80;
		*maxAbility = 80;
		break;
	default:
		Sys_Error("Com_GetAbility: Unknown employee type: %i\n", chr->empl_type);
	}
	if (team == TEAM_ALIEN) {
		*minAbility = 0;
		*maxAbility = 100;
	} else if (team == TEAM_CIVILIAN) {
		*minAbility = 0;
		*maxAbility = 20;
	}
	/* we always need both values - min and max - otherwise it was already a Sys_Error at parsing time */
	if (campaignID >= 0 && abilityValues[campaignID][team][chr->empl_type][0] >= 0) {
		*minAbility = abilityValues[campaignID][team][chr->empl_type][0];
		*maxAbility = abilityValues[campaignID][team][chr->empl_type][1];
	}
#ifdef PARANOID
	Com_DPrintf("Com_GetAbility: use minAbility %i and maxAbility %i for team %i and empl_type %i\n", *minAbility, *maxAbility, team, chr->empl_type);
#endif
}

/**
 * @brief Fills the min and max values for skill for the given character
 * @param[in] chr For which character - needed to check empl_type
 * @param[in] team TEAM_ALIEN, TEAM_CIVILIAN, ...
 * @param[in] minSkill Pointer to minSkill int value to use for this character
 * @param[in] maxSkill Pointer to maxSkill int value to use for this character
 * @sa Com_CharGenAbilitySkills
 */
void Com_GetSkill (character_t *chr, int team, int *minSkill, int *maxSkill, int campaignID)
{
	*minSkill = *maxSkill = 0;
	/* some default values */
	switch (chr->empl_type) {
	case EMPL_SOLDIER:
		*minSkill = 15;
		*maxSkill = 75;
		break;
	case EMPL_MEDIC:
		*minSkill = 15;
		*maxSkill = 75;
		break;
	case EMPL_SCIENTIST:
		*minSkill = 15;
		*maxSkill = 75;
		break;
	case EMPL_WORKER:
		*minSkill = 15;
		*maxSkill = 50;
		break;
	case EMPL_ROBOT:
		*minSkill = 80;
		*maxSkill = 80;
		break;
	default:
		Sys_Error("Com_GetSkill: Unknown employee type: %i\n", chr->empl_type);
	}
	if (team == TEAM_ALIEN) {
		*minSkill = 0;
		*maxSkill = 100;
	} else if (team == TEAM_CIVILIAN) {
		*minSkill = 0;
		*maxSkill = 20;
	}
	/* we always need both values - min and max - otherwise it was already a Sys_Error at parsing time */
	if (campaignID >= 0 && skillValues[campaignID][team][chr->empl_type][0] >= 0) {
		*minSkill = skillValues[campaignID][team][chr->empl_type][0];
		*maxSkill = skillValues[campaignID][team][chr->empl_type][1];
	}
#ifdef PARANOID
	Com_DPrintf("Com_GetSkill: use minSkill %i and maxSkill %i for team %i and empl_type %i\n", *minSkill, *maxSkill, team, chr->empl_type);
#endif
}

/**
 * @brief Sets the current active campaign id (see curCampaign pointer)
 * @note used to access the right values from skillValues and abilityValues
 * @sa CL_GameInit
 */
void Com_SetGlobalCampaignID (int campaignID)
{
	globalCampaignID = campaignID;
}

/**
 * @brief
 * @param[in] chr
 * @param[in] minAbility
 * @param[in] maxAbility
 * @param[in] minSkill
 * @param[in] maxSkill
 * @sa Com_GetAbility
 * @sa Com_GetSkill
 */
#define MAX_GENCHARRETRIES	20
void Com_CharGenAbilitySkills (character_t * chr, int team)
{
	float randomArray[SKILL_NUM_TYPES];
	int i, retry;
	float max, min, rand_avg;
	int minAbility = 0, maxAbility = 0, minSkill = 0, maxSkill = 0;

	assert(chr);
#ifdef DEBUG
	if (!chr)
		return;	/* never reached. need for code analyst. */
#endif

	Com_GetAbility(chr, team, &minAbility, &maxAbility, globalCampaignID);
	Com_GetSkill(chr, team, &minSkill, &maxSkill, globalCampaignID);
	retry = MAX_GENCHARRETRIES;
	do {
		/* Abilities */
		max = 0;
		min = 1;
		rand_avg = 0;
		for (i = 0; i < ABILITY_NUM_TYPES; i++) {
			randomArray[i] = frand();
			rand_avg += randomArray[i];
			if (randomArray[i] > max)
				max = randomArray[i];	/* get max */
			if (randomArray[i] < min)
				min = randomArray[i];	/* and min */
		}

		rand_avg /= ABILITY_NUM_TYPES;
		if (max - rand_avg < rand_avg - min)
			min = rand_avg - min;
		else
			min = max - rand_avg;
		for (i = 0; i < ABILITY_NUM_TYPES; i++) {
			/* Don't allow to generate skill > 100 - more than 100 only with implants. */
			if ((((randomArray[i] - rand_avg) / min * (maxAbility - minAbility) + minAbility + maxAbility) / 2 + frand() * 3) > 100)
				chr->skills[i] = 100;
			else
				chr->skills[i] = ((randomArray[i] - rand_avg) / min * (maxAbility - minAbility) + minAbility + maxAbility) / 2 + frand() * 3;
		}

		/* Skills */
		max = 0;
		min = 1;
		rand_avg = 0;
		for (i = 0; i < SKILL_NUM_TYPES - ABILITY_NUM_TYPES; i++) {
			randomArray[i] = frand();
			rand_avg += randomArray[i];
			if (randomArray[i] > max)
				max = randomArray[i];	/* get max */
			if (randomArray[i] < min)
				min = randomArray[i];	/* or min */
		}
		rand_avg /= SKILL_NUM_TYPES - ABILITY_NUM_TYPES;
		if (max - rand_avg < rand_avg - min)
			min = rand_avg - min;
		else
			min = max - rand_avg;
		for (i = 0; i < SKILL_NUM_TYPES - ABILITY_NUM_TYPES; i++) {
			/* Don't allow to generate skill > 100 - more than 100 only with implants. */
			if ((((randomArray[i] - rand_avg) / min * (maxSkill - minSkill) + minSkill + maxSkill) / 2 + frand() * 3) > 100)
				chr->skills[ABILITY_NUM_TYPES + i] = 100;
			else
				chr->skills[ABILITY_NUM_TYPES + i] = ((randomArray[i] - rand_avg) / min * (maxSkill - minSkill) + minSkill + maxSkill) / 2 + frand() * 3;
		}

		/* Check if it makes sense */
		max = ((max - rand_avg) / min * (maxSkill - minSkill) + minSkill + maxSkill) / 2;
		min = (maxAbility + minAbility) / 2.2;
		if ((max - 10 < chr->skills[SKILL_CLOSE] && (chr->skills[ABILITY_SPEED] < min || chr->skills[ABILITY_POWER] < min))
			|| (max - 10 < chr->skills[SKILL_HEAVY] && chr->skills[ABILITY_POWER] < min)
			|| (max - 10 < chr->skills[SKILL_SNIPER] && chr->skills[ABILITY_ACCURACY] < min)
			|| (max - 10 < chr->skills[SKILL_EXPLOSIVE] && chr->skills[ABILITY_MIND] < min))
			retry--;			/* try again. */
		else
			retry = 0;
	}
	while (retry > 0);
}


static char returnModel[MAX_VAR];

/**
 * @brief Returns the body model for the soldiers for armored and non armored soldiers
 * @param chr Pointer to character struct
 * @sa Com_CharGetBody
 */
char *Com_CharGetBody (character_t * const chr)
{
	char id[MAX_VAR];
	char *underline;

	assert(chr);
#ifdef DEBUG
	if (!chr)
		return NULL;	/* never reached. need for code analyst. */
#endif

	assert(chr->inv);
#ifdef DEBUG
	if (!chr->inv)
		return NULL;	/* never reached. need for code analyst. */
#endif

	/* models of UGVs don't change - because they are already armored */
	if (chr->inv->c[CSI->idArmor] && chr->fieldSize == ACTOR_SIZE_NORMAL) {
		assert(!Q_strcmp(CSI->ods[chr->inv->c[CSI->idArmor]->item.t].type, "armor"));
/*		Com_Printf("Com_CharGetBody: Use '%s' as armor\n", CSI->ods[chr->inv->c[CSI->idArmor]->item.t].id);*/

		/* check for the underline */
		Q_strncpyz(id, CSI->ods[chr->inv->c[CSI->idArmor]->item.t].id, MAX_VAR);
		underline = strchr(id, '_');
		if (underline)
			*underline = '\0';

		Com_sprintf(returnModel, MAX_VAR, "%s%s/%s", chr->path, id, chr->body);
	} else
		Com_sprintf(returnModel, MAX_VAR, "%s/%s", chr->path, chr->body);
	return returnModel;
}

/**
 * @brief Returns the head model for the soldiers for armored and non armored soldiers
 * @param chr Pointer to character struct
 * @sa Com_CharGetBody
 */
char *Com_CharGetHead (character_t * const chr)
{
	char id[MAX_VAR];
	char *underline;

	assert(chr);
#ifdef DEBUG
	if (!chr)
		return NULL;	/* never reached. need for code analyst. */
#endif

	assert(chr->inv);
#ifdef DEBUG
	if (!chr->inv)
		return NULL;	/* never reached. need for code analyst. */
#endif

	/* models of UGVs don't change - because they are already armored */
	if (chr->inv->c[CSI->idArmor] && chr->fieldSize == ACTOR_SIZE_NORMAL) {
		assert(!Q_strcmp(CSI->ods[chr->inv->c[CSI->idArmor]->item.t].type, "armor"));
/*		Com_Printf("Com_CharGetHead: Use '%s' as armor\n", CSI->ods[chr->inv->c[CSI->idArmor]->item.t].id);*/

		/* check for the underline */
		Q_strncpyz(id, CSI->ods[chr->inv->c[CSI->idArmor]->item.t].id, MAX_VAR);
		underline = strchr(id, '_');
		if (underline)
			*underline = '\0';

		Com_sprintf(returnModel, MAX_VAR, "%s%s/%s", chr->path, id, chr->head);
	} else
		Com_sprintf(returnModel, MAX_VAR, "%s/%s", chr->path, chr->head);
	return returnModel;
}


/*
==============================================================================
SCRIPT VALUE PARSING
==============================================================================
*/


/**
 * @brief possible values for parsing functions
 * @sa value_types
 */
const char *vt_names[V_NUM_TYPES] = {
	"",
	"bool",
	"char",
	"int",
	"int2",
	"float",
	"pos",
	"vector",
	"color",
	"rgba",
	"string",
	"translation_string",
	"translation2_string",
	"longstring",
	"pointer",
	"align",
	"blend",
	"style",
	"fade",
	"shapes",
	"shapeb",
	"dmgtype",
	"date",
	"if",
	"relabs"
};

const char *align_names[ALIGN_LAST] = {
	"ul", "uc", "ur", "cl", "cc", "cr", "ll", "lc", "lr"
};

const char *blend_names[BLEND_LAST] = {
	"replace", "blend", "add", "filter", "invfilter"
};

const char *style_names[STYLE_LAST] = {
	"facing", "rotated", "beam", "line", "axis"
};

const char *fade_names[FADE_LAST] = {
	"none", "in", "out", "sin", "saw", "blend"
};

const static char *if_strings[IF_SIZE] = {
	""
	"==",
	"<=",
	">=",
	">",
	"<",
	"!="
};

/**
 * @brief Translate the condition string to menuIfCondition_t enum value
 * @param[in] conditionString The string from scriptfiles (see if_strings)
 * @return menuIfCondition_t value
 * @return enum value for condition string
 * @note Produces a Sys_Error if conditionString was not found in if_strings array
 */
int Com_ParseConditionType (const char* conditionString, const char *token)
{
	int i = IF_SIZE;
	for (; i--;) {
		if (!Q_strncmp(if_strings[i], (char*)conditionString, 2)) {
			return i;
		}
	}
	Sys_Error("Invalid if statement with condition '%s' token: '%s'\n", conditionString, token);
	return -1;
}

/**
 * @brief
 * @note translateable string are marked with _ at the beginning
 * @code menu exampleName
 * {
 *   string "_this is translatable"
 * }
 * @endcode
 */
int Com_ParseValue (void *base, char *token, int type, int ofs)
{
	byte *b;
	char string[MAX_VAR];
	char string2[MAX_VAR];
	char condition[MAX_VAR];
	int x, y, w, h;

	b = (byte *) base + ofs;

	switch (type) {
	case V_NULL:
		return ALIGN(0);

	case V_BOOL:
		if (!Q_strncmp(token, "true", 4) || *token == '1')
			*b = qtrue;
		else
			*b = qfalse;
		return ALIGN(sizeof(byte));

	case V_CHAR:
		*(char *) b = *token;
		return ALIGN(sizeof(char));

	case V_INT:
		*(int *) b = atoi(token);
		return ALIGN(sizeof(int));

	case V_INT2:
		if (strstr(token, " ") == NULL)
			Sys_Error("Com_ParseValue: Illegal int2 statement '%s'\n", token);
		sscanf(token, "%i %i", &((int *) b)[0], &((int *) b)[1]);
		return ALIGN(2 * sizeof(int));

	case V_FLOAT:
		*(float *) b = atof(token);
		return ALIGN(sizeof(float));

	case V_POS:
		if (strstr(token, " ") == NULL)
			Sys_Error("Com_ParseValue: Illegal pos statement '%s'\n", token);
		sscanf(token, "%f %f", &((float *) b)[0], &((float *) b)[1]);
		return ALIGN(2 * sizeof(float));

	case V_VECTOR:
		if (strstr(strstr(token, " "), " ") == NULL)
			Sys_Error("Com_ParseValue: Illegal vector statement '%s'\n", token);
		sscanf(token, "%f %f %f", &((float *) b)[0], &((float *) b)[1], &((float *) b)[2]);
		return ALIGN(3 * sizeof(float));

	case V_COLOR:
		if (strstr(strstr(strstr(token, " "), " "), " ") == NULL)
			Sys_Error("Com_ParseValue: Illegal color statement '%s'\n", token);
		sscanf(token, "%f %f %f %f", &((float *) b)[0], &((float *) b)[1], &((float *) b)[2], &((float *) b)[3]);
		return ALIGN(4 * sizeof(float));

	case V_RGBA:
		if (strstr(strstr(strstr(token, " "), " "), " ") == NULL)
			Sys_Error("Com_ParseValue: Illegal rgba statement '%s'\n", token);
		sscanf(token, "%i %i %i %i", &((int *) b)[0], &((int *) b)[1], &((int *) b)[2], &((int *) b)[3]);
		return ALIGN(4);

	case V_STRING:
		Q_strncpyz((char *) b, token, MAX_VAR);
		w = (int)strlen(token) + 1;
		if (w > MAX_VAR)
			w = MAX_VAR;
		return ALIGN(w);

	case V_TRANSLATION_STRING:
		if (*token == '_')
			token++;

		Q_strncpyz((char *) b, _(token), MAX_VAR);
		return ALIGN((int)strlen((char *) b) + 1);

	/* just remove the _ but don't translate */
	case V_TRANSLATION2_STRING:
		if (*token == '_')
			token++;

		Q_strncpyz((char *) b, token, MAX_VAR);
		w = (int)strlen((char *) b) + 1;
		return ALIGN(w);

	case V_LONGSTRING:
		strcpy((char *) b, token);
		w = (int)strlen(token) + 1;
		return ALIGN(w);

	case V_POINTER:
		*(void **) b = (void *) token;
		return ALIGN((int)sizeof(void *));

	case V_ALIGN:
		for (w = 0; w < ALIGN_LAST; w++)
			if (!Q_strcmp(token, align_names[w]))
				break;
		if (w == ALIGN_LAST)
			*b = 0;
		else
			*b = w;
		return ALIGN(1);

	case V_BLEND:
		for (w = 0; w < BLEND_LAST; w++)
			if (!Q_strcmp(token, blend_names[w]))
				break;
		if (w == BLEND_LAST)
			*b = 0;
		else
			*b = w;
		return ALIGN(1);

	case V_STYLE:
		for (w = 0; w < STYLE_LAST; w++)
			if (!Q_strcmp(token, style_names[w]))
				break;
		if (w == STYLE_LAST)
			*b = 0;
		else
			*b = w;
		return ALIGN(1);

	case V_FADE:
		for (w = 0; w < FADE_LAST; w++)
			if (!Q_strcmp(token, fade_names[w]))
				break;
		if (w == FADE_LAST)
			*b = 0;
		else
			*b = w;
		return ALIGN(1);

	case V_SHAPE_SMALL:
		if (strstr(strstr(strstr(token, " "), " "), " ") == NULL)
			Sys_Error("Com_ParseValue: Illegal shape small statement '%s'\n", token);
		sscanf(token, "%i %i %i %i", &x, &y, &w, &h);
		for (h += y; y < h; y++)
			*(int *) b |= ((1 << w) - 1) << x << (y * 8);
		return ALIGN(4);

	case V_SHAPE_BIG:
		if (strstr(strstr(strstr(token, " "), " "), " ") == NULL)
			Sys_Error("Com_ParseValue: Illegal shape big statement '%s'\n", token);
		sscanf(token, "%i %i %i %i", &x, &y, &w, &h);
		w = ((1 << w) - 1) << x;
		for (h += y; y < h; y++)
			((int *) b)[y] |= w;
		return ALIGN(64);

	case V_DMGTYPE:
		for (w = 0; w < CSI->numDTs; w++)
			if (!Q_strcmp(token, CSI->dts[w]))
				break;
		if (w == CSI->numDTs)
			*b = 0;
		else
			*b = w;
		return ALIGN(1);

	case V_DATE:
		if (strstr(strstr(token, " "), " ") == NULL)
			Sys_Error("Com_ParseValue: Illegal if statement '%s'\n", token);
		sscanf(token, "%i %i %i", &x, &y, &w);
		((date_t *) b)->day = 365 * x + y;
		((date_t *) b)->sec = 3600 * w;
		return ALIGN(sizeof(date_t));

	case V_IF:
		if (strstr(strstr(token, " "), " ") == NULL)
			Sys_Error("Com_ParseValue: Illegal if statement '%s'\n", token);
		sscanf(token, "%s %s %s", string, condition, string2);

		Q_strncpyz(((menuDepends_t *) b)->var, string, MAX_VAR);
		Q_strncpyz(((menuDepends_t *) b)->value, string2, MAX_VAR);
		((menuDepends_t *) b)->cond = Com_ParseConditionType(condition, token);
		return ALIGN(sizeof(menuDepends_t));

	case V_RELABS:
		if (token[0] == '-' || token[0] == '+') {
			if (fabs(atof(token + 1)) <= 2.0f)
				Com_Printf("Com_ParseValue: a V_RELABS (absolute) value should always be bigger than +/-2.0\n");
			if (token[0] == '-')
				*(float *) b = atof(token+1) * (-1);
			else
				*(float *) b = atof(token+1);
		} else {
			if (fabs(atof(token)) > 2.0f)
				Com_Printf("Com_ParseValue: a V_RELABS (relative) value should only be between 0.00..1 and 2.0\n");
			*(float *) b = atof(token);
		}
		return ALIGN(sizeof(float));

	default:
		Sys_Error("Com_ParseValue: unknown value type '%s'\n", token);
		return -1;
	}
}


/**
 * @brief
 * @param[in] base The start pointer to a given data type (typedef, struct)
 * @param[in] set The data which should be parsed
 * @param[in] type The data type that should be parsed
 * @param[in] ofs The offset for the value
 * @sa Com_ValueToStr
 * @note The offset is most likely given by the offsetof macro
 */
int Com_SetValue (void *base, void *set, int type, int ofs)
{
	byte *b;
	int len;

	b = (byte *) base + ofs;

	switch (type) {
	case V_NULL:
		return 0;

	case V_BOOL:
		if (*(byte *) set)
			*b = qtrue;
		else
			*b = qfalse;
		return sizeof(byte);

	case V_CHAR:
		*(char *) b = *(char *) set;
		return sizeof(char);

	case V_INT:
		*(int *) b = *(int *) set;
		return sizeof(int);

	case V_INT2:
		((int *) b)[0] = ((int *) set)[0];
		((int *) b)[1] = ((int *) set)[1];
		return 2 * sizeof(int);

	case V_FLOAT:
		*(float *) b = *(float *) set;
		return sizeof(float);

	case V_POS:
		((float *) b)[0] = ((float *) set)[0];
		((float *) b)[1] = ((float *) set)[1];
		return 2 * sizeof(float);

	case V_VECTOR:
		((float *) b)[0] = ((float *) set)[0];
		((float *) b)[1] = ((float *) set)[1];
		((float *) b)[2] = ((float *) set)[2];
		return 3 * sizeof(float);

	case V_COLOR:
		((float *) b)[0] = ((float *) set)[0];
		((float *) b)[1] = ((float *) set)[1];
		((float *) b)[2] = ((float *) set)[2];
		((float *) b)[3] = ((float *) set)[3];
		return 4 * sizeof(float);

	case V_RGBA:
		((byte *) b)[0] = ((byte *) set)[0];
		((byte *) b)[1] = ((byte *) set)[1];
		((byte *) b)[2] = ((byte *) set)[2];
		((byte *) b)[3] = ((byte *) set)[3];
		return 4;

	case V_STRING:
		Q_strncpyz((char *) b, (char *) set, MAX_VAR);
		len = (int)strlen((char *) set) + 1;
		if (len > MAX_VAR)
			len = MAX_VAR;
		return len;

	case V_LONGSTRING:
		strcpy((char *) b, (char *) set);
		len = (int)strlen((char *) set) + 1;
		return len;

	case V_ALIGN:
	case V_BLEND:
	case V_STYLE:
	case V_FADE:
		*b = *(byte *) set;
		return 1;

	case V_SHAPE_SMALL:
		*(int *) b = *(int *) set;
		return 4;

	case V_SHAPE_BIG:
		memcpy(b, set, 64);
		return 64;

	case V_DMGTYPE:
		*b = *(byte *) set;
		return 1;

	case V_DATE:
		memcpy(b, set, sizeof(date_t));
		return sizeof(date_t);

	default:
		Sys_Error("Com_ParseValue: unknown value type\n");
		return -1;
	}
}

/**
 * @brief
 * @param[in] base The start pointer to a given data type (typedef, struct)
 * @param[in] type The data type that should be parsed
 * @param[in] ofs The offset for the value
 * @sa Com_SetValue
 * @return char pointer with translated data type value
 */
char *Com_ValueToStr (void *base, int type, int ofs)
{
	static char valuestr[MAX_VAR];
	byte *b;

	b = (byte *) base + ofs;

	switch (type) {
	case V_NULL:
		return 0;

	case V_BOOL:
		if (*b)
			return "true";
		else
			return "false";

	case V_CHAR:
		return (char *) b;
		break;

	case V_INT:
		Com_sprintf(valuestr, MAX_VAR, "%i", *(int *) b);
		return valuestr;

	case V_INT2:
		Com_sprintf(valuestr, MAX_VAR, "%i %i", ((int *) b)[0], ((int *) b)[1]);
		return valuestr;

	case V_FLOAT:
		Com_sprintf(valuestr, MAX_VAR, "%.2f", *(float *) b);
		return valuestr;

	case V_POS:
		Com_sprintf(valuestr, MAX_VAR, "%.2f %.2f", ((float *) b)[0], ((float *) b)[1]);
		return valuestr;

	case V_VECTOR:
		Com_sprintf(valuestr, MAX_VAR, "%.2f %.2f %.2f", ((float *) b)[0], ((float *) b)[1], ((float *) b)[2]);
		return valuestr;

	case V_COLOR:
		Com_sprintf(valuestr, MAX_VAR, "%.2f %.2f %.2f %.2f", ((float *) b)[0], ((float *) b)[1], ((float *) b)[2], ((float *) b)[3]);
		return valuestr;

	case V_RGBA:
		Com_sprintf(valuestr, MAX_VAR, "%3i %3i %3i %3i", ((byte *) b)[0], ((byte *) b)[1], ((byte *) b)[2], ((byte *) b)[3]);
		return valuestr;

	case V_TRANSLATION_STRING:
	case V_TRANSLATION2_STRING:
	case V_STRING:
	case V_LONGSTRING:
		return (char *) b;

	case V_ALIGN:
		assert(*(int *)b < ALIGN_LAST);
		Q_strncpyz(valuestr, align_names[*(align_t *)b], sizeof(valuestr));
		return valuestr;

	case V_BLEND:
		assert(*(blend_t *)b < BLEND_LAST);
		Q_strncpyz(valuestr, blend_names[*(blend_t *)b], sizeof(valuestr));
		return valuestr;

	case V_STYLE:
		assert(*(style_t *)b < STYLE_LAST);
		Q_strncpyz(valuestr, style_names[*(style_t *)b], sizeof(valuestr));
		return valuestr;

	case V_FADE:
		assert(*(fade_t *)b < FADE_LAST);
		Q_strncpyz(valuestr, fade_names[*(fade_t *)b], sizeof(valuestr));
		return valuestr;

	case V_SHAPE_SMALL:
	case V_SHAPE_BIG:
		return "";

	case V_DMGTYPE:
		assert(*(int *)b < MAX_DAMAGETYPES);
		return CSI->dts[*(int *)b];

	case V_DATE:
		Com_sprintf(valuestr, MAX_VAR, "%i %i %i", ((date_t *) b)->day / 365, ((date_t *) b)->day % 365, ((date_t *) b)->sec);
		return valuestr;

	case V_IF:
		return "";

	case V_RELABS:
		/* absolute value */
		if (*(float *) b > 2.0)
			Com_sprintf(valuestr, MAX_VAR, "+%.2f", *(float *) b);
		/* absolute value */
		else if (*(float *) b < 2.0)
			Com_sprintf(valuestr, MAX_VAR, "-%.2f", *(float *) b);
		/* relative value */
		else
			Com_sprintf(valuestr, MAX_VAR, "%.2f", *(float *) b);
		return valuestr;

	default:
		Sys_Error("Com_ParseValue: unknown value type %i\n", type);
		return NULL;
	}
}

/**
 * @brief Prints a description of an object
 * @param[in] index of object in CSI
 */
void Com_PrintItemDescription (int i)
{
	objDef_t *ods_temp;

	ods_temp = &CSI->ods[i];
	Com_Printf("Item: %s\n", ods_temp->id);
	Com_Printf("... name          -> %s\n", ods_temp->name);
	Com_Printf("... type          -> %s\n", ods_temp->type);
	Com_Printf("... category      -> %i\n", ods_temp->category);
	Com_Printf("... weapon        -> %i\n", ods_temp->weapon);
	Com_Printf("... holdtwohanded -> %i\n", ods_temp->holdtwohanded);
	Com_Printf("... firetwohanded -> %i\n", ods_temp->firetwohanded);
	Com_Printf("... twohanded     -> %i\n", ods_temp->holdtwohanded);
	Com_Printf("... thrown        -> %i\n", ods_temp->thrown);
	Com_Printf("... usable for weapon (if type is ammo):\n");
	for (i = 0; i < ods_temp->numWeapons; i++) {
		if (ods_temp->weap_idx[i] >= 0)
			Com_Printf("    ... %s\n", CSI->ods[ods_temp->weap_idx[i]].name);
	}
	Com_Printf("\n");
}


/**
 * @brief Lists all object definitions
 * @todo Why is this here - and not in a client only function
 */
void Com_InventoryList_f (void)
{
	int i;

	for (i = 0; i < CSI->numODs; i++)
		Com_PrintItemDescription(i);
}

/**
 * @brief Returns the index of this item in the inventory.
 * @todo This function should be located in a inventory-related file.
 * @note id may not be null or empty
 * @note previously known as RS_GetItem
 * @param[in] id the item id in our object definition array (csi.ods)
 */
int Com_GetItemByID (const char *id)
{
	int i;
	objDef_t *item = NULL;

#ifdef DEBUG
	if (!id || !*id) {
		Com_Printf("Com_GetItemByID: Called with empty id\n");
		return -1;
	}
#endif

	for (i = 0; i < CSI->numODs; i++) {	/* i = item index */
		item = &CSI->ods[i];
		if (!Q_strncmp(id, item->id, MAX_VAR)) {
			return i;
		}
	}

	Com_Printf("Com_GetItemByID: Item \"%s\" not found.\n", id);
	return -1;
}

/**
 * @brief Checks if an item can be used to reload a weapon.
 * @param[in] od The object definition of the ammo.
 * @param[in] weapon_idx The index of the weapon (in the inventory) to check the item with.
 * @return qboolean Returns qtrue if the item can be used in the given weapon, otherwise qfalse.
 * @note Formerly named INV_AmmoUsableInWeapon.
 * @sa Com_PackAmmoAndWeapon
 */
qboolean INV_LoadableInWeapon (objDef_t *od, int weapon_idx)
{
	int i;
	qboolean usable = qfalse;

	for (i = 0; i < od->numWeapons; i++) {
#ifdef DEBUG
		if (od->weap_idx[i] < 0) {
			Com_DPrintf("INV_LoadableInWeapon: negative weap_idx entry (%s) found in item '%s'.\n", od->weap_id[i], od->id );
			break;
		}
#endif
		if (weapon_idx == od->weap_idx[i]) {
			usable = qtrue;
			break;
		}
	}
#if 0
	Com_DPrintf("INV_LoadableInWeapon: item '%s' usable/unusable (%i) in weapon '%s'.\n", od->id, usable, CSI->ods[weapon_idx].id );
#endif
	return usable;
}

/**
 * @brief Returns the index of the array that has the firedefinitions for a given weapon (-index)
 * @param[in] od The object definition of the item.
 * @param[in] weapon_idx The index of the weapon (in the inventory) to check the item with.
 * @return int Returns the index in the fd array. -1 if the weapon-idx was not found. 0 (equals the default firemode) if an invalid or unknown weapon idx was given.
 * @note the return value of -1 is in most cases a fatal error (except the scripts are not parsed while e.g. maptesting)
 */
int INV_FiredefsIDXForWeapon (objDef_t *od, int weapon_idx)
{
	int i;

	if (!od) {
		Com_DPrintf("INV_FiredefsIDXForWeapon: object definition is NULL.\n");
		return -1;
	}

	if (weapon_idx == NONE) {
		Com_DPrintf("INV_FiredefsIDXForWeapon: bad weapon_idx (NONE) in item '%s' - using default weapon/firemodes.\n", od->id);
		/* FIXME: this won't work if there is no weapon_idx, don't it? - should be -1 here, too */
		return 0;
	}

	for (i = 0; i < od->numWeapons; i++) {
		if (weapon_idx == od->weap_idx[i])
			return i;
	}

	/* No firedef index found for this weapon/ammo. */
#ifdef DEBUG
	Com_DPrintf("INV_FiredefsIDXForWeapon: No firedef index found for weapon. od:%s weap_idx:%i).\n", od->id, weapon_idx);
#endif
	return -1;
}

/**
 * @brief Returns the default reaction firemode for a given ammo in a given weapon.
 * @param[in] ammo The ammo(or weapon-)object that contains the firedefs
 * @param[in] weapon_fds_idx The index in objDef[x]
 * @return Default reaction-firemode index in objDef->fd[][x]. -1 if an error occurs or no firedefs exist.
 */
int Com_GetDefaultReactionFire (objDef_t *ammo, int weapon_fds_idx)
{
	int fd_idx;
	if (weapon_fds_idx >= MAX_WEAPONS_PER_OBJDEF) {
		Com_Printf("Com_GetDefaultReactionFire: bad weapon_fds_idx (%i) Maximum is %i.\n", weapon_fds_idx, MAX_WEAPONS_PER_OBJDEF-1);
		return -1;
	}
	if (weapon_fds_idx < 0) {
		Com_Printf("Com_GetDefaultReactionFire: Negative weapon_fds_idx given.\n");
		return -1;
	}

	if (ammo->numFiredefs[weapon_fds_idx] == 0) {
		Com_Printf("Com_GetDefaultReactionFire: Probably not an ammo-object: %s\n", ammo->id);
		return -1;
	}

	for (fd_idx = 0; fd_idx < ammo->numFiredefs[weapon_fds_idx]; fd_idx++) {
		if (ammo->fd[weapon_fds_idx][fd_idx].reaction)
			return fd_idx;
	}

	return 0; /* 0 = The first firemode. Default for objects without a reaction-firemode */
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
