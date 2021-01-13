#pragma once
#pragma intrinsic(strcmp)

DECLSPEC_IMPORT BOOL     WINAPI   KERNEL32$CloseHandle(HANDLE);
DECLSPEC_IMPORT HANDLE   WINAPI   KERNEL32$CreateFileMappingW(HANDLE, LPSECURITY_ATTRIBUTES, DWORD, DWORD, DWORD, LPCWSTR);
DECLSPEC_IMPORT HANDLE   WINAPI   KERNEL32$CreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);
DECLSPEC_IMPORT HANDLE   WINAPI   KERNEL32$GetModuleHandleW(LPCWSTR);
DECLSPEC_IMPORT HMODULE  WINAPI   KERNEL32$LoadLibraryW(LPCWSTR);
DECLSPEC_IMPORT LPVOID   WINAPI   KERNEL32$MapViewOfFile(HANDLE, DWORD, DWORD, DWORD, SIZE_T);
DECLSPEC_IMPORT void     WINAPI   KERNEL32$OutputDebugStringA(LPCSTR lpOutputString);
DECLSPEC_IMPORT BOOL     WINAPI   KERNEL32$UnmapViewOfFile(LPCVOID);
DECLSPEC_IMPORT LPVOID   WINAPI   KERNEL32$VirtualAlloc(LPVOID lpAddress, SIZE_T dwSize, DWORD flAllocationType, DWORD flProtect);
DECLSPEC_IMPORT BOOL     WINAPI   KERNEL32$VirtualFree(LPVOID, SIZE_T, DWORD);
DECLSPEC_IMPORT BOOL     WINAPI   KERNEL32$VirtualProtect(LPVOID, SIZE_T, DWORD, PDWORD);

DECLSPEC_IMPORT int      __cdecl  MSVCRT$_wcsnicmp(const wchar_t *_Str1,const wchar_t *_Str2,size_t _MaxCount);
DECLSPEC_IMPORT void *   __cdecl  MSVCRT$calloc(size_t _NumOfElements,size_t _SizeOfElements);
DECLSPEC_IMPORT void     __cdecl  MSVCRT$free(void *_Memory);
DECLSPEC_IMPORT errno_t  __cdecl  MSVCRT$mbstowcs_s(size_t *_PtNumOfCharConverted,wchar_t *_DstBuf,size_t _SizeInWords,const char *_SrcBuf,size_t _MaxCount);
DECLSPEC_IMPORT int      __cdecl  MSVCRT$memcmp(const void *_Buf1,const void *_Buf2,size_t _Size);
DECLSPEC_IMPORT size_t   __cdecl  MSVCRT$strnlen(const char *_Str,size_t _MaxCount);
DECLSPEC_IMPORT char *   __cdecl  MSVCRT$strstr(const char *_Str,const char *_SubStr);
DECLSPEC_IMPORT int      __cdecl  MSVCRT$vsprintf_s(char *buffer, size_t numberOfElements, const char *format, ...);

#define _wcsnicmp MSVCRT$_wcsnicmp
#define calloc MSVCRT$calloc
#define free MSVCRT$free
#define mbstowcs_s MSVCRT$mbstowcs_s
#define memcmp MSVCRT$memcmp
#define strnlen MSVCRT$strnlen
#define strstr MSVCRT$strstr
#define vsprintf_s MSVCRT$vsprintf_s

//void dprintf(char * fmt, ...);
#define dprintf //
