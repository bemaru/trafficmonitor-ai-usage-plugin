// Compile this standalone Windows SDK test without the application's MFC PCH.
#define PCH_H
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>

#define private public
#include "../src/ClaudeUsagePlugin/CodexUsageData.h"
#undef private
#include "../src/ClaudeUsagePlugin/CodexUsageData.cpp"

namespace
{
int checks{};

void Check(bool condition, const char* description)
{
    ++checks;
    if (!condition)
    {
        std::cerr << "FAILED: " << description << '\n';
        std::exit(1);
    }
}

std::string Iso(long long seconds)
{
    ULARGE_INTEGER raw{};
    raw.QuadPart = (static_cast<unsigned long long>(seconds) + 11644473600ULL) * 10000000ULL;
    FILETIME file_time{ raw.LowPart, raw.HighPart };
    SYSTEMTIME utc{};
    Check(FileTimeToSystemTime(&file_time, &utc) != FALSE, "fixture UTC conversion");
    char buffer[40];
    sprintf_s(buffer, "%04u-%02u-%02uT%02u:%02u:%02u.000Z", utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond);
    return buffer;
}

void Write(const std::filesystem::path& path, const std::string& text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
    Check(file.good(), "fixture file write");
}

std::string WebMetric(const std::string& percentage, long long reset_at)
{
    return "{\"utilization\":" + percentage + ",\"resets_at\":\"" + Iso(reset_at) + "\"}";
}

std::string WebSnapshot(long long updated_at, const std::string& five_hour, const std::string& seven_day)
{
    return "{\"schema_version\":1,\"source\":\"codex-web\",\"updated_at\":\"" + Iso(updated_at) +
        "\",\"five_hour\":" + five_hour + ",\"seven_day\":" + seven_day + "}";
}
}

