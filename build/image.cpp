#include "image.h"
#include <string>
#include <ctype.h>
#include <stdio.h>
#include <memory>
#include "png.h"
#include "jpeglib.h"
#include "../Bagel/Engine/bkutf8.h"
using namespace std;

typedef struct 
{
    unsigned char* data;
    int size;
    int offset;
}tImageSource;

static void pngReadCallback(png_structp png_ptr, png_bytep data, png_size_t length)
{
    tImageSource* isource = (tImageSource*)png_get_io_ptr(png_ptr);

    if((int)(isource->offset + length) <= isource->size)
    {
        memcpy(data, isource->data+isource->offset, length);
        isource->offset += length;
    }
    else
    {
        png_error(png_ptr, "pngReaderCallback failed");
    }
}

//////////////////////////////////////////////////////////////////////////
// Implement CCImage
//////////////////////////////////////////////////////////////////////////

Image::Image()
: w(0)
, h(0)
, pitch(0)
, pixels(0)
{

}

Image::~Image()
{
    if (pixels)
    {
		delete[] pixels;
    }
}


bool Image::initWithFile(const std::wstring &file)
{
	FILE *f;
#ifdef _WIN32
	f = _wfopen(file.c_str(), L"rb");
#else
	f = fopen(UniToUTF8(file).c_str(), "rb");
#endif
	if (!f)
		return false;
	fseek(f, 0, SEEK_END);
	long s = ftell(f);
	fseek(f, 0, SEEK_SET);
	unique_ptr<unsigned char[]> buffer(new unsigned char[s]);
	fread(buffer.get(), s, 1, f);
	fclose(f);
	return initWithImageData(buffer.get(), s);
}

bool Image::initWithImageData(unsigned char * pData,
								unsigned int nDataLen, 
                                int nWidth/* = 0*/,
                                int nHeight/* = 0*/)
{
    bool bRet = false;
    do 
    {
        // if it is a png file buffer.
        if (nDataLen > 8)
        {
            unsigned char* pHead = (unsigned char*)pData;
            if (   pHead[0] == 0x89
                && pHead[1] == 0x50
                && pHead[2] == 0x4E
                && pHead[3] == 0x47
                && pHead[4] == 0x0D
                && pHead[5] == 0x0A
                && pHead[6] == 0x1A
                && pHead[7] == 0x0A)
            {
                bRet = _initWithPngData(pData, nDataLen);
                break;
            }
        }

        // if it is a jpeg file buffer.
        if (nDataLen > 2)
        {
            unsigned char* pHead = (unsigned char*)pData;
            if (   pHead[0] == 0xff
                && pHead[1] == 0xd8)
            {
                bRet = _initWithJpgData(pData, nDataLen);
                break;
            }
        }
    } while (0);
    return bRet;
}


void Image::init(int nWidth /*= 0*/, int nHeight /*= 0*/)
{
	h = (short)nHeight;
	w = (short)nWidth;
	pitch = w * 4;

	// only RGBA8888 supported
	int nSize = nHeight * nWidth * 4;
	pixels = new unsigned char[nSize];
}


void Image::clear()
{
	memset(pixels, 0, w * h * 4);
}

/*
 * ERROR HANDLING:
 *
 * The JPEG library's standard error handler (jerror.c) is divided into
 * several "methods" which you can override individually.  This lets you
 * adjust the behavior without duplicating a lot of code, which you might
 * have to update with each future release.
 *
 * We override the "error_exit" method so that control is returned to the
 * library's caller when a fatal error occurs, rather than calling exit()
 * as the standard error_exit method does.
 *
 * We use C's setjmp/longjmp facility to return control.  This means that the
 * routine which calls the JPEG library must first execute a setjmp() call to
 * establish the return point.  We want the replacement error_exit to do a
 * longjmp().  But we need to make the setjmp buffer accessible to the
 * error_exit routine.  To do this, we make a private extension of the
 * standard JPEG error handler object.  (If we were using C++, we'd say we
 * were making a subclass of the regular error handler.)
 *
 * Here's the extended error handler struct:
 */

