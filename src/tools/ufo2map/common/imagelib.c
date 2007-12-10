/**
 * @file imagelib.c
 * @brief Image related code
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

#include "shared.h"
#include "cmdlib.h"
#include "imagelib.h"

/*
============================================================================
TARGA IMAGE
============================================================================
*/

typedef struct _TargaHeader {
	unsigned char 	id_length, colormap_type, image_type;
	unsigned short	colormap_index, colormap_length;
	unsigned char	colormap_size;
	unsigned short	x_origin, y_origin, width, height;
	unsigned char	pixel_size, attributes;
} TargaHeader;

/**
 * @brief
 */
void LoadTGA (const char *name, byte ** pic, int *width, int *height)
{
	int columns, rows, numPixels;
	byte *pixbuf;
	int row, column;
	byte *buf_p;
	byte *buffer;
	int length;
	TargaHeader targa_header;
	byte *targa_rgba;
	byte tmp[2];

	if (*pic != NULL)
		Sys_Error("possible mem leak in LoadTGA\n");

	/* load the file */
	length = TryLoadFile(name, (void **) &buffer);
	if (!buffer)
		return;

	buf_p = buffer;

	targa_header.id_length = *buf_p++;
	targa_header.colormap_type = *buf_p++;
	targa_header.image_type = *buf_p++;

	tmp[0] = buf_p[0];
	tmp[1] = buf_p[1];
	targa_header.colormap_index = LittleShort(*((short *) tmp));
	buf_p += 2;
	tmp[0] = buf_p[0];
	tmp[1] = buf_p[1];
	targa_header.colormap_length = LittleShort(*((short *) tmp));
	buf_p += 2;
	targa_header.colormap_size = *buf_p++;
	targa_header.x_origin = LittleShort(*((short *) buf_p));
	buf_p += 2;
	targa_header.y_origin = LittleShort(*((short *) buf_p));
	buf_p += 2;
	targa_header.width = LittleShort(*((short *) buf_p));
	buf_p += 2;
	targa_header.height = LittleShort(*((short *) buf_p));
	buf_p += 2;
	targa_header.pixel_size = *buf_p++;
	targa_header.attributes = *buf_p++;

	if (targa_header.image_type != 2 && targa_header.image_type != 10)
		Sys_Error("LoadTGA: Only type 2 and 10 targa RGB images supported (%s) (type: %i)\n", name, targa_header.image_type);

	if (targa_header.colormap_type != 0 || (targa_header.pixel_size != 32 && targa_header.pixel_size != 24))
		Sys_Error("LoadTGA: Only 32 or 24 bit images supported (no colormaps) (%s) (pixel_size: %i)\n", name, targa_header.pixel_size);

	columns = targa_header.width;
	rows = targa_header.height;
	numPixels = columns * rows;

	if (width)
		*width = columns;
	if (height)
		*height = rows;

	targa_rgba = malloc(numPixels * 4);
	*pic = targa_rgba;

	if (targa_header.id_length != 0)
		buf_p += targa_header.id_length;	/* skip TARGA image comment */

	if (targa_header.image_type == 2) {	/* Uncompressed, RGB images */
		for (row = rows - 1; row >= 0; row--) {
			pixbuf = targa_rgba + row * columns * 4;
			for (column = 0; column < columns; column++) {
				unsigned char red, green, blue, alphabyte;

				red = green = blue = alphabyte = 0;
				switch (targa_header.pixel_size) {
				case 24:

					blue = *buf_p++;
					green = *buf_p++;
					red = *buf_p++;
					*pixbuf++ = red;
					*pixbuf++ = green;
					*pixbuf++ = blue;
					*pixbuf++ = 255;
					break;
				case 32:
					blue = *buf_p++;
					green = *buf_p++;
					red = *buf_p++;
					alphabyte = *buf_p++;
					*pixbuf++ = red;
					*pixbuf++ = green;
					*pixbuf++ = blue;
					*pixbuf++ = alphabyte;
					break;
				default:
					break;
				}
			}
		}
	} else if (targa_header.image_type == 10) {	/* Runlength encoded RGB images */
		unsigned char red, green, blue, alphabyte, packetHeader, packetSize, j;

		red = green = blue = alphabyte = 0;
		for (row = rows - 1; row >= 0; row--) {
			pixbuf = targa_rgba + row * columns * 4;
			for (column = 0; column < columns;) {
				packetHeader = *buf_p++;
				/* SCHAR_MAX although it's unsigned */
				packetSize = 1 + (packetHeader & SCHAR_MAX);
				if (packetHeader & 0x80) {	/* run-length packet */
					switch (targa_header.pixel_size) {
					case 24:
						blue = *buf_p++;
						green = *buf_p++;
						red = *buf_p++;
						alphabyte = 255;
						break;
					case 32:
						blue = *buf_p++;
						green = *buf_p++;
						red = *buf_p++;
						alphabyte = *buf_p++;
						break;
					default:
						break;
					}

					for (j = 0; j < packetSize; j++) {
						*pixbuf++ = red;
						*pixbuf++ = green;
						*pixbuf++ = blue;
						*pixbuf++ = alphabyte;
						column++;
						if (column == columns) {	/* run spans across rows */
							column = 0;
							if (row > 0)
								row--;
							else
								goto breakOut;
							pixbuf = targa_rgba + row * columns * 4;
						}
					}
				} else {		/* non run-length packet */
					for (j = 0; j < packetSize; j++) {
						switch (targa_header.pixel_size) {
						case 24:
							blue = *buf_p++;
							green = *buf_p++;
							red = *buf_p++;
							*pixbuf++ = red;
							*pixbuf++ = green;
							*pixbuf++ = blue;
							*pixbuf++ = 255;
							break;
						case 32:
							blue = *buf_p++;
							green = *buf_p++;
							red = *buf_p++;
							alphabyte = *buf_p++;
							*pixbuf++ = red;
							*pixbuf++ = green;
							*pixbuf++ = blue;
							*pixbuf++ = alphabyte;
							break;
						default:
							break;
						}
						column++;
						if (column == columns) {	/* pixel packet run spans across rows */
							column = 0;
							if (row > 0)
								row--;
							else
								goto breakOut;
							pixbuf = targa_rgba + row * columns * 4;
						}
					}
				}
			}
		  breakOut:;
		}
	}

	FreeFile(buffer);
}

