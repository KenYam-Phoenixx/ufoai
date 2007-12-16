/**
 * @file r_mesh_md3.c
 * @brief MD3 Model drawing code
 */

/*
All original material Copyright (C) 2002-2007 UFO: Alien Invasion team.

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

#include "r_local.h"
#include "r_error.h"

static void R_DrawAliasMD3FrameLerp (mAliasModel_t *paliashdr, mAliasMesh_t mesh, float backlerp)
{
	int i,j;
	/*int k;*/
	mAliasFrame_t	*frame, *oldframe;
	vec3_t	move, delta, vectors[3];
	mAliasVertex_t	*v, *ov;
	vec3_t	tempVertexArray[4096];
	vec3_t	tempNormalsArray[4096];
	vec3_t	color1,color2,color3;
	float	alpha;
	float	frontlerp;
	vec3_t	tempangle;

	color1[0] = color1[1] = color1[2] = 0;
	color2[0] = color2[1] = color2[2] = 0;
	color3[0] = color3[1] = color3[2] = 0;

	frontlerp = 1.0 - backlerp;

	if (currententity->flags & RF_TRANSLUCENT)
		alpha = currententity->alpha;
	else
		alpha = 1.0;

	frame = paliashdr->frames + currententity->as.frame;
	oldframe = paliashdr->frames + currententity->as.oldframe;

	VectorSubtract(currententity->oldorigin, currententity->origin, delta);
	VectorCopy(currententity->angles, tempangle);
	tempangle[YAW] = tempangle[YAW] - 90;
	AngleVectors(tempangle, vectors[0], vectors[1], vectors[2]);

	move[0] = DotProduct(delta, vectors[0]);	/* forward */
	move[1] = -DotProduct(delta, vectors[1]);	/* left */
	move[2] = DotProduct(delta, vectors[2]);	/* up */

	VectorAdd(move, oldframe->translate, move);

	for (i = 0; i < 3; i++) {
		move[i] = backlerp * move[i] + frontlerp * frame->translate[i];
	}

	v = mesh.vertexes + currententity->as.frame * mesh.num_verts;
	ov = mesh.vertexes + currententity->as.oldframe * mesh.num_verts;
	for (i = 0; i < mesh.num_verts; i++, v++, ov++) {
		VectorSet(tempNormalsArray[i],
				v->normal[0] + (ov->normal[0] - v->normal[0]) * backlerp,
				v->normal[1] + (ov->normal[1] - v->normal[1]) * backlerp,
				v->normal[2] + (ov->normal[2] - v->normal[2]) * backlerp);
		VectorSet(tempVertexArray[i],
				move[0] + ov->point[0] * backlerp + v->point[0] * frontlerp,
				move[1] + ov->point[1] * backlerp + v->point[1] * frontlerp,
				move[2] + ov->point[2] * backlerp + v->point[2] * frontlerp);
	}
	qglBegin(GL_TRIANGLES);

	for (j = 0; j < mesh.num_tris; j++) {
		qglTexCoord2f(mesh.stcoords[mesh.indexes[3 * j + 0]].st[0], mesh.stcoords[mesh.indexes[3 * j + 0]].st[1]);
		qglVertex3fv(tempVertexArray[mesh.indexes[3 * j + 0]]);

		qglTexCoord2f(mesh.stcoords[mesh.indexes[3 * j + 1]].st[0], mesh.stcoords[mesh.indexes[3 * j + 1]].st[1]);
		qglVertex3fv(tempVertexArray[mesh.indexes[3 * j + 1]]);

		qglTexCoord2f(mesh.stcoords[mesh.indexes[3 * j + 2]].st[0], mesh.stcoords[mesh.indexes[3 * j + 2]].st[1]);
		qglVertex3fv(tempVertexArray[mesh.indexes[3 * j + 2]]);
	}
	qglEnd();
	R_CheckError();
}

/**
 * @sa R_DrawAliasMD2Model
 * @todo Implement the md2 renderer effect (e.g. RF_HIGHLIGHT)
 */
void R_DrawAliasMD3Model (entity_t *e)
{
	mAliasModel_t	*paliashdr;
	image_t		*skin;
	int	i;

	assert(e->model->type == mod_alias_md3);

	paliashdr = (mAliasModel_t *)e->model->alias.extraData;

	/* set-up lighting */
	R_ModEnableLights(e);

	for (i = 0; i < paliashdr->num_meshes; i++) {
		c_alias_polys += paliashdr->meshes[i].num_tris;
	}

	qglPushMatrix();
	e->angles[PITCH] = -e->angles[PITCH];	/* sigh. */
	e->angles[YAW] = e->angles[YAW] - 90;
	R_RotateForEntity(e);
	e->angles[PITCH] = -e->angles[PITCH];	/* sigh. */
	e->angles[YAW] = e->angles[YAW] + 90;

	if (e->flags & RF_TRANSLUCENT)
		RSTATE_ENABLE_BLEND

	if ((e->as.frame >= paliashdr->num_frames) || (e->as.frame < 0)) {
		Com_Printf("R_DrawAliasMD3Model %s: no such frame %d\n", e->model->name, e->as.frame);
		e->as.frame = 0;
		e->as.oldframe = 0;
	}

	if ((e->as.oldframe >= paliashdr->num_frames) || (e->as.oldframe < 0)) {
		Com_Printf("R_DrawAliasMD3Model %s: no such oldframe %d\n",
			e->model->name, e->as.oldframe);
		e->as.frame = 0;
		e->as.oldframe = 0;
	}

	if (!r_lerpmodels->integer)
		e->as.backlerp = 0;

	if (r_shadows->integer == 1 && (e->flags & (RF_SHADOW | RF_BLOOD))) {
		if (!(e->flags & RF_TRANSLUCENT))
			qglDepthMask(GL_FALSE);
		RSTATE_ENABLE_BLEND

		R_Color(NULL);
		if (e->flags & RF_SHADOW)
			R_Bind(shadow->texnum);
		else
			R_Bind(blood->texnum);
		qglBegin(GL_QUADS);

		qglTexCoord2f(0.0, 1.0);
		qglVertex3f(-18.0, 14.0, -28.5);
		qglTexCoord2f(1.0, 1.0);
		qglVertex3f(10.0, 14.0, -28.5);
		qglTexCoord2f(1.0, 0.0);
		qglVertex3f(10.0, -14.0, -28.5);
		qglTexCoord2f(0.0, 0.0);
		qglVertex3f(-18.0, -14.0, -28.5);

		qglEnd();

		RSTATE_DISABLE_BLEND
		if (!(e->flags & RF_TRANSLUCENT))
			qglDepthMask(GL_TRUE);
	} else if (r_shadows->integer == 2 && (e->flags & (RF_SHADOW | RF_BLOOD))) {
		R_DrawShadowVolume(e);
	}

	if (r_fog->integer && refdef.fog)
		qglDisable(GL_FOG);

	for (i = 0; i < paliashdr->num_meshes; i++) {
		skin = e->model->alias.skins_img[e->skinnum];
		if (!skin)
			skin = r_notexture;
		R_Bind(skin->texnum);
		/* locate the proper data */
		c_alias_polys += paliashdr->meshes[i].num_tris;
		R_DrawAliasMD3FrameLerp(paliashdr, paliashdr->meshes[i], e->as.backlerp);
	}

	qglDisable(GL_LIGHTING);

	if (e->flags & RF_TRANSLUCENT)
		RSTATE_DISABLE_BLEND

	qglPopMatrix();

	if (r_fog->integer && refdef.fog)
		qglEnable(GL_FOG);

	R_Color(NULL);
}
