#pragma once

#include "ClaudeUsageData.h"
#include "CodexUsageData.h"
#include "PluginInterface.h"

class CClaudeUsageItem : public IPluginItem
{
public:
    explicit CClaudeUsageItem(ClaudeUsageWindow window);

    const wchar_t* GetItemName() const override;
    const wchar_t* GetItemId() const override;
    const wchar_t* GetItemLableText() const override;
    const wchar_t* GetItemValueText() const override;
    const wchar_t* GetItemValueSampleText() const override;
    bool IsCustomDraw() const override;
    int GetItemWidth() const override;
    int GetItemWidthEx(void* hDC) const override;
    void DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode) override;

private:
    ClaudeUsageWindow m_window;
    mutable std::wstring m_value_text_cache;
};

class CCodexUsageItem : public IPluginItem
{
public:
    explicit CCodexUsageItem(CodexUsageWindow window);

    const wchar_t* GetItemName() const override;
    const wchar_t* GetItemId() const override;
    const wchar_t* GetItemLableText() const override;
    const wchar_t* GetItemValueText() const override;
    const wchar_t* GetItemValueSampleText() const override;
    bool IsCustomDraw() const override;
    int GetItemWidth() const override;
    int GetItemWidthEx(void* hDC) const override;
    void DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode) override;

private:
    CodexUsageWindow m_window;
    mutable std::wstring m_value_text_cache;
};

class CClaudeUsagePlugin : public ITMPlugin
{
private:
    CClaudeUsagePlugin() = default;

public:
    static CClaudeUsagePlugin& Instance();

    IPluginItem* GetItem(int index) override;
    void OnInitialize(ITrafficMonitor* pApp) override;
    void DataRequired() override;
    const wchar_t* GetInfo(PluginInfoIndex index) override;
    const wchar_t* GetTooltipInfo() override;

private:
    CClaudeUsageItem m_five_hour_item{ ClaudeUsageWindow::Rolling5Hours };
    CClaudeUsageItem m_seven_day_item{ ClaudeUsageWindow::Rolling7Days };
    CCodexUsageItem m_codex_five_hour_item{ CodexUsageWindow::Rolling5Hours };
    CCodexUsageItem m_codex_seven_day_item{ CodexUsageWindow::Rolling7Days };
    std::wstring m_tooltip_text_cache;
};

#ifdef __cplusplus
extern "C" {
#endif
    __declspec(dllexport) ITMPlugin* TMPluginGetInstance();
#ifdef __cplusplus
}
#endif