struct my_error_mgr {
  struct jpeg_error_mgr pub;	/* "public" fields */

  jmp_buf setjmp_buffer;	/* for return to caller */
};

typedef struct my_error_mgr * my_error_ptr;

/*
 * Here's the routine that will replace the standard error_exit method:
 */

METHODDEF(void)
my_error_exit (j_common_ptr cinfo)
{
  /* cinfo->err really points to a my_error_mgr struct, so coerce pointer */
  my_error_ptr myerr = (my_error_ptr) cinfo->err;

  /* Always display the message. */
  /* We could postpone this until after returning, if we chose. */
  (*cinfo->err->output_message) (cinfo);

  /* Return control to the setjmp point */
  longjmp(myerr->setjmp_buffer, 1);
}

bool Image::_initWithJpgData(void * data, int nSize)
{
    /* these are standard libjpeg structures for reading(decompression) */
    struct jpeg_decompress_struct cinfo;
    /* We use our private extension JPEG error handler.
	 * Note that this struct must live as long as the main JPEG parameter
	 * struct, to avoid dangling-pointer problems.
	 */
	struct my_error_mgr jerr;
    /* libjpeg data structure for storing one row, that is, scanline of an image */
    JSAMPROW row_pointer[1] = {0};
    unsigned long location = 0;
    unsigned int i = 0;

    bool bRet = false;
    do 
    {
        /* We set up the normal JPEG error routines, then override error_exit. */
		cinfo.err = jpeg_std_error(&jerr.pub);
		jerr.pub.error_exit = my_error_exit;
		/* Establish the setjmp return context for my_error_exit to use. */
		if (setjmp(jerr.setjmp_buffer)) {
			/* If we get here, the JPEG code has signaled an error.
			 * We need to clean up the JPEG object, close the input file, and return.
			 */
			jpeg_destroy_decompress(&cinfo);
			break;
		}

        /* setup decompression process and source, then read JPEG header */
        jpeg_create_decompress( &cinfo );

        jpeg_mem_src( &cinfo, (unsigned char *) data, nSize );

        /* reading the image header which contains image information */
#if (JPEG_LIB_VERSION >= 90)
        // libjpeg 0.9 adds stricter types.
        jpeg_read_header( &cinfo, true );
#else
        jpeg_read_header( &cinfo, true );
#endif

        // we only support RGB or grayscale
        if (cinfo.jpeg_color_space != JCS_EXT_RGBA)
        {
            if (cinfo.jpeg_color_space == JCS_GRAYSCALE || cinfo.jpeg_color_space == JCS_YCbCr)
            {
                cinfo.out_color_space = JCS_EXT_RGBA;
            }
        }
        else
        {
            break;
        }

        /* Start decompression jpeg here */
        jpeg_start_decompress( &cinfo );

        /* init image info */
        w  = (short)(cinfo.output_width);
        h = (short)(cinfo.output_height);
		pitch = w * 4;
        row_pointer[0] = new unsigned char[cinfo.output_width*cinfo.output_components];
		if (!row_pointer[0])
			break;

        pixels = new unsigned char[cinfo.output_width*cinfo.output_height*cinfo.output_components];
		if (!pixels)
			break;

        /* now actually read the jpeg into the raw buffer */
        /* read one scan line at a time */
        while( cinfo.output_scanline < cinfo.output_height )
        {
            jpeg_read_scanlines( &cinfo, row_pointer, 1 );
            for( i=0; i<cinfo.output_width*cinfo.output_components;i++) 
            {
                pixels[location++] = row_pointer[0][i];
            }
        }

		/* When read image file with broken data, jpeg_finish_decompress() may cause error.
		 * Besides, jpeg_destroy_decompress() shall deallocate and release all memory associated
		 * with the decompression object.
		 * So it doesn't need to call jpeg_finish_decompress().
		 */
		//jpeg_finish_decompress( &cinfo );
        jpeg_destroy_decompress( &cinfo );
        /* wrap up decompression, destroy objects, free pointers and close open files */        
        bRet = true;
    } while (0);

	if (row_pointer[0])
		delete[] row_pointer[0];
    return bRet;
}