int wmain(int argc, wchar_t** argv)
{
    Check(argc == 2, "isolated fixture directory is supplied");
    const std::filesystem::path root = std::filesystem::absolute(argv[1]);
    Check(!std::filesystem::exists(root), "fixture directory must be new");
    std::filesystem::create_directories(root);
    _wputenv_s(L"LOCALAPPDATA", (root / L"local").c_str());
    _wputenv_s(L"USERPROFILE", (root / L"home").c_str());
    _wputenv_s(L"CODEX_HOME", (root / L"codex").c_str());
    const std::filesystem::path web_path = GetCodexWebUsagePath();
    const std::filesystem::path session_path = root / L"codex" / L"sessions" / L"fixture.jsonl";
    const long long now = GetNowUnixSeconds();
    const std::string five = WebMetric("12.5", now + 3600);
    const std::string weekly = WebMetric("47", now + 86400);
    const auto load_web = [&](const std::string& json, CCodexUsageData::Snapshot& snapshot) {
        Write(web_path, json);
        snapshot = {};
        return CCodexUsageData::LoadFromWebHelper(snapshot);
    };
    CCodexUsageData::Snapshot snapshot;
    Check(load_web(WebSnapshot(now, five, weekly), snapshot), "fresh authenticated usage snapshot loads");
    Check(snapshot.rolling_5h.percentage == 12.5 && snapshot.rolling_7d.percentage == 47, "both percentages preserve used semantics");
    Check(snapshot.rolling_5h.reset_at_unix_seconds == now + 3600, "reset UTC timestamp is preserved");
    Check(snapshot.source_text == L"Codex web helper", "web source is named");
    _wputenv_s(L"USERPROFILE", L"");
    _wputenv_s(L"CODEX_HOME", L"");
    Check(CCodexUsageData::LoadFromStore(snapshot) && snapshot.rolling_5h.percentage == 12.5, "web usage works without a Codex home or session directory");
    _wputenv_s(L"USERPROFILE", (root / L"home").c_str());
    _wputenv_s(L"CODEX_HOME", (root / L"codex").c_str());

    const std::string session = "{\"timestamp\":\"" + Iso(now) + "\",\"type\":\"event_msg\",\"payload\":{\"type\":\"token_count\",\"rate_limits\":{\"primary\":{\"used_percent\":68,\"resets_at\":" +
        std::to_string(now + 7200) + "},\"secondary\":{\"remaining_percent\":20,\"resets_at\":" + std::to_string(now + 172800) + "}}}}\n";
    Write(session_path, session);
    Check(CCodexUsageData::LoadFromStore(snapshot) && snapshot.rolling_5h.percentage == 12.5, "fresh web data takes precedence over JSONL");
    Check(!load_web(WebSnapshot(now - 181, five, weekly), snapshot), "snapshot older than 180 seconds is rejected");
    Check(CCodexUsageData::LoadFromStore(snapshot) && snapshot.rolling_5h.percentage == 68 && snapshot.rolling_7d.percentage == 80, "stale web falls back to JSONL and remaining percentages convert");
    Check(!load_web(WebSnapshot(now + 31, five, weekly), snapshot), "future clock skew above 30 seconds is rejected");
    Check(load_web(WebSnapshot(now + 29, five, weekly), snapshot), "small clock skew is tolerated");
    Check(load_web(WebSnapshot(now, "null", weekly), snapshot) && !snapshot.rolling_5h.available && snapshot.rolling_7d.available, "missing five-hour window does not inherit stale data");
    Check(load_web(WebSnapshot(now, WebMetric("12", now - 1), weekly), snapshot) && !snapshot.rolling_5h.available, "elapsed reset invalidates only the affected window");
    Check(!load_web(WebSnapshot(now, "null", "null"), snapshot), "snapshot with no windows is rejected");
    Check(!load_web(WebSnapshot(now, WebMetric("101", now + 3600), "null"), snapshot), "out-of-range percentage is rejected");
    Check(!load_web(WebSnapshot(now, WebMetric("1e999", now + 3600), "null"), snapshot), "non-finite percentage is rejected");
    Check(!load_web(WebSnapshot(now, WebMetric("\"12\"", now + 3600), "null"), snapshot), "quoted percentage is rejected");
    Check(!load_web(WebSnapshot(now, WebMetric("12", now + 86400), "null"), snapshot), "reset beyond five-hour window is rejected");

    const std::string valid = WebSnapshot(now, five, weekly);
    for (const std::string& invalid : { valid.substr(0, valid.size() - 1), valid + "garbage", "{\"wrapped\":" + valid + "}",
        std::string("{\"schema_version\":2,") + valid.substr(1), std::string("{\"source\":\"other\",") + valid.substr(1),
        valid.substr(0, valid.size() - 1) + ",}", std::string("{\"schema_version\":01,") + valid.substr(1) })
        Check(!load_web(invalid, snapshot), "malformed, duplicated, or nested-spoofed snapshot is rejected");
    std::string invalid_time = valid;
    invalid_time.replace(invalid_time.find(".000Z"), 5, "+09:00");
    Check(!load_web(invalid_time, snapshot), "updated_at must explicitly be UTC");

    Write(web_path, valid);
    CCodexUsageData reader;
    Check(reader.Refresh(), "reader refresh loads web data");
    Check(reader.GetValueText(CodexUsageWindow::Rolling5Hours) == L"12.5%", "value text uses fresh web metric");
    reader.m_last_refresh_tick = GetTickCount64();
    reader.m_last_refresh_succeeded = true;
    std::filesystem::remove(web_path);
    Check(reader.GetValueText(CodexUsageWindow::Rolling5Hours) == L"--", "deleted helper cache immediately hides cached values");
    reader.RefreshIfNeeded();
    Check(reader.GetValueText(CodexUsageWindow::Rolling5Hours) == L"68%", "cache deletion immediately triggers JSONL fallback");
    reader.m_snapshot.rolling_5h.reset_at_unix_seconds = now - 1;
    Check(!reader.GetMetric(CodexUsageWindow::Rolling5Hours).available, "in-memory metric expires before the refresh interval");
    Write(web_path, valid);
    Check(reader.Refresh(), "web cache recovers after successful refresh");
    reader.m_snapshot.web_updated_at_unix_seconds = now - 181;
    Check(reader.GetValueText(CodexUsageWindow::Rolling5Hours) == L"--", "in-memory web cache obeys freshness TTL");

    std::filesystem::remove(web_path);
    std::string expired_session = session;
    const auto replace_reset = [&](const std::string& from) {
        expired_session.replace(expired_session.find(from), from.size(), std::to_string(now - 1));
    };
    replace_reset(std::to_string(now + 7200));
    replace_reset(std::to_string(now + 172800));
    Write(session_path, expired_session);
    snapshot = {};
    Check(!CCodexUsageData::LoadFromStore(snapshot), "expired JSONL windows cannot reappear as fallback");
    Write(web_path.parent_path() / L"codex-web-helper-status.json", "{\"state\":\"login_required\",\"updated_at\":\"" + Iso(now) + "\"}");
    CCodexUsageData::FinalizeSnapshot(snapshot);
    Check(snapshot.tooltip_text.find(L"needs login") != std::wstring::npos, "known helper failure is shown without raw response text");
    Write(session_path, "{\"timestamp\":\"" + Iso(now) + "\",\"type\":\"event_msg\",\"payload\":{\"type\":\"token_count\",\"rate_limits\":{\"primary\":{\"used_percent\":7}}}}\n");
    snapshot = {};
    Check(CCodexUsageData::LoadFromStore(snapshot) && snapshot.rolling_5h.percentage == 7, "recent JSONL without reset remains supported");
    Write(session_path, "{\"timestamp\":\"" + Iso(now - 901) + "\",\"type\":\"event_msg\",\"payload\":{\"type\":\"token_count\",\"rate_limits\":{\"primary\":{\"used_percent\":7}}}}\n");
    snapshot = {};
    Check(!CCodexUsageData::LoadFromStore(snapshot), "old JSONL without reset cannot remain available indefinitely");
    std::cout << "PASS: " << checks << " Codex web reader checks\n";
    return 0;
}
