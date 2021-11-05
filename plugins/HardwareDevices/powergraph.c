/*
 * Process Hacker Plugins -
 *   Hardware Devices Plugin
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

#include "devices.h"

INT_PTR CALLBACK RaplDevicePanelDialogProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    PDV_RAPL_SYSINFO_CONTEXT context = NULL;

    if (uMsg == WM_INITDIALOG)
    {
        context = (PDV_RAPL_SYSINFO_CONTEXT)lParam;

        PhSetWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT, context);
    }
    else
    {
        context = PhGetWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT);

        if (uMsg == WM_NCDESTROY)
        {
            PhRemoveWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT);
        }
    }

    if (context == NULL)
        return FALSE;

    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            context->RaplDeviceProcessorUsageLabel = GetDlgItem(hwndDlg, IDC_PACKAGE_L);
            context->RaplDeviceCoreUsageLabel = GetDlgItem(hwndDlg, IDC_CORE_L);
            context->RaplDeviceDimmUsageLabel = GetDlgItem(hwndDlg, IDC_DIMM_L);
            context->RaplDeviceGpuLimitLabel = GetDlgItem(hwndDlg, IDC_GPUDISCRETE_L);
            context->RaplDeviceComponentUsageLabel = GetDlgItem(hwndDlg, IDC_CPUCOMP_L);
            context->RaplDeviceTotalUsageLabel = GetDlgItem(hwndDlg, IDC_TOTALPOWER_L);
        }
        break;
    }

    return FALSE;
}

VOID RaplDeviceInitializeGraphStates(
    _Inout_ PDV_RAPL_SYSINFO_CONTEXT Context
    )
{
    PhInitializeGraphState(&Context->ProcessorGraphState);
    PhInitializeGraphState(&Context->CoreGraphState);
    PhInitializeGraphState(&Context->DimmGraphState);
    PhInitializeGraphState(&Context->TotalGraphState);
}

VOID RaplDeviceCreateGraphs(
    _Inout_ PDV_RAPL_SYSINFO_CONTEXT Context
    )
{
    Context->ProcessorGraphHandle = CreateWindow(
        PH_GRAPH_CLASSNAME,
        NULL,
        WS_VISIBLE | WS_CHILD | WS_BORDER,
        0,
        0,
        3,
        3,
        Context->WindowHandle,
        NULL,
        NULL,
        NULL
        );
    Graph_SetTooltip(Context->ProcessorGraphHandle, TRUE);

    Context->CoreGraphHandle = CreateWindow(
        PH_GRAPH_CLASSNAME,
        NULL,
        WS_VISIBLE | WS_CHILD | WS_BORDER,
        0,
        0,
        3,
        3,
        Context->WindowHandle,
        NULL,
        NULL,
        NULL
        );
    Graph_SetTooltip(Context->CoreGraphHandle, TRUE);

    Context->DimmGraphHandle = CreateWindow(
        PH_GRAPH_CLASSNAME,
        NULL,
        WS_VISIBLE | WS_CHILD | WS_BORDER,
        0,
        0,
        3,
        3,
        Context->WindowHandle,
        NULL,
        NULL,
        NULL
        );
    Graph_SetTooltip(Context->DimmGraphHandle, TRUE);

    Context->TotalGraphHandle = CreateWindow(
        PH_GRAPH_CLASSNAME,
        NULL,
        WS_VISIBLE | WS_CHILD | WS_BORDER,
        0,
        0,
        3,
        3,
        Context->WindowHandle,
        NULL,
        NULL,
        NULL
        );
    Graph_SetTooltip(Context->TotalGraphHandle, TRUE);
}

VOID RaplDeviceUpdateGraphs(
    _Inout_ PDV_RAPL_SYSINFO_CONTEXT Context
    )
{
    Context->ProcessorGraphState.Valid = FALSE;
    Context->ProcessorGraphState.TooltipIndex = ULONG_MAX;
    Graph_MoveGrid(Context->ProcessorGraphHandle, 1);
    Graph_Draw(Context->ProcessorGraphHandle);
    Graph_UpdateTooltip(Context->ProcessorGraphHandle);
    InvalidateRect(Context->ProcessorGraphHandle, NULL, FALSE);

    Context->CoreGraphState.Valid = FALSE;
    Context->CoreGraphState.TooltipIndex = ULONG_MAX;
    Graph_MoveGrid(Context->CoreGraphHandle, 1);
    Graph_Draw(Context->CoreGraphHandle);
    Graph_UpdateTooltip(Context->CoreGraphHandle);
    InvalidateRect(Context->CoreGraphHandle, NULL, FALSE);

    Context->DimmGraphState.Valid = FALSE;
    Context->DimmGraphState.TooltipIndex = ULONG_MAX;
    Graph_MoveGrid(Context->DimmGraphHandle, 1);
    Graph_Draw(Context->DimmGraphHandle);
    Graph_UpdateTooltip(Context->DimmGraphHandle);
    InvalidateRect(Context->DimmGraphHandle, NULL, FALSE);

    Context->TotalGraphState.Valid = FALSE;
    Context->TotalGraphState.TooltipIndex = ULONG_MAX;
    Graph_MoveGrid(Context->TotalGraphHandle, 1);
    Graph_Draw(Context->TotalGraphHandle);
    Graph_UpdateTooltip(Context->TotalGraphHandle);
    InvalidateRect(Context->TotalGraphHandle, NULL, FALSE);
}

VOID RaplDeviceUpdatePanel(
    _Inout_ PDV_RAPL_SYSINFO_CONTEXT Context
    )
{
    PH_FORMAT format[2];
    WCHAR formatBuffer[512];

    PhInitFormatF(&format[0], Context->DeviceEntry->CurrentProcessorPower, 2);
    PhInitFormatS(&format[1], L" W");

    if (PhFormatToBuffer(format, RTL_NUMBER_OF(format), formatBuffer, sizeof(formatBuffer), NULL))
        PhSetWindowText(Context->RaplDeviceProcessorUsageLabel, formatBuffer);
    else
        PhSetWindowText(Context->RaplDeviceProcessorUsageLabel, L"N/A");

    PhInitFormatF(&format[0], Context->DeviceEntry->CurrentCorePower, 2);
    PhInitFormatS(&format[1], L" W");

    if (PhFormatToBuffer(format, RTL_NUMBER_OF(format), formatBuffer, sizeof(formatBuffer), NULL))
        PhSetWindowText(Context->RaplDeviceCoreUsageLabel, formatBuffer);
    else
        PhSetWindowText(Context->RaplDeviceCoreUsageLabel, L"N/A");

    PhInitFormatF(&format[0], Context->DeviceEntry->CurrentDramPower, 2);
    PhInitFormatS(&format[1], L" W");

    if (PhFormatToBuffer(format, RTL_NUMBER_OF(format), formatBuffer, sizeof(formatBuffer), NULL))
        PhSetWindowText(Context->RaplDeviceDimmUsageLabel, formatBuffer);
    else
        PhSetWindowText(Context->RaplDeviceDimmUsageLabel, L"N/A");

    PhInitFormatF(&format[0], Context->DeviceEntry->CurrentDiscreteGpuPower, 2);
    PhInitFormatS(&format[1], L" W");

    if (PhFormatToBuffer(format, RTL_NUMBER_OF(format), formatBuffer, sizeof(formatBuffer), NULL))
        PhSetWindowText(Context->RaplDeviceGpuLimitLabel, formatBuffer);
    else
        PhSetWindowText(Context->RaplDeviceGpuLimitLabel, L"N/A");

    PhInitFormatF(&format[0], Context->DeviceEntry->CurrentComponentPower, 2);
    PhInitFormatS(&format[1], L" W");

    if (PhFormatToBuffer(format, RTL_NUMBER_OF(format), formatBuffer, sizeof(formatBuffer), NULL))
        PhSetWindowText(Context->RaplDeviceComponentUsageLabel, formatBuffer);
    else
        PhSetWindowText(Context->RaplDeviceComponentUsageLabel, L"N/A");

    PhInitFormatF(&format[0], Context->DeviceEntry->CurrentTotalPower, 2);
    PhInitFormatS(&format[1], L" W");

    if (PhFormatToBuffer(format, RTL_NUMBER_OF(format), formatBuffer, sizeof(formatBuffer), NULL))
        PhSetWindowText(Context->RaplDeviceTotalUsageLabel, formatBuffer);
    else
        PhSetWindowText(Context->RaplDeviceTotalUsageLabel, L"N/A");
}

VOID RaplDeviceLayoutGraphs(
    _Inout_ PDV_RAPL_SYSINFO_CONTEXT Context
    )
{
    RECT clientRect;
    RECT labelRect;
    ULONG graphWidth;
    ULONG graphHeight;
    HDWP deferHandle;
    ULONG y;

    GetClientRect(Context->WindowHandle, &clientRect);
    GetClientRect(GetDlgItem(Context->WindowHandle, IDC_PACKAGE_L), &labelRect);
    graphWidth = clientRect.right - Context->GraphMargin.left - Context->GraphMargin.right;
    graphHeight = (clientRect.bottom - Context->GraphMargin.top - Context->GraphMargin.bottom - labelRect.bottom * 4 - RAPL_GRAPH_PADDING * 5) / 4;

    deferHandle = BeginDeferWindowPos(8);
    y = Context->GraphMargin.top;

    deferHandle = DeferWindowPos(
        deferHandle,
        GetDlgItem(Context->WindowHandle, IDC_PACKAGE_L),
        NULL,
        Context->GraphMargin.left,
        y,
        0,
        0,
        SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER
        );
    y += labelRect.bottom + RAPL_GRAPH_PADDING;

    deferHandle = DeferWindowPos(
        deferHandle,
        Context->ProcessorGraphHandle,
        NULL,
        Context->GraphMargin.left,
        y,
        graphWidth,
        graphHeight,
        SWP_NOACTIVATE | SWP_NOZORDER
        );
    y += graphHeight + RAPL_GRAPH_PADDING;

    deferHandle = DeferWindowPos(
        deferHandle,
        GetDlgItem(Context->WindowHandle, IDC_CORE_L),
        NULL,
        Context->GraphMargin.left,
        y,
        0,
        0,
        SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER
        );
    y += labelRect.bottom + RAPL_GRAPH_PADDING;

    deferHandle = DeferWindowPos(
        deferHandle,
        Context->CoreGraphHandle,
        NULL,
        Context->GraphMargin.left,
        y,
        graphWidth,
        graphHeight,
        SWP_NOACTIVATE | SWP_NOZORDER
        );
    y += graphHeight + RAPL_GRAPH_PADDING;

    deferHandle = DeferWindowPos(
        deferHandle,
        GetDlgItem(Context->WindowHandle, IDC_DIMM_L),
        NULL,
        Context->GraphMargin.left,
        y,
        0,
        0,
        SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER
        );
    y += labelRect.bottom + RAPL_GRAPH_PADDING;

    deferHandle = DeferWindowPos(
        deferHandle,
        Context->DimmGraphHandle,
        NULL,
        Context->GraphMargin.left,
        y,
        graphWidth,
        graphHeight,
        SWP_NOACTIVATE | SWP_NOZORDER
        );
    y += graphHeight + RAPL_GRAPH_PADDING;

    deferHandle = DeferWindowPos(
        deferHandle,
        GetDlgItem(Context->WindowHandle, IDC_TOTAL_L),
        NULL,
        Context->GraphMargin.left,
        y,
        0,
        0,
        SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER
        );
    y += labelRect.bottom + RAPL_GRAPH_PADDING;

    deferHandle = DeferWindowPos(
        deferHandle,
        Context->TotalGraphHandle,
        NULL,
        Context->GraphMargin.left,
        y,
        graphWidth,
        clientRect.bottom - Context->GraphMargin.bottom - y,
        SWP_NOACTIVATE | SWP_NOZORDER
        );
    y += graphHeight + RAPL_GRAPH_PADDING;

    EndDeferWindowPos(deferHandle);
}

PPH_STRING RaplGraphSingleLabelYFunction(
    _In_ PPH_GRAPH_DRAW_INFO DrawInfo,
    _In_ ULONG DataIndex,
    _In_ FLOAT Value,
    _In_ FLOAT Parameter
    )
{
    DOUBLE value;

    value = (DOUBLE)((DOUBLE)Value * Parameter);

    if (value != 0)
    {
        PH_FORMAT format[2];

        PhInitFormatF(&format[0], value, 2);
        PhInitFormatS(&format[1], L" W");

        return PhFormat(format, RTL_NUMBER_OF(format), 0);
    }
    else
    {
        return PhReferenceEmptyString();
    }
}

VOID RaplDeviceNotifyProcessorGraph(
    _Inout_ PDV_RAPL_SYSINFO_CONTEXT Context,
    _In_ NMHDR *Header
    )
{
    switch (Header->code)
    {
    case GCN_GETDRAWINFO:
        {
            PPH_GRAPH_GETDRAWINFO getDrawInfo = (PPH_GRAPH_GETDRAWINFO)Header;
            PPH_GRAPH_DRAW_INFO drawInfo = getDrawInfo->DrawInfo;

            drawInfo->Flags = PH_GRAPH_USE_GRID_X | PH_GRAPH_USE_GRID_Y | PH_GRAPH_LABEL_MAX_Y;
            Context->SysinfoSection->Parameters->ColorSetupFunction(drawInfo, PhGetIntegerSetting(L"ColorCpuKernel"), 0);
            PhGraphStateGetDrawInfo(&Context->ProcessorGraphState, getDrawInfo, Context->DeviceEntry->PackageBuffer.Count);

            if (!Context->ProcessorGraphState.Valid)
            {
                FLOAT max = 0;

                PhCopyCircularBuffer_FLOAT(&Context->DeviceEntry->PackageBuffer, Context->ProcessorGraphState.Data1, drawInfo->LineDataCount);

                for (ULONG i = 0; i < drawInfo->LineDataCount; i++)
                {
                    FLOAT data = Context->ProcessorGraphState.Data1[i];

                    if (max < data)
                        max = data;
                }

                if (max != 0)
                {
                    PhDivideSinglesBySingle(Context->ProcessorGraphState.Data1, max, drawInfo->LineDataCount);
                }

                drawInfo->LabelYFunction = RaplGraphSingleLabelYFunction;
                drawInfo->LabelYFunctionParameter = max;

                Context->ProcessorGraphState.Valid = TRUE;
            }
        }
        break;
    case GCN_GETTOOLTIPTEXT:
        {
            PPH_GRAPH_GETTOOLTIPTEXT getTooltipText = (PPH_GRAPH_GETTOOLTIPTEXT)Header;

            if (getTooltipText->Index < getTooltipText->TotalCount)
            {
                if (Context->ProcessorGraphState.TooltipIndex != getTooltipText->Index)
                {
                    FLOAT value;

                    value = PhGetItemCircularBuffer_FLOAT(&Context->DeviceEntry->PackageBuffer, getTooltipText->Index);

                    PhMoveReference(&Context->ProcessorGraphState.TooltipText, PhFormatString(
                        L"%.2f W\n%s",
                        value,
                        ((PPH_STRING)PH_AUTO(PhGetStatisticsTimeString(NULL, getTooltipText->Index)))->Buffer
                        ));
                }

                getTooltipText->Text = PhGetStringRef(Context->ProcessorGraphState.TooltipText);
            }
        }
        break;
    }
}

VOID RaplDeviceNotifyPackageGraph(
    _Inout_ PDV_RAPL_SYSINFO_CONTEXT Context,
    _In_ NMHDR *Header
    )
{
    switch (Header->code)
    {
    case GCN_GETDRAWINFO:
        {
            PPH_GRAPH_GETDRAWINFO getDrawInfo = (PPH_GRAPH_GETDRAWINFO)Header;
            PPH_GRAPH_DRAW_INFO drawInfo = getDrawInfo->DrawInfo;

            drawInfo->Flags = PH_GRAPH_USE_GRID_X | PH_GRAPH_USE_GRID_Y | PH_GRAPH_LABEL_MAX_Y;
            Context->SysinfoSection->Parameters->ColorSetupFunction(drawInfo, PhGetIntegerSetting(L"ColorPhysical"), 0);
            PhGraphStateGetDrawInfo(&Context->CoreGraphState, getDrawInfo, Context->DeviceEntry->CoreBuffer.Count);

            if (!Context->CoreGraphState.Valid)
            {
                FLOAT max = 0;

                PhCopyCircularBuffer_FLOAT(&Context->DeviceEntry->CoreBuffer, Context->CoreGraphState.Data1, drawInfo->LineDataCount);

                for (ULONG i = 0; i < drawInfo->LineDataCount; i++)
                {
                    FLOAT data = Context->CoreGraphState.Data1[i];

                    if (max < data)
                        max = data;
                }

                if (max != 0)
                {
                    PhDivideSinglesBySingle(Context->CoreGraphState.Data1, max, drawInfo->LineDataCount);
                }

                drawInfo->LabelYFunction = RaplGraphSingleLabelYFunction;
                drawInfo->LabelYFunctionParameter = max;
            }
        }
        break;
    case GCN_GETTOOLTIPTEXT:
        {
            PPH_GRAPH_GETTOOLTIPTEXT getTooltipText = (PPH_GRAPH_GETTOOLTIPTEXT)Header;

            if (getTooltipText->Index < getTooltipText->TotalCount)
            {
                if (Context->CoreGraphState.TooltipIndex != getTooltipText->Index)
                {
                    FLOAT value;
                    PH_FORMAT format[3];

                    value = PhGetItemCircularBuffer_FLOAT(&Context->DeviceEntry->CoreBuffer, getTooltipText->Index);

                    // %.2f W\%s
                    PhInitFormatF(&format[0], value, 2);
                    PhInitFormatS(&format[1], L" W\n");
                    PhInitFormatSR(&format[2], PH_AUTO_T(PH_STRING, PhGetStatisticsTimeString(NULL, getTooltipText->Index))->sr);

                    PhMoveReference(&Context->CoreGraphState.TooltipText, PhFormat(format, RTL_NUMBER_OF(format), 0));
                }

                getTooltipText->Text = PhGetStringRef(Context->CoreGraphState.TooltipText);
            }
        }
        break;
    }
}

VOID RaplDeviceNotifyDimmGraph(
    _Inout_ PDV_RAPL_SYSINFO_CONTEXT Context,
    _In_ NMHDR *Header
    )
{
    switch (Header->code)
    {
    case GCN_GETDRAWINFO:
        {
            PPH_GRAPH_GETDRAWINFO getDrawInfo = (PPH_GRAPH_GETDRAWINFO)Header;
            PPH_GRAPH_DRAW_INFO drawInfo = getDrawInfo->DrawInfo;

            drawInfo->Flags = PH_GRAPH_USE_GRID_X | PH_GRAPH_USE_GRID_Y | PH_GRAPH_LABEL_MAX_Y;
            Context->SysinfoSection->Parameters->ColorSetupFunction(drawInfo, PhGetIntegerSetting(L"ColorIoWrite"), 0);
            PhGraphStateGetDrawInfo(&Context->DimmGraphState, getDrawInfo, Context->DeviceEntry->DimmBuffer.Count);

            if (!Context->DimmGraphState.Valid)
            {
                FLOAT max = 0;

                PhCopyCircularBuffer_FLOAT(&Context->DeviceEntry->DimmBuffer, Context->DimmGraphState.Data1, drawInfo->LineDataCount);

                for (ULONG i = 0; i < drawInfo->LineDataCount; i++)
                {
                    FLOAT data = Context->DimmGraphState.Data1[i];

                    if (max < data)
                        max = data;
                }

                if (max != 0)
                {
                    PhDivideSinglesBySingle(Context->DimmGraphState.Data1, max, drawInfo->LineDataCount);
                }

                drawInfo->LabelYFunction = RaplGraphSingleLabelYFunction;
                drawInfo->LabelYFunctionParameter = max;

                Context->DimmGraphState.Valid = TRUE;
            }
        }
        break;
    case GCN_GETTOOLTIPTEXT:
        {
            PPH_GRAPH_GETTOOLTIPTEXT getTooltipText = (PPH_GRAPH_GETTOOLTIPTEXT)Header;

            if (getTooltipText->Index < getTooltipText->TotalCount)
            {
                if (Context->DimmGraphState.TooltipIndex != getTooltipText->Index)
                {
                    FLOAT value;
                    PH_FORMAT format[3];

                    value = PhGetItemCircularBuffer_FLOAT(&Context->DeviceEntry->DimmBuffer, getTooltipText->Index);

                    // %.2f W\%s
                    PhInitFormatF(&format[0], value, 2);
                    PhInitFormatS(&format[1], L" W\n");
                    PhInitFormatSR(&format[2], PH_AUTO_T(PH_STRING, PhGetStatisticsTimeString(NULL, getTooltipText->Index))->sr);

                    PhMoveReference(&Context->DimmGraphState.TooltipText, PhFormat(format, RTL_NUMBER_OF(format), 0));
                }

                getTooltipText->Text = PhGetStringRef(Context->DimmGraphState.TooltipText);
            }
        }
        break;
    }
}

VOID RaplDeviceNotifyTotalGraph(
    _Inout_ PDV_RAPL_SYSINFO_CONTEXT Context,
    _In_ NMHDR* Header
    )
{
    switch (Header->code)
    {
    case GCN_GETDRAWINFO:
        {
            PPH_GRAPH_GETDRAWINFO getDrawInfo = (PPH_GRAPH_GETDRAWINFO)Header;
            PPH_GRAPH_DRAW_INFO drawInfo = getDrawInfo->DrawInfo;

            drawInfo->Flags = PH_GRAPH_USE_GRID_X | PH_GRAPH_USE_GRID_Y | PH_GRAPH_LABEL_MAX_Y;
            Context->SysinfoSection->Parameters->ColorSetupFunction(drawInfo, PhGetIntegerSetting(L"ColorPrivate"), 0);
            PhGraphStateGetDrawInfo(&Context->TotalGraphState, getDrawInfo, Context->DeviceEntry->TotalBuffer.Count);

            if (!Context->TotalGraphState.Valid)
            {
                FLOAT max = 0;

                PhCopyCircularBuffer_FLOAT(&Context->DeviceEntry->TotalBuffer, Context->TotalGraphState.Data1, drawInfo->LineDataCount);

                for (ULONG i = 0; i < drawInfo->LineDataCount; i++)
                {
                    FLOAT data = Context->TotalGraphState.Data1[i];

                    if (max < data)
                        max = data;
                }

                if (max != 0)
                {
                    PhDivideSinglesBySingle(Context->TotalGraphState.Data1, max, drawInfo->LineDataCount);
                }

                drawInfo->LabelYFunction = RaplGraphSingleLabelYFunction;
                drawInfo->LabelYFunctionParameter = max;

                Context->TotalGraphState.Valid = TRUE;
            }
        }
        break;
    case GCN_GETTOOLTIPTEXT:
        {
            PPH_GRAPH_GETTOOLTIPTEXT getTooltipText = (PPH_GRAPH_GETTOOLTIPTEXT)Header;

            if (getTooltipText->Index < getTooltipText->TotalCount)
            {
                if (Context->TotalGraphState.TooltipIndex != getTooltipText->Index)
                {
                    FLOAT value;
                    PH_FORMAT format[3];

                    value = PhGetItemCircularBuffer_FLOAT(&Context->DeviceEntry->TotalBuffer, getTooltipText->Index);

                    // %.2f W\%s
                    PhInitFormatF(&format[0], value, 2);
                    PhInitFormatS(&format[1], L" W\n");
                    PhInitFormatSR(&format[2], PH_AUTO_T(PH_STRING, PhGetStatisticsTimeString(NULL, getTooltipText->Index))->sr);

                    PhMoveReference(&Context->TotalGraphState.TooltipText, PhFormat(format, RTL_NUMBER_OF(format), 0));
                }

                getTooltipText->Text = PhGetStringRef(Context->TotalGraphState.TooltipText);
            }
        }
        break;
    }
}

VOID RaplDeviceTickDialog(
    _Inout_ PDV_RAPL_SYSINFO_CONTEXT Context
    )
{
    RaplDeviceUpdateGraphs(Context);
    RaplDeviceUpdatePanel(Context);
}

INT_PTR CALLBACK RaplDeviceDialogProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    PDV_RAPL_SYSINFO_CONTEXT context = NULL;

    if (uMsg == WM_INITDIALOG)
    {
        context = (PDV_RAPL_SYSINFO_CONTEXT)lParam;

        PhSetWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT, context);
    }
    else
    {
        context = PhGetWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT);

        if (uMsg == WM_DESTROY)
        {
            PhRemoveWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT);
        }
    }

    if (context == NULL)
        return FALSE;

    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            PPH_LAYOUT_ITEM graphItem;
            PPH_LAYOUT_ITEM panelItem;

            context->WindowHandle = hwndDlg;
            PhInitializeLayoutManager(&context->LayoutManager, hwndDlg);
            PhAddLayoutItem(&context->LayoutManager, GetDlgItem(hwndDlg, IDC_DEVICENAME), NULL, PH_ANCHOR_LEFT | PH_ANCHOR_TOP | PH_ANCHOR_RIGHT | PH_LAYOUT_FORCE_INVALIDATE);
            graphItem = PhAddLayoutItem(&context->LayoutManager, GetDlgItem(hwndDlg, IDC_GRAPH_LAYOUT), NULL, PH_ANCHOR_ALL);
            panelItem = PhAddLayoutItem(&context->LayoutManager, GetDlgItem(hwndDlg, IDC_PANEL_LAYOUT), NULL, PH_ANCHOR_LEFT | PH_ANCHOR_RIGHT | PH_ANCHOR_BOTTOM);
            context->GraphMargin = graphItem->Margin;

            SetWindowFont(GetDlgItem(hwndDlg, IDC_TITLE), context->SysinfoSection->Parameters->LargeFont, FALSE);
            SetWindowFont(GetDlgItem(hwndDlg, IDC_DEVICENAME), context->SysinfoSection->Parameters->MediumFont, FALSE);
            PhSetDialogItemText(hwndDlg, IDC_DEVICENAME, PhGetStringOrDefault(context->DeviceEntry->DeviceName, L"Unknown"));

            context->RaplDevicePanel = PhCreateDialog(PluginInstance->DllBase, MAKEINTRESOURCE(IDD_RAPLDEVICE_PANEL), hwndDlg, RaplDevicePanelDialogProc, context);
            ShowWindow(context->RaplDevicePanel, SW_SHOW);
            PhAddLayoutItemEx(&context->LayoutManager, context->RaplDevicePanel, NULL, PH_ANCHOR_LEFT | PH_ANCHOR_RIGHT | PH_ANCHOR_BOTTOM, panelItem->Margin);

            RaplDeviceInitializeGraphStates(context);
            RaplDeviceCreateGraphs(context);
            RaplDeviceUpdateGraphs(context);
            RaplDeviceUpdatePanel(context);
        }
        break;
    case WM_DESTROY:
        {
            PhDeleteLayoutManager(&context->LayoutManager);

            PhDeleteGraphState(&context->ProcessorGraphState);
            PhDeleteGraphState(&context->CoreGraphState);
            PhDeleteGraphState(&context->DimmGraphState);
            PhDeleteGraphState(&context->TotalGraphState);

            if (context->ProcessorGraphHandle)
                DestroyWindow(context->ProcessorGraphHandle);
            if (context->CoreGraphHandle)
                DestroyWindow(context->CoreGraphHandle);
            if (context->DimmGraphHandle)
                DestroyWindow(context->DimmGraphHandle);
            if (context->TotalGraphHandle)
                DestroyWindow(context->TotalGraphHandle);
            if (context->RaplDevicePanel)
                DestroyWindow(context->RaplDevicePanel);
        }
        break;
    case WM_SIZE:
        {
            PhLayoutManagerLayout(&context->LayoutManager);
            RaplDeviceLayoutGraphs(context);
        }
        break;
    case WM_NOTIFY:
        {
            NMHDR* header = (NMHDR*)lParam;

            if (header->hwndFrom == context->ProcessorGraphHandle)
            {
                RaplDeviceNotifyProcessorGraph(context, header);
            }
            else if (header->hwndFrom == context->CoreGraphHandle)
            {
                RaplDeviceNotifyPackageGraph(context, header);
            }
            else if (header->hwndFrom == context->DimmGraphHandle)
            {
                RaplDeviceNotifyDimmGraph(context, header);
            }
            else if (header->hwndFrom == context->TotalGraphHandle)
            {
                RaplDeviceNotifyTotalGraph(context, header);
            }
        }
        break;
    }

    return FALSE;
}

BOOLEAN RaplDeviceSectionCallback(
    _In_ PPH_SYSINFO_SECTION Section,
    _In_ PH_SYSINFO_SECTION_MESSAGE Message,
    _In_opt_ PVOID Parameter1,
    _In_opt_ PVOID Parameter2
    )
{
    PDV_RAPL_SYSINFO_CONTEXT context = (PDV_RAPL_SYSINFO_CONTEXT)Section->Context;

    switch (Message)
    {
    case SysInfoCreate:
        {
            if (PhIsNullOrEmptyString(context->DeviceEntry->DeviceName))
            {
                PPH_STRING deviceName;

                if (QueryRaplDeviceInterfaceDescription(PhGetString(context->DeviceEntry->Id.DevicePath), &deviceName))
                {
                    PhMoveReference(&context->DeviceEntry->DeviceName, deviceName);
                }
            }
        }
        return TRUE;
    case SysInfoDestroy:
        {
            PhDereferenceObject(context->DeviceEntry);
            PhFree(context);
        }
        return TRUE;
    case SysInfoTick:
        {
            if (context->WindowHandle)
            {
                RaplDeviceTickDialog(context);
            }
        }
        return TRUE;
    case SysInfoCreateDialog:
        {
            PPH_SYSINFO_CREATE_DIALOG createDialog = (PPH_SYSINFO_CREATE_DIALOG)Parameter1;

            if (!createDialog)
                break;

            createDialog->Instance = PluginInstance->DllBase;
            createDialog->Template = MAKEINTRESOURCE(IDD_RAPLDEVICE_DIALOG);
            createDialog->DialogProc = RaplDeviceDialogProc;
            createDialog->Parameter = context;
        }
        return TRUE;
    case SysInfoGraphGetDrawInfo:
        {
            PPH_GRAPH_DRAW_INFO drawInfo = (PPH_GRAPH_DRAW_INFO)Parameter1;

            if (!drawInfo)
                break;

            drawInfo->Flags = PH_GRAPH_USE_GRID_X | PH_GRAPH_USE_GRID_Y | PH_GRAPH_LABEL_MAX_Y;
            Section->Parameters->ColorSetupFunction(drawInfo, PhGetIntegerSetting(L"ColorPrivate"), 0);
            PhGetDrawInfoGraphBuffers(&Section->GraphState.Buffers, drawInfo, context->DeviceEntry->TotalBuffer.Count);

            if (!Section->GraphState.Valid)
            {
                FLOAT max = 0;

                PhCopyCircularBuffer_FLOAT(&context->DeviceEntry->TotalBuffer, Section->GraphState.Data1, drawInfo->LineDataCount);

                for (ULONG i = 0; i < drawInfo->LineDataCount; i++)
                {
                    FLOAT data = Section->GraphState.Data1[i];

                    if (max < data)
                        max = data;
                }

                if (max != 0)
                {
                    PhDivideSinglesBySingle(Section->GraphState.Data1, max, drawInfo->LineDataCount);
                }

                drawInfo->LabelYFunction = RaplGraphSingleLabelYFunction;
                drawInfo->LabelYFunctionParameter = max;

                Section->GraphState.Valid = TRUE;
            }
        }
        return TRUE;
    case SysInfoGraphGetTooltipText:
        {
            PPH_SYSINFO_GRAPH_GET_TOOLTIP_TEXT getTooltipText = (PPH_SYSINFO_GRAPH_GET_TOOLTIP_TEXT)Parameter1;
            FLOAT value;
            PH_FORMAT format[3];

            if (!getTooltipText)
                break;

            value = PhGetItemCircularBuffer_FLOAT(&context->DeviceEntry->TotalBuffer, getTooltipText->Index);

            // %.2f W\%s
            PhInitFormatF(&format[0], value, 2);
            PhInitFormatS(&format[1], L" W\n");
            PhInitFormatSR(&format[2], PH_AUTO_T(PH_STRING, PhGetStatisticsTimeString(NULL, getTooltipText->Index))->sr);

            PhMoveReference(&Section->GraphState.TooltipText, PhFormat(format, RTL_NUMBER_OF(format), 0));
            getTooltipText->Text = PhGetStringRef(Section->GraphState.TooltipText);
        }
        return TRUE;
    case SysInfoGraphDrawPanel:
        {
            PPH_SYSINFO_DRAW_PANEL drawPanel = (PPH_SYSINFO_DRAW_PANEL)Parameter1;
            PH_FORMAT format[2];

            if (!drawPanel)
                break;

            drawPanel->Title = PhCreateString(L"RAPL");

            // %.2f W
            PhInitFormatF(&format[0], context->DeviceEntry->CurrentTotalPower, 2);
            PhInitFormatS(&format[1], L" W");

            drawPanel->SubTitle = PhFormat(format, RTL_NUMBER_OF(format), 0);
        }
        return TRUE;
    }

    return FALSE;
}

VOID RaplDeviceSysInfoInitializing(
    _In_ PPH_PLUGIN_SYSINFO_POINTERS Pointers,
    _In_ _Assume_refs_(1) PDV_RAPL_ENTRY DeviceEntry
    )
{
    static PH_STRINGREF text = PH_STRINGREF_INIT(L"RAPL");
    PDV_RAPL_SYSINFO_CONTEXT context;
    PH_SYSINFO_SECTION section;

    context = PhAllocateZero(sizeof(DV_RAPL_SYSINFO_CONTEXT));
    context->DeviceEntry = DeviceEntry;

    memset(&section, 0, sizeof(PH_SYSINFO_SECTION));
    section.Context = context;
    section.Callback = RaplDeviceSectionCallback;
    section.Name = text;

    context->SysinfoSection = Pointers->CreateSection(&section);
}
