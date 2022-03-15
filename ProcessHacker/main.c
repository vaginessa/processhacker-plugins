/*
 * Process Hacker -
 *   main program
 *
 * Copyright (C) 2009-2016 wj32
 * Copyright (C) 2017-2022 dmex
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
#include <colorbox.h>
#include <hexedit.h>
#include <hndlinfo.h>
#include <kphuser.h>

#include <extmgri.h>
#include <mainwnd.h>
#include <netprv.h>
#include <phsvc.h>
#include <procprv.h>

#include <shlobj.h>

#include <settings.h>
#include <srvprv.h>

LONG PhMainMessageLoop(
    VOID
    );

VOID PhActivatePreviousInstance(
    VOID
    );

VOID PhInitializeCommonControls(
    VOID
    );

VOID PhInitializeKph(
    VOID
    );

BOOLEAN PhInitializeAppSystem(
    VOID
    );

VOID PhpInitializeSettings(
    VOID
    );

VOID PhpProcessStartupParameters(
    VOID
    );

VOID PhpEnablePrivileges(
    VOID
    );

BOOLEAN PhInitializeDirectoryPolicy(
    VOID
    );

BOOLEAN PhInitializeExceptionPolicy(
    VOID
    );

BOOLEAN PhInitializeNamespacePolicy(
    VOID
    );

BOOLEAN PhInitializeMitigationPolicy(
    VOID
    );

BOOLEAN PhInitializeMitigationSignaturePolicy(
    VOID
    );

BOOLEAN PhInitializeComPolicy(
    VOID
    );

BOOLEAN PhPluginsEnabled = FALSE;
PPH_STRING PhSettingsFileName = NULL;
PH_STARTUP_PARAMETERS PhStartupParameters;

PH_PROVIDER_THREAD PhPrimaryProviderThread;
PH_PROVIDER_THREAD PhSecondaryProviderThread;

static PPH_LIST DialogList = NULL;
static PPH_LIST FilterList = NULL;
static PH_AUTO_POOL BaseAutoPool;

INT WINAPI wWinMain(
    _In_ HINSTANCE Instance,
    _In_opt_ HINSTANCE PrevInstance,
    _In_ PWSTR CmdLine,
    _In_ INT CmdShow
    )
{
    LONG result;
#ifdef DEBUG
    PHP_BASE_THREAD_DBG dbg;
#endif

    if (!NT_SUCCESS(PhInitializePhLibEx(L"Process Hacker", ULONG_MAX, Instance, 0, 0)))
        return 1;
    if (!PhInitializeDirectoryPolicy())
        return 1;
    if (!PhInitializeExceptionPolicy())
        return 1;
    if (!PhInitializeNamespacePolicy())
        return 1;
    if (!PhInitializeMitigationPolicy())
        return 1;
    if (!PhInitializeComPolicy())
        return 1;

    PhpProcessStartupParameters();
    PhpEnablePrivileges();

    if (PhStartupParameters.RunAsServiceMode)
    {
        PhExitApplication(PhRunAsServiceStart(PhStartupParameters.RunAsServiceMode));
    }

    PhSettingsInitialization();
    PhpInitializeSettings();

    if (PhGetIntegerSetting(L"AllowOnlyOneInstance") &&
        !PhStartupParameters.NewInstance &&
        !PhStartupParameters.ShowOptions &&
        !PhStartupParameters.PhSvc)
    {
        PhActivatePreviousInstance();
    }

    if (PhGetIntegerSetting(L"EnableStartAsAdmin") &&
        !PhStartupParameters.NewInstance &&
        !PhStartupParameters.ShowOptions &&
        !PhStartupParameters.PhSvc)
    {
        if (!PhGetOwnTokenAttributes().Elevated)
        {
            AllowSetForegroundWindow(ASFW_ANY);

            if (SUCCEEDED(PhRunAsAdminTask(L"ProcessHackerTaskAdmin")))
            {
                PhActivatePreviousInstance();
                PhExitApplication(STATUS_SUCCESS);
            }
        }
    }

    if (PhGetIntegerSetting(L"EnableKph") &&
        !PhStartupParameters.NoKph &&
        !PhIsExecutingInWow64()
        )
    {
        PhInitializeKph();
    }

#ifdef DEBUG
    dbg.ClientId = NtCurrentTeb()->ClientId;
    dbg.StartAddress = wWinMain;
    dbg.Parameter = NULL;
    InsertTailList(&PhDbgThreadListHead, &dbg.ListEntry);
    TlsSetValue(PhDbgThreadDbgTlsIndex, &dbg);
#endif

    PhInitializeAutoPool(&BaseAutoPool);

    PhInitializeCommonControls();
    PhGuiSupportInitialization();
    PhTreeNewInitialization();
    PhGraphControlInitialization();
    PhHexEditInitialization();
    PhColorBoxInitialization();

    PhInitializeAppSystem();
    PhInitializeCallbacks();
    PhEmInitialization();

    if (PhStartupParameters.ShowOptions)
    {
        PhShowOptionsDialog(PhStartupParameters.WindowHandle);
        PhExitApplication(STATUS_SUCCESS);
    }

    if (PhPluginsEnabled && !PhStartupParameters.NoPlugins)
    {
        PhLoadPlugins();
    }

    PhInitializeMitigationSignaturePolicy();

    if (PhStartupParameters.PhSvc)
    {
        MSG message;

        // Turn the feedback cursor off.
        PostMessage(NULL, WM_NULL, 0, 0);
        GetMessage(&message, NULL, 0, 0);

        PhExitApplication(PhSvcMain(NULL, NULL));
    }

#ifndef DEBUG
    if (PhIsExecutingInWow64())
    {
        PhShowWarning(
            NULL,
            L"%s",
            L"You are attempting to run the 32-bit version of Process Hacker on 64-bit Windows. "
            L"Most features will not work correctly.\n\n"
            L"Please run the 64-bit version of Process Hacker instead."
            );
        PhExitApplication(STATUS_IMAGE_SUBSYSTEM_NOT_PRESENT);
    }
#endif

    // Set the default priority.
    {
        PROCESS_PRIORITY_CLASS priorityClass;

        priorityClass.Foreground = FALSE;
        priorityClass.PriorityClass = PROCESS_PRIORITY_CLASS_HIGH;

        if (PhStartupParameters.PriorityClass != 0)
            priorityClass.PriorityClass = (UCHAR)PhStartupParameters.PriorityClass;

        PhSetProcessPriority(NtCurrentProcess(), priorityClass);
    }

    if (!PhMainWndInitialization(CmdShow))
    {
        PhShowError(NULL, L"%s", L"Unable to initialize the main window.");
        return 1;
    }

    PhDrainAutoPool(&BaseAutoPool);

    result = PhMainMessageLoop();
    PhExitApplication(result);
}

LONG PhMainMessageLoop(
    VOID
    )
{
    BOOL result;
    MSG message;
    HACCEL acceleratorTable;

    acceleratorTable = LoadAccelerators(PhInstanceHandle, MAKEINTRESOURCE(IDR_MAINWND_ACCEL));

    while (result = GetMessage(&message, NULL, 0, 0))
    {
        BOOLEAN processed = FALSE;
        ULONG i;

        if (result == -1)
            return 1;

        if (FilterList)
        {
            for (i = 0; i < FilterList->Count; i++)
            {
                PPH_MESSAGE_LOOP_FILTER_ENTRY entry = FilterList->Items[i];

                if (entry->Filter(&message, entry->Context))
                {
                    processed = TRUE;
                    break;
                }
            }
        }

        if (!processed)
        {
            if (
                message.hwnd == PhMainWndHandle ||
                IsChild(PhMainWndHandle, message.hwnd)
                )
            {
                if (TranslateAccelerator(PhMainWndHandle, acceleratorTable, &message))
                    processed = TRUE;
            }

            if (DialogList)
            {
                for (i = 0; i < DialogList->Count; i++)
                {
                    if (IsDialogMessage((HWND)DialogList->Items[i], &message))
                    {
                        processed = TRUE;
                        break;
                    }
                }
            }
        }

        if (!processed)
        {
            TranslateMessage(&message);
            DispatchMessage(&message);
        }

        PhDrainAutoPool(&BaseAutoPool);
    }

    return (LONG)message.wParam;
}

VOID PhRegisterDialog(
    _In_ HWND DialogWindowHandle
    )
{
    if (!DialogList)
        DialogList = PhCreateList(2);

    PhAddItemList(DialogList, (PVOID)DialogWindowHandle);
}

VOID PhUnregisterDialog(
    _In_ HWND DialogWindowHandle
    )
{
    ULONG indexOfDialog;

    if (!DialogList)
        return;

    indexOfDialog = PhFindItemList(DialogList, (PVOID)DialogWindowHandle);

    if (indexOfDialog != -1)
        PhRemoveItemList(DialogList, indexOfDialog);
}

PPH_MESSAGE_LOOP_FILTER_ENTRY PhRegisterMessageLoopFilter(
    _In_ PPH_MESSAGE_LOOP_FILTER Filter,
    _In_opt_ PVOID Context
    )
{
    PPH_MESSAGE_LOOP_FILTER_ENTRY entry;

    if (!FilterList)
        FilterList = PhCreateList(2);

    entry = PhAllocate(sizeof(PH_MESSAGE_LOOP_FILTER_ENTRY));
    entry->Filter = Filter;
    entry->Context = Context;
    PhAddItemList(FilterList, entry);

    return entry;
}

VOID PhUnregisterMessageLoopFilter(
    _In_ PPH_MESSAGE_LOOP_FILTER_ENTRY FilterEntry
    )
{
    ULONG indexOfFilter;

    if (!FilterList)
        return;

    indexOfFilter = PhFindItemList(FilterList, FilterEntry);

    if (indexOfFilter != ULONG_MAX)
        PhRemoveItemList(FilterList, indexOfFilter);

    PhFree(FilterEntry);
}

typedef struct _PHP_PREVIOUS_MAIN_WINDOW_CONTEXT
{
    HANDLE ProcessId;
    PPH_STRING WindowName;
    PPH_LIST WindowList;
} PHP_PREVIOUS_MAIN_WINDOW_CONTEXT, *PPHP_PREVIOUS_MAIN_WINDOW_CONTEXT;

static BOOL CALLBACK PhpPreviousInstanceWindowEnumProc(
    _In_ HWND WindowHandle,
    _In_opt_ PVOID Context
    )
{
    PPHP_PREVIOUS_MAIN_WINDOW_CONTEXT context = (PPHP_PREVIOUS_MAIN_WINDOW_CONTEXT)Context;
    ULONG processId = ULONG_MAX;

    if (!context)
        return TRUE;

    GetWindowThreadProcessId(WindowHandle, &processId);

    if (UlongToHandle(processId) == context->ProcessId && context->WindowName)
    {
        WCHAR className[256];

        if (GetClassName(WindowHandle, className, RTL_NUMBER_OF(className)))
        {
            if (PhEqualStringZ(className, PhGetString(context->WindowName), FALSE))
            {
                PhAddItemList(context->WindowList, WindowHandle);
            }
        }
    }

    return TRUE;
}

static BOOLEAN NTAPI PhpPreviousInstancesCallback(
    _In_ PPH_STRINGREF Name,
    _In_ PPH_STRINGREF TypeName,
    _In_opt_ PVOID Context
    )
{
    static PH_STRINGREF objectNameSr = PH_STRINGREF_INIT(L"PhMutant_");
    HANDLE objectHandle;
    UNICODE_STRING objectNameUs;
    OBJECT_ATTRIBUTES objectAttributes;
    MUTANT_OWNER_INFORMATION objectInfo;

    if (!PhStartsWithStringRef(Name, &objectNameSr, FALSE))
        return TRUE;
    if (!PhStringRefToUnicodeString(Name, &objectNameUs))
        return TRUE;

    InitializeObjectAttributes(
        &objectAttributes,
        &objectNameUs,
        OBJ_CASE_INSENSITIVE,
        PhGetNamespaceHandle(),
        NULL
        );

    if (!NT_SUCCESS(NtOpenMutant(
        &objectHandle,
        MUTANT_QUERY_STATE,
        &objectAttributes
        )))
    {
        return TRUE;
    }

    if (NT_SUCCESS(PhGetMutantOwnerInformation(
        objectHandle,
        &objectInfo
        )))
    {
        HANDLE processHandle = NULL;
        HANDLE tokenHandle = NULL;
        PTOKEN_USER tokenUser = NULL;
        ULONG attempts = 50;

        if (objectInfo.ClientId.UniqueProcess == NtCurrentProcessId())
            goto CleanupExit;
        if (!NT_SUCCESS(PhOpenProcess(&processHandle, PROCESS_QUERY_LIMITED_INFORMATION, objectInfo.ClientId.UniqueProcess)))
            goto CleanupExit;
        if (!NT_SUCCESS(PhOpenProcessToken(processHandle, TOKEN_QUERY, &tokenHandle)))
            goto CleanupExit;
        if (!NT_SUCCESS(PhGetTokenUser(tokenHandle, &tokenUser)))
            goto CleanupExit;
        if (!RtlEqualSid(tokenUser->User.Sid, PhGetOwnTokenAttributes().TokenSid))
            goto CleanupExit;

        // Try to locate the window a few times because some users reported that it might not yet have been created. (dmex)
        do
        {
            PHP_PREVIOUS_MAIN_WINDOW_CONTEXT context;

            memset(&context, 0, sizeof(PHP_PREVIOUS_MAIN_WINDOW_CONTEXT));
            context.ProcessId = objectInfo.ClientId.UniqueProcess;
            context.WindowName = PhGetStringSetting(L"MainWindowClassName");
            context.WindowList = PhCreateList(2);

            PhEnumWindows(PhpPreviousInstanceWindowEnumProc, &context);

            for (ULONG i = 0; i < context.WindowList->Count; i++)
            {
                HWND windowHandle = context.WindowList->Items[i];
                ULONG_PTR result = 0;

                SendMessageTimeout(windowHandle, WM_PH_ACTIVATE, PhStartupParameters.SelectPid, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 5000, &result);

                if (result == PH_ACTIVATE_REPLY)
                {
                    SetForegroundWindow(windowHandle);
                    PhExitApplication(STATUS_SUCCESS);
                }
            }

            PhDereferenceObject(context.WindowList);
            PhDereferenceObject(context.WindowName);
            PhDelayExecution(100);
        } while (--attempts != 0);

    CleanupExit:
        if (tokenUser) PhFree(tokenUser);
        if (tokenHandle) NtClose(tokenHandle);
        if (processHandle) NtClose(processHandle);
    }

    NtClose(objectHandle);
    return TRUE;
}

VOID PhActivatePreviousInstance(
    VOID
    )
{
    PhEnumDirectoryObjects(PhGetNamespaceHandle(), PhpPreviousInstancesCallback, NULL);
}

VOID PhInitializeCommonControls(
    VOID
    )
{
    INITCOMMONCONTROLSEX icex;

    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC =
        ICC_LISTVIEW_CLASSES |
        ICC_TREEVIEW_CLASSES |
        ICC_BAR_CLASSES |
        ICC_TAB_CLASSES |
        ICC_PROGRESS_CLASS |
        ICC_COOL_CLASSES |
        ICC_STANDARD_CLASSES |
        ICC_LINK_CLASS
        ;

    InitCommonControlsEx(&icex);
}

VOID PhInitializeFont(
    VOID
    )
{
    NONCLIENTMETRICS metrics = { sizeof(metrics) };
    HFONT oldFont = PhApplicationFont;

    if (
        !(PhApplicationFont = PhCreateFont(L"Microsoft Sans Serif", 8, FW_NORMAL)) &&
        !(PhApplicationFont = PhCreateFont(L"Tahoma", 8, FW_NORMAL))
        )
    {
        if (SystemParametersInfo(SPI_GETNONCLIENTMETRICS, 0, &metrics, 0))
            PhApplicationFont = CreateFontIndirect(&metrics.lfMessageFont);
        else
            PhApplicationFont = NULL;
    }

    if (oldFont) DeleteFont(oldFont);
}

BOOLEAN PhInitializeDirectoryPolicy(
    VOID
    )
{
    PPH_STRING applicationDirectory;
    UNICODE_STRING applicationDirectoryUs;

    if (!(applicationDirectory = PhGetApplicationDirectory()))
        return FALSE;

    if (!PhStringRefToUnicodeString(&applicationDirectory->sr, &applicationDirectoryUs))
    {
        PhDereferenceObject(applicationDirectory);
        return FALSE;
    }

    if (!NT_SUCCESS(RtlSetCurrentDirectory_U(&applicationDirectoryUs)))
    {
        PhDereferenceObject(applicationDirectory);
        return FALSE;
    }

    PhDereferenceObject(applicationDirectory);
    return TRUE;
}

#ifndef DEBUG
#include <symprv.h>
#include <minidumpapiset.h>

VOID PhpCreateUnhandledExceptionCrashDump(
    _In_ PEXCEPTION_POINTERS ExceptionInfo,
    _In_ BOOLEAN MoreInfoDump
    )
{
    static PH_STRINGREF dumpFilePath = PH_STRINGREF_INIT(L"%USERPROFILE%\\Desktop\\");
    HANDLE fileHandle;
    PPH_STRING dumpDirectory;
    PPH_STRING dumpFileName;
    WCHAR alphastring[16] = L"";

    dumpDirectory = PhExpandEnvironmentStrings(&dumpFilePath);
    PhGenerateRandomAlphaString(alphastring, RTL_NUMBER_OF(alphastring));

    dumpFileName = PhConcatStrings(
        4,
        PhGetString(dumpDirectory),
        L"\\ProcessHacker_",
        alphastring,
        L"_DumpFile.dmp"
        );

    if (NT_SUCCESS(PhCreateFileWin32(
        &fileHandle,
        dumpFileName->Buffer,
        FILE_GENERIC_WRITE,
        FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ | FILE_SHARE_DELETE,
        FILE_OVERWRITE_IF,
        FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT
        )))
    {
        MINIDUMP_EXCEPTION_INFORMATION exceptionInfo;

        exceptionInfo.ThreadId = HandleToUlong(NtCurrentThreadId());
        exceptionInfo.ExceptionPointers = ExceptionInfo;
        exceptionInfo.ClientPointers = FALSE;

        if (MoreInfoDump)
        {
            // Note: This isn't a full dump, still very limited but just enough to see filenames on the stack. (dmex)
            PhWriteMiniDumpProcess(
                NtCurrentProcess(),
                NtCurrentProcessId(),
                fileHandle,
                MiniDumpScanMemory | MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithProcessThreadData,
                &exceptionInfo,
                NULL,
                NULL
                );
        }
        else
        {
            PhWriteMiniDumpProcess(
                NtCurrentProcess(),
                NtCurrentProcessId(),
                fileHandle,
                MiniDumpNormal,
                &exceptionInfo,
                NULL,
                NULL
                );
        }

        NtClose(fileHandle);
    }

    PhDereferenceObject(dumpFileName);
    PhDereferenceObject(dumpDirectory);
}

ULONG CALLBACK PhpUnhandledExceptionCallback(
    _In_ PEXCEPTION_POINTERS ExceptionInfo
    )
{
    PPH_STRING errorMessage;
    INT result;
    PPH_STRING message;

    if (NT_NTWIN32(ExceptionInfo->ExceptionRecord->ExceptionCode))
        errorMessage = PhGetStatusMessage(0, WIN32_FROM_NTSTATUS(ExceptionInfo->ExceptionRecord->ExceptionCode));
    else
        errorMessage = PhGetStatusMessage(ExceptionInfo->ExceptionRecord->ExceptionCode, 0);

    message = PhFormatString(
        L"0x%08X (%s)",
        ExceptionInfo->ExceptionRecord->ExceptionCode,
        PhGetStringOrEmpty(errorMessage)
        );

    if (TaskDialogIndirect)
    {
        TASKDIALOGCONFIG config = { sizeof(TASKDIALOGCONFIG) };
        TASKDIALOG_BUTTON buttons[4];

        buttons[0].nButtonID = IDYES;
        buttons[0].pszButtonText = L"Normaldump";
        buttons[1].nButtonID = IDNO;
        buttons[1].pszButtonText = L"Minidump";
        buttons[2].nButtonID = IDABORT;
        buttons[2].pszButtonText = L"Restart";
        buttons[3].nButtonID = IDCANCEL;
        buttons[3].pszButtonText = L"Exit";

        config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
        config.pszWindowTitle = PhApplicationName;
        config.pszMainIcon = TD_ERROR_ICON;
        config.pszMainInstruction = L"Process Hacker has crashed :(";
        config.pszContent = PhGetStringOrEmpty(message);
        config.cButtons = RTL_NUMBER_OF(buttons);
        config.pButtons = buttons;
        config.nDefaultButton = IDCANCEL;

        if (SUCCEEDED(TaskDialogIndirect(&config, &result, NULL, NULL)))
        {
            switch (result)
            {
            case IDCANCEL:
                {
                    PhExitApplication(ExceptionInfo->ExceptionRecord->ExceptionCode);
                }
                break;
            case IDABORT:
                {
                    PhShellProcessHacker(
                        NULL,
                        NULL,
                        SW_SHOW,
                        0,
                        PH_SHELL_APP_PROPAGATE_PARAMETERS | PH_SHELL_APP_PROPAGATE_PARAMETERS_IGNORE_VISIBILITY,
                        0,
                        NULL
                        );
                }
                break;
            case IDYES:
                PhpCreateUnhandledExceptionCrashDump(ExceptionInfo, TRUE);
                break;
            case IDNO:
                PhpCreateUnhandledExceptionCrashDump(ExceptionInfo, FALSE);
                break;
            }
        }
    }
    else
    {
        if (PhShowMessage(
            NULL,
            MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2,
            L"Process Hacker has crashed :(\r\n\r\n%s",
            L"Do you want to create a minidump on the Desktop?"
            ) == IDYES)
        {
            PhpCreateUnhandledExceptionCrashDump(ExceptionInfo, FALSE);
        }

        PhShellProcessHacker(
            NULL,
            NULL,
            SW_SHOW,
            0,
            PH_SHELL_APP_PROPAGATE_PARAMETERS | PH_SHELL_APP_PROPAGATE_PARAMETERS_IGNORE_VISIBILITY,
            0,
            NULL
            );
    }

    PhExitApplication(ExceptionInfo->ExceptionRecord->ExceptionCode);

    PhDereferenceObject(message);
    PhDereferenceObject(errorMessage);

    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

BOOLEAN PhInitializeExceptionPolicy(
    VOID
    )
{
#ifndef DEBUG
    ULONG errorMode;
    
    if (NT_SUCCESS(PhGetProcessErrorMode(NtCurrentProcess(), &errorMode)))
    {
        errorMode &= ~(SEM_NOOPENFILEERRORBOX | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        PhSetProcessErrorMode(NtCurrentProcess(), errorMode);
    }

    RtlSetUnhandledExceptionFilter(PhpUnhandledExceptionCallback);
#endif
    return TRUE;
}

BOOLEAN PhInitializeNamespacePolicy(
    VOID
    )
{
    HANDLE mutantHandle;
    SIZE_T returnLength;
    WCHAR formatBuffer[PH_INT64_STR_LEN_1];
    OBJECT_ATTRIBUTES objectAttributes;
    UNICODE_STRING objectNameUs;
    PH_STRINGREF objectNameSr;
    PH_FORMAT format[2];

    PhInitFormatS(&format[0], L"PhMutant_");
    PhInitFormatU(&format[1], HandleToUlong(NtCurrentProcessId()));

    if (!PhFormatToBuffer(
        format,
        RTL_NUMBER_OF(format),
        formatBuffer,
        sizeof(formatBuffer),
        &returnLength
        ))
    {
        return FALSE;
    }

    objectNameSr.Length = returnLength - sizeof(UNICODE_NULL);
    objectNameSr.Buffer = formatBuffer;

    if (!PhStringRefToUnicodeString(&objectNameSr, &objectNameUs))
        return FALSE;

    InitializeObjectAttributes(
        &objectAttributes,
        &objectNameUs,
        OBJ_CASE_INSENSITIVE,
        PhGetNamespaceHandle(),
        NULL
        );

    NtCreateMutant(
        &mutantHandle,
        MUTANT_QUERY_STATE,
        &objectAttributes,
        TRUE
        );

    return TRUE;
}

BOOLEAN PhInitializeMitigationPolicy(
    VOID
    )
{
#if (PHNT_VERSION >= PHNT_WIN7)
#ifndef DEBUG
    BOOLEAN PhpIsExploitProtectionEnabled(VOID); // Forwarded from options.c (dmex)
#define DEFAULT_MITIGATION_POLICY_FLAGS \
    (PROCESS_CREATION_MITIGATION_POLICY_HEAP_TERMINATE_ALWAYS_ON | \
     PROCESS_CREATION_MITIGATION_POLICY_BOTTOM_UP_ASLR_ALWAYS_ON | \
     PROCESS_CREATION_MITIGATION_POLICY_HIGH_ENTROPY_ASLR_ALWAYS_ON | \
     PROCESS_CREATION_MITIGATION_POLICY_EXTENSION_POINT_DISABLE_ALWAYS_ON | \
     PROCESS_CREATION_MITIGATION_POLICY_PROHIBIT_DYNAMIC_CODE_ALWAYS_ON | \
     PROCESS_CREATION_MITIGATION_POLICY_CONTROL_FLOW_GUARD_ALWAYS_ON | \
     PROCESS_CREATION_MITIGATION_POLICY_IMAGE_LOAD_NO_REMOTE_ALWAYS_ON | \
     PROCESS_CREATION_MITIGATION_POLICY_IMAGE_LOAD_NO_LOW_LABEL_ALWAYS_ON)
//#define DEFAULT_MITIGATION_POLICY_FLAGS2 \
//    (PROCESS_CREATION_MITIGATION_POLICY2_LOADER_INTEGRITY_CONTINUITY_ALWAYS_ON | \
//     PROCESS_CREATION_MITIGATION_POLICY2_STRICT_CONTROL_FLOW_GUARD_ALWAYS_ON | \
//     PROCESS_CREATION_MITIGATION_POLICY2_MODULE_TAMPERING_PROTECTION_ALWAYS_ON | \
//     PROCESS_CREATION_MITIGATION_POLICY2_RESTRICT_INDIRECT_BRANCH_PREDICTION_ALWAYS_ON | \
//     PROCESS_CREATION_MITIGATION_POLICY2_SPECULATIVE_STORE_BYPASS_DISABLE_ALWAYS_ON | \
//     PROCESS_CREATION_MITIGATION_POLICY2_CET_USER_SHADOW_STACKS_ALWAYS_ON)
    static PH_STRINGREF nompCommandlinePart = PH_STRINGREF_INIT(L" -nomp");
    static PH_STRINGREF rasCommandlinePart = PH_STRINGREF_INIT(L" -ras");
    BOOLEAN success = TRUE;
    //HANDLE jobObjectHandle = NULL;
    PPH_STRING commandline = NULL;
    PH_STRINGREF commandlineSr;
    ULONG64 options[2] = { 0 };
    PS_SYSTEM_DLL_INIT_BLOCK(*LdrSystemDllInitBlock_I) = NULL;
    STARTUPINFOEX startupInfo = { sizeof(STARTUPINFOEX) };
    SIZE_T attributeListLength;

    if (WindowsVersion < WINDOWS_10_RS3)
        return TRUE;
    if (!PhpIsExploitProtectionEnabled())
        return TRUE;

    PhUnicodeStringToStringRef(&NtCurrentPeb()->ProcessParameters->CommandLine, &commandlineSr);
    //if (!NT_SUCCESS(PhGetProcessCommandLine(NtCurrentProcess(), &commandline)))
    //    goto CleanupExit;
    if (PhFindStringInStringRef(&commandlineSr, &rasCommandlinePart, FALSE) != -1)
        goto CleanupExit;
    if (PhEndsWithStringRef(&commandlineSr, &nompCommandlinePart, FALSE))
        goto CleanupExit;

    PhMoveReference(&commandline, PhConcatStringRef2(&commandlineSr, &nompCommandlinePart));

    if (!(LdrSystemDllInitBlock_I = PhGetDllProcedureAddress(L"ntdll.dll", "LdrSystemDllInitBlock", 0)))
        goto CleanupExit;
    if (!RTL_CONTAINS_FIELD(LdrSystemDllInitBlock_I, LdrSystemDllInitBlock_I->Size, MitigationOptionsMap))
        goto CleanupExit;
    if ((LdrSystemDllInitBlock_I->MitigationOptionsMap.Map[0] & DEFAULT_MITIGATION_POLICY_FLAGS) == DEFAULT_MITIGATION_POLICY_FLAGS)
        goto CleanupExit;

    //if (NT_SUCCESS(NtCreateJobObject(&jobObjectHandle, JOB_OBJECT_ALL_ACCESS, NULL)))
    //{
    //    JOBOBJECT_BASIC_UI_RESTRICTIONS basicJobRestrictions;
    //
    //    basicJobRestrictions.UIRestrictionsClass = JOB_OBJECT_UILIMIT_GLOBALATOMS | JOB_OBJECT_UILIMIT_HANDLES;
    //    NtSetInformationJobObject(
    //        jobObjectHandle,
    //        JobObjectBasicUIRestrictions,
    //        &basicJobRestrictions,
    //        sizeof(JOBOBJECT_BASIC_UI_RESTRICTIONS)
    //        );
    //}

    if (!InitializeProcThreadAttributeList(NULL, 1, 0, &attributeListLength) && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        goto CleanupExit;

    startupInfo.lpAttributeList = PhAllocate(attributeListLength);

    if (!InitializeProcThreadAttributeList(startupInfo.lpAttributeList, 1, 0, &attributeListLength))
        goto CleanupExit;
    if (!UpdateProcThreadAttribute(startupInfo.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY, &(ULONG64){ DEFAULT_MITIGATION_POLICY_FLAGS }, sizeof(ULONG64), NULL, NULL))
        goto CleanupExit;
    //if (!UpdateProcThreadAttribute(startupInfo.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, &jobObjectHandle, sizeof(HANDLE), NULL, NULL))
    //    goto CleanupExit;

    if (NT_SUCCESS(PhCreateProcessWin32Ex(
        NULL,
        PhGetString(commandline),
        NULL,
        NULL,
        &startupInfo.StartupInfo,
        PH_CREATE_PROCESS_EXTENDED_STARTUPINFO | PH_CREATE_PROCESS_BREAKAWAY_FROM_JOB,
        NULL,
        NULL,
        NULL,
        NULL
        )))
    {
        success = FALSE;
    }

CleanupExit:

    if (commandline)
        PhDereferenceObject(commandline);

    if (startupInfo.lpAttributeList)
    {
        DeleteProcThreadAttributeList(startupInfo.lpAttributeList);
        PhFree(startupInfo.lpAttributeList);
    }

    //if (jobObjectHandle)
    //    NtClose(jobObjectHandle);

    return success;
#else
    return TRUE;
#endif

#else
    return TRUE;
#endif
}

BOOLEAN PhInitializeMitigationSignaturePolicy(
    VOID
    )
{
#ifndef DEBUG
    BOOLEAN PhpIsExploitProtectionEnabled(VOID); // Forwarded from options.c (dmex)
    // Starting with Win10 20H1 processes with uiAccess=true override the ProcessExtensionPointDisablePolicy
    // blocking hook DLL injection and inject the window hook anyway. This override doesn't check if the process has also enabled 
    // the MicrosoftSignedOnly policy causing an infinite loop of APC messages and hook DLL loading/unloading
    // inside user32!_ClientLoadLibrary while calling the GetMessageW API for the window message loop.
    // ...
    // 1) GetMessageW processes the APC message for loading the window hook DLL with user32!_ClientLoadLibrary.
    // 2) user32!_ClientLoadLibrary calls LoadLibraryEx with the DLL path.
    // 3) LoadLibraryEx returns an error loading the window hook DLL because we enabled MicrosoftSignedOnly.
    // 4) SetWindowsHookEx ignores the result and re-queues the APC message from step 1.
    // ...
    // Mouse/keyboard/window messages passing through GetMessageW generate large volumes of calls to LoadLibraryEx
    // making the application unresponsive as each message processes the APC message and loads/unloads the hook DLL...
    // So don't use MicrosoftSignedOnly on versions of Windows where Process Hacker becomes unresponsive
    // because a third party application called SetWindowsHookEx on the machine. (dmex)
    if (
        WindowsVersion >= WINDOWS_10 &&
        PhpIsExploitProtectionEnabled()
        //WindowsVersion != WINDOWS_10_20H1 &&
        //WindowsVersion != WINDOWS_10_20H2
        )
    {
        PROCESS_MITIGATION_POLICY_INFORMATION policyInfo;

        policyInfo.Policy = ProcessSignaturePolicy;
        policyInfo.SignaturePolicy.Flags = 0;
        policyInfo.SignaturePolicy.MicrosoftSignedOnly = TRUE;

        NtSetInformationProcess(NtCurrentProcess(), ProcessMitigationPolicy, &policyInfo, sizeof(PROCESS_MITIGATION_POLICY_INFORMATION));
    }
#endif

    return TRUE;
}

BOOLEAN PhInitializeComPolicy(
    VOID
    )
{
#ifdef PH_COM_SVC
    static SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    ULONG securityDescriptorAllocationLength;
    PSECURITY_DESCRIPTOR securityDescriptor;
    UCHAR administratorsSidBuffer[FIELD_OFFSET(SID, SubAuthority) + sizeof(ULONG) * 2];
    PSID administratorsSid;
    PACL dacl;

    if (!SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)))
        return TRUE; // Continue without COM support. (dmex)

    administratorsSid = (PSID)administratorsSidBuffer;
    RtlInitializeSid(administratorsSid, &ntAuthority, 2);
    *RtlSubAuthoritySid(administratorsSid, 0) = SECURITY_BUILTIN_DOMAIN_RID;
    *RtlSubAuthoritySid(administratorsSid, 1) = DOMAIN_ALIAS_RID_ADMINS;

    securityDescriptorAllocationLength = SECURITY_DESCRIPTOR_MIN_LENGTH +
        (ULONG)sizeof(ACL) +
        (ULONG)sizeof(ACCESS_ALLOWED_ACE) +
        RtlLengthSid(&PhSeAuthenticatedUserSid) +
        (ULONG)sizeof(ACCESS_ALLOWED_ACE) +
        RtlLengthSid(&PhSeLocalSystemSid) +
        (ULONG)sizeof(ACCESS_ALLOWED_ACE) +
        RtlLengthSid(&administratorsSid);

    securityDescriptor = PhAllocate(securityDescriptorAllocationLength);
    dacl = PTR_ADD_OFFSET(securityDescriptor, SECURITY_DESCRIPTOR_MIN_LENGTH);
    RtlCreateSecurityDescriptor(securityDescriptor, SECURITY_DESCRIPTOR_REVISION);
    RtlCreateAcl(dacl, securityDescriptorAllocationLength - SECURITY_DESCRIPTOR_MIN_LENGTH, ACL_REVISION);
    RtlAddAccessAllowedAce(dacl, ACL_REVISION, FILE_READ_DATA | FILE_WRITE_DATA, &PhSeAuthenticatedUserSid);
    RtlAddAccessAllowedAce(dacl, ACL_REVISION, FILE_READ_DATA | FILE_WRITE_DATA, &PhSeLocalSystemSid);
    RtlAddAccessAllowedAce(dacl, ACL_REVISION, FILE_READ_DATA | FILE_WRITE_DATA, administratorsSid);
    RtlSetDaclSecurityDescriptor(securityDescriptor, TRUE, dacl, FALSE);
    RtlSetGroupSecurityDescriptor(securityDescriptor, administratorsSid, FALSE);
    RtlSetOwnerSecurityDescriptor(securityDescriptor, administratorsSid, FALSE);

    if (!SUCCEEDED(CoInitializeSecurity(
        securityDescriptor,
        UINT_MAX,
        NULL,
        NULL,
        RPC_C_AUTHN_LEVEL_PKT_PRIVACY,
        RPC_C_IMP_LEVEL_IDENTIFY,
        NULL,
        EOAC_NONE,
        NULL
        )))
    {
        NOTHING;
    }

    PhFree(securityDescriptor);
    return TRUE;
#else
    if (!SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)))
        NOTHING;

    return TRUE;
#endif
}

NTSTATUS PhpReadSignature(
    _In_ PWSTR FileName,
    _Out_ PUCHAR *Signature,
    _Out_ PULONG SignatureSize
    )
{
    NTSTATUS status;
    HANDLE fileHandle;
    PUCHAR signature;
    ULONG bufferSize;
    IO_STATUS_BLOCK iosb;

    status = PhCreateFileWin32(
        &fileHandle,
        FileName,
        FILE_GENERIC_READ,
        FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ,
        FILE_OPEN,
        FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT
        );

    if (!NT_SUCCESS(status))
        return status;

    bufferSize = 1024;
    signature = PhAllocate(bufferSize);

    status = NtReadFile(
        fileHandle,
        NULL,
        NULL,
        NULL,
        &iosb,
        signature,
        bufferSize,
        NULL,
        NULL
        );

    NtClose(fileHandle);

    if (NT_SUCCESS(status))
    {
        *Signature = signature;
        *SignatureSize = (ULONG)iosb.Information;
        return status;
    }
    else
    {
        PhFree(signature);
        return status;
    }
}

VOID PhpShowKphError(
    _In_ PWSTR Message, 
    _In_opt_ NTSTATUS Status
    )
{
    if (Status == STATUS_NO_SUCH_FILE)
    {
        PhShowError2(
            NULL,
            Message,
            L"%s",
            L"You will be unable to use more advanced features, view details about system processes or terminate malicious software."
            );
    }
    else
    {
        PPH_STRING errorMessage;
        PPH_STRING statusMessage;

        if (!(errorMessage = PhGetStatusMessage(Status, 0)))
            errorMessage = PhGetStatusMessage(0, Status);

        if (errorMessage)
        {
            statusMessage = PhConcatStrings(
                3,
                errorMessage->Buffer,
                L"\r\n\r\n",
                L"You will be unable to use more advanced features, view details about system processes or terminate malicious software."
                );
            PhShowError2(NULL, Message, L"%s", statusMessage->Buffer);
            PhDereferenceObject(statusMessage);
            PhDereferenceObject(errorMessage);
        }
        else
        {
            PhShowError2(
                NULL, 
                Message,
                L"%s",
                L"You will be unable to use more advanced features, view details about system processes or terminate malicious software."
                );
        }
    }
}

VOID PhInitializeKph(
    VOID
    )
{
    NTSTATUS status;
    PPH_STRING kphDirectory = NULL;
    PPH_STRING kphFileName = NULL;
    PPH_STRING kphSigFileName = NULL;
    PPH_STRING kphServiceName = NULL;

    if (!(kphDirectory = PhGetApplicationDirectory()))
        return;

    kphServiceName = PhGetStringSetting(L"KphServiceName");

    if (kphServiceName && PhIsNullOrEmptyString(kphServiceName))
        PhClearReference(&kphServiceName);

    kphFileName = PhConcatStringRefZ(&kphDirectory->sr, L"kprocesshacker.sys");
    kphSigFileName = PhConcatStringRefZ(&kphDirectory->sr, L"processhacker.sig");

    // Reset KPH after a Windows build update. (dmex)
    {
        ULONG latestBuildNumber = PhGetIntegerSetting(L"KphBuildNumber");

        if (latestBuildNumber == 0)
        {
            PhSetIntegerSetting(L"KphBuildNumber", PhOsVersion.dwBuildNumber);
        }
        else
        {
            if (latestBuildNumber != PhOsVersion.dwBuildNumber)
            {
                if (KphParametersExists(PhGetString(kphServiceName)))
                {
                    if (PhShowMessage(
                        NULL,
                        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2,
                        L"Process Hacker needs to reset the driver configuration\r\n\r\n%s",
                        L"Do you want to continue?"
                        ) != IDYES)
                    {
                        return;
                    }

                    if (NT_SUCCESS(KphResetParameters(PhGetString(kphServiceName))))
                    {
                        PhSetIntegerSetting(L"KphBuildNumber", PhOsVersion.dwBuildNumber);
                    }
                }
                else
                {
                    PhSetIntegerSetting(L"KphBuildNumber", PhOsVersion.dwBuildNumber);
                }
            }
        }
    }

    if (PhDoesFileExistsWin32(kphFileName->Buffer))
    {
        KPH_PARAMETERS parameters;

        parameters.SecurityLevel = KphSecuritySignatureAndPrivilegeCheck;
        parameters.CreateDynamicConfiguration = TRUE;

        if (NT_SUCCESS(status = KphConnect2Ex(
            PhGetString(kphServiceName),
            KPH_DEVICE_SHORT_NAME,
            kphFileName->Buffer,
            &parameters
            )))
        {
            PUCHAR signature;
            ULONG signatureSize;

            status = PhpReadSignature(
                kphSigFileName->Buffer,
                &signature,
                &signatureSize
                );

            if (NT_SUCCESS(status))
            {
                status = KphVerifyClient(signature, signatureSize);

                if (!NT_SUCCESS(status))
                {
                    if (PhGetIntegerSetting(L"EnableKphWarnings") && !PhStartupParameters.PhSvc)
                        PhpShowKphError(L"Unable to verify the kernel driver signature.", status);
                }

                PhFree(signature);
            }
            else
            {
                if (PhGetIntegerSetting(L"EnableKphWarnings") && !PhStartupParameters.PhSvc)
                    PhpShowKphError(L"Unable to read the kernel driver signature.", status);
            }
        }
        else
        {
            if (PhGetIntegerSetting(L"EnableKphWarnings") && PhGetOwnTokenAttributes().Elevated && !PhStartupParameters.PhSvc)
                PhpShowKphError(L"Unable to load the kernel driver service.", status);
        }
    }
    else
    {
        if (PhGetIntegerSetting(L"EnableKphWarnings") && !PhStartupParameters.PhSvc)
            PhpShowKphError(L"The kernel driver was not found.", STATUS_NO_SUCH_FILE);
    }

    if (kphServiceName)
        PhDereferenceObject(kphServiceName);
    if (kphSigFileName)
        PhDereferenceObject(kphSigFileName);
    if (kphFileName)
        PhDereferenceObject(kphFileName);
    if (kphDirectory)
        PhDereferenceObject(kphDirectory);
}

BOOLEAN PhInitializeAppSystem(
    VOID
    )
{
    if (!PhProcessProviderInitialization())
        return FALSE;
    if (!PhServiceProviderInitialization())
        return FALSE;
    if (!PhNetworkProviderInitialization())
        return FALSE;

    PhSetHandleClientIdFunction(PhGetClientIdName);

    return TRUE;
}

VOID PhpInitializeSettings(
    VOID
    )
{
    NTSTATUS status;

    if (!PhStartupParameters.NoSettings)
    {
        static PH_STRINGREF settingsPath = PH_STRINGREF_INIT(L"%APPDATA%\\Process Hacker\\settings.xml");
        static PH_STRINGREF settingsSuffix = PH_STRINGREF_INIT(L".settings.xml");
        PPH_STRING settingsFileName;

        // There are three possible locations for the settings file:
        // 1. The file name given in the command line.
        // 2. A file named ProcessHacker.exe.settings.xml in the program directory. (This changes
        //    based on the executable file name.)
        // 3. The default location.

        // 1. File specified in command line
        if (PhStartupParameters.SettingsFileName)
        {
            // Get an absolute path now.
            PhSettingsFileName = PhGetFullPath(PhStartupParameters.SettingsFileName->Buffer, NULL);
        }

        // 2. File in program directory
        if (!PhSettingsFileName)
        {
            PPH_STRING applicationFileName;

            if (applicationFileName = PhGetApplicationFileName())
            {
                settingsFileName = PhConcatStringRef2(&applicationFileName->sr, &settingsSuffix);

                if (PhDoesFileExistsWin32(settingsFileName->Buffer))
                {
                    PhSettingsFileName = settingsFileName;
                }
                else
                {
                    PhDereferenceObject(settingsFileName);
                }

                PhDereferenceObject(applicationFileName);
            }
        }

        // 3. Default location
        if (!PhSettingsFileName)
        {
            PhSettingsFileName = PhExpandEnvironmentStrings(&settingsPath);
        }

        if (PhSettingsFileName)
        {
            status = PhLoadSettings(PhSettingsFileName->Buffer);

            // If we didn't find the file, it will be created. Otherwise,
            // there was probably a parsing error and we don't want to
            // change anything.
            if (status == STATUS_FILE_CORRUPT_ERROR)
            {
                if (PhShowMessage2(
                    NULL,
                    TDCBF_YES_BUTTON | TDCBF_NO_BUTTON,
                    TD_WARNING_ICON,
                    L"Process Hacker's settings file is corrupt. Do you want to reset it?",
                    L"If you select No, the settings system will not function properly."
                    ) == IDYES)
                {
                    HANDLE fileHandle;
                    IO_STATUS_BLOCK isb;
                    CHAR data[] = "<settings></settings>";

                    // This used to delete the file. But it's better to keep the file there
                    // and overwrite it with some valid XML, especially with case (2) above.
                    if (NT_SUCCESS(PhCreateFileWin32(
                        &fileHandle,
                        PhSettingsFileName->Buffer,
                        FILE_GENERIC_WRITE,
                        FILE_ATTRIBUTE_NORMAL,
                        FILE_SHARE_READ | FILE_SHARE_DELETE,
                        FILE_OVERWRITE,
                        FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT
                        )))
                    {
                        NtWriteFile(fileHandle, NULL, NULL, NULL, &isb, data, sizeof(data) - 1, NULL, NULL);
                        NtClose(fileHandle);
                    }
                }
                else
                {
                    // Pretend we don't have a settings store so bad things
                    // don't happen.
                    PhDereferenceObject(PhSettingsFileName);
                    PhSettingsFileName = NULL;
                }
            }
        }
    }

    // Apply basic global settings.
    PhPluginsEnabled = !!PhGetIntegerSetting(L"EnablePlugins");
    PhMaxSizeUnit = PhGetIntegerSetting(L"MaxSizeUnit");

    if (PhGetIntegerSetting(L"SampleCountAutomatic"))
    {
        ULONG sampleCount;

        sampleCount = (GetSystemMetrics(SM_CXVIRTUALSCREEN) + 1) / 2;

        if (sampleCount > 2048)
            sampleCount = 2048;

        PhSetIntegerSetting(L"SampleCount", sampleCount);
    }
}

typedef enum _PH_COMMAND_ARG
{
    PH_ARG_NONE,
    PH_ARG_SETTINGS,
    PH_ARG_NOSETTINGS,
    PH_ARG_SHOWVISIBLE,
    PH_ARG_SHOWHIDDEN,
    PH_ARG_RUNASSERVICEMODE,
    PH_ARG_NOKPH,
    PH_ARG_INSTALLKPH,
    PH_ARG_UNINSTALLKPH,
    PH_ARG_DEBUG,
    PH_ARG_HWND,
    PH_ARG_POINT,
    PH_ARG_SHOWOPTIONS,
    PH_ARG_PHSVC,
    PH_ARG_NOPLUGINS,
    PH_ARG_NEWINSTANCE,
    PH_ARG_ELEVATE,
    PH_ARG_SILENT,
    PH_ARG_HELP,
    PH_ARG_SELECTPID,
    PH_ARG_PRIORITY,
    PH_ARG_PLUGIN,
    PH_ARG_SELECTTAB,
    PH_ARG_SYSINFO
} PH_COMMAND_ARG;

BOOLEAN NTAPI PhpCommandLineOptionCallback(
    _In_opt_ PPH_COMMAND_LINE_OPTION Option,
    _In_opt_ PPH_STRING Value,
    _In_opt_ PVOID Context
    )
{
    ULONG64 integer;

    if (Option)
    {
        switch (Option->Id)
        {
        case PH_ARG_SETTINGS:
            PhSwapReference(&PhStartupParameters.SettingsFileName, Value);
            break;
        case PH_ARG_NOSETTINGS:
            PhStartupParameters.NoSettings = TRUE;
            break;
        case PH_ARG_SHOWVISIBLE:
            PhStartupParameters.ShowVisible = TRUE;
            break;
        case PH_ARG_SHOWHIDDEN:
            PhStartupParameters.ShowHidden = TRUE;
            break;
        case PH_ARG_RUNASSERVICEMODE:
            PhSwapReference(&PhStartupParameters.RunAsServiceMode, Value);
            break;
        case PH_ARG_NOKPH:
            PhStartupParameters.NoKph = TRUE;
            break;
        case PH_ARG_INSTALLKPH:
            PhStartupParameters.InstallKph = TRUE;
            break;
        case PH_ARG_UNINSTALLKPH:
            PhStartupParameters.UninstallKph = TRUE;
            break;
        case PH_ARG_DEBUG:
            PhStartupParameters.Debug = TRUE;
            break;
        case PH_ARG_HWND:
            if (Value && PhStringToInteger64(&Value->sr, 16, &integer))
                PhStartupParameters.WindowHandle = (HWND)(ULONG_PTR)integer;
            break;
        case PH_ARG_POINT:
            {
                PH_STRINGREF xString;
                PH_STRINGREF yString;

                if (Value && PhSplitStringRefAtChar(&Value->sr, L',', &xString, &yString))
                {
                    LONG64 x;
                    LONG64 y;

                    if (PhStringToInteger64(&xString, 10, &x) && PhStringToInteger64(&yString, 10, &y))
                    {
                        PhStartupParameters.Point.x = (LONG)x;
                        PhStartupParameters.Point.y = (LONG)y;
                    }
                }
            }
            break;
        case PH_ARG_SHOWOPTIONS:
            PhStartupParameters.ShowOptions = TRUE;
            break;
        case PH_ARG_PHSVC:
            PhStartupParameters.PhSvc = TRUE;
            break;
        case PH_ARG_NOPLUGINS:
            PhStartupParameters.NoPlugins = TRUE;
            break;
        case PH_ARG_NEWINSTANCE:
            PhStartupParameters.NewInstance = TRUE;
            break;
        case PH_ARG_ELEVATE:
            PhStartupParameters.Elevate = TRUE;
            break;
        case PH_ARG_SILENT:
            PhStartupParameters.Silent = TRUE;
            break;
        case PH_ARG_HELP:
            PhStartupParameters.Help = TRUE;
            break;
        case PH_ARG_SELECTPID:
            if (Value && PhStringToInteger64(&Value->sr, 0, &integer))
                PhStartupParameters.SelectPid = (ULONG)integer;
            break;
        case PH_ARG_PRIORITY:
            if (Value && PhEqualString2(Value, L"r", TRUE))
                PhStartupParameters.PriorityClass = PROCESS_PRIORITY_CLASS_REALTIME;
            else if (Value && PhEqualString2(Value, L"h", TRUE))
                PhStartupParameters.PriorityClass = PROCESS_PRIORITY_CLASS_HIGH;
            else if (Value && PhEqualString2(Value, L"n", TRUE))
                PhStartupParameters.PriorityClass = PROCESS_PRIORITY_CLASS_NORMAL;
            else if (Value && PhEqualString2(Value, L"l", TRUE))
                PhStartupParameters.PriorityClass = PROCESS_PRIORITY_CLASS_IDLE;
            break;
        case PH_ARG_PLUGIN:
            if (!PhStartupParameters.PluginParameters)
                PhStartupParameters.PluginParameters = PhCreateList(3);
            if (Value)
                PhAddItemList(PhStartupParameters.PluginParameters, PhReferenceObject(Value));
            break;
        case PH_ARG_SELECTTAB:
            PhSwapReference(&PhStartupParameters.SelectTab, Value);
            break;
        case PH_ARG_SYSINFO:
            PhSwapReference(&PhStartupParameters.SysInfo, Value ? Value : PhReferenceEmptyString());
            break;
        }
    }
    else
    {
        PPH_STRING upperValue = NULL;

        if (Value)
            upperValue = PhUpperString(Value);

        if (upperValue)
        {
            if (PhFindStringInString(upperValue, 0, L"TASKMGR.EXE") != -1)
            {
                // User probably has Process Hacker replacing Task Manager. Force
                // the main window to start visible.
                PhStartupParameters.ShowVisible = TRUE;
            }

            PhDereferenceObject(upperValue);
        }
    }

    return TRUE;
}

VOID PhpProcessStartupParameters(
    VOID
    )
{
    PH_COMMAND_LINE_OPTION options[] =
    {
        { PH_ARG_SETTINGS, L"settings", MandatoryArgumentType },
        { PH_ARG_NOSETTINGS, L"nosettings", NoArgumentType },
        { PH_ARG_SHOWVISIBLE, L"v", NoArgumentType },
        { PH_ARG_SHOWHIDDEN, L"hide", NoArgumentType },
        { PH_ARG_RUNASSERVICEMODE, L"ras", MandatoryArgumentType },
        { PH_ARG_NOKPH, L"nokph", NoArgumentType },
        { PH_ARG_INSTALLKPH, L"installkph", NoArgumentType },
        { PH_ARG_UNINSTALLKPH, L"uninstallkph", NoArgumentType },
        { PH_ARG_DEBUG, L"debug", NoArgumentType },
        { PH_ARG_HWND, L"hwnd", MandatoryArgumentType },
        { PH_ARG_POINT, L"point", MandatoryArgumentType },
        { PH_ARG_SHOWOPTIONS, L"showoptions", NoArgumentType },
        { PH_ARG_PHSVC, L"phsvc", NoArgumentType },
        { PH_ARG_NOPLUGINS, L"noplugins", NoArgumentType },
        { PH_ARG_NEWINSTANCE, L"newinstance", NoArgumentType },
        { PH_ARG_ELEVATE, L"elevate", NoArgumentType },
        { PH_ARG_SILENT, L"s", NoArgumentType },
        { PH_ARG_HELP, L"help", NoArgumentType },
        { PH_ARG_SELECTPID, L"selectpid", MandatoryArgumentType },
        { PH_ARG_PRIORITY, L"priority", MandatoryArgumentType },
        { PH_ARG_PLUGIN, L"plugin", MandatoryArgumentType },
        { PH_ARG_SELECTTAB, L"selecttab", MandatoryArgumentType },
        { PH_ARG_SYSINFO, L"sysinfo", OptionalArgumentType }
    };
    PH_STRINGREF commandLine;

    PhUnicodeStringToStringRef(&NtCurrentPeb()->ProcessParameters->CommandLine, &commandLine);

    memset(&PhStartupParameters, 0, sizeof(PH_STARTUP_PARAMETERS));

    if (!PhParseCommandLine(
        &commandLine,
        options,
        RTL_NUMBER_OF(options),
        PH_COMMAND_LINE_IGNORE_UNKNOWN_OPTIONS | PH_COMMAND_LINE_IGNORE_FIRST_PART,
        PhpCommandLineOptionCallback,
        NULL
        ) || PhStartupParameters.Help)
    {
        PhShowInformation(
            NULL,
            L"%s",
            L"Command line options:\n\n"
            L"-debug\n"
            L"-elevate\n"
            L"-help\n"
            L"-hide\n"
            L"-installkph\n"
            L"-newinstance\n"
            L"-nokph\n"
            L"-noplugins\n"
            L"-nosettings\n"
            L"-plugin pluginname:value\n"
            L"-priority r|h|n|l\n"
            L"-s\n"
            L"-selectpid pid-to-select\n"
            L"-selecttab name-of-tab-to-select\n"
            L"-settings filename\n"
            L"-sysinfo [section-name]\n"
            L"-uninstallkph\n"
            L"-v\n"
            );

        if (PhStartupParameters.Help)
            PhExitApplication(STATUS_SUCCESS);
    }

    if (PhStartupParameters.InstallKph)
    {
        NTSTATUS status;
        PPH_STRING applicationDirectory;
        PPH_STRING kprocesshackerFileName;
        KPH_PARAMETERS parameters;

        if (!(applicationDirectory = PhGetApplicationDirectory()))
            return;

        kprocesshackerFileName = PhConcatStringRefZ(&applicationDirectory->sr, L"\\kprocesshacker.sys");
        PhDereferenceObject(applicationDirectory);

        parameters.SecurityLevel = KphSecuritySignatureCheck;
        parameters.CreateDynamicConfiguration = TRUE;

        status = KphInstallEx(KPH_DEVICE_SHORT_NAME, kprocesshackerFileName->Buffer, &parameters);

        if (!NT_SUCCESS(status) && !PhStartupParameters.Silent)
            PhShowStatus(NULL, L"Unable to install KProcessHacker", status, 0);

        PhExitApplication(status);
    }

    if (PhStartupParameters.UninstallKph)
    {
        NTSTATUS status;

        status = KphUninstall(KPH_DEVICE_SHORT_NAME);

        if (!NT_SUCCESS(status) && !PhStartupParameters.Silent)
            PhShowStatus(NULL, L"Unable to uninstall KProcessHacker", status, 0);

        PhExitApplication(status);
    }

    if (PhStartupParameters.Elevate && !PhGetOwnTokenAttributes().Elevated)
    {
        PhShellProcessHacker(
            NULL,
            NULL,
            SW_SHOW,
            PH_SHELL_EXECUTE_ADMIN,
            PH_SHELL_APP_PROPAGATE_PARAMETERS | PH_SHELL_APP_PROPAGATE_PARAMETERS_FORCE_SETTINGS,
            0,
            NULL
            );
        PhExitApplication(STATUS_SUCCESS);
    }

    if (PhStartupParameters.Debug)
    {
        // The symbol provider won't work if this is chosen.
        PhShowDebugConsole();
    }
}

VOID PhpEnablePrivileges(
    VOID
    )
{
    HANDLE tokenHandle;

    if (NT_SUCCESS(PhOpenProcessToken(
        NtCurrentProcess(),
        TOKEN_ADJUST_PRIVILEGES,
        &tokenHandle
        )))
    {
        CHAR privilegesBuffer[FIELD_OFFSET(TOKEN_PRIVILEGES, Privileges) + sizeof(LUID_AND_ATTRIBUTES) * 9];
        PTOKEN_PRIVILEGES privileges;
        ULONG i;

        privileges = (PTOKEN_PRIVILEGES)privilegesBuffer;
        privileges->PrivilegeCount = 9;

        for (i = 0; i < privileges->PrivilegeCount; i++)
        {
            privileges->Privileges[i].Attributes = SE_PRIVILEGE_ENABLED;
            privileges->Privileges[i].Luid.HighPart = 0;
        }

        privileges->Privileges[0].Luid.LowPart = SE_DEBUG_PRIVILEGE;
        privileges->Privileges[1].Luid.LowPart = SE_INC_BASE_PRIORITY_PRIVILEGE;
        privileges->Privileges[2].Luid.LowPart = SE_INC_WORKING_SET_PRIVILEGE;
        privileges->Privileges[3].Luid.LowPart = SE_LOAD_DRIVER_PRIVILEGE;
        privileges->Privileges[4].Luid.LowPart = SE_PROF_SINGLE_PROCESS_PRIVILEGE;
        privileges->Privileges[5].Luid.LowPart = SE_BACKUP_PRIVILEGE;
        privileges->Privileges[6].Luid.LowPart = SE_RESTORE_PRIVILEGE;
        privileges->Privileges[7].Luid.LowPart = SE_SHUTDOWN_PRIVILEGE;
        privileges->Privileges[8].Luid.LowPart = SE_TAKE_OWNERSHIP_PRIVILEGE;

        NtAdjustPrivilegesToken(
            tokenHandle,
            FALSE,
            privileges,
            0,
            NULL,
            NULL
            );

        NtClose(tokenHandle);
    }
}

// CRT delayload support
// NOTE: The default delayload handler throws exceptions
// when imports are unavailable instead of returning NULL
// breaking backwards compatibility. (dmex)
// TODO: Move to a better location. (dmex)

_Success_(return != NULL)
PVOID WINAPI __delayLoadHelper2(
    _In_ PIMAGE_DELAYLOAD_DESCRIPTOR Entry,
    _Inout_ PVOID* ImportAddress
    )
{
    BOOLEAN importNeedsFree = FALSE;
    PSTR importDllName;
    PVOID procedureAddress;
    PVOID moduleHandle;
    PVOID* importHandle;
    PIMAGE_THUNK_DATA importEntry;
    PIMAGE_THUNK_DATA importTable;
    PIMAGE_THUNK_DATA importNameTable;
    PIMAGE_NT_HEADERS imageNtHeaders;
    SIZE_T importDirectorySectionSize;
    PVOID importDirectorySectionAddress;
    ULONG importSectionProtect;

    importDllName = PTR_ADD_OFFSET(PhInstanceHandle, Entry->DllNameRVA);
    importHandle = PTR_ADD_OFFSET(PhInstanceHandle, Entry->ModuleHandleRVA);
    importTable = PTR_ADD_OFFSET(PhInstanceHandle, Entry->ImportAddressTableRVA);
    importNameTable = PTR_ADD_OFFSET(PhInstanceHandle, Entry->ImportNameTableRVA);

    if (!(moduleHandle = *importHandle))
    {
        PPH_STRING importDllNameSr = PhZeroExtendToUtf16(importDllName);

        if (!(moduleHandle = PhLoadLibrary(importDllNameSr->Buffer)))
        {
            PhDereferenceObject(importDllNameSr);
            return NULL;
        }

        PhDereferenceObject(importDllNameSr);
        importNeedsFree = TRUE;
    }

    importEntry = PTR_ADD_OFFSET(importNameTable, PTR_SUB_OFFSET(ImportAddress, importTable));

    if (IMAGE_SNAP_BY_ORDINAL(importEntry->u1.Ordinal))
    {
        USHORT procedureOrdinal = IMAGE_ORDINAL(importEntry->u1.Ordinal);
        procedureAddress = PhGetDllBaseProcedureAddress(moduleHandle, NULL, procedureOrdinal);
    }
    else
    {
        PIMAGE_IMPORT_BY_NAME importByName = PTR_ADD_OFFSET(PhInstanceHandle, importEntry->u1.AddressOfData);
        procedureAddress = PhGetDllBaseProcedureAddressWithHint(moduleHandle, importByName->Name, importByName->Hint);
    }

    if (!procedureAddress)
        return NULL;

    if (!NT_SUCCESS(PhGetLoaderEntryImageNtHeaders(
        PhInstanceHandle,
        &imageNtHeaders
        )))
    {
        return NULL;
    }

    if (!NT_SUCCESS(PhGetLoaderEntryImageVaToSection(
        PhInstanceHandle,
        imageNtHeaders,
        importTable,
        &importDirectorySectionAddress,
        &importDirectorySectionSize
        )))
    {
        return NULL;
    }

    if (!NT_SUCCESS(NtProtectVirtualMemory(
        NtCurrentProcess(),
        &importDirectorySectionAddress,
        &importDirectorySectionSize,
        PAGE_READWRITE,
        &importSectionProtect
        )))
    {
        return NULL;
    }

    InterlockedExchangePointer(ImportAddress, procedureAddress);

    if (!NT_SUCCESS(NtProtectVirtualMemory(
        NtCurrentProcess(),
        &importDirectorySectionAddress,
        &importDirectorySectionSize,
        importSectionProtect,
        &importSectionProtect
        )))
    {
        return NULL;
    }

    if ((InterlockedExchangePointer(importHandle, moduleHandle) == moduleHandle) && importNeedsFree)
    {
        FreeLibrary(moduleHandle); // A different thread has already updated the cache. (dmex)
    }

    return procedureAddress;
}