/**
 * @brief This is robbed from the engine source code, and refactored for
 * use during map compilation.
 */
int TryLoadTGA (const char *path, miptex_t **mt)
{
	byte *pic = NULL;
	int width, height;
	size_t size;
	byte *dest;

	LoadTGA(path, &pic, &width, &height);
	if (pic == NULL)
		return -1;

	size = sizeof(miptex_t) + width * height * 4;
	*mt = (miptex_t *)malloc(size);

	memset(*mt, 0, size);

	dest = (byte*)(*mt) + sizeof(miptex_t);
	memcpy(dest, pic, width * height * 4);  /* stuff RGBA into this opaque space */
	free(pic);

	/* copy relevant header fields to miptex_t */
	(*mt)->width = width;
	(*mt)->height = height;
	(*mt)->offsets[0] = sizeof(miptex_t);

	return 0;
}


/*
=================================================================
JPEG LOADING
By Robert 'Heffo' Heffernan
=================================================================
*/

/**
 * @brief
 */
static void jpg_null (j_decompress_ptr cinfo)
{
}

/**
 * @brief
 */
static boolean jpg_fill_input_buffer (j_decompress_ptr cinfo)
{
	Sys_FPrintf(SYS_VRB, "Premature end of JPEG data\n");
	return 1;
}

/**
 * @brief
 */
static void jpg_skip_input_data (j_decompress_ptr cinfo, long num_bytes)
{
	if (cinfo->src->bytes_in_buffer < (size_t) num_bytes)
		Sys_FPrintf(SYS_VRB, "Premature end of JPEG data\n");

	cinfo->src->next_input_byte += (size_t) num_bytes;
	cinfo->src->bytes_in_buffer -= (size_t) num_bytes;
}

/**
 * @brief
 */
static void jpeg_mem_src (j_decompress_ptr cinfo, byte * mem, int len)
{
	cinfo->src = (struct jpeg_source_mgr *) (*cinfo->mem->alloc_small) ((j_common_ptr) cinfo, JPOOL_PERMANENT, sizeof(struct jpeg_source_mgr));
	cinfo->src->init_source = jpg_null;
	cinfo->src->fill_input_buffer = jpg_fill_input_buffer;
	cinfo->src->skip_input_data = jpg_skip_input_data;
	cinfo->src->resync_to_restart = jpeg_resync_to_restart;
	cinfo->src->term_source = jpg_null;
	cinfo->src->bytes_in_buffer = len;
	cinfo->src->next_input_byte = mem;
}

