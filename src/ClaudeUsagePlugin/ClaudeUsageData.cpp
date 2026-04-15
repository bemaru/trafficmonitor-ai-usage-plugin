#include "pch.h"
#include "ClaudeUsageData.h"

#include <winhttp.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwctype>
#include <string>
#include <vector>

namespace
{
#pragma comment(lib, "winhttp.lib")

constexpr unsigned long long REFRESH_INTERVAL_MS = 180ULL * 1000ULL;
constexpr unsigned long long RETRY_INTERVAL_MS = 30ULL * 1000ULL;
constexpr unsigned long long STATUSLINE_REFRESH_INTERVAL_MS = 5ULL * 1000ULL;
constexpr unsigned long long RATE_LIMIT_RETRY_FALLBACK_MS = 5ULL * 60ULL * 1000ULL;
constexpr unsigned long long MAX_RETRY_AFTER_MS = 12ULL * 60ULL * 60ULL * 1000ULL;
constexpr unsigned long long CACHE_MAX_AGE_MS = 180ULL * 1000ULL;
constexpr unsigned long long MAX_JSON_FILE_SIZE = 1024ULL * 1024ULL;
constexpr wchar_t USAGE_API_HOST[] = L"api.anthropic.com";
constexpr wchar_t USAGE_API_PATH[] = L"/api/oauth/usage";
constexpr wchar_t USAGE_API_BETA_HEADER[] = L"oauth-2025-04-20";
constexpr wchar_t PLUGIN_CACHE_DIR_NAME[] = L"trafficmonitor-claude-usage-plugin";
constexpr wchar_t LEGACY_PLUGIN_CACHE_DIR_NAME[] = L"trafficmonitor-ai-usage-plugin";
constexpr wchar_t PLUGIN_CACHE_FILE_NAME[] = L"claude-usage.json";
constexpr wchar_t STATUSLINE_CACHE_FILE_NAME[] = L"claude-statusline.json";

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

struct UsageCacheCandidate
{
    std::wstring path;
    std::wstring source_text;
    int priority{};
    unsigned long long last_write_time_ms{};
};

std::wstring GetCacheDirByName(const wchar_t* dir_name)
{
    const std::wstring local_app_data = TrimString(GetEnvVar(L"LOCALAPPDATA"));
    if (!local_app_data.empty())
        return JoinPath(local_app_data, dir_name);

    const std::wstring home = TrimString(GetEnvVar(L"USERPROFILE"));
    if (home.empty())
        return std::wstring();

    return JoinPath(JoinPath(home, L".cache"), dir_name);
}

std::wstring GetPluginCacheDir()
{
    return GetCacheDirByName(PLUGIN_CACHE_DIR_NAME);
}

std::wstring GetLegacyPluginCacheDir()
{
    return GetCacheDirByName(LEGACY_PLUGIN_CACHE_DIR_NAME);
}

std::wstring BuildCachePath(const std::wstring& cache_dir, const wchar_t* file_name)
{
    if (cache_dir.empty())
        return std::wstring();
    return JoinPath(cache_dir, file_name);
}

std::wstring GetPluginCachePath()
{
    return BuildCachePath(GetPluginCacheDir(), PLUGIN_CACHE_FILE_NAME);
}

std::wstring GetLegacyPluginCachePath()
{
    return BuildCachePath(GetLegacyPluginCacheDir(), PLUGIN_CACHE_FILE_NAME);
}

std::wstring GetStatuslineCachePath()
{
    return BuildCachePath(GetPluginCacheDir(), STATUSLINE_CACHE_FILE_NAME);
}

std::wstring GetLegacyStatuslineCachePath()
{
    return BuildCachePath(GetLegacyPluginCacheDir(), STATUSLINE_CACHE_FILE_NAME);
}

bool FileExists(const std::wstring& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool EnsureDirectoryExists(const std::wstring& path)
{
    if (path.empty())
        return false;

    DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES)
        return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

    size_t start = 0;
    if (path.size() >= 2 && path[1] == L':')
        start = 3;
    else if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\')
    {
        const size_t first_sep = path.find(L'\\', 2);
        if (first_sep == std::wstring::npos)
            return false;
        const size_t second_sep = path.find(L'\\', first_sep + 1);
        if (second_sep == std::wstring::npos)
            return false;
        start = second_sep + 1;
    }

    for (size_t index = start; index <= path.size(); ++index)
    {
        if (index != path.size() && path[index] != L'\\' && path[index] != L'/')
            continue;

        const std::wstring segment = path.substr(0, index);
        if (segment.empty())
            continue;

        attributes = GetFileAttributesW(segment.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES)
        {
            if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                return false;
            continue;
        }

        if (!CreateDirectoryW(segment.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
            return false;
    }

    return true;
}

bool GetCurrentTimeMs(unsigned long long& current_time_ms)
{
    FILETIME file_time{};
    GetSystemTimeAsFileTime(&file_time);

    ULARGE_INTEGER value{};
    value.LowPart = file_time.dwLowDateTime;
    value.HighPart = file_time.dwHighDateTime;
    current_time_ms = value.QuadPart / 10000ULL;
    return true;
}

bool GetFileLastWriteTimeMs(const std::wstring& path, unsigned long long& last_write_time_ms)
{
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes))
        return false;

    ULARGE_INTEGER value{};
    value.LowPart = attributes.ftLastWriteTime.dwLowDateTime;
    value.HighPart = attributes.ftLastWriteTime.dwHighDateTime;
    last_write_time_ms = value.QuadPart / 10000ULL;
    return true;
}

bool IsFreshFile(const std::wstring& path, unsigned long long max_age_ms)
{
    if (path.empty() || !FileExists(path))
        return false;

    unsigned long long last_write_time_ms{};
    if (!GetFileLastWriteTimeMs(path, last_write_time_ms))
        return false;

    unsigned long long now_ms{};
    if (!GetCurrentTimeMs(now_ms))
        return false;

    return !(now_ms >= last_write_time_ms && now_ms - last_write_time_ms > max_age_ms);
}

bool HasFreshStatuslineCache()
{
    return IsFreshFile(GetStatuslineCachePath(), CACHE_MAX_AGE_MS) ||
        IsFreshFile(GetLegacyStatuslineCachePath(), CACHE_MAX_AGE_MS);
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

bool WriteUtf8File(const std::wstring& path, const std::wstring& content)
{
    const size_t separator_pos = path.find_last_of(L"\\/");
    if (separator_pos != std::wstring::npos)
    {
        if (!EnsureDirectoryExists(path.substr(0, separator_pos)))
            return false;
    }

    const int utf8_size = WideCharToMultiByte(CP_UTF8, 0, content.c_str(), static_cast<int>(content.size()), nullptr, 0, nullptr, nullptr);
    if (utf8_size < 0)
        return false;

    std::string utf8(static_cast<size_t>(utf8_size), '\0');
    if (utf8_size > 0)
    {
        WideCharToMultiByte(CP_UTF8, 0, content.c_str(), static_cast<int>(content.size()), &utf8[0], utf8_size, nullptr, nullptr);
    }

    FILE* file{};
    if (_wfopen_s(&file, path.c_str(), L"wb") != 0 || file == nullptr)
        return false;

    bool success = true;
    if (!utf8.empty())
        success = fwrite(utf8.data(), 1, utf8.size(), file) == utf8.size();

    fclose(file);
    return success;
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

bool QueryHeaderString(HINTERNET request, const wchar_t* header_name, std::wstring& value)
{
    value.clear();

    DWORD size{};
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, header_name, WINHTTP_NO_OUTPUT_BUFFER, &size, WINHTTP_NO_HEADER_INDEX))
    {
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            return false;
    }

    if (size == 0)
        return false;

    std::wstring buffer(size / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, header_name, buffer.empty() ? nullptr : &buffer[0], &size, WINHTTP_NO_HEADER_INDEX))
        return false;

