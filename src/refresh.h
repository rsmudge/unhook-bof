#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifdef _DEBUG
#define OUTPUTDBGA(str) OutputDebugStringA(str);
#define OUTPUTDBGW(str) OutputDebugStringW(str);
#else
#define OUTPUTDBGA(str)
#define OUTPUTDBGW(str)
#endif

void RefreshPE(void * out, char* stomp);
HMODULE CustomLoadLibrary(const PWCHAR wszFullDllName, const PWCHAR wszBaseDllName, ULONG_PTR pDllBase);
HMODULE CustomGetModuleHandleW(const PWSTR wszModule);
FARPROC WINAPI CustomGetProcAddressEx(HMODULE hModule, const PCHAR lpProcName, PWSTR wszOriginalModule);
VOID ScanAndFixModule(void * out, PCHAR pKnown, PCHAR pSuspect, PWCHAR wszBaseDllName);
VOID ScanAndFixSection(void * out, PWCHAR wszBaseDllName, PCHAR szSectionName, PCHAR pKnown, PCHAR pSuspect, size_t stLength);
