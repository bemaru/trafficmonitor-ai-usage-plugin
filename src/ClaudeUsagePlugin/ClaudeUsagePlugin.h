#pragma once

#include "ClaudeUsageData.h"
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

private:
    ClaudeUsageWindow m_window;
};

class CClaudeUsagePlugin : public ITMPlugin
{
private:
    CClaudeUsagePlugin() = default;

public:
    static CClaudeUsagePlugin& Instance();

    IPluginItem* GetItem(int index) override;
    void DataRequired() override;
    const wchar_t* GetInfo(PluginInfoIndex index) override;
    const wchar_t* GetTooltipInfo() override;

private:
    CClaudeUsageItem m_five_hour_item{ ClaudeUsageWindow::Rolling5Hours };
    CClaudeUsageItem m_seven_day_item{ ClaudeUsageWindow::Rolling7Days };
};

#ifdef __cplusplus
extern "C" {
#endif
    __declspec(dllexport) ITMPlugin* TMPluginGetInstance();
#ifdef __cplusplus
}
#endif
