// ImagePacker.cpp : 定义控制台应用程序的入口点。
//

#include "stdafx.h"

void print_usage(const char *exe)
{
	cout << exe << " -o filename [-d Directory=./] [-sz width=256] [--includesubdir] [--disablerot] [--disablebound] [--enablesplit] [-format format=bagel]" << endl
		<< "For example:" << endl
		<< exe << "-o output -sz 512 --enablesplit -format bagel" << endl << endl
		<< "\t-o means output file (image file and data file), without extension" << endl
		<< "\t-d means input directory" << endl
		<< "\t-sz means only handle picture with area (after bounding) smaller or euqal to this value's square" << endl
		<< "\t--disablerot means don't rotate picture 90 degree when generate packer image" << endl
		<< "\t--disablebound means don't scissor alpha area when packing images" << endl
		<< "\t--enablesplit means we may split the picture into small parts during packing, usually used in long slice image" << endl
		<< "\t-format means the format of output data, can be bagel" << endl;
}

struct
{
	//UTF8
	string dir;
	string output;
	bool subdir;
	int width;
	bool rot90;
	bool bound;
	bool split;
	enum
	{
		FMT_BAGEL,
		FMT_JSON,
		FMT_PLIST
	}format;
}options;

void initOption()
{
	options.dir = "./";
	options.width = 256;
	options.subdir = false;
	options.rot90 = true;
	options.bound = true;
	options.split = false;
	options.format = options.FMT_BAGEL;
}

void readOption(int argc, char ** argv)
{
	(void)argc;
	++argv;
	while (*argv)
	{
		if (!strcmp("-d", *argv))
		{
			++argv;
			if (argv)
			{
				options.dir = *argv;
				if (options.dir.empty())
					options.dir = "./";
				if (options.dir.back() != '/' && options.dir.back() != '\\')
					options.dir.push_back('/');
			}
		}
		else if (!strcmp("-o", *argv))
		{
			++argv;
			if (argv)
				options.output = *argv;
		}
		else if (!strcmp("-sz", *argv))
		{
			++argv;
			if (argv)
				options.width = strtol(*argv, nullptr, 10);
		}
		else if (!strcmp("--includesubdir", *argv))
		{
			options.subdir = true;
		}
		else if (!strcmp("--disablerot", *argv))
		{
			options.rot90 = false;
		}
		else if (!strcmp("--disablebound", *argv))
		{
			options.bound = false;
		}
		else if (!strcmp("--enablesplit", *argv))
		{
			options.split = true;
		}
		else if (!strcmp("-format", *argv))
		{
			++argv;
			if (argv)
			{
				if (!strcmp("bagel", *argv))
				{
					options.format = options.FMT_BAGEL;
				}
				else if (!strcmp("json", *argv))
				{
					options.format = options.FMT_JSON;
				}
				else if (!strcmp("plist", *argv))
				{
					options.format = options.FMT_PLIST;
				}
				else
				{
					cout << "invalid format:" << *argv << endl;
				}
			}
		}
		else
		{
			cout << "invalid param:" << *argv << endl;
		}
		if(argv)
			++argv;
	}
	options.output = options.dir + options.output;
}

unordered_set<string> exts = {
	"bmp",
	"jpg",
	"jpeg",
	"jpe",
	"png",
	"pnm",
	"pcx",
	"tif",
	"tiff",
	"tga",
	"lbm",
	"webp",
	"xcf",
	"xpm",
	"xv",
	"gif",
};

list<string> input_files;

#ifdef WIN32
#define _WINSOCKAPI_
#include <Windows.h>

auto v = GetVersion();
bool nt = ((v & 0xFF) >= 6) && ((v & 0xFF00) >= 100);

