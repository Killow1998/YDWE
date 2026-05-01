// storm_stubs.cpp — Stub Blizzard Storm functions for testing
// The real Storm.dll is only available inside the WorldEdit process.
// These stubs use standard C library equivalents.

#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

extern "C" {

char* __stdcall BLZSStrCopy(char* dst, const char* src, size_t sizeLimit) {
    if (!dst || !src || sizeLimit == 0)
        return dst;
    size_t i = 0;
    while (i < sizeLimit - 1 && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return dst;
}

int __stdcall BLZSStrLen(const char* str) {
    return str ? (int)strlen(str) : 0;
}

int __cdecl BLZSStrPrintf(char* buf, size_t size, const char* fmt, ...) {
    va_list va;
    va_start(va, fmt);
    int n = vsnprintf(buf, size, fmt, va);
    va_end(va);
    return n;
}

int __stdcall BLZSStrCmp(const char* s1, const char* s2, size_t size) {
    if (!s1 && !s2)
        return 0;
    if (!s1)
        return -1;
    if (!s2)
        return 1;
    return strncmp(s1, s2, size);
}

} // extern "C"
