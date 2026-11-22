#include "pch.h"
#include "CodexUsageData.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <climits>
#include <map>
#include <share.h>
#include <string>
#include <vector>

namespace
{
constexpr unsigned long long REFRESH_INTERVAL_MS = 60ULL * 1000ULL;
constexpr unsigned long long RETRY_INTERVAL_MS = 5ULL * 1000ULL;
constexpr unsigned long long MAX_TEXT_FILE_SIZE = 32ULL * 1024ULL * 1024ULL;
constexpr long long RECENT_EVENT_WINDOW_SECONDS = 15LL * 60LL;
constexpr long long WEB_CACHE_MAX_AGE_SECONDS = 180LL;
constexpr long long WEB_CACHE_CLOCK_SKEW_SECONDS = 30LL;
constexpr unsigned long long MAX_WEB_JSON_FILE_SIZE = 64ULL * 1024ULL;
constexpr wchar_t CODEX_SESSION_DIR_NAME[] = L"sessions";

struct SessionFileCandidate
{
    std::wstring path;
    FILETIME last_write_time{};
};

struct SessionSnapshotCandidate
{
    CCodexUsageData::Snapshot snapshot;
    std::string event_timestamp;
    bool has_event_unix_seconds{};
    long long event_unix_seconds{};
    FILETIME file_last_write_time{};
    std::wstring file_path;
};

struct MetricSelection
{
    bool found{};
    CCodexUsageData::Metric metric;
    std::string event_timestamp;
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

std::wstring GetCodexWebUsagePath()
{
    std::wstring cache_root = TrimString(GetEnvVar(L"LOCALAPPDATA"));
    if (cache_root.empty())
    {
        const std::wstring home = TrimString(GetEnvVar(L"USERPROFILE"));
        if (home.empty())
            return std::wstring();
        cache_root = JoinPath(home, L".cache");
    }
    return JoinPath(JoinPath(cache_root, L"trafficmonitor-claude-usage-plugin"), L"codex-web-usage.json");
}

long long GetNowUnixSeconds()
{
    FILETIME file_time{};
    GetSystemTimeAsFileTime(&file_time);
    ULARGE_INTEGER value{};
    value.LowPart = file_time.dwLowDateTime;
    value.HighPart = file_time.dwHighDateTime;
    return static_cast<long long>((value.QuadPart - 116444736000000000ULL) / 10000000ULL);
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

std::wstring GetDirectoryPath(const std::wstring& path)
{
    const size_t separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? std::wstring() : path.substr(0, separator);
}

std::wstring GetCurrentModulePath()
{
    HMODULE module_handle{};
    if (!GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&GetCurrentModulePath), &module_handle))
        return std::wstring();

