/*
 * Process Hacker -
 *   minidump writer
 *
 * Copyright (C) 2010-2015 wj32
 * Copyright (C) 2016-2021 dmex
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
#include <apiimport.h>
#include <appresolver.h>

#include <dbghelp.h>
#include <processsnapshot.h>
#include <symprv.h>

#include <actions.h>
#include <phsvccl.h>
#include <procprv.h>

#define WM_PH_MINIDUMP_STATUS_UPDATE (WM_APP + 301)

#define PH_MINIDUMP_STATUS_UPDATE 1
#define PH_MINIDUMP_COMPLETED 2
#define PH_MINIDUMP_ERROR 3

typedef struct _PROCESS_MINIDUMP_CONTEXT
{
    HANDLE ProcessId;
    PWSTR FileName;
    MINIDUMP_TYPE DumpType;
    BOOLEAN IsWow64;
    BOOLEAN IsProcessSnapshot;

    HANDLE ProcessHandle;
    HANDLE FileHandle;

    HWND WindowHandle;
    BOOLEAN Stop;
    BOOLEAN Succeeded;

    ULONG64 LastTickCount;
} PROCESS_MINIDUMP_CONTEXT, *PPROCESS_MINIDUMP_CONTEXT;

BOOLEAN PhpCreateProcessMiniDumpWithProgress(
    _In_ HWND hWnd,
    _In_ HANDLE ProcessId,
    _In_ PWSTR FileName
    );

INT_PTR CALLBACK PhpProcessMiniDumpDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    );

BOOLEAN PhUiCreateDumpFileProcess(
    _In_ HWND hWnd,
    _In_ PPH_PROCESS_ITEM Process
    )
{
    static PH_FILETYPE_FILTER filters[] =
    {
        { L"Dump files (*.dmp)", L"*.dmp" },
        { L"All files (*.*)", L"*.*" }
    };
    PVOID fileDialog;
    PPH_STRING fileName;

    fileDialog = PhCreateSaveFileDialog();
    PhSetFileDialogFilter(fileDialog, filters, sizeof(filters) / sizeof(PH_FILETYPE_FILTER));
    PhSetFileDialogFileName(fileDialog, PhaConcatStrings2(Process->ProcessName->Buffer, L".dmp")->Buffer);

    if (!PhShowFileDialog(hWnd, fileDialog))
    {
        PhFreeFileDialog(fileDialog);
        return FALSE;
    }

    fileName = PH_AUTO(PhGetFileDialogFileName(fileDialog));
    PhFreeFileDialog(fileDialog);

    return PhpCreateProcessMiniDumpWithProgress(
        hWnd,
        Process->ProcessId,
        fileName->Buffer
        );
}

BOOLEAN PhpCreateProcessMiniDumpWithProgress(
    _In_ HWND hWnd,
    _In_ HANDLE ProcessId,
    _In_ PWSTR FileName
    )
{
    NTSTATUS status;
    PROCESS_MINIDUMP_CONTEXT context;

    memset(&context, 0, sizeof(PROCESS_MINIDUMP_CONTEXT));
    context.ProcessId = ProcessId;
    context.FileName = FileName;

    // task manager uses these flags (wj32)
    if (WindowsVersion >= WINDOWS_10)
    {
        context.DumpType =
            MiniDumpWithFullMemory |
            MiniDumpWithHandleData |
            MiniDumpWithUnloadedModules |
            MiniDumpWithFullMemoryInfo |
            MiniDumpWithThreadInfo |
            MiniDumpIgnoreInaccessibleMemory |
            MiniDumpWithIptTrace;

        if (!NT_SUCCESS(status = PhOpenProcess(
            &context.ProcessHandle,
            PROCESS_ALL_ACCESS,
            ProcessId
            )))
        {
            goto LimitedDump;
        }
    }
    else
    {
LimitedDump:
        context.DumpType =
            MiniDumpWithFullMemory |
            MiniDumpWithHandleData |
            MiniDumpWithUnloadedModules |
            MiniDumpWithFullMemoryInfo |
            MiniDumpWithThreadInfo;

        if (!NT_SUCCESS(status = PhOpenProcess(
            &context.ProcessHandle,
            PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
            ProcessId
            )))
        {
            PhShowStatus(hWnd, L"Unable to open the process", status, 0);
            return FALSE;
        }
    }

#ifdef _WIN64
    PhGetProcessIsWow64(context.ProcessHandle, &context.IsWow64);
#endif

    status = PhCreateFileWin32(
        &context.FileHandle,
        FileName,
        FILE_GENERIC_WRITE | DELETE,
        FILE_ATTRIBUTE_NORMAL,
        0,
        FILE_OVERWRITE_IF,
        FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT
        );

    if (!NT_SUCCESS(status))
    {
        PhShowStatus(hWnd, L"Unable to access the dump file", status, 0);
        NtClose(context.ProcessHandle);
        return FALSE;
    }

    DialogBoxParam(
        PhInstanceHandle,
        MAKEINTRESOURCE(IDD_PROGRESS),
        hWnd,
        PhpProcessMiniDumpDlgProc,
        (LPARAM)&context
        );

    NtClose(context.FileHandle);
    NtClose(context.ProcessHandle);

    return context.Succeeded;
}

static BOOL CALLBACK PhpProcessMiniDumpCallback(
    _In_ PVOID CallbackParam,
    _In_ const PMINIDUMP_CALLBACK_INPUT CallbackInput,
    _Inout_ PMINIDUMP_CALLBACK_OUTPUT CallbackOutput
    )
{
    PPROCESS_MINIDUMP_CONTEXT context = CallbackParam;
    PPH_STRING message = NULL;

    // Don't try to send status updates if we're creating a dump of the current process.
    if (context->ProcessId == NtCurrentProcessId())
        return TRUE;

    // MiniDumpWriteDump seems to get bored of calling the callback
    // after it begins dumping the process handles. The code is
    // still here in case they fix this problem in the future.

    switch (CallbackInput->CallbackType)
    {
    case CancelCallback:
        {
            if (context->Stop)
                CallbackOutput->Cancel = TRUE;
        }
        break;
    case IsProcessSnapshotCallback:
        {
            if (context->IsProcessSnapshot)
                CallbackOutput->Status = S_FALSE;
        }
        break;
    case ModuleCallback:
        {
            message = PhFormatString(L"Processing module %s...", CallbackInput->Module.FullPath);
        }
        break;
    case ThreadCallback:
        {
            message = PhFormatString(L"Processing thread %u...", CallbackInput->Thread.ThreadId);
        }
        break;
    }

    if (message)
    {
        SendMessage(
            context->WindowHandle,
            WM_PH_MINIDUMP_STATUS_UPDATE,
            PH_MINIDUMP_STATUS_UPDATE,
            (LPARAM)message->Buffer
            );
        PhDereferenceObject(message);
    }

    return TRUE;
}

NTSTATUS PhpProcessMiniDumpThreadStart(
    _In_ PVOID Parameter
    )
{
    PPROCESS_MINIDUMP_CONTEXT context = Parameter;
    MINIDUMP_CALLBACK_INFORMATION callbackInfo;
    HPSS snapshotHandle = NULL;

    callbackInfo.CallbackRoutine = PhpProcessMiniDumpCallback;
    callbackInfo.CallbackParam = context;

#ifdef _WIN64
    if (context->IsWow64)
    {
        if (PhUiConnectToPhSvcEx(NULL, Wow64PhSvcMode, FALSE))
        {
            NTSTATUS status;

            if (NT_SUCCESS(status = PhSvcCallWriteMiniDumpProcess(
                context->ProcessHandle,
                context->ProcessId,
                context->FileHandle,
                context->DumpType
                )))
            {
                context->Succeeded = TRUE;
            }
            else
            {
                SendMessage(
                    context->WindowHandle,
                    WM_PH_MINIDUMP_STATUS_UPDATE,
                    PH_MINIDUMP_ERROR,
                    (LPARAM)PhNtStatusToDosError(status)
                    );
            }

            PhUiDisconnectFromPhSvc();

            goto Completed;
        }
        else
        {
            if (PhShowMessage2(
                context->WindowHandle,
                TDCBF_YES_BUTTON | TDCBF_NO_BUTTON,
                TD_WARNING_ICON,
                L"The 32-bit version of Process Hacker could not be located.",
                L"A 64-bit dump will be created instead. Do you want to continue?"
                ) == IDNO)
            {
                PhDeleteFile(context->FileHandle);
                goto Completed;
            }
        }
    }
#endif

    if (PssCaptureSnapshot_Import())
    {
        PssCaptureSnapshot_Import()(
            context->ProcessHandle,
            PSS_CAPTURE_VA_CLONE | PSS_CAPTURE_VA_SPACE | PSS_CAPTURE_VA_SPACE_SECTION_INFORMATION |
            PSS_CAPTURE_IPT_TRACE | PSS_CAPTURE_HANDLE_TRACE | PSS_CAPTURE_HANDLES | PSS_CAPTURE_HANDLE_BASIC_INFORMATION |
            PSS_CAPTURE_HANDLE_TYPE_SPECIFIC_INFORMATION | PSS_CAPTURE_HANDLE_NAME_INFORMATION |
            PSS_CAPTURE_THREADS | PSS_CAPTURE_THREAD_CONTEXT | PSS_CREATE_USE_VM_ALLOCATIONS,
            CONTEXT_ALL,
            &snapshotHandle
            );
            
        if (snapshotHandle)
        {
            context->IsProcessSnapshot = TRUE;
        }
    }

    // Note: Task Manager will disable package tasks and signal the crashdump task. (dmex)
    // TODO: Determine if we should uncomment this code and do the same?
    //HANDLE packageTaskHandle = NULL;
    //PhAppResolverPackageStopSessionRedirection(context->PackageFullName);
    //PhAppResolverBeginCrashDumpTask(context->ProcessId, &packageTaskHandle);

    if (PhWriteMiniDumpProcess(
        snapshotHandle ? snapshotHandle : context->ProcessHandle,
        context->ProcessId,
        context->FileHandle,
        context->DumpType,
        NULL,
        NULL,
        &callbackInfo
        ))
    {
        context->Succeeded = TRUE;
    }
    else
    {
        SendMessage(
            context->WindowHandle,
            WM_PH_MINIDUMP_STATUS_UPDATE,
            PH_MINIDUMP_ERROR,
            (LPARAM)GetLastError()
            );
    }

    if (snapshotHandle)
    {
        PSS_VA_CLONE_INFORMATION processInfo;

        if (PssQuerySnapshot_Import() && PssQuerySnapshot_Import()(
            snapshotHandle,
            PSS_QUERY_VA_CLONE_INFORMATION,
            &processInfo,
            sizeof(PSS_VA_CLONE_INFORMATION)
            ) == ERROR_SUCCESS)
        {
            NtClose(processInfo.VaCloneHandle);
        }

        if (PssFreeSnapshot_Import())
        {
            PssFreeSnapshot_Import()(context->ProcessHandle, snapshotHandle);
        }
    }

#ifdef _WIN64
Completed:
#endif
    SendMessage(
        context->WindowHandle,
        WM_PH_MINIDUMP_STATUS_UPDATE,
        PH_MINIDUMP_COMPLETED,
        0
        );

    //if (packageTaskHandle)
    //    PhAppResolverEndCrashDumpTask(packageTaskHandle);

    return STATUS_SUCCESS;
}

INT_PTR CALLBACK PhpProcessMiniDumpDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    PPROCESS_MINIDUMP_CONTEXT context;

    if (uMsg == WM_INITDIALOG)
    {
        context = (PPROCESS_MINIDUMP_CONTEXT)lParam;

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
            PhCenterWindow(hwndDlg, GetParent(hwndDlg));

            PhSetDialogItemText(hwndDlg, IDC_PROGRESSTEXT, L"Creating the dump file...");

            PhSetWindowStyle(GetDlgItem(hwndDlg, IDC_PROGRESS), PBS_MARQUEE, PBS_MARQUEE);
            SendMessage(GetDlgItem(hwndDlg, IDC_PROGRESS), PBM_SETMARQUEE, TRUE, 75);

            context->WindowHandle = hwndDlg;

            PhCreateThread2(PhpProcessMiniDumpThreadStart, context);

            SetTimer(hwndDlg, 1, 500, NULL);
        }
        break;
    case WM_DESTROY:
        {
            PhRemoveWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT);
        }
        break;
    case WM_COMMAND:
        {
            switch (GET_WM_COMMAND_ID(wParam, lParam))
            {
            case IDCANCEL:
                {
                    EnableWindow(GetDlgItem(hwndDlg, IDCANCEL), FALSE);
                    context->Stop = TRUE;
                }
                break;
            }
        }
        break;
    case WM_TIMER:
        {
            if (wParam == 1)
            {
                ULONG64 currentTickCount;

                currentTickCount = NtGetTickCount64();

                if (currentTickCount - context->LastTickCount >= 2000)
                {
                    // No status message update for 2 seconds.

                    PhSetDialogItemText(hwndDlg, IDC_PROGRESSTEXT, L"Creating the dump file...");

                    context->LastTickCount = currentTickCount;
                }
            }
        }
        break;
    case WM_PH_MINIDUMP_STATUS_UPDATE:
        {
            switch (wParam)
            {
            case PH_MINIDUMP_STATUS_UPDATE:
                PhSetDialogItemText(hwndDlg, IDC_PROGRESSTEXT, (PWSTR)lParam);
                context->LastTickCount = NtGetTickCount64();
                break;
            case PH_MINIDUMP_ERROR:
                PhShowStatus(hwndDlg, L"Unable to create the minidump", 0, (ULONG)lParam);
                break;
            case PH_MINIDUMP_COMPLETED:
                EndDialog(hwndDlg, IDOK);
                break;
            }
        }
        break;
    }

    return FALSE;
}
