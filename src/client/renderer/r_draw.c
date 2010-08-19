/**
 * @file r_draw.c
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
#include "r_sphere.h"
#include "r_error.h"
#include "r_draw.h"
#include "r_mesh.h"
#include "r_framebuffer.h"
#include "r_program.h"
#include "../cl_console.h"

image_t *shadow;

/* console font */
static image_t *draw_chars;

/**
 * @brief Loads some textures and init the 3d globe
 * @sa R_Init
 */
void R_DrawInitLocal (void)
{
	shadow = R_FindImage("pics/sfx/shadow", it_effect);
	if (shadow == r_noTexture)
		Com_Printf("Could not find shadow image in game pics/sfx directory!\n");

	draw_chars = R_FindImage("pics/conchars", it_chars);
	if (draw_chars == r_noTexture)
		Com_Error(ERR_FATAL, "Could not find conchars image in game pics directory!");
}

#define MAX_CHARS 8192

/* chars are batched per frame so that they are drawn in one shot */
static float char_texcoords[MAX_CHARS * 4 * 2];
static short char_verts[MAX_CHARS * 4 * 2];
static int char_index = 0;

/**
 * @brief Draws one 8*8 graphics character with 0 being transparent.
 * It can be clipped to the top of the screen to allow the console to be
 * smoothly scrolled off.
 */
void R_DrawChar (int x, int y, int num)
{
	int row, col;
	float frow, fcol;

	num &= 255;

	if ((num & 127) == ' ')		/* space */
		return;

	if (y <= -con_fontHeight)
		return;					/* totally off screen */

	if (char_index >= MAX_CHARS * 8)
		return;

	row = (int) num >> 4;
	col = (int) num & 15;

	/* 0.0625 => 16 cols (conchars images) */
	frow = row * 0.0625;
	fcol = col * 0.0625;

	char_texcoords[char_index + 0] = fcol;
	char_texcoords[char_index + 1] = frow;
	char_texcoords[char_index + 2] = fcol + 0.0625;
	char_texcoords[char_index + 3] = frow;
	char_texcoords[char_index + 4] = fcol + 0.0625;
	char_texcoords[char_index + 5] = frow + 0.0625;
	char_texcoords[char_index + 6] = fcol;
	char_texcoords[char_index + 7] = frow + 0.0625;

	char_verts[char_index + 0] = x;
	char_verts[char_index + 1] = y;
	char_verts[char_index + 2] = x + con_fontWidth;
	char_verts[char_index + 3] = y;
	char_verts[char_index + 4] = x + con_fontWidth;
	char_verts[char_index + 5] = y + con_fontHeight;
	char_verts[char_index + 6] = x;
	char_verts[char_index + 7] = y + con_fontHeight;

	char_index += 8;
}

void R_DrawChars (void)
{
	R_BindTexture(draw_chars->texnum);

	/* alter the array pointers */
	glVertexPointer(2, GL_SHORT, 0, char_verts);
	glTexCoordPointer(2, GL_FLOAT, 0, char_texcoords);

	glDrawArrays(GL_QUADS, 0, char_index / 2);

	char_index = 0;

	/* and restore them */
	R_BindDefaultArray(GL_TEXTURE_COORD_ARRAY);
	R_BindDefaultArray(GL_VERTEX_ARRAY);
}

/**
 * @brief Uploads image data
 * @param[in] name The name of the texture to use for this data
 * @param[in] frame The frame data that is uploaded
 * @param[in] width The width of the texture
 * @param[in] height The height of the texture
 * @return the texture number of the uploaded images
 */
