// ImagePacker.cpp : 定义控制台应用程序的入口点。
//

#include "stdafx.h"
#include "picosha2.h"
#include "ThreadPool.h"
#include <atomic>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

void print_usage(const wchar_t *exe)
{
	wcout << exe << " -o filename [-d Directory=./] [-sz width=256] [--includesubdir] [--disablerot] [--disablebound] [--enablesplit] [-format format=bagel] [-ol listfilename]" << endl
		<< "For example:" << endl
		<< exe << "-o output -sz 512 --enablesplit -format bagel" << endl << endl
		<< "\t-o means output file (image file and data file), without extension" << endl
		<< "\t-d means input directory" << endl
		<< "\t-sz means only handle picture with area (after bounding) smaller or euqal to this value's square" << endl
		<< "\t--disablerot means don't rotate picture 90 degree when generate packer image" << endl
		<< "\t--disablebound means don't scissor alpha area when packing images" << endl
		<< "\t--enablesplit means we may split the picture into small parts during packing, usually used in long slice image" << endl
		<< "\t-format means the format of output data, now only can be bke" << endl
		<< "\t-ol means the filename of the output list file, tell you which files are packed" << endl;
}

struct
{
	//UTF8
	wstring dir;
	wstring output;
	wstring outlistfile;
	bool subdir;
	int width;
	bool rot90;
	bool bound;
	bool split;
	enum
	{
		FMT_BKE,
		FMT_JSON,
		FMT_PLIST
	}format;
	bool compact;
}options;

void initOption()
{
	options.dir = L"./";
	options.width = 256;
	options.subdir = false;
	options.rot90 = true;
	options.bound = true;
	options.split = false;
	options.format = options.FMT_BKE;
	options.compact = false;
}

//if file is a relative path, set it to be full path by see it as a file under dir
void mergePath(const wstring &dir, wstring &file)
{
	if (file.empty())
		return;
	//absolute path on windows
	if (file.size() > 2 && file[1] == ':')
		return;
	//linux
	if (file[0] == '/' || file.substr(0,2) == L"~/")
		return;
	file = dir + file;
}

void readOption(int argc, wchar_t ** argv)
{
	(void)argc;
	++argv;
	while (*argv)
	{
		if (!wcscmp(L"-d", *argv))
		{
			++argv;
			if (argv)
			{
				options.dir = *argv;
				if (options.dir.empty())
					options.dir = L"./";
				if (options.dir.back() != '/' && options.dir.back() != '\\')
					options.dir.push_back('/');
			}
		}
		else if (!wcscmp(L"-o", *argv))
		{
			++argv;
			if (argv)
				options.output = *argv;
		}
		else if (!wcscmp(L"-ol", *argv))
		{
			++argv;
			if (argv)
				options.outlistfile = *argv;
		}
		else if (!wcscmp(L"-sz", *argv))
		{
			++argv;
			if (argv)
				options.width = wcstol(*argv, nullptr, 10);
		}
		else if (!wcscmp(L"--includesubdir", *argv))
		{
			options.subdir = true;
		}
		else if (!wcscmp(L"--disablerot", *argv))
		{
			options.rot90 = false;
		}
		else if (!wcscmp(L"--disablebound", *argv))
		{
			options.bound = false;
		}
		else if (!wcscmp(L"--enablesplit", *argv))
		{
			options.split = true;
		}
		else if (!wcscmp(L"-format", *argv))
		{
			++argv;
			if (argv)
			{
				if (!wcscmp(L"bagel", *argv))
				{
					options.format = options.FMT_BKE;
				}
				else if (!wcscmp(L"json", *argv))
				{
					options.format = options.FMT_JSON;
				}
				else if (!wcscmp(L"plist", *argv))
				{
					options.format = options.FMT_PLIST;
				}
				else
				{
					wcout << "invalid format:" << *argv << endl;
				}
			}
		}
		else if (!wcscmp(L"-compact", *argv))
		{
			options.compact = true;
		}
		else
		{
			wcout << "invalid param:" << *argv << endl;
		}
		if(argv)
			++argv;
	}
	mergePath(options.dir, options.output);
	mergePath(options.dir, options.outlistfile);
}

unordered_set<wstring> exts = {
	L"jpg",
	L"jpeg",
	L"png",
};

list<wstring> input_files;

#ifdef WIN32
#define _WINSOCKAPI_
#include <Windows.h>

auto v = GetVersion();
bool nt = ((v & 0xFF) >= 6) && ((v & 0xFF00) >= 100);

