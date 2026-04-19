#include "pch.h"
#include "CodexUsageData.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <string>
#include <vector>

namespace
{
constexpr unsigned long long REFRESH_INTERVAL_MS = 60ULL * 1000ULL;
constexpr unsigned long long RETRY_INTERVAL_MS = 5ULL * 1000ULL;
constexpr unsigned long long MAX_TEXT_FILE_SIZE = 32ULL * 1024ULL * 1024ULL;
constexpr wchar_t CODEX_SESSION_DIR_NAME[] = L"sessions";

struct SessionFileCandidate
{
    std::wstring path;
    FILETIME last_write_time{};
};

std::wstring TrimString(const std::wstring& value)
{
    size_t start{};
    while (start < value.size() && iswspace(value[start]))
        ++start;

    size_t end = value.size();
    while (end > start && iswspace(value[end - 1]))
        --end;

    return value.substr(start, end - start);
}

std::wstring GetEnvVar(const wchar_t* name)
{
    const DWORD length = GetEnvironmentVariableW(name, nullptr, 0);
    if (length <= 1)
        return std::wstring();

    std::wstring value(length - 1, L'\0');
    GetEnvironmentVariableW(name, &value[0], length);
    return value;
}

std::wstring JoinPath(const std::wstring& left, const std::wstring& right)
{
    if (left.empty())
        return right;
    if (left.back() == L'\\' || left.back() == L'/')
        return left + right;
    return left + L'\\' + right;
}

bool EndsWithIgnoreCase(const std::wstring& value, const wchar_t* suffix)
{
    const size_t suffix_length = wcslen(suffix);
    if (value.size() < suffix_length)
        return false;

    const size_t start = value.size() - suffix_length;
    for (size_t index = 0; index < suffix_length; ++index)
    {
        if (towlower(value[start + index]) != towlower(suffix[index]))
            return false;
    }
    return true;
}

std::wstring NormalizePossibleWslPath(const std::wstring& path)
{
    if (path.size() >= 7 && path[0] == L'/' && path[1] == L'm' && path[2] == L'n' && path[3] == L't' && path[4] == L'/' && iswalpha(path[5]) && path[6] == L'/')
    {
        std::wstring converted;
        converted.push_back(static_cast<wchar_t>(towupper(path[5])));
        converted += L":";
        converted += path.substr(6);
        std::replace(converted.begin(), converted.end(), L'/', L'\\');
        return converted;
    }

    return path;
}

std::wstring ExpandEnvironmentVariables(const std::wstring& text)
{
    if (text.find(L'%') == std::wstring::npos)
        return text;

    const DWORD required_size = ExpandEnvironmentStringsW(text.c_str(), nullptr, 0);
    if (required_size <= 1)
        return text;

    std::wstring expanded(required_size, L'\0');
    if (ExpandEnvironmentStringsW(text.c_str(), &expanded[0], required_size) == 0)
        return text;

    if (!expanded.empty() && expanded.back() == L'\0')
        expanded.pop_back();
    return expanded;
}

std::wstring GetCodexConfigDir()
{
    const std::wstring override_dir = NormalizePossibleWslPath(ExpandEnvironmentVariables(TrimString(GetEnvVar(L"CODEX_HOME"))));
    if (!override_dir.empty())
        return override_dir;

    const std::wstring home = TrimString(GetEnvVar(L"USERPROFILE"));
    if (home.empty())
        return std::wstring();

    return JoinPath(home, L".codex");
}

bool FileExists(const std::wstring& path)
{
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool DirectoryExists(const std::wstring& path)
{
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool GetFileSizeBytes(const std::wstring& path, unsigned long long& size)
{
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes))
        return false;

    size = (static_cast<unsigned long long>(attributes.nFileSizeHigh) << 32) | static_cast<unsigned long long>(attributes.nFileSizeLow);
    return true;
}

bool ReadUtf8Line(FILE* file, std::string& line)
{
    line.clear();
    if (file == nullptr)
        return false;

    char buffer[8192];
    bool read_any = false;
    while (fgets(buffer, sizeof(buffer), file) != nullptr)
    {
        read_any = true;
        line += buffer;
        if (strchr(buffer, '\n') != nullptr)
            return true;
        if (strlen(buffer) < sizeof(buffer) - 1)
            return true;
    }

    return read_any;
}

bool FindJsonKey(const std::string& json, const char* key, size_t& value_pos)
{
    std::string token = "\"";
    token += key;
    token += "\"";

    const size_t key_pos = json.find(token);
    if (key_pos == std::string::npos)
        return false;

    const size_t colon_pos = json.find(':', key_pos + token.size());
    if (colon_pos == std::string::npos)
        return false;

    value_pos = colon_pos + 1;
    return true;
}

size_t FindMatchingBracket(const std::string& text, size_t open_pos, char open_char, char close_char)
{
    bool in_string = false;
    bool escape = false;
    int depth = 0;

    for (size_t index = open_pos; index < text.size(); ++index)
    {
        const char ch = text[index];
        if (in_string)
        {
            if (escape)
                escape = false;
            else if (ch == '\\')
                escape = true;
            else if (ch == '"')
                in_string = false;
            continue;
        }

        if (ch == '"')
        {
            in_string = true;
            continue;
        }

        if (ch == open_char)
            ++depth;
        else if (ch == close_char)
        {
            --depth;
            if (depth == 0)
                return index;
        }
    }

    return std::string::npos;
}

bool TryGetJsonDouble(const std::string& json, const char* key, double& value)
{
    size_t value_pos{};
    if (!FindJsonKey(json, key, value_pos))
        return false;

    while (value_pos < json.size() && isspace(static_cast<unsigned char>(json[value_pos])))
        ++value_pos;

    if (value_pos >= json.size())
        return false;

    char* end_ptr{};
    value = strtod(json.c_str() + value_pos, &end_ptr);
    return end_ptr != json.c_str() + value_pos;
}

bool TryGetJsonInt64(const std::string& json, const char* key, long long& value)
{
    size_t value_pos{};
    if (!FindJsonKey(json, key, value_pos))
        return false;

    while (value_pos < json.size() && isspace(static_cast<unsigned char>(json[value_pos])))
        ++value_pos;

    if (value_pos >= json.size())
        return false;

    char* end_ptr{};
    value = _strtoi64(json.c_str() + value_pos, &end_ptr, 10);
    return end_ptr != json.c_str() + value_pos;
}

bool TryGetJsonObject(const std::string& json, const char* key, std::string& value)
{
    size_t value_pos{};
    if (!FindJsonKey(json, key, value_pos))
        return false;

    while (value_pos < json.size() && isspace(static_cast<unsigned char>(json[value_pos])))
        ++value_pos;

    if (value_pos >= json.size() || json[value_pos] != '{')
        return false;

    const size_t end_pos = FindMatchingBracket(json, value_pos, '{', '}');
    if (end_pos == std::string::npos)
        return false;

    value = json.substr(value_pos, end_pos - value_pos + 1);
    return true;
}

bool UnixSecondsToLocalText(long long unix_seconds, std::wstring& text)
{
    if (unix_seconds < 0)
        return false;

    FILETIME file_time{};
    ULARGE_INTEGER value{};
    value.QuadPart = (static_cast<unsigned long long>(unix_seconds) + 11644473600ULL) * 10000000ULL;
    file_time.dwLowDateTime = value.LowPart;
    file_time.dwHighDateTime = value.HighPart;

    SYSTEMTIME utc_time{};
    if (!FileTimeToSystemTime(&file_time, &utc_time))
        return false;

    SYSTEMTIME local_time{};
    if (!SystemTimeToTzSpecificLocalTime(nullptr, &utc_time, &local_time))
        return false;

    const int date_length = GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &local_time, nullptr, nullptr, 0, nullptr);
    if (date_length <= 1)
        return false;