int R_UploadData (const char *name, byte *frame, int width, int height)
{
	image_t *img;
	unsigned *scaled;
	int scaledWidth, scaledHeight;
	int samples = r_config.gl_compressed_solid_format ? r_config.gl_compressed_solid_format : r_config.gl_solid_format;
	int i, c;
	byte *scan;

	R_GetScaledTextureSize(width, height, &scaledWidth, &scaledHeight);

	img = R_FindImage(name, it_pic);
	if (img == r_noTexture)
		Com_Error(ERR_FATAL, "Could not find the searched image: %s", name);

	/* scan the texture for any non-255 alpha */
	c = scaledWidth * scaledHeight;
	/* set scan to the first alpha byte */
	for (i = 0, scan = ((byte *) frame) + 3; i < c; i++, scan += 4) {
		if (*scan != 255) {
			samples = r_config.gl_compressed_alpha_format ? r_config.gl_compressed_alpha_format : r_config.gl_alpha_format;
			break;
		}
	}

	if (scaledWidth != width || scaledHeight != height) {  /* whereas others need to be scaled */
		scaled = (unsigned *)Mem_PoolAllocExt(scaledWidth * scaledHeight * sizeof(unsigned), qfalse, vid_imagePool, 0);
		R_ScaleTexture((unsigned *)frame, width, height, scaled, scaledWidth, scaledHeight);
	} else {
		scaled = (unsigned *)frame;
	}

	R_BindTexture(img->texnum);
	if (img->upload_width == scaledWidth && img->upload_height == scaledHeight) {
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, scaledWidth, scaledHeight, GL_RGBA, GL_UNSIGNED_BYTE, scaled);
	} else {
		/* Reallocate the texture */
		img->width = width;
		img->height = height;
		img->upload_width = scaledWidth;
		img->upload_height = scaledHeight;
		glTexImage2D(GL_TEXTURE_2D, 0, samples, scaledWidth, scaledHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, scaled);
	}
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	R_CheckError();

	if (scaled != (unsigned *)frame)
		Mem_Free(scaled);

	return img->texnum;
}

/**
 * @brief Searches for an image in the image array
 * @param[in] name The name of the image relative to pics/
 * @note name may not be null and has to be longer than 4 chars
 * @return NULL on error or image_t pointer on success
 * @sa R_FindImage
 */
const image_t *R_RegisterImage (const char *name)
{
	const image_t *image = R_FindImage(va("pics/%s", name), it_pic);
	if (image == r_noTexture)
		return NULL;
	return image;
}

/**
 * @brief Bind and draw a texture
 * @param[in] texnum The texture id (already uploaded of course)
 * @param[in] x normalized x value on the screen
 * @param[in] y normalized y value on the screen
 * @param[in] w normalized width value
 * @param[in] h normalized height value
 */
void R_DrawTexture (int texnum, int x, int y, int w, int h)
{
	const vec2_t vertexes[4] = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};

	R_BindTexture(texnum);
	R_DrawImageArray(default_texcoords, vertexes, NULL);
}

/**
 * @brief Draws an image or parts of it
 * @param[in] x X position to draw the image to
 * @param[in] y Y position to draw the image to
 */
void R_DrawImage (float x, float y, const image_t *image)
{
	if (!image)
		return;

	R_DrawTexture(image->texnum, x * viddef.rx, y * viddef.ry, image->width * viddef.rx, image->height * viddef.ry);
}

void R_DrawStretchImage (float x, float y, int w, int h, const image_t *image)
{
	if (!image)
		return;

	R_DrawTexture(image->texnum, x * viddef.rx, y * viddef.ry, w * viddef.rx, h * viddef.ry);
}

const image_t *R_DrawImageArray (const vec2_t texcoords[4], const vec2_t verts[4], const image_t *image)
{
	/* alter the array pointers */
	glVertexPointer(2, GL_FLOAT, 0, verts);
	R_BindArray(GL_TEXTURE_COORD_ARRAY, GL_FLOAT, texcoords);

	if (image != NULL)
		R_BindTexture(image->texnum);

	glDrawArrays(GL_QUADS, 0, 4);

	/* and restore them */
	R_BindDefaultArray(GL_TEXTURE_COORD_ARRAY);
	R_BindDefaultArray(GL_VERTEX_ARRAY);

	return image;
}

/**
 * @brief Fills a box of pixels with a single color
 */
void R_DrawFill (int x, int y, int w, int h, const vec4_t color)
{
	const float nx = x * viddef.rx;
	const float ny = y * viddef.ry;
	const float nw = w * viddef.rx;
	const float nh = h * viddef.ry;

	R_Color(color);

	glDisable(GL_TEXTURE_2D);

	glBegin(GL_QUADS);
	glVertex2f(nx, ny);
	glVertex2f(nx + nw, ny);
	glVertex2f(nx + nw, ny + nh);
	glVertex2f(nx, ny + nh);
	glEnd();

	glEnable(GL_TEXTURE_2D);

	R_Color(NULL);
}