    if (!buffer.empty() && buffer.back() == L'\0')
        buffer.pop_back();

    value = TrimString(buffer);
    return !value.empty();
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

bool TryParseRetryAfterMs(const std::wstring& raw_value, unsigned long long& retry_after_ms)
{
    retry_after_ms = 0;

    const std::wstring value = TrimString(raw_value);
    if (value.empty())
        return false;

    bool numeric = true;
    for (wchar_t ch : value)
    {
        if (ch < L'0' || ch > L'9')
        {
            numeric = false;
            break;
        }
    }

    if (numeric)
    {
        wchar_t* end_ptr{};
        const unsigned long long seconds = wcstoull(value.c_str(), &end_ptr, 10);
        if (end_ptr != value.c_str())
        {
            retry_after_ms = seconds * 1000ULL;
            return true;
        }
    }

    SYSTEMTIME retry_time{};
    if (!WinHttpTimeToSystemTime(value.c_str(), &retry_time))
        return false;

    FILETIME retry_file_time{};
    if (!SystemTimeToFileTime(&retry_time, &retry_file_time))
        return false;

    FILETIME now_file_time{};
    GetSystemTimeAsFileTime(&now_file_time);

    ULARGE_INTEGER retry_value{};
    retry_value.LowPart = retry_file_time.dwLowDateTime;
    retry_value.HighPart = retry_file_time.dwHighDateTime;

    ULARGE_INTEGER now_value{};
    now_value.LowPart = now_file_time.dwLowDateTime;
    now_value.HighPart = now_file_time.dwHighDateTime;

    if (retry_value.QuadPart <= now_value.QuadPart)
    {
        retry_after_ms = 0;
        return true;
    }

    retry_after_ms = (retry_value.QuadPart - now_value.QuadPart) / 10000ULL;
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

    if (TryGetJsonString(credentials_json, L"accessToken", access_token) && !access_token.empty())
        return true;

    std::wstring oauth_json;
    if (!TryGetJsonObject(credentials_json, L"claudeAiOauth", oauth_json))
        return false;

    return TryGetJsonString(oauth_json, L"accessToken", access_token) && !access_token.empty();
}

bool FetchUsageApiJson(const std::wstring& access_token, std::wstring& response_body, DWORD& status_code, unsigned long long& retry_after_ms)
{
    status_code = 0;
    retry_after_ms = 0;
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

        if (status_code == 429)
        {
            std::wstring retry_after_header;
            if (QueryHeaderString(request, L"Retry-After", retry_after_header))
            {
                unsigned long long parsed_retry_after_ms{};
                if (TryParseRetryAfterMs(retry_after_header, parsed_retry_after_ms))
                    retry_after_ms = parsed_retry_after_ms;
            }
        }

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

void ApplyResetAtValue(const std::wstring& reset_at, CClaudeUsageData::Metric& metric)
{
    if (reset_at.empty())
        return;

    FILETIME reset_file_time{};
    if (TryParseUtcIso8601(reset_at, reset_file_time))
    {
        metric.has_reset_time = FileTimeToUnixSeconds(reset_file_time, metric.reset_at_unix_seconds);
        if (!FileTimeToLocalText(reset_file_time, metric.reset_time_text))
            metric.reset_time_text = reset_at;
    }
    else
    {
        metric.reset_time_text = reset_at;
    }
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
        ApplyResetAtValue(resets_at, metric);

    return true;
}

bool LoadSnapshotFromCachedJson(const std::wstring& json, CClaudeUsageData::Snapshot& snapshot)
{
    const bool has_api_5h = LoadMetricFromApiSection(json, L"five_hour", snapshot.rolling_5h);
    const bool has_api_7d = LoadMetricFromApiSection(json, L"seven_day", snapshot.rolling_7d);
    return has_api_5h || has_api_7d;
}

bool TryLoadCachedUsageSnapshot(CClaudeUsageData::Snapshot& snapshot, bool require_fresh_cache)
{
    std::vector<UsageCacheCandidate> candidates;

    const std::wstring statusline_cache_path = GetStatuslineCachePath();
    if (!statusline_cache_path.empty() && FileExists(statusline_cache_path))
    {
        unsigned long long last_write_time_ms{};
        if (GetFileLastWriteTimeMs(statusline_cache_path, last_write_time_ms))
            candidates.push_back(UsageCacheCandidate{ statusline_cache_path, L"Claude Code statusline", 4, last_write_time_ms });
    }

    const std::wstring legacy_statusline_cache_path = GetLegacyStatuslineCachePath();
    if (!legacy_statusline_cache_path.empty() && FileExists(legacy_statusline_cache_path))
    {
        unsigned long long last_write_time_ms{};
        if (GetFileLastWriteTimeMs(legacy_statusline_cache_path, last_write_time_ms))
            candidates.push_back(UsageCacheCandidate{ legacy_statusline_cache_path, L"Claude Code statusline", 3, last_write_time_ms });
    }

    const std::wstring plugin_cache_path = GetPluginCachePath();
    if (!plugin_cache_path.empty() && FileExists(plugin_cache_path))
    {
        unsigned long long last_write_time_ms{};
        if (GetFileLastWriteTimeMs(plugin_cache_path, last_write_time_ms))
            candidates.push_back(UsageCacheCandidate{ plugin_cache_path, L"Claude usage cache", 2, last_write_time_ms });
    }

    const std::wstring legacy_plugin_cache_path = GetLegacyPluginCachePath();
    if (!legacy_plugin_cache_path.empty() && FileExists(legacy_plugin_cache_path))
    {
        unsigned long long last_write_time_ms{};
        if (GetFileLastWriteTimeMs(legacy_plugin_cache_path, last_write_time_ms))
            candidates.push_back(UsageCacheCandidate{ legacy_plugin_cache_path, L"Claude usage cache", 1, last_write_time_ms });
    }

    if (candidates.empty())
        return false;

    const unsigned long long now_ms = []() {
        unsigned long long value{};
        GetCurrentTimeMs(value);
        return value;
    }();

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const UsageCacheCandidate& left, const UsageCacheCandidate& right)
        {
            if (left.priority != right.priority)
                return left.priority > right.priority;
            return left.last_write_time_ms > right.last_write_time_ms;
        });

