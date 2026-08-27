#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "PluginInterface.h"

namespace
{
using GetPluginInstance = ITMPlugin* (*)();

struct TestCase
{
    const wchar_t* name;
    const char* rate_limits;
    const wchar_t* expected_5h;
    const wchar_t* expected_7d;
    const char* preceding_rate_limits = nullptr;
};

const TestCase TEST_CASES[] = {
    {
        L"weekly-primary",
        R"({"primary":{"used_percent":30,"window_minutes":10080,"resets_at":1893456000},"secondary":null})",
        L"--",
        L"30%",
    },
    {
        L"both-windows",
        R"({"primary":{"used_percent":12,"window_minutes":300,"resets_at":1893456000},"secondary":{"used_percent":34,"window_minutes":10080,"resets_at":1893456000}})",
        L"12%",
        L"34%",
    },
    {
        L"swapped-windows",
        R"({"primary":{"used_percent":34,"window_minutes":10080,"resets_at":1893456000},"secondary":{"used_percent":12,"window_minutes":300,"resets_at":1893456000}})",
        L"12%",
        L"34%",
    },
    {
        L"legacy-no-window",
        R"({"primary":{"used_percent":12,"resets_at":1893456000},"secondary":{"used_percent":34,"resets_at":1893456000}})",
        L"12%",
        L"34%",
    },
    {
        L"unknown-window",
        R"({"primary":{"used_percent":50,"window_minutes":1440,"resets_at":1893456000},"secondary":null})",
        L"--",
        L"--",
    },
    {
        L"five-hour-primary",
        R"({"primary":{"used_percent":12,"window_minutes":300,"resets_at":1893456000},"secondary":null})",
        L"12%",
        L"--",
    },
    {
        L"five-hour-secondary",
        R"({"primary":null,"secondary":{"used_percent":12,"window_minutes":300,"resets_at":1893456000}})",
        L"12%",
        L"--",
    },
    {
        L"both-windows-zero",
        R"({"primary":{"used_percent":0,"window_minutes":300,"resets_at":1893456000},"secondary":{"used_percent":0,"window_minutes":10080,"resets_at":1893456000}})",
        L"0%",
        L"0%",
    },
    {
        L"remaining-percent-swapped",
        R"({"primary":{"remaining_percent":66,"window_minutes":10080,"resets_at":1893456000},"secondary":{"remaining_percent":88,"window_minutes":300,"resets_at":1893456000}})",
        L"12%",
        L"34%",
    },
    {
        L"weekly-to-both",
        R"({"limit_id":"codex","limit_name":null,"primary":{"used_percent":12,"window_minutes":300,"resets_at":1893456000},"secondary":{"used_percent":34,"window_minutes":10080,"resets_at":1893456000},"credits":{"has_credits":false,"unlimited":false,"balance":"0"},"plan_type":"pro"})",
        L"12%",
        L"34%",
        R"({"primary":{"used_percent":30,"window_minutes":10080,"resets_at":1893456000},"secondary":null})",
    },
    {
        L"weekly-to-swapped",
        R"({"primary":{"used_percent":34,"window_minutes":10080,"resets_at":1893456000},"secondary":{"used_percent":12,"window_minutes":300,"resets_at":1893456000}})",
        L"12%",
        L"34%",
        R"({"primary":{"used_percent":30,"window_minutes":10080,"resets_at":1893456000},"secondary":null})",
    },
    {
        L"both-to-weekly",
        R"({"primary":{"used_percent":35,"window_minutes":10080,"resets_at":1893456000},"secondary":null})",
        L"--",
        L"35%",
        R"({"primary":{"used_percent":12,"window_minutes":300,"resets_at":1893456000},"secondary":{"used_percent":34,"window_minutes":10080,"resets_at":1893456000}})",
    },
};

const TestCase* FindTestCase(const std::wstring& name)
{
    for (const TestCase& test_case : TEST_CASES)
    {
        if (name == test_case.name)
            return &test_case;
    }
    return nullptr;
}

std::filesystem::path CreateFixture(const TestCase& test_case)
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        (L"trafficmonitor-ai-usage-plugin-test-" + std::to_wstring(GetCurrentProcessId()));
    const std::filesystem::path sessions = root / L"sessions" / L"2026" / L"07" / L"27";
    std::filesystem::create_directories(sessions);