void getFiles(const wstring &parentDir)
{
	WIN32_FIND_DATAW data;
	HANDLE h;
	wstring fmt = parentDir.empty() ? L"*" : (parentDir + L"*");
	if (nt)
		h = FindFirstFileEx(fmt.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
	else
		h = FindFirstFile(fmt.c_str(), &data);
	if (h == INVALID_HANDLE_VALUE)
		return;
	while (FindNextFile(h, &data))
	{
		wstring fname = data.cFileName;
		if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
		{
			int dotpos = fname.find_last_of('.');
			if (dotpos >= 0 && exts.count(fname.substr(dotpos + 1)))
			{
				input_files.emplace_back(parentDir + fname);
			}
		}
		else if (options.subdir && fname != L"." && fname != L"..")
		{
			wstring p = parentDir + fname + L'/';
			getFiles(p);
		}
	}
	FindClose(h);
}

#else

#include <dirent.h>
void getFiles(const wstring &parentDir)
{
	DIR *dp;
	struct dirent *dmsg;
	if ((dp = opendir(UniToUTF8(parentDir).c_str())) != nullptr)
	{
		while ((dmsg = readdir(dp)) != NULL)
		{
			if (!strcmp(dmsg->d_name, ".") || !strcmp(dmsg->d_name, ".."))
			{
				continue;
			}
			if (options.subdir && dmsg->d_type == DT_DIR)
			{
				getFiles(parentDir + UniFromUTF8(dmsg->d_name) + L'/');
			}
			else if (dmsg->d_type == DT_REG)
			{
				wstring fname = UniFromUTF8(dmsg->d_name);
				int dotpos = fname.find_last_of('.');
				if (dotpos >= 0 && exts.count(fname.substr(dotpos + 1)))
				{
					input_files.emplace_back(parentDir + data.cFileName);
				}
			}
		}
	}
}

#endif

typedef Image Img;
typedef struct  
{
	int x, y;
} Point;
typedef struct  
{
	int x, y;
	int w, h;
} Rect;

struct ImageInfo
{
	vector<wstring> filenames;
	int rawwidth;
	int rawheight;
	Rect bounding;
	vector<Rect> split;
	Point boundingoffset;

	bool rot90;
	vector<Rect> dstRect;
};

struct
{
	int maxWidth;
	int maxHeight;
	int totalArea;
}stat_info;

struct array32_hash
{
	size_t operator()(const array<unsigned char, 32> &arr) const
	{
		if (sizeof(size_t) == 4)
		{
			size_t a = *(size_t *)&arr[0];
			size_t b = *(size_t *)&arr[4];
			size_t c = *(size_t *)&arr[8];
			size_t d = *(size_t *)&arr[12];
			size_t e = *(size_t *)&arr[16];
			size_t f = *(size_t *)&arr[20];
			size_t g = *(size_t *)&arr[24];
			size_t h = *(size_t *)&arr[28];
			return a ^ b ^ c ^ d ^ e ^ f ^ g ^ h;
		}
		else
		{
			size_t a = *(size_t *)&arr[0];
			size_t b = *(size_t *)&arr[8];
			size_t c = *(size_t *)&arr[16];
			size_t d = *(size_t *)&arr[24];
			return a ^ b ^ c ^ d;
		}
	}
};

struct array32_equal
{
	bool operator()(const array<unsigned char, 32> &l, const array<unsigned char, 32> &r) const
	{
		return memcmp(l.data(), r.data(), 32) == 0;
	}
};

std::mutex infomapmutex;
unordered_map<Img*, ImageInfo> infomap;
unordered_map<array<unsigned char, 32>, Img *, array32_hash, array32_equal> hashedinfomap; // Map with hash to Img

void findBounding(Img *img, ImageInfo& info)
{
	//ABGR8888
	//memory layout:R->G->B->A
	info.bounding = { 0,0,info.rawwidth, info.rawheight };
	if (!options.bound)
	{
		return;
	}
	auto data = (unsigned char*)img->pixels;
	int width = img->w;
	int height = img->h;
	//x axis
	auto scanx = [&](int x1, int x2, int y)
	{
		auto start = &data[y * img->pitch + x1] + 3;
		for (int x = x1; x < x2; x++)
		{
			if (*start > 0)
				return false;
			start += 4;
		}
		return true;
	};
	while (info.bounding.y < info.rawheight)
	{
		if (!scanx(info.bounding.x, info.bounding.x + info.bounding.w, info.bounding.y))
			break;
		++info.bounding.y;
		--info.bounding.h;
	}
	while (info.bounding.h > 0)
	{
		if (!scanx(info.bounding.x, info.bounding.x + info.bounding.w, info.bounding.y + info.bounding.h - 1))
			break;
		--info.bounding.h;
	}
	info.bounding.h = max(info.bounding.h, 2);
	info.bounding.y = min(info.bounding.y, info.rawheight - info.bounding.h);
	//y axis
	auto scany = [&](int y1, int y2, int x)
	{
		auto start = &data[y1 * img->pitch + 4 * x] + 3;
		for (int y = y1; y < y2; y++)
		{
			if (*start > 0)
				return false;
			start += img->pitch;
		}
		return true;
	};
	while (info.bounding.x < info.rawwidth)
	{
		if (!scany(info.bounding.y, info.bounding.y + info.bounding.h, info.bounding.x))
			break;
		++info.bounding.x;
		--info.bounding.w;
	}
	while (info.bounding.w > 0)
	{
		if (!scany(info.bounding.y, info.bounding.y + info.bounding.h, info.bounding.x + info.bounding.w - 1))
			break;
		--info.bounding.w;
	}
	info.bounding.w = max(info.bounding.w, 2);
	info.bounding.x = min(info.bounding.x, info.rawwidth - info.bounding.w);
	////alignment for 32
	//int m1 = info.bounding.w % 32;
	//if (m1)
	//{
	//	int mm1 = 16 - m1 / 2;
	//	if (info.bounding.x < mm1)
	//		mm1 = info.bounding.x;
	//	int mm2 = 32 - m1 - mm1;
	//	info.bounding.x -= mm1;
	//	info.bounding.w += mm2;
	//	//so bounding.w may bigger than rawwidth
	//}
	//m1 = info.bounding.h % 32;
	//if (m1)
	//{
	//	int mm1 = 16 - m1 / 2;
	//	if (info.bounding.y < mm1)
	//		mm1 = info.bounding.y;
	//	int mm2 = 32 - m1 - mm1;
	//	info.bounding.y -= mm1;
	//	info.bounding.h += mm2;
	//	//so bounding.w may bigger than rawwidth
	//}
}

void loadAllImages()
{
	ThreadPool &tp = ThreadPool::getInstance();
	stat_info.maxHeight = 0;
	stat_info.maxWidth = 0;
	stat_info.totalArea = 0;
	atomic<int> size = input_files.size();
	for (auto it : input_files)
	{
		tp.enqueue([&, it]() {
			size--;
			Img *img = new Img();
			if (img->initWithFile(it))
			{
				array<unsigned char, 32> sha;
				if (options.compact)
				{
					using namespace picosha2;
					hash256((unsigned char *)img->pixels, (unsigned char *)img->pixels + (img->w * img->h * 4), sha);
					infomapmutex.lock();
					auto it2 = hashedinfomap.find(sha);
					if (it2 != hashedinfomap.end())
					{
						infomap[it2->second].filenames.push_back(it);
						wcout << "Compact file ";
						wcout << it;
						wcout << "(" << infomap[it2->second].bounding.w << "*" << infomap[it2->second].bounding.h << ")" << endl;
						infomapmutex.unlock();
						delete img;
						return;
					}
					infomapmutex.unlock();
				}
				ImageInfo info;
				info.filenames.push_back(it);
				info.rot90 = false;
				info.rawwidth = img->w;
				info.rawheight = img->h;
				info.boundingoffset = { 0,0 };

				findBounding(img, info);
				if (info.bounding.w * info.bounding.h > options.width * options.width)
				{
					//sync wcout
					infomapmutex.lock();
					wcout << "Ignore large file ";
					wcout << it;
					wcout << "(" << info.bounding.w << "*" << info.bounding.h << ")" << endl;
					infomapmutex.unlock();
					delete img;
				}
				else
				{
					if (options.rot90 && info.bounding.h > info.bounding.w)
					{
						info.rot90 = true;
						//save the rotated img
						Img *src = new Img();
						src->init(info.bounding.h, info.bounding.w);
						//copy rot90
						for (int x = info.bounding.x; x < info.bounding.x + info.bounding.w; x++)
						{
							//rot 90 counterclockwise
							int newy = info.bounding.w - 1 - (x - info.bounding.x);
							for (int y = info.bounding.y; y < info.bounding.y + info.bounding.h; y++)
							{
								uint32_t *srcdata = (uint32_t*)img->pixels + img->w * y + x;
								uint32_t *dstdata = (uint32_t*)src->pixels + src->w * newy + y - info.bounding.y;
								*dstdata = *srcdata;
							}
						}
						delete img;
						img = src;
						//info.boundingoffset = { info.bounding.y, info.rawwidth - 1 - info.bounding.x - info.bounding.w };
						info.boundingoffset = { info.bounding.x, info.bounding.y };	//offset at raw image
						swap(info.bounding.w, info.bounding.h);
						info.bounding.x = info.bounding.y = 0;
					}
					infomapmutex.lock();
					if (options.compact)
					{
						hashedinfomap[sha] = img;
					}
					infomap[img] = info;
					stat_info.maxWidth = max(stat_info.maxWidth, info.bounding.w);
					stat_info.maxHeight = max(stat_info.maxHeight, info.bounding.h);
					stat_info.totalArea += info.bounding.w * info.bounding.h;

					wcout << "Packed: ";
					wcout << it;
					wcout << "(" << info.bounding.w << "*" << info.bounding.h << ")" << endl;
					infomapmutex.unlock();
				}
			}
			else
			{
				delete img;
			}
		});
	}
	while (size != 0)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	}
}

