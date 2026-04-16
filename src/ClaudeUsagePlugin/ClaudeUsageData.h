#pragma once

#include <string>
#include <mutex>

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
        bool has_reset_time{};
        long long reset_at_unix_seconds{};
        std::wstring reset_time_text;
    };

public:
    static CClaudeUsageData& Instance();

    struct Snapshot
    {
        Metric rolling_5h;
        Metric rolling_7d;
        std::wstring value_5h_text{ L"--" };
        std::wstring value_7d_text{ L"--" };
        std::wstring tooltip_text{ L"Claude usage limits unavailable" };
        std::wstring error_text;
        std::wstring source_text{ L"Claude OAuth usage API" };
    };

    void RefreshIfNeeded();
    void AutoStartBundledHelperIfNeeded();
    const std::wstring& GetValueText(ClaudeUsageWindow window) const;
    const Metric& GetMetric(ClaudeUsageWindow window) const;
    const std::wstring& GetTooltipText() const;

private:
    CClaudeUsageData() = default;

    bool Refresh(unsigned long long& retry_after_ms, bool allow_api_request);
    static bool LoadFromUsageApi(Snapshot& snapshot, unsigned long long& retry_after_ms, bool allow_api_request);
    static void FinalizeSnapshot(Snapshot& snapshot);
    static bool HasAvailableMetric(const Snapshot& snapshot);

private:
    mutable std::mutex m_state_mutex;
    Snapshot m_snapshot;
    unsigned long long m_last_refresh_tick{};
    unsigned long long m_next_refresh_tick{};
    bool m_last_refresh_succeeded{};
    bool m_refresh_in_progress{};
    bool m_helper_auto_start_attempted{};
};

#define g_claude_usage_data CClaudeUsageData::Instance()
