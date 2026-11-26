// Exercise the real Codex startup logic with filesystem fixtures. Capture the
// Win32 process boundary so this test never starts Chrome or a real helper.
#define PCH_H
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>

namespace
{
int checks{};
int launch_count{};
bool launch_succeeds = true;
bool module_lookup_succeeds = true;
std::wstring module_file_path;
struct LaunchCapture
{
    std::wstring application;
    std::wstring command_line;
    std::wstring working_directory;
    DWORD creation_flags{};
    DWORD startup_flags{};
    WORD show_window{};
    BOOL inherit_handles{};
} launched;

void Check(bool condition, const char* description)
{
    ++checks;
    if (!condition)
    {
        std::cerr << "FAILED: " << description << '\n';
        std::exit(1);
    }
}

BOOL WINAPI CaptureGetModuleHandleExW(DWORD flags, LPCWSTR address, HMODULE* module)
{
    Check((flags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS) != 0 &&
        (flags & GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT) != 0 && address != nullptr,
        "module lookup uses the containing DLL without changing its reference count");
    *module = GetModuleHandleW(nullptr);
    return module_lookup_succeeds ? TRUE : FALSE;
}

DWORD WINAPI CaptureGetModuleFileNameW(HMODULE, LPWSTR buffer, DWORD size)
{
    const size_t copied = (std::min)(module_file_path.size(), static_cast<size_t>(size - 1));
    std::copy_n(module_file_path.c_str(), copied, buffer);
    buffer[copied] = L'\0';
    return static_cast<DWORD>(copied);
}

BOOL WINAPI CaptureCreateProcessW(LPCWSTR application, LPWSTR command_line,
    LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL inherit_handles, DWORD creation_flags,
    LPVOID environment, LPCWSTR working_directory, LPSTARTUPINFOW startup, LPPROCESS_INFORMATION process)
{
    ++launch_count;
    launched = { application ? application : L"", command_line, working_directory,
        creation_flags, startup->dwFlags, startup->wShowWindow, inherit_handles };
    Check(environment == nullptr, "helper inherits the current user environment");
    if (!launch_succeeds)
        return FALSE;
    // Real handles make the production CloseHandle cleanup observable to the
    // normal Windows API without retaining any child process.
    process->hProcess = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    process->hThread = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Check(process->hProcess != nullptr && process->hThread != nullptr, "test process handles are created");
    return TRUE;
}
}

#define private public
#include "../src/ClaudeUsagePlugin/CodexUsageData.h"
#undef private
#define GetModuleHandleExW CaptureGetModuleHandleExW
#define GetModuleFileNameW CaptureGetModuleFileNameW
#define CreateProcessW CaptureCreateProcessW
#include "../src/ClaudeUsagePlugin/CodexUsageData.cpp"
#undef GetModuleHandleExW
#undef GetModuleFileNameW
#undef CreateProcessW

namespace
{
void Write(const std::filesystem::path& path)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << "public fixture";
    Check(file.good(), "public fixture file is created");
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
    const std::filesystem::path profile = root / L"local" / L"trafficmonitor-claude-usage-plugin" / L"codex-browser-profile";
    const std::filesystem::path module_dir = root / L"plugin assets" / L"plugins";
    module_file_path = (module_dir / L"ClaudeUsagePlugin.dll").wstring();
    const std::filesystem::path bundled_script = module_dir / L"ClaudeUsagePlugin" / L"codex-web-helper.ps1";
    Write(bundled_script);

    CCodexUsageData not_configured;
    not_configured.AutoStartBundledHelperIfNeeded();
    Check(launch_count == 0 && !std::filesystem::exists(profile), "missing profile neither launches a process nor creates browser data");
    Write(profile / L"Local State");
    CCodexUsageData partial_profile;
    partial_profile.AutoStartBundledHelperIfNeeded();
    Check(launch_count == 0, "profile without a cookie store is not auto-started");
    Write(profile / L"Default" / L"Network" / L"Cookies");
    not_configured.AutoStartBundledHelperIfNeeded();
    Check(launch_count == 0, "a skipped startup is attempted only once per plugin lifetime");

