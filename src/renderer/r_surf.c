/**
 * @file r_surf.c
 * @brief surface-related refresh code
 */

/*
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

static vec3_t modelorg;			/* relative to viewpoint */

static mBspSurface_t *r_alpha_surfaces;

#define LIGHTMAP_BYTES 4

#define	BLOCK_WIDTH		256
#define	BLOCK_HEIGHT	256

#define	MAX_LIGHTMAPS	256

static int c_visible_lightmaps;

#define GL_LIGHTMAP_FORMAT GL_RGBA

typedef struct {
	int current_lightmap_texture;

	mBspSurface_t *lightmap_surfaces[MAX_LIGHTMAPS];

	int allocated[BLOCK_WIDTH];

	/* the lightmap texture data needs to be kept in */
	/* main memory so texsubimage can update properly */
	byte lightmap_buffer[LIGHTMAP_BYTES * BLOCK_WIDTH * BLOCK_HEIGHT];
} gllightmapstate_t;

static gllightmapstate_t gl_lms;


static void LM_InitBlock(void);
static void LM_UploadBlock(qboolean dynamic);
static qboolean LM_AllocBlock(int w, int h, int *x, int *y);

/*
=============================================================
BRUSH MODELS
=============================================================
*/

/**
 * @brief Returns the proper texture for a given time and base texture
 */
static image_t *R_TextureAnimation (mBspTexInfo_t * tex)
{
	int c;

	if (!tex->next)
		return tex->image;

	c = currententity->as.frame % tex->numframes;
	while (c) {
		tex = tex->next;
		c--;
	}

	return tex->image;
}

void R_DrawTriangleOutlines (void)
{
	int i, j;
	mBspPoly_t *p;

	if (!r_showtris->integer)
		return;

	qglDisable(GL_TEXTURE_2D);
	qglDisable(GL_DEPTH_TEST);
	R_Color(NULL);

	for (i = 0; i < MAX_LIGHTMAPS; i++) {
		mBspSurface_t *surf;

		for (surf = gl_lms.lightmap_surfaces[i]; surf != 0; surf = surf->lightmapchain) {
			p = surf->polys;
			for (; p; p = p->chain) {
				for (j = 2; j < p->numverts; j++) {
					qglBegin(GL_LINE_STRIP);
					qglVertex3fv(p->verts[0]);
					qglVertex3fv(p->verts[j - 1]);
					qglVertex3fv(p->verts[j]);
					qglVertex3fv(p->verts[0]);
					qglEnd();
				}
			}
		}
	}

	qglEnable(GL_DEPTH_TEST);
	qglEnable(GL_TEXTURE_2D);
}

/**
 * @param[in] scroll != 0 for SURF_FLOWING
 */
static void R_DrawPoly (const mBspSurface_t * fa, const float scroll)
{
	int i;
	float *v;
	mBspPoly_t *p = fa->polys;

	qglBegin(GL_POLYGON);
	v = p->verts[0];
	for (i = 0; i < p->numverts; i++, v += VERTEXSIZE) {
		qglTexCoord2f(v[3] + scroll, v[4]);
		qglVertex3fv(v);
	}
	qglEnd();
}

/**
 * @brief Used for flowing and non-flowing surfaces
 * @param[in] scroll != 0 for SURF_FLOWING
 * @sa R_DrawSurface
 * @sa R_DrawPolyChainOffset
 */
static void R_DrawPolyChain (const mBspSurface_t *surf, const float scroll)
{
	int i, nv = surf->polys->numverts;
	float *v;
	mBspPoly_t *p;

	assert(surf->polys);
	for (p = surf->polys; p; p = p->chain) {
		v = p->verts[0];
		qglBegin(GL_POLYGON);
		for (i = 0; i < nv; i++, v += VERTEXSIZE) {
			qglMultiTexCoord2fARB(gl_texture0, (v[3] + scroll), v[4]);
			qglMultiTexCoord2fARB(gl_texture1, v[5], v[6]);
			qglVertex3fv(v);
		}
		qglEnd();
	}
}

/**
 * @sa R_DrawPolyChain
 */
static void R_DrawPolyChainOffset (const mBspPoly_t * p, const float soffset, const float toffset)
{
	for (; p != 0; p = p->chain) {
		const float *v;
		int j;

		qglBegin(GL_POLYGON);
		v = p->verts[0];
		for (j = 0; j < p->numverts; j++, v += VERTEXSIZE) {
			qglTexCoord2f(v[5] - soffset, v[6] - toffset);
			qglVertex3fv(v);
		}
		qglEnd();
	}
}

