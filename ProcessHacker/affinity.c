/*
 * Process Hacker -
 *   process affinity editor
 *
 * Copyright (C) 2010-2015 wj32
 * Copyright (C) 2020-2021 dmex
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

/*
 * The affinity dialog was originally created to support the modification
 * of process affinity masks, but now supports modifying thread affinity
 * and generic masks.
 */

#include <phapp.h>
#include <phsettings.h>
#include <procprv.h>
#include <thrdprv.h>

typedef struct _PH_AFFINITY_DIALOG_CONTEXT
{
    PPH_PROCESS_ITEM ProcessItem;
    PPH_THREAD_ITEM ThreadItem;
    ULONG_PTR NewAffinityMask;

    // Multiple selected items (dmex)
    PPH_THREAD_ITEM* Threads;
    ULONG NumberOfThreads;
    PHANDLE ThreadHandles;
} PH_AFFINITY_DIALOG_CONTEXT, *PPH_AFFINITY_DIALOG_CONTEXT;

INT_PTR CALLBACK PhpProcessAffinityDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    );

VOID PhShowProcessAffinityDialog(
    _In_ HWND ParentWindowHandle,
    _In_opt_ PPH_PROCESS_ITEM ProcessItem,
    _In_opt_ PPH_THREAD_ITEM ThreadItem
    )
{
    PH_AFFINITY_DIALOG_CONTEXT context;

    assert(!!ProcessItem != !!ThreadItem); // make sure we have one and not the other (wj32)

    memset(&context, 0, sizeof(PH_AFFINITY_DIALOG_CONTEXT));
    context.ProcessItem = ProcessItem;
    context.ThreadItem = ThreadItem;

    DialogBoxParam(
        PhInstanceHandle,
        MAKEINTRESOURCE(IDD_AFFINITY),
        ParentWindowHandle,
        PhpProcessAffinityDlgProc,
        (LPARAM)&context
        );
}

