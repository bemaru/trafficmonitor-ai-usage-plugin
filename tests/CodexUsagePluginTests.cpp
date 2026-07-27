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

    const bool passed =
        ExpectValue(plugin->GetItem(2), test_case->expected_5h, L"Codex 5h") &&
        ExpectValue(plugin->GetItem(3), test_case->expected_7d, L"Codex 7d");

    std::filesystem::remove_all(fixture_root);
    if (!passed)
        return 1;

    std::wcout << L"PASS " << test_case->name << L"\n";
    return 0;
}
