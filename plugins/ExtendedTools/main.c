/*
 * Process Hacker Extended Tools -
 *   main program
 *
 * Copyright (C) 2010-2015 wj32
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

PPH_PLUGIN PluginInstance = NULL;
HWND ProcessTreeNewHandle = NULL;
HWND NetworkTreeNewHandle = NULL;
LIST_ENTRY EtProcessBlockListHead = { &EtProcessBlockListHead, &EtProcessBlockListHead };
LIST_ENTRY EtNetworkBlockListHead = { &EtNetworkBlockListHead, &EtNetworkBlockListHead };
PH_CALLBACK_REGISTRATION PluginLoadCallbackRegistration;
PH_CALLBACK_REGISTRATION PluginUnloadCallbackRegistration;
PH_CALLBACK_REGISTRATION PluginShowOptionsCallbackRegistration;
PH_CALLBACK_REGISTRATION PluginMenuItemCallbackRegistration;
PH_CALLBACK_REGISTRATION PluginTreeNewMessageCallbackRegistration;
PH_CALLBACK_REGISTRATION PluginPhSvcRequestCallbackRegistration;
PH_CALLBACK_REGISTRATION MainWindowShowingCallbackRegistration;
PH_CALLBACK_REGISTRATION ProcessesUpdatedCallbackRegistration;
PH_CALLBACK_REGISTRATION ProcessPropertiesInitializingCallbackRegistration;
PH_CALLBACK_REGISTRATION HandlePropertiesInitializingCallbackRegistration;
PH_CALLBACK_REGISTRATION ProcessMenuInitializingCallbackRegistration;
PH_CALLBACK_REGISTRATION ThreadMenuInitializingCallbackRegistration;
PH_CALLBACK_REGISTRATION ModuleMenuInitializingCallbackRegistration;
PH_CALLBACK_REGISTRATION ProcessTreeNewInitializingCallbackRegistration;
PH_CALLBACK_REGISTRATION NetworkTreeNewInitializingCallbackRegistration;
PH_CALLBACK_REGISTRATION SystemInformationInitializingCallbackRegistration;
PH_CALLBACK_REGISTRATION MiniInformationInitializingCallbackRegistration;
PH_CALLBACK_REGISTRATION TrayIconsInitializingCallbackRegistration;
PH_CALLBACK_REGISTRATION ProcessItemsUpdatedCallbackRegistration;
PH_CALLBACK_REGISTRATION NetworkItemsUpdatedCallbackRegistration;
PH_CALLBACK_REGISTRATION ProcessStatsEventCallbackRegistration;

ULONG ProcessesUpdatedCount = 0;
static HANDLE ModuleProcessId = NULL;

VOID NTAPI LoadCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    EtEtwStatisticsInitialization();
    EtGpuMonitorInitialization();
}

VOID NTAPI UnloadCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    EtSaveSettingsDiskTreeList();
    EtEtwStatisticsUninitialization();
}

VOID NTAPI ShowOptionsCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    PPH_PLUGIN_OPTIONS_POINTERS optionsEntry = (PPH_PLUGIN_OPTIONS_POINTERS)Parameter;

    if (optionsEntry)
    {
        optionsEntry->CreateSection(
            L"ExtendedTools",
            PluginInstance->DllBase,
            MAKEINTRESOURCE(IDD_OPTIONS),
            OptionsDlgProc,
            NULL
            );
    }
}

VOID NTAPI MenuItemCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    PPH_PLUGIN_MENU_ITEM menuItem = Parameter;

    if (!menuItem)
        return;

    switch (menuItem->Id)
    {
    case ID_PROCESS_UNLOADEDMODULES:
        {
            EtShowUnloadedDllsDialog(menuItem->OwnerWindow, menuItem->Context);
        }
        break;
    case ID_PROCESS_WSWATCH:
        {
            EtShowWsWatchDialog(menuItem->OwnerWindow, menuItem->Context);
        }
        break;
    case ID_THREAD_CANCELIO:
        {
            EtUiCancelIoThread(menuItem->OwnerWindow, menuItem->Context);
        }
        break;
    case ID_MODULE_SERVICES:
        {
            EtShowModuleServicesDialog(
                menuItem->OwnerWindow,
                ModuleProcessId,
                ((PPH_MODULE_ITEM)menuItem->Context)->Name
                );
        }
        break;
    }
}

VOID NTAPI TreeNewMessageCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    PPH_PLUGIN_TREENEW_MESSAGE message = Parameter;

    if (!message)
        return;

    if (message->TreeNewHandle == ProcessTreeNewHandle)
        EtProcessTreeNewMessage(Parameter);
    else if (message->TreeNewHandle == NetworkTreeNewHandle)
        EtNetworkTreeNewMessage(Parameter);
}

VOID NTAPI PhSvcRequestCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    if (!Parameter)
        return;

    DispatchPhSvcRequest(Parameter);
}

VOID NTAPI MainWindowShowingCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    EtInitializeDiskTab();
    EtInitializeFirewallTab();
    EtRegisterToolbarGraphs();
}

VOID NTAPI ProcessesUpdatedCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    if (ProcessesUpdatedCount != 3)
    {
        ProcessesUpdatedCount++;
        return;
    }
}

VOID NTAPI ProcessPropertiesInitializingCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    if (Parameter)
    {
        EtProcessGpuPropertiesInitializing(Parameter);
        EtProcessEtwPropertiesInitializing(Parameter);
    }
}

VOID NTAPI HandlePropertiesInitializingCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    if (Parameter)
        EtHandlePropertiesInitializing(Parameter);
}

VOID NTAPI ProcessMenuInitializingCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    PPH_PLUGIN_MENU_INFORMATION menuInfo = Parameter;
    PPH_PROCESS_ITEM processItem;
    ULONG flags;
    PPH_EMENU_ITEM miscMenu;

    if (!menuInfo)
        return;

    if (menuInfo->u.Process.NumberOfProcesses == 1)
        processItem = menuInfo->u.Process.Processes[0];
    else
        processItem = NULL;

    flags = 0;

    if (!processItem)
        flags = PH_EMENU_DISABLED;

    miscMenu = PhFindEMenuItem(menuInfo->Menu, 0, NULL, PHAPP_ID_PROCESS_MISCELLANEOUS);

    if (miscMenu)
    {
        PhInsertEMenuItem(miscMenu, PhPluginCreateEMenuItem(PluginInstance, flags, ID_PROCESS_UNLOADEDMODULES, L"&Unloaded modules", processItem), ULONG_MAX);
        PhInsertEMenuItem(miscMenu, PhPluginCreateEMenuItem(PluginInstance, flags, ID_PROCESS_WSWATCH, L"&WS watch", processItem), ULONG_MAX);
    }
}

VOID NTAPI ThreadMenuInitializingCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    PPH_PLUGIN_MENU_INFORMATION menuInfo = Parameter;
    PPH_THREAD_ITEM threadItem;
    ULONG insertIndex;
    PPH_EMENU_ITEM menuItem;

    if (!menuInfo)
        return;

    if (menuInfo->u.Thread.NumberOfThreads == 1)
        threadItem = menuInfo->u.Thread.Threads[0];
    else
        threadItem = NULL;

    if (menuItem = PhFindEMenuItem(menuInfo->Menu, 0, NULL, PHAPP_ID_THREAD_RESUME))
        insertIndex = PhIndexOfEMenuItem(menuInfo->Menu, menuItem) + 1;
    else
        insertIndex = ULONG_MAX;

    menuItem = PhPluginCreateEMenuItem(PluginInstance, 0, ID_THREAD_CANCELIO, L"Ca&ncel I/O", threadItem);
    PhInsertEMenuItem(menuInfo->Menu, menuItem, insertIndex);

    if (!threadItem)
        PhSetDisabledEMenuItem(menuItem);
    if (menuInfo->u.Thread.ProcessId == SYSTEM_IDLE_PROCESS_ID)
        PhSetDisabledEMenuItem(menuItem);
}

VOID NTAPI ModuleMenuInitializingCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    PPH_PLUGIN_MENU_INFORMATION menuInfo = Parameter;
    PPH_PROCESS_ITEM processItem;
    BOOLEAN addMenuItem;
    PPH_MODULE_ITEM moduleItem;
    ULONG insertIndex;
    PPH_EMENU_ITEM menuItem;

    addMenuItem = FALSE;

    if (!menuInfo)
        return;

    if (processItem = PhReferenceProcessItem(menuInfo->u.Module.ProcessId))
    {
        if (processItem->ServiceList && processItem->ServiceList->Count != 0)
            addMenuItem = TRUE;

        PhDereferenceObject(processItem);
    }

    if (!addMenuItem)
        return;

    if (menuInfo->u.Module.NumberOfModules == 1)
        moduleItem = menuInfo->u.Module.Modules[0];
    else
        moduleItem = NULL;

    if (menuItem = PhFindEMenuItem(menuInfo->Menu, 0, NULL, PHAPP_ID_MODULE_UNLOAD))
        insertIndex = PhIndexOfEMenuItem(menuInfo->Menu, menuItem) + 1;
    else
        insertIndex = ULONG_MAX;

    ModuleProcessId = menuInfo->u.Module.ProcessId;

    menuItem = PhPluginCreateEMenuItem(PluginInstance, 0, ID_MODULE_SERVICES, L"Ser&vices", moduleItem);
    PhInsertEMenuItem(menuInfo->Menu, PhCreateEMenuSeparator(), insertIndex);
    PhInsertEMenuItem(menuInfo->Menu, menuItem, insertIndex + 1);

    if (!moduleItem) menuItem->Flags |= PH_EMENU_DISABLED;
}

VOID NTAPI ProcessTreeNewInitializingCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    PPH_PLUGIN_TREENEW_INFORMATION treeNewInfo = Parameter;

    if (!treeNewInfo)
        return;

    ProcessTreeNewHandle = treeNewInfo->TreeNewHandle;
    EtProcessTreeNewInitializing(Parameter);
}

VOID NTAPI NetworkTreeNewInitializingCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    PPH_PLUGIN_TREENEW_INFORMATION treeNewInfo = Parameter;

    if (!treeNewInfo)
        return;

    NetworkTreeNewHandle = treeNewInfo->TreeNewHandle;
    EtNetworkTreeNewInitializing(Parameter);
}

VOID NTAPI SystemInformationInitializingCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    if (!Parameter)
        return;

    if (EtGpuEnabled)
        EtGpuSystemInformationInitializing(Parameter);
    if (EtEtwEnabled && !!PhGetIntegerSetting(SETTING_NAME_ENABLE_SYSINFO_GRAPHS))
        EtEtwSystemInformationInitializing(Parameter);
}

VOID NTAPI MiniInformationInitializingCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    if (!Parameter)
        return;

    if (EtGpuEnabled)
        EtGpuMiniInformationInitializing(Parameter);
    if (EtEtwEnabled)
        EtEtwMiniInformationInitializing(Parameter);
}

VOID NTAPI TrayIconsInitializingCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    if (!Parameter)
        return;

    EtLoadTrayIconGuids();
    EtRegisterNotifyIcons(Parameter);
}

VOID NTAPI ProcessItemsUpdatedCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    PLIST_ENTRY listEntry;

    // Note: no lock is needed because we only ever modify the list on this same thread.

    listEntry = EtProcessBlockListHead.Flink;

    while (listEntry != &EtProcessBlockListHead)
    {
        PET_PROCESS_BLOCK block;

        block = CONTAINING_RECORD(listEntry, ET_PROCESS_BLOCK, ListEntry);

        PhUpdateDelta(&block->HardFaultsDelta, block->ProcessItem->HardFaultCount);

        // Invalidate all text.

        PhAcquireQueuedLockExclusive(&block->TextCacheLock);
        memset(block->TextCacheValid, 0, sizeof(block->TextCacheValid));
        PhReleaseQueuedLockExclusive(&block->TextCacheLock);

        listEntry = listEntry->Flink;
    }
}

VOID NTAPI NetworkItemsUpdatedCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    PLIST_ENTRY listEntry;

    // Note: no lock is needed because we only ever modify the list on this same thread.

    listEntry = EtNetworkBlockListHead.Flink;

    while (listEntry != &EtNetworkBlockListHead)
    {
        PET_NETWORK_BLOCK block;

        block = CONTAINING_RECORD(listEntry, ET_NETWORK_BLOCK, ListEntry);

        // Invalidate all text.

        PhAcquireQueuedLockExclusive(&block->TextCacheLock);
        memset(block->TextCacheValid, 0, sizeof(block->TextCacheValid));
        PhReleaseQueuedLockExclusive(&block->TextCacheLock);

        listEntry = listEntry->Flink;
    }
}

VOID NTAPI ProcessStatsEventCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    PPH_PLUGIN_PROCESS_STATS_EVENT event = Parameter;

    if (!event)
        return;
    if (event->Version)
        return;

    switch (event->Type)
    {
    case 1:
        {
            HWND listViewHandle = event->Parameter;
            PET_PROCESS_BLOCK block;

            if (!(block = EtGetProcessBlock(event->ProcessItem)))
                break;

            block->ListViewGroupCache[ET_PROCESS_STATISTICS_CATEGORY_GPU] = PhAddListViewGroup(
                listViewHandle, (INT)ListView_GetGroupCount(listViewHandle), L"GPU");
            block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_RUNNINGTIME] = PhAddListViewGroupItem(
                listViewHandle, block->ListViewGroupCache[ET_PROCESS_STATISTICS_CATEGORY_GPU], MAXINT, L"Running time", NULL);
            PhSetListViewItemParam(listViewHandle, block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_RUNNINGTIME],
                UlongToPtr(block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_RUNNINGTIME]));
            block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_CONTEXTSWITCHES] = PhAddListViewGroupItem(
                listViewHandle, block->ListViewGroupCache[ET_PROCESS_STATISTICS_CATEGORY_GPU], MAXINT, L"Context switches", NULL);
            PhSetListViewItemParam(listViewHandle, block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_CONTEXTSWITCHES],
                UlongToPtr(block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_CONTEXTSWITCHES]));
            //block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_TOTALNODES] = PhAddListViewGroupItem(
            //    listViewHandle, block->ListViewGroupCache[ET_PROCESS_STATISTICS_CATEGORY_GPU], MAXINT, L"Total Nodes", NULL);
            //block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_TOTALSEGMENTS] = PhAddListViewGroupItem(
            //    listViewHandle, block->ListViewGroupCache[ET_PROCESS_STATISTICS_CATEGORY_GPU], MAXINT, L"Total Segments", NULL);
            block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_TOTALDEDICATED] = PhAddListViewGroupItem(
                listViewHandle, block->ListViewGroupCache[ET_PROCESS_STATISTICS_CATEGORY_GPU], MAXINT, L"Total Dedicated", NULL);
            PhSetListViewItemParam(listViewHandle, block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_TOTALDEDICATED],
                UlongToPtr(block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_TOTALDEDICATED]));
            block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_TOTALSHARED] = PhAddListViewGroupItem(
                listViewHandle, block->ListViewGroupCache[ET_PROCESS_STATISTICS_CATEGORY_GPU], MAXINT, L"Total Shared", NULL);
            PhSetListViewItemParam(listViewHandle, block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_TOTALSHARED],
                UlongToPtr(block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_TOTALSHARED]));
            block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_TOTALCOMMIT] = PhAddListViewGroupItem(
                listViewHandle, block->ListViewGroupCache[ET_PROCESS_STATISTICS_CATEGORY_GPU], MAXINT, L"Total Commit", NULL);
            PhSetListViewItemParam(listViewHandle, block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_TOTALCOMMIT],
                UlongToPtr(block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_TOTALCOMMIT]));

            block->ListViewGroupCache[ET_PROCESS_STATISTICS_CATEGORY_DISK] = PhAddListViewGroup(
                listViewHandle, (INT)ListView_GetGroupCount(listViewHandle), L"Disk I/O");
            block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_DISKREADS] = PhAddListViewGroupItem(
                listViewHandle, block->ListViewGroupCache[ET_PROCESS_STATISTICS_CATEGORY_DISK], MAXINT, L"Reads", NULL);
            PhSetListViewItemParam(listViewHandle, block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_DISKREADS],
                UlongToPtr(block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_DISKREADS]));
            block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_DISKREADBYTES] = PhAddListViewGroupItem(
                listViewHandle, block->ListViewGroupCache[ET_PROCESS_STATISTICS_CATEGORY_DISK], MAXINT, L"Read bytes", NULL);
            PhSetListViewItemParam(listViewHandle, block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_DISKREADBYTES],
                UlongToPtr(block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_DISKREADBYTES]));
            block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_DISKREADBYTESDELTA] = PhAddListViewGroupItem(
                listViewHandle, block->ListViewGroupCache[ET_PROCESS_STATISTICS_CATEGORY_DISK], MAXINT, L"Read bytes delta", NULL);
            PhSetListViewItemParam(listViewHandle, block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_DISKREADBYTESDELTA],
                UlongToPtr(block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_DISKREADBYTESDELTA]));
            block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_DISKWRITES] = PhAddListViewGroupItem(
                listViewHandle, block->ListViewGroupCache[ET_PROCESS_STATISTICS_CATEGORY_DISK], MAXINT, L"Writes", NULL);
            PhSetListViewItemParam(listViewHandle, block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_DISKWRITES],
                UlongToPtr(block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_DISKWRITES]));
            block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_DISKWRITEBYTES] = PhAddListViewGroupItem(
                listViewHandle, block->ListViewGroupCache[ET_PROCESS_STATISTICS_CATEGORY_DISK], MAXINT, L"Write bytes", NULL);
            PhSetListViewItemParam(listViewHandle, block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_DISKWRITEBYTES],
                UlongToPtr(block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_DISKWRITEBYTES]));
            block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_DISKWRITEBYTESDELTA] = PhAddListViewGroupItem(
                listViewHandle, block->ListViewGroupCache[ET_PROCESS_STATISTICS_CATEGORY_DISK], MAXINT, L"Write bytes delta", NULL);
            PhSetListViewItemParam(listViewHandle, block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_DISKWRITEBYTESDELTA],
                UlongToPtr(block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_DISKWRITEBYTESDELTA]));

            block->ListViewGroupCache[ET_PROCESS_STATISTICS_CATEGORY_NETWORK] = PhAddListViewGroup(
                listViewHandle, (INT)ListView_GetGroupCount(listViewHandle), L"Network I/O");
            block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_NETWORKREADS] = PhAddListViewGroupItem(
                listViewHandle, block->ListViewGroupCache[ET_PROCESS_STATISTICS_CATEGORY_NETWORK], MAXINT, L"Receives", NULL);
            PhSetListViewItemParam(listViewHandle, block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_NETWORKREADS],
                UlongToPtr(block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_NETWORKREADS]));
            block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_NETWORKREADBYTES] = PhAddListViewGroupItem(
                listViewHandle, block->ListViewGroupCache[ET_PROCESS_STATISTICS_CATEGORY_NETWORK], MAXINT, L"Receive bytes", NULL);
            PhSetListViewItemParam(listViewHandle, block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_NETWORKREADBYTES],
                UlongToPtr(block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_NETWORKREADBYTES]));
            block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_NETWORKREADBYTESDELTA] = PhAddListViewGroupItem(
                listViewHandle, block->ListViewGroupCache[ET_PROCESS_STATISTICS_CATEGORY_NETWORK], MAXINT, L"Receive bytes delta", NULL);
            PhSetListViewItemParam(listViewHandle, block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_NETWORKREADBYTESDELTA],
                UlongToPtr(block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_NETWORKREADBYTESDELTA]));
            block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_NETWORKWRITES] = PhAddListViewGroupItem(
                listViewHandle, block->ListViewGroupCache[ET_PROCESS_STATISTICS_CATEGORY_NETWORK], MAXINT, L"Sends", NULL);
            PhSetListViewItemParam(listViewHandle, block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_NETWORKWRITES],
                UlongToPtr(block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_NETWORKWRITES]));
            block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_NETWORKWRITEBYTES] = PhAddListViewGroupItem(
                listViewHandle, block->ListViewGroupCache[ET_PROCESS_STATISTICS_CATEGORY_NETWORK], MAXINT, L"Send bytes", NULL);
            PhSetListViewItemParam(listViewHandle, block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_NETWORKWRITEBYTES],
                UlongToPtr(block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_NETWORKWRITEBYTES]));
            block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_NETWORKWRITEBYTESDELTA] = PhAddListViewGroupItem(
                listViewHandle, block->ListViewGroupCache[ET_PROCESS_STATISTICS_CATEGORY_NETWORK], MAXINT, L"Send bytes delta", NULL);
            PhSetListViewItemParam(listViewHandle, block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_NETWORKWRITEBYTESDELTA],
                UlongToPtr(block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_NETWORKWRITEBYTESDELTA]));
        }
        break;
    case 2:
        {
            NMLVDISPINFO* dispInfo = (NMLVDISPINFO*)event->Parameter;
            PET_PROCESS_BLOCK block;

            if (!(block = EtGetProcessBlock(event->ProcessItem)))
                break;

            if (dispInfo->item.iSubItem == 1)
            {
                if (dispInfo->item.mask & LVIF_TEXT)
                {
                    ULONG index = PtrToUlong((PVOID)dispInfo->item.lParam);

                    if (index == block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_RUNNINGTIME])
                    {
                        WCHAR runningTimeString[PH_TIMESPAN_STR_LEN_1] = L"N/A";

                        PhPrintTimeSpan(runningTimeString, block->GpuRunningTimeDelta.Value * 10, PH_TIMESPAN_HMSM);
                        wcsncpy_s(dispInfo->item.pszText, dispInfo->item.cchTextMax, runningTimeString, _TRUNCATE);
                    }
                    else if (index == block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_CONTEXTSWITCHES])
                    {
                        PPH_STRING value;

                        value = PhFormatUInt64(block->GpuContextSwitches, TRUE);
                        wcsncpy_s(dispInfo->item.pszText, dispInfo->item.cchTextMax, value->Buffer, _TRUNCATE);
                        PhDereferenceObject(value);
                    }
                    //else if (index == ET_PROCESS_STATISTICS_INDEX_TOTALNODES)
                    //{
                    //    PPH_STRING value;
                    //    value = PhFormatUInt64(gpuStatistics.NodeCount, TRUE);
                    //    wcsncpy_s(dispInfo->item.pszText, dispInfo->item.cchTextMax, value->Buffer, _TRUNCATE);
                    //    PhDereferenceObject(value);
                    //}
                    //break;
                    //PhSetListViewSubItem(listViewHandle, block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_TOTALSEGMENTS], 1, PhaFormatUInt64(gpuStatistics.SegmentCount, TRUE)->Buffer);
                    else if (index == block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_TOTALDEDICATED])
                    {
                        PPH_STRING value;

                        value = PhFormatSize(block->GpuDedicatedUsage, ULONG_MAX);
                        wcsncpy_s(dispInfo->item.pszText, dispInfo->item.cchTextMax, value->Buffer, _TRUNCATE);
                        PhDereferenceObject(value);
                    }
                    else if (index == block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_TOTALSHARED])
                    {
                        PPH_STRING value;

                        value = PhFormatSize(block->GpuSharedUsage, ULONG_MAX);
                        wcsncpy_s(dispInfo->item.pszText, dispInfo->item.cchTextMax, value->Buffer, _TRUNCATE);
                        PhDereferenceObject(value);
                    }
                    else if (index == block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_TOTALCOMMIT])
                    {
                        PPH_STRING value;

                        value = PhFormatSize(block->GpuCommitUsage, ULONG_MAX);
                        wcsncpy_s(dispInfo->item.pszText, dispInfo->item.cchTextMax, value->Buffer, _TRUNCATE);
                        PhDereferenceObject(value);
                    }
                    else if (index == block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_DISKREADS])
                    {
                        PPH_STRING value;

                        value = PhFormatUInt64(block->DiskReadCount, TRUE);
                        wcsncpy_s(dispInfo->item.pszText, dispInfo->item.cchTextMax, value->Buffer, _TRUNCATE);
                        PhDereferenceObject(value);
                    }
                    else if (index == block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_DISKREADBYTES])
                    {
                        PPH_STRING value;

                        value = PhFormatSize(block->DiskReadRawDelta.Value, ULONG_MAX);
                        wcsncpy_s(dispInfo->item.pszText, dispInfo->item.cchTextMax, value->Buffer, _TRUNCATE);
                        PhDereferenceObject(value);
                    }
                    else if (index == block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_DISKREADBYTESDELTA])
                    {
                        PPH_STRING value;

                        value = PhFormatSize(block->DiskReadRawDelta.Delta, ULONG_MAX);
                        wcsncpy_s(dispInfo->item.pszText, dispInfo->item.cchTextMax, value->Buffer, _TRUNCATE);
                        PhDereferenceObject(value);
                    }
                    else if (index == block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_DISKWRITES])
                    {
                        PPH_STRING value;

                        value = PhFormatUInt64(block->DiskWriteCount, TRUE);
                        wcsncpy_s(dispInfo->item.pszText, dispInfo->item.cchTextMax, value->Buffer, _TRUNCATE);
                        PhDereferenceObject(value);
                    }
                    else if (index == block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_DISKWRITEBYTES])
                    {
                        PPH_STRING value;

                        value = PhFormatSize(block->DiskWriteRawDelta.Value, ULONG_MAX);
                        wcsncpy_s(dispInfo->item.pszText, dispInfo->item.cchTextMax, value->Buffer, _TRUNCATE);
                        PhDereferenceObject(value);
                    }
                    else if (index == block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_DISKWRITEBYTESDELTA])
                    {
                        PPH_STRING value;

                        value = PhFormatSize(block->DiskWriteRawDelta.Delta, ULONG_MAX);
                        wcsncpy_s(dispInfo->item.pszText, dispInfo->item.cchTextMax, value->Buffer, _TRUNCATE);
                        PhDereferenceObject(value);
                    }
                    else if (index == block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_NETWORKREADS])
                    {
                        PPH_STRING value;

                        value = PhFormatUInt64(block->NetworkReceiveCount, TRUE);
                        wcsncpy_s(dispInfo->item.pszText, dispInfo->item.cchTextMax, value->Buffer, _TRUNCATE);
                        PhDereferenceObject(value);
                    }
                    else if (index == block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_NETWORKREADBYTES])
                    {
                        PPH_STRING value;

                        value = PhFormatSize(block->NetworkReceiveRawDelta.Value, ULONG_MAX);
                        wcsncpy_s(dispInfo->item.pszText, dispInfo->item.cchTextMax, value->Buffer, _TRUNCATE);
                        PhDereferenceObject(value);
                    }
                    else if (index == block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_NETWORKREADBYTESDELTA])
                    {
                        PPH_STRING value;

                        value = PhFormatSize(block->NetworkReceiveRawDelta.Delta, ULONG_MAX);
                        wcsncpy_s(dispInfo->item.pszText, dispInfo->item.cchTextMax, value->Buffer, _TRUNCATE);
                        PhDereferenceObject(value);
                    }
                    else if (index == block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_NETWORKWRITES])
                    {
                        PPH_STRING value;

                        value = PhFormatUInt64(block->NetworkSendCount, TRUE);
                        wcsncpy_s(dispInfo->item.pszText, dispInfo->item.cchTextMax, value->Buffer, _TRUNCATE);
                        PhDereferenceObject(value);
                    }
                    else if (index == block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_NETWORKWRITEBYTES])
                    {
                        PPH_STRING value;

                        value = PhFormatSize(block->NetworkSendRawDelta.Value, ULONG_MAX);
                        wcsncpy_s(dispInfo->item.pszText, dispInfo->item.cchTextMax, value->Buffer, _TRUNCATE);
                        PhDereferenceObject(value);
                    }
                    else if (index == block->ListViewRowCache[ET_PROCESS_STATISTICS_INDEX_NETWORKWRITEBYTESDELTA])
                    {
                        PPH_STRING value;

                        value = PhFormatSize(block->NetworkSendRawDelta.Delta, ULONG_MAX);
                        wcsncpy_s(dispInfo->item.pszText, dispInfo->item.cchTextMax, value->Buffer, _TRUNCATE);
                        PhDereferenceObject(value);
                    }
                }
            }
        }
        break;
    }
}

PET_PROCESS_BLOCK EtGetProcessBlock(
    _In_ PPH_PROCESS_ITEM ProcessItem
    )
{
    return PhPluginGetObjectExtension(PluginInstance, ProcessItem, EmProcessItemType);
}

PET_NETWORK_BLOCK EtGetNetworkBlock(
    _In_ PPH_NETWORK_ITEM NetworkItem
    )
{
    return PhPluginGetObjectExtension(PluginInstance, NetworkItem, EmNetworkItemType);
}

VOID EtInitializeProcessBlock(
    _Out_ PET_PROCESS_BLOCK Block,
    _In_ PPH_PROCESS_ITEM ProcessItem
    )
{
    ULONG sampleCount;

    memset(Block, 0, sizeof(ET_PROCESS_BLOCK));
    Block->ProcessItem = ProcessItem;
    PhInitializeQueuedLock(&Block->TextCacheLock);

    sampleCount = PhGetIntegerSetting(L"SampleCount");
    PhInitializeCircularBuffer_ULONG64(&Block->DiskReadHistory, sampleCount);
    PhInitializeCircularBuffer_ULONG64(&Block->DiskWriteHistory, sampleCount);
    PhInitializeCircularBuffer_ULONG64(&Block->NetworkSendHistory, sampleCount);
    PhInitializeCircularBuffer_ULONG64(&Block->NetworkReceiveHistory, sampleCount);

    PhInitializeCircularBuffer_FLOAT(&Block->GpuHistory, sampleCount);
    PhInitializeCircularBuffer_ULONG(&Block->MemoryHistory, sampleCount);
    PhInitializeCircularBuffer_ULONG(&Block->MemorySharedHistory, sampleCount);
    PhInitializeCircularBuffer_ULONG(&Block->GpuCommittedHistory, sampleCount);

    //Block->GpuTotalRunningTimeDelta = PhAllocate(sizeof(PH_UINT64_DELTA) * EtGpuTotalNodeCount);
    //memset(Block->GpuTotalRunningTimeDelta, 0, sizeof(PH_UINT64_DELTA) * EtGpuTotalNodeCount);
    //Block->GpuTotalNodesHistory = PhAllocate(sizeof(PH_CIRCULAR_BUFFER_FLOAT) * EtGpuTotalNodeCount);

    InsertTailList(&EtProcessBlockListHead, &Block->ListEntry);
}

VOID EtDeleteProcessBlock(
    _In_ PET_PROCESS_BLOCK Block
    )
{
    PhDeleteCircularBuffer_ULONG64(&Block->DiskReadHistory);
    PhDeleteCircularBuffer_ULONG64(&Block->DiskWriteHistory);
    PhDeleteCircularBuffer_ULONG64(&Block->NetworkSendHistory);
    PhDeleteCircularBuffer_ULONG64(&Block->NetworkReceiveHistory);

    PhDeleteCircularBuffer_ULONG(&Block->GpuCommittedHistory);
    PhDeleteCircularBuffer_ULONG(&Block->MemorySharedHistory);
    PhDeleteCircularBuffer_ULONG(&Block->MemoryHistory);
    PhDeleteCircularBuffer_FLOAT(&Block->GpuHistory);

    RemoveEntryList(&Block->ListEntry);
}

VOID EtInitializeNetworkBlock(
    _Out_ PET_NETWORK_BLOCK Block,
    _In_ PPH_NETWORK_ITEM NetworkItem
    )
{
    memset(Block, 0, sizeof(ET_NETWORK_BLOCK));
    Block->NetworkItem = NetworkItem;
    PhInitializeQueuedLock(&Block->TextCacheLock);
    InsertTailList(&EtNetworkBlockListHead, &Block->ListEntry);
}

VOID EtDeleteNetworkBlock(
    _In_ PET_NETWORK_BLOCK Block
    )
{
    RemoveEntryList(&Block->ListEntry);
}

VOID NTAPI ProcessItemCreateCallback(
    _In_ PVOID Object,
    _In_ PH_EM_OBJECT_TYPE ObjectType,
    _In_ PVOID Extension
    )
{
    EtInitializeProcessBlock(Extension, Object);
}

VOID NTAPI ProcessItemDeleteCallback(
    _In_ PVOID Object,
    _In_ PH_EM_OBJECT_TYPE ObjectType,
    _In_ PVOID Extension
    )
{
    EtDeleteProcessBlock(Extension);
}

VOID NTAPI NetworkItemCreateCallback(
    _In_ PVOID Object,
    _In_ PH_EM_OBJECT_TYPE ObjectType,
    _In_ PVOID Extension
    )
{
    EtInitializeNetworkBlock(Extension, Object);
}

VOID NTAPI NetworkItemDeleteCallback(
    _In_ PVOID Object,
    _In_ PH_EM_OBJECT_TYPE ObjectType,
    _In_ PVOID Extension
    )
{
    EtDeleteNetworkBlock(Extension);
}

LOGICAL DllMain(
    _In_ HINSTANCE Instance,
    _In_ ULONG Reason,
    _Reserved_ PVOID Reserved
    )
{
    switch (Reason)
    {
    case DLL_PROCESS_ATTACH:
        {
            PPH_PLUGIN_INFORMATION info;
            PH_SETTING_CREATE settings[] =
            {
                { StringSettingType, SETTING_NAME_DISK_TREE_LIST_COLUMNS, L"" },
                { IntegerPairSettingType, SETTING_NAME_DISK_TREE_LIST_SORT, L"4,2" }, // 4, DescendingSortOrder
                { IntegerSettingType, SETTING_NAME_ENABLE_GPUPERFCOUNTERS, L"0" },
                { IntegerSettingType, SETTING_NAME_ENABLE_DISKEXT, L"0" },
                { IntegerSettingType, SETTING_NAME_ENABLE_ETW_MONITOR, L"1" },
                { IntegerSettingType, SETTING_NAME_ENABLE_GPU_MONITOR, L"1" },
                { IntegerSettingType, SETTING_NAME_ENABLE_SYSINFO_GRAPHS, L"1" },
                { StringSettingType, SETTING_NAME_GPU_NODE_BITMAP, L"01000000" },
                { IntegerSettingType, SETTING_NAME_GPU_LAST_NODE_COUNT, L"0" },
                { IntegerPairSettingType, SETTING_NAME_UNLOADED_WINDOW_POSITION, L"0,0" },
                { ScalableIntegerPairSettingType, SETTING_NAME_UNLOADED_WINDOW_SIZE, L"@96|350,270" },
                { StringSettingType, SETTING_NAME_UNLOADED_COLUMNS, L"" },
                { IntegerPairSettingType, SETTING_NAME_MODULE_SERVICES_WINDOW_POSITION, L"0,0" },
                { ScalableIntegerPairSettingType, SETTING_NAME_MODULE_SERVICES_WINDOW_SIZE, L"@96|850,490" },
                { StringSettingType, SETTING_NAME_MODULE_SERVICES_COLUMNS, L"" },
                { IntegerPairSettingType, SETTING_NAME_GPU_NODES_WINDOW_POSITION, L"0,0" },
                { ScalableIntegerPairSettingType, SETTING_NAME_GPU_NODES_WINDOW_SIZE, L"@96|850,490" },
                { IntegerPairSettingType, SETTING_NAME_WSWATCH_WINDOW_POSITION, L"0,0" },
                { ScalableIntegerPairSettingType, SETTING_NAME_WSWATCH_WINDOW_SIZE, L"@96|325,266" },
                { StringSettingType, SETTING_NAME_WSWATCH_COLUMNS, L"" },
                { StringSettingType, SETTING_NAME_TRAYICON_GUIDS, L"" },
                { IntegerSettingType, SETTING_NAME_ENABLE_FAHRENHEIT, L"0" },
                { StringSettingType, SETTING_NAME_FW_TREE_LIST_COLUMNS, L"" },
                { IntegerPairSettingType, SETTING_NAME_FW_TREE_LIST_SORT, L"12,2" },
                { IntegerSettingType, SETTING_NAME_FW_IGNORE_PORTSCAN, L"0" }
            };

            PluginInstance = PhRegisterPlugin(PLUGIN_NAME, Instance, &info);

            if (!PluginInstance)
                return FALSE;

            info->DisplayName = L"Extended Tools";
            info->Author = L"dmex, wj32";
            info->Description = L"Extended functionality for Windows 7 and above, including ETW, GPU, Disk and Firewall monitoring tabs.";
            info->Url = L"https://wj32.org/processhacker/forums/viewtopic.php?t=1114";

            PhRegisterCallback(
                PhGetPluginCallback(PluginInstance, PluginCallbackLoad),
                LoadCallback,
                NULL,
                &PluginLoadCallbackRegistration
                );
            PhRegisterCallback(
                PhGetPluginCallback(PluginInstance, PluginCallbackUnload),
                UnloadCallback,
                NULL,
                &PluginUnloadCallbackRegistration
                );
            PhRegisterCallback(
                PhGetGeneralCallback(GeneralCallbackOptionsWindowInitializing),
                ShowOptionsCallback,
                NULL,
                &PluginShowOptionsCallbackRegistration
                );
            PhRegisterCallback(
                PhGetPluginCallback(PluginInstance, PluginCallbackMenuItem),
                MenuItemCallback,
                NULL,
                &PluginMenuItemCallbackRegistration
                );
            PhRegisterCallback(
                PhGetPluginCallback(PluginInstance, PluginCallbackTreeNewMessage),
                TreeNewMessageCallback,
                NULL,
                &PluginTreeNewMessageCallbackRegistration
                );
            PhRegisterCallback(
                PhGetPluginCallback(PluginInstance, PluginCallbackPhSvcRequest),
                PhSvcRequestCallback,
                NULL,
                &PluginPhSvcRequestCallbackRegistration
                );

            PhRegisterCallback(
                PhGetGeneralCallback(GeneralCallbackMainWindowShowing),
                MainWindowShowingCallback,
                NULL,
                &MainWindowShowingCallbackRegistration
                );
            PhRegisterCallback(
                PhGetGeneralCallback(GeneralCallbackProcessesUpdated),
                ProcessesUpdatedCallback,
                NULL,
                &ProcessesUpdatedCallbackRegistration
                );

            PhRegisterCallback(
                PhGetGeneralCallback(GeneralCallbackProcessPropertiesInitializing),
                ProcessPropertiesInitializingCallback,
                NULL,
                &ProcessPropertiesInitializingCallbackRegistration
                );
            PhRegisterCallback(
                PhGetGeneralCallback(GeneralCallbackHandlePropertiesInitializing),
                HandlePropertiesInitializingCallback,
                NULL,
                &HandlePropertiesInitializingCallbackRegistration
                );
            PhRegisterCallback(
                PhGetGeneralCallback(GeneralCallbackProcessMenuInitializing),
                ProcessMenuInitializingCallback,
                NULL,
                &ProcessMenuInitializingCallbackRegistration
                );
            PhRegisterCallback(
                PhGetGeneralCallback(GeneralCallbackThreadMenuInitializing),
                ThreadMenuInitializingCallback,
                NULL,
                &ThreadMenuInitializingCallbackRegistration
                );
            PhRegisterCallback(
                PhGetGeneralCallback(GeneralCallbackModuleMenuInitializing),
                ModuleMenuInitializingCallback,
                NULL,
                &ModuleMenuInitializingCallbackRegistration
                );
            PhRegisterCallback(
                PhGetGeneralCallback(GeneralCallbackProcessTreeNewInitializing),
                ProcessTreeNewInitializingCallback,
                NULL,
                &ProcessTreeNewInitializingCallbackRegistration
                );
            PhRegisterCallback(
                PhGetGeneralCallback(GeneralCallbackNetworkTreeNewInitializing),
                NetworkTreeNewInitializingCallback,
                NULL,
                &NetworkTreeNewInitializingCallbackRegistration
                );
            PhRegisterCallback(
                PhGetGeneralCallback(GeneralCallbackSystemInformationInitializing),
                SystemInformationInitializingCallback,
                NULL,
                &SystemInformationInitializingCallbackRegistration
                );
            PhRegisterCallback(
                PhGetGeneralCallback(GeneralCallbackMiniInformationInitializing),
                MiniInformationInitializingCallback,
                NULL,
                &MiniInformationInitializingCallbackRegistration
                );

            PhRegisterCallback(
                PhGetGeneralCallback(GeneralCallbackTrayIconsInitializing),
                TrayIconsInitializingCallback,
                NULL,
                &TrayIconsInitializingCallbackRegistration
                );

            PhRegisterCallback(
                PhGetGeneralCallback(GeneralCallbackProcessProviderUpdatedEvent),
                ProcessItemsUpdatedCallback,
                NULL,
                &ProcessItemsUpdatedCallbackRegistration
                );
            PhRegisterCallback(
                PhGetGeneralCallback(GeneralCallbackNetworkProviderUpdatedEvent),
                NetworkItemsUpdatedCallback,
                NULL,
                &NetworkItemsUpdatedCallbackRegistration
                );

            PhRegisterCallback(
                PhGetGeneralCallback(GeneralCallbackProcessStatsNotifyEvent),
                ProcessStatsEventCallback,
                NULL,
                &ProcessStatsEventCallbackRegistration
                );

            PhPluginSetObjectExtension(
                PluginInstance,
                EmProcessItemType,
                sizeof(ET_PROCESS_BLOCK),
                ProcessItemCreateCallback,
                ProcessItemDeleteCallback
                );
            PhPluginSetObjectExtension(
                PluginInstance,
                EmNetworkItemType,
                sizeof(ET_NETWORK_BLOCK),
                NetworkItemCreateCallback,
                NetworkItemDeleteCallback
                );

            PhAddSettings(settings, RTL_NUMBER_OF(settings));
        }
        break;
    }

    return TRUE;
}
