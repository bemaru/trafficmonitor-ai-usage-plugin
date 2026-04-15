#include "pch.h"
#include "ClaudeUsagePlugin.h"

namespace
{
struct DrawColors
{
    COLORREF accent{};
    COLORREF track{};
    COLORREF border{};
    COLORREF text{};
};

DrawColors GetDrawColors(ClaudeUsageWindow window, bool dark_mode)
{
    if (window == ClaudeUsageWindow::Rolling5Hours)
    {
        return dark_mode
            ? DrawColors{ RGB(78, 143, 255), RGB(36, 48, 68), RGB(56, 76, 110), RGB(226, 232, 242) }
            : DrawColors{ RGB(24, 92, 204), RGB(224, 233, 246), RGB(166, 184, 208), RGB(52, 60, 74) };
    }

    return dark_mode
        ? DrawColors{ RGB(106, 160, 236), RGB(42, 54, 72), RGB(60, 78, 104), RGB(220, 228, 240) }
        : DrawColors{ RGB(66, 124, 196), RGB(224, 233, 246), RGB(170, 186, 206), RGB(54, 62, 76) };
}

int MeasureTextWidth(CDC* pDC, const wchar_t* text)
{
    if (pDC == nullptr || text == nullptr || *text == L'\0')
        return 0;

    return pDC->GetTextExtent(text).cx;
}

float GetUsageRatio(const CClaudeUsageData::Metric& metric)
{
    if (!metric.available)
        return 0.0f;

    if (metric.percentage <= 0.0)
        return 0.0f;

    if (metric.percentage >= 100.0)
        return 1.0f;

    return static_cast<float>(metric.percentage / 100.0);
}

float GetUsageRatio(const CCodexUsageData::Metric& metric)
{
    if (!metric.available)
        return 0.0f;

    if (metric.percentage <= 0.0)
        return 0.0f;

    if (metric.percentage >= 100.0)
        return 1.0f;

    return static_cast<float>(metric.percentage / 100.0);
}

DrawColors GetCodexDrawColors(CodexUsageWindow window, bool dark_mode)
{
    if (window == CodexUsageWindow::Rolling5Hours)
    {
        return dark_mode
            ? DrawColors{ RGB(38, 166, 92), RGB(40, 68, 49), RGB(66, 100, 78), RGB(226, 230, 236) }
            : DrawColors{ RGB(34, 152, 86), RGB(224, 244, 233), RGB(173, 206, 184), RGB(54, 62, 76) };
    }

    return dark_mode
        ? DrawColors{ RGB(92, 176, 118), RGB(42, 64, 48), RGB(64, 94, 74), RGB(226, 230, 236) }
        : DrawColors{ RGB(92, 164, 112), RGB(234, 245, 238), RGB(184, 205, 190), RGB(54, 62, 76) };
}

void DrawUsageItemBar(
    CDC* pDC,
    const DrawColors& colors,
    const wchar_t* label_text,
    const wchar_t* value_text,
    const wchar_t* value_sample_text,
    bool available,
    float ratio,
    int x,
    int y,
    int w,
    int h)
{
    if (pDC == nullptr || label_text == nullptr || value_text == nullptr || value_sample_text == nullptr || w <= 0 || h <= 0)
        return;

    const int padding = 4;
    const int gap = 6;
    const int accent_width = 3;
    const int bar_min_width = 36;
    const int bar_height = (h >= 16 ? 6 : 4);

    const int label_width = MeasureTextWidth(pDC, label_text);
    const int value_width = max(MeasureTextWidth(pDC, value_text), MeasureTextWidth(pDC, value_sample_text));

    CRect rect(x, y, x + w, y + h);
    CRect accent_rect(rect.left + padding, rect.top + 2, rect.left + padding + accent_width, rect.bottom - 2);
    pDC->FillSolidRect(accent_rect, colors.accent);

    int content_left = accent_rect.right + gap;
    int content_right = rect.right - padding;
    int value_left = content_right - value_width;
    int bar_left = content_left + label_width + gap;
    int bar_right = value_left - gap;

    if (bar_right - bar_left < bar_min_width)
    {
        const int shortage = bar_min_width - (bar_right - bar_left);
        const int trim_label = shortage / 2;
        content_left += trim_label;
        value_left += shortage - trim_label;
        bar_left = content_left + label_width + gap;
        bar_right = value_left - gap;
    }

    if (bar_right <= bar_left)
        bar_right = bar_left + bar_min_width;

    const int center_y = rect.top + (h / 2);
    const int bar_top = center_y - (bar_height / 2);
    const int bar_bottom = bar_top + bar_height;
    CRect bar_rect(bar_left, bar_top, bar_right, bar_bottom);
    CRect bar_fill_rect = bar_rect;

    bar_fill_rect.right = bar_fill_rect.left + static_cast<int>((bar_fill_rect.Width() * ratio) + 0.5f);
    pDC->FillSolidRect(bar_rect, colors.track);
    if (bar_fill_rect.Width() > 0)
        pDC->FillSolidRect(bar_fill_rect, colors.accent);
    CBrush border_brush;
    border_brush.CreateSolidBrush(colors.border);
    pDC->FrameRect(&bar_rect, &border_brush);

    const int old_bk_mode = pDC->SetBkMode(TRANSPARENT);
    const COLORREF old_text_color = pDC->GetTextColor();

    CRect label_rect(content_left, rect.top, bar_left - gap, rect.bottom);
    CRect value_rect(value_left, rect.top, rect.right - padding, rect.bottom);

    pDC->SetTextColor(colors.text);
    pDC->DrawTextW(label_text, -1, &label_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    pDC->SetTextColor(available ? colors.text : colors.border);
    pDC->DrawTextW(value_text, -1, &value_rect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    pDC->SetTextColor(old_text_color);
    pDC->SetBkMode(old_bk_mode);
}
}

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
    m_value_text_cache = g_claude_usage_data.GetValueText(m_window);
    return m_value_text_cache.c_str();
}

const wchar_t* CClaudeUsageItem::GetItemValueSampleText() const
{
    return L"99.9%";
}

bool CClaudeUsageItem::IsCustomDraw() const
{
    return true;
}

int CClaudeUsageItem::GetItemWidth() const
{
    return 96;
}

int CClaudeUsageItem::GetItemWidthEx(void* hDC) const
{
    CDC* pDC = CDC::FromHandle(static_cast<HDC>(hDC));
    if (pDC == nullptr)
        return GetItemWidth();

    const int padding = 4;
    const int gap = 6;
    const int bar_min_width = 36;
    const int label_width = MeasureTextWidth(pDC, GetItemLableText());
    const int value_width = MeasureTextWidth(pDC, GetItemValueSampleText());
    return padding * 2 + 4 + label_width + gap + bar_min_width + gap + value_width;
}

void CClaudeUsageItem::DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode)
{
    CDC* pDC = CDC::FromHandle(static_cast<HDC>(hDC));
    if (pDC == nullptr || w <= 0 || h <= 0)
        return;

    const DrawColors colors = GetDrawColors(m_window, dark_mode);
    const CClaudeUsageData::Metric metric = g_claude_usage_data.GetMetric(m_window);
    const std::wstring value_text = g_claude_usage_data.GetValueText(m_window);
    DrawUsageItemBar(pDC, colors, GetItemLableText(), value_text.c_str(), GetItemValueSampleText(), metric.available, GetUsageRatio(metric), x, y, w, h);
}

