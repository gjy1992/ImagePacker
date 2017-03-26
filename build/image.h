#ifndef __CC_IMAGE_H__
#define __CC_IMAGE_H__

#include <string>

class Image
{
public:
    Image();
    ~Image();

    typedef enum
    {
        kFmtJpg = 0,
        kFmtPng,
        kFmtWebp,
        kFmtRawData,
        kFmtUnKnown
    }EImageFormat;

    typedef enum
    {
        kAlignCenter        = 0x33, ///< Horizontal center and vertical center.
        kAlignTop           = 0x13, ///< Horizontal center and vertical top.
        kAlignTopRight      = 0x12, ///< Horizontal right and vertical top.
        kAlignRight         = 0x32, ///< Horizontal right and vertical center.
        kAlignBottomRight   = 0x22, ///< Horizontal right and vertical bottom.
        kAlignBottom        = 0x23, ///< Horizontal center and vertical bottom.
        kAlignBottomLeft    = 0x21, ///< Horizontal left and vertical bottom.
        kAlignLeft          = 0x31, ///< Horizontal left and vertical center.
        kAlignTopLeft       = 0x11, ///< Horizontal left and vertical top.
    }ETextAlign;
    
	bool initWithFile(const std::wstring &file);

    bool initWithImageData(unsigned char * pData,
							unsigned int nDataLen,
                           EImageFormat eFmt = kFmtUnKnown,
                           int nWidth = 0,
                           int nHeight = 0);

	void init(int nWidth = 0,
		int nHeight = 0);
	void clear();

	bool Image::saveImageToPNG(const std::wstring & pszFilePath, bool bIsToRGB);

	unsigned int w, h, pitch;
	unsigned char *pixels;

protected:
    bool _initWithJpgData(void * data, int nSize);
    bool _initWithPngData(void * pData, int nDatalen);
    // @warning kFmtRawData only support RGBA8888
	bool _initWithRawData(unsigned char * pData,
		unsigned int nDataLen, int nWidth, int nHeight);

    

private:
    // noncopyable
    Image(const Image&    rImg);
    Image & operator=(const Image&);
};

#define CC_BREAK_IF(__cond__) if((__cond__)) break
#define CC_SAFE_DELETE_ARRAY(__obj__) if(__obj__) delete[] (__obj__)
#define CC_SAFE_FREE(__obj__) if(__obj__) free (__obj__)

// end of platform group
/// @}

	
#endif    // __CC_IMAGE_H__