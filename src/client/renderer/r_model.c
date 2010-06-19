/**
 * @file r_model.c
 * @brief model loading and caching
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

model_t r_models[MAX_MOD_KNOWN];
int r_numModels;
static int r_numModelsStatic;

model_t *r_mapTiles[MAX_MAPTILES];
int r_numMapTiles;

/* the inline * models from the current map are kept seperate */
model_t r_modelsInline[MAX_MOD_KNOWN];
int r_numModelsInline;

/**
 * @brief Prints all loaded models
 */
void R_ModModellist_f (void)
{
	int i;
	model_t *mod;

	Com_Printf("Loaded models:\n");
	Com_Printf("Type | #Slot | #Tris   | #Meshes | Filename\n");
	for (i = 0, mod = r_models; i < r_numModels; i++, mod++) {
		if (!mod->name[0]) {
			Com_Printf("Empty slot %i\n", i);
			continue;
		}
		switch(mod->type) {
		case mod_alias_md3:
			Com_Printf("MD3 ");
			break;
		case mod_alias_md2:
			Com_Printf("MD2 ");
			break;
		case mod_alias_dpm:
			Com_Printf("DPM ");
			break;
		case mod_bsp:
			Com_Printf("BSP ");
			break;
		case mod_bsp_submodel:
			Com_Printf("SUB ");
			break;
		case mod_obj:
			Com_Printf("OBJ ");
			break;
		default:
			Com_Printf("%3i ", mod->type);
			break;
		}
		if (mod->alias.num_meshes) {
			int j;
			Com_Printf(" | %5i | %7i | %7i | %s (skins: %i)\n", i, mod->alias.num_meshes, mod->alias.meshes[0].num_tris, mod->name, mod->alias.meshes[0].num_skins);
			for (j = 0; j < mod->alias.meshes[0].num_skins; j++) {
				mAliasSkin_t *skin = &mod->alias.meshes[0].skins[j];
				Com_Printf("     \\-- skin %i: '%s' (texnum %i and image type %i)\n", j + 1, skin->name, skin->skin->texnum, skin->skin->type);
			}
		} else
			Com_Printf(" | %5i | %7i | unknown | %s\n", i, mod->alias.num_meshes, mod->name);
	}
	Com_Printf("%4i models loaded\n", r_numModels);
	Com_Printf(" - %4i static models\n", r_numModelsStatic);
	Com_Printf(" - %4i bsp models\n", r_numMapTiles);
	Com_Printf(" - %4i inline models\n", r_numModelsInline);
}

/**
 * @brief Loads in a model for the given name
 * @param[in] name Filename relative to base dir and with extension (models/model.md2)
 */
static model_t *R_ModForName (const char *name, qboolean crash)
{
	model_t *mod;
	byte *buf;
	int i;
	int modfilelen;

	if (name[0] == '\0')
		Com_Error(ERR_FATAL, "R_ModForName: NULL name");

	/* inline models are grabbed only from worldmodel */
	if (name[0] == '*') {
		i = atoi(name + 1) - 1;
		if (i < 0 || i >= r_numModelsInline)
			Com_Error(ERR_FATAL, "bad inline model number '%s' (%i/%i)", name, i, r_numModelsInline);
		return &r_modelsInline[i];
	}

	/* search the currently loaded models */
	for (i = 0, mod = r_models; i < r_numModels; i++, mod++)
		if (!strcmp(mod->name, name))
			return mod;

	/* find a free model slot spot */
	for (i = 0, mod = r_models; i < r_numModels; i++, mod++) {
		if (!mod->name[0])
			break;				/* free spot */
	}

	if (i == r_numModels) {
		if (r_numModels == MAX_MOD_KNOWN)
			Com_Error(ERR_FATAL, "r_numModels == MAX_MOD_KNOWN");
		r_numModels++;
	}

	memset(mod, 0, sizeof(*mod));
	Q_strncpyz(mod->name, name, sizeof(mod->name));

	/* load the file */
	modfilelen = FS_LoadFile(mod->name, &buf);
	if (!buf) {
		if (crash)
			Com_Error(ERR_FATAL, "R_ModForName: %s not found", mod->name);
		memset(mod->name, 0, sizeof(mod->name));
		r_numModels--;
		return NULL;
	}

	/* call the appropriate loader */
	switch (LittleLong(*(unsigned *) buf)) {
	case IDALIASHEADER:
		/* MD2 header */
		R_ModLoadAliasMD2Model(mod, buf, modfilelen, qtrue);
		break;

	case DPMHEADER:
		R_ModLoadAliasDPMModel(mod, buf, modfilelen);
		break;

	case IDMD3HEADER:
		/* MD3 header */
		R_ModLoadAliasMD3Model(mod, buf, modfilelen);
		break;

	case IDBSPHEADER:
		Com_Error(ERR_FATAL, "R_ModForName: don't load BSPs with this function");
		break;

	default:
		if (!Q_strcasecmp(mod->name + strlen(mod->name) - 4, ".obj"))
			R_LoadObjModel(mod, buf, modfilelen);
		else
			Com_Error(ERR_FATAL, "R_ModForName: unknown fileid for %s", mod->name);
	}

	FS_FreeFile(buf);

	return mod;
}

