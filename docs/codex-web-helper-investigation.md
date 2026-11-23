# Codex Web Helper Investigation

References:

- [Issue #7](https://github.com/bemaru/trafficmonitor-ai-usage-plugin/issues/7)
- [Draft PR #8](https://github.com/bemaru/trafficmonitor-ai-usage-plugin/pull/8)

## PoC Review

The local implementation continues PR #8's `feature/7-codex-web-helper-poc`
branch (`a0010a3`), rather than recreating its profile-management work.

The May 2026 PoC established `https://chatgpt.com/codex` as the login surface
and introduced `scripts/codex-web-helper.ps1`, the dedicated
`codex-browser-profile`, and `codex-web-helper-status.json`. Its final state
provided login/status/reset only; it did not call authenticated endpoints or
write usage snapshots. The PoC was intentionally excluded from release output
until authentication and usage payloads could be verified. Issue discussion
also warned that unauthenticated requests can encounter a Cloudflare challenge.

This change adds an independent helper and optional web snapshot source at
the user's request. JSONL support remains available as a fallback. The live
verification record below is separate from automated tests; local builds or
fork publication do not imply that a GitHub release has been published.

## Authentication Design

1. `login` opens Google Chrome at `https://chatgpt.com/codex/settings/usage` using only
   `%LOCALAPPDATA%\trafficmonitor-claude-usage-plugin\codex-browser-profile`.
2. The user signs in directly in that dedicated profile, then closes Chrome.
3. `once` or `watch` launches headless Chrome with the same profile and a
   private remote-debugging pipe. No debugging TCP port is opened.
4. A same-origin browser request calls `https://chatgpt.com/api/auth/session`.
5. If the session contains a usable `accessToken`, browser code calls
   `https://chatgpt.com/backend-api/wham/usage` with Bearer authentication.
6. Only sanitized usage data and fixed error states return to Node. The raw
   session response, access token, cookies, and response headers never leave
   the browser context or enter helper output files.

Chrome handles its own profile encryption. The Codex helper does not decrypt
cookies or depend on the Claude helper's DPAPI code. The existing Claude
helper and its Windows PowerShell 5.1 compatibility fix remain independent.

## Automatic Startup

The plugin launches the bundled Codex watcher once per plugin load, matching
the Claude helper's lifecycle. This uses a hidden PowerShell `start` command
and preserves the existing process-identity and Node exclusive-lock checks.
No profile is opened interactively during plugin startup.

Startup is skipped when the helper script is missing or the dedicated Chrome
profile has not been initialized: `Local State` and a cookie database
(`Default\Network\Cookies`, or legacy `Default\Cookies`) must exist. The
plugin checks only file existence and does not read credential files.

After the initial `login`, close its Chrome window and restart TrafficMonitor
to reuse the saved session automatically. Manual `start` resumes refresh
without a reload. `stop` remains effective for the current plugin instance;
reloading the plugin attempts startup again. No scheduled task, Windows
service, or additional Windows startup entry is installed.

The helper must never inspect the normal Chrome profile, Codex CLI OAuth
credentials, API keys, prompts, repository data, or unrelated browser tabs.
It does not upload usage data to a third party. Browser-managed login cookies
remain in the dedicated profile, as they do for an ordinary web login.

## Usage Contract

The usage endpoint's expected rate-limit window fields are `used_percent`,
`limit_window_seconds`, and `reset_at`, under `rate_limit.primary_window` and
`rate_limit.secondary_window`. Window duration identifies the 5-hour and
weekly windows; missing or invalid data must not be interpreted as zero usage.
These are private web-interface fields, not a supported public API contract.

The plugin reads only the normalized local snapshot:

```json
{
  "schema_version": 1,
  "source": "codex-web",
  "updated_at": "2026-11-23T00:00:00.000Z",
  "five_hour": {
    "utilization": 25,
    "resets_at": "2026-11-23T05:00:00.000Z"
  },
  "seven_day": {
    "utilization": 50,
    "resets_at": "2026-11-30T00:00:00.000Z"
  }
}
```

This is a synthetic example. Usage and reset timestamps are the only
account-derived fields allowed in `codex-web-usage.json`; account IDs, email
addresses, workspace names, raw endpoint responses, and credentials are
excluded. The helper refreshes every 60 seconds while its watcher runs.

The plugin prefers valid web data no older than 180 seconds, rejecting
timestamps more than 30 seconds in the future. It rejects expired reset
windows. A failed refresh removes the previous web snapshot immediately;
otherwise a crashed helper's snapshot expires through the TTL. When web data
cannot be used, the existing JSONL reader is the fallback.

## Commands and Local Files

From the source root:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\codex-web-helper.ps1 login
powershell -ExecutionPolicy Bypass -File .\scripts\codex-web-helper.ps1 once
powershell -ExecutionPolicy Bypass -File .\scripts\codex-web-helper.ps1 start
powershell -ExecutionPolicy Bypass -File .\scripts\codex-web-helper.ps1 status
powershell -ExecutionPolicy Bypass -File .\scripts\codex-web-helper.ps1 stop
```

`watch` is the foreground equivalent of `start`. In a deployed install, use
`.\plugins\ClaudeUsagePlugin\codex-web-helper.ps1`. Both helper packages are
copied into local build output; no release has been published by this change.
`reset` removes only Codex's profile and helper files after stopping the
watcher; close the interactive Codex Chrome window first.

Files are under `%LOCALAPPDATA%\trafficmonitor-claude-usage-plugin`:

- `codex-browser-profile`: Chrome-managed login state; never share this directory
- `codex-web-usage.json`: sanitized usage snapshot
- `codex-web-helper-status.json`: sanitized operational state
- `codex-web-helper-watch.lock`: watcher identity and lifecycle metadata
- `codex-web-helper-run.lock`: exclusive login/fetch operation identity
- `codex-web-helper-stop.json`: temporary stop request with watcher identity

## Live Verification Record

Verified locally on 2026-11-23 after the user signed in directly in the
dedicated Chrome profile and closed its interactive window:

- `once` exited successfully with `Codex helper updated usage.`; two
  subsequent independent invocations also succeeded without another login.
- `/api/auth/session` returned a successful 2xx JSON response with a usable
  session access token. Its value was neither printed nor exported.
- The browser used that session in a Bearer-authenticated request to
  `/backend-api/wham/usage`, which also returned a successful 2xx JSON response.
  Exact success status codes were not recorded by the helper.
- Both expected windows (18,000 and 604,800 seconds) passed strict numeric
  used-percentage and future reset-timestamp validation.
- The helper generated `codex-web-usage.json` with only the documented
  allowlisted fields. No raw session response or credentials were written.
- `start` launched a hidden background watcher, and `status` reported the
  watcher running with a successful snapshot.
- Two subsequent background snapshots were written approximately 60 seconds
  apart. `stop` exited gracefully, removed its lock and usage snapshot, and
  `status` reported `stopped`.
- A separate empty Chrome profile returned `login_required` and produced no
  usage snapshot. The signed-in profile was not modified by that test.
- The deployed x64 DLL was loaded by the restarted local TrafficMonitor.
  Its installed Codex helper reported a running watcher and a fresh `ok`
  snapshot using the same authenticated profile. The original DLL is backed
  up locally under the ignored `test-data/installed-backup` directory.
- Both `Release|x64` and `Release|Win32` MSBuild builds succeeded outside the
  restricted execution sandbox. An earlier sandboxed compile failed with
  `C1902: Program database manager mismatch`; it was an environment issue,
  not a failing source compile after the unrestricted build.
- All 23 Codex Node tests and the actual Windows PowerShell 5.1 Claude DPAPI
  regression test passed (24 total). The actual C++ reader passed 87 checks
  on each of x64 and x86. `git diff --check` passed.

Plugin-load automatic startup was verified separately after adding the hook:

- Stopped the installed Codex watcher and confirmed that its lock and usage
  snapshot were absent, then closed TrafficMonitor.
- Installed the new x64 DLL and started only TrafficMonitor, without any
  manual helper `start` command. Within five seconds its plugin load created
  a running watcher, successful helper status, and a fresh usage snapshot.
- Restarted only TrafficMonitor while keeping that watcher running. The
  watcher identity and start timestamp remained unchanged, with exactly one
  Codex Node watcher process; plugin reload did not create a duplicate.
- The C++ automatic-start tests passed 40 checks on each of x64 and x86.
  Both release architectures built successfully, alongside the existing
  reader, Node, and Claude PowerShell 5.1 regression checks above.

The deployed Claude helper implementation and PowerShell wrapper match the
existing installation byte for byte. The source carries forward the local
process-identity lock improvements and conditional `System.Security` assembly
load required by Windows PowerShell 5.1.

Compatibility was rechecked on 2026-11-24 after merging upstream changes
through `b6dfeac` (v0.3.13). The JSONL `window_minutes` classification and
active-file selection from #12 and the Claude watch-lock hardening from #13
remain intact. Both full release solution builds completed without warnings
or errors, the five upstream DLL classification scenarios passed on each
architecture, and all four upstream Claude watch-lock checks passed. The
24 Node/DPAPI tests and 87 reader plus 40 automatic-start checks per
architecture also passed again. The classification fixtures now isolate
`LOCALAPPDATA` and `USERPROFILE` as well as `CODEX_HOME` so a real web cache
cannot override their synthetic JSONL data.

This live result validates the session-to-Bearer flow for the
tested dedicated login; it does not establish behavior for all account plans,
workspaces, or future endpoint versions.

For further verification, record only these sanitized findings:

1. Whether interactive login completes in the dedicated Chrome profile.
2. HTTP status and response classification for `/api/auth/session`, plus
   whether a usable session access token exists; never record its value.
3. HTTP status and response classification for `/backend-api/wham/usage`.
4. Whether the expected two window durations, used percentages, and reset
   fields are present and match the account's usage dashboard.
5. Which workspace the dedicated browser login selects and whether accounts
   with multiple workspaces require an explicit selection mechanism.
6. Behavior for signed-out sessions, denied access, Cloudflare challenges,
   failed HTTP requests, missing windows, and changed payloads.

Do not paste session responses, cookies, authorization headers, DevTools HAR
files, or credentials into logs, screenshots, issues, or pull requests.

## Remaining Limitations

- ChatGPT's web endpoints and headless challenge behavior can change.
- Multi-workspace behavior must be checked against the dedicated login; do
  not assume the endpoint reflects a different CLI account or workspace.
- Interactive login and the headless collector cannot hold the same Chrome
  profile simultaneously. Stop the watcher before signing in again.
- The helper is a background process, not a Windows service. Plugin load or
  manual `start` launches it for the current Windows session; neither
  registers a scheduled task or additional Windows startup entry.
- Network and authentication failures are unavailable states, never evidence
  that usage is zero.