CCodexUsageItem::CCodexUsageItem(CodexUsageWindow window)
    : m_window(window)
{
}

const wchar_t* CCodexUsageItem::GetItemName() const
{
    return (m_window == CodexUsageWindow::Rolling5Hours ? L"Codex 5h" : L"Codex 7d");
}

const wchar_t* CCodexUsageItem::GetItemId() const
{
    return (m_window == CodexUsageWindow::Rolling5Hours ? L"CodexUsage5Hours" : L"CodexUsage7Days");
}

const wchar_t* CCodexUsageItem::GetItemLableText() const
{
    return (m_window == CodexUsageWindow::Rolling5Hours ? L"X5h" : L"X7d");
}

const wchar_t* CCodexUsageItem::GetItemValueText() const
{
    m_value_text_cache = g_codex_usage_data.GetValueText(m_window);
    return m_value_text_cache.c_str();
}

const wchar_t* CCodexUsageItem::GetItemValueSampleText() const
{
    return L"99.9%";
}

bool CCodexUsageItem::IsCustomDraw() const
{
    return true;
}

int CCodexUsageItem::GetItemWidth() const
{
    return 96;
}

int CCodexUsageItem::GetItemWidthEx(void* hDC) const
{
    CDC* pDC = CDC::FromHandle(static_cast<HDC>(hDC));
    if (pDC == nullptr)
        return GetItemWidth();

    const int padding = 4;
    const int gap = 6;
    const int bar_min_width = 36;
    const int label_width = MeasureTextWidth(pDC, GetItemLableText());
    const int value_width = MeasureTextWidth(pDC, GetItemValueSampleText());
    return padding * 2 + 4 + label_width + gap + bar_min_width + gap + value_width;
}

void CCodexUsageItem::DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode)
{
    CDC* pDC = CDC::FromHandle(static_cast<HDC>(hDC));
    if (pDC == nullptr || w <= 0 || h <= 0)
        return;

    const DrawColors colors = GetCodexDrawColors(m_window, dark_mode);
    const CCodexUsageData::Metric metric = g_codex_usage_data.GetMetric(m_window);
    const std::wstring value_text = g_codex_usage_data.GetValueText(m_window);
    DrawUsageItemBar(pDC, colors, GetItemLableText(), value_text.c_str(), GetItemValueSampleText(), metric.available, GetUsageRatio(metric), x, y, w, h);
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
    case 2:
        return &m_codex_five_hour_item;
    case 3:
        return &m_codex_seven_day_item;
    default:
        return nullptr;
    }
}

void CClaudeUsagePlugin::DataRequired()
{
    g_claude_usage_data.RefreshIfNeeded();
    g_codex_usage_data.RefreshIfNeeded();
}

const wchar_t* CClaudeUsagePlugin::GetInfo(PluginInfoIndex index)
{
    static std::wstring value;
    switch (index)
    {
    case TMI_NAME:
        value = L"Claude/Codex Usage";
        break;
    case TMI_DESCRIPTION:
        value = L"Shows Claude and Codex account usage percentages.";
        break;
    case TMI_AUTHOR:
        value = L"bemaru";
        break;
    case TMI_COPYRIGHT:
        value = L"Copyright (C) 2026";
        break;
    case TMI_VERSION:
        value = L"0.3.0";
        break;
    case TMI_URL:
        value = L"https://github.com/bemaru/trafficmonitor-ai-usage-plugin";
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
    g_codex_usage_data.RefreshIfNeeded();

    m_tooltip_text_cache = g_claude_usage_data.GetTooltipText();
    const std::wstring codex_tooltip = g_codex_usage_data.GetTooltipText();
    if (!m_tooltip_text_cache.empty() && !codex_tooltip.empty())
        m_tooltip_text_cache += L"\n\n";
    m_tooltip_text_cache += codex_tooltip;
    return m_tooltip_text_cache.c_str();
}

ITMPlugin* TMPluginGetInstance()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    return &CClaudeUsagePlugin::Instance();
}
