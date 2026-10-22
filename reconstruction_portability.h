/* Bounded CRT adapters for the MIT-licensed FidelityFX host sources on Linux. */
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cmath>
#include <algorithm>
#include <cerrno>
#include <locale>
#include <cstdarg>
using std::abs;
#ifndef FFX_UNUSED
#define FFX_UNUSED(x) ((void)(x))
#endif
#define _countof(a) (sizeof(a)/sizeof((a)[0]))
inline int sprintf_s(char *out,size_t size,const char *format,...) {
    va_list args;va_start(args,format);int n=std::vsnprintf(out,size,format,args);va_end(args);
    return n>=0 && size_t(n)<size?n:-1;
}
inline int wcscpy_s(wchar_t *out,size_t size,const wchar_t *in) {
    if(!out || !in || !size)return EINVAL;
    if(std::wcslen(in)>=size){out[0]=0;return ERANGE;}
    std::wcscpy(out,in);return 0;
}
template<size_t N> int wcscpy_s(wchar_t (&out)[N],const wchar_t *in){return wcscpy_s(out,N,in);}
inline int strcpy_s(char *out,size_t size,const char *in) {
    if(!out || !in || !size)return EINVAL;
    if(std::strlen(in)>=size){out[0]=0;return ERANGE;}
    std::strcpy(out,in);return 0;
}
template<size_t N> int strcpy_s(char (&out)[N],const char *in){return strcpy_s(out,N,in);}
inline int memcpy_s(void *out,size_t size,const void *in,size_t bytes) {
    if(!out || (!in && bytes) || bytes>size)return EINVAL;
    if(bytes)std::memcpy(out,in,bytes);return 0;
}