/**
 * @brief Draws a rect to the screen. Also has support for stippled rendering of the rect.
 *
 * @param[in] x X-position of the rect
 * @param[in] y Y-position of the rect
 * @param[in] w Width of the rect
 * @param[in] h Height of the rect
 * @param[in] color RGBA color of the rect
 * @param[in] lineWidth Line strength in pixel of the rect
 * @param[in] pattern Specifies a 16-bit integer whose bit pattern determines
 * which fragments of a line will be drawn when the line is rasterized.
 * Bit zero is used first; the default pattern is all 1's
 * @note The stipple factor is @c 2 for this function
 */
void R_DrawRect (int x, int y, int w, int h, const vec4_t color, float lineWidth, int pattern)
{
	const float nx = x * viddef.rx;
	const float ny = y * viddef.ry;
	const float nw = w * viddef.rx;
	const float nh = h * viddef.ry;

	R_Color(color);

	glDisable(GL_TEXTURE_2D);
	glLineWidth(lineWidth);
	glLineStipple(2, pattern);
	glEnable(GL_LINE_STIPPLE);

	glBegin(GL_LINE_LOOP);
	glVertex2f(nx, ny);
	glVertex2f(nx + nw, ny);
	glVertex2f(nx + nw, ny + nh);
	glVertex2f(nx, ny + nh);
	glEnd();

	glEnable(GL_TEXTURE_2D);
	glLineWidth(1.0f);
	glDisable(GL_LINE_STIPPLE);

	R_Color(NULL);
}

/**
 * @brief Draws a circle out of lines
 * @param[in] mid Center of the circle
 * @param[in] radius Radius of the circle
 * @param[in] color The color of the circle lines
 * @sa R_DrawPtlCircle
 * @sa R_DrawLineStrip
 */
void R_DrawCircle (vec3_t mid, float radius, const vec4_t color, int thickness)
{
	float theta;
	const float accuracy = 5.0;
	const float step = M_PI / radius * accuracy;

	glDisable(GL_TEXTURE_2D);
	glEnable(GL_LINE_SMOOTH);

	R_Color(color);

	assert(radius > thickness);

	/* scale it */
	radius *= viddef.rx;
	thickness *= viddef.rx;

	/* store the matrix - we are using glTranslate */
	glPushMatrix();

	/* translate the position */
	glTranslated(mid[0], mid[1], mid[2]);

	if (thickness <= 1) {
		glBegin(GL_LINE_STRIP);
		for (theta = 0.0; theta <= 2.0 * M_PI; theta += step) {
			glVertex3f(radius * cos(theta), radius * sin(theta), 0.0);
		}
		glEnd();
	} else {
		glBegin(GL_TRIANGLE_STRIP);
		for (theta = 0.0; theta <= 2.0 * M_PI; theta += step) {
			glVertex3f(radius * cos(theta), radius * sin(theta), 0.0);
			glVertex3f(radius * cos(theta - step), radius * sin(theta - step), 0.0);
			glVertex3f((radius - thickness) * cos(theta - step), (radius - thickness) * sin(theta - step), 0.0);
			glVertex3f((radius - thickness) * cos(theta), (radius - thickness) * sin(theta), 0.0);
		}
		glEnd();
	}

	glPopMatrix();

	R_Color(NULL);

	glDisable(GL_LINE_SMOOTH);
	glEnable(GL_TEXTURE_2D);
}

#define CIRCLE_LINE_COUNT	40

/**
 * @brief Draws a circle out of lines
 * @param[in] x X screen coordinate
 * @param[in] y Y screen coordinate
 * @param[in] radius Radius of the circle
 * @param[in] color The color of the circle lines
 * @param[in] fill Fill the circle with the given color
 * @param[in] thickness The thickness of the lines
 * @sa R_DrawPtlCircle
 * @sa R_DrawLineStrip
 */