    const int time_length = GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &local_time, nullptr, nullptr, 0);
    if (time_length <= 1)
        return false;

    std::wstring date_text(static_cast<size_t>(date_length - 1), L'\0');
    std::wstring time_text(static_cast<size_t>(time_length - 1), L'\0');
    if (GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &local_time, nullptr, &date_text[0], date_length, nullptr) == 0)
        return false;
    if (GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &local_time, nullptr, &time_text[0], time_length) == 0)
        return false;

    text = date_text;
    text += L" ";
    text += time_text;
    return true;
}

std::wstring FormatDurationFromSeconds(unsigned long long total_seconds)
{
    if (total_seconds < 60ULL)
        return L"<1m";

    const unsigned long long total_minutes = total_seconds / 60ULL;
    const unsigned long long days = total_minutes / (24ULL * 60ULL);
    const unsigned long long hours = (total_minutes / 60ULL) % 24ULL;
    const unsigned long long minutes = total_minutes % 60ULL;

    std::wstring text;
    if (days > 0)
    {
        text = std::to_wstring(days) + L"d";
        if (hours > 0)
            text += L" " + std::to_wstring(hours) + L"h";
        return text;
    }

    if (hours > 0)
    {
        text = std::to_wstring(hours) + L"h";
        if (minutes > 0)
            text += L" " + std::to_wstring(minutes) + L"m";
        return text;
    }

    return std::to_wstring(minutes) + L"m";
}