/**
 * @brief
 * @sa LoadTGA
 * @sa LoadPNG
 * @sa R_FindImage
 */
void LoadJPG (const char *filename, byte ** pic, int *width, int *height)
{
	struct jpeg_decompress_struct cinfo;
	struct jpeg_error_mgr jerr;
	byte *rawdata, *rgbadata, *scanline, *p, *q;
	int rawsize, i;

	if (*pic != NULL)
		Sys_Error("possible mem leak in LoadJPG\n");

	/* Load JPEG file into memory */
	rawsize = TryLoadFile(filename, (void **) &rawdata);

	if (!rawdata)
		return;

	/* Knightmare- check for bad data */
	if (rawdata[6] != 'J' || rawdata[7] != 'F' || rawdata[8] != 'I' || rawdata[9] != 'F') {
		Sys_FPrintf(SYS_VRB, "Bad jpg file %s\n", filename);
		free(rawdata);
		return;
	}

	/* Initialise libJpeg Object */
	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_decompress(&cinfo);

	/* Feed JPEG memory into the libJpeg Object */
	jpeg_mem_src(&cinfo, rawdata, rawsize);

	/* Process JPEG header */
	jpeg_read_header(&cinfo, qtrue);

	/* Start Decompression */
	jpeg_start_decompress(&cinfo);

	/* Check Colour Components */
	if (cinfo.output_components != 3 && cinfo.output_components != 4) {
		Sys_FPrintf(SYS_VRB, "Invalid JPEG colour components\n");
		jpeg_destroy_decompress(&cinfo);
		free(rawdata);
		return;
	}

	/* Allocate Memory for decompressed image */
	rgbadata = malloc(cinfo.output_width * cinfo.output_height * 4);
	if (!rgbadata) {
		Sys_FPrintf(SYS_VRB, "Insufficient RAM for JPEG buffer\n");
		jpeg_destroy_decompress(&cinfo);
		free(rawdata);
		return;
	}

	/* Pass sizes to output */
	*width = cinfo.output_width;
	*height = cinfo.output_height;

	/* Allocate Scanline buffer */
	scanline = malloc(cinfo.output_width * 4);
	if (!scanline) {
		Sys_FPrintf(SYS_VRB, "Insufficient RAM for JPEG scanline buffer\n");
		free(rgbadata);
		jpeg_destroy_decompress(&cinfo);
		free(rawdata);
		return;
	}

	/* Read Scanlines, and expand from RGB to RGBA */
	q = rgbadata;
	while (cinfo.output_scanline < cinfo.output_height) {
		p = scanline;
		jpeg_read_scanlines(&cinfo, &scanline, 1);

		for (i = 0; i < cinfo.output_width; i++) {
			q[0] = p[0];
			q[1] = p[1];
			q[2] = p[2];
			q[3] = 255;

			p += 3;
			q += 4;
		}
	}

	/* Free the scanline buffer */
	free(scanline);

	/* Finish Decompression */
	jpeg_finish_decompress(&cinfo);

	/* Destroy JPEG object */
	jpeg_destroy_decompress(&cinfo);

	/* Return the 'rgbadata' */
	*pic = rgbadata;
	free(rawdata);
}

/**
 * @brief This is robbed from the engine source code, and refactored for
 * use during map compilation.
 */
int TryLoadJPG (const char *path, miptex_t **mt)
{
	byte *pic = NULL;
	int width, height;
	size_t size;
	byte *dest;

	LoadJPG(path, &pic, &width, &height);
	if (pic == NULL)
		return -1;

	size = sizeof(miptex_t) + width * height * 4;
	*mt = (miptex_t *)malloc(size);

	memset(*mt, 0, size);

	dest = (byte*)(*mt) + sizeof(miptex_t);
	memcpy(dest, pic, width * height * 4);  /* stuff RGBA into this opaque space */
	free(pic);

	/* copy relevant header fields to miptex_t */
	(*mt)->width = width;
	(*mt)->height = height;
	(*mt)->offsets[0] = sizeof(miptex_t);

	return 0;
}
