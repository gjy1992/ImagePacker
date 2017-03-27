#pragma once

#include <string>

class Image
{
public:
    Image();
    ~Image();
    
	bool initWithFile(const std::wstring &file);

    bool initWithImageData(unsigned char * pData,
							unsigned int nDataLen,
                           int nWidth = 0,
                           int nHeight = 0);

	void init(int nWidth = 0,
		int nHeight = 0);
	void clear();

	bool saveImageToPNG(const std::wstring & pszFilePath, bool bIsToRGB);

	unsigned int w, h, pitch;
	unsigned char *pixels;

protected:
    bool _initWithJpgData(void * data, int nSize);
    bool _initWithPngData(void * pData, int nDatalen);

private:
    // noncopyable
    Image(const Image& rImg) = delete;
};