/**
 * @brief This routine takes all the given light mapped surfaces in the world and
 * blends them into the framebuffer.
 * Only used for inline bmodels or used when no multitexturing is supported
 */
static void R_BlendLightmaps (void)
{
	int i;
	mBspSurface_t *surf, *newdrawsurf = 0;

	if (!rTiles[0]->bsp.lightdata)
		return;

	/* don't bother writing Z */
	qglDepthMask(GL_FALSE);

	/* set the appropriate blending mode unless we're only looking at the
	 * lightmaps. */
	if (!r_lightmap->integer) {
		RSTATE_ENABLE_BLEND

		qglBlendFunc(GL_ZERO, GL_SRC_COLOR);
	}

	if (currentmodel == rTiles[0])
		c_visible_lightmaps = 0;

	/* render static lightmaps first */
	for (i = 1; i < MAX_LIGHTMAPS; i++) {
		if (gl_lms.lightmap_surfaces[i]) {
			if (currentmodel == rTiles[0])
				c_visible_lightmaps++;
			R_Bind(r_state.lightmap_texnum + i);

			for (surf = gl_lms.lightmap_surfaces[i]; surf; surf = surf->lightmapchain) {
				if (surf->polys)
					R_DrawPolyChainOffset(surf->polys, 0, 0);
			}
		}
	}

	/* render dynamic lightmaps */
	if (r_dynamic->integer) {
		LM_InitBlock();

		R_Bind(r_state.lightmap_texnum + 0);

		if (currentmodel == rTiles[0])
			c_visible_lightmaps++;

		newdrawsurf = gl_lms.lightmap_surfaces[0];

		for (surf = gl_lms.lightmap_surfaces[0]; surf; surf = surf->lightmapchain) {
			int smax, tmax;
			byte *base;

			smax = (surf->extents[0] >> surf->lquant) + 1;
			tmax = (surf->extents[1] >> surf->lquant) + 1;

			if (LM_AllocBlock(smax, tmax, &surf->dlight_s, &surf->dlight_t)) {
				base = gl_lms.lightmap_buffer;
				base += (surf->dlight_t * BLOCK_WIDTH + surf->dlight_s) * LIGHTMAP_BYTES;

				R_BuildLightMap(surf, base, BLOCK_WIDTH * LIGHTMAP_BYTES);
			} else {
				mBspSurface_t *drawsurf;

				/* upload what we have so far */
				LM_UploadBlock(qtrue);

				/* draw all surfaces that use this lightmap */
				for (drawsurf = newdrawsurf; drawsurf != surf; drawsurf = drawsurf->lightmapchain) {
					if (drawsurf->polys)
						R_DrawPolyChainOffset(drawsurf->polys,
							(drawsurf->light_s - drawsurf->dlight_s) * (1.0 / BLOCK_WIDTH),
							(drawsurf->light_t - drawsurf->dlight_t) * (1.0 / BLOCK_HEIGHT));
				}

				newdrawsurf = drawsurf;

				/* clear the block */
				LM_InitBlock();

				/* try uploading the block now */
				if (!LM_AllocBlock(smax, tmax, &surf->dlight_s, &surf->dlight_t))
					Sys_Error("Consecutive calls to LM_AllocBlock(%d,%d) failed (dynamic)\n", smax, tmax);

				base = gl_lms.lightmap_buffer;
				base += (surf->dlight_t * BLOCK_WIDTH + surf->dlight_s) * LIGHTMAP_BYTES;

				R_BuildLightMap(surf, base, BLOCK_WIDTH * LIGHTMAP_BYTES);
			}
		}

		/* draw remainder of dynamic lightmaps that haven't been uploaded yet */
		if (newdrawsurf)
			LM_UploadBlock(qtrue);

		for (surf = newdrawsurf; surf != 0; surf = surf->lightmapchain) {
			if (surf->polys)
				R_DrawPolyChainOffset(surf->polys, (surf->light_s - surf->dlight_s) * (1.0 / BLOCK_WIDTH), (surf->light_t - surf->dlight_t) * (1.0 / BLOCK_HEIGHT));
		}
	}

	/* restore state */
	RSTATE_DISABLE_BLEND
	qglBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	qglDepthMask(GL_TRUE);
}

/**
 * @brief
 * @sa R_BuildLightMap
 */
