# TrafficMonitor AI Usage Limits

![Platform](https://img.shields.io/badge/platform-Windows-0078D4)
![TrafficMonitor](https://img.shields.io/badge/TrafficMonitor-plugin-2EA043)
![Arch](https://img.shields.io/badge/arch-x64%20%7C%20x86-555555)
![Usage Sources](https://img.shields.io/badge/usage-Claude%20%2B%20Codex-0A7F5A)
[![GitHub Sponsors](https://img.shields.io/badge/GitHub%20Sponsors-support-30363D?logo=githubsponsors&logoColor=EA4AAA)](https://github.com/sponsors/bemaru)

语言: [English](README.md) | [한국어](README.ko.md) | 简体中文

这是一个用于 Windows [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) 的插件，可以在任务栏中显示 Claude 和 Codex 的用量限制状态。
本仓库发布一个 `ClaudeUsagePlugin.dll`。在 TrafficMonitor 中，插件显示为 `AI Usage Limits`。

<p align="center">
  <img src="docs/images/trafficmonitor-taskbar-compact.png" alt="TrafficMonitor 任务栏显示 Claude 和 Codex 使用量条" />
</p>

我开发这个插件，是因为 Windows 上缺少一个合适的小工具来查看这类 AI 用量限制状态。Claude 用量虽然可以通过 Claude Code statusline、Claude Desktop 或 Claude 的 VS Code 扩展查看，但这些界面会随着当前工作环境变化。Windows 任务栏则会在编辑器、终端和浏览器之间切换时一直保持可见，因此 TrafficMonitor 的任务栏插件形式很适合这个用途。

这个插件把 Claude 和 Codex 的用量限制放在固定的任务栏位置，不需要一直打开单独的用量页面。

## 主要功能

- 在 Windows 任务栏中以已使用百分比显示 `C5h`, `C7d`, `X5h`, `X7d`
- 当数据源提供重置时间信息时，在 tooltip 中显示重置时间
- Claude 数值来自随插件发布的 Claude web helper
- Codex 数值来自本地 session JSONL 文件，并支持 `CODEX_HOME`
- 不会长期保留过期 Claude 数据，过期后会显示 unavailable 状态

## 快速开始

1. 先安装 TrafficMonitor。
   - 从 [TrafficMonitor Releases](https://github.com/zhongyang219/TrafficMonitor/releases) 下载官方版本。
   - 解压到任意目录，然后运行 `TrafficMonitor.exe`。
   - 如果不需要温度监控，通常 Lite 包就够用。
2. 根据已安装的 TrafficMonitor 架构选择插件。
   - `x64` TrafficMonitor 使用 `x64` 插件
   - `x86` TrafficMonitor 使用 `x86` 插件
3. 将 release zip 的内容复制到 `TrafficMonitor\plugins`。

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

4. 重启 TrafficMonitor。
5. 打开 TrafficMonitor 的任务栏显示设置，启用 `Claude 5h`, `Claude 7d`, `Codex 5h`, `Codex 7d`。
6. 如果需要实时 Claude 数值，请执行一次 Claude 登录。

```powershell
powershell -ExecutionPolicy Bypass -File .\plugins\ClaudeUsagePlugin\claude-web-helper.ps1 login
```

7. 如果 Codex 状态不在 `%USERPROFILE%\.codex` 下，请在启动 TrafficMonitor 前设置 Windows 环境变量 `CODEX_HOME`。
   在该目录下的 session JSONL 文件写入 rate-limit payload 之前，Codex 会显示 unavailable。

如果 helper 文件保留在 `plugins\ClaudeUsagePlugin` 下，完成首次登录后，插件加载时可以自动启动内置的 Claude watcher。

完整截图安装流程请参考 [docs/install.md](docs/install.md)。

## 显示内容

- `C` = Claude, `X` = Codex
- `5h` = 当前 5 小时限制周期
- `7d` = 当前 7 天限制周期
- `C5h`, `C7d`, `X5h`, `X7d` 显示已使用百分比
- tooltip 显示相同的使用百分比，并在有重置时间信息时显示重置时间
- 如果 Codex 本地数据提供 remaining percentage，插件会在显示前转换为 used percentage

<p align="center">
  <img src="docs/images/trafficmonitor-tooltip.png" alt="TrafficMonitor tooltip 显示 Claude 和 Codex 使用限制以及重置时间" />
</p>

## 数据来源

- Claude 从 `%LOCALAPPDATA%\trafficmonitor-claude-usage-plugin\claude-web-usage.json` 读取最新的 helper snapshot。
- Claude helper 登录会把 cookie 保存在专用本地浏览器 profile 中。详情请参考 [PRIVACY.md](PRIVACY.md)。
- Codex 从 `%USERPROFILE%\.codex\sessions\**\*.jsonl` 读取本地状态。
- Codex 只支持 session JSONL 文件，不再使用 `logs_2.sqlite` fallback。
- 如果没有 session JSONL 文件包含 rate-limit payload，Codex 会显示 unavailable。
- Codex 本地 payload 可能提供 `used_percent` 或 `remaining_percent`，remaining 值会被转换为 used percentage。
- 当 `CODEX_HOME` 指向 Windows 可读取的位置时，会覆盖默认 Codex 路径。
- TrafficMonitor 运行在 Windows 上，因此无法读取 `/home/<user>/.codex` 这类 Linux-only 路径。
- Claude helper 需要 Node.js 22+，以及本地 Edge 或 Chrome。

完整 runtime 模型和 helper 命令请参考 [docs/runtime.md](docs/runtime.md)。

## 兼容性

- 仅支持 Windows
- TrafficMonitor plugin API v7
- 本仓库不包含 TrafficMonitor 本体
- 官方 release asset 当前提供 `x64` 和 `x86`
- 为保持 TrafficMonitor 兼容性，内部部署名称仍为 `ClaudeUsagePlugin.dll` 和 `ClaudeUsagePlugin\...`

如果不需要 TrafficMonitor 的温度监控功能，官方 Lite 版本通常已经足够。

## 从源码构建

需求:

- Windows
- Visual Studio 2022 或 Build Tools 2022
- Desktop development with C++
- MSVC `v143` toolset
- `v143` toolset 对应的 MFC
- Visual Studio 中选择的 Windows SDK

可以使用 Visual Studio 构建，也可以运行:

```powershell
MSBuild.exe .\ClaudeUsagePlugin.sln /t:ClaudeUsagePlugin /p:Configuration=Release /p:Platform=x64
```

主要构建输出:

- `build\x64\Release\plugins\ClaudeUsagePlugin.dll`
- `build\Release\plugins\ClaudeUsagePlugin.dll`

完整输出结构和打包说明请参考 [docs/build.md](docs/build.md)。

## 文档

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

## 支持

如果这个插件帮你节省了时间，可以在这里支持后续维护:

- [GitHub Sponsors](https://github.com/sponsors/bemaru)

## 常见问题

- 插件已加载但使用量项目没有出现: 通常是架构不匹配，或者项目还没有启用。请参考 [docs/install.md](docs/install.md) 和 [docs/troubleshooting.md](docs/troubleshooting.md)。
- Claude 显示 unavailable: 通常是 helper snapshot 不存在、已过期，或者 Claude 登录已失效。请参考 [docs/runtime.md](docs/runtime.md) 和 [docs/troubleshooting.md](docs/troubleshooting.md)。
- Codex 数值不显示: 请检查 `CODEX_HOME`、Windows 路径可见性，以及 Codex 是否已写入最近的 session JSONL rate-limit 数据。请参考 [docs/troubleshooting.md](docs/troubleshooting.md)。