void printImageBounding()
{
	wcout << endl;
	for (auto &info : infomap)
	{
		for (auto && filename : info.second.filenames)
		{
			wcout << filename << endl;
			wcout << "\t[" << info.second.bounding.x << "," << info.second.bounding.y << "," << info.second.bounding.w << "," << info.second.bounding.h << "]" << endl;
		}
	}
}

int nextPOT(double x)
{
	return 1 << (int)ceil(log2(x));
}

void calcCanvasSize(int &w, int &h)
{
	int len = nextPOT(1.1 * sqrt(stat_info.totalArea));
	if (!options.split)
	{
		w = max(len, stat_info.maxWidth);
		w = nextPOT(w);
		h = nextPOT(1.2 * stat_info.totalArea / w);
		h = max(h, stat_info.maxHeight);
		h = nextPOT(h);
	}
	else
	{
		w = len;
		h = nextPOT(1.2 * stat_info.totalArea / w);
	}
	w = max(w, 32);
	h = max(h, 32);
}

void clearRectsInfo()
{
	for (auto &it : infomap)
	{
		it.second.dstRect.clear();
	}
}

int findLeft(map<int, int>& LT, int t)
{
	auto it = LT.upper_bound(t);
	if (it == LT.end())
		return 0;
	return it->second;
}