bool Image::_initWithPngData(void * pData, int nDatalen)
{
// length of bytes to check if it is a valid png file
#define PNGSIGSIZE  8
    bool bRet = false;
    png_byte        header[PNGSIGSIZE]   = {0}; 
    png_structp     png_ptr     =   0;
    png_infop       info_ptr    = 0;

    do 
    {
        // png header len is 8 bytes
		if (nDatalen < PNGSIGSIZE)
			break;

        // check the data is png or not
        memcpy(header, pData, PNGSIGSIZE);
		if (png_sig_cmp(header, 0, PNGSIGSIZE))
			break;

        // init png_struct
        png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, 0, 0, 0);
		if (!png_ptr)
			break;

        // init png_info
        info_ptr = png_create_info_struct(png_ptr);
		if (!info_ptr)
			break;

#if (CC_TARGET_PLATFORM != CC_PLATFORM_BADA && CC_TARGET_PLATFORM != CC_PLATFORM_NACL)
        CC_BREAK_IF(setjmp(png_jmpbuf(png_ptr)));
#endif

        // set the read call back function
        tImageSource imageSource;
        imageSource.data    = (unsigned char*)pData;
        imageSource.size    = nDatalen;
        imageSource.offset  = 0;
        png_set_read_fn(png_ptr, &imageSource, pngReadCallback);

        // read png header info
        
        // read png file info
        png_read_info(png_ptr, info_ptr);
        
        w = png_get_image_width(png_ptr, info_ptr);
		h = png_get_image_height(png_ptr, info_ptr);
		pitch = w * 4;
		int m_nBitsPerComponent = png_get_bit_depth(png_ptr, info_ptr);
        png_uint_32 color_type = png_get_color_type(png_ptr, info_ptr);

        //CCLOG("color type %u", color_type);
        
        // force palette images to be expanded to 24-bit RGB
        // it may include alpha channel
        if (color_type == PNG_COLOR_TYPE_PALETTE)
        {
            png_set_palette_to_rgb(png_ptr);
        }
        // low-bit-depth grayscale images are to be expanded to 8 bits
        if (color_type == PNG_COLOR_TYPE_GRAY && m_nBitsPerComponent < 8)
        {
            png_set_expand_gray_1_2_4_to_8(png_ptr);
        }
        // expand any tRNS chunk data into a full alpha channel
        if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
        {
            png_set_tRNS_to_alpha(png_ptr);
        }  
        // reduce images with 16-bit samples to 8 bits
        if (m_nBitsPerComponent == 16)
        {
            png_set_strip_16(png_ptr);            
        } 
        // expand grayscale images to RGB
        if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        {
            png_set_gray_to_rgb(png_ptr);
        }

        // read png data
        // m_nBitsPerComponent will always be 8
        m_nBitsPerComponent = 8;
        png_uint_32 rowbytes;
        png_bytep* row_pointers = (png_bytep*)malloc( sizeof(png_bytep) * h );
        
        png_read_update_info(png_ptr, info_ptr);
        
        rowbytes = png_get_rowbytes(png_ptr, info_ptr);
        
        pixels = new unsigned char[rowbytes * h];
		if (!pixels)
			break;
        
        for (unsigned short i = 0; i < h; ++i)
        {
            row_pointers[i] = pixels + i*rowbytes;
        }
        png_read_image(png_ptr, row_pointers);
        
        png_read_end(png_ptr, NULL);
        
        png_uint_32 channel = rowbytes/w;

		if (row_pointers)
			free(row_pointers);

        bRet = true;
    } while (0);

    if (png_ptr)
    {
        png_destroy_read_struct(&png_ptr, (info_ptr) ? &info_ptr : 0, 0);
    }
    return bRet;
}


