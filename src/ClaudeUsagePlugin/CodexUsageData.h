#pragma once

#include <mutex>
#include <string>

enum class CodexUsageWindow
{
    Rolling5Hours,
    Rolling7Days,
};

class CCodexUsageData
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
    static CCodexUsageData& Instance();

    void RefreshIfNeeded();
    const std::wstring& GetValueText(CodexUsageWindow window) const;
    const Metric& GetMetric(CodexUsageWindow window) const;
    const std::wstring& GetTooltipText() const;

public:
    struct Snapshot
    {
        Metric rolling_5h;
        Metric rolling_7d;
        std::wstring value_5h_text{ L"--" };
        std::wstring value_7d_text{ L"--" };
        std::wstring tooltip_text{ L"Codex usage limits unavailable" };
        std::wstring error_text;
        std::wstring source_text;
    };

private:
    CCodexUsageData() = default;

    bool Refresh();
    static bool LoadFromStore(Snapshot& snapshot);
    static bool LoadFromSessionJsonlStore(const std::wstring& store_dir, Snapshot& snapshot);
    static bool LoadLatestSessionJsonlFile(const std::wstring& file_path, Snapshot& snapshot);
    static void FinalizeSnapshot(Snapshot& snapshot);
    static bool HasAvailableMetric(const Snapshot& snapshot);

private:
    mutable std::mutex m_state_mutex;
    Snapshot m_snapshot;
    unsigned long long m_last_refresh_tick{};
    bool m_last_refresh_succeeded{};
    bool m_refresh_in_progress{};
};

#define g_codex_usage_data CCodexUsageData::Instance()
