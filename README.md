<div align="center">

# ETS2 / TruckersMP Chat Translator

[![Platform: Windows](https://img.shields.io/badge/Platform-Windows%20x64-blue?style=flat-square&logo=windows)](https://github.com/)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg?style=flat-square)](https://opensource.org/licenses/MIT)

**Euro Truck Simulator 2 / TruckersMP 实时聊天翻译插件**

[功能特点](#-功能特点) • [快速开始](#-快速开始) • [支持的翻译平台](#-支持的翻译平台) • [常见问题](#-日志与排错)

</div>

---

## 📖 简介

ETS2 / TruckersMP 聊天翻译插件。DLL 以 **SCS Telemetry** 插件形式加载，无缝读取 TruckersMP 聊天日志，将最新聊天内容异步翻译后，实时显示在游戏内的透明覆盖窗口中。

自带一个现代化的 **Electron 管理器**，让你可以轻松实现一键安装、更新 DLL、以及可视化配置各种翻译平台参数！

---

## ✨ 功能特点

- 🎮 **沉浸式覆盖窗口**：无缝贴合游戏，支持自定义字体大小。
- ⚡ **高性能异步翻译**：多 Worker 并发处理、队列限制、内存缓存，不卡顿游戏帧率。
- 🔄 **只读最新消息**：智能从文件末尾开始监听，拒绝历史日志刷屏。
- 🔌 **热重载支持**：修改配置后立即生效，下一条聊天自动重启翻译引擎。
- 🧠 **多引擎回退机制**：支持配置多个翻译 Provider，主通道失败自动回退备用通道。
- 📖 **本地短语字典**：极速兜底常见简写，如 `sry`、`pls`、`gg`、`brb`。
- 🛠️ **一体化管理器**：自动识别 ETS2 目录、一键安装/卸载、可视化配置 API Key。
- 📝 **完善的日志记录**：HTTP 状态和响应预览写入 `game.log.txt`，方便排查。

---

## 🚀 快速开始

### 1. 安装与启动

1. **下载并运行安装包**：
   ```bash
   build\installer\ETS2-Chat-Translator-Manager-Setup-0.1.0.exe
   ```
2. **打开管理器**：启动 `ETS2 Chat Translator Manager`。
3. **定位游戏目录**：选择或自动识别你的 ETS2 安装目录。
4. **一键安装**：点击 `安装 / 更新 DLL`。
5. **配置翻译平台**：在 `主翻译平台` 里选择你想用的接口协议或服务商，填写 `base_url`、`API Key`、`模型` 等字段（详见下方平台支持）。
6. **保存并生效**：点击 `保存配置`。
7. **进入游戏**：启动或重启 ETS2 / TruckersMP，享受跨越语言障碍的联机体验！

> 💡 **调试版运行**：如果你是开发者，可以直接运行 `build\ets2_chat_translator_app\ETS2 Chat Translator Manager.exe`。

### 2. 文件位置参考

- **DLL 安装位置**：`[ETS2 安装目录]\bin\win_x64\plugins\ets2_chat_translator.dll`
- **配置文件位置**：`[ETS2 安装目录]\bin\win_x64\plugins\ets2_chat_translator_config.json`

---

## 🌐 支持的翻译平台与协议

插件内置了极其强大的**语言检测分层处理**，确保针对 `source=auto` 能做出最准确的判断，不仅限于英语，完美适配 TruckersMP 的多语言环境（例如土耳其语、俄语、波兰语等）。

<details>
<summary><b>🤖 OpenAI 协议 (支持兼容服务)</b></summary>

- **类型 (`kind`)**: `openai_compatible`, `openai`, `chat_completions`
- **说明**: 支持 OpenAI 官方、DeepSeek、硅基流动、OpenRouter、LM Studio、Ollama 等所有兼容服务。
- **配置示例**:
  ```json
  {
    "kind": "openai_compatible",
    "label": "OpenAI",
    "enabled": true,
    "base_url": "https://api.openai.com/v1",
    "api_key": "YOUR_KEY",
    "model": "gpt-4o-mini",
    "source": "auto",
    "target": "zh-CN"
  }
  ```
</details>

<details>
<summary><b>🧠 Anthropic 协议 (Claude)</b></summary>

- **类型 (`kind`)**: `anthropic`, `claude`, `anthropic_messages`
- **配置示例**:
  ```json
  {
    "kind": "anthropic",
    "label": "Claude",
    "enabled": true,
    "base_url": "https://api.anthropic.com/v1",
    "api_key": "YOUR_KEY",
    "model": "claude-3-5-haiku-latest",
    "source": "auto",
    "target": "zh-CN"
  }
  ```
</details>

<details>
<summary><b>🌍 常用大厂云翻译 (DeepL, Google, Microsoft, 百度)</b></summary>

- **DeepL (`deepl`)**:
  - `base_url`: `https://api-free.deepl.com` (免费版) 或 `https://api.deepl.com` (Pro版)
- **Google Cloud Translate (`google_cloud`)**:
  - `base_url`: `https://translation.googleapis.com`
- **Microsoft Translator (`microsoft`)**:
  - 需要在 `api_secret` 填写 Azure Region (如 `eastasia`)。
- **百度翻译 (`baidu`)**:
  - 需要提供 `api_key` (APP ID) 和 `api_secret` (Secret Key)。
- **有道智云 (`youdao`)**:
  - 需要提供 `api_key` (APP Key) 和 `api_secret` (APP Secret)。
</details>

<details>
<summary><b>🆓 免费 / 兜底翻译 (MyMemory, LibreTranslate)</b></summary>

- **MyMemory (`mymemory`)**: 免费兜底接口。对短句、俚语效果一般，但无需 Key。
- **LibreTranslate (`libretranslate`)**: 根据使用的实例决定是否需要 Key。
</details>

---

## ⚙️ 配置文件说明 (`config.json`)

你可以通过管理器界面修改，或者直接编辑 JSON：

| 字段 | 说明 | 示例 |
| --- | --- | --- |
| `target_lang` | 默认目标语言 | `zh-CN` |
| `workers` | 并发翻译 Worker 数 (1-32) | `8` |
| `queue_limit` | 等待翻译队列上限 | `1000` |
| `cache_limit` | 翻译缓存条数 | `1500` |
| `timeout_ms` | HTTP 接收超时 (毫秒) | `10000` |
| `font_size` | 覆盖窗口字体大小 | `18` |
| `providers` | 翻译服务提供商数组，**按顺序尝试，失败自动回退** | `[{...}, {...}]` |

---

## 🛠️ 构建项目

本项目需要 Windows x64、Visual Studio 2019/2022 Build Tools、Node.js / npm 环境。

```bat
# 运行构建脚本
build.bat --no-pause
```

<details>
<summary><b>查看构建输出目录</b></summary>

```text
build\ets2_chat_translator.dll                        # 核心 DLL
build\installer\ETS2-Chat-Translator-Manager-Setup-0.1.0.exe # 安装包
build\ets2_chat_translator_app\ETS2 Chat Translator Manager.exe # 免安装管理器
```
*注：安装包使用 NSIS 制作，DLL 会自动打包进管理器的 `resources` 目录。*
</details>

---

## 🔍 日志与排错

如果遇到问题，请检查游戏日志文件：
`C:\Users\<你的用户名>\Documents\Euro Truck Simulator 2\game.log.txt`

搜索以下关键字快速定位：`[ChatTranslator]`、`[TranslateHTTP]`、`[Translate]`。

**常见 HTTP 错误对照：**

| 错误代码 / 提示 | 可能的原因 |
| --- | --- |
| `HTTP 400` | 模型名不被该 endpoint 支持，请检查大小写和模型 ID |
| `HTTP 401` | API Key 无效，或者 Key 不属于当前接口/区域 |
| `HTTP 403` | 鉴权失败、服务未开通、或缺少必需的参数 |
| `HTTP 429` | 请求过快或额度耗尽，请降低 `workers` 或等待恢复 |
| `cannot parse response`| 返回 JSON 格式与解析逻辑不匹配，请查看日志中的 payload 预览 |
| `returned original...`| 翻译平台原样返回了源文本，插件已拒绝将其视为有效翻译 |

---

## 🔮 后续规划

- [ ] Provider 级别的细粒度限流和自动重试机制
- [ ] 翻译耗时统计与请求时间戳记录
- [ ] 智能合并相同文本的翻译请求
- [ ] Provider 错误熔断与自动恢复功能
- [ ] 接入腾讯云、阿里云、火山翻译等带签名的国内 API
- [ ] 游戏内热更新覆盖窗口的字体和布局

<div align="center">
  <i>Made with ❤️ for the TruckersMP Community</i>
</div>