    CCodexUsageData configured;
    configured.AutoStartBundledHelperIfNeeded();
    Check(launch_count == 1, "initialized profile launches the helper on plugin initialization");
    const std::wstring expected_powershell = (std::filesystem::path(GetEnvVar(L"SystemRoot")) / L"System32" /
        L"WindowsPowerShell" / L"v1.0" / L"powershell.exe").wstring();
    Check(launched.application == expected_powershell, "startup uses the Windows PowerShell executable directly");
    Check(launched.command_line.find(L"-File \"" + bundled_script.wstring() + L"\" start") != std::wstring::npos,
        "quoted Unicode and space-containing script path invokes start");
    Check(launched.command_line.find(L"-NonInteractive") != std::wstring::npos &&
        launched.command_line.find(L"-WindowStyle Hidden") != std::wstring::npos, "PowerShell is noninteractive and hidden");
    Check(launched.command_line.find(L" login") == std::wstring::npos, "automatic startup never invokes login");
    Check(launched.creation_flags == CREATE_NO_WINDOW &&
        (launched.startup_flags & STARTF_USESHOWWINDOW) != 0 && launched.show_window == SW_HIDE,
        "process creation suppresses the console window");
    Check(launched.inherit_handles == FALSE && launched.working_directory == bundled_script.parent_path(),
        "helper uses its own directory without inheriting process handles");
    configured.AutoStartBundledHelperIfNeeded();
    configured.AutoStartBundledHelperIfNeeded();
    Check(launch_count == 1, "repeated initialization does not launch duplicate startup processes");

    launch_succeeds = false;
    CCodexUsageData failed_launch;
    failed_launch.AutoStartBundledHelperIfNeeded();
    failed_launch.AutoStartBundledHelperIfNeeded();
    Check(launch_count == 2, "failed process creation is not retried on every data refresh");
    launch_succeeds = true;
    Check(!LaunchBundledHelperStart(L"") && !LaunchBundledHelperStart((root / L"missing.ps1").wstring()) && launch_count == 2,
        "missing scripts do not create child processes");

    const auto root_script = module_dir / L"codex-web-helper.ps1";
    Write(root_script);
    Check(FindBundledHelperScriptPath() == bundled_script, "bundled helper subdirectory has priority");
    std::filesystem::remove(bundled_script);
    Check(FindBundledHelperScriptPath() == root_script, "legacy root helper layout remains supported");
    std::filesystem::remove(root_script);
    CCodexUsageData missing_script;
    missing_script.AutoStartBundledHelperIfNeeded();
    Check(launch_count == 2, "missing packaged script is skipped quietly");

    const auto source_script = root / L"source" / L"scripts" / L"codex-web-helper.ps1";
    Write(source_script);
    module_file_path = (root / L"source" / L"build" / L"Release" / L"plugins" / L"ClaudeUsagePlugin.dll").wstring();
    std::filesystem::create_directories(std::filesystem::path(module_file_path).parent_path());
    Check(std::filesystem::weakly_canonical(FindBundledHelperScriptPath()) == source_script, "Win32 source build layout is supported");
    module_file_path = (root / L"source" / L"build" / L"x64" / L"Release" / L"plugins" / L"ClaudeUsagePlugin.dll").wstring();
    std::filesystem::create_directories(std::filesystem::path(module_file_path).parent_path());
    Check(std::filesystem::weakly_canonical(FindBundledHelperScriptPath()) == source_script, "x64 source build layout is supported");
    module_lookup_succeeds = false;
    Check(FindBundledHelperScriptPath().empty(), "module discovery failure is safe");
    module_lookup_succeeds = true;

    std::filesystem::remove(profile / L"Default" / L"Network" / L"Cookies");
    Write(profile / L"Default" / L"Cookies");
    Check(HasInitializedHelperProfile(), "legacy dedicated Chrome cookie-store layout remains supported");
    std::filesystem::remove(profile / L"Local State");
    Check(!HasInitializedHelperProfile(), "cookie-store presence alone cannot initialize a profile");
    std::cout << "PASS: " << checks << " Codex automatic startup checks\n";
    return 0;
}