void getFiles(const string &parentDir)
{
	WIN32_FIND_DATAW data;
	HANDLE h;
	string fmt = parentDir.empty() ? "*" : (parentDir + "*");
	wstring fmtUni = UniFromUTF8(fmt);
	if (nt)
		h = FindFirstFileEx(fmtUni.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
	else
		h = FindFirstFile(fmtUni.c_str(), &data);
	if (h == INVALID_HANDLE_VALUE)
		return;
	while (FindNextFile(h, &data))
	{
		string fname = UniToUTF8(data.cFileName);
		if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
		{
			int dotpos = fname.find_last_of('.');
			if (dotpos >= 0 && exts.count(fname.substr(dotpos + 1)))
			{
				input_files.emplace_back(parentDir + fname);
			}
		}
		else if (options.subdir && fname != "." && fname != "..")
		{
			string p = parentDir + fname + '/';
			getFiles(p);
		}
	}
	FindClose(h);
}

#else

#include <dirent.h>
void getFiles(const string &parentDir)
{
	DIR *dp;
	struct dirent *dmsg;
	if ((dp = opendir(parentDir.c_str())) != nullptr)
	{
		while ((dmsg = readdir(dp)) != NULL)
		{
			if (!strcmp(dmsg->d_name, ".") || !strcmp(dmsg->d_name, ".."))
			{
				continue;
			}
			if (options.subdir && dmsg->d_type == DT_DIR)
			{
				getFiles(parentDir + dmsg->d_name + '/');
			}
			else if (dmsg->d_type == DT_REG)
			{
				string fname = dmsg->d_name;
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

typedef SDL_Surface Img;

typedef SDL_Rect Rect;

struct ImageInfo
{
	string filename;
	int rawwidth;
	int rawheight;
	Rect bounding;
	vector<Rect> split;
	SDL_Point boundingoffset;

	bool rot90;
	vector<Rect> dstRect;
};

struct
{
	int maxWidth;
	int maxHeight;
	int totalArea;
}stat_info;

unordered_map<Img*, ImageInfo> infomap;

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
	stat_info.maxHeight = 0;
	stat_info.maxWidth = 0;
	stat_info.totalArea = 0;
	for (auto & it : input_files)
	{
		Img *img = nullptr;
		img = IMG_Load(it.c_str());
		if (img)
		{
			ImageInfo info;
			info.filename = it;
			info.rot90 = false;
			info.rawwidth = img->w;
			info.rawheight = img->h;
			info.boundingoffset = { 0,0 };
			if (img->format->format != SDL_PIXELFORMAT_ABGR8888)
			{
				Img *img2 = SDL_ConvertSurfaceFormat(img, SDL_PIXELFORMAT_ABGR8888, 0);
				SDL_FreeSurface(img);
				img = img2;
			}
			if (!img)
			{
				cout << "failed to load " << it << " as RGBA" << endl;
			}
			else
			{
				findBounding(img, info);
				if (info.bounding.w * info.bounding.h > options.width * options.width)
				{
					cout << "Ignore large file ";
				#ifdef WIN32
					wstring tmp = UniFromUTF8(it);
					WriteConsole(GetStdHandle(STD_OUTPUT_HANDLE), tmp.c_str(), tmp.size(), NULL, NULL);
				#else
					cout << it;
				#endif
					cout << "(" << info.bounding.w << "*" << info.bounding.h << ")" << endl;
					SDL_FreeSurface(img);
				}
				else
				{
					if (options.rot90 && info.bounding.h > info.bounding.w)
					{
						info.rot90 = true;
						//save the rotated img
						SDL_Surface *src = SDL_CreateRGBSurface(0, info.bounding.h, info.bounding.w, 32, 0xFF, 0xFF00, 0xFF0000, 0xFF000000);
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
						SDL_FreeSurface(img);
						img = src;
						//info.boundingoffset = { info.bounding.y, info.rawwidth - 1 - info.bounding.x - info.bounding.w };
						info.boundingoffset = { info.bounding.x, info.bounding.y };	//offset at raw image
						swap(info.bounding.w, info.bounding.h);
						info.bounding.x = info.bounding.y = 0;
					}
					infomap[img] = info;
					stat_info.maxWidth = max(stat_info.maxWidth, info.bounding.w);
					stat_info.maxHeight = max(stat_info.maxHeight, info.bounding.h);
					stat_info.totalArea += info.bounding.w * info.bounding.h;
				}
			}
		}
	}
}

void printImageBounding()
{
	cout << endl;
#ifdef WIN32
	auto h = GetStdHandle(STD_OUTPUT_HANDLE);
#endif
	for (auto &info : infomap)
	{
	#ifdef WIN32
		wstring tmp = UniFromUTF8(info.second.filename);
		WriteConsole(h, tmp.c_str(), tmp.size(), NULL, NULL);
		cout << endl;
	#else
		cout << info.second.filename << endl;
	#endif
		cout << "\t[" << info.second.bounding.x << "," << info.second.bounding.y << "," << info.second.bounding.w << "," << info.second.bounding.h << "]" << endl;
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

bool DoWithoutSplit(int w, int h)
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



Img *BlitImages(int w, int h)
{
	SDL_Surface *batch = SDL_CreateRGBSurface(0, w, h, 32, 0xFF, 0xFF00, 0xFF0000, 0xFF000000);
	SDL_FillRect(batch, NULL, SDL_MapRGBA(batch->format, 0, 0, 0, 0));
	for (auto &it : infomap)
	{
		SDL_Surface *src = it.first;
		SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_NONE);
		if (it.second.split.empty())
		{
			SDL_BlitSurface(src, &it.second.bounding, batch, &it.second.dstRect.back());
		}
		else
		{
			for (int i = 0; i < it.second.split.size(); i++)
			{
				SDL_BlitSurface(src, &it.second.split[i], batch, &it.second.dstRect[i]);
			}
		}
	}
	return batch;
}

void saveImageFile(Img *img)
{
	IMG_SavePNG(img, (options.output + ".png").c_str());
	SDL_FreeSurface(img);
}

void saveToBagelFile()
{
	auto v = new Bagel_Array();
	Bagel_StringHolder name = W("name");
	Bagel_StringHolder size = W("size");
	Bagel_StringHolder rects = W("rects");
	Bagel_StringHolder rectsInBatch = W("rectsInBatch");
	Bagel_StringHolder rot90 = W("rot90");
	int dirlen = options.dir.size();
	for (auto &it : infomap)
	{
		auto d = new Bagel_Dic();
		auto &&info = it.second;
		d->setMember(name, UTF16FromUTF8(info.filename.substr(dirlen)));
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
		v->pushMember(d);
	}
	Bagel_Var res = v;
	res.saveToFile(options.output + ".bkpsr", true);
}

void saveToFile(Img *img)
{
	saveImageFile(img);
	switch (options.format)
	{
	case options.FMT_BAGEL:
		saveToBagelFile();
		break;

	}
}

void releaseImgs()
{
	for (auto &it : infomap)
	{
		SDL_FreeSurface(it.first);
	}
}

int main(int argc, char ** argv)
{
	if (argc == 1)
	{
		print_usage(argv[0]);
		return 0;
	}
	SDL_Init(SDL_INIT_VIDEO);
	IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG | IMG_INIT_TIF | IMG_INIT_WEBP);
	initOption();
	readOption(argc, argv);
	getFiles(options.dir);
	loadAllImages();

	//printImageBounding();
	int w, h;
	calcCanvasSize(w, h);

	if (!options.split)
	{
		bool res = DoWithoutSplit(w, h);
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
			res = DoWithoutSplit(w, h);
		}
		if (!res)
		{
			goto fail;
		}
		Img *batch = BlitImages(w, h);
		if (!batch)
			goto fail;
		saveToFile(batch);
		cout << "pack success!" << endl;
		cout << "Pack image size " << w << " * " << h << endl;
		goto end;
	}


fail:
	cout << "fail to pack iamges, maybe too many images to pack" << endl;


end:
	releaseImgs();
	IMG_Quit();
    return 0;
}