bool doWithoutSplit(int w, int h)
{
	//find imgs with large h
	vector<ImageInfo*> LHimgs;
	//the rest
	vector<ImageInfo*> NMimgs;
	for (auto &it : infomap)
	{
		if (it.second.bounding.h >= h / 2)
		{
			LHimgs.push_back(&it.second);
		}
		else
		{
			NMimgs.push_back(&it.second);
		}
	}
	bool retryonce = false;
retry:
	sort(LHimgs.begin(), LHimgs.end(), [](ImageInfo* a, ImageInfo* b)
	{
		return a->bounding.h < b->bounding.h || (a->bounding.h == b->bounding.h && a->bounding.w < b->bounding.w);
	});
	sort(NMimgs.begin(), NMimgs.end(), [](ImageInfo* a, ImageInfo* b)
	{
		return a->bounding.h < b->bounding.h || (a->bounding.h == b->bounding.h && a->bounding.w < b->bounding.w);
	});
	map<int, int> LT;
	//put all LHimgs to left
	int left = 0;
	for (auto it = LHimgs.rbegin(); it != LHimgs.rend(); ++it)
	{
		(*it)->dstRect.push_back({ left, 0, (*it)->bounding.w,(*it)->bounding.h });
		LT[(*it)->bounding.h] = left + (*it)->bounding.w;
		left += (*it)->bounding.w;
		//间隔
		left += 2;
	}
	//then we fill rest part
	int linetop = 0;
	int lineheight = 0;
	left = findLeft(LT, linetop);
	for (auto it = NMimgs.begin(); it != NMimgs.end();)
	{
		if (left + (*it)->bounding.w <= w)
		{
			if (linetop + (*it)->bounding.h > h)
			{
				//retry
				if (retryonce)
					return false;
				retryonce = true;
				LT.clear();
				//set all these imgs as LHimgs
				LHimgs.insert(LHimgs.end(), it, NMimgs.end());
				NMimgs.erase(it, NMimgs.end());
				clearRectsInfo();
				goto retry;
			}
			(*it)->dstRect.push_back({ left, linetop, (*it)->bounding.w,(*it)->bounding.h });
			lineheight = max(lineheight, (*it)->bounding.h);
			left += (*it)->bounding.w;
			//间隔
			left += 2;
			++it;
		}
		else if (lineheight > 0)
		{
			linetop += lineheight;
			//间隔
			linetop += 2;
			lineheight = 0;
			left = findLeft(LT, linetop);
			continue;
		}
		else
		{
			//rest width is not enough
			auto it2 = LT.begin();
			while (it2 != LT.end())
			{
				if (it2->second + (*it)->bounding.w <= w)
					break;
				++it2;
			}
			if (it2 == LT.end())
				return false;
			if (w < (*it)->bounding.w || it2->first + (*it)->bounding.h + 2 > h)
				return false;
			linetop = it2->first;
			linetop += 2;
			lineheight = 0;
			left = findLeft(LT, linetop);
			continue;
		}
	}
	return true;
}