static void R_SetCacheState (mBspSurface_t * surf)
{
	int maps;

	for (maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++)
		surf->cached_light[maps] = refdef.lightstyles[surf->styles[maps]].white;
}

static void R_RenderBrushPoly (mBspSurface_t * fa)
{
	int maps;
	image_t *image;
	qboolean is_dynamic = qfalse;

	c_brush_polys++;

	image = R_TextureAnimation(fa->texinfo);

#ifdef HAVE_SHADERS
	if (image->shader)
		SH_UseShader(image->shader, qfalse);
#endif

	if (fa->flags & SURF_DRAWTURB) {
		vec4_t color = {r_state.inverse_intensity, r_state.inverse_intensity, r_state.inverse_intensity, 1.0f};
		R_Bind(image->texnum);

		/* warp texture, no lightmaps */
		R_TexEnv(GL_MODULATE);
		R_Color(color);
		R_DrawTurbSurface(fa);
		R_TexEnv(GL_REPLACE);
		return;
	} else {
		R_Bind(image->texnum);
		R_TexEnv(GL_REPLACE);
	}

	if (fa->texinfo->flags & SURF_FLOWING) {
		float scroll;
		scroll = -64 * ((refdef.time / 40.0) - (int) (refdef.time / 40.0));
		if (scroll == 0.0)
			scroll = -64.0;
		R_DrawPoly(fa, scroll);
	} else
		R_DrawPoly(fa, 0.0f);

	/* check for lightmap modification */
	for (maps = 0; maps < MAXLIGHTMAPS && fa->styles[maps] != 255; maps++) {
		if (refdef.lightstyles[fa->styles[maps]].white != fa->cached_light[maps])
			goto dynamic;
	}

	/* dynamic this frame or dynamic previously */
	if ((fa->dlightframe == r_framecount)) {
	  dynamic:
		if (r_dynamic->integer) {
			if (!(fa->texinfo->flags & (SURF_TRANS33 | SURF_TRANS66 | SURF_WARP)))
				is_dynamic = qtrue;
		}
	}

	if (is_dynamic) {
		if ((fa->styles[maps] >= 32 || fa->styles[maps] == 0) && (fa->dlightframe != r_framecount)) {
			unsigned temp[34 * 34];
			int smax, tmax;

			smax = (fa->extents[0] >> fa->lquant) + 1;
			tmax = (fa->extents[1] >> fa->lquant) + 1;

			R_BuildLightMap(fa, (byte *) temp, smax * 4);
			R_SetCacheState(fa);

			R_Bind(r_state.lightmap_texnum + fa->lightmaptexturenum);

			qglTexSubImage2D(GL_TEXTURE_2D, 0, fa->light_s, fa->light_t, smax, tmax, GL_LIGHTMAP_FORMAT, GL_UNSIGNED_BYTE, temp);
			R_CheckError();

			fa->lightmapchain = gl_lms.lightmap_surfaces[fa->lightmaptexturenum];
			gl_lms.lightmap_surfaces[fa->lightmaptexturenum] = fa;
		} else {
			fa->lightmapchain = gl_lms.lightmap_surfaces[0];
			gl_lms.lightmap_surfaces[0] = fa;
		}
	} else {
		fa->lightmapchain = gl_lms.lightmap_surfaces[fa->lightmaptexturenum];
		gl_lms.lightmap_surfaces[fa->lightmaptexturenum] = fa;
	}

#ifdef HAVE_SHADERS
	if (image->shader)
		SH_UseShader(image->shader, qtrue);
#endif
}


/**
 * @brief Draw water surfaces and windows.
 * The BSP tree is waled front to back, so unwinding the chain
 * of alpha_surfaces will draw back to front, giving proper ordering.
 */
