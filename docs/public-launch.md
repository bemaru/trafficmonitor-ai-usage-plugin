# Public Launch Plan

This document is a launch draft only. Do not make the repository public, create a GitHub Release, email maintainers, or post to external communities until the owner explicitly approves.

## Recommended Order

1. Make the GitHub repository public.
2. Create the version tag and GitHub Release with the `x64` and `x86` zip assets.
3. Ask the TrafficMonitor maintainer to list the plugin on the official plugin download page.
4. Post to GeekNews Show GN for Korean developer feedback.
5. Post to Hacker News Show HN only after the English README and install flow have been tested by at least one fresh install.

## TrafficMonitor Plugin Listing

TrafficMonitor's official plugin discovery path is the `TrafficMonitorPlugins` repository, not a separate marketplace:

- Main project: <https://github.com/zhongyang219/TrafficMonitor>
- Plugin repository: <https://github.com/zhongyang219/TrafficMonitorPlugins>
- English plugin download page: <https://github.com/zhongyang219/TrafficMonitorPlugins/blob/main/download/plugin_download_en.md>
- Chinese plugin download page: <https://github.com/zhongyang219/TrafficMonitorPlugins/blob/main/download/plugin_download.md>

The plugin download page asks plugin authors to email `zhongyang219@hotmail.com` with the download URL and mark the email as `TrafficMonitor Plugin`.

### Email Draft

Subject:

```text
TrafficMonitor Plugin - AI Usage Limits
```

Body:

```text
Hello,

I built a TrafficMonitor plugin named TrafficMonitor AI Usage Limits.

It adds taskbar items for Claude and Codex usage-limit windows:
- Claude 5h / Claude 7d
- Codex 5h / Codex 7d

Repository:
<public repository URL>

Release downloads:
<GitHub Release URL>

Assets:
- TrafficMonitorAIUsageLimits_v0.3.10_x64.zip
- TrafficMonitorAIUsageLimits_v0.3.10_x86.zip

Notes:
- The plugin DLL is still named ClaudeUsagePlugin.dll for compatibility.
- TrafficMonitor itself is not bundled.
- This is not an official Anthropic or OpenAI plugin.

Please consider adding it to the TrafficMonitor plugin download page.

Thank you.
```

## GeekNews Show GN

GeekNews Show GN is appropriate because it is explicitly for products, services, apps, and open-source projects that people can try. Use it after the public GitHub Release exists.

Title draft:

```text
Show GN: TrafficMonitor에서 Claude/Codex 사용량을 작업표시줄에 표시하는 플러그인
```

Body draft:

```text
Windows에서 TrafficMonitor를 쓰면서 Claude와 Codex 사용량 한도를 매번 웹 설정 페이지에서 확인하는 게 번거로워서 플러그인을 만들었습니다.

작업표시줄에 C5h/C7d/X5h/X7d 항목을 추가해서 Claude 5시간/7일, Codex 5시간/7일 사용량을 바로 볼 수 있습니다. 툴팁에는 reset 시간도 표시합니다.

특징:
- TrafficMonitor 플러그인 DLL로 동작
- x64/x86 릴리스 zip 제공
- Claude는 로컬 웹 헬퍼 스냅샷 사용
- Codex는 로컬 session JSONL에서 rate-limit 이벤트를 읽음
- Anthropic/OpenAI 공식 플러그인은 아니고 로컬 사용 편의를 위한 best-effort 도구

GitHub:
<public repository URL>

Release:
<GitHub Release URL>
```

## Hacker News Show HN

Show HN is only appropriate when users can try the project immediately. The repo must be public, the release assets must be downloadable, and the README should explain privacy and local data sources clearly.

Title draft:

```text
Show HN: A TrafficMonitor plugin for Claude and Codex usage in the Windows taskbar
```

Text draft:

```text
I built a small TrafficMonitor plugin for Windows that shows Claude and Codex usage-limit windows directly in the taskbar.

It adds four items: Claude 5h/7d and Codex 5h/7d. Claude values come from a local browser-helper snapshot, and Codex values are read from local Codex session JSONL files. It does not bundle TrafficMonitor, and it is not an official Anthropic/OpenAI plugin.

I made it because I kept checking separate usage pages while working with both tools. Feedback on the install flow and data-source assumptions would be useful.
```

## Pre-Post Checklist

- Repository is public.
- GitHub Release exists with both architecture assets.
- `README.md` describes installation, data sources, supported architectures, and known constraints.
- `CHANGELOG.md` has a dated release section.
- Release zip layout matches `docs/release-checklist.md`.
- A fresh TrafficMonitor install can load the `x64` asset.
- No screenshots expose private account, path, token, or workspace information.
