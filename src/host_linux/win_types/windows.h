#pragma once

// obl-sim only (src/host_linux/CMakeLists.txt): the plain-data Windows types and constants that shared sources still
// name in their declarations and game layouts. No Windows function is declared here: a source that calls one fails to
// compile on Linux and gets a portable path or an `#ifdef _WIN32` branch (docs/architecture/linux-host.md).
// On i386 Linux `long` is 32 bits like LONG/DWORD.

#include <stddef.h>
#include <stdint.h>

typedef int BOOL;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned long DWORD;
typedef long LONG;
typedef unsigned long ULONG;
typedef unsigned int UINT;
typedef int INT;
typedef short SHORT;
typedef unsigned short USHORT;
typedef char CHAR;
typedef wchar_t WCHAR;
typedef float FLOAT;
typedef void* LPVOID;
typedef const void* LPCVOID;
typedef void* PVOID;
typedef char* LPSTR;
typedef const char* LPCSTR;
typedef wchar_t* LPWSTR;
typedef const wchar_t* LPCWSTR;
typedef BYTE* LPBYTE;
typedef DWORD* LPDWORD;
typedef long long LONGLONG;
typedef unsigned long long ULONGLONG;
typedef uintptr_t UINT_PTR;
typedef intptr_t INT_PTR;
typedef uintptr_t ULONG_PTR;
typedef intptr_t LONG_PTR;
typedef UINT_PTR WPARAM;
typedef LONG_PTR LPARAM;
typedef LONG_PTR LRESULT;
typedef DWORD COLORREF;
typedef long HRESULT;

#define DECLARE_OBL_HANDLE(name)                                                                                                           \
    struct name##__;                                                                                                                       \
    typedef struct name##__* name
DECLARE_OBL_HANDLE(HWND);
DECLARE_OBL_HANDLE(HINSTANCE);
DECLARE_OBL_HANDLE(HDC);
DECLARE_OBL_HANDLE(HBITMAP);
DECLARE_OBL_HANDLE(HFONT);
DECLARE_OBL_HANDLE(HCURSOR);
DECLARE_OBL_HANDLE(HICON);
DECLARE_OBL_HANDLE(HMENU);
DECLARE_OBL_HANDLE(HBRUSH);
DECLARE_OBL_HANDLE(HGDIOBJ);
DECLARE_OBL_HANDLE(HPALETTE);
DECLARE_OBL_HANDLE(HMONITOR);
#undef DECLARE_OBL_HANDLE
typedef HINSTANCE HMODULE;
typedef void* HANDLE;

#define WINAPI __attribute__((stdcall))
#define CALLBACK __attribute__((stdcall))
#define APIENTRY WINAPI
#define TRUE 1
#define FALSE 0
#define MAX_PATH 260

typedef struct tagRECT {
    LONG left, top, right, bottom;
} RECT, *LPRECT;
typedef const RECT* LPCRECT;
typedef struct tagPOINT {
    LONG x, y;
} POINT, *LPPOINT;
typedef struct tagSIZE {
    LONG cx, cy;
} SIZE, *LPSIZE;

typedef union _LARGE_INTEGER {
    struct {
        DWORD LowPart;
        LONG HighPart;
    };
    LONGLONG QuadPart;
} LARGE_INTEGER;

typedef struct _SYSTEMTIME {
    WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
} SYSTEMTIME;

#define LOWORD(l) ((WORD)(((DWORD)(l)) & 0xffff))
#define HIWORD(l) ((WORD)((((DWORD)(l)) >> 16) & 0xffff))
#define RGB(r, g, b) ((COLORREF)(((BYTE)(r) | ((WORD)((BYTE)(g)) << 8)) | (((DWORD)(BYTE)(b)) << 16)))

typedef struct tagBITMAPINFOHEADER {
    DWORD biSize;
    LONG biWidth, biHeight;
    WORD biPlanes, biBitCount;
    DWORD biCompression, biSizeImage;
    LONG biXPelsPerMeter, biYPelsPerMeter;
    DWORD biClrUsed, biClrImportant;
} BITMAPINFOHEADER;
typedef struct tagRGBQUAD {
    BYTE rgbBlue, rgbGreen, rgbRed, rgbReserved;
} RGBQUAD;
typedef struct tagBITMAPINFO {
    BITMAPINFOHEADER bmiHeader;
    RGBQUAD bmiColors[1];
} BITMAPINFO;
typedef struct tagKERNINGPAIR {
    WORD wFirst, wSecond;
    int iKernAmount;
} KERNINGPAIR;
#define BI_RGB 0L
#define BI_BITFIELDS 3L
#define GDI_ERROR (0xFFFFFFFFL)
