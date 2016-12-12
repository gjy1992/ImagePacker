// stdafx.h : ��׼ϵͳ�����ļ��İ����ļ���
// ���Ǿ���ʹ�õ��������ĵ�
// �ض�����Ŀ�İ����ļ�
//

#pragma once

#include "targetver.h"

#include <stdio.h>
#include <tchar.h>



// TODO:  �ڴ˴����ó�����Ҫ������ͷ�ļ�
#include "../SDL2/include/SDL.h"
#include "../SDL2/include/SDL_image.h"

#include <iostream>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <functional>

using namespace std;

#include "../Bagel/Engine/Bagel_Include.h"
#include "../Rapidjson/include/rapidjson/rapidjson.h"

#ifdef _MSC_VER
 #ifdef WIN64
  #pragma comment(lib, "../SDL2/lib/x64/SDL2main.lib")
  #pragma comment(lib, "../SDL2/lib/x64/SDL2.lib")
  #pragma comment(lib, "../SDL2/lib/x64/SDL2_image.lib")
 #else
  #pragma comment(lib, "../SDL2/lib/x86/SDL2main.lib")
  #pragma comment(lib, "../SDL2/lib/x86/SDL2.lib")
  #pragma comment(lib, "../SDL2/lib/x86/SDL2_image.lib")
 #endif
#endif