void R_DrawAlphaSurfaces (void)
{
	mBspSurface_t *s;
	float intens;
	vec4_t color = {1, 1, 1, 1};

	/* go back to the world matrix */
	qglLoadMatrixf(r_world_matrix);

	RSTATE_ENABLE_BLEND
	qglDepthMask(GL_FALSE); /* disable depth writing */
	R_TexEnv(GL_MODULATE);

	/* the textures are prescaled up for a better lighting range, */
	/* so scale it back down */
	intens = r_state.inverse_intensity;

	for (s = r_alpha_surfaces; s; s = s->texturechain) {
#ifdef HAVE_SHADERS
		if (s->texinfo->image->shader)
			SH_UseShader(s->texinfo->image->shader, qfalse);
#endif
		R_Bind(s->texinfo->image->texnum);
		c_brush_polys++;

		if (s->texinfo->flags & SURF_TRANS33)
			color[3] = 0.33;
		else if (s->texinfo->flags & SURF_TRANS66)
			color[3] = 0.66;
		else
			color[3] = 1.0;

		R_Color(color);

		if (s->flags & SURF_DRAWTURB)
			R_DrawTurbSurface(s);
		else if (s->texinfo->flags & SURF_FLOWING) {
			float scroll;
			scroll = -64 * ((refdef.time / 40.0) - (int) (refdef.time / 40.0));
			if (scroll == 0.0)
				scroll = -64.0;
			R_DrawPoly(s, scroll);
		} else
			R_DrawPoly(s, 0.0f);

#ifdef HAVE_SHADERS
		if (s->texinfo->image->shader)
			SH_UseShader(s->texinfo->image->shader, qtrue);
#endif
	}

	R_TexEnv(GL_REPLACE);
	qglDepthMask(GL_TRUE); /* reenable depth writing */
	R_ColorBlend(NULL);

	r_alpha_surfaces = NULL;
}

/**
 * @brief Draw the lightmapped surface
 * @note If multitexturing is not supported this function will only draw the
 * surface without any lightmap
 * @sa R_RenderBrushPoly
 * @sa R_DrawPolyChain
 * @sa R_DrawWorld
 */
static void R_DrawSurface (mBspSurface_t * surf)
{
	int map;
	image_t *image = R_TextureAnimation(surf->texinfo);
	qboolean is_dynamic = qfalse;
	unsigned lmtex = surf->lightmaptexturenum;

	/* no multitexturing supported - draw the poly now and blend the lightmap
	 * later in R_DrawWorld */
	if (!qglMultiTexCoord2fARB) {
		R_RenderBrushPoly(surf);
		return;
	}

	if (surf->texinfo->flags & SURF_ALPHATEST)
		RSTATE_ENABLE_ALPHATEST

	for (map = 0; map < MAXLIGHTMAPS && surf->styles[map] != 255; map++) {
		if (refdef.lightstyles[surf->styles[map]].white != surf->cached_light[map])
			goto dynamic;
	}

	/* dynamic this frame or dynamic previously */
	if ((surf->dlightframe == r_framecount)) {
	  dynamic:
		if (r_dynamic->integer) {
			if (!(surf->texinfo->flags & (SURF_TRANS33 | SURF_TRANS66 | SURF_WARP)))
				is_dynamic = qtrue;
		}
	}

	if (is_dynamic) {
		unsigned temp[BLOCK_WIDTH * BLOCK_HEIGHT];
		int smax, tmax;

		if ((surf->styles[map] >= 32 || surf->styles[map] == 0) && (surf->dlightframe != r_framecount)) {
			smax = (surf->extents[0] >> surf->lquant) + 1;
			tmax = (surf->extents[1] >> surf->lquant) + 1;

			R_BuildLightMap(surf, (byte *) temp, smax * 4);
			R_SetCacheState(surf);

			R_MBind(gl_texture1, r_state.lightmap_texnum + surf->lightmaptexturenum);

			lmtex = surf->lightmaptexturenum;

			qglTexSubImage2D(GL_TEXTURE_2D, 0, surf->light_s, surf->light_t, smax, tmax, GL_LIGHTMAP_FORMAT, GL_UNSIGNED_BYTE, temp);
			R_CheckError();

		} else {
			smax = (surf->extents[0] >> surf->lquant) + 1;
			tmax = (surf->extents[1] >> surf->lquant) + 1;

			R_BuildLightMap(surf, (byte *) temp, smax * 4);

			R_MBind(gl_texture1, r_state.lightmap_texnum + 0);

			lmtex = 0;

			qglTexSubImage2D(GL_TEXTURE_2D, 0, surf->light_s, surf->light_t, smax, tmax, GL_LIGHTMAP_FORMAT, GL_UNSIGNED_BYTE, temp);
			R_CheckError();
		}
	}

	c_brush_polys++;

	R_MBind(gl_texture0, image->texnum);
	R_MBind(gl_texture1, r_state.lightmap_texnum + lmtex);

	if (surf->texinfo->flags & SURF_FLOWING) {
		float scroll;

		scroll = -64 * ((refdef.time / 40.0) - (int) (refdef.time / 40.0));
		if (scroll == 0.0)
			scroll = -64.0;

		R_DrawPolyChain(surf, scroll);
	} else {
		R_DrawPolyChain(surf, 0.0f);
	}

	RSTATE_DISABLE_ALPHATEST
}