    std::wstring module_path(MAX_PATH, L'\0');
    DWORD length = GetModuleFileNameW(module_handle, &module_path[0], static_cast<DWORD>(module_path.size()));
    while (length >= module_path.size() - 1)
    {
        module_path.resize(module_path.size() * 2);
        length = GetModuleFileNameW(module_handle, &module_path[0], static_cast<DWORD>(module_path.size()));
    }
    if (length == 0)
        return std::wstring();
    module_path.resize(length);
    return module_path;
}

bool HasInitializedHelperProfile()
{
    const std::wstring cache_dir = GetDirectoryPath(GetCodexWebUsagePath());
    if (cache_dir.empty())
        return false;
    const std::wstring profile_dir = JoinPath(cache_dir, L"codex-browser-profile");
    // Check only file existence, never cookie contents. Authentication belongs
    // to the helper; automatic startup must not create a profile or open login.
    return FileExists(JoinPath(profile_dir, L"Local State")) &&
        (FileExists(JoinPath(profile_dir, L"Default\\Network\\Cookies")) ||
            FileExists(JoinPath(profile_dir, L"Default\\Cookies")));
}

std::wstring FindBundledHelperScriptPath()
{
    const std::wstring module_dir = GetDirectoryPath(GetCurrentModulePath());
    if (module_dir.empty())
        return std::wstring();
    for (const wchar_t* relative_path : {
        L"ClaudeUsagePlugin\\codex-web-helper.ps1",
        L"codex-web-helper.ps1",
        L"..\\..\\..\\scripts\\codex-web-helper.ps1",
        L"..\\..\\..\\..\\scripts\\codex-web-helper.ps1" })
    {
        const std::wstring script_path = JoinPath(module_dir, relative_path);
        if (FileExists(script_path))
            return script_path;
    }
    return std::wstring();
}

bool LaunchBundledHelperStart(const std::wstring& script_path)
{
    if (script_path.empty() || !FileExists(script_path))
        return false;
    const std::wstring script_dir = GetDirectoryPath(script_path);
    if (script_dir.empty() || !DirectoryExists(script_dir))
        return false;
    std::wstring powershell_path = JoinPath(TrimString(GetEnvVar(L"SystemRoot")), L"System32\\WindowsPowerShell\\v1.0\\powershell.exe");
    if (!FileExists(powershell_path))
        powershell_path = L"powershell.exe";

    std::wstring command_line = L"\"" + powershell_path +
        L"\" -NoProfile -NonInteractive -WindowStyle Hidden -ExecutionPolicy Bypass -File \"" + script_path + L"\" start";
    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    startup_info.dwFlags = STARTF_USESHOWWINDOW;
    startup_info.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process_info{};
    command_line.push_back(L'\0');
    const BOOL created = CreateProcessW(
        powershell_path.c_str(), &command_line[0], nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, script_dir.c_str(), &startup_info, &process_info);
    if (!created)
        return false;
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    return true;
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

bool TryGetJsonString(const std::string& json, const char* key, std::string& value)
{
    size_t value_pos{};
    if (!FindJsonKey(json, key, value_pos))
        return false;

    while (value_pos < json.size() && isspace(static_cast<unsigned char>(json[value_pos])))
        ++value_pos;

    if (value_pos >= json.size() || json[value_pos] != '"')
        return false;

    ++value_pos;
    std::string parsed;
    bool escape = false;
    for (size_t index = value_pos; index < json.size(); ++index)
    {
        const char ch = json[index];
        if (escape)
        {
            parsed.push_back(ch);
            escape = false;
            continue;
        }

        if (ch == '\\')
        {
            escape = true;
            continue;
        }

        if (ch == '"')
        {
            value = parsed;
            return true;
        }

        parsed.push_back(ch);
    }

    return false;
}

bool TryParseIso8601UtcSeconds(const std::string& timestamp, long long& unix_seconds)
{
    int year{};
    int month{};
    int day{};
    int hour{};
    int minute{};
    int second{};
    if (sscanf_s(timestamp.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &year, &month, &day, &hour, &minute, &second) != 6)
        return false;

    SYSTEMTIME utc_time{};
    utc_time.wYear = static_cast<WORD>(year);
    utc_time.wMonth = static_cast<WORD>(month);
    utc_time.wDay = static_cast<WORD>(day);
    utc_time.wHour = static_cast<WORD>(hour);
    utc_time.wMinute = static_cast<WORD>(minute);
    utc_time.wSecond = static_cast<WORD>(second);

    FILETIME file_time{};
    if (!SystemTimeToFileTime(&utc_time, &file_time))
        return false;

    ULARGE_INTEGER value{};
    value.LowPart = file_time.dwLowDateTime;
    value.HighPart = file_time.dwHighDateTime;
    if (value.QuadPart < 116444736000000000ULL)
        return false;

    unix_seconds = static_cast<long long>((value.QuadPart - 116444736000000000ULL) / 10000000ULL);
    return true;
}

bool TryParseWebUtcSeconds(const std::string& timestamp, long long& unix_seconds)
{
    // The helper writes Date.toISOString(): require an explicit UTC suffix.
    if (timestamp.size() < 20 || timestamp.back() != 'Z')
        return false;
    for (size_t index = 0; index < 19; ++index)
    {
        const char expected = (index == 4 || index == 7 ? '-' :
            (index == 10 ? 'T' : (index == 13 || index == 16 ? ':' : '\0')));
        if (expected != '\0' ? timestamp[index] != expected :
            timestamp[index] < '0' || timestamp[index] > '9')
            return false;
    }
    if (timestamp.size() != 20)
    {
        if (timestamp.size() < 22 || timestamp[19] != '.')
            return false;
        for (size_t index = 20; index + 1 < timestamp.size(); ++index)
            if (timestamp[index] < '0' || timestamp[index] > '9')
                return false;
    }
    return TryParseIso8601UtcSeconds(timestamp, unix_seconds);
}

using WebJsonObject = std::map<std::string, std::string>;

void SkipJsonWhitespace(const std::string& text, size_t& position)
{
    while (position < text.size() &&
        (text[position] == ' ' || text[position] == '\t' || text[position] == '\r' || text[position] == '\n'))
        ++position;
}

bool SkipJsonString(const std::string& text, size_t& position)
{
    if (position >= text.size() || text[position++] != '"')
        return false;
    while (position < text.size())
    {
        const unsigned char ch = static_cast<unsigned char>(text[position++]);
        if (ch == '"')
            return true;
        if (ch < 0x20)
            return false;
        if (ch == '\\')
        {
            if (position >= text.size())
                return false;
            const char escaped = text[position++];
            if (escaped == 'u')
            {
                for (int index = 0; index < 4; ++index)
                    if (position >= text.size() || !isxdigit(static_cast<unsigned char>(text[position++])))
                        return false;
            }
            else if (strchr("\"\\/bfnrt", escaped) == nullptr)
                return false;
        }
    }
    return false;
}

bool ParseWebJsonValue(const std::string& text, size_t& position, int depth, WebJsonObject* members)
{
    SkipJsonWhitespace(text, position);
    if (position >= text.size() || depth > 8)
        return false;
    if (text[position] == '{')
    {
        ++position;
        SkipJsonWhitespace(text, position);
        WebJsonObject parsed;
        if (position < text.size() && text[position] != '}')
        {
            while (true)
            {
                const size_t key_start = position;
                if (!SkipJsonString(text, position))
                    return false;
                const std::string key = text.substr(key_start, position - key_start);
                SkipJsonWhitespace(text, position);
                if (position >= text.size() || text[position++] != ':')
                    return false;
                SkipJsonWhitespace(text, position);
                const size_t value_start = position;
                if (!ParseWebJsonValue(text, position, depth + 1, nullptr) ||
                    !parsed.emplace(key, text.substr(value_start, position - value_start)).second)
                    return false;
                SkipJsonWhitespace(text, position);
                if (position >= text.size())
                    return false;
                if (text[position] != ',')
                    break;
                ++position;
                SkipJsonWhitespace(text, position);
            }
        }
        if (position >= text.size() || text[position++] != '}')
            return false;
        if (members)
            *members = std::move(parsed);
        return true;
    }
    if (members)
        return false;
    if (text[position] == '"')
        return SkipJsonString(text, position);
    for (const char* literal : { "null", "true", "false" })
    {
        const size_t length = strlen(literal);
        if (text.compare(position, length, literal) == 0)
        {
            position += length;
            return true;
        }
    }
    // Snapshots only contain objects and scalars; reject malformed numbers.
    if (text[position] == '-')
        ++position;
    if (position >= text.size() || !isdigit(static_cast<unsigned char>(text[position])))
        return false;
    if (text[position] == '0')
        ++position;
    else
        while (position < text.size() && isdigit(static_cast<unsigned char>(text[position])))
            ++position;
    if (position < text.size() && text[position] == '.')
    {
        const size_t fraction = ++position;
        while (position < text.size() && isdigit(static_cast<unsigned char>(text[position])))
            ++position;
        if (position == fraction)
            return false;
    }
    if (position < text.size() && (text[position] == 'e' || text[position] == 'E'))
    {
        ++position;
        if (position < text.size() && (text[position] == '+' || text[position] == '-'))
            ++position;
        const size_t exponent = position;
        while (position < text.size() && isdigit(static_cast<unsigned char>(text[position])))
            ++position;
        if (position == exponent)
            return false;
    }
    return true;
}

bool ParseWebJsonObject(const std::string& text, WebJsonObject& members)
{
    size_t position{};
    if (!ParseWebJsonValue(text, position, 0, &members))
        return false;
    SkipJsonWhitespace(text, position);
    return position == text.size();
}

bool ReadWebJsonFile(const std::wstring& path, std::string& json)
{
    unsigned long long file_size{};
    if (!GetFileSizeBytes(path, file_size) || file_size == 0 || file_size > MAX_WEB_JSON_FILE_SIZE)
        return false;
    FILE* file = _wfsopen(path.c_str(), L"rb", _SH_DENYNO);
    if (file == nullptr)
        return false;
    json.resize(static_cast<size_t>(file_size));
    const size_t read_size = fread(&json[0], 1, json.size(), file);
    const bool succeeded = read_size == json.size() && ferror(file) == 0;
    fclose(file);
    return succeeded;
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

bool TryGetWebString(const WebJsonObject& members, const char* key, std::string& value)
{
    const auto member = members.find(std::string("\"") + key + "\"");
    if (member == members.end())
        return false;
    const std::string& raw = member->second;
    if (raw.size() < 2 || raw.front() != '"' || raw.back() != '"' || raw.find('\\') != std::string::npos)
        return false;
    value = raw.substr(1, raw.size() - 2);
    return true;
}

bool LoadWebMetric(const WebJsonObject& root, const char* key, long long window_seconds, CCodexUsageData::Metric& metric)
{
    const auto member = root.find(std::string("\"") + key + "\"");
    WebJsonObject fields;
    if (member == root.end() || !ParseWebJsonObject(member->second, fields))
        return false;
    const auto utilization = fields.find("\"utilization\"");
    if (utilization == fields.end())
        return false;
    char* end{};
    const std::string& raw = utilization->second;
    const double percentage = strtod(raw.c_str(), &end);
    if (end == raw.c_str() || *end != '\0' || !std::isfinite(percentage) || percentage < 0.0 || percentage > 100.0)
        return false;
    std::string reset_text;
    long long reset_at{};
    const long long now = GetNowUnixSeconds();
    if (!TryGetWebString(fields, "resets_at", reset_text) || !TryParseWebUtcSeconds(reset_text, reset_at) ||
        reset_at <= now || reset_at > now + window_seconds + 60LL)
        return false;
    metric.available = true;
    metric.percentage = percentage;
    metric.has_reset_time = true;
    metric.reset_at_unix_seconds = reset_at;
    UnixSecondsToLocalText(reset_at, metric.reset_time_text);
    return true;
}

std::wstring GetWebHelperStatusSummary()
{
    const std::wstring usage_path = GetCodexWebUsagePath();
    const size_t separator = usage_path.find_last_of(L"\\/");
    if (separator == std::wstring::npos)
        return std::wstring();
    std::string json;
    WebJsonObject fields;
    std::string state;
    if (!ReadWebJsonFile(usage_path.substr(0, separator + 1) + L"codex-web-helper-status.json", json) ||
        !ParseWebJsonObject(json, fields) || !TryGetWebString(fields, "state", state))
        return std::wstring();
    if (state == "login_required")
        return L"Codex web helper needs login";
    if (state == "access_denied" || state == "account_unavailable")
        return L"Codex web helper usage access was denied";
    if (state == "rate_limited")
        return L"Codex web helper hit a rate limit";
    if (state == "cloudflare_blocked")
        return L"Codex web helper is blocked by Cloudflare";
    if (state == "login_browser_opened")
        return L"Codex web helper login window is open";
    if (state == "stopped")
        return L"Codex web helper is stopped";
    if (state == "ok")
        return L"Codex web helper snapshot is missing, invalid, or expired";
    if (state == "http_error" || state == "unexpected_response" || state == "request_timeout" ||
        state == "request_failed" || state == "usage_unavailable")
        return L"Codex web helper could not refresh usage";
    if (state == "browser_not_found" || state == "browser_start_failed" || state == "browser_closed" ||
        state == "browser_protocol_failed" || state == "browser_timeout" || state == "browser_evaluation_failed" ||
        state == "navigation_failed" || state == "profile_in_use" || state == "process_check_failed" || state == "storage_failed")
        return L"Codex web helper could not access its browser profile or local files";
    return std::wstring();
}

bool IsWebSnapshotCurrent(const CCodexUsageData::Snapshot& snapshot)
{
    if (snapshot.web_updated_at_unix_seconds == 0)
        return true;
    const long long age = GetNowUnixSeconds() - snapshot.web_updated_at_unix_seconds;
    return age >= -WEB_CACHE_CLOCK_SKEW_SECONDS && age <= WEB_CACHE_MAX_AGE_SECONDS &&
        FileExists(GetCodexWebUsagePath());
}

bool HasExpiredMetric(const CCodexUsageData::Snapshot& snapshot)
{
    const long long now = GetNowUnixSeconds();
    return (snapshot.rolling_5h.available && snapshot.rolling_5h.has_reset_time && snapshot.rolling_5h.reset_at_unix_seconds <= now) ||
        (snapshot.rolling_7d.available && snapshot.rolling_7d.has_reset_time && snapshot.rolling_7d.reset_at_unix_seconds <= now);
}

void RemoveExpiredMetrics(CCodexUsageData::Snapshot& snapshot)
{
    if (!IsWebSnapshotCurrent(snapshot))
    {
        snapshot.rolling_5h = {};
        snapshot.rolling_7d = {};
        snapshot.error_text = L"Codex web helper snapshot unavailable or expired";
        return;
    }
    const long long now = GetNowUnixSeconds();
    for (CCodexUsageData::Metric* metric : { &snapshot.rolling_5h, &snapshot.rolling_7d })
        if (metric->available && metric->has_reset_time && metric->reset_at_unix_seconds <= now)
            *metric = {};
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

    if (!std::isfinite(metric.percentage) || metric.percentage < 0.0 || metric.percentage > 100.0)
        return false;

    long long reset_at{};
    if (TryGetJsonInt64(metric_json, "reset_at", reset_at) || TryGetJsonInt64(metric_json, "resets_at", reset_at))
    {
        if (reset_at <= GetNowUnixSeconds())
            return false;
        metric.has_reset_time = true;
        metric.reset_at_unix_seconds = reset_at;
        std::wstring reset_text;
        if (UnixSecondsToLocalText(reset_at, reset_text))
            metric.reset_time_text = reset_text;
        else
            metric.reset_time_text = std::to_wstring(reset_at);
    }

    metric.available = true;
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

bool IsCodexRateLimitEventLine(const std::string& line)
{
    return
        line.find("\"type\":\"event_msg\"") != std::string::npos &&
        line.find("\"payload\":{\"type\":\"token_count\"") != std::string::npos &&
        line.find("\"rate_limits\"") != std::string::npos;
}

bool IsTimestampNewer(const std::string& left, const std::string& right)
{
    if (left.empty())
        return false;
    if (right.empty())
        return true;
    return left > right;
}

bool IsMetricBetterForSelection(const CCodexUsageData::Metric& metric, const std::string& event_timestamp, const MetricSelection& current)
{
    if (!metric.available)
        return false;

    const long long now = GetNowUnixSeconds();
    if (metric.has_reset_time)
    {
        if (metric.reset_at_unix_seconds <= now)
            return false;
    }
    else
    {
        long long event_at{};
        if (!TryParseIso8601UtcSeconds(event_timestamp, event_at) ||
            event_at > now + WEB_CACHE_CLOCK_SKEW_SECONDS || now - event_at > RECENT_EVENT_WINDOW_SECONDS)
            return false;
    }

    if (!current.found)
        return true;

    if (metric.has_reset_time && current.metric.has_reset_time)
    {
        if (metric.reset_at_unix_seconds != current.metric.reset_at_unix_seconds)
            return metric.reset_at_unix_seconds > current.metric.reset_at_unix_seconds;
    }
    else if (metric.has_reset_time != current.metric.has_reset_time)
    {
        return metric.has_reset_time;
    }

    return IsTimestampNewer(event_timestamp, current.event_timestamp);
}

void ConsiderMetricForSelection(const CCodexUsageData::Metric& metric, const std::string& event_timestamp, MetricSelection& selection)
{
    if (!IsMetricBetterForSelection(metric, event_timestamp, selection))
        return;

    selection.found = true;
    selection.metric = metric;
    selection.event_timestamp = event_timestamp;
}

void SelectMetricsFromCandidates(
    const std::vector<SessionSnapshotCandidate>& candidates,
    bool recent_only,
    long long newest_event_unix_seconds,
    MetricSelection& five_hour,
    MetricSelection& seven_day)
{
    for (const SessionSnapshotCandidate& candidate : candidates)
    {
        if (recent_only)
        {
            if (!candidate.has_event_unix_seconds)
                continue;
            if (newest_event_unix_seconds - candidate.event_unix_seconds > RECENT_EVENT_WINDOW_SECONDS)
                continue;
        }

        ConsiderMetricForSelection(candidate.snapshot.rolling_5h, candidate.event_timestamp, five_hour);
        ConsiderMetricForSelection(candidate.snapshot.rolling_7d, candidate.event_timestamp, seven_day);
    }
}

void SelectMissingMetricsFromCandidates(
    const std::vector<SessionSnapshotCandidate>& candidates,
    MetricSelection& five_hour,
    MetricSelection& seven_day)
{
    MetricSelection fallback_five_hour;
    MetricSelection fallback_seven_day;
    for (const SessionSnapshotCandidate& candidate : candidates)
    {
        ConsiderMetricForSelection(candidate.snapshot.rolling_5h, candidate.event_timestamp, fallback_five_hour);
        ConsiderMetricForSelection(candidate.snapshot.rolling_7d, candidate.event_timestamp, fallback_seven_day);
    }

    if (!five_hour.found && fallback_five_hour.found)
        five_hour = fallback_five_hour;
    if (!seven_day.found && fallback_seven_day.found)
        seven_day = fallback_seven_day;
}

bool LoadSessionJsonlFile(const std::wstring& file_path, const FILETIME& last_write_time, SessionSnapshotCandidate& result)
{
    unsigned long long file_size{};
    if (!GetFileSizeBytes(file_path, file_size) || file_size > MAX_TEXT_FILE_SIZE)
        return false;

    FILE* file{};
    file = _wfsopen(file_path.c_str(), L"rb", _SH_DENYNO);
    if (file == nullptr)
        return false;

    bool found = false;
    CCodexUsageData::Snapshot current;
    std::string latest_event_timestamp;
    bool latest_has_event_unix_seconds = false;
    long long latest_event_unix_seconds = 0;
    std::string line;
    while (ReadUtf8Line(file, line))
    {
        if (!IsCodexRateLimitEventLine(line))
            continue;

        CCodexUsageData::Snapshot candidate;
        if (LoadFromRateLimitsJson(line, candidate))
        {
            std::string candidate_timestamp;
            const bool has_timestamp = TryGetJsonString(line, "timestamp", candidate_timestamp);
            long long candidate_event_unix_seconds{};
            const bool has_event_unix_seconds =
                has_timestamp && TryParseIso8601UtcSeconds(candidate_timestamp, candidate_event_unix_seconds);
            const bool take_candidate =
                !found ||
                (has_timestamp && IsTimestampNewer(candidate_timestamp, latest_event_timestamp)) ||
                (!has_timestamp && latest_event_timestamp.empty());

            if (take_candidate)
            {
                current = candidate;
                if (has_timestamp)
                    latest_event_timestamp = candidate_timestamp;
                latest_has_event_unix_seconds = has_event_unix_seconds;
                latest_event_unix_seconds = candidate_event_unix_seconds;
                found = true;
            }
        }
    }

    fclose(file);

    if (!found)
        return false;

    result.snapshot.rolling_5h = current.rolling_5h;
    result.snapshot.rolling_7d = current.rolling_7d;
    result.snapshot.source_text = L"sessions JSONL";
    result.event_timestamp = latest_event_timestamp;
    result.has_event_unix_seconds = latest_has_event_unix_seconds;
    result.event_unix_seconds = latest_event_unix_seconds;
    result.file_last_write_time = last_write_time;
    result.file_path = file_path;
    return true;
}

} // namespace

CCodexUsageData& CCodexUsageData::Instance()
{
    static CCodexUsageData instance;
    return instance;
}

void CCodexUsageData::AutoStartBundledHelperIfNeeded()
{
    std::lock_guard<std::mutex> lock(m_state_mutex);
    if (m_helper_auto_start_attempted)
        return;
    m_helper_auto_start_attempted = true;
    if (!HasInitializedHelperProfile())
        return;
    // The wrapper validates the watch process and the helper owns an OS lock;
    // do not duplicate its process identity or concurrency checks here.
    const std::wstring script_path = FindBundledHelperScriptPath();
    if (!script_path.empty())
        LaunchBundledHelperStart(script_path);
}

void CCodexUsageData::RefreshIfNeeded()
{
    const unsigned long long now = GetTickCount64();
    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        if (m_refresh_in_progress)
            return;

        const bool snapshot_expired = !IsWebSnapshotCurrent(m_snapshot) || HasExpiredMetric(m_snapshot);
        const unsigned long long refresh_interval_ms = GetRefreshIntervalMs(m_last_refresh_succeeded);
        if (!snapshot_expired && m_last_refresh_tick != 0 && now - m_last_refresh_tick < refresh_interval_ms)
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

    const Metric metric = GetMetric(window);
    value_text = metric.available ? FormatPercentage(metric.percentage) : L"--";
    return value_text;
}

const CCodexUsageData::Metric& CCodexUsageData::GetMetric(CodexUsageWindow window) const
{
    thread_local Metric metric;

    std::lock_guard<std::mutex> lock(m_state_mutex);
    metric = (window == CodexUsageWindow::Rolling5Hours ? m_snapshot.rolling_5h : m_snapshot.rolling_7d);
    if (!IsWebSnapshotCurrent(m_snapshot) ||
        (metric.available && metric.has_reset_time && metric.reset_at_unix_seconds <= GetNowUnixSeconds()))
        metric = {};
    return metric;
}

const std::wstring& CCodexUsageData::GetTooltipText() const
{
    thread_local std::wstring tooltip_text;

    Snapshot snapshot;
    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        snapshot = m_snapshot;
    }
    FinalizeSnapshot(snapshot);
    tooltip_text = snapshot.tooltip_text;
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

bool CCodexUsageData::LoadFromWebHelper(Snapshot& snapshot)
{
    std::string json;
    WebJsonObject root;
    if (!ReadWebJsonFile(GetCodexWebUsagePath(), json) || !ParseWebJsonObject(json, root))
        return false;
    const auto schema = root.find("\"schema_version\"");
    const auto source = root.find("\"source\"");
    if (schema == root.end() || schema->second != "1" ||
        source == root.end() || source->second != "\"codex-web\"")
        return false;
    std::string updated_at;
    long long updated_seconds{};
    if (!TryGetWebString(root, "updated_at", updated_at) || !TryParseWebUtcSeconds(updated_at, updated_seconds))
        return false;
    const long long age = GetNowUnixSeconds() - updated_seconds;
    if (age < -WEB_CACHE_CLOCK_SKEW_SECONDS || age > WEB_CACHE_MAX_AGE_SECONDS)
        return false;

    Snapshot candidate;
    LoadWebMetric(root, "five_hour", 5LL * 60LL * 60LL, candidate.rolling_5h);
    LoadWebMetric(root, "seven_day", 7LL * 24LL * 60LL * 60LL, candidate.rolling_7d);
    if (!HasAvailableMetric(candidate))
        return false;
    candidate.source_text = L"Codex web helper";
    candidate.web_updated_at_unix_seconds = updated_seconds;
    snapshot = candidate;
    return true;
}

bool CCodexUsageData::LoadFromStore(Snapshot& snapshot)
{
    Snapshot web_snapshot;
    if (LoadFromWebHelper(web_snapshot))
    {
        snapshot = web_snapshot;
        return true;
    }

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

    std::vector<SessionSnapshotCandidate> parsed_candidates;
    bool has_newest_event_unix_seconds = false;
    long long newest_event_unix_seconds = LLONG_MIN;
    for (const SessionFileCandidate& candidate : candidates)
    {
        SessionSnapshotCandidate parsed;
        if (!LoadSessionJsonlFile(candidate.path, candidate.last_write_time, parsed))
            continue;

        if (parsed.has_event_unix_seconds)
        {
            if (!has_newest_event_unix_seconds || parsed.event_unix_seconds > newest_event_unix_seconds)
            {
                newest_event_unix_seconds = parsed.event_unix_seconds;
                has_newest_event_unix_seconds = true;
            }
        }

        parsed_candidates.push_back(parsed);
    }

    if (!parsed_candidates.empty())
    {
        MetricSelection five_hour;
        MetricSelection seven_day;

        // Multiple active sessions can write conflicting rate-limit windows.
        // Within recent events, prefer the newest reset window and use the event
        // timestamp only as a tie-breaker so stale windows do not flicker back.
        if (has_newest_event_unix_seconds)
            SelectMetricsFromCandidates(parsed_candidates, true, newest_event_unix_seconds, five_hour, seven_day);

        if (!five_hour.found || !seven_day.found)
            SelectMissingMetricsFromCandidates(parsed_candidates, five_hour, seven_day);

        if (five_hour.found)
            snapshot.rolling_5h = five_hour.metric;
        if (seven_day.found)
            snapshot.rolling_7d = seven_day.metric;

        snapshot.source_text = L"sessions JSONL";
        if (HasAvailableMetric(snapshot))
            return true;
    }

    snapshot.error_text = L"Codex sessions JSONL returned no rate limits";
    return false;
}

void CCodexUsageData::FinalizeSnapshot(Snapshot& snapshot)
{
    RemoveExpiredMetrics(snapshot);
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
        const std::wstring helper_status = GetWebHelperStatusSummary();
        if (!helper_status.empty())
            snapshot.tooltip_text += L"\n" + helper_status;
        return;
    }

    snapshot.tooltip_text = L"Codex usage limits";
    snapshot.tooltip_text += L"\n";
    snapshot.tooltip_text += BuildMetricTooltip(L"5h", snapshot.rolling_5h);
    snapshot.tooltip_text += L"\n";
    snapshot.tooltip_text += BuildMetricTooltip(L"7d", snapshot.rolling_7d);
    if (!snapshot.source_text.empty())
        snapshot.tooltip_text += L"\nSource: " + snapshot.source_text;
}

bool CCodexUsageData::HasAvailableMetric(const Snapshot& snapshot)
{
    return snapshot.rolling_5h.available || snapshot.rolling_7d.available;
}