    for (const UsageCacheCandidate& candidate : candidates)
    {
        if (require_fresh_cache && now_ms >= candidate.last_write_time_ms && now_ms - candidate.last_write_time_ms > CACHE_MAX_AGE_MS)
            continue;

        std::wstring cached_json;
        if (!ReadUtf8File(candidate.path, cached_json))
            continue;

        CClaudeUsageData::Snapshot cached_snapshot;
        if (!LoadSnapshotFromCachedJson(cached_json, cached_snapshot))
            continue;

        snapshot.rolling_5h = cached_snapshot.rolling_5h;
        snapshot.rolling_7d = cached_snapshot.rolling_7d;
        snapshot.source_text = candidate.source_text;
        return true;
    }

    return false;
}

void WriteUsageCache(const std::wstring& response_json)
{
    const std::wstring cache_path = GetPluginCachePath();
    if (cache_path.empty())
        return;

    WriteUtf8File(cache_path, response_json);
}
}

CClaudeUsageData& CClaudeUsageData::Instance()
{
    static CClaudeUsageData instance;
    return instance;
}

void CClaudeUsageData::RefreshIfNeeded()
{
    const unsigned long long started_at = GetTickCount64();
    const bool has_fresh_statusline_cache = HasFreshStatuslineCache();
    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        if (m_refresh_in_progress)
            return;

        if (m_next_refresh_tick != 0 && started_at < m_next_refresh_tick && !has_fresh_statusline_cache)
            return;

        const unsigned long long refresh_interval_ms =
            (has_fresh_statusline_cache ? STATUSLINE_REFRESH_INTERVAL_MS : GetRefreshIntervalMs(m_last_refresh_succeeded));
        if (m_last_refresh_tick != 0 && started_at - m_last_refresh_tick < refresh_interval_ms)
            return;

        m_refresh_in_progress = true;
    }

    unsigned long long retry_after_ms{};
    const bool succeeded = Refresh(retry_after_ms);
    const unsigned long long completed_at = GetTickCount64();

    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        m_last_refresh_tick = completed_at;
        m_last_refresh_succeeded = succeeded;
        m_next_refresh_tick = (retry_after_ms == 0 ? 0 : completed_at + retry_after_ms);
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

