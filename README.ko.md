# TrafficMonitor AI Usage Limits

![Platform](https://img.shields.io/badge/platform-Windows-0078D4)
![TrafficMonitor](https://img.shields.io/badge/TrafficMonitor-plugin-2EA043)
![Arch](https://img.shields.io/badge/arch-x64%20%7C%20x86-555555)
![Usage Sources](https://img.shields.io/badge/usage-Claude%20%2B%20Codex-0A7F5A)
[![GitHub Sponsors](https://img.shields.io/badge/GitHub%20Sponsors-support-30363D?logo=githubsponsors&logoColor=EA4AAA)](https://github.com/sponsors/bemaru)

언어: [English](README.md) | 한국어 | [简体中文](README.zh-CN.md)

Windows에서 [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor)를 통해 Claude와 Codex 사용량 한도를 작업 표시줄에 보여주는 플러그인입니다.
이 저장소는 단일 `ClaudeUsagePlugin.dll`을 배포합니다. TrafficMonitor 안에서는 플러그인이 `AI Usage Limits`로 표시됩니다.

<p align="center">
  <img src="docs/images/trafficmonitor-taskbar-compact.png" alt="TrafficMonitor 작업 표시줄에 Claude와 Codex 사용량 바가 표시된 모습" />
</p>

[Claude 사용량 페이지](https://claude.ai/settings/usage)와 [Codex 사용량 페이지](https://chatgpt.com/codex/cloud/settings/usage)를 매번 따로 확인하는 것이 번거로워서 만들었습니다. TrafficMonitor는 작업 표시줄에 상태를 가볍게 표시하기 좋은 영역을 제공하므로, 이 플러그인은 현재 사용량 한도를 그 위치에 바로 표시합니다.

## 주요 기능

- Windows 작업 표시줄에 `C5h`, `C7d`, `X5h`, `X7d`를 사용률 퍼센트로 표시
- 데이터 소스가 초기화 정보를 제공하면 툴팁에 초기화 시간 표시
- Claude 값은 포함된 Claude web helper로 읽음
- Codex 값은 로컬 session JSONL 파일에서 읽고 `CODEX_HOME` 지원
- 오래된 Claude 값을 계속 유지하지 않고, 만료된 데이터는 unavailable로 전환

## 빠른 시작

1. 먼저 TrafficMonitor를 설치합니다.
   - [TrafficMonitor Releases](https://github.com/zhongyang219/TrafficMonitor/releases)에서 공식 릴리스를 다운로드합니다.
   - 원하는 위치에 압축을 풀고 `TrafficMonitor.exe`를 실행합니다.
   - 온도 모니터링이 필요 없다면 보통 Lite 패키지로 충분합니다.
2. 설치한 TrafficMonitor와 같은 아키텍처의 플러그인을 선택합니다.
   - `x64` TrafficMonitor에는 `x64` 플러그인
   - `x86` TrafficMonitor에는 `x86` 플러그인
3. 릴리스 zip 내용을 `TrafficMonitor\plugins`에 복사합니다.

```text
plugins
├─ ClaudeUsagePlugin.dll
└─ ClaudeUsagePlugin
   ├─ claude-web-helper.ps1
   └─ helper
      └─ claude-web-helper
         ├─ index.mjs
         ├─ package.json
         └─ package-lock.json
```

4. TrafficMonitor를 재시작합니다.
5. TrafficMonitor의 작업 표시줄 표시 설정에서 `Claude 5h`, `Claude 7d`, `Codex 5h`, `Codex 7d`를 활성화합니다.
6. Claude 값을 실시간으로 보려면 최초 1회 Claude 로그인을 실행합니다.

```powershell
powershell -ExecutionPolicy Bypass -File .\plugins\ClaudeUsagePlugin\claude-web-helper.ps1 login
```

7. Codex 상태가 `%USERPROFILE%\.codex`에 저장되어 있지 않다면 TrafficMonitor를 실행하기 전에 Windows 환경 변수 `CODEX_HOME`을 설정합니다.
   해당 디렉터리 아래 session JSONL 파일에 rate-limit payload가 생기기 전까지 Codex는 unavailable로 표시됩니다.

helper 파일이 `plugins\ClaudeUsagePlugin` 아래에 있으면, 최초 로그인 후 플러그인 로드 시 포함된 Claude watcher가 자동으로 시작될 수 있습니다.

스크린샷을 포함한 전체 설치 흐름은 [docs/install.md](docs/install.md)를 참고하세요.

## 표시 항목

- `C` = Claude, `X` = Codex
- `5h` = 현재 5시간 한도 구간
- `7d` = 현재 7일 한도 구간
- `C5h`, `C7d`, `X5h`, `X7d`는 사용률 퍼센트를 표시
- 툴팁에는 동일한 사용률이 표시되며, 초기화 정보가 있을 때는 초기화 시간도 함께 표시됨
- Codex 로컬 데이터가 남은 퍼센트를 제공하면 플러그인이 표시 전에 사용률로 변환

<p align="center">
  <img src="docs/images/trafficmonitor-tooltip.png" alt="Claude와 Codex 사용량 한도 및 초기화 시간이 표시된 TrafficMonitor 툴팁" />
</p>

## 데이터 소스

- Claude는 `%LOCALAPPDATA%\trafficmonitor-claude-usage-plugin\claude-web-usage.json`의 최신 helper snapshot을 읽습니다.
- Claude helper 로그인은 전용 로컬 브라우저 프로필에 쿠키를 저장합니다. 자세한 내용은 [PRIVACY.md](PRIVACY.md)를 참고하세요.
- Codex는 `%USERPROFILE%\.codex\sessions\**\*.jsonl`의 로컬 상태를 읽습니다.
- Codex session JSONL 파일만 지원합니다. `logs_2.sqlite` fallback은 없습니다.
- rate-limit payload가 포함된 session JSONL 파일이 없으면 Codex는 unavailable로 표시됩니다.
- Codex 로컬 payload는 `used_percent` 또는 `remaining_percent`를 제공할 수 있으며, 남은 값은 사용률로 변환됩니다.
- `CODEX_HOME`은 Windows에서 읽을 수 있는 경로일 때 기본 Codex 경로를 대체합니다.
- TrafficMonitor는 Windows에서 실행되므로 `/home/<user>/.codex` 같은 Linux 전용 경로는 읽을 수 없습니다.
- Claude helper를 사용하려면 Node.js 22+와 로컬 Edge 또는 Chrome 설치가 필요합니다.

전체 runtime 모델과 helper 명령은 [docs/runtime.md](docs/runtime.md)를 참고하세요.

## 호환성

- Windows 전용
- TrafficMonitor plugin API v7
- TrafficMonitor 본체는 이 저장소에 포함되지 않음
- 공식 릴리스 asset은 현재 `x64`와 `x86` 제공
- TrafficMonitor 호환성을 위해 내부 배포 이름은 `ClaudeUsagePlugin.dll` 및 `ClaudeUsagePlugin\...` 유지

TrafficMonitor의 온도 모니터링 기능이 필요 없다면 공식 Lite 릴리스로 충분한 경우가 많습니다.

## 소스에서 빌드

필요한 환경:

- Windows
- Visual Studio 2022 또는 Build Tools 2022
- Desktop development with C++
- MSVC `v143` toolset
- `v143` toolset용 MFC
- Visual Studio에서 선택된 Windows SDK

Visual Studio에서 빌드하거나 다음 명령을 실행합니다.

```powershell
MSBuild.exe .\ClaudeUsagePlugin.sln /t:ClaudeUsagePlugin /p:Configuration=Release /p:Platform=x64
```

주요 빌드 출력:

- `build\x64\Release\plugins\ClaudeUsagePlugin.dll`
- `build\Release\plugins\ClaudeUsagePlugin.dll`

전체 출력 구조와 패키징 메모는 [docs/build.md](docs/build.md)를 참고하세요.

## 문서

- [Install guide](docs/install.md)
- [Runtime and helper guide](docs/runtime.md)
- [Build guide](docs/build.md)
- [Troubleshooting](docs/troubleshooting.md)
- [Privacy and local data](PRIVACY.md)
- [License](LICENSE)
- [Notices](NOTICE.md)
- [Changelog](CHANGELOG.md)
- [Release checklist](docs/release-checklist.md)
- [Release notes template](docs/release-notes-template.md)

## 후원

이 플러그인이 시간을 절약해준다면 아래에서 유지보수를 후원할 수 있습니다.

- [GitHub Sponsors](https://github.com/sponsors/bemaru)

## 자주 발생하는 문제

- 플러그인은 로드됐지만 사용량 항목이 보이지 않음: 보통 아키텍처가 맞지 않거나 항목이 아직 활성화되지 않은 경우입니다. [docs/install.md](docs/install.md)와 [docs/troubleshooting.md](docs/troubleshooting.md)를 참고하세요.
- Claude가 unavailable로 표시됨: 보통 helper snapshot이 없거나 오래됐거나 Claude 로그인이 만료된 경우입니다. [docs/runtime.md](docs/runtime.md)와 [docs/troubleshooting.md](docs/troubleshooting.md)를 참고하세요.
- Codex 값이 보이지 않음: `CODEX_HOME`, Windows 경로 접근 가능 여부, Codex가 최근 session JSONL rate-limit 데이터를 기록했는지 확인하세요. [docs/troubleshooting.md](docs/troubleshooting.md)를 참고하세요.