static void R_DrawInlineBModel (void)
{
	int i, k;
	cBspPlane_t *pplane;
	float dot;
	mBspSurface_t *psurf, *s;
	dlight_t *lt;
	qboolean duplicate = qfalse;
	/* calculate dynamic lighting for bmodel */
	if (!r_flashblend->integer) {
		lt = refdef.dlights;
		for (k = 0; k < refdef.num_dlights; k++, lt++)
			R_MarkLights(lt, 1 << k, currentmodel->bsp.nodes + currentmodel->bsp.firstnode);
	}

	psurf = &currentmodel->bsp.surfaces[currentmodel->bsp.firstmodelsurface];

	if (currententity->flags & RF_TRANSLUCENT) {
		vec4_t color = {1, 1, 1, 0.25};
		R_ColorBlend(color);
		R_TexEnv(GL_MODULATE);
	}

	/* set r_alpha_surfaces = NULL to ensure psurf->texturechain terminates */
	/* FIXME: needed? if we do this, transparent surfaces may be skipped */
	/*r_alpha_surfaces = NULL;*/

	/* draw texture */
	for (i = 0; i < currentmodel->bsp.nummodelsurfaces; i++, psurf++) {
		/* find which side of the node we are on */
		pplane = psurf->plane;

		if (r_isometric->integer)
			dot = -DotProduct(vpn, pplane->normal);
		else
			dot = DotProduct(modelorg, pplane->normal) - pplane->dist;

		/* draw the polygon */
		if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) || (!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON))) {
			/* add to the translucent chain */
			if (psurf->texinfo->flags & (SURF_TRANS33 | SURF_TRANS66)) {
				/* if bmodel is used by multiple entities, adding surface
				 * to linked list more than once would result in an infinite loop */
				for (s = r_alpha_surfaces; s; s = s->texturechain)
					if (s == psurf) {
						duplicate = qtrue;
						break;
					}
				/* Don't allow surface to be added twice (fixes hang) */
				if (!duplicate) {
					psurf->texturechain = r_alpha_surfaces;
					r_alpha_surfaces = psurf;
				}
			} else if (!(psurf->flags & SURF_DRAWTURB)) {
				R_DrawSurface(psurf);
			} else {
				R_EnableMultitexture(qfalse);
				R_RenderBrushPoly(psurf);
				R_EnableMultitexture(qtrue);
			}
		}
	}

	if (!(currententity->flags & RF_TRANSLUCENT)) {
		if (!qglMultiTexCoord2fARB)
			R_BlendLightmaps();
	} else {
		R_ColorBlend(NULL);
		R_TexEnv(GL_REPLACE);
	}
}

/**
 * @brief Draws a brush model
 * @note E.g. a func_breakable or func_door
 */
void R_DrawBrushModel (entity_t * e)
{
	vec3_t mins, maxs;
	int i;
	qboolean rotated;

/*	Com_DPrintf(DEBUG_RENDERER, "Brush model %i!\n", currentmodel->bsp.nummodelsurfaces);*/

	if (currentmodel->bsp.nummodelsurfaces == 0)
		return;

	currententity = e;
	r_state.currenttextures[0] = r_state.currenttextures[1] = -1;

	if (e->angles[0] || e->angles[1] || e->angles[2]) {
		rotated = qtrue;
		for (i = 0; i < 3; i++) {
			mins[i] = e->origin[i] - currentmodel->radius;
			maxs[i] = e->origin[i] + currentmodel->radius;
		}
	} else {
		rotated = qfalse;
		VectorAdd(e->origin, currentmodel->mins, mins);
		VectorAdd(e->origin, currentmodel->maxs, maxs);
	}

	if (R_CullBox(mins, maxs))
		return;

	R_Color(NULL);
	memset(gl_lms.lightmap_surfaces, 0, sizeof(gl_lms.lightmap_surfaces));

	VectorSubtract(refdef.vieworg, e->origin, modelorg);
	if (rotated) {
		vec3_t temp;
		vec3_t forward, right, up;

		VectorCopy(modelorg, temp);
		AngleVectors(e->angles, forward, right, up);
		modelorg[0] = DotProduct(temp, forward);
		modelorg[1] = -DotProduct(temp, right);
		modelorg[2] = DotProduct(temp, up);
	}

	qglPushMatrix();
	e->angles[0] = -e->angles[0];	/* stupid quake bug */
	e->angles[2] = -e->angles[2];	/* stupid quake bug */
	R_RotateForEntity(e);
	e->angles[0] = -e->angles[0];	/* stupid quake bug */
	e->angles[2] = -e->angles[2];	/* stupid quake bug */

	R_EnableMultitexture(qtrue);
	R_SelectTexture(gl_texture0);
	R_TexEnv(GL_REPLACE);
	R_SelectTexture(gl_texture1);
	R_TexEnv(GL_MODULATE);

	R_DrawInlineBModel();
	R_EnableMultitexture(qfalse);

	qglPopMatrix();
}