std::wstring FormatResetRemaining(long long reset_at_unix_seconds)
{
    FILETIME now_file_time{};
    GetSystemTimeAsFileTime(&now_file_time);

    ULARGE_INTEGER now_value{};
    now_value.LowPart = now_file_time.dwLowDateTime;
    now_value.HighPart = now_file_time.dwHighDateTime;
    if (now_value.QuadPart < 116444736000000000ULL)
        return std::wstring();

    const long long now_unix_seconds = static_cast<long long>((now_value.QuadPart - 116444736000000000ULL) / 10000000ULL);
    if (reset_at_unix_seconds <= now_unix_seconds)
        return L"now";

    return L"in " + FormatDurationFromSeconds(static_cast<unsigned long long>(reset_at_unix_seconds - now_unix_seconds));
}

std::wstring FormatPercentage(double value)
{
    const double clamped = (value < 0.0 ? 0.0 : (value > 100.0 ? 100.0 : value));
    const double rounded_whole = std::round(clamped);
    wchar_t buffer[32];
    if (std::fabs(clamped - rounded_whole) < 0.05)
        swprintf_s(buffer, L"%.0f%%", rounded_whole);
    else
        swprintf_s(buffer, L"%.1f%%", clamped);
    return buffer;
}

std::wstring BuildMetricTooltip(const wchar_t* label, const CCodexUsageData::Metric& metric)
{
    std::wstring text(label);
    text += L": ";
    if (!metric.available)
    {
        text += L"unavailable";
        return text;
    }

    text += FormatPercentage(metric.percentage);
    const std::wstring reset_remaining = (metric.has_reset_time ? FormatResetRemaining(metric.reset_at_unix_seconds) : std::wstring());
    if (!reset_remaining.empty() && !metric.reset_time_text.empty())
    {
        text += L" (resets ";
        text += reset_remaining;
        text += L" at ";
        text += metric.reset_time_text;
        text += L")";
    }
    else if (!reset_remaining.empty())
    {
        text += L" (resets ";
        text += reset_remaining;
        text += L")";
    }
    else if (!metric.reset_time_text.empty())
    {
        text += L" (resets at ";
        text += metric.reset_time_text;
        text += L")";
    }
    return text;
}

unsigned long long GetRefreshIntervalMs(bool last_refresh_succeeded)
{
    return (last_refresh_succeeded ? REFRESH_INTERVAL_MS : RETRY_INTERVAL_MS);
}

bool LoadMetricFromRateLimitsSection(const std::string& section_json, const char* key, CCodexUsageData::Metric& metric)
{
    std::string metric_json;
    if (!TryGetJsonObject(section_json, key, metric_json))
        return false;

    double used_percent{};
    double remaining_percent{};
    if (TryGetJsonDouble(metric_json, "used_percent", used_percent))
        metric.percentage = used_percent;
    else if (TryGetJsonDouble(metric_json, "remaining_percent", remaining_percent))
        metric.percentage = 100.0 - remaining_percent;
    else
        return false;

    metric.available = true;

    long long reset_at{};
    if (TryGetJsonInt64(metric_json, "reset_at", reset_at) || TryGetJsonInt64(metric_json, "resets_at", reset_at))
    {
        metric.has_reset_time = true;
        metric.reset_at_unix_seconds = reset_at;
        std::wstring reset_text;
        if (UnixSecondsToLocalText(reset_at, reset_text))
            metric.reset_time_text = reset_text;
        else
            metric.reset_time_text = std::to_wstring(reset_at);
    }

    return true;
}

