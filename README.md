# AI Cubpet

SpacemiT K1 Cubpet 本地语音交互应用，集成 Qt UI、DDS 通信、4 声道采集、WebRTC AGC、Silero VAD、SenseVoice ASR、关键词动作分发和电机控制。

`ai_cubpet_daemon` 是推荐的用户入口。它会自动准备 UI 运行环境、启动 `ai-cubpet-ui` 和 `ai-cubpet`，并把日志写到 `~/.cache/ai-cubpet/logs/`。普通使用不需要手动启动 UI 或裸跑 `ai-cubpet`。

## 快速开始

拉取 SDK workspace：

```bash
mkdir -p ~/workspace/spacemit-ai-cubpet
cd ~/workspace/spacemit-ai-cubpet
repo init -u https://github.com/spacemit-robotics/manifest.git -b main -m default.xml \
  --repo-url=https://gitee.com/spacemit-robotics/git-repo \
  -g core,agent_tools,ai-cubpet,model_zoo,multimedia,peripherals
repo sync -j4
repo start robot-dev --all
```

编译当前 target：

```bash
source build/envsetup.sh
lunch k1-ai-cubpet
m
```

启动：

```bash
ai_cubpet_daemon start
ai_cubpet_daemon status
ai_cubpet_daemon logs
ai_cubpet_daemon stop
```

首次启动会自动下载缺失的 GIF 和音频资源，也会触发 VAD/ASR 模型准备，耗时会比后续启动更长。启动成功后，可以直接说 `摇摇头`、`点点头`、`摇尾巴` 等命令观察 UI、音频和电机动作。

## 运行入口

```text
ai_cubpet_daemon [--config FILE] <command>
```

| 命令 | 说明 |
| --- | --- |
| `start` | 启动 daemon、UI 进程和语音控制进程 |
| `restart` | 先停止再启动 |
| `stop` | 停止 daemon、UI 和语音控制进程 |
| `status` | 显示 daemon / UI / voice PID 和当前日志路径 |
| `logs` | 跟随当前 daemon 日志 |
| `config-init [--force]` | 写入默认配置；`--force` 覆盖已有配置 |
| `config-show` | 输出合并后的有效配置 |

默认配置文件：

```text
~/.config/ai-cubpet/ai_cubpet.json
```

如果设置了 `XDG_CONFIG_HOME`，配置目录会变为：

```text
$XDG_CONFIG_HOME/ai-cubpet/ai_cubpet.json
```

## 默认配置

首次 `start` 会自动写入缺失的默认配置。需要手动生成或重置配置时：

```bash
ai_cubpet_daemon config-init
ai_cubpet_daemon config-init --force
ai_cubpet_daemon config-show
```

核心默认值：

```json
{
  "audio": {
    "input": -1,
    "input_device_hints": ["SPV Composite", "USB Audio"],
    "output": -1,
    "output_device_hints": ["SPV Composite", "USB Audio"],
    "rate": 16000,
    "channels": 4,
    "speech_channel": 1
  },
  "dds": {
    "enabled": true,
    "domain_id": 0
  },
  "agc": {
    "enabled": true
  },
  "ui": {
    "enabled": true,
    "user": "initer",
    "qt_qpa_platform": "wayland"
  }
}
```

`audio.input = -1` 表示按 `input_device_hints` 自动匹配录音设备。`audio.output = -1` 表示按 `output_device_hints` 自动匹配播放设备，匹配失败时回退到默认 ALSA 输出。默认优先选择名称包含 `SPV Composite` 或 `USB Audio` 的设备，避免刷机、HDMI 插拔或 ALSA card 顺序变化导致固定数字 index 失效。确实需要固定设备时，把对应字段改成非负 PortAudio 设备 index。

查看当前输入/输出设备：

```bash
ai-cubpet --list-devices
```

常用配置项：

| 需求 | 修改位置 |
| --- | --- |
| 固定录音设备 | `audio.input` |
| 调整设备名匹配 | `audio.input_device_hints` |
| 固定播放设备 | `audio.output` |
| 调整播放设备名匹配 | `audio.output_device_hints` |
| 修改采样率或声道数 | `audio.rate` / `audio.channels` |
| 修改送入 VAD/ASR 的通道 | `audio.speech_channel` |
| 调 VAD 灵敏度 | `vad.threshold` / `vad.stop_threshold` |
| 保存 ASR 输入 WAV | `debug.save_wav` / `debug.save_wav_file` |
| 保存原始语音通道 WAV | `debug.save_raw_wav` / `debug.save_raw_wav_file` |
| 修改 DDS domain | `dds.domain_id` |
| 指定 GIF/音频资源目录 | `ui.gif_dir` / `ui.audio_dir` |

## 资源下载

仓库不携带 GIF 和音频资源。`ai_cubpet_daemon start` 会检查必需资源，缺失或空文件会自动下载。

下载地址：

```text
https://archive.spacemit.com/spacemit-ai/model_zoo/assets/audio/
https://archive.spacemit.com/spacemit-ai/model_zoo/assets/gif/
```

默认缓存目录优先级：

1. `ui.audio_dir` / `ui.gif_dir`
2. `AI_CUBPET_ASSET_ROOT/audio` 和 `AI_CUBPET_ASSET_ROOT/gif`
3. `$XDG_CACHE_HOME/models/assets/audio` 和 `$XDG_CACHE_HOME/models/assets/gif`
4. `$HOME/.cache/models/assets/audio` 和 `$HOME/.cache/models/assets/gif`
5. `/root/.cache/models/assets/audio` 和 `/root/.cache/models/assets/gif`