/*
=============================================================
WORLD MODEL
=============================================================
*/

/**
 * @sa R_DrawWorld
 */
static void R_RecursiveWorldNode (mBspNode_t * node)
{
	int c, side, sidebit;
	cBspPlane_t *plane;
	mBspSurface_t *surf;

	float dot;
	image_t *image;

	if (node->contents == CONTENTS_SOLID)
		return;					/* solid */

	if (R_CullBox(node->minmaxs, node->minmaxs + 3))
		return;

	/* if a leaf node, draw stuff */
	if (node->contents != LEAFNODE)
		return;

	/* node is just a decision point, so go down the apropriate sides */
	/* find which side of the node we are on */
	plane = node->plane;

	if (r_isometric->integer) {
		dot = -DotProduct(vpn, plane->normal);
	} else if (plane->type >= 3) {
		dot = DotProduct(modelorg, plane->normal) - plane->dist;
	} else {
		dot = modelorg[plane->type] - plane->dist;
	}

	if (dot >= 0) {
		side = 0;
		sidebit = 0;
	} else {
		side = 1;
		sidebit = SURF_PLANEBACK;
	}

	/* recurse down the children, front side first */
	R_RecursiveWorldNode(node->children[side]);

	/* draw stuff */
	for (c = node->numsurfaces, surf = currentmodel->bsp.surfaces + node->firstsurface; c; c--, surf++) {
		if ((surf->flags & SURF_PLANEBACK) != sidebit)
			continue;			/* wrong side */

		/* add to the translucent chain */
		if (surf->texinfo->flags & (SURF_TRANS33 | SURF_TRANS66)) {
			surf->texturechain = r_alpha_surfaces;
			r_alpha_surfaces = surf;
		} else {
			if (!(surf->flags & SURF_DRAWTURB))
				R_DrawSurface(surf);
			else {
				/* the polygon is visible, so add it to the texture */
				/* sorted chain */
				/* FIXME: this is a hack for animation */
				image = R_TextureAnimation(surf->texinfo);
				surf->texturechain = image->texturechain;
				image->texturechain = surf;
			}
		}
	}

	/* recurse down the back side */
	R_RecursiveWorldNode(node->children[!side]);
}


/**
 * @sa R_RecursiveWorldNode
 * @sa R_DrawLevelBrushes
 * @todo Batch all the static surfaces from each of the 8 levels in a surface list
 * and only recurse down when the level changed
 */
static void R_DrawWorld (mBspNode_t * nodes)
{
	entity_t ent;

	VectorCopy(refdef.vieworg, modelorg);

	/* auto cycle the world frame for texture animation */
	memset(&ent, 0, sizeof(ent));
	ent.as.frame = (int) (refdef.time * 2);
	currententity = &ent;

	r_state.currenttextures[0] = r_state.currenttextures[1] = -1;

	R_Color(NULL);
	memset(gl_lms.lightmap_surfaces, 0, sizeof(gl_lms.lightmap_surfaces));

	if (qglMultiTexCoord2fARB) {
		R_EnableMultitexture(qtrue);

		R_SelectTexture(gl_texture0);
		R_TexEnv(GL_REPLACE);
		R_SelectTexture(gl_texture1);

		if (r_config.envCombine) {
			R_TexEnv(r_config.envCombine);
			qglTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE_EXT, r_intensity->value);
		} else {
			R_TexEnv(GL_MODULATE);
		}

		R_RecursiveWorldNode(nodes);

		R_EnableMultitexture(qfalse);
	} else {
		R_RecursiveWorldNode(nodes);
		R_BlendLightmaps();
	}
}


/**
 * @sa R_DrawLevelBrushes
 */