#undef min
#undef max
void drawRectAt(unsigned char *dst, uint32_t dstWidth, uint32_t dstHeight, const unsigned char *src, uint32_t srcWidth, uint32_t srcHeight, uint32_t srcRectX, uint32_t srcRectY, uint32_t srcRectWidth, uint32_t srcRectHeight, int32_t x, int32_t y)
{
	if ((int32_t)dstWidth < x || (int32_t)dstHeight < y)
		return;
	int32_t width, height;
	if (x >= 0)
		width = (int32_t)(std::min(srcRectWidth, dstWidth - x));
	else
		width = (int32_t)std::min(srcRectWidth + x, dstWidth);
	if (y >= 0)
		height = (int32_t)(std::min(srcRectHeight, dstHeight - y));
	else
		height = (int32_t)std::min(srcRectHeight + y, dstHeight);
	int32_t srcskip = 4 * (srcWidth - width);
	int32_t dstskip = 4 * (dstWidth - width);
	if (!width || !height)
		return;
	int32_t base, base2;
	base = y > 0 ? y*dstWidth : 0;
	base += x > 0 ? x : 0;
	base *= 4;
	base2 = y < 0 ? -y*srcRectWidth : 0;
	base2 -= x < 0 ? x : 0;
	base2 += srcRectY * srcWidth;
	base2 += srcRectX;
	base2 *= 4;

	while (height--)
	{
		int32_t ww = (width + 3) / 4;
		switch (width & 3)
		{
		case 0:
			do
			{
				*(int32_t*)&dst[base] = *(int32_t*)&src[base2];
				base += 4; base2 += 4;
		case 3:
			*(int32_t*)&dst[base] = *(int32_t*)&src[base2];
			base += 4; base2 += 4;
		case 2:
			*(int32_t*)&dst[base] = *(int32_t*)&src[base2];
			base += 4; base2 += 4;
		case 1:
			*(int32_t*)&dst[base] = *(int32_t*)&src[base2];
			base += 4; base2 += 4;
			} while (--ww > 0);
		}
		base += dstskip;
		base2 += srcskip;
	}
}


Img *blitImages(int w, int h)
{
	wcout << "Generating batch image..." << endl;
	Img *batch = new Img();
	batch->init(w, h);
	batch->clear();
	ThreadPool &tp = ThreadPool::getInstance();
	atomic<int> size = infomap.size();
	for (auto it : infomap)
	{
		tp.enqueue([&, it]() {
			size--;
			Img *src = it.first;
			if (it.second.split.empty())
			{
				drawRectAt(batch->pixels, batch->w, batch->h, src->pixels, src->w, src->h, it.second.bounding.x, it.second.bounding.y, it.second.bounding.w, it.second.bounding.h, it.second.dstRect.back().x, it.second.dstRect.back().y);
			}
			else
			{
				for (int i = 0; i < it.second.split.size(); i++)
				{
					drawRectAt(batch->pixels, batch->w, batch->h, src->pixels, src->w, src->h, it.second.split[i].x, it.second.split[i].y, it.second.split[i].w, it.second.split[i].h, it.second.dstRect[i].x, it.second.dstRect[i].y);
				}
			}
		});
	}
	while (size != 0)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	}
	return batch;
}

void saveImageFile(Img *img)
{
	img->saveImageToPNG(options.output + L".png", false);
	delete img;
}