示例：

```bash
export AI_CUBPET_ASSET_ROOT=/home/user/.cache/models/assets
ai_cubpet_daemon start
```

日志中看到 `/tmp/ai-cubpet-ui-runtime-<uid>/share/ai-cubpet/gif` 是正常现象。daemon 会把 UI 二进制、DDS 运行库、GIF 和 IDL 拷贝到 `/tmp` runtime mirror，再以 `initer` 用户启动 UI。音频播放由 `ai-cubpet` 进程通过 SDK audio 组件完成，真实下载缓存仍在上面的资源缓存目录。

## 构建说明

本仓库作为 SDK application 包使用，推荐在 SDK workspace 中构建：

```bash
source build/envsetup.sh
lunch k1-ai-cubpet
m
```

`source build/envsetup.sh` 后，`output/staging/bin` 会加入 `PATH`，因此可以直接执行 `ai_cubpet_daemon`。

`package.xml` 声明了系统依赖，包括 PortAudio、sndfile、FFTW、samplerate、libcurl、Qt Base、nlohmann-json、ONNX Runtime 等。SDK 构建会按包依赖检查这些系统包。

CMake 默认构建以下产品二进制：

| 目标 | 说明 |
| --- | --- |
| `ai_cubpet_daemon` | 推荐入口，管理 UI 和语音控制进程 |
| `ai-cubpet` | 本地语音控制和音频播放进程，由 daemon 启动 |
| `ai-cubpet-ui` | DDS 驱动的 Qt UI 显示进程，由 daemon 启动 |

默认不会安装开发调试工具。需要 DDS/UI 调试工具时：

```bash
cmake -S application/native/ai-cubpet -B build/ai-cubpet \
  -DAI_CUBPET_BUILD_TEST_TOOLS=ON
cmake --build build/ai-cubpet
```

可选 CMake 开关：

| 开关 | 默认值 | 说明 |
| --- | --- | --- |
| `AI_CUBPET_BUILD_VOICE_DEMO` | `ON` | 构建 `ai-cubpet` |
| `AI_CUBPET_BUILD_UI_DEMO` | `ON` | 构建 `ai-cubpet-ui` |
| `AI_CUBPET_BUILD_TEST_TOOLS` | `OFF` | 构建 `ai-cubpet-dds-test-pub` 等调试工具 |
| `AI_CUBPET_USE_AEC` | `ON` | 拉取并构建 WebRTC audio processing |
| `AI_CUBPET_USE_DDS` | `ON` | 拉取并构建 CycloneDDS/CycloneDDS-CXX |

第三方源码通过 CMake 拉取：

```text
https://gitee.com/spacemit-robotics/cyclonedds.git
https://gitee.com/spacemit-robotics/cyclonedds-cxx.git
https://gitee.com/spacemit-robotics/webrtc-audio-processing.git
```

## 调试

查看 daemon 日志：

```bash
ai_cubpet_daemon logs
```

列出输入/输出设备：

```bash
ai-cubpet --list-devices
```

保存调试音频：

```json
{
  "debug": {
    "save_wav": true,
    "save_wav_file": "~/.cache/ai-cubpet/asr_input.wav",
    "save_raw_wav": true,
    "save_raw_wav_file": "~/.cache/ai-cubpet/raw_input.wav"
  }
}
```

裸跑 `ai-cubpet` 只用于调试，不是产品入口：

```bash
ai-cubpet --input -1 --input-device-hint "SPV Composite" \
  --output -1 --output-device-hint "SPV Composite" \
  --rate 16000 --channels 4 --speech-channel 1
```

单独启动 UI 也只用于调试：

```bash
ai-cubpet-ui
```

构建调试工具后，可以手动向 UI 发布 DDS 媒体命令；UI 只消费 GIF，`--audio` 字段会被忽略：

```bash
ai-cubpet-dds-test-pub --gif 03_diz.gif --audio 013_shake_head.wav
```

## 常见问题

### daemon 启动后很快退出

先看日志：

```bash
ai_cubpet_daemon logs
```

如果看到类似：

```text
Requested 4 channels but device only has 0
[audio] failed to start capture
```

说明录音设备选错了，通常是固定数字 index 指到了 HDMI。保持 `audio.input = -1`，并使用 `input_device_hints` 匹配 `SPV Composite`/`USB Audio`。

### 日志里 GIF 路径在 `/tmp`

这是 UI runtime mirror，不是下载目录。daemon 需要以 `initer` 用户运行 UI，但 `initer` 通常不能访问 `/root/workspace/...`，所以会把 UI 运行所需文件复制到：

```text
/tmp/ai-cubpet-ui-runtime-<uid>/
```

### 首次启动时间长

首次启动会下载 GIF/audio 资源，也可能下载 VAD/ASR 模型。后续资源和模型存在后启动会明显更快。

### UI 没有显示

确认 HDMI 或 Wayland/X11 环境正常，然后看日志里的 Qt platform：

```text
starting ai-cubpet-ui with QT_QPA_PLATFORM=...
```

默认优先 Wayland，缺少 Wayland socket 时会 fallback 到可用平台。