void R_DrawCircle2D (int x, int y, float radius, qboolean fill, const vec4_t color, float thickness)
{
	int i;

	glPushAttrib(GL_ALL_ATTRIB_BITS);

	glDisable(GL_TEXTURE_2D);
	R_Color(color);

	if (thickness > 0.0)
		glLineWidth(thickness);

	if (fill)
		glBegin(GL_TRIANGLE_STRIP);
	else
		glBegin(GL_LINE_LOOP);

	/* Create a vertex at the exact position specified by the start angle. */
	glVertex2f(x + radius, y);

	for (i = 0; i < CIRCLE_LINE_COUNT; i++) {
		const float angle = (2.0 * M_PI / CIRCLE_LINE_COUNT) * i;
		glVertex2f(x + radius * cos(angle), y - radius * sin(angle));

		/* When filling we're drawing triangles so we need to
		 * create a vertex in the middle of the vertex to fill
		 * the entire pie slice/circle. */
		if (fill)
			glVertex2f(x, y);
	}

	glVertex2f(x + radius, y);
	glEnd();
	glEnable(GL_TEXTURE_2D);
	R_Color(NULL);

	glPopAttrib();
}

#define MAX_LINEVERTS 256

static inline void R_Draw2DArray (int points, int *verts, GLenum mode)
{
	int i;

	/* fit it on screen */
	if (points > MAX_LINEVERTS * 2)
		points = MAX_LINEVERTS * 2;

	/* set vertex array pointer */
	glVertexPointer(2, GL_SHORT, 0, r_state.vertex_array_2d);

	for (i = 0; i < points * 2; i += 2) {
		r_state.vertex_array_2d[i] = verts[i] * viddef.rx;
		r_state.vertex_array_2d[i + 1] = verts[i + 1] * viddef.ry;
	}

	glDisable(GL_TEXTURE_2D);
	glDrawArrays(mode, 0, points);
	glEnable(GL_TEXTURE_2D);
	glVertexPointer(3, GL_FLOAT, 0, r_state.vertex_array_3d);
}

/**
 * @brief 2 dimensional line strip
 * @sa R_DrawCircle
 * @sa R_DrawLineLoop
 */
void R_DrawLineStrip (int points, int *verts)
{
	R_Draw2DArray(points, verts, GL_LINE_STRIP);
}

/**
 * @sa R_DrawCircle
 * @sa R_DrawLineStrip
 */
void R_DrawLineLoop (int points, int *verts)
{
	R_Draw2DArray(points, verts, GL_LINE_LOOP);
}

/**
 * @brief Draws one line with only one start and one end point
 * @sa R_DrawCircle
 * @sa R_DrawLineStrip
 */
void R_DrawLine (int *verts, float thickness)
{
	if (thickness > 0.0)
		glLineWidth(thickness);

	R_Draw2DArray(2, verts, GL_LINES);

	if (thickness > 0.0)
		glLineWidth(1.0);
}

/**
 * @sa R_DrawCircle
 * @sa R_DrawLineStrip
 * @sa R_DrawLineLoop
 */
void R_DrawPolygon (int points, int *verts)
{
	R_Draw2DArray(points, verts, GL_POLYGON);
}

typedef struct {
	int x;
	int y;
	int width;
	int height;
} rect_t;

#define MAX_CLIPRECT 16

static rect_t clipRect[MAX_CLIPRECT];

static int currentClipRect = 0;

/**
 * @brief Compute the intersection of 2 rect
 * @param[in] a A rect
 * @param[in] b A rect
 * @param[out] out The intersection rect
 */
static void R_RectIntersection (const rect_t* a, const rect_t* b, rect_t* out)
{
	out->x = (a->x > b->x) ? a->x : b->x;
	out->y = (a->y > b->y) ? a->y : b->y;
	out->width = ((a->x + a->width < b->x + b->width) ? a->x + a->width : b->x + b->width) - out->x;
	out->height = ((a->y + a->height < b->y + b->height) ? a->y + a->height : b->y + b->height) - out->y;
	if (out->width < 0)
		out->width = 0;
	if (out->height < 0)
		out->height = 0;
}

/**
 * @brief Force to draw only on a rect
 * @note Don't forget to call @c R_EndClipRect
 * @sa R_PopClipRect
 */