/**
 * @brief all supported model formats
 * @sa modtype_t
 */
static const char *mod_extensions[] = {
	"md2", "md3", "dpm", "obj", NULL
};

/**
 * @brief Tries to load a model
 * @param[in] name The model path or name (with or without extension) - see notes
 * this parameter is always relativ to the game base dir - it can also be relative
 * to the models/ dir in the game folder
 * @note trying all supported model formats is only supported when you are using
 * a name without extension and relative to models/ dir
 * @note if first char of name is a '*' - this is an inline model
 * @note if there is not extension in the given filename the function will
 * try to load one of the supported model formats
 * @return NULL if no model could be found with the given name, model_t otherwise
 */
model_t *R_RegisterModelShort (const char *name)
{
	model_t *mod;
	int i = 0;

	if (!name || !name[0])
		return NULL;

	if (name[0] != '*' && (strlen(name) < 4 || name[strlen(name) - 4] != '.')) {
		char filename[MAX_QPATH];

		while (mod_extensions[i]) {
			Com_sprintf(filename, sizeof(filename), "models/%s.%s", name, mod_extensions[i]);
			mod = R_ModForName(filename, qfalse);
			if (mod)
				return mod;
			i++;
		}
		Com_Printf("R_RegisterModelShort: Could not find: '%s'\n", name);
		return NULL;
	} else
		return R_ModForName(name, qfalse);
}

#define MEM_TAG_STATIC_MODELS 1
/**
 * @brief After all static models are loaded, switch the pool tag for these models
 * to not free them everytime R_ShutdownModels is called
 * @sa CL_InitAfter
 * @sa R_FreeWorldImages
 */
void R_SwitchModelMemPoolTag (void)
{
	int i, j, k;
	model_t* mod;

	r_numModelsStatic = r_numModels;
	Mem_ChangeTag(vid_modelPool, 0, MEM_TAG_STATIC_MODELS);

	/* mark the static model textures as it_static, thus R_FreeWorldImages
	 * won't free them */
	for (i = 0, mod = r_models; i < r_numModelsStatic; i++, mod++) {
		if (!mod->alias.num_meshes)
			Com_Printf("Model '%s' has no meshes\n", mod->name);
		for (j = 0; j < mod->alias.num_meshes; j++) {
			if (!mod->alias.meshes[j].num_skins)
				Com_Printf("Model '%s' has no skins\n", mod->name);
			for (k = 0; k < mod->alias.meshes[j].num_skins; k++) {
				if (mod->alias.meshes[j].skins[k].skin != r_noTexture)
					mod->alias.meshes[j].skins[k].skin->type = it_static;
				else
					Com_Printf("No skin for #%i of '%s'\n", j, mod->name);
			}
		}
	}

	Com_Printf("%i static models loaded\n", r_numModels);
}

/**
 * @brief Frees the model pool
 * @param complete If this is true the static mesh models are freed, too
 * @sa R_SwitchModelMemPoolTag
 */