static void R_FindModelNodes_r (mBspNode_t * node)
{
	if (!node->plane) {
		R_FindModelNodes_r(node->children[0]);
		R_FindModelNodes_r(node->children[1]);
	} else {
		R_DrawWorld(node);
	}
}


/**
 * @brief Draws the brushes for the current worldlevel and hide other levels
 * @sa cvar cl_worldlevel
 * @sa R_FindModelNodes_r
 */
void R_DrawLevelBrushes (void)
{
	entity_t ent;
	int i, tile, mask;

	if (refdef.rdflags & RDF_NOWORLDMODEL)
		return;

	if (!r_drawworld->integer)
		return;

	memset(&ent, 0, sizeof(ent));
	currententity = &ent;

	mask = 1 << refdef.worldlevel;

	for (tile = 0; tile < rNumTiles; tile++) {
		currentmodel = rTiles[tile];

		/* don't draw weapon-, actorclip and stepon */
		/* @note Change this to 258 to see the actorclip brushes in-game */
		for (i = 0; i <= LEVEL_LASTVISIBLE; i++) {
			/* check the worldlevel flags */
			if (i && !(i & mask))
				continue;

			if (!currentmodel->bsp.submodels[i].numfaces)
				continue;

			R_FindModelNodes_r(currentmodel->bsp.nodes + currentmodel->bsp.submodels[i].headnode);
		}
	}
}



/*
=============================================================================
LIGHTMAP ALLOCATION
=============================================================================
*/

static void LM_InitBlock (void)
{
	memset(gl_lms.allocated, 0, sizeof(gl_lms.allocated));
}

static void LM_UploadBlock (qboolean dynamic)
{
	int texture;
	int height = 0;

	if (dynamic)
		texture = 0;
	else
		texture = gl_lms.current_lightmap_texture;

	R_Bind(r_state.lightmap_texnum + texture);
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	R_CheckError();
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	R_CheckError();

	if (dynamic) {
		int i;

		for (i = 0; i < BLOCK_WIDTH; i++) {
			if (gl_lms.allocated[i] > height)
				height = gl_lms.allocated[i];
		}

		qglTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, BLOCK_WIDTH, height, GL_LIGHTMAP_FORMAT, GL_UNSIGNED_BYTE, gl_lms.lightmap_buffer);
		R_CheckError();
	} else {
		qglTexImage2D(GL_TEXTURE_2D, 0, gl_solid_format, BLOCK_WIDTH, BLOCK_HEIGHT, 0, GL_LIGHTMAP_FORMAT, GL_UNSIGNED_BYTE, gl_lms.lightmap_buffer);
		R_CheckError();
		if (++gl_lms.current_lightmap_texture == MAX_LIGHTMAPS)
			Com_Error(ERR_DROP, "LM_UploadBlock() - MAX_LIGHTMAPS exceeded\n");
	}
}

/**
 * @brief returns a texture number and the position inside it
 */
static qboolean LM_AllocBlock (int w, int h, int *x, int *y)
{
	int i, j;
	int best, best2;

	best = BLOCK_HEIGHT;

	for (i = 0; i < BLOCK_WIDTH - w; i++) {
		best2 = 0;

		for (j = 0; j < w; j++) {
			if (gl_lms.allocated[i + j] >= best)
				break;
			if (gl_lms.allocated[i + j] > best2)
				best2 = gl_lms.allocated[i + j];
		}
		/* this is a valid spot */
		if (j == w) {
			*x = i;
			*y = best = best2;
		}
	}

	if (best + h > BLOCK_HEIGHT)
		return qfalse;

	for (i = 0; i < w; i++)
		gl_lms.allocated[*x + i] = best + h;

	return qtrue;
}

