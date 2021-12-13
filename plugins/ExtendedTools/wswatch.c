/*
 * Process Hacker Extended Tools -
 *   working set watch
 *
 * Copyright (C) 2011 wj32
 * Copyright (C) 2018-2021 dmex
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

#include "exttools.h"
#include <workqueue.h>
#include <symprv.h>

typedef struct _WS_WATCH_CONTEXT
{
    LONG RefCount;

    PPH_PROCESS_ITEM ProcessItem;
    HWND WindowHandle;
    HWND ListViewHandle;
    BOOLEAN Enabled;
    BOOLEAN Destroying;
    PPH_HASHTABLE Hashtable;
    HANDLE ProcessId;
    HANDLE ProcessHandle;
    PVOID Buffer;
    ULONG BufferSize;

    PH_LAYOUT_MANAGER LayoutManager;

    PPH_SYMBOL_PROVIDER SymbolProvider;
    HANDLE LoadingSymbolsForProcessId;
    SINGLE_LIST_ENTRY ResultListHead;
    PH_QUEUED_LOCK ResultListLock;
} WS_WATCH_CONTEXT, *PWS_WATCH_CONTEXT;

typedef struct _SYMBOL_LOOKUP_RESULT
{
    SINGLE_LIST_ENTRY ListEntry;
    PWS_WATCH_CONTEXT Context;
    PVOID Address;
    PPH_STRING Symbol;
    PPH_STRING FileName;
} SYMBOL_LOOKUP_RESULT, *PSYMBOL_LOOKUP_RESULT;

INT_PTR CALLBACK EtpWsWatchDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    );

VOID EtShowWsWatchDialog(
    _In_ HWND ParentWindowHandle,
    _In_ PPH_PROCESS_ITEM ProcessItem
    )
{
    PWS_WATCH_CONTEXT context;

    context = PhAllocateZero(sizeof(WS_WATCH_CONTEXT));
    context->RefCount = 1;
    context->ProcessItem = PhReferenceObject(ProcessItem);

    DialogBoxParam(
        PluginInstance->DllBase,
        MAKEINTRESOURCE(IDD_WSWATCH),
        !!PhGetIntegerSetting(L"ForceNoParent") ? NULL : ParentWindowHandle,
        EtpWsWatchDlgProc,
        (LPARAM)context
        );
}

VOID EtpReferenceWsWatchContext(
    _Inout_ PWS_WATCH_CONTEXT Context
    )
{
    _InterlockedIncrement(&Context->RefCount);
}

VOID EtpDereferenceWsWatchContext(
    _Inout_ PWS_WATCH_CONTEXT Context
    )
{
    if (_InterlockedDecrement(&Context->RefCount) == 0)
    {
        PSINGLE_LIST_ENTRY listEntry;

        if (Context->SymbolProvider)
            PhDereferenceObject(Context->SymbolProvider);
        if (Context->ProcessHandle)
            NtClose(Context->ProcessHandle);

        // Free all unused results.

        PhAcquireQueuedLockExclusive(&Context->ResultListLock);

        listEntry = Context->ResultListHead.Next;

        while (listEntry)
        {
            PSYMBOL_LOOKUP_RESULT result;

            result = CONTAINING_RECORD(listEntry, SYMBOL_LOOKUP_RESULT, ListEntry);
            listEntry = listEntry->Next;

            if (result->Symbol)
                PhDereferenceObject(result->Symbol);
            if (result->FileName)
                PhDereferenceObject(result->FileName);

            PhFree(result);
        }

        PhReleaseQueuedLockExclusive(&Context->ResultListLock);

        PhDereferenceObject(Context->ProcessItem);

        PhFree(Context);
    }
}

static NTSTATUS EtpSymbolLookupFunction(
    _In_ PVOID Parameter
    )
{
    PSYMBOL_LOOKUP_RESULT result = Parameter;

    // Don't bother looking up the symbol if the window has already closed.
    if (result->Context->Destroying)
    {
        EtpDereferenceWsWatchContext(result->Context);
        PhFree(result);
        return STATUS_SUCCESS;
    }

    result->Symbol = PhGetSymbolFromAddress(
        result->Context->SymbolProvider,
        (ULONG64)result->Address,
        NULL,
        &result->FileName,
        NULL,
        NULL
        );

    // Fail if we don't have a symbol.
    if (!result->Symbol)
    {
        EtpDereferenceWsWatchContext(result->Context);
        PhFree(result);
        return STATUS_SUCCESS;
    }

    PhAcquireQueuedLockExclusive(&result->Context->ResultListLock);
    PushEntryList(&result->Context->ResultListHead, &result->ListEntry);
    PhReleaseQueuedLockExclusive(&result->Context->ResultListLock);
    EtpDereferenceWsWatchContext(result->Context);

    return STATUS_SUCCESS;
}

static VOID EtpQueueSymbolLookup(
    _In_ PWS_WATCH_CONTEXT Context,
    _In_ PVOID Address
    )
{
    PSYMBOL_LOOKUP_RESULT result;

    result = PhAllocate(sizeof(SYMBOL_LOOKUP_RESULT));
    result->Context = Context;
    result->Address = Address;
    EtpReferenceWsWatchContext(Context);

    PhQueueItemWorkQueue(PhGetGlobalWorkQueue(), EtpSymbolLookupFunction, result);
}

static PPH_STRING EtpGetBasicSymbol(
    _In_ PPH_SYMBOL_PROVIDER SymbolProvider,
    _In_ ULONG64 Address
    )
{
    ULONG64 modBase;
    PPH_STRING fileName = NULL;
    PPH_STRING baseName = NULL;
    PPH_STRING symbol;

    modBase = PhGetModuleFromAddress(SymbolProvider, Address, &fileName);

    if (!fileName)
    {
        symbol = PhCreateStringEx(NULL, PH_PTR_STR_LEN * sizeof(WCHAR));
        PhPrintPointer(symbol->Buffer, (PVOID)Address);
        PhTrimToNullTerminatorString(symbol);
    }
    else
    {
        PH_FORMAT format[3];

        baseName = PhGetBaseName(fileName);

        PhInitFormatSR(&format[0], baseName->sr);
        PhInitFormatS(&format[1], L"+0x");
        PhInitFormatIX(&format[2], (ULONG_PTR)(Address - modBase));

        symbol = PhFormat(format, 3, baseName->Length + 6 + 32);
    }

    if (fileName)
        PhDereferenceObject(fileName);
    if (baseName)
        PhDereferenceObject(baseName);

    return symbol;
}

static VOID EtpProcessSymbolLookupResults(
    _In_ HWND hwndDlg,
    _In_ PWS_WATCH_CONTEXT Context
    )
{
    PSINGLE_LIST_ENTRY listEntry;

    // Remove all results.
    PhAcquireQueuedLockExclusive(&Context->ResultListLock);
    listEntry = Context->ResultListHead.Next;
    Context->ResultListHead.Next = NULL;
    PhReleaseQueuedLockExclusive(&Context->ResultListLock);

    // Update the list view with the results.
    while (listEntry)
    {
        PSYMBOL_LOOKUP_RESULT result;
        INT lvItemIndex;

        result = CONTAINING_RECORD(listEntry, SYMBOL_LOOKUP_RESULT, ListEntry);
        listEntry = listEntry->Next;

        lvItemIndex = PhFindListViewItemByParam(Context->ListViewHandle, -1, result->Address);

        PhSetListViewSubItem(
            Context->ListViewHandle,
            lvItemIndex,
            0,
            PhGetString(result->Symbol)
            );

        PhSetListViewSubItem(
            Context->ListViewHandle,
            lvItemIndex,
            1,
            PhGetString(result->FileName)
            );

        PhDereferenceObject(result->Symbol);
        PhFree(result);
    }
}

static BOOLEAN EtpUpdateWsWatch(
    _In_ HWND hwndDlg,
    _In_ PWS_WATCH_CONTEXT Context
    )
{
    NTSTATUS status;
    BOOLEAN result;
    ULONG returnLength;
    PPROCESS_WS_WATCH_INFORMATION_EX wsWatchInfo;

    // Query WS watch information.

    if (!Context->Buffer)
        return FALSE;

    status = NtQueryInformationProcess(
        Context->ProcessHandle,
        ProcessWorkingSetWatchEx,
        Context->Buffer,
        Context->BufferSize,
        &returnLength
        );

    if (status == STATUS_UNSUCCESSFUL)
    {
        // WS Watch is not enabled.
        return FALSE;
    }

    if (status == STATUS_NO_MORE_ENTRIES)
    {
        // There were no new faults, but we still need to process symbol lookup results.
        result = TRUE;
        goto SkipBuffer;
    }

    if (status == STATUS_BUFFER_TOO_SMALL || status == STATUS_INFO_LENGTH_MISMATCH)
    {
        PhFree(Context->Buffer);
        Context->Buffer = PhAllocate(returnLength);
        Context->BufferSize = returnLength;

        status = NtQueryInformationProcess(
            Context->ProcessHandle,
            ProcessWorkingSetWatchEx,
            Context->Buffer,
            Context->BufferSize,
            &returnLength
            );
    }

    if (!NT_SUCCESS(status))
    {
        // Error related to the buffer size. Try again later.
        result = FALSE;
        goto SkipBuffer;
    }

    // Update the hashtable and list view.

    ExtendedListView_SetRedraw(Context->ListViewHandle, FALSE);

    wsWatchInfo = Context->Buffer;

    while (wsWatchInfo->BasicInfo.FaultingPc)
    {
        PVOID *entry;
        WCHAR buffer[PH_INT32_STR_LEN_1];
        INT lvItemIndex;
        ULONG newCount;

        // Update the count in the entry for this instruction pointer, or add a new entry if it doesn't exist.

        entry = PhFindItemSimpleHashtable(Context->Hashtable, wsWatchInfo->BasicInfo.FaultingPc);

        if (entry)
        {
            newCount = PtrToUlong(*entry) + 1;
            *entry = UlongToPtr(newCount);
            lvItemIndex = PhFindListViewItemByParam(Context->ListViewHandle, -1, wsWatchInfo->BasicInfo.FaultingPc);
        }
        else
        {
            PPH_STRING basicSymbol;

            newCount = 1;
            PhAddItemSimpleHashtable(Context->Hashtable, wsWatchInfo->BasicInfo.FaultingPc, UlongToPtr(1));

            // Get a basic symbol name (module+offset).
            basicSymbol = EtpGetBasicSymbol(Context->SymbolProvider, (ULONG64)wsWatchInfo->BasicInfo.FaultingPc);

            lvItemIndex = PhAddListViewItem(Context->ListViewHandle, MAXINT, basicSymbol->Buffer, wsWatchInfo->BasicInfo.FaultingPc);
            PhDereferenceObject(basicSymbol);

            // Queue a full symbol lookup.
            EtpQueueSymbolLookup(Context, wsWatchInfo->BasicInfo.FaultingPc);
        }

        // Update the count in the list view item.
        PhPrintUInt32(buffer, newCount);
        PhSetListViewSubItem(
            Context->ListViewHandle,
            lvItemIndex,
            2,
            buffer
            );

        wsWatchInfo++;
    }

    ExtendedListView_SetRedraw(Context->ListViewHandle, TRUE);
    result = TRUE;

SkipBuffer:
    EtpProcessSymbolLookupResults(hwndDlg, Context);
    ExtendedListView_SortItems(Context->ListViewHandle);

    return result;
}

static BOOLEAN NTAPI EnumGenericModulesCallback(
    _In_ PPH_MODULE_INFO Module,
    _In_opt_ PVOID Context
    )
{
    PWS_WATCH_CONTEXT context = Context;

    if (!context)
        return TRUE;

    // If we're loading kernel module symbols for a process other than
    // System, ignore modules which are in user space. This may happen
    // in Windows 7.
    if (
        context->LoadingSymbolsForProcessId == SYSTEM_PROCESS_ID &&
        (ULONG_PTR)Module->BaseAddress <= PhSystemBasicInformation.MaximumUserModeAddress
        )
        return TRUE;

    PhLoadModuleSymbolProvider(
        context->SymbolProvider,
        Module->FileName,
        (ULONG64)Module->BaseAddress,
        Module->Size
        );

    return TRUE;
}

INT_PTR CALLBACK EtpWsWatchDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    PWS_WATCH_CONTEXT context;

    if (uMsg == WM_INITDIALOG)
    {
        context = (PWS_WATCH_CONTEXT)lParam;
        PhSetWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT, context);
    }
    else
    {
        context = PhGetWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT);

        if (uMsg == WM_DESTROY)
            PhRemoveWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT);
    }

    if (!context)
        return FALSE;

    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            context->WindowHandle = hwndDlg;
            context->ListViewHandle = GetDlgItem(hwndDlg, IDC_LIST);

            PhSetApplicationWindowIcon(hwndDlg);

            PhSetListViewStyle(context->ListViewHandle, TRUE, TRUE);
            PhSetControlTheme(context->ListViewHandle, L"explorer");
            PhAddListViewColumn(context->ListViewHandle, 0, 0, 0, LVCFMT_LEFT, 250, L"Instruction");
            PhAddListViewColumn(context->ListViewHandle, 1, 1, 1, LVCFMT_LEFT, 80, L"Filename");
            PhAddListViewColumn(context->ListViewHandle, 2, 2, 2, LVCFMT_LEFT, 80, L"Count");
            PhSetExtendedListView(context->ListViewHandle);
            PhLoadListViewColumnsFromSetting(SETTING_NAME_WSWATCH_COLUMNS, context->ListViewHandle);
            ExtendedListView_SetSort(context->ListViewHandle, 2, DescendingSortOrder);

            PhInitializeLayoutManager(&context->LayoutManager, hwndDlg);
            PhAddLayoutItem(&context->LayoutManager, context->ListViewHandle, NULL, PH_ANCHOR_ALL);
            PhAddLayoutItem(&context->LayoutManager, GetDlgItem(hwndDlg, IDOK), NULL, PH_ANCHOR_RIGHT | PH_ANCHOR_BOTTOM);

            if (PhGetIntegerPairSetting(SETTING_NAME_WSWATCH_WINDOW_POSITION).X != 0)
                PhLoadWindowPlacementFromSetting(SETTING_NAME_WSWATCH_WINDOW_POSITION, SETTING_NAME_WSWATCH_WINDOW_SIZE, hwndDlg);
            else
                PhCenterWindow(hwndDlg, GetParent(hwndDlg));

            context->Hashtable = PhCreateSimpleHashtable(64);
            context->ProcessId = context->ProcessItem->ProcessId;
            context->BufferSize = 0x2000;
            context->Buffer = PhAllocate(context->BufferSize);

            if (PH_IS_REAL_PROCESS_ID(context->ProcessId))
            {
                NTSTATUS status;
                HANDLE processHandle;

                status = PhOpenProcess(
                    &processHandle,
                    PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                    context->ProcessId
                    );

                if (!NT_SUCCESS(status))
                {
                    status = PhOpenProcess(
                        &processHandle,
                        MAXIMUM_ALLOWED,
                        context->ProcessId
                        );
                }

                if (NT_SUCCESS(status))
                {
                    context->ProcessHandle = processHandle;
                }
                else
                {
                    PhShowError(hwndDlg, L"%s", L"Unable to open the process.");
                    EndDialog(hwndDlg, IDCANCEL);
                    break;
                }
            }

            PhInitializeQueuedLock(&context->ResultListLock);
            context->SymbolProvider = PhCreateSymbolProvider(context->ProcessId);
            PhLoadSymbolProviderOptions(context->SymbolProvider);

            if (!context->SymbolProvider)
            {
                PhShowError(hwndDlg, L"%s", L"Unable to create the symbol provider.");
                EndDialog(hwndDlg, IDCANCEL);
                break;
            }

            // Load symbols for both process and kernel modules.
            context->LoadingSymbolsForProcessId = context->ProcessId;
            PhEnumGenericModules(
                context->ProcessId,
                context->ProcessHandle,
                0,
                EnumGenericModulesCallback,
                context
                );
            context->LoadingSymbolsForProcessId = SYSTEM_PROCESS_ID;
            PhEnumGenericModules(
                SYSTEM_PROCESS_ID,
                NULL,
                0,
                EnumGenericModulesCallback,
                context
                );

            context->Enabled = EtpUpdateWsWatch(hwndDlg, context);

            if (context->Enabled)
            {
                // WS Watch is already enabled for the process. Enable updating.
                EnableWindow(GetDlgItem(hwndDlg, IDC_ENABLE), FALSE);
                ShowWindow(GetDlgItem(hwndDlg, IDC_WSWATCHENABLED), SW_SHOW);
                SetTimer(hwndDlg, 1, 1000, NULL);
            }
            else
            {
                // WS Watch has not yet been enabled for the process.
            }

            PhInitializeWindowTheme(hwndDlg, !!PhGetIntegerSetting(L"EnableThemeSupport"));
        }
        break;
    case WM_DESTROY:
        {
            context->Destroying = TRUE;

            KillTimer(hwndDlg, 1);

            PhSaveListViewColumnsToSetting(SETTING_NAME_WSWATCH_COLUMNS, context->ListViewHandle);
            PhSaveWindowPlacementToSetting(SETTING_NAME_WSWATCH_WINDOW_POSITION, SETTING_NAME_WSWATCH_WINDOW_SIZE, hwndDlg);

            PhDereferenceObject(context->Hashtable);

            if (context->Buffer)
            {
                PhFree(context->Buffer);
                context->Buffer = NULL;
            }

            PhDeleteLayoutManager(&context->LayoutManager);

            EtpDereferenceWsWatchContext(context);
        }
        break;
    case WM_COMMAND:
        {
            switch (GET_WM_COMMAND_ID(wParam, lParam))
            {
            case IDCANCEL:
            case IDOK:
                EndDialog(hwndDlg, IDOK);
                break;
            case IDC_ENABLE:
                {
                    NTSTATUS status;
                    HANDLE processHandle;

                    if (NT_SUCCESS(status = PhOpenProcess(
                        &processHandle,
                        PROCESS_SET_INFORMATION,
                        context->ProcessId
                        )))
                    {
                        status = NtSetInformationProcess(
                            processHandle,
                            ProcessWorkingSetWatchEx,
                            NULL,
                            0
                            );
                        NtClose(processHandle);
                    }

                    if (NT_SUCCESS(status))
                    {
                        EnableWindow(GetDlgItem(hwndDlg, IDC_ENABLE), FALSE);
                        ShowWindow(GetDlgItem(hwndDlg, IDC_WSWATCHENABLED), SW_SHOW);
                        SetTimer(hwndDlg, 1, 1000, NULL);
                    }
                    else
                    {
                        PhShowStatus(hwndDlg, L"Unable to enable WS watch.", status, 0);
                    }
                }
                break;
            }
        }
        break;
    case WM_NOTIFY:
        {
            LPNMHDR header = (LPNMHDR)lParam;

            switch (header->code)
            {
            case NM_DBLCLK:
                {
                    if (header->hwndFrom == context->ListViewHandle)
                    {
                        PVOID entry;

                        if (entry = PhGetSelectedListViewItemParam(context->ListViewHandle))
                        {
                            INT lvItemIndex;
                            PPH_STRING fileNameWin32;

                            lvItemIndex = PhFindListViewItemByParam(context->ListViewHandle, -1, entry);

                            if (lvItemIndex == -1)
                                break;

                            fileNameWin32 = PhGetListViewItemText(context->ListViewHandle, lvItemIndex, 1);

                            if (
                                !PhIsNullOrEmptyString(fileNameWin32) &&
                                PhDoesFileExistsWin32(PhGetString(fileNameWin32))
                                )
                            {
                                PhShellExecuteUserString(
                                    hwndDlg,
                                    L"ProgramInspectExecutables",
                                    PhGetString(fileNameWin32),
                                    FALSE,
                                    L"Make sure the PE Viewer executable file is present."
                                    );
                            }
                        }
                    }
                }
                break;
            }

            PhHandleListViewNotifyForCopy(lParam, context->ListViewHandle);
        }
        break;
    case WM_SIZE:
        {
            PhLayoutManagerLayout(&context->LayoutManager);
        }
        break;
    case WM_TIMER:
        {
            switch (wParam)
            {
            case 1:
                EtpUpdateWsWatch(hwndDlg, context);
                break;
            }
        }
        break;
    case WM_CONTEXTMENU:
        {
            POINT point;
            PPH_EMENU menu;
            PPH_EMENU item;
            PPH_STRING fileNameWin32;
            INT lvItemIndex;
            PVOID listviewItem;

            point.x = GET_X_LPARAM(lParam);
            point.y = GET_Y_LPARAM(lParam);

            if (point.x == -1 && point.y == -1)
                PhGetListViewContextMenuPoint((HWND)wParam, &point);

            if (!(listviewItem = PhGetSelectedListViewItemParam(context->ListViewHandle)))
                break;
            if ((lvItemIndex = PhFindListViewItemByParam(context->ListViewHandle, -1, listviewItem)) == -1)
                break;

            fileNameWin32 = PhGetListViewItemText(context->ListViewHandle, lvItemIndex, 1);

            menu = PhCreateEMenu();
            PhInsertEMenuItem(menu, PhCreateEMenuItem(0, 1, L"&Inspect", NULL, NULL), ULONG_MAX);
            PhInsertEMenuItem(menu, PhCreateEMenuSeparator(), ULONG_MAX);
            PhInsertEMenuItem(menu, PhCreateEMenuItem(0, 2, L"Open &file location", NULL, NULL), ULONG_MAX);
            PhInsertEMenuItem(menu, PhCreateEMenuSeparator(), ULONG_MAX);
            PhInsertEMenuItem(menu, PhCreateEMenuItem(0, 3, L"&Copy", NULL, NULL), ULONG_MAX);
            PhInsertCopyListViewEMenuItem(menu, 3, context->ListViewHandle);
            PhSetFlagsEMenuItem(menu, 1, PH_EMENU_DEFAULT, PH_EMENU_DEFAULT);

            if (PhIsNullOrEmptyString(fileNameWin32) || !PhDoesFileExistsWin32(PhGetString(fileNameWin32)))
            {
                PhSetFlagsEMenuItem(menu, 1, PH_EMENU_DISABLED, PH_EMENU_DISABLED);
                PhSetFlagsEMenuItem(menu, 2, PH_EMENU_DISABLED, PH_EMENU_DISABLED);
            }

            item = PhShowEMenu(
                menu,
                hwndDlg,
                PH_EMENU_SHOW_LEFTRIGHT,
                PH_ALIGN_LEFT | PH_ALIGN_TOP,
                point.x,
                point.y
                );

            if (item)
            {
                if (!PhHandleCopyListViewEMenuItem(item))
                {
                    switch (item->Id)
                    {
                    case 1:
                        {
                            PhShellExecuteUserString(
                                hwndDlg,
                                L"ProgramInspectExecutables",
                                PhGetString(fileNameWin32),
                                FALSE,
                                L"Make sure the PE Viewer executable file is present."
                                );
                        }
                        break;
                    case 2:
                        {
                            PhShellExecuteUserString(
                                hwndDlg,
                                L"FileBrowseExecutable",
                                PhGetString(fileNameWin32),
                                FALSE,
                                L"Make sure the Explorer executable file is present."
                                );
                        }
                        break;
                    case 3:
                        PhCopyListView(context->ListViewHandle);
                        break;
                    }
                }
            }

            PhDestroyEMenu(menu);
        }
        break;
    }

    return FALSE;
}