void R_ShutdownModels (qboolean complete)
{
	int i;
	const int start = complete ? 0 : r_numModelsStatic;

	/* free the vertex buffer - but not for the static models
	 * the world, the submodels and all the misc_models are located in the
	 * r_models array */
	for (i = start; i < r_numModels; i++) {
		model_t *mod = &r_models[i];

		if (mod->bsp.vertex_buffer)
			qglDeleteBuffers(1, &mod->bsp.vertex_buffer);
		if (mod->bsp.texcoord_buffer)
			qglDeleteBuffers(1, &mod->bsp.texcoord_buffer);
		if (mod->bsp.lmtexcoord_buffer)
			qglDeleteBuffers(1, &mod->bsp.lmtexcoord_buffer);
		if (mod->bsp.normal_buffer)
			qglDeleteBuffers(1, &mod->bsp.normal_buffer);
		if (mod->bsp.tangent_buffer)
			qglDeleteBuffers(1, &mod->bsp.tangent_buffer);
	}

	/* don't free the static models with the tag MEM_TAG_STATIC_MODELS */
	if (complete) {
		if (vid_modelPool)
			Mem_FreePool(vid_modelPool);
		if (vid_lightPool)
			Mem_FreePool(vid_lightPool);
		r_numModels = 0;
		memset(&r_models, 0, sizeof(r_models));
	} else {
		if (vid_modelPool)
			Mem_FreeTag(vid_modelPool, 0);
		if (vid_lightPool)
			Mem_FreeTag(vid_lightPool, 0);
		r_numModels = r_numModelsStatic;
	}
}

static void R_ModelBsp2Alias (model_t *mod)
{
	int i;
	mBspModel_t *bsp = &(mod->bsp);
	mAliasModel_t *alias = &(mod->alias);
	mAliasMesh_t *mesh = mod->alias.meshes;

	alias->num_frames = 1;
	alias->curFrame = -1;
	alias->num_bones = 0;
	alias->num_anims = 0;
	alias->curAnim = 0;
	alias->num_meshes = 1;

	mesh = (mAliasMesh_t *) Mem_PoolAlloc(sizeof(mAliasMesh_t), vid_modelPool, 0);
	strncpy(mesh->name, "BSP Mesh", MODEL_MAX_PATH);
	mesh->num_bones=0;

	mesh->verts = bsp->verts;
	mesh->normals = bsp->normals;
	mesh->tangents = bsp->tangents;
	mesh->texcoords = bsp->texcoords;

	/* @todo - add vertex buffers to alias models? */

	mesh->num_verts = bsp->numvertexes;
	mesh->vertexes = (mAliasVertex_t *) Mem_PoolAlloc(sizeof(mAliasVertex_t) * bsp->numvertexes, vid_modelPool, 0);
	mesh->stcoords = (mAliasCoord_t *) Mem_PoolAlloc(sizeof(mAliasCoord_t) * bsp->numvertexes, vid_modelPool, 0);
	for (i=0; i < bsp->numvertexes; i++) {
		/* @todo - BSPs are drawn as polygons; need to do triangle tesselation*/
		VectorCopy(bsp->vertexes[i].position, mesh->vertexes[i].point);
		VectorCopy(bsp->vertexes[i].normal, mesh->vertexes[i].normal);
		/* @todo - convert texcoods with:
		 * float u = v[0] * uv[0] + v[1] * uv[1] + v[2] * uv[2] + u_offset
		 * float v = v[0] * vv[0] + v[1] * vv[1] + v[2] * vv[2] + v_offset
		 */
		//Vector2Copy(bsp->vertexes[i].normal, mesh->texcoords[i]);
	}

}


void R_MakeDrawable (model_t *mod)
{
	switch (mod->type) {
		case mod_drawable: /* already done */
			break;
		case mod_bsp: /* translate BSP model into alias model */
			R_ModelBsp2Alias(mod);
			break;
		case mod_alias_md2: /* we already have an alias model */
		case mod_alias_md3:
		case mod_alias_dpm:
			mod->type = mod_drawable;
			break;
		case mod_obj:
		default:
			Com_Error(ERR_DROP, "R_MakeDrawable: bad or unhandled model format for %s", mod->name);
			break;
	}
}