_Success_(return)
BOOLEAN PhShowProcessAffinityDialog2(
    _In_ HWND ParentWindowHandle,
    _In_ PPH_PROCESS_ITEM ProcessItem,
    _Out_ PULONG_PTR NewAffinityMask
    )
{
    PH_AFFINITY_DIALOG_CONTEXT context;

    memset(&context, 0, sizeof(PH_AFFINITY_DIALOG_CONTEXT));
    context.ProcessItem = ProcessItem;
    context.ThreadItem = NULL;

    if (DialogBoxParam(
        PhInstanceHandle,
        MAKEINTRESOURCE(IDD_AFFINITY),
        ParentWindowHandle,
        PhpProcessAffinityDlgProc,
        (LPARAM)&context
        ) == IDOK)
    {
        *NewAffinityMask = context.NewAffinityMask;

        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

VOID PhShowThreadAffinityDialog(
    _In_ HWND ParentWindowHandle,
    _In_ PPH_THREAD_ITEM* Threads,
    _In_ ULONG NumberOfThreads
    )
{
    PH_AFFINITY_DIALOG_CONTEXT context;

    memset(&context, 0, sizeof(PH_AFFINITY_DIALOG_CONTEXT));
    context.Threads = Threads;
    context.NumberOfThreads = NumberOfThreads;
    context.ThreadHandles = PhAllocateZero(NumberOfThreads * sizeof(HANDLE));

    // Cache handles to each thread since the ThreadId gets 
    // reassigned to a different process after the thread exits. (dmex)
    for (ULONG i = 0; i < NumberOfThreads; i++)
    {
        PhOpenThread(
            &context.ThreadHandles[i],
            THREAD_QUERY_LIMITED_INFORMATION | THREAD_SET_LIMITED_INFORMATION,
            Threads[i]->ThreadId
            );
    }

    DialogBoxParam(
        PhInstanceHandle,
        MAKEINTRESOURCE(IDD_AFFINITY),
        ParentWindowHandle,
        PhpProcessAffinityDlgProc,
        (LPARAM)&context
        );
}

static BOOLEAN PhpShowThreadErrorAffinity(
    _In_ HWND hWnd,
    _In_ PPH_THREAD_ITEM Thread,
    _In_ NTSTATUS Status,
    _In_opt_ ULONG Win32Result
    )
{
    return PhShowContinueStatus(
        hWnd,
        PhaFormatString(
        L"Unable to change affinity of thread %lu",
        HandleToUlong(Thread->ThreadId)
        )->Buffer,
        Status,
        Win32Result
        );
}

BOOLEAN PhpCheckThreadsHaveSameAffinity(
    _In_ PPH_AFFINITY_DIALOG_CONTEXT Context
    )
{
    BOOLEAN result = TRUE;
    THREAD_BASIC_INFORMATION basicInfo;
    ULONG_PTR lastAffinityMask = 0;
    ULONG_PTR affinityMask = 0;

    if (NT_SUCCESS(PhGetThreadBasicInformation(Context->ThreadHandles[0], &basicInfo)))
    {
        lastAffinityMask = basicInfo.AffinityMask;
    }

    for (ULONG i = 0; i < Context->NumberOfThreads; i++)
    {
        if (!Context->ThreadHandles[i])
            continue;

        if (NT_SUCCESS(PhGetThreadBasicInformation(Context->ThreadHandles[i], &basicInfo)))
        {
            affinityMask = basicInfo.AffinityMask;
        }

        if (lastAffinityMask != affinityMask)
        {
            result = FALSE;
            break;
        }
    }

    return result;
}

INT_PTR CALLBACK PhpProcessAffinityDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    PPH_AFFINITY_DIALOG_CONTEXT context = NULL;

    if (uMsg == WM_INITDIALOG)
    {
        context = (PPH_AFFINITY_DIALOG_CONTEXT)lParam;
        PhSetWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT, context);
    }
    else
    {
        context = PhGetWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT);
    }

    if (!context)
        return FALSE;

    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            NTSTATUS status = STATUS_UNSUCCESSFUL;
            BOOLEAN differentAffinity = FALSE;
            ULONG_PTR systemAffinityMask = 0;
            ULONG_PTR affinityMask = 0;
            ULONG i;

            PhCenterWindow(hwndDlg, GetParent(hwndDlg));

            if (context->ProcessItem)
            {
                HANDLE processHandle;
                PROCESS_BASIC_INFORMATION basicInfo;

                if (NT_SUCCESS(status = PhOpenProcess(
                    &processHandle,
                    PROCESS_QUERY_LIMITED_INFORMATION,
                    context->ProcessItem->ProcessId
                    )))
                {
                    status = PhGetProcessBasicInformation(processHandle, &basicInfo);

                    if (NT_SUCCESS(status))
                        affinityMask = basicInfo.AffinityMask;

                    NtClose(processHandle);
                }
            }
            else if (context->ThreadItem)
            {
                HANDLE threadHandle;
                THREAD_BASIC_INFORMATION basicInfo;
                HANDLE processHandle;
                PROCESS_BASIC_INFORMATION processBasicInfo;

                if (NT_SUCCESS(status = PhOpenThread(
                    &threadHandle,
                    THREAD_QUERY_LIMITED_INFORMATION,
                    context->ThreadItem->ThreadId
                    )))
                {
                    status = PhGetThreadBasicInformation(threadHandle, &basicInfo);

                    if (NT_SUCCESS(status))
                    {
                        affinityMask = basicInfo.AffinityMask;

                        // A thread's affinity mask is restricted by the process affinity mask,
                        // so use that as the system affinity mask. (wj32)

                        if (NT_SUCCESS(PhOpenProcess(
                            &processHandle,
                            PROCESS_QUERY_LIMITED_INFORMATION,
                            basicInfo.ClientId.UniqueProcess
                            )))
                        {
                            if (NT_SUCCESS(PhGetProcessBasicInformation(processHandle, &processBasicInfo)))
                                systemAffinityMask = processBasicInfo.AffinityMask;

                            NtClose(processHandle);
                        }
                    }

                    NtClose(threadHandle);
                }
            }
            else if (context->Threads)
            {
                THREAD_BASIC_INFORMATION basicInfo;
                HANDLE processHandle;
                PROCESS_BASIC_INFORMATION processBasicInfo;
                PPH_STRING windowText;

                windowText = PH_AUTO(PhGetWindowText(hwndDlg));
                PhSetWindowText(hwndDlg, PhaFormatString(
                    L"%s (%lu threads)",
                    windowText->Buffer,
                    context->NumberOfThreads
                    )->Buffer);

                differentAffinity = !PhpCheckThreadsHaveSameAffinity(context);

                // Use affinity from the first thread when all threads are identical (dmex)
                status = PhGetThreadBasicInformation(
                    context->ThreadHandles[0],
                    &basicInfo
                    );

                if (NT_SUCCESS(status))
                {
                    affinityMask = basicInfo.AffinityMask;

                    if (NT_SUCCESS(PhOpenProcess(
                        &processHandle,
                        PROCESS_QUERY_LIMITED_INFORMATION,
                        basicInfo.ClientId.UniqueProcess
                        )))
                    {
                        if (NT_SUCCESS(PhGetProcessBasicInformation(processHandle, &processBasicInfo)))
                            systemAffinityMask = processBasicInfo.AffinityMask;

                        NtClose(processHandle);
                    }
                }
            }

            if (NT_SUCCESS(status) && systemAffinityMask == 0)
            {
                SYSTEM_BASIC_INFORMATION systemBasicInfo;

                status = NtQuerySystemInformation(
                    SystemBasicInformation,
                    &systemBasicInfo,
                    sizeof(SYSTEM_BASIC_INFORMATION),
                    NULL
                    );

                if (NT_SUCCESS(status))
                    systemAffinityMask = systemBasicInfo.ActiveProcessorsAffinityMask;
            }

            if (!NT_SUCCESS(status))
            {
                PhShowStatus(hwndDlg, L"Unable to retrieve the affinity", status, 0);
                EndDialog(hwndDlg, IDCANCEL);
                break;
            }

            // Disable the CPU checkboxes which aren't part of the system affinity mask,
            // and check the CPU checkboxes which are part of the affinity mask. (wj32)

            for (i = 0; i < 8 * 8; i++)
            {
                if ((i < sizeof(ULONG_PTR) * 8) && ((systemAffinityMask >> i) & 0x1))
                {
                    if (differentAffinity) // Skip for multiple selection (dmex)
                        continue;

                    if ((affinityMask >> i) & 0x1)
                    {
                        Button_SetCheck(GetDlgItem(hwndDlg, IDC_CPU0 + i), BST_CHECKED);
                    }
                }
                else
                {
                    EnableWindow(GetDlgItem(hwndDlg, IDC_CPU0 + i), FALSE);
                }
            }

            PhInitializeWindowTheme(hwndDlg, PhEnableThemeSupport);
        }
        break;
    case WM_DESTROY:
        {
            PhRemoveWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT);

            if (context->ThreadHandles)
            {
                for (ULONG i = 0; i < context->NumberOfThreads; i++)
                {
                    if (context->ThreadHandles[i])
                    {
                        NtClose(context->ThreadHandles[i]);
                        context->ThreadHandles[i] = NULL;
                    }
                }

                PhFree(context->ThreadHandles);
            }
        }
        break;
    case WM_COMMAND:
        {
            switch (GET_WM_COMMAND_ID(wParam, lParam))
            {
            case IDCANCEL:
                EndDialog(hwndDlg, IDCANCEL);
                break;
            case IDOK:
                {
                    NTSTATUS status = STATUS_UNSUCCESSFUL;
                    ULONG i;
                    ULONG_PTR affinityMask;

                    // Work out the affinity mask.

                    affinityMask = 0;

                    for (i = 0; i < sizeof(ULONG_PTR) * 8; i++)
                    {
                        if (Button_GetCheck(GetDlgItem(hwndDlg, IDC_CPU0 + i)) == BST_CHECKED)
                            affinityMask |= (ULONG_PTR)1 << i;
                    }

                    if (context->ProcessItem)
                    {
                        HANDLE processHandle;

                        if (NT_SUCCESS(status = PhOpenProcess(
                            &processHandle,
                            PROCESS_SET_INFORMATION,
                            context->ProcessItem->ProcessId
                            )))
                        {
                            status = PhSetProcessAffinityMask(processHandle, affinityMask);
                            NtClose(processHandle);
                        }

                        if (NT_SUCCESS(status))
                        {
                            context->NewAffinityMask = affinityMask;
                        }
                    }
                    else if (context->ThreadItem)
                    {
                        HANDLE threadHandle;

                        if (NT_SUCCESS(status = PhOpenThread(
                            &threadHandle,
                            THREAD_SET_LIMITED_INFORMATION,
                            context->ThreadItem->ThreadId
                            )))
                        {
                            status = PhSetThreadAffinityMask(threadHandle, affinityMask);
                            NtClose(threadHandle);
                        }
                    }
                    else if (context->Threads)
                    {
                        for (ULONG i = 0; i < context->NumberOfThreads; i++)
                        {
                            if (!context->ThreadHandles[i])
                                continue;

                            status = PhSetThreadAffinityMask(context->ThreadHandles[i], affinityMask);
                       
                            //if (!NT_SUCCESS(status))
                            //{
                            //    if (!PhpShowThreadErrorAffinity(hwndDlg, context->Threads[i], status, 0))
                            //        break;
                            //}
                        }
                    }

                    if (NT_SUCCESS(status))
                        EndDialog(hwndDlg, IDOK);
                    else
                        PhShowStatus(hwndDlg, L"Unable to set the affinity", status, 0);
                }
                break;
            case IDC_SELECTALL:
            case IDC_DESELECTALL:
                {
                    for (ULONG i = 0; i < sizeof(ULONG_PTR) * 8; i++)
                    {
                        HWND checkBox = GetDlgItem(hwndDlg, IDC_CPU0 + i);

                        if (IsWindowEnabled(checkBox))
                            Button_SetCheck(checkBox, GET_WM_COMMAND_ID(wParam, lParam) == IDC_SELECTALL ? BST_CHECKED : BST_UNCHECKED);
                    }
                }
                break;
            }
        }
        break;
    case WM_CTLCOLORBTN:
        return HANDLE_WM_CTLCOLORBTN(hwndDlg, wParam, lParam, PhWindowThemeControlColor);
    case WM_CTLCOLORDLG:
        return HANDLE_WM_CTLCOLORDLG(hwndDlg, wParam, lParam, PhWindowThemeControlColor);
    case WM_CTLCOLORSTATIC:
        return HANDLE_WM_CTLCOLORSTATIC(hwndDlg, wParam, lParam, PhWindowThemeControlColor);
    }

    return FALSE;
}

