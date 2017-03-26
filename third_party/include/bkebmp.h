#ifndef BKEBMP_H
#define BKEBMP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"{
#endif

#pragma pack(push)

#pragma pack(1)

typedef struct {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
	uint8_t alpha;
} rgba_pixel_t;

typedef struct {
	uint8_t blue;
	uint8_t green;
	uint8_t red;
	uint8_t alpha;
} bgra_pixel_t;

typedef struct
{
	uint8_t red;
	uint8_t green;
	uint8_t blue;
} rgb_pixel_t;

typedef struct
{
	uint8_t blue;
	uint8_t green;
	uint8_t red;
} bgr_pixel_t;

typedef struct {
	uint8_t magic[2];   /* the magic number used to identify the BMP file:
						0x42 0x4D (Hex code points for B and M).
						The following entries are possible:
						BM - Windows 3.1x, 95, NT, ... etc
						BA - OS/2 Bitmap Array
						CI - OS/2 Color Icon
						CP - OS/2 Color Pointer
						IC - OS/2 Icon
						PT - OS/2 Pointer. */
	uint32_t filesz;    /* the size of the BMP file in bytes */
	uint16_t creator1;  /* reserved. */
	uint16_t creator2;  /* reserved. */
	uint32_t offset;    /* the offset, i.e. starting address,
						of the byte where the bitmap data can be found. */
} bmp_header_t;

typedef struct {
	uint32_t header_sz;     /* the size of this header (40 bytes) */
	uint32_t width;         /* the bitmap width in pixels */
	uint32_t height;        /* the bitmap height in pixels */
	uint16_t nplanes;       /* the number of color planes being used.
							Must be set to 1. */
	uint16_t depth;         /* the number of bits per pixel,
							which is the color depth of the image.
							Typical values are 1, 4, 8, 16, 24 and 32. */
	uint32_t compress_type; /* the compression method being used.
							See also bmp_compression_method_t. */
	uint32_t bmp_bytesz;    /* the image size. This is the size of the raw bitmap
							data (see below), and should not be confused
							with the file size. */
	uint32_t hres;          /* the horizontal resolution of the image.
							(pixel per meter) */
	uint32_t vres;          /* the vertical resolution of the image.
							(pixel per meter) */
	uint32_t ncolors;       /* the number of colors in the color palette,
							or 0 to default to 2<sup><i>n</i></sup>. */
	uint32_t nimpcolors;    /* the number of important colors used,
							or 0 when every color is important;
							generally ignored. */
} bmp_info_header_t;

typedef void* bmp_structp_t;

//return 1 if succeed ,0 if failed
typedef int(*bmp_read)(bmp_structp_t p, uint8_t *dst, uint32_t size);

typedef void(*bmp_log)(const char *);

typedef struct {
	bmp_header_t header;
	bmp_info_header_t infoheader;
	bmp_structp_t bmp_ptr;
	bmp_read read_f;
	bmp_log log_f;
} bmp_file_t;

uint32_t bmp_get_pixels_size(bmp_file_t *s);

uint32_t bmp_get_width(bmp_file_t *s);

uint32_t bmp_get_height(bmp_file_t *s);

uint32_t bmp_get_depth(bmp_file_t *s);

bmp_file_t *bmp_create_from_io(bmp_structp_t bmp_ptr, bmp_read r ,bmp_log l);

//return 1 if succeed ,0 if failed
int bmp_convert_to_rgba32_or_rgb24(bmp_file_t *, uint8_t *);

void bmp_free(bmp_file_t *s);


//////////////////////////////////////// Writable Functions /////////////////////////////////////////


bmp_file_t *bmp_create_for_write(uint32_t width, uint32_t height, uint32_t depth, bmp_log l);

//return 1 if succeed ,0 if failed
typedef int(*bmp_write)(bmp_structp_t p, uint8_t *src, uint32_t size);

//return 1 if succeed ,0 if failed
int bmp_write_from_rgba32_or_rgb24(bmp_file_t *, uint8_t *data, bmp_structp_t bmp_ptr, bmp_write w);

uint32_t bmp_get_file_size(bmp_file_t *);


#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif // !BKEBMP_H

