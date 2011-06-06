/**
 * @file images.c
 * @brief Image loading and saving functions
 */

/*
 Copyright (C) 2002-2009 Quake2World.
 Copyright (C) 2002-2011 UFO: Alien Invasion.

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

#include "images.h"
#include "shared.h"

/* Workaround for a warning about redeclaring the macro. jpeglib sets this macro
 * and SDL, too. */
#undef HAVE_STDLIB_H

#include <jpeglib.h>
#include <png.h>
#include <zlib.h>

/** image formats, tried in this order */
static char const* const IMAGE_TYPES[] = { "png", "jpg", "tga", NULL };

/** default pixel format to which all images are converted */
static SDL_PixelFormat format = {
	.palette = NULL,  /**< palette */
	.BitsPerPixel = 32,  /**< bits */
	.BytesPerPixel = 4,  /**< bytes */
	.Rloss = 0,  /**< rloss */
	.Gloss = 0,  /**< gloss */
	.Bloss = 0,  /**< bloss */
	.Aloss = 0,  /**< aloss */
	.Rshift = 24,  /**< rshift */
	.Gshift = 16,  /**< gshift */
	.Bshift = 8,  /**< bshift */
	.Ashift = 0,  /**< ashift */
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	.Rmask = 0xff000000,
	.Gmask = 0x00ff0000,
	.Bmask = 0x0000ff00,
	.Amask = 0x000000ff,
#else
	.Rmask = 0x000000ff,
	.Gmask = 0x0000ff00,
	.Bmask = 0x00ff0000,
	.Amask = 0xff000000,
#endif
#if SDL_VERSION_ATLEAST(1,3,0)
	.next = NULL,
	.format = 0,
	.refcount = 0,
#else
	.colorkey = 0,  /**< colorkey */
	.alpha = 1   /**< alpha */
#endif
};

#define TGA_UNMAP_COMP			10

char const* const* Img_GetImageTypes (void)
{
	return IMAGE_TYPES;
}

/**
 * @sa R_LoadTGA
 * @sa R_LoadJPG
 * @sa R_FindImage
 */
void R_WritePNG (qFILE *f, byte *buffer, int width, int height)
{
	int			i;
	png_structp	png_ptr;
	png_infop	info_ptr;
	png_bytep	*row_pointers;

	png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png_ptr) {
		return;
	}

	info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr) {
		png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
		return;
	}

	png_init_io(png_ptr, f->f);

	png_set_IHDR(png_ptr, info_ptr, width, height, 8, PNG_COLOR_TYPE_RGB,
				PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

	png_set_compression_level(png_ptr, Z_DEFAULT_COMPRESSION);
	png_set_compression_mem_level(png_ptr, 9);

	png_write_info(png_ptr, info_ptr);

	row_pointers = (png_bytep *)malloc(height * sizeof(png_bytep));
	for (i = 0; i < height; i++)
		row_pointers[i] = buffer + (height - 1 - i) * 3 * width;

	png_write_image(png_ptr, row_pointers);
	png_write_end(png_ptr, info_ptr);

	png_destroy_write_struct(&png_ptr, &info_ptr);

	free(row_pointers);
}

#define TGA_CHANNELS 3

/**
 * @sa R_LoadTGA
 * @sa R_WriteTGA
 */
