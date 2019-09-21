#ifndef __LIBHTTP_H__
#define __LIBHTTP_H__
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef WIN32
#ifndef _WINSOCK2API_
#include <WinSock2.h>
#else
#include <Windows.h>
#endif
#include <Shlwapi.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "shlwapi.lib")
#else
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <dlfcn.h>
#include <limits.h>
#include <stdarg.h>
#include <dirent.h>
#include <sys/stat.h>
#endif

namespace libhttp {
	
}

#endif //__LIBHTTP_H__
