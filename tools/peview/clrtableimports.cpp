/*
 * Process Hacker -
 *   PE viewer
 *
 * Copyright (C) 2021 dmex
 *
 * This file is part of Process Hacker.
 *
 * Process Hacker is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Process Hacker is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Process Hacker.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <peview.h>
#include "metahost.h"
#include <cor.h>

#define TBL_ModuleRef 26UL
#define TBL_ImplMap 28UL

EXTERN_C
PPH_STRING PvClrImportFlagsToString(
    _In_ ULONG Flags
    )
{
    PH_STRING_BUILDER stringBuilder;
    WCHAR pointer[PH_PTR_STR_LEN_1];

    PhInitializeStringBuilder(&stringBuilder, 10);

    if (IsPmNoMangle(Flags))
        PhAppendStringBuilder2(&stringBuilder, const_cast<wchar_t*>(L"No mangle, "));
    if (IsPmCharSetAnsi(Flags))
        PhAppendStringBuilder2(&stringBuilder, const_cast<wchar_t*>(L"Ansi charset, "));
    if (IsPmCharSetUnicode(Flags))
        PhAppendStringBuilder2(&stringBuilder, const_cast<wchar_t*>(L"Unicode charset, "));
    if (IsPmCharSetAuto(Flags))
        PhAppendStringBuilder2(&stringBuilder, const_cast<wchar_t*>(L"Auto charset, "));
    if (IsPmSupportsLastError(Flags))
        PhAppendStringBuilder2(&stringBuilder, const_cast<wchar_t*>(L"Supports last error, "));
    if (IsPmCallConvWinapi(Flags))
        PhAppendStringBuilder2(&stringBuilder, const_cast<wchar_t*>(L"Winapi, "));
    if (IsPmCallConvCdecl(Flags))
        PhAppendStringBuilder2(&stringBuilder, const_cast<wchar_t*>(L"Cdecl, "));
    if (IsPmCallConvStdcall(Flags))
        PhAppendStringBuilder2(&stringBuilder, const_cast<wchar_t*>(L"Stdcall, "));
    if (IsPmCallConvThiscall(Flags))
        PhAppendStringBuilder2(&stringBuilder, const_cast<wchar_t*>(L"Thiscall, "));
    if (IsPmCallConvFastcall(Flags))
        PhAppendStringBuilder2(&stringBuilder, const_cast<wchar_t*>(L"Fastcall, "));
    if (IsPmBestFitEnabled(Flags))
        PhAppendStringBuilder2(&stringBuilder, const_cast<wchar_t*>(L"Bestfit enabled, "));
    if (IsPmBestFitDisabled(Flags))
        PhAppendStringBuilder2(&stringBuilder, const_cast<wchar_t*>(L"Bestfit disabled, "));
    if (IsPmBestFitUseAssem(Flags))
        PhAppendStringBuilder2(&stringBuilder, const_cast<wchar_t*>(L"Bestfit assembly, "));
    if (IsPmThrowOnUnmappableCharEnabled(Flags))
        PhAppendStringBuilder2(&stringBuilder, const_cast<wchar_t*>(L"ThrowOnUnmappableChar enabled, "));
    if (IsPmThrowOnUnmappableCharDisabled(Flags))
        PhAppendStringBuilder2(&stringBuilder, const_cast<wchar_t*>(L"ThrowOnUnmappableChar disabled, "));
    if (IsPmThrowOnUnmappableCharUseAssem(Flags))
        PhAppendStringBuilder2(&stringBuilder, const_cast<wchar_t*>(L"ThrowOnUnmappableChar assembly, "));

    if (PhEndsWithString2(stringBuilder.String, const_cast<wchar_t*>(L", "), FALSE))
        PhRemoveEndStringBuilder(&stringBuilder, 2);

    PhPrintPointer(pointer, UlongToPtr(Flags));
    PhAppendFormatStringBuilder(&stringBuilder, const_cast<wchar_t*>(L" (%s)"), pointer);

    return PhFinalStringBuilderString(&stringBuilder);
}

// TODO: Add support for dynamic imports by enumerating the types. (dmex) 
EXTERN_C HRESULT PvGetClrImageImports(
    _In_ PVOID ClrRuntimInfo,
    _In_ PWSTR FileName,
    _Out_ PPH_LIST* ClrImportsList
    )
{
    HRESULT status;
    ICLRRuntimeInfo* clrRuntimeInfo = reinterpret_cast<ICLRRuntimeInfo*>(ClrRuntimInfo);
    IMetaDataDispenser* metaDataDispenser = nullptr;
    IMetaDataImport* metaDataImport = nullptr;
    IMetaDataTables* metaDataTables = nullptr;
    PPH_LIST clrImportsList;
    ULONG rowModuleCount = 0;
    ULONG rowModuleColumns = 0;
    ULONG rowImportCount = 0;
    ULONG rowImportColumns = 0;

    status = clrRuntimeInfo->GetInterface(
        CLSID_CorMetaDataDispenser,
        IID_IMetaDataDispenser,
        reinterpret_cast<void**>(&metaDataDispenser)
        );

    if (!SUCCEEDED(status))
        return status;

    status = metaDataDispenser->OpenScope(
        FileName,
        ofReadOnly,
        IID_IMetaDataImport,
        reinterpret_cast<IUnknown**>(&metaDataImport)
        );

    if (!SUCCEEDED(status))
    {
        metaDataDispenser->Release();
        return status;
    }

    status = metaDataImport->QueryInterface(
        IID_IMetaDataTables,
        reinterpret_cast<void**>(&metaDataTables)
        );

    if (!SUCCEEDED(status))
    {
        metaDataImport->Release();
        metaDataDispenser->Release();
        return status;
    }

    clrImportsList = PhCreateList(64);

    // dummy unknown entry at index 0
    {
        PPV_CLR_IMAGE_IMPORT_DLL importDll;

        importDll = (PPV_CLR_IMAGE_IMPORT_DLL)PhAllocateZero(sizeof(PV_CLR_IMAGE_IMPORT_DLL));
        importDll->ImportName = PhCreateString(const_cast<wchar_t*>(L"Unknown"));
        importDll->ImportToken = ULONG_MAX;

        PhAddItemList(clrImportsList, importDll);
    }

    if (SUCCEEDED(metaDataTables->GetTableInfo(TBL_ModuleRef, NULL, &rowModuleCount, &rowModuleColumns, NULL, NULL)))
    {
        for (ULONG i = 1; i <= rowModuleCount; i++)
        {
            ULONG moduleNameValue = 0;
            const char* moduleName = nullptr;

            if (SUCCEEDED(metaDataTables->GetColumn(TBL_ModuleRef, 0, i, &moduleNameValue)))
            {
                if (SUCCEEDED(metaDataTables->GetString(moduleNameValue, &moduleName)))
                {
                    PPV_CLR_IMAGE_IMPORT_DLL importDll;

                    importDll = (PPV_CLR_IMAGE_IMPORT_DLL)PhAllocateZero(sizeof(PV_CLR_IMAGE_IMPORT_DLL));
                    importDll->ImportName = PhConvertUtf8ToUtf16(const_cast<char*>(moduleName));
                    importDll->ImportToken = TokenFromRid(i, mdtModuleRef);

                    PhAddItemList(clrImportsList, importDll);
                }
            }
        }
    }

    if (SUCCEEDED(metaDataTables->GetTableInfo(TBL_ImplMap, NULL, &rowImportCount, &rowImportColumns, NULL, NULL)))
    {
        for (ULONG i = 1; i <= rowImportCount; i++)
        {
            bool found = false;
            ULONG importFlagsValue = 0;
            ULONG importNameValue = 0;
            ULONG moduleTokenValue = 0;
            const char* importName = nullptr;

            metaDataTables->GetColumn(TBL_ImplMap, 0, i, &importFlagsValue);

            if (SUCCEEDED(metaDataTables->GetColumn(TBL_ImplMap, 2, i, &importNameValue)))
            {
                metaDataTables->GetString(importNameValue, &importName);
            }

            if (!SUCCEEDED(metaDataTables->GetColumn(TBL_ImplMap, 3, i, &moduleTokenValue)))
            {
                moduleTokenValue = ULONG_MAX;
            }

            for (ULONG i = 0; i < clrImportsList->Count; i++)
            {
                PPV_CLR_IMAGE_IMPORT_DLL importDll = (PPV_CLR_IMAGE_IMPORT_DLL)clrImportsList->Items[i];

                if (importDll->ImportToken == moduleTokenValue)
                {
                    if (!importDll->Functions)
                        importDll->Functions = PhCreateList(1);
                    if (!importName)
                        importName = "Unknown";

                    if (importDll->Functions)
                    {
                        PPV_CLR_IMAGE_IMPORT_FUNCTION importFunction;

                        importFunction = (PPV_CLR_IMAGE_IMPORT_FUNCTION)PhAllocateZero(sizeof(PV_CLR_IMAGE_IMPORT_FUNCTION));
                        importFunction->FunctionName = PhConvertUtf8ToUtf16(const_cast<char*>(importName));
                        importFunction->Flags = importFlagsValue;

                        PhAddItemList(importDll->Functions, importFunction);
                    }

                    found = true;
                    break;
                }
            }

            if (!found)
            {
                PPV_CLR_IMAGE_IMPORT_DLL unknownImportDll = (PPV_CLR_IMAGE_IMPORT_DLL)clrImportsList->Items[0];

                if (!unknownImportDll->Functions)
                    unknownImportDll->Functions = PhCreateList(1);
                if (!importName)
                    importName = "Unknown";

                if (unknownImportDll->Functions)
                {
                    PPV_CLR_IMAGE_IMPORT_FUNCTION importFunction;

                    importFunction = (PPV_CLR_IMAGE_IMPORT_FUNCTION)PhAllocateZero(sizeof(PV_CLR_IMAGE_IMPORT_FUNCTION));
                    importFunction->FunctionName = PhConvertUtf8ToUtf16(const_cast<char*>(importName));
                    importFunction->Flags = importFlagsValue;

                    PhAddItemList(unknownImportDll->Functions, importFunction);
                }
            }
        }
    }

    metaDataTables->Release();
    metaDataImport->Release();
    metaDataDispenser->Release();

    *ClrImportsList = clrImportsList;
    return S_OK;
}