bool CClaudeUsageData::Refresh(unsigned long long& retry_after_ms)
{
    Snapshot snapshot;
    const bool succeeded = LoadFromUsageApi(snapshot, retry_after_ms);
    FinalizeSnapshot(snapshot);

    std::lock_guard<std::mutex> lock(m_state_mutex);
    m_snapshot = snapshot;
    return succeeded;
}

bool CClaudeUsageData::LoadFromUsageApi(Snapshot& snapshot, unsigned long long& retry_after_ms)
{
    retry_after_ms = 0;

    if (TryLoadCachedUsageSnapshot(snapshot, true))
        return true;

    std::wstring access_token;
    if (!LoadAccessToken(access_token))
    {
        snapshot.error_text = L"Claude access token not found";
        if (TryLoadCachedUsageSnapshot(snapshot, false))
        {
            snapshot.error_text += L"; showing cached values";
            return true;
        }
        return false;
    }

    std::wstring response_json;
    DWORD status_code{};
    if (!FetchUsageApiJson(access_token, response_json, status_code, retry_after_ms))
    {
        snapshot.error_text = L"Claude usage API request failed";
        if (TryLoadCachedUsageSnapshot(snapshot, false))
        {
            snapshot.error_text += L"; showing cached values";
            return true;
        }
        return false;
    }

    if (status_code == HTTP_STATUS_DENIED || status_code == HTTP_STATUS_FORBIDDEN)
    {
        snapshot.error_text = L"Claude login required";
        if (TryLoadCachedUsageSnapshot(snapshot, false))
        {
            snapshot.error_text += L"; showing cached values";
            return true;
        }
        return false;
    }
    if (status_code == 429)
    {
        if (retry_after_ms == 0)
            retry_after_ms = RATE_LIMIT_RETRY_FALLBACK_MS;
        if (retry_after_ms > MAX_RETRY_AFTER_MS)
            retry_after_ms = MAX_RETRY_AFTER_MS;

        snapshot.error_text = L"Claude usage API rate limited";
        snapshot.error_text += L" (retry in ";
        snapshot.error_text += FormatDurationFromSeconds((retry_after_ms + 999ULL) / 1000ULL);
        snapshot.error_text += L")";
        if (TryLoadCachedUsageSnapshot(snapshot, false))
        {
            snapshot.error_text += L"; showing cached values";
            return true;
        }
        return false;
    }
    if (status_code != HTTP_STATUS_OK)
    {
        snapshot.error_text = L"Claude usage API HTTP ";
        snapshot.error_text += std::to_wstring(status_code);
        if (TryLoadCachedUsageSnapshot(snapshot, false))
        {
            snapshot.error_text += L"; showing cached values";
            return true;
        }
        return false;
    }

    const bool has_five_hour = LoadMetricFromApiSection(response_json, L"five_hour", snapshot.rolling_5h);
    const bool has_seven_day = LoadMetricFromApiSection(response_json, L"seven_day", snapshot.rolling_7d);
    if (!has_five_hour && !has_seven_day)
    {
        snapshot.error_text = L"Claude usage API returned unexpected data";
        if (TryLoadCachedUsageSnapshot(snapshot, false))
        {
            snapshot.error_text += L"; showing cached values";
            return true;
        }
        return false;
    }

    snapshot.source_text = L"Claude OAuth usage API";
    WriteUsageCache(response_json);
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
    if (!snapshot.error_text.empty())
    {
        snapshot.tooltip_text += L"\n";
        snapshot.tooltip_text += snapshot.error_text;
    }
}

bool CClaudeUsageData::HasAvailableMetric(const Snapshot& snapshot)
{
    return snapshot.rolling_5h.available || snapshot.rolling_7d.available;
}
