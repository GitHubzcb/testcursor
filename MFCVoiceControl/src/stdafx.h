#pragma once

#ifndef WINVER
#define WINVER 0x0601        // Windows 7+
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS

#include <afxwin.h>
#include <afxext.h>
#include <afxdialogex.h>

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <map>
#include <regex>
#include <sstream>
#include <fstream>
#include <cmath>

#pragma comment(lib, "winmm.lib")

#include <mmsystem.h>
#include <mmreg.h>