void R_WriteCompressedTGA (qFILE *f, const byte *buffer, int width, int height)
{
	byte pixel_data[TGA_CHANNELS];
	byte block_data[TGA_CHANNELS * 128];
	byte rle_packet;
	int compress = 0;
	size_t block_length = 0;
	byte header[18];
	char footer[26];

	int y;
	size_t x;

	OBJZERO(header);
	OBJZERO(footer);

	/* Fill in header */
	/* byte 0: image ID field length */
	/* byte 1: color map type */
	header[2] = TGA_UNMAP_COMP;		/* image type: Truecolor RLE encoded */
	/* byte 3 - 11: palette data */
	/* image width */
	header[12] = width & 255;
	header[13] = (width >> 8) & 255;
	/* image height */
	header[14] = height & 255;
	header[15] = (height >> 8) & 255;
	header[16] = 8 * TGA_CHANNELS;	/* Pixel size 24 (RGB) or 32 (RGBA) */
	header[17] = 0x20;	/* Origin at bottom left */

	/* write header */
	FS_Write(header, sizeof(header), f);

	for (y = height - 1; y >= 0; y--) {
		for (x = 0; x < width; x++) {
			const size_t index = y * width * TGA_CHANNELS + x * TGA_CHANNELS;
			pixel_data[0] = buffer[index + 2];
			pixel_data[1] = buffer[index + 1];
			pixel_data[2] = buffer[index];

			if (block_length == 0) {
				memcpy(block_data, pixel_data, TGA_CHANNELS);
				block_length++;
				compress = 0;
			} else {
				if (!compress) {
					/* uncompressed block and pixel_data differs from the last pixel */
					if (memcmp(&block_data[(block_length - 1) * TGA_CHANNELS], pixel_data, TGA_CHANNELS) != 0) {
						/* append pixel */
						memcpy(&block_data[block_length * TGA_CHANNELS], pixel_data, TGA_CHANNELS);

						block_length++;
					} else {
						/* uncompressed block and pixel data is identical */
						if (block_length > 1) {
							/* write the uncompressed block */
							rle_packet = block_length - 2;
							FS_Write(&rle_packet,1, f);
							FS_Write(block_data, (block_length - 1) * TGA_CHANNELS, f);
							block_length = 1;
						}
						memcpy(block_data, pixel_data, TGA_CHANNELS);
						block_length++;
						compress = 1;
					}
				} else {
					/* compressed block and pixel data is identical */
					if (memcmp(block_data, pixel_data, TGA_CHANNELS) == 0) {
						block_length++;
					} else {
						/* compressed block and pixel data differs */
						if (block_length > 1) {
							/* write the compressed block */
							rle_packet = block_length + 127;
							FS_Write(&rle_packet, 1, f);
							FS_Write(block_data, TGA_CHANNELS, f);
							block_length = 0;
						}
						memcpy(&block_data[block_length * TGA_CHANNELS], pixel_data, TGA_CHANNELS);
						block_length++;
						compress = 0;
					}
				}
			}

			if (block_length == 128) {
				rle_packet = block_length - 1;
				if (!compress) {
					FS_Write(&rle_packet, 1, f);
					FS_Write(block_data, 128 * TGA_CHANNELS, f);
				} else {
					rle_packet += 128;
					FS_Write(&rle_packet, 1, f);
					FS_Write(block_data, TGA_CHANNELS, f);
				}

				block_length = 0;
				compress = 0;
			}
		}
	}

	/* write remaining bytes */
	if (block_length) {
		rle_packet = block_length - 1;
		if (!compress) {
			FS_Write(&rle_packet, 1, f);
			FS_Write(block_data, block_length * TGA_CHANNELS, f);
		} else {
			rle_packet += 128;
			FS_Write(&rle_packet, 1, f);
			FS_Write(block_data, TGA_CHANNELS, f);
		}
	}

	/* write footer (optional, but the specification recommends it) */
	strncpy(&footer[8] , "TRUEVISION-XFILE", 16);
	footer[24] = '.';
	footer[25] = 0;
	FS_Write(footer, sizeof(footer), f);
}

/**
 * @sa R_ScreenShot_f
 * @sa R_LoadJPG
 */
void R_WriteJPG (qFILE *f, byte *buffer, int width, int height, int quality)
{
	int offset, w3;
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	byte *s;

	/* Initialise the jpeg compression object */
	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_compress(&cinfo);
	jpeg_stdio_dest(&cinfo, f->f);

	/* Setup jpeg parameters */
	cinfo.image_width = width;
	cinfo.image_height = height;
	cinfo.in_color_space = JCS_RGB;
	cinfo.input_components = 3;
	cinfo.progressive_mode = TRUE;

	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, quality, TRUE);
	jpeg_start_compress(&cinfo, qtrue);	/* start compression */
	jpeg_write_marker(&cinfo, JPEG_COM, (const byte *) "UFOAI", (uint32_t) 5);

	/* Feed scanline data */
	w3 = cinfo.image_width * 3;
	offset = w3 * cinfo.image_height - w3;
	while (cinfo.next_scanline < cinfo.image_height) {
		s = &buffer[offset - (cinfo.next_scanline * w3)];
		jpeg_write_scanlines(&cinfo, &s, 1);
	}

	/* Finish compression */
	jpeg_finish_compress(&cinfo);
	jpeg_destroy_compress(&cinfo);
}

/**
 * @brief Loads the specified image from the game filesystem and populates
 * the provided SDL_Surface.
 */
static SDL_Surface* Img_LoadTypedImage (char const* name, char const* type)
{
	char path[MAX_QPATH];
	byte *buf;
	int len;
	SDL_RWops *rw;
	SDL_Surface *surf;
	SDL_Surface *s = 0;

	snprintf(path, sizeof(path), "%s.%s", name, type);
	if ((len = FS_LoadFile(path, &buf)) != -1) {
		if ((rw = SDL_RWFromMem(buf, len))) {
			if ((surf = IMG_LoadTyped_RW(rw, 0, (char*)(uintptr_t)type))) {
				s = SDL_ConvertSurface(surf, &format, 0);
				SDL_FreeSurface(surf);
			}
			SDL_FreeRW(rw);
		}
		FS_FreeFile(buf);
	}
	return s;
}

/**
 * @brief Loads the specified image from the game filesystem and populates
 * the provided SDL_Surface.
 *
 * Image formats are tried in the order they appear in TYPES.
 * @note Make sure to free the given @c SDL_Surface after you are done with it.
 */
SDL_Surface* Img_LoadImage (char const* name)
{
	int i;

	i = 0;
	while (IMAGE_TYPES[i]) {
		SDL_Surface* const surf = Img_LoadTypedImage(name, IMAGE_TYPES[i++]);
		if (surf)
			return surf;
	}

	return 0;
}
