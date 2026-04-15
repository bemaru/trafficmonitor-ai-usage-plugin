#pragma once

#include <string>

enum class ClaudeUsageWindow
{
    Rolling5Hours,
    Rolling7Days,
};

class CClaudeUsageData
{
public:
    struct Metric
    {
        bool available{};
        double percentage{};
        std::wstring reset_time_text;
    };

public:
    static CClaudeUsageData& Instance();

    void RefreshIfNeeded();
    const std::wstring& GetValueText(ClaudeUsageWindow window) const;
    const std::wstring& GetTooltipText() const;

private:
    CClaudeUsageData() = default;

    struct Snapshot
    {
        Metric rolling_5h;
        Metric rolling_7d;
        std::wstring value_5h_text{ L"--" };
        std::wstring value_7d_text{ L"--" };
        std::wstring tooltip_text{ L"Claude account usage unavailable" };
        std::wstring error_text;
    };

    void Refresh();
    static bool LoadFromUsageApi(Snapshot& snapshot);
    static void FinalizeSnapshot(Snapshot& snapshot);
    static bool HasAvailableMetric(const Snapshot& snapshot);

private:
    Snapshot m_snapshot;
    unsigned long long m_last_refresh_tick{};
};

#define g_claude_usage_data CClaudeUsageData::Instance()
