#include "pch.h"
#include "ClaudeUsageData.h"

#include <winhttp.h>

#include <cmath>
#include <cstdio>
#include <cwctype>
#include <string>

namespace
{
#pragma comment(lib, "winhttp.lib")

constexpr unsigned long long REFRESH_INTERVAL_MS = 60ULL * 1000ULL;
constexpr unsigned long long RETRY_INTERVAL_MS = 5ULL * 1000ULL;
constexpr unsigned long long MAX_JSON_FILE_SIZE = 1024ULL * 1024ULL;
constexpr wchar_t USAGE_API_HOST[] = L"api.anthropic.com";
constexpr wchar_t USAGE_API_PATH[] = L"/api/oauth/usage";
constexpr wchar_t USAGE_API_BETA_HEADER[] = L"oauth-2025-04-20";

std::wstring GetEnvVar(const wchar_t* name)
{
    const DWORD length = GetEnvironmentVariableW(name, nullptr, 0);
    if (length == 0)
        return std::wstring();

    std::wstring value(length - 1, L'\0');
    GetEnvironmentVariableW(name, value.empty() ? nullptr : &value[0], length);
    return value;
}

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

std::wstring JoinPath(const std::wstring& left, const std::wstring& right)
{
    if (left.empty())
        return right;
    if (left.back() == L'\\' || left.back() == L'/')
        return left + right;
    return left + L'\\' + right;
}

std::wstring GetClaudeConfigDir()
{
    const std::wstring override_dir = TrimString(GetEnvVar(L"CLAUDE_CONFIG_DIR"));
    if (!override_dir.empty())
        return override_dir;

    const std::wstring home = GetEnvVar(L"USERPROFILE");
    if (home.empty())
        return std::wstring();

    return JoinPath(home, L".claude");
}

bool ReadUtf8File(const std::wstring& path, std::wstring& output)
{
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes))
        return false;

    const unsigned long long file_size =
        (static_cast<unsigned long long>(attributes.nFileSizeHigh) << 32) |
        static_cast<unsigned long long>(attributes.nFileSizeLow);
    if (file_size > MAX_JSON_FILE_SIZE)
        return false;

    FILE* file{};
    if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || file == nullptr)
        return false;

    std::string bytes;
    bytes.reserve(static_cast<size_t>(file_size));

    char buffer[4096];
    while (true)
    {
        const size_t read_size = fread(buffer, 1, sizeof(buffer), file);
        if (read_size == 0)
            break;
        bytes.append(buffer, read_size);
    }
    fclose(file);

    if (bytes.size() >= 3 &&
        static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF)
    {
        bytes.erase(0, 3);
    }

    if (bytes.empty())
    {
        output.clear();
        return true;
    }

    const int wide_size = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (wide_size <= 0)
        return false;

    output.assign(wide_size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), output.empty() ? nullptr : &output[0], wide_size);
    return true;
}

bool FindJsonKey(const std::wstring& json, const wchar_t* key, size_t& value_pos)
{
    std::wstring token = L"\"";
    token += key;
    token += L"\"";

    const size_t key_pos = json.find(token);
    if (key_pos == std::wstring::npos)
        return false;

    const size_t colon_pos = json.find(L':', key_pos + token.size());
    if (colon_pos == std::wstring::npos)
        return false;

    value_pos = colon_pos + 1;
    return true;
}

