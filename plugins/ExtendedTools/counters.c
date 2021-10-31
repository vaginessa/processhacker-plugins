/*
 * Process Hacker .NET Tools -
 *   GPU performance counters
 *
 * Copyright (C) 2019-2021 dmex
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
#include <winperf.h>
#include <pdh.h>
#include <pdhmsg.h>

PPH_HASHTABLE EtGpuRunningTimeHashTable = NULL;
PH_QUEUED_LOCK EtGpuRunningTimeHashTableLock = PH_QUEUED_LOCK_INIT;
PPH_HASHTABLE EtGpuDedicatedHashTable = NULL;
PH_QUEUED_LOCK EtGpuDedicatedHashTableLock = PH_QUEUED_LOCK_INIT;
PPH_HASHTABLE EtGpuSharedHashTable = NULL;
PH_QUEUED_LOCK EtGpuSharedHashTableLock = PH_QUEUED_LOCK_INIT;
PPH_HASHTABLE EtGpuCommitHashTable = NULL;
PH_QUEUED_LOCK EtGpuCommitHashTableLock = PH_QUEUED_LOCK_INIT;
PPH_HASHTABLE EtGpuAdapterDedicatedHashTable = NULL;
PH_QUEUED_LOCK EtGpuAdapterDedicatedHashTableLock = PH_QUEUED_LOCK_INIT;

NTSTATUS NTAPI EtGpuCounterQueryThread(
    _In_ PVOID ThreadParameter
    );

typedef struct _ET_GPU_COUNTER
{
    ULONG64 Node;
    HANDLE ProcessId;
    ULONG EngineId;
    LUID EngineLuid;

    union
    {
        DOUBLE Value;
        ULONG64 Value64;
        FLOAT ValueF;
    };
} ET_GPU_COUNTER, * PET_GPU_COUNTER;

typedef struct _ET_GPU_ADAPTER_COUNTER
{
    LUID EngineLuid;
    ULONG PhysicalId;

    union
    {
        DOUBLE Value;
        ULONG64 Value64;
        FLOAT ValueF;
    };
} ET_GPU_ADAPTER_COUNTER, *PET_GPU_ADAPTER_COUNTER;

//typedef struct _ET_COUNTER_CONFIG
//{
//    PWSTR Path;
//    PDH_HCOUNTER Handle;
//} ET_COUNTER_CONFIG, *PET_COUNTER_CONFIG;
//
//static ET_COUNTER_CONFIG CounterConfigArray[] =
//{
//    { L"\\GPU Engine(*)\\Running Time", NULL },
//    { L"\\GPU Process Memory(*)\\Dedicated Usage", NULL },
//    { L"\\GPU Process Memory(*)\\Shared Usage", NULL },
//};

static BOOLEAN NTAPI EtpRunningTimeEqualFunction(
    _In_ PVOID Entry1,
    _In_ PVOID Entry2
    )
{
    PET_GPU_COUNTER entry1 = Entry1;
    PET_GPU_COUNTER entry2 = Entry2;

    return entry1->ProcessId == entry2->ProcessId &&
        entry1->EngineId == entry2->EngineId &&
        RtlIsEqualLuid(&entry1->EngineLuid, &entry2->EngineLuid);
}

static ULONG NTAPI EtpEtpRunningTimeHashFunction(
    _In_ PVOID Entry
    )
{
    PET_GPU_COUNTER entry = Entry;

    return (HandleToUlong(entry->ProcessId) / 4) ^
        PhHashInt32(entry->EngineId) ^
        PhHashInt32(entry->EngineLuid.LowPart) ^
        PhHashInt32(entry->EngineLuid.HighPart);
}

static BOOLEAN NTAPI EtpDedicatedEqualFunction(
    _In_ PVOID Entry1,
    _In_ PVOID Entry2
    )
{
    PET_GPU_COUNTER entry1 = Entry1;
    PET_GPU_COUNTER entry2 = Entry2;

    return entry1->ProcessId == entry2->ProcessId &&
        RtlIsEqualLuid(&entry1->EngineLuid, &entry2->EngineLuid);
}

static ULONG NTAPI EtpDedicatedHashFunction(
    _In_ PVOID Entry
    )
{
    PET_GPU_COUNTER entry = Entry;

    return (HandleToUlong(entry->ProcessId) / 4) ^
        PhHashInt32(entry->EngineLuid.LowPart) ^
        PhHashInt32(entry->EngineLuid.HighPart);
}

static ULONG NTAPI EtAdapterDedicatedHashFunction(
    _In_ PVOID Entry
    )
{
    PET_GPU_ADAPTER_COUNTER entry = Entry;

    return PhHashInt32(entry->EngineLuid.LowPart) ^
        PhHashInt32(entry->EngineLuid.HighPart);
}

static BOOLEAN NTAPI EtAdapterDedicatedEqualFunction(
    _In_ PVOID Entry1,
    _In_ PVOID Entry2
    )
{
    PET_GPU_ADAPTER_COUNTER entry1 = Entry1;
    PET_GPU_ADAPTER_COUNTER entry2 = Entry2;

    return entry1->EngineLuid.LowPart == entry2->EngineLuid.LowPart &&
        entry1->EngineLuid.HighPart == entry2->EngineLuid.HighPart;
}

VOID EtGpuCreateRunningTimeHashTable(
    VOID
    )
{
    EtGpuRunningTimeHashTable = PhCreateHashtable(
        sizeof(ET_GPU_COUNTER),
        EtpRunningTimeEqualFunction,
        EtpEtpRunningTimeHashFunction,
        10
        );
}

VOID EtGpuCreateDedicatedHashTable(
    VOID
    )
{
    EtGpuDedicatedHashTable = PhCreateHashtable(
        sizeof(ET_GPU_COUNTER),
        EtpDedicatedEqualFunction,
        EtpDedicatedHashFunction,
        10
        );
}

VOID EtGpuCreateSharedHashTable(
    VOID
    )
{
    EtGpuSharedHashTable = PhCreateHashtable(
        sizeof(ET_GPU_COUNTER),
        EtpDedicatedEqualFunction,
        EtpDedicatedHashFunction,
        10
        );
}

VOID EtGpuCreateCommitHashTable(
    VOID
    )
{
    EtGpuCommitHashTable = PhCreateHashtable(
        sizeof(ET_GPU_COUNTER),
        EtpDedicatedEqualFunction,
        EtpDedicatedHashFunction,
        10
        );
}

VOID EtGpuCreateAdapterDedicatedHashTable(
    VOID
    )
{
    EtGpuAdapterDedicatedHashTable = PhCreateHashtable(
        sizeof(ET_GPU_ADAPTER_COUNTER),
        EtAdapterDedicatedEqualFunction,
        EtAdapterDedicatedHashFunction,
        2
        );
}

VOID EtGpuCountersInitialization(
    VOID
    )
{
    static PH_INITONCE initOnce = PH_INITONCE_INIT;

    if (PhBeginInitOnce(&initOnce))
    {
        EtGpuCreateRunningTimeHashTable();
        EtGpuCreateDedicatedHashTable();
        EtGpuCreateSharedHashTable();
        EtGpuCreateCommitHashTable();
        EtGpuCreateAdapterDedicatedHashTable();

        PhCreateThread2(EtGpuCounterQueryThread, NULL);

        PhEndInitOnce(&initOnce);
    }
}

FLOAT EtLookupProcessGpuUtilization(
    _In_ HANDLE ProcessId
    )
{
    FLOAT value = 0;
    ULONG enumerationKey;
    PET_GPU_COUNTER entry;

    if (!EtGpuRunningTimeHashTable)
    {
        EtGpuCountersInitialization();
        return 0;
    }

    PhAcquireQueuedLockShared(&EtGpuRunningTimeHashTableLock);

    enumerationKey = 0;

    while (PhEnumHashtable(EtGpuRunningTimeHashTable, (PVOID*)&entry, &enumerationKey))
    {
        if (entry->ProcessId == ProcessId)
        {
            value += entry->ValueF;
        }
    }

    PhReleaseQueuedLockShared(&EtGpuRunningTimeHashTableLock);

    if (value > 0)
        value = value / 100;

    return value;
}

FLOAT EtLookupTotalGpuUtilization(
    VOID
    )
{
    FLOAT value = 0;
    ULONG enumerationKey;
    PET_GPU_COUNTER entry;

    if (!EtGpuRunningTimeHashTable)
    {
        EtGpuCountersInitialization();
        return 0;
    }

    PhAcquireQueuedLockShared(&EtGpuRunningTimeHashTableLock);

    enumerationKey = 0;

    while (PhEnumHashtable(EtGpuRunningTimeHashTable, (PVOID*)&entry, &enumerationKey))
    {
        FLOAT usage = entry->ValueF;

        if (usage > value)
            value = usage;
    }

    PhReleaseQueuedLockShared(&EtGpuRunningTimeHashTableLock);

    if (value > 0)
        value = value / 100;

    return value;
}

FLOAT EtLookupTotalGpuEngineUtilization(
    _In_ ULONG EngineId
    )
{
    FLOAT value = 0;
    ULONG enumerationKey;
    PET_GPU_COUNTER entry;

    if (!EtGpuRunningTimeHashTable)
    {
        EtGpuCountersInitialization();
        return 0;
    }

    PhAcquireQueuedLockShared(&EtGpuRunningTimeHashTableLock);

    enumerationKey = 0;

    while (PhEnumHashtable(EtGpuRunningTimeHashTable, (PVOID*)&entry, &enumerationKey))
    {
        if (entry->EngineId == EngineId)
        {
            FLOAT usage = entry->ValueF;

            if (usage > value)
                value = usage;
        }
    }

    PhReleaseQueuedLockShared(&EtGpuRunningTimeHashTableLock);

    if (value > 0)
        value = value / 100;

    return value;
}

ULONG64 EtLookupProcessGpuDedicated(
    _In_opt_ HANDLE ProcessId
    )
{
    ULONG64 value = 0;
    ULONG enumerationKey;
    PET_GPU_COUNTER entry;

    if (!EtGpuDedicatedHashTable)
    {
        EtGpuCountersInitialization();
        return 0;
    }

    PhAcquireQueuedLockShared(&EtGpuDedicatedHashTableLock);

    enumerationKey = 0;

    while (PhEnumHashtable(EtGpuDedicatedHashTable, (PVOID*)&entry, &enumerationKey))
    {
        if (entry->ProcessId == ProcessId)
        {
            value += entry->Value64;
        }
    }

    PhReleaseQueuedLockShared(&EtGpuDedicatedHashTableLock);

    return value;
}

ULONG64 EtLookupTotalProcessGpuDedicated(
    VOID
    )
{
    ULONG64 value = 0;
    ULONG enumerationKey;
    PET_GPU_COUNTER entry;

    if (!EtGpuDedicatedHashTable)
    {
        EtGpuCountersInitialization();
        return 0;
    }

    PhAcquireQueuedLockShared(&EtGpuDedicatedHashTableLock);

    enumerationKey = 0;

    while (PhEnumHashtable(EtGpuDedicatedHashTable, (PVOID*)&entry, &enumerationKey))
    {
        value += entry->Value64;
    }

    PhReleaseQueuedLockShared(&EtGpuDedicatedHashTableLock);

    return value;
}

ULONG64 EtLookupTotalAdapterGpuDedicated(
    VOID
    )
{
    ULONG64 value = 0;
    ULONG enumerationKey;
    PET_GPU_ADAPTER_COUNTER entry;

    if (!EtGpuAdapterDedicatedHashTable)
    {
        EtGpuCountersInitialization();
        return 0;
    }

    PhAcquireQueuedLockShared(&EtGpuAdapterDedicatedHashTableLock);

    enumerationKey = 0;

    while (PhEnumHashtable(EtGpuAdapterDedicatedHashTable, (PVOID*)&entry, &enumerationKey))
    {
        value += entry->Value64;
    }

    PhReleaseQueuedLockShared(&EtGpuAdapterDedicatedHashTableLock);

    return value;
}

ULONG64 EtLookupProcessGpuSharedUsage(
    _In_opt_ HANDLE ProcessId
    )
{
    ULONG64 value = 0;
    ULONG enumerationKey;
    PET_GPU_COUNTER entry;

    if (!EtGpuSharedHashTable)
    {
        EtGpuCountersInitialization();
        return 0;
    }

    PhAcquireQueuedLockShared(&EtGpuSharedHashTableLock);

    enumerationKey = 0;

    while (PhEnumHashtable(EtGpuSharedHashTable, (PVOID*)&entry, &enumerationKey))
    {
        if (entry->ProcessId == ProcessId)
        {
            value += entry->Value64;
        }
    }

    PhReleaseQueuedLockShared(&EtGpuSharedHashTableLock);

    return value;
}

ULONG64 EtLookupTotalGpuShared(
    VOID
    )
{
    ULONG64 value = 0;
    ULONG enumerationKey;
    PET_GPU_COUNTER entry;

    if (!EtGpuSharedHashTable)
    {
        EtGpuCountersInitialization();
        return 0;
    }

    PhAcquireQueuedLockShared(&EtGpuSharedHashTableLock);

    enumerationKey = 0;

    while (PhEnumHashtable(EtGpuSharedHashTable, (PVOID*)&entry, &enumerationKey))
    {
        value += entry->Value64;
    }

    PhReleaseQueuedLockShared(&EtGpuSharedHashTableLock);

    return value;
}

ULONG64 EtLookupProcessGpuCommitUsage(
    _In_opt_ HANDLE ProcessId
    )
{  
    ULONG64 value = 0;
    ULONG enumerationKey;
    PET_GPU_COUNTER entry;

    if (!EtGpuCommitHashTable)
    {
        EtGpuCountersInitialization();
        return 0;
    }

    PhAcquireQueuedLockShared(&EtGpuCommitHashTableLock);

    enumerationKey = 0;

    while (PhEnumHashtable(EtGpuCommitHashTable, (PVOID*)&entry, &enumerationKey))
    {
        if (entry->ProcessId == ProcessId)
        {
            value += entry->Value64;
        }
    }

    PhReleaseQueuedLockShared(&EtGpuCommitHashTableLock);

    return value;
}

ULONG64 EtLookupTotalGpuCommit(
    VOID
    )
{
    ULONG64 value = 0;
    ULONG enumerationKey;
    PET_GPU_COUNTER entry;

    if (!EtGpuCommitHashTable)
    {
        EtGpuCountersInitialization();
        return 0;
    }

    PhAcquireQueuedLockShared(&EtGpuCommitHashTableLock);

    enumerationKey = 0;

    while (PhEnumHashtable(EtGpuCommitHashTable, (PVOID*)&entry, &enumerationKey))
    {
        value += entry->Value64;
    }

    PhReleaseQueuedLockShared(&EtGpuCommitHashTableLock);

    return value;
}

VOID ParseGpuEngineUtilizationCounter(
    _In_ PWSTR InstanceName,
    _In_ DOUBLE InstanceValue
    )
{
    PH_STRINGREF pidPartSr;
    PH_STRINGREF luidHighPartSr;
    PH_STRINGREF luidLowPartSr;
    PH_STRINGREF physPartSr;
    PH_STRINGREF engPartSr;
    //PH_STRINGREF engTypePartSr;
    PH_STRINGREF remainingPart;
    ULONG64 processId;
    ULONG64 engineId;
    LONG64 engineLuidLow;
    //LONG64 engineLuidHigh;
    PET_GPU_COUNTER entry;
    ET_GPU_COUNTER lookupEntry;

    if (!EtGpuRunningTimeHashTable)
        return;

    // pid_12704_luid_0x00000000_0x0000D503_phys_0_eng_3_engtype_VideoDecode
    PhInitializeStringRefLongHint(&remainingPart, InstanceName);

    PhSkipStringRef(&remainingPart, 4 * sizeof(WCHAR));

    if (!PhSplitStringRefAtChar(&remainingPart, L'_', &pidPartSr, &remainingPart))
        return;

    PhSkipStringRef(&remainingPart, 5 * sizeof(WCHAR));

    if (!PhSplitStringRefAtChar(&remainingPart, L'_', &luidHighPartSr, &remainingPart))
        return;
    if (!PhSplitStringRefAtChar(&remainingPart, L'_', &luidLowPartSr, &remainingPart))
        return;

    PhSkipStringRef(&remainingPart, 5 * sizeof(WCHAR));

    if (!PhSplitStringRefAtChar(&remainingPart, L'_', &physPartSr, &remainingPart))
        return;

    PhSkipStringRef(&remainingPart, 4 * sizeof(WCHAR));

    if (!PhSplitStringRefAtChar(&remainingPart, L'_', &engPartSr, &remainingPart))
        return;

    //PhSkipStringRef(&remainingPart, 8 * sizeof(WCHAR));
    //PhSplitStringRefAtChar(&remainingPart, L'_', &engTypePartSr, &remainingPart);
    //PhSkipStringRef(&luidHighPartSr, 2 * sizeof(WCHAR));
    PhSkipStringRef(&luidLowPartSr, 2 * sizeof(WCHAR));

    if (
        PhStringToInteger64(&pidPartSr, 10, &processId) &&
        //PhStringToInteger64(&luidHighPartSr, 16, &engineLuidHigh) &&
        PhStringToInteger64(&luidLowPartSr, 16, &engineLuidLow) &&
        PhStringToInteger64(&engPartSr, 10, &engineId)
        )
    {
        lookupEntry.ProcessId = (HANDLE)processId;
        lookupEntry.EngineId = (ULONG)engineId;
        lookupEntry.EngineLuid.LowPart = (ULONG)engineLuidLow;
        lookupEntry.EngineLuid.HighPart = 0; // (LONG)engineLuidHigh;

        if (entry = PhFindEntryHashtable(EtGpuRunningTimeHashTable, &lookupEntry))
        {
            //if (entry->ValueF < (FLOAT)InstanceValue)
            entry->ValueF = (FLOAT)InstanceValue;
        }
        else
        {
            lookupEntry.ProcessId = (HANDLE)processId;
            lookupEntry.EngineId = (ULONG)engineId;
            lookupEntry.EngineLuid.LowPart = (ULONG)engineLuidLow;
            lookupEntry.EngineLuid.HighPart = 0; // (LONG)engineLuidHigh;
            lookupEntry.ValueF = (FLOAT)InstanceValue;

            PhAddEntryHashtable(EtGpuRunningTimeHashTable, &lookupEntry);
        }
    }
}

VOID ParseGpuProcessMemoryDedicatedUsageCounter(
    _In_ PWSTR InstanceName,
    _In_ ULONG64 InstanceValue
    )
{
    PH_STRINGREF pidPartSr;
    PH_STRINGREF luidHighPartSr;
    PH_STRINGREF luidLowPartSr;
    //PH_STRINGREF physPartSr;
    PH_STRINGREF remainingPart;
    ULONG64 processId;
    LONG64 engineLuidLow;
    //LONG64 engineLuidHigh;
    PET_GPU_COUNTER entry;
    ET_GPU_COUNTER lookupEntry;

    if (!EtGpuDedicatedHashTable)
        return;

    // pid_1116_luid_0x00000000_0x0000D3EC_phys_0
    PhInitializeStringRefLongHint(&remainingPart, InstanceName);

    PhSkipStringRef(&remainingPart, 4 * sizeof(WCHAR));

    if (!PhSplitStringRefAtChar(&remainingPart, L'_', &pidPartSr, &remainingPart))
        return;

    PhSkipStringRef(&remainingPart, 5 * sizeof(WCHAR));

    if (!PhSplitStringRefAtChar(&remainingPart, L'_', &luidHighPartSr, &remainingPart))
        return;
    if (!PhSplitStringRefAtChar(&remainingPart, L'_', &luidLowPartSr, &remainingPart))
        return;

    //PhSkipStringRef(&remainingPart, 8 * sizeof(WCHAR));
    //PhSplitStringRefAtChar(&remainingPart, L'_', &engTypePartSr, &remainingPart);
    //PhSkipStringRef(&luidHighPartSr, 2 * sizeof(WCHAR));
    PhSkipStringRef(&luidLowPartSr, 2 * sizeof(WCHAR));

    if (
        PhStringToInteger64(&pidPartSr, 10, &processId) &&
        //PhStringToInteger64(&luidHighPartSr, 16, &engineLuidHigh) &&
        PhStringToInteger64(&luidLowPartSr, 16, &engineLuidLow)
        )
    {
        lookupEntry.ProcessId = (HANDLE)processId;
        lookupEntry.EngineLuid.LowPart = (ULONG)engineLuidLow;
        lookupEntry.EngineLuid.HighPart = 0; // (LONG)engineLuidHigh;

        if (entry = PhFindEntryHashtable(EtGpuDedicatedHashTable, &lookupEntry))
        {
            //if (entry->Value64 < InstanceValue)
            entry->Value64 = InstanceValue;
        }
        else
        {
            lookupEntry.ProcessId = (HANDLE)processId;
            lookupEntry.EngineLuid.LowPart = (ULONG)engineLuidLow;
            lookupEntry.EngineLuid.HighPart = 0; // (LONG)engineLuidHigh;
            lookupEntry.Value64 = InstanceValue;

            PhAddEntryHashtable(EtGpuDedicatedHashTable, &lookupEntry);
        }
    }
}

VOID ParseGpuProcessMemorySharedUsageCounter(
    _In_ PWSTR InstanceName,
    _In_ ULONG64 InstanceValue
    )
{
    PH_STRINGREF pidPartSr;
    PH_STRINGREF luidHighPartSr;
    PH_STRINGREF luidLowPartSr;
    //PH_STRINGREF physPartSr;
    PH_STRINGREF remainingPart;
    ULONG64 processId;
    LONG64 engineLuidLow;
    //LONG64 engineLuidHigh;
    PET_GPU_COUNTER entry;
    ET_GPU_COUNTER lookupEntry;

    if (!EtGpuSharedHashTable)
        return;

    // pid_1116_luid_0x00000000_0x0000D3EC_phys_0
    PhInitializeStringRefLongHint(&remainingPart, InstanceName);

    PhSkipStringRef(&remainingPart, 4 * sizeof(WCHAR));

    if (!PhSplitStringRefAtChar(&remainingPart, L'_', &pidPartSr, &remainingPart))
        return;

    PhSkipStringRef(&remainingPart, 5 * sizeof(WCHAR));

    if (!PhSplitStringRefAtChar(&remainingPart, L'_', &luidHighPartSr, &remainingPart))
        return;
    if (!PhSplitStringRefAtChar(&remainingPart, L'_', &luidLowPartSr, &remainingPart))
        return;

    //PhSkipStringRef(&remainingPart, 8 * sizeof(WCHAR));
    //PhSplitStringRefAtChar(&remainingPart, L'_', &engTypePartSr, &remainingPart);
    PhSkipStringRef(&luidHighPartSr, 2 * sizeof(WCHAR));
    PhSkipStringRef(&luidLowPartSr, 2 * sizeof(WCHAR));

    if (
        PhStringToInteger64(&pidPartSr, 10, &processId) &&
        //PhStringToInteger64(&luidHighPartSr, 16, &engineLuidHigh) && 
        PhStringToInteger64(&luidLowPartSr, 16, &engineLuidLow)
        )
    {
        lookupEntry.ProcessId = (HANDLE)processId;
        lookupEntry.EngineLuid.LowPart = (ULONG)engineLuidLow;
        lookupEntry.EngineLuid.HighPart = 0; // (LONG)engineLuidHigh;

        if (entry = PhFindEntryHashtable(EtGpuSharedHashTable, &lookupEntry))
        {
            //if (entry->Value64 < InstanceValue)
            entry->Value64 = InstanceValue;
        }
        else
        {
            lookupEntry.ProcessId = (HANDLE)processId;
            lookupEntry.EngineLuid.LowPart = (ULONG)engineLuidLow;
            lookupEntry.EngineLuid.HighPart = 0; // (LONG)engineLuidHigh;
            lookupEntry.Value64 = InstanceValue;

            PhAddEntryHashtable(EtGpuSharedHashTable, &lookupEntry);
        }
    }
}

VOID ParseGpuProcessMemoryCommitUsageCounter(
    _In_ PWSTR InstanceName,
    _In_ ULONG64 InstanceValue
    )
{
    PH_STRINGREF pidPartSr;
    PH_STRINGREF luidHighPartSr;
    PH_STRINGREF luidLowPartSr;
    //PH_STRINGREF physPartSr;
    PH_STRINGREF remainingPart;
    ULONG64 processId;
    LONG64 engineLuidLow;
    //LONG64 engineLuidHigh;
    PET_GPU_COUNTER entry;
    ET_GPU_COUNTER lookupEntry;

    if (!EtGpuCommitHashTable)
        return;

    // pid_1116_luid_0x00000000_0x0000D3EC_phys_0
    PhInitializeStringRefLongHint(&remainingPart, InstanceName);

    PhSkipStringRef(&remainingPart, 4 * sizeof(WCHAR));

    if (!PhSplitStringRefAtChar(&remainingPart, L'_', &pidPartSr, &remainingPart))
        return;

    PhSkipStringRef(&remainingPart, 5 * sizeof(WCHAR));

    if (!PhSplitStringRefAtChar(&remainingPart, L'_', &luidHighPartSr, &remainingPart))
        return;
    if (!PhSplitStringRefAtChar(&remainingPart, L'_', &luidLowPartSr, &remainingPart))
        return;

    //PhSkipStringRef(&remainingPart, 8 * sizeof(WCHAR));
    //PhSplitStringRefAtChar(&remainingPart, L'_', &engTypePartSr, &remainingPart);
    //PhSkipStringRef(&luidHighPartSr, 2 * sizeof(WCHAR));
    PhSkipStringRef(&luidLowPartSr, 2 * sizeof(WCHAR));

    if (
        PhStringToInteger64(&pidPartSr, 10, &processId) &&
        //PhStringToInteger64(&luidHighPartSr, 16, &engineLuidHigh) &&
        PhStringToInteger64(&luidLowPartSr, 16, &engineLuidLow)
        )
    {
        lookupEntry.ProcessId = (HANDLE)processId;
        lookupEntry.EngineLuid.LowPart = (ULONG)engineLuidLow;
        lookupEntry.EngineLuid.HighPart = 0; // (LONG)engineLuidHigh;

        if (entry = PhFindEntryHashtable(EtGpuCommitHashTable, &lookupEntry))
        {
            //if (entry->Value64 < InstanceValue)
            entry->Value64 = InstanceValue;
        }
        else
        {
            lookupEntry.ProcessId = (HANDLE)processId;
            lookupEntry.EngineLuid.LowPart = (ULONG)engineLuidLow;
            lookupEntry.EngineLuid.HighPart = 0; // (LONG)engineLuidHigh;
            lookupEntry.Value64 = InstanceValue;

            PhAddEntryHashtable(EtGpuCommitHashTable, &lookupEntry);
        }
    }
}

VOID ParseGpuAdapterDedicatedUsageCounter(
    _In_ PWSTR InstanceName,
    _In_ ULONG64 InstanceValue
    )
{
    PH_STRINGREF luidHighPartSr;
    PH_STRINGREF luidLowPartSr;
    //PH_STRINGREF physPartSr;
    PH_STRINGREF remainingPart;
    LONG64 engineLuidLow;
    //LONG64 engineLuidHigh;
    PET_GPU_ADAPTER_COUNTER entry;
    ET_GPU_ADAPTER_COUNTER lookupEntry;

    if (!EtGpuAdapterDedicatedHashTable)
        return;

    // luid_0x00000000_0x0000C4CF_phys_0
    PhInitializeStringRefLongHint(&remainingPart, InstanceName);

    PhSkipStringRef(&remainingPart, 5 * sizeof(WCHAR));

    if (!PhSplitStringRefAtChar(&remainingPart, L'_', &luidHighPartSr, &remainingPart))
        return;
    if (!PhSplitStringRefAtChar(&remainingPart, L'_', &luidLowPartSr, &remainingPart))
        return;

    //PhSkipStringRef(&remainingPart, 8 * sizeof(WCHAR));
    //PhSplitStringRefAtChar(&remainingPart, L'_', &engTypePartSr, &remainingPart);
    //PhSkipStringRef(&luidHighPartSr, 2 * sizeof(WCHAR));
    PhSkipStringRef(&luidLowPartSr, 2 * sizeof(WCHAR));

    if (//PhStringToInteger64(&luidHighPartSr, 16, &engineLuidHigh) &&
        PhStringToInteger64(&luidLowPartSr, 16, &engineLuidLow)
        )
    {
        lookupEntry.EngineLuid.LowPart = (ULONG)engineLuidLow;
        lookupEntry.EngineLuid.HighPart = 0; // (LONG)engineLuidHigh;

        if (entry = PhFindEntryHashtable(EtGpuAdapterDedicatedHashTable, &lookupEntry))
        {
            //if (entry->Value64 < InstanceValue)
            entry->Value64 = InstanceValue;
        }
        else
        {
            lookupEntry.EngineLuid.LowPart = (ULONG)engineLuidLow;
            lookupEntry.EngineLuid.HighPart = 0; // (LONG)engineLuidHigh;
            lookupEntry.Value64 = InstanceValue;

            PhAddEntryHashtable(EtGpuAdapterDedicatedHashTable, &lookupEntry);
        }
    }
}

_Success_(return)
BOOLEAN GetCounterArrayBuffer(
    _In_ PDH_HCOUNTER CounterHandle,
    _In_ ULONG Format,
    _Out_ ULONG *ArrayCount,
    _Out_ PPDH_FMT_COUNTERVALUE_ITEM *Array
    )
{
    PDH_STATUS status;
    ULONG bufferLength = 0;
    ULONG bufferCount = 0;
    PPDH_FMT_COUNTERVALUE_ITEM buffer = NULL;

    status = PdhGetFormattedCounterArray(
        CounterHandle,
        Format,
        &bufferLength,
        &bufferCount,
        NULL
        );

    if (status == PDH_MORE_DATA)
    {
        buffer = PhAllocate(bufferLength);

        status = PdhGetFormattedCounterArray(
            CounterHandle,
            Format,
            &bufferLength,
            &bufferCount,
            buffer
            );
    }

    if (status == ERROR_SUCCESS)
    {
        if (ArrayCount)
        {
            *ArrayCount = bufferCount;
        }

        if (Array)
        {
            *Array = buffer;
        }

        return TRUE;
    }

    if (buffer)
        PhFree(buffer);

    return FALSE;
}

NTSTATUS NTAPI EtGpuCounterQueryThread(
    _In_ PVOID ThreadParameter
    )
{
    HANDLE gpuCounterQueryEvent = NULL;
    PDH_HQUERY gpuPerfCounterQueryHandle = NULL;
    PDH_HCOUNTER gpuPerfCounterRunningTimeHandle = NULL;
    PDH_HCOUNTER gpuPerfCounterDedicatedUsageHandle = NULL;
    PDH_HCOUNTER gpuPerfCounterSharedUsageHandle = NULL;
    PDH_HCOUNTER gpuPerfCounterCommittedUsageHandle = NULL;
    PDH_HCOUNTER gpuPerfCounterAdapterDedicatedUsageHandle = NULL;
    PPDH_FMT_COUNTERVALUE_ITEM buffer;
    ULONG bufferCount;

    if (!NT_SUCCESS(NtCreateEvent(&gpuCounterQueryEvent, EVENT_ALL_ACCESS, NULL, SynchronizationEvent, FALSE)))
        goto CleanupExit;

    if (PdhOpenQuery(NULL, 0, &gpuPerfCounterQueryHandle) != ERROR_SUCCESS)
        goto CleanupExit;

    // \GPU Engine(*)\Running Time
    // \GPU Engine(*)\Utilization Percentage
    // \GPU Process Memory(*)\Shared Usage
    // \GPU Process Memory(*)\Dedicated Usage
    // \GPU Process Memory(*)\Non Local Usage
    // \GPU Process Memory(*)\Local Usage
    // \GPU Adapter Memory(*)\Shared Usage
    // \GPU Adapter Memory(*)\Dedicated Usage
    // \GPU Adapter Memory(*)\Total Committed
    // \GPU Local Adapter Memory(*)\Local Usage
    // \GPU Non Local Adapter Memory(*)\Non Local Usage

    if (PdhAddCounter(gpuPerfCounterQueryHandle, L"\\GPU Engine(*)\\Utilization Percentage", 0, &gpuPerfCounterRunningTimeHandle) != ERROR_SUCCESS)
        goto CleanupExit;
    if (PdhAddCounter(gpuPerfCounterQueryHandle, L"\\GPU Process Memory(*)\\Shared Usage", 0, &gpuPerfCounterSharedUsageHandle) != ERROR_SUCCESS)
        goto CleanupExit;
    if (PdhAddCounter(gpuPerfCounterQueryHandle, L"\\GPU Process Memory(*)\\Dedicated Usage", 0, &gpuPerfCounterDedicatedUsageHandle) != ERROR_SUCCESS)
        goto CleanupExit;
    if (PdhAddCounter(gpuPerfCounterQueryHandle, L"\\GPU Process Memory(*)\\Total Committed", 0, &gpuPerfCounterCommittedUsageHandle) != ERROR_SUCCESS)
        goto CleanupExit;
    if (PdhAddCounter(gpuPerfCounterQueryHandle, L"\\GPU Adapter Memory(*)\\Dedicated Usage", 0, &gpuPerfCounterAdapterDedicatedUsageHandle) != ERROR_SUCCESS)
        goto CleanupExit;

    if (PdhCollectQueryDataEx(gpuPerfCounterQueryHandle, 1, gpuCounterQueryEvent) != ERROR_SUCCESS)
        goto CleanupExit;

    for (;;)
    {
        if (NtWaitForSingleObject(gpuCounterQueryEvent, FALSE, NULL) != WAIT_OBJECT_0)
            break;

        if (EtGpuRunningTimeHashTable)
        {
            if (GetCounterArrayBuffer(
                gpuPerfCounterRunningTimeHandle,
                PDH_FMT_DOUBLE,
                &bufferCount,
                &buffer
                ))
            {
                PhAcquireQueuedLockExclusive(&EtGpuRunningTimeHashTableLock);

                // Reset hashtable once in a while.
                {
                    static ULONG64 lastTickCount = 0;
                    ULONG64 tickCount = NtGetTickCount64();

                    if (lastTickCount == 0)
                        lastTickCount = tickCount;

                    if (tickCount - lastTickCount >= 30 * 1000)
                    {
                        PhDereferenceObject(EtGpuRunningTimeHashTable);
                        EtGpuCreateRunningTimeHashTable();;
                        lastTickCount = tickCount;
                    }
                    else
                    {
                        PhClearHashtable(EtGpuRunningTimeHashTable);
                    }
                }

                for (ULONG i = 0; i < bufferCount; i++)
                {
                    PPDH_FMT_COUNTERVALUE_ITEM entry = PTR_ADD_OFFSET(buffer, sizeof(PDH_FMT_COUNTERVALUE_ITEM) * i);

                    if (entry->FmtValue.CStatus)
                        continue;
                    if (entry->FmtValue.doubleValue == 0.0)
                        continue;

                    ParseGpuEngineUtilizationCounter(entry->szName, entry->FmtValue.doubleValue);
                }

                PhReleaseQueuedLockExclusive(&EtGpuRunningTimeHashTableLock);

                PhFree(buffer);
            }
        }

        if (EtGpuDedicatedHashTable)
        {
            if (GetCounterArrayBuffer(
                gpuPerfCounterDedicatedUsageHandle,
                PDH_FMT_LARGE,
                &bufferCount,
                &buffer
                ))
            {
                PhAcquireQueuedLockExclusive(&EtGpuDedicatedHashTableLock);

                // Reset hashtable once in a while.
                {
                    static ULONG64 lastTickCount = 0;
                    ULONG64 tickCount = NtGetTickCount64();

                    if (lastTickCount == 0)
                        lastTickCount = tickCount;

                    if (tickCount - lastTickCount >= 30 * CLOCKS_PER_SEC)
                    {
                        PhDereferenceObject(EtGpuDedicatedHashTable);
                        EtGpuCreateDedicatedHashTable();
                        lastTickCount = tickCount;
                    }
                    else
                    {
                        PhClearHashtable(EtGpuDedicatedHashTable);
                    }
                }

                for (ULONG i = 0; i < bufferCount; i++)
                {
                    PPDH_FMT_COUNTERVALUE_ITEM entry = PTR_ADD_OFFSET(buffer, sizeof(PDH_FMT_COUNTERVALUE_ITEM) * i);

                    if (entry->FmtValue.CStatus)
                        continue;
                    if (entry->FmtValue.largeValue == 0)
                        continue;

                    ParseGpuProcessMemoryDedicatedUsageCounter(entry->szName, entry->FmtValue.largeValue);
                }

                PhReleaseQueuedLockExclusive(&EtGpuDedicatedHashTableLock);

                PhFree(buffer);
            }
        }

        if (EtGpuSharedHashTable)
        {
            if (GetCounterArrayBuffer(
                gpuPerfCounterSharedUsageHandle,
                PDH_FMT_LARGE,
                &bufferCount,
                &buffer
                ))
            {
                PhAcquireQueuedLockExclusive(&EtGpuSharedHashTableLock);

                // Reset hashtable once in a while.
                {
                    static ULONG64 lastTickCount = 0;
                    ULONG64 tickCount = NtGetTickCount64();

                    if (lastTickCount == 0)
                        lastTickCount = tickCount;

                    if (tickCount - lastTickCount >= 30 * CLOCKS_PER_SEC)
                    {
                        PhDereferenceObject(EtGpuSharedHashTable);
                        EtGpuCreateSharedHashTable();
                        lastTickCount = tickCount;
                    }
                    else
                    {
                        PhClearHashtable(EtGpuSharedHashTable);
                    }
                }

                for (ULONG i = 0; i < bufferCount; i++)
                {
                    PPDH_FMT_COUNTERVALUE_ITEM entry = PTR_ADD_OFFSET(buffer, sizeof(PDH_FMT_COUNTERVALUE_ITEM) * i);

                    if (entry->FmtValue.CStatus)
                        continue;
                    if (entry->FmtValue.largeValue == 0)
                        continue;

                    ParseGpuProcessMemorySharedUsageCounter(entry->szName, entry->FmtValue.largeValue);
                }

                PhReleaseQueuedLockExclusive(&EtGpuSharedHashTableLock);

                PhFree(buffer);
            }
        }

        if (EtGpuCommitHashTable)
        {
            if (GetCounterArrayBuffer(
                gpuPerfCounterCommittedUsageHandle,
                PDH_FMT_LARGE,
                &bufferCount,
                &buffer
                ))
            {
                PhAcquireQueuedLockExclusive(&EtGpuCommitHashTableLock);

                // Reset hashtable once in a while.
                {
                    static ULONG64 lastTickCount = 0;
                    ULONG64 tickCount = NtGetTickCount64();

                    if (lastTickCount == 0)
                        lastTickCount = tickCount;

                    if (tickCount - lastTickCount >= 30 * CLOCKS_PER_SEC)
                    {
                        PhDereferenceObject(EtGpuCommitHashTable);
                        EtGpuCreateCommitHashTable();
                        lastTickCount = tickCount;
                    }
                    else
                    {
                        PhClearHashtable(EtGpuCommitHashTable);
                    }
                }

                for (ULONG i = 0; i < bufferCount; i++)
                {
                    PPDH_FMT_COUNTERVALUE_ITEM entry = PTR_ADD_OFFSET(buffer, sizeof(PDH_FMT_COUNTERVALUE_ITEM)* i);

                    if (entry->FmtValue.CStatus)
                        continue;
                    if (entry->FmtValue.largeValue == 0)
                        continue;

                    ParseGpuProcessMemoryCommitUsageCounter(entry->szName, entry->FmtValue.largeValue);
                }

                PhReleaseQueuedLockExclusive(&EtGpuCommitHashTableLock);

                PhFree(buffer);
            }
        }

        if (EtGpuAdapterDedicatedHashTable)
        {
            if (GetCounterArrayBuffer(
                gpuPerfCounterAdapterDedicatedUsageHandle,
                PDH_FMT_LARGE,
                &bufferCount,
                &buffer
                ))
            {
                PhAcquireQueuedLockExclusive(&EtGpuAdapterDedicatedHashTableLock);

                // Reset hashtable once in a while.
                {
                    static ULONG64 lastTickCount = 0;
                    ULONG64 tickCount = NtGetTickCount64();

                    if (lastTickCount == 0)
                        lastTickCount = tickCount;

                    if (tickCount - lastTickCount >= 30 * CLOCKS_PER_SEC)
                    {
                        PhDereferenceObject(EtGpuAdapterDedicatedHashTable);
                        EtGpuCreateAdapterDedicatedHashTable();
                        lastTickCount = tickCount;
                    }
                    else
                    {
                        PhClearHashtable(EtGpuAdapterDedicatedHashTable);
                    }
                }

                for (ULONG i = 0; i < bufferCount; i++)
                {
                    PPDH_FMT_COUNTERVALUE_ITEM entry = PTR_ADD_OFFSET(buffer, sizeof(PDH_FMT_COUNTERVALUE_ITEM) * i);

                    if (entry->FmtValue.CStatus)
                        continue;
                    if (entry->FmtValue.largeValue == 0)
                        continue;

                    ParseGpuAdapterDedicatedUsageCounter(entry->szName, entry->FmtValue.largeValue);
                }

                PhReleaseQueuedLockExclusive(&EtGpuAdapterDedicatedHashTableLock);

                PhFree(buffer);
            }
        }
    }

CleanupExit:

    if (gpuPerfCounterQueryHandle)
        PdhCloseQuery(gpuPerfCounterQueryHandle);
    if (gpuCounterQueryEvent)
        NtClose(gpuCounterQueryEvent);

    return STATUS_SUCCESS;
}