    std::ofstream output(sessions / L"rollout-test.jsonl", std::ios::binary);
    if (test_case.preceding_rate_limits != nullptr)
    {
        output
            << R"({"timestamp":"2026-07-27T08:11:08Z","type":"event_msg","payload":{"type":"token_count","rate_limits":)"
            << test_case.preceding_rate_limits
            << "}}\n";
    }
    output
        << R"({"timestamp":"2026-07-27T08:12:08Z","type":"event_msg","payload":{"type":"token_count","rate_limits":)"
        << test_case.rate_limits
        << "}}\n";
    output.close();
    return root;
}

bool ExpectValue(IPluginItem* item, const wchar_t* expected, const wchar_t* label)
{
    if (item == nullptr)
    {
        std::wcerr << label << L" item was not available.\n";
        return false;
    }

    const std::wstring actual = item->GetItemValueText();
    if (actual == expected)
        return true;

    std::wcerr << label << L": expected \"" << expected << L"\", got \"" << actual << L"\".\n";
    return false;
}

bool ExpectTooltip(ITMPlugin* plugin, const TestCase& test_case)
{
    const std::wstring tooltip = plugin->GetTooltipInfo();
    const size_t codex_start = tooltip.find(L"Codex usage limits");
    if (codex_start == std::wstring::npos)
    {
        std::wcerr << L"Codex tooltip was not available.\n";
        return false;
    }

    const std::wstring codex_tooltip = tooltip.substr(codex_start);
    const auto expect_metric = [&](const wchar_t* label, const wchar_t* expected) {
        const std::wstring value = (std::wstring(expected) == L"--" ? L"unavailable" : expected);
        const std::wstring expected_line = std::wstring(L"\n") + label + L": " + value;
        if (codex_tooltip.find(expected_line) != std::wstring::npos)
            return true;

        std::wcerr << L"Codex tooltip missing " << label << L": " << value << L".\n";
        return false;
    };

    if (std::wstring(test_case.expected_5h) == L"--" && std::wstring(test_case.expected_7d) == L"--")
        return codex_tooltip.find(L"Codex usage limits unavailable") == 0;

    const bool five_hour_passed = expect_metric(L"5h", test_case.expected_5h);
    const bool seven_day_passed = expect_metric(L"7d", test_case.expected_7d);
    return five_hour_passed && seven_day_passed;
}
}

int wmain(int argc, wchar_t* argv[])
{
    if (argc != 3)
    {
        std::wcerr << L"Usage: CodexUsagePluginTests.exe <plugin-dll> <scenario>\n";
        return 2;
    }

    const TestCase* test_case = FindTestCase(argv[2]);
    if (test_case == nullptr)
    {
        std::wcerr << L"Unknown scenario: " << argv[2] << L"\n";
        return 2;
    }

    const std::filesystem::path fixture_root = CreateFixture(*test_case);
    if (!SetEnvironmentVariableW(L"CODEX_HOME", fixture_root.c_str()))
    {
        std::wcerr << L"Failed to set CODEX_HOME.\n";
        return 2;
    }

    HMODULE module = LoadLibraryW(argv[1]);
    if (module == nullptr)
    {
        std::wcerr << L"Failed to load plugin DLL: " << GetLastError() << L"\n";
        std::filesystem::remove_all(fixture_root);
        return 2;
    }

    const auto get_instance = reinterpret_cast<GetPluginInstance>(GetProcAddress(module, "TMPluginGetInstance"));
    if (get_instance == nullptr)
    {
        std::wcerr << L"TMPluginGetInstance export was not found.\n";
        std::filesystem::remove_all(fixture_root);
        return 2;
    }

    ITMPlugin* plugin = get_instance();
    plugin->DataRequired();

    const bool five_hour_passed = ExpectValue(plugin->GetItem(2), test_case->expected_5h, L"Codex 5h");
    const bool seven_day_passed = ExpectValue(plugin->GetItem(3), test_case->expected_7d, L"Codex 7d");
    const bool tooltip_passed = ExpectTooltip(plugin, *test_case);
    const bool passed = five_hour_passed && seven_day_passed && tooltip_passed;

    std::filesystem::remove_all(fixture_root);
    if (!passed)
        return 1;

    std::wcout << L"PASS " << test_case->name << L"\n";
    return 0;
}
