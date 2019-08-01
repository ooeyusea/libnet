#ifndef __UTIL_H__
#define __UTIL_H__
#include "libnet.h"

#ifdef __cplusplus
extern "C" {
#endif
	void __Libnet_Assert(const char* file, int32_t line, const char* funname, const char* debug);
#ifdef __cplusplus
};
#endif

#define SafeSprintf snprintf

#ifdef _DEBUG
#define LIBNET_ASSERT(p, format, ...) { \
    char debug[4096]; \
    SafeSprintf(debug, sizeof(debug), format, ##__VA_ARGS__); \
    ((p) ? (void)0 : (void)__Libnet_Assert(__FILE__, __LINE__, __FUNCTION__, debug)); \
}
#else
#define LIBNET_ASSERT(p, format, ...)
#endif

#endif