void R_BuildPolygonFromSurface (mBspSurface_t * fa, int shift[3])
{
	int i, lindex, lnumverts;
	mBspEdge_t *pedges, *r_pedge;
	int vertpage;
	float *vec;
	float s, t;
	mBspPoly_t *poly;
	vec3_t total;

	/* reconstruct the polygon */
	pedges = currentmodel->bsp.edges;
	lnumverts = fa->numedges;
	vertpage = 0;

	VectorClear(total);

	/* draw texture */
	poly = VID_TagAlloc(vid_modelPool, sizeof(mBspPoly_t) + (lnumverts - 4) * VERTEXSIZE * sizeof(float), 0);
	poly->next = fa->polys;
	fa->polys = poly;
	poly->numverts = lnumverts;

	for (i = 0; i < lnumverts; i++) {
		lindex = currentmodel->bsp.surfedges[fa->firstedge + i];

		if (lindex > 0) {
			r_pedge = &pedges[lindex];
			vec = currentmodel->bsp.vertexes[r_pedge->v[0]].position;
		} else {
			r_pedge = &pedges[-lindex];
			vec = currentmodel->bsp.vertexes[r_pedge->v[1]].position;
		}
		s = DotProduct(vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		s /= fa->texinfo->image->width;

		t = DotProduct(vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
		t /= fa->texinfo->image->height;

		VectorAdd(total, vec, total);
		VectorAdd(vec, shift, poly->verts[i]);
		poly->verts[i][3] = s;
		poly->verts[i][4] = t;

		/* lightmap texture coordinates */
		s = DotProduct(vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		s -= fa->texturemins[0];
		s += fa->light_s << fa->lquant;
		s += 1 << (fa->lquant - 1);
		s /= BLOCK_WIDTH << fa->lquant;

		t = DotProduct(vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
		t -= fa->texturemins[1];
		t += fa->light_t << fa->lquant;
		t += 1 << (fa->lquant - 1);
		t /= BLOCK_HEIGHT << fa->lquant;

		poly->verts[i][5] = s;
		poly->verts[i][6] = t;
	}

	poly->numverts = lnumverts;
}

/**
 * @sa R_ModLoadFaces
 */
void R_CreateSurfaceLightmap (mBspSurface_t * surf)
{
	int smax, tmax;
	byte *base;

	if (surf->flags & SURF_DRAWTURB)
		return;

	smax = (surf->extents[0] >> surf->lquant) + 1;
	tmax = (surf->extents[1] >> surf->lquant) + 1;

	if (!LM_AllocBlock(smax, tmax, &surf->light_s, &surf->light_t)) {
		LM_UploadBlock(qfalse);
		LM_InitBlock();
		if (!LM_AllocBlock(smax, tmax, &surf->light_s, &surf->light_t))
			Sys_Error("Consecutive calls to LM_AllocBlock(%d,%d) failed (lquant: %i)\n", smax, tmax, surf->lquant);
	}

	surf->lightmaptexturenum = gl_lms.current_lightmap_texture;

	base = gl_lms.lightmap_buffer;
	base += (surf->light_t * BLOCK_WIDTH + surf->light_s) * LIGHTMAP_BYTES;

	R_SetCacheState(surf);
	R_BuildLightMap(surf, base, BLOCK_WIDTH * LIGHTMAP_BYTES);
}


/**
 * @sa R_ModBeginLoading
 * @sa R_EndBuildingLightmaps
 */
void R_BeginBuildingLightmaps (void)
{
	static lightstyle_t lightstyles[MAX_LIGHTSTYLES];
	int i;
	unsigned dummy[BLOCK_WIDTH * BLOCK_HEIGHT];

	memset(gl_lms.allocated, 0, sizeof(gl_lms.allocated));

	r_framecount = 1;			/* no dlightcache */

	R_EnableMultitexture(qtrue);
	R_SelectTexture(gl_texture1);

	/*
	 ** setup the base lightstyles so the lightmaps won't have to be regenerated
	 ** the first time they're seen
	 */
	for (i = 0; i < MAX_LIGHTSTYLES; i++) {
		lightstyles[i].rgb[0] = 1;
		lightstyles[i].rgb[1] = 1;
		lightstyles[i].rgb[2] = 1;
		lightstyles[i].white = 3;
	}
	refdef.lightstyles = lightstyles;

	if (!r_state.lightmap_texnum)
		r_state.lightmap_texnum = TEXNUM_LIGHTMAPS;

	gl_lms.current_lightmap_texture = 1;

	/* initialize the dynamic lightmap texture */
	R_Bind(r_state.lightmap_texnum + 0);
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	R_CheckError();
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	R_CheckError();
	qglTexImage2D(GL_TEXTURE_2D, 0, gl_solid_format, BLOCK_WIDTH, BLOCK_HEIGHT, 0, GL_LIGHTMAP_FORMAT, GL_UNSIGNED_BYTE, dummy);
	R_CheckError();
}

/**
 * @sa R_BeginBuildingLightmaps
 * @sa R_ModBeginLoading
 */
void R_EndBuildingLightmaps (void)
{
	LM_UploadBlock(qfalse);
	R_EnableMultitexture(qfalse);
/*	Com_Printf("lightmaps: %i\n", gl_lms.current_lightmap_texture); */
}