void R_PushClipRect (int x, int y, int width, int height)
{
	const int depth = currentClipRect;
	assert(depth < MAX_CLIPRECT);

	if (depth == 0) {
		clipRect[depth].x = x * viddef.rx;
		clipRect[depth].y = (viddef.virtualHeight - (y + height)) * viddef.ry;
		clipRect[depth].width = width * viddef.rx;
		clipRect[depth].height = height * viddef.ry;
	} else {
		rect_t rect;
		rect.x = x * viddef.rx;
		rect.y = (viddef.virtualHeight - (y + height)) * viddef.ry;
		rect.width = width * viddef.rx;
		rect.height = height * viddef.ry;
		R_RectIntersection(&clipRect[depth - 1], &rect, &clipRect[depth]);
	}

	glScissor(clipRect[depth].x, clipRect[depth].y, clipRect[depth].width, clipRect[depth].height);

	if (currentClipRect == 0)
		glEnable(GL_SCISSOR_TEST);
	currentClipRect++;
}

/**
 * @sa R_PushClipRect
 */
void R_PopClipRect (void)
{
	assert(currentClipRect > 0);
	currentClipRect--;
	if (currentClipRect == 0)
		glDisable(GL_SCISSOR_TEST);
	else {
		const int depth = currentClipRect - 1;
		glScissor(clipRect[depth].x, clipRect[depth].y, clipRect[depth].width, clipRect[depth].height);
	}
}

/**
 * @brief "Clean up" the depth buffer into a rect
 * @note we use a big value (but not too big) to set the depth buffer, then it is not really a clean up
 * @todo can we fix bigZ with a value come from glGet?
 */
void R_CleanupDepthBuffer (int x, int y, int width, int height)
{
	const int nx = x * viddef.rx;
	const int ny = y * viddef.ry;
	const int nwidth = width * viddef.rx;
	const int nheight = height * viddef.ry;
	const GLboolean hasDepthTest = glIsEnabled(GL_DEPTH_TEST);
	const GLdouble bigZ = 2000;
	GLint depthFunc;
	glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);

	/* we want to overwrite depth buffer not to have his constraints */
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_ALWAYS);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

	glBegin(GL_QUADS);
	glVertex3d(nx, ny, bigZ);
	glVertex3d(nx + nwidth, ny, bigZ);
	glVertex3d(nx + nwidth, ny + nheight, bigZ);
	glVertex3d(nx, ny + nheight, bigZ);
	glEnd();

	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	if (!hasDepthTest)
		glDisable(GL_DEPTH_TEST);
	glDepthFunc(depthFunc);
}

/**
 * @brief Compute the bounding box for an entity out of the mins, maxs
 * @sa R_DrawBoundingBox
 */
static void R_ComputeBoundingBox (const vec3_t mins, const vec3_t maxs, vec3_t bbox[8])
{
	int i;

	/* compute a full bounding box */
	for (i = 0; i < 8; i++) {
		bbox[i][0] = (i & 1) ? mins[0] : maxs[0];
		bbox[i][1] = (i & 2) ? mins[1] : maxs[1];
		bbox[i][2] = (i & 4) ? mins[2] : maxs[2];
	}
}

/**
 * @brief Draws the model bounding box
 * @sa R_EntityComputeBoundingBox
 */
void R_DrawBoundingBox (const vec3_t mins, const vec3_t maxs)
{
	vec3_t bbox[8];

	R_ComputeBoundingBox(mins, maxs, bbox);

	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	/* Draw top and sides */
	glBegin(GL_TRIANGLE_STRIP);
	glVertex3fv(bbox[2]);
	glVertex3fv(bbox[1]);
	glVertex3fv(bbox[0]);
	glVertex3fv(bbox[1]);
	glVertex3fv(bbox[4]);
	glVertex3fv(bbox[5]);
	glVertex3fv(bbox[1]);
	glVertex3fv(bbox[7]);
	glVertex3fv(bbox[3]);
	glVertex3fv(bbox[2]);
	glVertex3fv(bbox[7]);
	glVertex3fv(bbox[6]);
	glVertex3fv(bbox[2]);
	glVertex3fv(bbox[4]);
	glVertex3fv(bbox[0]);
	glEnd();

	/* Draw bottom */
	glBegin(GL_TRIANGLE_STRIP);
	glVertex3fv(bbox[4]);
	glVertex3fv(bbox[6]);
	glVertex3fv(bbox[7]);
	glEnd();

	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