bool LoadFromRateLimitsJson(const std::string& json, CCodexUsageData::Snapshot& snapshot)
{
    std::string rate_limits_json;
    if (!TryGetJsonObject(json, "rate_limits", rate_limits_json))
        return false;

    const bool has_5h = LoadMetricFromRateLimitsSection(rate_limits_json, "primary", snapshot.rolling_5h);
    const bool has_7d = LoadMetricFromRateLimitsSection(rate_limits_json, "secondary", snapshot.rolling_7d);
    if (!has_5h && !has_7d)
        return false;

    return true;
}

std::wstring GetCodexSessionsDir()
{
    const std::wstring config_dir = GetCodexConfigDir();
    if (config_dir.empty())
        return std::wstring();
    return JoinPath(config_dir, CODEX_SESSION_DIR_NAME);
}

void CollectJsonlFilesRecursive(const std::wstring& root_dir, std::vector<SessionFileCandidate>& candidates)
{
    if (!DirectoryExists(root_dir))
        return;

    const std::wstring search_path = JoinPath(root_dir, L"*");
    WIN32_FIND_DATAW find_data{};
    HANDLE handle = FindFirstFileW(search_path.c_str(), &find_data);
    if (handle == INVALID_HANDLE_VALUE)
        return;

    do
    {
        const wchar_t* name = find_data.cFileName;
        if (wcscmp(name, L".") == 0 || wcscmp(name, L"..") == 0)
            continue;

        const std::wstring child_path = JoinPath(root_dir, name);
        if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            CollectJsonlFilesRecursive(child_path, candidates);
            continue;
        }

        if (!EndsWithIgnoreCase(child_path, L".jsonl"))
            continue;

        candidates.push_back(SessionFileCandidate{ child_path, find_data.ftLastWriteTime });
    }
    while (FindNextFileW(handle, &find_data));

    FindClose(handle);
}

bool CompareFileTimeNewer(const FILETIME& left, const FILETIME& right)
{
    return CompareFileTime(&left, &right) > 0;
}

bool LoadSessionJsonlFile(const std::wstring& file_path, CCodexUsageData::Snapshot& snapshot)
{
    unsigned long long file_size{};
    if (!GetFileSizeBytes(file_path, file_size) || file_size > MAX_TEXT_FILE_SIZE)
        return false;

    FILE* file{};
    if (_wfopen_s(&file, file_path.c_str(), L"rb") != 0 || file == nullptr)
        return false;

    bool found = false;
    CCodexUsageData::Snapshot current;
    std::string line;
    while (ReadUtf8Line(file, line))
    {
        if (line.find("\"rate_limits\"") == std::string::npos)
            continue;

        CCodexUsageData::Snapshot candidate;
        if (LoadFromRateLimitsJson(line, candidate))
        {
            current = candidate;
            found = true;
        }
    }

    fclose(file);

    if (!found)
        return false;

    snapshot.rolling_5h = current.rolling_5h;
    snapshot.rolling_7d = current.rolling_7d;
    snapshot.source_text = L"sessions JSONL";
    return true;
}

} // namespace

CCodexUsageData& CCodexUsageData::Instance()
{
    static CCodexUsageData instance;
    return instance;
}

void CCodexUsageData::RefreshIfNeeded()
{
    const unsigned long long now = GetTickCount64();
    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        if (m_refresh_in_progress)
            return;

        const unsigned long long refresh_interval_ms = GetRefreshIntervalMs(m_last_refresh_succeeded);
        if (m_last_refresh_tick != 0 && now - m_last_refresh_tick < refresh_interval_ms)
            return;

        m_refresh_in_progress = true;
    }

    const bool succeeded = Refresh();

    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        m_last_refresh_tick = now;
        m_last_refresh_succeeded = succeeded;
        m_refresh_in_progress = false;
    }
}

const std::wstring& CCodexUsageData::GetValueText(CodexUsageWindow window) const
{
    thread_local std::wstring value_text;

    std::lock_guard<std::mutex> lock(m_state_mutex);
    value_text = (window == CodexUsageWindow::Rolling5Hours ? m_snapshot.value_5h_text : m_snapshot.value_7d_text);
    return value_text;
}

const CCodexUsageData::Metric& CCodexUsageData::GetMetric(CodexUsageWindow window) const
{
    thread_local Metric metric;

    std::lock_guard<std::mutex> lock(m_state_mutex);
    metric = (window == CodexUsageWindow::Rolling5Hours ? m_snapshot.rolling_5h : m_snapshot.rolling_7d);
    return metric;
}

