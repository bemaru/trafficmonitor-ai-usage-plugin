#include "pch.h"
#include "ClaudeUsagePlugin.h"

CClaudeUsageItem::CClaudeUsageItem(ClaudeUsageWindow window)
    : m_window(window)
{
}

const wchar_t* CClaudeUsageItem::GetItemName() const
{
    return (m_window == ClaudeUsageWindow::Rolling5Hours ? L"Claude 5h" : L"Claude 7d");
}

const wchar_t* CClaudeUsageItem::GetItemId() const
{
    return (m_window == ClaudeUsageWindow::Rolling5Hours ? L"ClaudeUsage5Hours" : L"ClaudeUsage7Days");
}

const wchar_t* CClaudeUsageItem::GetItemLableText() const
{
    return (m_window == ClaudeUsageWindow::Rolling5Hours ? L"C5h" : L"C7d");
}

const wchar_t* CClaudeUsageItem::GetItemValueText() const
{
    return g_claude_usage_data.GetValueText(m_window).c_str();
}

const wchar_t* CClaudeUsageItem::GetItemValueSampleText() const
{
    return L"100.0%";
}

CClaudeUsagePlugin& CClaudeUsagePlugin::Instance()
{
    static CClaudeUsagePlugin instance;
    return instance;
}

IPluginItem* CClaudeUsagePlugin::GetItem(int index)
{
    switch (index)
    {
    case 0:
        return &m_five_hour_item;
    case 1:
        return &m_seven_day_item;
    default:
        return nullptr;
    }
}

void CClaudeUsagePlugin::DataRequired()
{
    g_claude_usage_data.RefreshIfNeeded();
}

const wchar_t* CClaudeUsagePlugin::GetInfo(PluginInfoIndex index)
{
    static std::wstring value;
    switch (index)
    {
    case TMI_NAME:
        value = L"Claude Usage";
        break;
    case TMI_DESCRIPTION:
        value = L"Shows Claude account usage percentages from the Claude OAuth usage API.";
        break;
    case TMI_AUTHOR:
        value = L"bemaru";
        break;
    case TMI_COPYRIGHT:
        value = L"Copyright (C) 2026";
        break;
    case TMI_VERSION:
        value = L"0.2.0";
        break;
    case TMI_URL:
        value = L"https://github.com/bemaru/trafficmonitor-claude-usage-plugin";
        break;
    default:
        value.clear();
        break;
    }
    return value.c_str();
}

const wchar_t* CClaudeUsagePlugin::GetTooltipInfo()
{
    g_claude_usage_data.RefreshIfNeeded();
    return g_claude_usage_data.GetTooltipText().c_str();
}

ITMPlugin* TMPluginGetInstance()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    return &CClaudeUsagePlugin::Instance();
}