static void bke_write_png_callback(png_structp png_ptr, png_bytep data, png_size_t length)
{
	FILE *f=(FILE *)png_get_io_ptr(png_ptr);
	fwrite(data, 1, length, f);
}

bool Image::saveImageToPNG(const wstring &pszFilePath, bool bIsToRGB)
{
    bool bRet = false;
    do 
    {
		wstring filename;
		size_t offset=pszFilePath.rfind('.');
		filename=pszFilePath.substr(0,offset) + L".png";
        FILE *f = NULL;
        png_structp png_ptr;
        png_infop info_ptr;
        png_colorp palette;
        png_bytep *row_pointers;

#ifdef _WIN32
		f = _wfopen(pszFilePath.c_str(), L"wb");
#else
		f = fopen(UniToUTF8(pszFilePath).c_str(), "wb");
#endif
		if (f == NULL)
			break;

        png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

        if (NULL == png_ptr)
        {
            fclose(f);
            break;
        }

        info_ptr = png_create_info_struct(png_ptr);
        if (info_ptr == NULL)
        {
			fclose(f);
            png_destroy_write_struct(&png_ptr, NULL);
            break;
        }
#if (CC_TARGET_PLATFORM != CC_PLATFORM_BADA && CC_TARGET_PLATFORM != CC_PLATFORM_NACL)
        if (setjmp(png_jmpbuf(png_ptr)))
        {
            fclose(fp);
            png_destroy_write_struct(&png_ptr, &info_ptr);
            break;
        }
#endif
        png_set_write_fn(png_ptr ,f ,bke_write_png_callback ,NULL);

        if (!bIsToRGB)
        {
            png_set_IHDR(png_ptr, info_ptr, w, h, 8, PNG_COLOR_TYPE_RGB_ALPHA,
                PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
        } 
        else
        {
            png_set_IHDR(png_ptr, info_ptr, w, h, 8, PNG_COLOR_TYPE_RGB,
                PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
        }

        palette = (png_colorp)png_malloc(png_ptr, PNG_MAX_PALETTE_LENGTH * sizeof (png_color));
        png_set_PLTE(png_ptr, info_ptr, palette, PNG_MAX_PALETTE_LENGTH);

        png_write_info(png_ptr, info_ptr);

        png_set_packing(png_ptr);

        row_pointers = (png_bytep *)malloc(h * sizeof(png_bytep));
        if(row_pointers == NULL)
        {
			fclose(f);
            png_destroy_write_struct(&png_ptr, &info_ptr);
            break;
        }

		if (bIsToRGB)
		{
			unsigned char *pTempData = new unsigned char[w * h * 3];
			if (pTempData == NULL)
			{
				fclose(f);
				png_destroy_write_struct(&png_ptr, &info_ptr);
				break;
			}

			for (unsigned int i = 0; i < h; ++i)
			{
				for (unsigned int j = 0; j < w; ++j)
				{
					pTempData[(i * w + j) * 3] = pixels[(i * w + j) * 4];
					pTempData[(i * w + j) * 3 + 1] = pixels[(i * w + j) * 4 + 1];
					pTempData[(i * w + j) * 3 + 2] = pixels[(i * w + j) * 4 + 2];
				}
			}

			for (int i = 0; i < (int)h; i++)
			{
				row_pointers[i] = (png_bytep)pTempData + i * w * 3;
			}

			png_write_image(png_ptr, row_pointers);

			free(row_pointers);
			row_pointers = NULL;

			if (pTempData)
				delete[] pTempData;
		}
		else
		{
			for (int i = 0; i < (int)h; i++)
			{
				row_pointers[i] = (png_bytep)pixels + i * w * 4;
			}

			png_write_image(png_ptr, row_pointers);

			free(row_pointers);
			row_pointers = NULL;
		}

        png_write_end(png_ptr, info_ptr);

        png_free(png_ptr, palette);
        palette = NULL;

        png_destroy_write_struct(&png_ptr, &info_ptr);

		fclose(f);

        bRet = true;
    } while (0);
    return bRet;
}