const std::wstring& CCodexUsageData::GetTooltipText() const
{
    thread_local std::wstring tooltip_text;

    std::lock_guard<std::mutex> lock(m_state_mutex);
    tooltip_text = m_snapshot.tooltip_text;
    return tooltip_text;
}

bool CCodexUsageData::Refresh()
{
    Snapshot snapshot;
    const bool succeeded = LoadFromStore(snapshot);
    FinalizeSnapshot(snapshot);

    std::lock_guard<std::mutex> lock(m_state_mutex);
    m_snapshot = snapshot;
    return succeeded;
}

bool CCodexUsageData::LoadFromStore(Snapshot& snapshot)
{
    const std::wstring config_dir = GetCodexConfigDir();
    if (config_dir.empty())
    {
        snapshot.error_text = L"Codex config directory not found";
        return false;
    }

    const std::wstring sessions_dir = GetCodexSessionsDir();
    const bool has_sessions_dir = DirectoryExists(sessions_dir);

    if (has_sessions_dir)
    {
        Snapshot candidate;
        if (LoadFromSessionJsonlStore(sessions_dir, candidate))
        {
            snapshot = candidate;
            return true;
        }
        snapshot.error_text = candidate.error_text;
    }
    else
    {
        snapshot.error_text = L"Codex sessions JSONL not found";
    }

    if (snapshot.error_text.empty())
        snapshot.error_text = L"Codex usage data unavailable";

    return false;
}

bool CCodexUsageData::LoadFromSessionJsonlStore(const std::wstring& store_dir, Snapshot& snapshot)
{
    std::vector<SessionFileCandidate> candidates;
    CollectJsonlFilesRecursive(store_dir, candidates);
    if (candidates.empty())
    {
        snapshot.error_text = L"Codex sessions JSONL not found";
        return false;
    }

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const SessionFileCandidate& left, const SessionFileCandidate& right)
        {
            if (CompareFileTimeNewer(left.last_write_time, right.last_write_time))
                return true;
            if (CompareFileTimeNewer(right.last_write_time, left.last_write_time))
                return false;
            return left.path < right.path;
        });

    for (const SessionFileCandidate& candidate : candidates)
    {
        Snapshot parsed;
        if (LoadLatestSessionJsonlFile(candidate.path, parsed))
        {
            snapshot.rolling_5h = parsed.rolling_5h;
            snapshot.rolling_7d = parsed.rolling_7d;
            snapshot.source_text = parsed.source_text;
            return true;
        }
    }

    snapshot.error_text = L"Codex sessions JSONL returned no rate limits";
    return false;
}

bool CCodexUsageData::LoadLatestSessionJsonlFile(const std::wstring& file_path, Snapshot& snapshot)
{
    Snapshot candidate;
    if (!LoadSessionJsonlFile(file_path, candidate))
        return false;

    snapshot.rolling_5h = candidate.rolling_5h;
    snapshot.rolling_7d = candidate.rolling_7d;
    snapshot.source_text = candidate.source_text;
    return true;
}

void CCodexUsageData::FinalizeSnapshot(Snapshot& snapshot)
{
    snapshot.value_5h_text = (snapshot.rolling_5h.available ? FormatPercentage(snapshot.rolling_5h.percentage) : L"--");
    snapshot.value_7d_text = (snapshot.rolling_7d.available ? FormatPercentage(snapshot.rolling_7d.percentage) : L"--");

    if (!HasAvailableMetric(snapshot))
    {
        snapshot.tooltip_text = L"Codex usage limits unavailable";
        if (!snapshot.error_text.empty())
        {
            snapshot.tooltip_text += L"\n";
            snapshot.tooltip_text += snapshot.error_text;
        }
        return;
    }

    snapshot.tooltip_text = L"Codex usage limits";
    snapshot.tooltip_text += L"\n";
    snapshot.tooltip_text += BuildMetricTooltip(L"5h", snapshot.rolling_5h);
    snapshot.tooltip_text += L"\n";
    snapshot.tooltip_text += BuildMetricTooltip(L"7d", snapshot.rolling_7d);
}

bool CCodexUsageData::HasAvailableMetric(const Snapshot& snapshot)
{
    return snapshot.rolling_5h.available || snapshot.rolling_7d.available;
}