size_t FindMatchingBracket(const std::wstring& text, size_t open_pos, wchar_t open_char, wchar_t close_char)
{
    bool in_string = false;
    bool escape = false;
    int depth = 0;

    for (size_t index = open_pos; index < text.size(); ++index)
    {
        const wchar_t ch = text[index];
        if (in_string)
        {
            if (escape)
                escape = false;
            else if (ch == L'\\')
                escape = true;
            else if (ch == L'"')
                in_string = false;
            continue;
        }

        if (ch == L'"')
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

    return std::wstring::npos;
}

bool TryGetJsonString(const std::wstring& json, const wchar_t* key, std::wstring& value)
{
    size_t value_pos{};
    if (!FindJsonKey(json, key, value_pos))
        return false;

    while (value_pos < json.size() && iswspace(json[value_pos]))
        ++value_pos;

    if (value_pos >= json.size() || json[value_pos] != L'"')
        return false;

    const size_t end_pos = json.find(L'"', value_pos + 1);
    if (end_pos == std::wstring::npos)
        return false;

    value = json.substr(value_pos + 1, end_pos - value_pos - 1);
    return true;
}

bool TryGetJsonDouble(const std::wstring& json, const wchar_t* key, double& value)
{
    size_t value_pos{};
    if (!FindJsonKey(json, key, value_pos))
        return false;

    while (value_pos < json.size() && iswspace(json[value_pos]))
        ++value_pos;

    if (value_pos >= json.size())
        return false;

    wchar_t* end_ptr{};
    value = wcstod(json.c_str() + value_pos, &end_ptr);
    return end_ptr != json.c_str() + value_pos;
}

bool TryGetJsonObject(const std::wstring& json, const wchar_t* key, std::wstring& value)
{
    size_t value_pos{};
    if (!FindJsonKey(json, key, value_pos))
        return false;

    while (value_pos < json.size() && iswspace(json[value_pos]))
        ++value_pos;

    if (value_pos >= json.size() || json[value_pos] != L'{')
        return false;

    const size_t end_pos = FindMatchingBracket(json, value_pos, L'{', L'}');
    if (end_pos == std::wstring::npos)
        return false;

    value = json.substr(value_pos, end_pos - value_pos + 1);
    return true;
}

bool TryParseUtcIso8601(const std::wstring& text, FILETIME& file_time)
{
    int year{}, month{}, day{}, hour{}, minute{}, second{}, millisecond{};
    int offset_hour{}, offset_minute{};
    wchar_t offset_sign{};

    int matched = swscanf_s(text.c_str(), L"%d-%d-%dT%d:%d:%d.%dZ", &year, &month, &day, &hour, &minute, &second, &millisecond);
    if (matched < 6)
        matched = swscanf_s(text.c_str(), L"%d-%d-%dT%d:%d:%dZ", &year, &month, &day, &hour, &minute, &second);
    if (matched < 6)
        matched = swscanf_s(text.c_str(), L"%d-%d-%dT%d:%d:%d.%d%c%d:%d", &year, &month, &day, &hour, &minute, &second, &millisecond, &offset_sign, 1, &offset_hour, &offset_minute);
    if (matched < 6)
        matched = swscanf_s(text.c_str(), L"%d-%d-%dT%d:%d:%d%c%d:%d", &year, &month, &day, &hour, &minute, &second, &offset_sign, 1, &offset_hour, &offset_minute);
    if (matched < 6)
        return false;

    SYSTEMTIME system_time{};
    system_time.wYear = static_cast<WORD>(year);
    system_time.wMonth = static_cast<WORD>(month);
    system_time.wDay = static_cast<WORD>(day);
    system_time.wHour = static_cast<WORD>(hour);
    system_time.wMinute = static_cast<WORD>(minute);
    system_time.wSecond = static_cast<WORD>(second);
    system_time.wMilliseconds = static_cast<WORD>((matched == 7 || matched == 10) ? millisecond : 0);

    if (!SystemTimeToFileTime(&system_time, &file_time))
        return false;

    if (offset_sign == L'+' || offset_sign == L'-')
    {
        ULARGE_INTEGER raw_time{};
        raw_time.LowPart = file_time.dwLowDateTime;
        raw_time.HighPart = file_time.dwHighDateTime;

        const unsigned long long offset_ticks =
            (static_cast<unsigned long long>(offset_hour) * 60ULL + static_cast<unsigned long long>(offset_minute)) *
            60ULL * 10000000ULL;

        if (offset_sign == L'+')
            raw_time.QuadPart -= offset_ticks;
        else
            raw_time.QuadPart += offset_ticks;

        file_time.dwLowDateTime = raw_time.LowPart;
        file_time.dwHighDateTime = raw_time.HighPart;
    }

    return true;
}

bool FileTimeToUnixSeconds(const FILETIME& file_time, long long& unix_seconds)
{
    ULARGE_INTEGER value{};
    value.LowPart = file_time.dwLowDateTime;
    value.HighPart = file_time.dwHighDateTime;
    if (value.QuadPart < 116444736000000000ULL)
        return false;

    unix_seconds = static_cast<long long>((value.QuadPart - 116444736000000000ULL) / 10000000ULL);
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

    long long now_unix_seconds{};
    if (!FileTimeToUnixSeconds(now_file_time, now_unix_seconds))
        return std::wstring();

    if (reset_at_unix_seconds <= now_unix_seconds)
        return L"now";

    return L"in " + FormatDurationFromSeconds(static_cast<unsigned long long>(reset_at_unix_seconds - now_unix_seconds));
}

bool FileTimeToLocalText(const FILETIME& utc_file_time, std::wstring& text)
{
    SYSTEMTIME utc_time{};
    if (!FileTimeToSystemTime(&utc_file_time, &utc_time))
        return false;

    SYSTEMTIME local_time{};
    if (!SystemTimeToTzSpecificLocalTime(nullptr, &utc_time, &local_time))
        return false;

    wchar_t buffer[32];
    swprintf_s(
        buffer,
        L"%04u-%02u-%02u %02u:%02u",
        static_cast<unsigned>(local_time.wYear),
        static_cast<unsigned>(local_time.wMonth),
        static_cast<unsigned>(local_time.wDay),
        static_cast<unsigned>(local_time.wHour),
        static_cast<unsigned>(local_time.wMinute));
    text = buffer;
    return true;
}

std::wstring FormatResetTime(const std::wstring& raw_value)
{
    FILETIME utc_file_time{};
    if (!TryParseUtcIso8601(raw_value, utc_file_time))
        return raw_value;

    std::wstring text;
    if (!FileTimeToLocalText(utc_file_time, text))
        return raw_value;
    return text;
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

std::wstring BuildMetricTooltip(const wchar_t* label, const CClaudeUsageData::Metric& metric)
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

bool LoadAccessToken(std::wstring& access_token)
{
    const std::wstring config_dir = GetClaudeConfigDir();
    if (config_dir.empty())
        return false;

    std::wstring credentials_json;
    if (!ReadUtf8File(JoinPath(config_dir, L".credentials.json"), credentials_json))
        return false;

    return TryGetJsonString(credentials_json, L"accessToken", access_token) && !access_token.empty();
}

bool FetchUsageApiJson(const std::wstring& access_token, std::wstring& response_body, DWORD& status_code)
{
    status_code = 0;
    HINTERNET session{};
    HINTERNET connection{};
    HINTERNET request{};

    session = WinHttpOpen(L"TrafficMonitor_ClaudeUsagePlugin/0.3.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr)
        return false;

    connection = WinHttpConnect(session, USAGE_API_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (connection == nullptr)
    {
        if (session != nullptr)
            WinHttpCloseHandle(session);
        return false;
    }

    request = WinHttpOpenRequest(connection, L"GET", USAGE_API_PATH, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (request == nullptr)
    {
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return false;
    }

    const DWORD decoding_enabled = WINHTTP_DECOMPRESSION_FLAG_GZIP | WINHTTP_DECOMPRESSION_FLAG_DEFLATE;
    WinHttpSetOption(request, WINHTTP_OPTION_DECOMPRESSION, const_cast<DWORD*>(&decoding_enabled), sizeof(decoding_enabled));

    std::wstring headers = L"Authorization: Bearer ";
    headers += access_token;
    headers += L"\r\nanthropic-beta: ";
    headers += USAGE_API_BETA_HEADER;
    headers += L"\r\nAccept: application/json\r\n";

    bool succeed = false;
    if (WinHttpSendRequest(request, headers.c_str(), static_cast<DWORD>(headers.length()), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr))
    {
        DWORD header_size = sizeof(status_code);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &header_size, WINHTTP_NO_HEADER_INDEX);

        std::string response_bytes;
        while (true)
        {
            DWORD available_size{};
            if (!WinHttpQueryDataAvailable(request, &available_size))
                break;
            if (available_size == 0)
            {
                succeed = true;
                break;
            }

            std::string chunk(available_size, '\0');
            DWORD read_size{};
            if (!WinHttpReadData(request, &chunk[0], available_size, &read_size))
                break;

            chunk.resize(read_size);
            response_bytes += chunk;
        }

        if (succeed)
        {
            if (response_bytes.empty())
            {
                response_body.clear();
            }
            else
            {
                const int wide_size = MultiByteToWideChar(CP_UTF8, 0, response_bytes.data(), static_cast<int>(response_bytes.size()), nullptr, 0);
                if (wide_size > 0)
                {
                    response_body.assign(wide_size, L'\0');
                    MultiByteToWideChar(CP_UTF8, 0, response_bytes.data(), static_cast<int>(response_bytes.size()), &response_body[0], wide_size);
                }
                else
                {
                    succeed = false;
                }
            }
        }
    }

    if (request != nullptr)
        WinHttpCloseHandle(request);
    if (connection != nullptr)
        WinHttpCloseHandle(connection);
    if (session != nullptr)
        WinHttpCloseHandle(session);
    return succeed;
}

bool LoadMetricFromApiSection(const std::wstring& response_json, const wchar_t* section_name, CClaudeUsageData::Metric& metric)
{
    std::wstring section_json;
    if (!TryGetJsonObject(response_json, section_name, section_json))
        return false;

    double utilization{};
    if (!TryGetJsonDouble(section_json, L"utilization", utilization))
        return false;

    metric.available = true;
    metric.percentage = utilization;

    std::wstring resets_at;
    if (TryGetJsonString(section_json, L"resets_at", resets_at))
    {
        FILETIME reset_file_time{};
        if (TryParseUtcIso8601(resets_at, reset_file_time))
        {
            metric.has_reset_time = FileTimeToUnixSeconds(reset_file_time, metric.reset_at_unix_seconds);
            if (!FileTimeToLocalText(reset_file_time, metric.reset_time_text))
                metric.reset_time_text = resets_at;
        }
        else
        {
            metric.reset_time_text = resets_at;
        }
    }

    return true;
}
}

CClaudeUsageData& CClaudeUsageData::Instance()
{
    static CClaudeUsageData instance;
    return instance;
}

void CClaudeUsageData::RefreshIfNeeded()
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

const std::wstring& CClaudeUsageData::GetValueText(ClaudeUsageWindow window) const
{
    thread_local std::wstring value_text;

    std::lock_guard<std::mutex> lock(m_state_mutex);
    value_text = (window == ClaudeUsageWindow::Rolling5Hours ? m_snapshot.value_5h_text : m_snapshot.value_7d_text);
    return value_text;
}

const CClaudeUsageData::Metric& CClaudeUsageData::GetMetric(ClaudeUsageWindow window) const
{
    thread_local Metric metric;

    std::lock_guard<std::mutex> lock(m_state_mutex);
    metric = (window == ClaudeUsageWindow::Rolling5Hours ? m_snapshot.rolling_5h : m_snapshot.rolling_7d);
    return metric;
}

const std::wstring& CClaudeUsageData::GetTooltipText() const
{
    thread_local std::wstring tooltip_text;

    std::lock_guard<std::mutex> lock(m_state_mutex);
    tooltip_text = m_snapshot.tooltip_text;
    return tooltip_text;
}

bool CClaudeUsageData::Refresh()
{
    Snapshot snapshot;
    const bool succeeded = LoadFromUsageApi(snapshot);
    FinalizeSnapshot(snapshot);

    std::lock_guard<std::mutex> lock(m_state_mutex);
    m_snapshot = snapshot;
    return succeeded;
}

bool CClaudeUsageData::LoadFromUsageApi(Snapshot& snapshot)
{
    std::wstring access_token;
    if (!LoadAccessToken(access_token))
    {
        snapshot.error_text = L"Claude access token not found";
        return false;
    }

    std::wstring response_json;
    DWORD status_code{};
    if (!FetchUsageApiJson(access_token, response_json, status_code))
    {
        snapshot.error_text = L"Claude usage API request failed";
        return false;
    }

    if (status_code == HTTP_STATUS_DENIED || status_code == HTTP_STATUS_FORBIDDEN)
    {
        snapshot.error_text = L"Claude login required";
        return false;
    }
    if (status_code == 429)
    {
        snapshot.error_text = L"Claude usage API rate limited";
        return false;
    }
    if (status_code != HTTP_STATUS_OK)
    {
        snapshot.error_text = L"Claude usage API HTTP ";
        snapshot.error_text += std::to_wstring(status_code);
        return false;
    }

    const bool has_five_hour = LoadMetricFromApiSection(response_json, L"five_hour", snapshot.rolling_5h);
    const bool has_seven_day = LoadMetricFromApiSection(response_json, L"seven_day", snapshot.rolling_7d);
    if (!has_five_hour && !has_seven_day)
    {
        snapshot.error_text = L"Claude usage API returned unexpected data";
        return false;
    }

    return true;
}

void CClaudeUsageData::FinalizeSnapshot(Snapshot& snapshot)
{
    snapshot.value_5h_text = (snapshot.rolling_5h.available ? FormatPercentage(snapshot.rolling_5h.percentage) : L"--");
    snapshot.value_7d_text = (snapshot.rolling_7d.available ? FormatPercentage(snapshot.rolling_7d.percentage) : L"--");

    if (!HasAvailableMetric(snapshot))
    {
        snapshot.tooltip_text = L"Claude account usage unavailable";
        if (!snapshot.error_text.empty())
        {
            snapshot.tooltip_text += L"\n";
            snapshot.tooltip_text += snapshot.error_text;
        }
        return;
    }

    snapshot.tooltip_text = L"Claude account usage";
    snapshot.tooltip_text += L"\n";
    snapshot.tooltip_text += BuildMetricTooltip(L"5h", snapshot.rolling_5h);
    snapshot.tooltip_text += L"\n";
    snapshot.tooltip_text += BuildMetricTooltip(L"7d", snapshot.rolling_7d);
    snapshot.tooltip_text += L"\nSource: Claude OAuth usage API";
}

bool CClaudeUsageData::HasAvailableMetric(const Snapshot& snapshot)
{
    return snapshot.rolling_5h.available || snapshot.rolling_7d.available;
}