void saveListFile()
{
	u16string res;
	int dirlen = options.dir.size();
	set<wstring> sorted;
	for (auto &it : infomap)
	{
		for (auto && filename : it.second.filenames)
		{
			sorted.insert(filename.substr(dirlen));
		}
	}
	for (auto &it : sorted)
	{
		res += UniToUTF16(it);
		res += '\n';
	}
	_globalStructures.writeFunc(res, UniToUTF16(options.outlistfile), 0);
}

void saveToBagelFile()
{
	auto v = new Bagel_Array();
	Bagel_StringHolder name = W("name");
	Bagel_StringHolder size = W("size");
	Bagel_StringHolder rects = W("rects");
	Bagel_StringHolder rectsInBatch = W("rectsInBatch");
	Bagel_StringHolder rot90 = W("rot90");
	Bagel_StringHolder link = W("link");
	int dirlen = options.dir.size();
	for (auto &it : infomap)
	{
		wstring filename = it.second.filenames[0];
		auto d = new Bagel_Dic();
		auto &&info = it.second;
		d->setMember(name, UniToUTF16(filename.substr(dirlen)));
		d->setMember(size, { info.rawwidth, info.rawheight });
		d->setMember(rot90, info.rot90);
		auto r = new Bagel_Array();
		if (info.split.empty())
		{
			if (info.rot90)
			{
				r->pushMember({ info.boundingoffset.x, info.boundingoffset.y, info.bounding.h, info.bounding.w });
			}
			else
			{
				r->pushMember({ info.bounding.x, info.bounding.y, info.bounding.w, info.bounding.h });
			}
		}
		else
		{
			for (auto &it2 : info.split)
			{
				if (info.rot90)
				{
					r->pushMember({ info.bounding.h - it2.y - it2.h + info.boundingoffset.x, it2.x + info.boundingoffset.y, it2.h, it2.w });
				}
				else
				{
					r->pushMember({ it2.x, it2.y, it2.w, it2.h });
				}
			}
		}
		d->setMember(rects, r);
		auto rb = new Bagel_Array();
		for (auto &it2 : info.dstRect)
		{
			rb->pushMember({ it2.x, it2.y, it2.w, it2.h });
		}
		d->setMember(rectsInBatch, rb);
		if (it.second.filenames.size() > 1)
		{
			auto dd = new Bagel_Array();
			for (int i = 1; i < it.second.filenames.size(); i++)
			{
				dd->pushMember(UniToUTF16(it.second.filenames[i].substr(dirlen)));
			}
			d->setMember(link, dd);
		}
		v->pushMember(d);
	}
	Bagel_Var res = v;
	res.saveToFile(UniToUTF16(options.output + L".bkpsr"), true);
}

void saveToFile(Img *img)
{
	saveImageFile(img);
	saveListFile();
	switch (options.format)
	{
	case options.FMT_BKE:
		saveToBagelFile();
		break;

	}
}

void releaseImgs()
{
	for (auto &it : infomap)
	{
		delete it.first;
	}
}

int wmain(int argc, wchar_t ** argv)
{
#ifdef WIN32
#ifdef _MSC_VER
	_setmode(_fileno(stdout), _O_U16TEXT);
#endif
#elif defined(LINUX)
	setlocale(LC_CTYPE, "");
	locale::global(locale(""));
	wcout.imbue(locale(""));
#else
	setlocale(LC_CTYPE, "en_US.UTF-8");
	wcout.imbue(locale(locale(), locale("en_US.UTF-8"), locale::ctype));
#endif
	ios::sync_with_stdio(false);
	if (argc == 1)
	{
		print_usage(argv[0]);
		return 0;
	}
	initOption();
	readOption(argc, argv);
	getFiles(options.dir);
	loadAllImages();

	//printImageBounding();
	int w, h;
	calcCanvasSize(w, h);

	if (!options.split)
	{
		bool res = doWithoutSplit(w, h);
		while (!res && (w < 2048 || h < 2048))
		{
			if (w != h)
			{
				w = max(w, h);
				h = max(w, h);
			}
			else
			{
				w *= 2;
			}
			clearRectsInfo();
			res = doWithoutSplit(w, h);
		}
		if (!res)
		{
			goto fail;
		}
		Img *batch = blitImages(w, h);
		if (!batch)
			goto fail;
		saveToFile(batch);
		wcout << "pack success!" << endl;
		wcout << "Pack image size " << w << " * " << h << endl;
		goto end;
	}


fail:
	wcout << "fail to pack iamges, maybe too many images to pack" << endl;


end:
	releaseImgs();
    return 0;
}