// Note: Workaround for UserNotes plugin dialog overrides (dmex)
NTSTATUS PhSetProcessItemAffinityMask(
    _In_ PPH_PROCESS_ITEM ProcessItem,
    _In_ ULONG_PTR AffinityMask
    )
{
    NTSTATUS status;
    HANDLE processHandle;

    status = PhOpenProcess(
        &processHandle,
        PROCESS_SET_INFORMATION,
        ProcessItem->ProcessId
        );

    if (NT_SUCCESS(status))
    {
        status = PhSetProcessAffinityMask(processHandle, AffinityMask);
        NtClose(processHandle);
    }

    return status;
}

// Note: Workaround for UserNotes plugin dialog overrides (dmex)
NTSTATUS PhSetProcessItemPagePriority(
    _In_ PPH_PROCESS_ITEM ProcessItem,
    _In_ ULONG PagePriority
    )
{
    NTSTATUS status;
    HANDLE processHandle;

    status = PhOpenProcess(
        &processHandle,
        PROCESS_SET_INFORMATION,
        ProcessItem->ProcessId
        );

    if (NT_SUCCESS(status))
    {
        status = PhSetProcessPagePriority(processHandle, PagePriority);
        NtClose(processHandle);
    }

    return status;
}

// Note: Workaround for UserNotes plugin dialog overrides (dmex)
NTSTATUS PhSetProcessItemIoPriority(
    _In_ PPH_PROCESS_ITEM ProcessItem,
    _In_ IO_PRIORITY_HINT IoPriority
    )
{
    NTSTATUS status;
    HANDLE processHandle;

    status = PhOpenProcess(
        &processHandle,
        PROCESS_SET_INFORMATION,
        ProcessItem->ProcessId
        );

    if (NT_SUCCESS(status))
    {
        status = PhSetProcessIoPriority(processHandle, IoPriority);
        NtClose(processHandle);
    }

    return status;
}

// Note: Workaround for UserNotes plugin dialog overrides (dmex)
NTSTATUS PhSetProcessItemPriority(
    _In_ PPH_PROCESS_ITEM ProcessItem,
    _In_ PROCESS_PRIORITY_CLASS PriorityClass
    )
{
    NTSTATUS status;
    HANDLE processHandle;

    status = PhOpenProcess(
        &processHandle,
        PROCESS_SET_INFORMATION,
        ProcessItem->ProcessId
        );

    if (NT_SUCCESS(status))
    {
        status = PhSetProcessPriority(processHandle, PriorityClass);
        NtClose(processHandle);
    }

    return status;
}
