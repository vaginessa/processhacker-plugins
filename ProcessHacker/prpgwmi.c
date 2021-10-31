/*
 * Process Hacker -
 *   Process properties: WMI Provider page
 *
 * Copyright (C) 2017-2021 dmex
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

#include <phapp.h>
#include <phplug.h>
#include <phsettings.h>
#include <procprp.h>
#include <procprv.h>
#include <settings.h>
#include <emenu.h>

#include <wbemidl.h>

#define WM_PH_WMI_UPDATE (WM_APP + 251)

typedef struct _PH_WMI_CONTEXT
{
    PH_CALLBACK_REGISTRATION ProcessesUpdatedRegistration;
    HWND WindowHandle;
    HWND ListViewHandle;
    BOOLEAN Enabled;
    PPH_LIST WmiProviderList;
} PH_WMI_CONTEXT, *PPH_WMI_CONTEXT;

typedef struct _PH_WMI_ENTRY
{
    PPH_STRING ProviderName;
    PPH_STRING NamespacePath;
    PPH_STRING FileName;
    PPH_STRING UserName;
} PH_WMI_ENTRY, *PPH_WMI_ENTRY;

PVOID PhpGetWmiProviderDllBase(
    VOID
    )
{
    static PH_INITONCE initOnce = PH_INITONCE_INIT;
    static PVOID imageBaseAddress = NULL;

    if (PhBeginInitOnce(&initOnce))
    {
        PPH_STRING systemDirectory;
        PPH_STRING systemFileName;

        if (systemDirectory = PhGetSystemDirectory())
        {
            if (systemFileName = PhConcatStringRefZ(&systemDirectory->sr, L"\\wbem\\wbemprox.dll"))
            {
                if (!(imageBaseAddress = PhGetLoaderEntryFullDllBase(PhGetString(systemFileName))))
                    imageBaseAddress = PhLoadLibrary(PhGetString(systemFileName));

                PhDereferenceObject(systemFileName);
            }

            PhDereferenceObject(systemDirectory);
        }

        PhEndInitOnce(&initOnce);
    }

    return imageBaseAddress;
}

HRESULT PhpWmiProviderExecMethod(
    _In_ PWSTR Method,
    _In_ PWSTR ProcessIdString,
    _In_ PPH_WMI_ENTRY Entry
    )
{
    HRESULT status;
    PVOID imageBaseAddress;
    PPH_STRING querySelectString = NULL;
    BSTR wbemResourceString = NULL;
    BSTR wbemLanguageString = NULL;
    BSTR wbemQueryString = NULL;
    IWbemLocator* wbemLocator = NULL;
    IWbemServices* wbemServices = NULL;
    IEnumWbemClassObject* wbemEnumerator = NULL;
    IWbemClassObject* wbemClassObject;

    if (!(imageBaseAddress = PhpGetWmiProviderDllBase()))
        return ERROR_MOD_NOT_FOUND;

    status = PhGetClassObjectDllBase(
        imageBaseAddress,
        &CLSID_WbemLocator,
        &IID_IWbemLocator,
        &wbemLocator
        );

    if (FAILED(status))
        goto CleanupExit;

    wbemResourceString = SysAllocString(L"Root\\CIMV2");
    status = IWbemLocator_ConnectServer(
        wbemLocator,
        wbemResourceString,
        NULL,
        NULL,
        NULL,
        WBEM_FLAG_CONNECT_USE_MAX_WAIT,
        NULL,
        NULL,
        &wbemServices
        );

    if (FAILED(status))
        goto CleanupExit;

    querySelectString = PhConcatStrings2(
        L"SELECT Namespace,Provider,User,__PATH FROM Msft_Providers WHERE HostProcessIdentifier = ",
        ProcessIdString
        );
    wbemLanguageString = SysAllocString(L"WQL");
    wbemQueryString = SysAllocString(PhGetString(querySelectString));

    if (FAILED(status = IWbemServices_ExecQuery(
        wbemServices,
        wbemLanguageString,
        wbemQueryString,
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        NULL,
        &wbemEnumerator
        )))
    {
        goto CleanupExit;
    }

    while (TRUE)
    {
        PPH_STRING namespacePath = NULL;
        PPH_STRING providerName = NULL;
        PPH_STRING userName = NULL;
        PPH_STRING instancePath = NULL;
        ULONG count = 0;
        VARIANT variant;

        if (FAILED(IEnumWbemClassObject_Next(wbemEnumerator, WBEM_INFINITE, 1, &wbemClassObject, &count)))
            break;

        if (count == 0)
            break;

        VariantInit(&variant);

        if (SUCCEEDED(IWbemClassObject_Get(wbemClassObject, L"Namespace", 0, &variant, 0, 0)))
        {
            namespacePath = PhCreateString(V_BSTR(&variant));
            VariantClear(&variant);
        }

        VariantInit(&variant);

        if (SUCCEEDED(IWbemClassObject_Get(wbemClassObject, L"Provider", 0, &variant, 0, 0)))
        {
            providerName = PhCreateString(V_BSTR(&variant));
            VariantClear(&variant);
        }

        VariantInit(&variant);

        if (SUCCEEDED(IWbemClassObject_Get(wbemClassObject, L"User", 0, &variant, 0, 0)))
        {
            userName = PhCreateString(V_BSTR(&variant));
            VariantClear(&variant);
        }

        VariantInit(&variant);

        if (SUCCEEDED(IWbemClassObject_Get(wbemClassObject, L"__PATH", 0, &variant, 0, 0)))
        {
            instancePath = PhCreateString(V_BSTR(&variant));
            VariantClear(&variant);
        }

        if (namespacePath && providerName && userName && instancePath)
        {
            if (
                PhEqualString(Entry->NamespacePath, namespacePath, FALSE) &&
                PhEqualString(Entry->ProviderName, providerName, FALSE) &&
                PhEqualString(Entry->UserName, userName, FALSE)
                )
            {
                BSTR wbemPathString = SysAllocString(PhGetString(instancePath));
                BSTR wbemMethodString = SysAllocString(Method);

                status = IWbemServices_ExecMethod(
                    wbemServices,
                    wbemPathString,
                    wbemMethodString,
                    0,
                    NULL,
                    wbemClassObject,
                    NULL,
                    NULL
                    );

                SysFreeString(wbemMethodString);
                SysFreeString(wbemPathString);
            }
        }

        if (instancePath)
            PhDereferenceObject(instancePath);
        if (userName)
            PhDereferenceObject(userName);
        if (providerName)
            PhDereferenceObject(providerName);
        if (namespacePath)
            PhDereferenceObject(namespacePath);

        IWbemClassObject_Release(wbemClassObject);
    }

CleanupExit:
    if (wbemQueryString)
        SysFreeString(wbemQueryString);
    if (wbemLanguageString)
        SysFreeString(wbemLanguageString);
    if (wbemResourceString)
        SysFreeString(wbemResourceString);
    if (querySelectString)
        PhDereferenceObject(querySelectString);
    if (wbemEnumerator)
        IEnumWbemClassObject_Release(wbemEnumerator);
    if (wbemServices)
        IWbemServices_Release(wbemServices);
    if (wbemLocator)
        IWbemLocator_Release(wbemLocator);

    return status;
}

HRESULT PhpQueryWmiProviderFileName(
    _In_ PPH_STRING ProviderNameSpace,
    _In_ PPH_STRING ProviderName,
    _Out_ PPH_STRING *FileName
    )
{
    HRESULT status;
    PVOID imageBaseAddress;
    PPH_STRING fileName = NULL;
    PPH_STRING clsidString = NULL;
    PPH_STRING querySelectString = NULL;
    BSTR wbemResourceString = NULL;
    BSTR wbemLanguageString = NULL;
    BSTR wbemQueryString = NULL;
    IWbemLocator* wbemLocator = NULL;
    IWbemServices* wbemServices = NULL;
    IEnumWbemClassObject* wbemEnumerator = NULL;
    IWbemClassObject *wbemClassObject = NULL;
    ULONG count = 0;

    if (!(imageBaseAddress = PhpGetWmiProviderDllBase()))
        return ERROR_MOD_NOT_FOUND;

    status = PhGetClassObjectDllBase(
        imageBaseAddress,
        &CLSID_WbemLocator,
        &IID_IWbemLocator,
        &wbemLocator
        );

    if (FAILED(status))
        goto CleanupExit;

    wbemResourceString = SysAllocString(PhGetString(ProviderNameSpace));
    status = IWbemLocator_ConnectServer(
        wbemLocator,
        wbemResourceString,
        NULL,
        NULL,
        NULL,
        WBEM_FLAG_CONNECT_USE_MAX_WAIT,
        NULL,
        NULL,
        &wbemServices
        );

    if (FAILED(status))
        goto CleanupExit;

    querySelectString = PhFormatString(
        L"SELECT clsid FROM __Win32Provider WHERE Name = '%s'",
        PhGetString(ProviderName)
        );
    wbemLanguageString = SysAllocString(L"WQL");
    wbemQueryString = SysAllocString(PhGetString(querySelectString));

    if (FAILED(status = IWbemServices_ExecQuery(
        wbemServices,
        wbemLanguageString,
        wbemQueryString,
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        NULL,
        &wbemEnumerator
        )))
    {
        goto CleanupExit;
    }

    if (SUCCEEDED(status = IEnumWbemClassObject_Next(
        wbemEnumerator,
        WBEM_INFINITE, 
        1, 
        &wbemClassObject, 
        &count
        )))
    {
        VARIANT variant;

        VariantInit(&variant);

        if (SUCCEEDED(IWbemClassObject_Get(wbemClassObject, L"CLSID", 0, &variant, 0, 0)))
        {
            if (V_BSTR(&variant)) // returns NULL for some host processes (dmex)
            {
                clsidString = PhCreateString(V_BSTR(&variant));
            }

            VariantClear(&variant);
        }

        IWbemClassObject_Release(wbemClassObject);
    }

    // Lookup the GUID in the registry to determine the name and file name.

    if (clsidString)
    {
        HANDLE keyHandle;
        PPH_STRING keyPath;

        keyPath = PhConcatStrings(
            4,
            L"CLSID\\",
            PhGetString(clsidString),
            L"\\",
            L"InprocServer32"
            );

        if (SUCCEEDED(status = HRESULT_FROM_NT(PhOpenKey(
            &keyHandle,
            KEY_READ,
            PH_KEY_CLASSES_ROOT,
            &keyPath->sr,
            0
            ))))
        {
            if (fileName = PhQueryRegistryString(keyHandle, NULL))
            {
                PPH_STRING expandedString;

                if (expandedString = PhExpandEnvironmentStrings(&fileName->sr))
                {
                    PhMoveReference(&fileName, expandedString);
                }
            }

            NtClose(keyHandle);
        }

        PhDereferenceObject(keyPath);
    }

CleanupExit:
    if (wbemQueryString)
        SysFreeString(wbemQueryString);
    if (wbemLanguageString)
        SysFreeString(wbemLanguageString);
    if (wbemResourceString)
        SysFreeString(wbemResourceString);
    if (clsidString)
        PhDereferenceObject(clsidString);
    if (querySelectString)
        PhDereferenceObject(querySelectString);
    if (wbemEnumerator)
        IEnumWbemClassObject_Release(wbemEnumerator);
    if (wbemServices)
        IWbemServices_Release(wbemServices);
    if (wbemLocator)
        IWbemLocator_Release(wbemLocator);

    if (SUCCEEDED(status))
    {
        *FileName = fileName;
    }

    return status;
}

PPH_LIST PhpQueryWmiProviderHostProcess(
    _In_ PPH_PROCESS_ITEM ProcessItem
    )
{
    HRESULT status;
    PVOID imageBaseAddress;
    PPH_LIST providerList = NULL;
    PPH_STRING querySelectString = NULL;
    BSTR wbemResourceString = NULL;
    BSTR wbemLanguageString = NULL;
    BSTR wbemQueryString = NULL;
    IWbemLocator* wbemLocator = NULL;
    IWbemServices* wbemServices = NULL;
    IEnumWbemClassObject* wbemEnumerator = NULL;
    IWbemClassObject *wbemClassObject;

    if (!(imageBaseAddress = PhpGetWmiProviderDllBase()))
        return NULL;

    status = PhGetClassObjectDllBase(
        imageBaseAddress,
        &CLSID_WbemLocator,
        &IID_IWbemLocator,
        &wbemLocator
        );

    if (FAILED(status))
        goto CleanupExit;

    wbemResourceString = SysAllocString(L"Root\\CIMV2");
    status = IWbemLocator_ConnectServer(
        wbemLocator,
        wbemResourceString,
        NULL,
        NULL,
        NULL,
        WBEM_FLAG_CONNECT_USE_MAX_WAIT,
        NULL,
        NULL,
        &wbemServices
        );

    if (FAILED(status))
        goto CleanupExit;

    querySelectString = PhConcatStrings2(
        L"SELECT Namespace,Provider,User FROM Msft_Providers WHERE HostProcessIdentifier = ",
        ProcessItem->ProcessIdString
        );
    wbemLanguageString = SysAllocString(L"WQL");
    wbemQueryString = SysAllocString(PhGetString(querySelectString));

    if (FAILED(status = IWbemServices_ExecQuery(
        wbemServices,
        wbemLanguageString,
        wbemQueryString,
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        NULL,
        &wbemEnumerator
        )))
    {
        goto CleanupExit;
    }

    providerList = PhCreateList(1);

    while (TRUE)
    {
        ULONG count = 0;
        VARIANT variant;
        PPH_WMI_ENTRY entry;

        if (FAILED(IEnumWbemClassObject_Next(wbemEnumerator, WBEM_INFINITE, 1, &wbemClassObject, &count)))
            break;

        if (count == 0)
            break;

        entry = PhAllocate(sizeof(PH_WMI_ENTRY));
        memset(entry, 0, sizeof(PH_WMI_ENTRY));

        VariantInit(&variant);

        if (SUCCEEDED(IWbemClassObject_Get(wbemClassObject, L"Namespace", 0, &variant, 0, 0)))
        {
            entry->NamespacePath = PhCreateString(V_BSTR(&variant));
            VariantClear(&variant);
        }

        VariantInit(&variant);

        if (SUCCEEDED(IWbemClassObject_Get(wbemClassObject, L"Provider", 0, &variant, 0, 0)))
        {
            entry->ProviderName = PhCreateString(V_BSTR(&variant));
            VariantClear(&variant);
        }

        VariantInit(&variant);

        if (SUCCEEDED(IWbemClassObject_Get(wbemClassObject, L"User", 0, &variant, 0, 0)))
        {
            entry->UserName = PhCreateString(V_BSTR(&variant));
            VariantClear(&variant);
        }

        IWbemClassObject_Release(wbemClassObject);

        if (entry->NamespacePath && entry->ProviderName)
        {
            PPH_STRING fileName = NULL;

            if (SUCCEEDED(PhpQueryWmiProviderFileName(entry->NamespacePath, entry->ProviderName, &fileName)))
            {
                entry->FileName = fileName;
            }
        }

        PhAddItemList(providerList, entry);
    }

CleanupExit:
    if (wbemQueryString)
        SysFreeString(wbemQueryString);
    if (wbemLanguageString)
        SysFreeString(wbemLanguageString);
    if (wbemResourceString)
        SysFreeString(wbemResourceString);
    if (querySelectString)
        PhDereferenceObject(querySelectString);
    if (wbemEnumerator)
        IEnumWbemClassObject_Release(wbemEnumerator);
    if (wbemServices)
        IWbemServices_Release(wbemServices);
    if (wbemLocator)
        IWbemLocator_Release(wbemLocator);

    return providerList;
}

//VOID PhQueryWmiHostProcessString(
//    _In_ PPH_PROCESS_ITEM ProcessItem,
//    _Inout_ PPH_STRING_BUILDER Providers
//    )
//{
//    PPH_LIST providerList;
//
//    if (providerList = PhpQueryWmiProviderHostProcess(ProcessItem))
//    {
//        for (ULONG i = 0; i < providerList->Count; i++)
//        {
//            PPH_WMI_ENTRY entry = providerList->Items[i];
//
//            PhAppendFormatStringBuilder(
//                Providers,
//                L"    %s (%s)\n", 
//                PhGetStringOrEmpty(entry->ProviderName),
//                PhGetStringOrEmpty(entry->FileName)
//                );
//
//            if (entry->NamespacePath)
//                PhDereferenceObject(entry->NamespacePath);
//            if (entry->ProviderName)
//                PhDereferenceObject(entry->ProviderName);
//            if (entry->FileName)
//                PhDereferenceObject(entry->FileName);
//            if (entry->UserName)
//                PhDereferenceObject(entry->UserName);
//
//            PhFree(entry);
//        }
//
//        PhDereferenceObject(providerList);
//    }
//}

static VOID NTAPI PhpWmiProviderUpdateHandler(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    PPH_WMI_CONTEXT context = (PPH_WMI_CONTEXT)Context;

    if (context && context->Enabled)
    {
        PostMessage(context->WindowHandle, WM_PH_WMI_UPDATE, 0, 0);
    }
}

VOID PhpClearWmiProviderItems(
    _Inout_ PPH_WMI_CONTEXT Context
    )
{
    ULONG i;
    PPH_WMI_ENTRY entry;

    for (i = 0; i < Context->WmiProviderList->Count; i++)
    {
        entry = Context->WmiProviderList->Items[i];

        if (entry->NamespacePath)
            PhDereferenceObject(entry->NamespacePath);
        if (entry->ProviderName)
            PhDereferenceObject(entry->ProviderName);
        if (entry->FileName)
            PhDereferenceObject(entry->FileName);
        if (entry->UserName)
            PhDereferenceObject(entry->UserName);

        PhFree(entry);
    }

    PhClearList(Context->WmiProviderList);
}

VOID PhpRefreshWmiProviders(
    _In_ HWND hwndDlg,
    _Inout_ PPH_WMI_CONTEXT Context,
    _In_ PPH_PROCESS_ITEM ProcessItem
    )
{
    PVOID selectedIndex;
    PPH_LIST providerList;

    ExtendedListView_SetRedraw(Context->ListViewHandle, FALSE);

    selectedIndex = PhGetSelectedListViewItemParam(Context->ListViewHandle);
    ListView_DeleteAllItems(Context->ListViewHandle);
    PhpClearWmiProviderItems(Context);

    if (providerList = PhpQueryWmiProviderHostProcess(ProcessItem))
    {
        for (ULONG i = 0; i < providerList->Count; i++)
        {
            PPH_WMI_ENTRY entry = providerList->Items[i];
            INT lvItemIndex;

            lvItemIndex = PhAddListViewItem(
                Context->ListViewHandle,
                MAXINT, 
                PhGetStringOrEmpty(entry->ProviderName),
                UlongToPtr(Context->WmiProviderList->Count + 1)
                );
            PhSetListViewSubItem(Context->ListViewHandle, lvItemIndex, 1, PhGetStringOrEmpty(entry->NamespacePath));
            PhSetListViewSubItem(Context->ListViewHandle, lvItemIndex, 2, PhGetStringOrEmpty(entry->FileName));
            PhSetListViewSubItem(Context->ListViewHandle, lvItemIndex, 3, PhGetStringOrEmpty(entry->UserName));

            PhAddItemList(Context->WmiProviderList, entry);
        }

        PhDereferenceObject(providerList);
    }

    if (selectedIndex)
    {
        ListView_SetItemState(Context->ListViewHandle, PtrToUlong(selectedIndex) - 1,
            LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
        ListView_EnsureVisible(Context->ListViewHandle, PtrToUlong(selectedIndex) - 1, FALSE);
    }

    ExtendedListView_SetRedraw(Context->ListViewHandle, TRUE);
}

INT_PTR CALLBACK PhpProcessWmiProvidersDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    LPPROPSHEETPAGE propSheetPage;
    PPH_PROCESS_PROPPAGECONTEXT propPageContext;
    PPH_PROCESS_ITEM processItem;
    PPH_WMI_CONTEXT context;

    if (PhPropPageDlgProcHeader(hwndDlg, uMsg, lParam, &propSheetPage, &propPageContext, &processItem))
    {
        context = (PPH_WMI_CONTEXT)propPageContext->Context;
    }
    else
    {
        return FALSE;
    }

    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            context = propPageContext->Context = PhAllocateZero(sizeof(PH_WMI_CONTEXT));
            context->WindowHandle = hwndDlg;
            context->ListViewHandle = GetDlgItem(hwndDlg, IDC_LIST);
            context->Enabled = TRUE;
            context->WmiProviderList = PhCreateList(1);

            PhSetListViewStyle(context->ListViewHandle, FALSE, TRUE);
            PhSetControlTheme(context->ListViewHandle, L"explorer");
            PhAddListViewColumn(context->ListViewHandle, 0, 0, 0, LVCFMT_LEFT, 140, L"Provider");
            PhAddListViewColumn(context->ListViewHandle, 1, 1, 1, LVCFMT_LEFT, 180, L"Namespace");
            PhAddListViewColumn(context->ListViewHandle, 2, 2, 2, LVCFMT_LEFT, 260, L"File name");
            PhAddListViewColumn(context->ListViewHandle, 3, 3, 3, LVCFMT_LEFT, 80, L"User");
            PhSetExtendedListView(context->ListViewHandle);
            PhLoadListViewColumnsFromSetting(L"WmiProviderListViewColumns", context->ListViewHandle);

            PhpRefreshWmiProviders(hwndDlg, context, processItem);

            PhRegisterCallback(
                PhGetGeneralCallback(GeneralCallbackProcessProviderUpdatedEvent),
                PhpWmiProviderUpdateHandler,
                context,
                &context->ProcessesUpdatedRegistration
                );

            PhInitializeWindowTheme(hwndDlg, PhEnableThemeSupport);
        }
        break;
    case WM_DESTROY:
        {
            PhUnregisterCallback(PhGetGeneralCallback(GeneralCallbackProcessProviderUpdatedEvent), &context->ProcessesUpdatedRegistration);

            PhSaveListViewColumnsToSetting(L"WmiProviderListViewColumns", context->ListViewHandle);

            PhpClearWmiProviderItems(context);
            PhDereferenceObject(context->WmiProviderList);

            PhFree(context);
        }
        break;
    case WM_SHOWWINDOW:
        {
            PPH_LAYOUT_ITEM dialogItem;

            if (dialogItem = PhBeginPropPageLayout(hwndDlg, propPageContext))
            {
                PhAddPropPageLayoutItem(hwndDlg, context->ListViewHandle, dialogItem, PH_ANCHOR_ALL);
                PhEndPropPageLayout(hwndDlg, propPageContext);
            }
        }
        break;
    case WM_NOTIFY:
        {
            LPNMHDR header = (LPNMHDR)lParam;

            switch (header->code)
            {
            case PSN_SETACTIVE:
                context->Enabled = TRUE;
                break;
            case PSN_KILLACTIVE:
                context->Enabled = FALSE;
                break;
            }

            PhHandleListViewNotifyForCopy(lParam, context->ListViewHandle);
        }
        break;
    case WM_CONTEXTMENU:
        {
            if ((HWND)wParam == context->ListViewHandle)
            {
                POINT point;
                PVOID index;
                PPH_EMENU_ITEM selectedItem;

                point.x = GET_X_LPARAM(lParam);
                point.y = GET_Y_LPARAM(lParam);

                if (point.x == -1 && point.y == -1)
                    PhGetListViewContextMenuPoint((HWND)wParam, &point);

                if (index = PhGetSelectedListViewItemParam(context->ListViewHandle))
                {
                    PPH_EMENU menu;

                    menu = PhCreateEMenu();
                    if (PhGetIntegerSetting(L"WmiProviderEnableHiddenMenu"))
                    {
                        PhInsertEMenuItem(menu, PhCreateEMenuItem(0, 1, L"&Suspend", NULL, NULL), ULONG_MAX);
                        PhInsertEMenuItem(menu, PhCreateEMenuItem(0, 2, L"Res&ume", NULL, NULL), ULONG_MAX);
                        PhInsertEMenuItem(menu, PhCreateEMenuItem(0, 3, L"Un&load", NULL, NULL), ULONG_MAX);
                        PhInsertEMenuItem(menu, PhCreateEMenuSeparator(), ULONG_MAX);
                    }
                    PhInsertEMenuItem(menu, PhCreateEMenuItem(0, 4, L"Open &file location", NULL, NULL), ULONG_MAX);
                    PhInsertEMenuItem(menu, PhCreateEMenuSeparator(), ULONG_MAX);
                    PhInsertEMenuItem(menu, PhCreateEMenuItem(0, 5, L"&Inspect", NULL, NULL), ULONG_MAX);
                    PhInsertEMenuItem(menu, PhCreateEMenuSeparator(), ULONG_MAX);
                    PhInsertEMenuItem(menu, PhCreateEMenuItem(0, IDC_COPY, L"&Copy", NULL, NULL), ULONG_MAX);
                    PhInsertCopyListViewEMenuItem(menu, IDC_COPY, context->ListViewHandle);

                    selectedItem = PhShowEMenu(
                        menu,
                        hwndDlg,
                        PH_EMENU_SHOW_LEFTRIGHT,
                        PH_ALIGN_LEFT | PH_ALIGN_TOP,
                        point.x,
                        point.y
                        );
                    PhDestroyEMenu(menu);

                    if (selectedItem && selectedItem->Id != ULONG_MAX)
                    {
                        PPH_WMI_ENTRY entry;
                        BOOLEAN handled;

                        handled = PhHandleCopyListViewEMenuItem(selectedItem);

                        //if (!handled && PhPluginsEnabled)
                        //    handled = PhPluginTriggerEMenuItem(&menuInfo, item);

                        if (handled)
                            break;

                        entry = context->WmiProviderList->Items[PtrToUlong(index) - 1];

                        switch (selectedItem->Id)
                        {
                        case 1:
                            PhpWmiProviderExecMethod(L"Suspend", processItem->ProcessIdString, entry);
                            break;
                        case 2:
                            PhpWmiProviderExecMethod(L"Resume", processItem->ProcessIdString, entry);
                            break;
                        case 3:
                            PhpWmiProviderExecMethod(L"Unload", processItem->ProcessIdString, entry);
                            break;
                        case 4:
                            {
                                if (!PhIsNullOrEmptyString(entry->FileName) && PhDoesFileExistsWin32(PhGetString(entry->FileName)))
                                {
                                    PhShellExecuteUserString(
                                        hwndDlg,
                                        L"FileBrowseExecutable",
                                        PhGetString(entry->FileName),
                                        FALSE,
                                        L"Make sure the Explorer executable file is present."
                                        );
                                }
                            }
                            break;
                        case 5:
                            {
                                if (!PhIsNullOrEmptyString(entry->FileName) && PhDoesFileExistsWin32(PhGetString(entry->FileName)))
                                {
                                    PhShellExecuteUserString(
                                        hwndDlg,
                                        L"ProgramInspectExecutables",
                                        PhGetString(entry->FileName),
                                        FALSE,
                                        L"Make sure the PE Viewer executable file is present."
                                        );
                                }
                            }
                            break;
                        case IDC_COPY:
                            {
                                PhCopyListView(context->ListViewHandle);
                            }
                            break;
                        }
                    }
                }
            }
        }
        break;
    case WM_PH_WMI_UPDATE:
        {
            PhpRefreshWmiProviders(hwndDlg, context, processItem);
        }
        break;
    }

    return FALSE;
}
