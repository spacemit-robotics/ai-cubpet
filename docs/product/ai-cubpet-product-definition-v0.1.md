# AI Cubpet 产品方案定义 V0.1

日期：2026-07-03

## 1. 产品定位

AI Cubpet 定义为一款离线可玩、联网增强的多模态 AI 宠物玩具。它不是单纯语音 Demo，而是通过语音、触摸、NFC、姿态、光感、LED、电机、UI 动画和音频共同塑造有感知、有情绪、有动作反馈的玩具体验。

核心原则：

- 默认离线可用，联网不是开机门槛。
- 用户主动进入联网和配网流程。
- 默认使用硬件唤醒触发对话，持续对话作为用户主动开启的模式。
- 外设不作为孤立功能存在，而是统一映射到状态、情绪、动作和反馈。
- 当前先适配 MUSE-Pi-Pro 的硬件能力，后续通过板级配置扩展更多触摸点和外设能力。

## 2. 当前代码基础

当前工程已经具备以下基础能力：

- 语音链路：硬件唤醒 GPIO、音频采集、VAD、ASR、关键词意图、DOA。
- UI 链路：通过 DDS 发布 `ToyCommand`，驱动 Qt UI GIF 显示。
- 动作链路：`CubpetMotorActions` 已支持语音意图触发头部电机动作。
- 板级配置：`cubpet_peripheral_config` 和 `config/boards/MUSE-Pi-Pro.json` 已配置 motor、NFC、light sensor、g-sensor、touch/wake GPIO、fan、PM。
- 外设示例：`examples/peripherals.c` 已覆盖 Wi-Fi、NFC、触摸 GPIO、PM、电机、风扇、光感等 SDK 调用。

后续开发建议不是把外设调用直接塞进语音 Demo，而是补齐统一的外设管理和玩具状态机。

## 3. 整体状态机

建议产品状态定义如下：

| 状态 | 含义 | 典型反馈 |
| --- | --- | --- |
| `Booting` | 开机、自检、加载模型和外设配置 | 开机动画、LED 呼吸 |
| `Idle` | 默认待机，可响应触摸、NFC、wake GPIO | 低频待机表情、低亮度 LED |
| `Awake` | 被唤醒，进入注意力状态 | 醒来表情、提示音、LED 变色 |
| `Listening` | 语音监听窗口 | 聆听动画、LED 聆听态 |
| `Thinking` | ASR、意图、云端或本地处理 | 思考动画、LED 律动 |
| `Speaking` | 播放语音、表情动画 | 说话动画、音频播放 |
| `Acting` | 执行电机、LED、音效组合动作 | 点头、摇头、抬头、摇尾 |
| `ContinuousConversation` | 持续对话模式 | 长时间保持可对话 |
| `Provisioning` | Wi-Fi 配网模式 | 配网动画、蓝色快闪 |
| `Sleep` | 低功耗或安静状态 | 睡眠动画、低亮呼吸灯 |
| `Safe/Error` | 低电、跌落、外设异常保护状态 | 警告提示、限制电机动作 |

基础流转：

```text
Booting -> Idle
Idle/Sleep --wake GPIO active--> Awake -> Listening -> Thinking -> Speaking/Acting -> Idle
Listening/SessionHold -> Provisioning -> Idle/WiFiConnected
任意状态 -> Safe/Error -> Idle/Sleep
Listening/SessionHold -> ContinuousConversation -> Idle
```

## 4. Wi-Fi 功能定义

Wi-Fi 是基础联网能力，用于资源更新、云端 AI、内容下载、时间同步、日志上报和远程配置。Wi-Fi 不应成为开机阻塞流程，断网时本地玩具能力仍需可用。

### 4.1 配网入口

- 主入口：硬件唤醒后的 Listening/SessionHold 窗口内语音触发，例如“开始配网”“连接 Wi-Fi”“重新配网”“更换 Wi-Fi”。
- 兜底入口：头、鼻子、脚三个触摸同时长按 10 秒。
- 不做开机强制配网。已保存 Wi-Fi 的开机自动连接由 NetworkManager/nmcli 等系统网络服务负责，`ai-cubpet` 不重复实现。

### 4.2 Wi-Fi 状态

| 状态 | 行为 |
| --- | --- |
| `WiFiUnconfigured` | 无保存网络，本地功能可用，等待用户主动配网 |
| `WiFiConnecting` | 系统网络服务正在连接已保存网络，`ai-cubpet` 只感知结果 |
| `WiFiConnected` | 启用联网增强能力 |
| `WiFiDisconnected` | 保持离线功能，系统网络服务可继续按自身策略重连 |
| `WiFiProvisioning` | 用户主动进入 SoftAP/linkd 配网 |
| `WiFiError` | 配网失败后语音和 UI 提示，返回离线可用 |

### 4.3 配网流程

1. 用户在硬件唤醒后的 Listening/SessionHold 窗口内说“开始配网”，或头、鼻子、脚同时长按 10 秒。
2. 玩具播放提示音或语音：“进入配网模式”。
3. UI 切换配网动画，LED 蓝色快闪。
4. 启动 SoftAP/linkd。
5. 用户通过手机提交目标 Wi-Fi。
6. 设备关闭 SoftAP，切换 STA 模式连接目标 Wi-Fi。
7. 成功提示“联网成功”，失败提示原因并回到离线可用状态。

配网模式建议 5 分钟超时，超时自动退出。配网期间再次三触摸长按 3 秒，或在有效语音窗口/持续对话模式下说“退出配网”，可退出配网模式。

## 5. 触摸功能定义

当前 MUSE-Pi-Pro 有三个触摸点，先按产品语义定义为头、鼻子、脚。未来硬件方案有更多触摸点时，只扩展配置和触摸语义映射，不改变主状态机。

### 5.1 触摸区域

| 触摸点 | 当前语义 | 短触反馈 | 长触反馈 |
| --- | --- | --- | --- |
| `touch1` | 头 | 开心、点头、亲近反馈 | 撒娇、慢点头、放松 |
| `touch2` | 鼻子 | 害羞、惊讶、小幅后仰 | 轻微拒绝、摇头 |
| `touch3` | 脚 | 好奇、玩耍、小幅摆头 | 兴奋互动、连续小动作 |

建议在板级配置中将 role 固化为：

```json
{
  "touch1": "head",
  "touch2": "nose",
  "touch3": "foot"
}
```

### 5.2 组合规则

- 头、鼻子、脚同时长按 10 秒进入 Wi-Fi 配网模式。
- 组合触发期间不执行普通触摸反馈。
- 触摸动作需要限流，例如 2 秒内最多触发一次明显电机动作。
- 高频触摸可以降级为 UI 和音效反馈，避免电机频繁动作。

### 5.3 反馈组合

触摸反馈建议由 UI、音频、LED 和电机共同组成：

```text
触摸事件 -> 情绪判断 -> UI 表情 -> 音效/语音 -> LED 情绪态 -> 电机轻动作
```

摇头不应被滥用。摇头更适合鼻子长触、连续打扰、错误操作等带有拒绝含义的场景。头部触摸更适合点头、抬头、慢动作等亲近反馈。

## 6. 唤醒模块定义

唤醒模块是 AI 玩具的注意力入口。当前 MUSE-Pi-Pro 方案中，语音唤醒词不由主 ASR 识别，而是由独立硬件语音唤醒模块识别。用户喊“小安小安”后，硬件唤醒模块拉低 wake GPIO，主控把该 GPIO active 事件作为权威唤醒事件。

### 6.1 触发方式

- 硬件语音唤醒模块识别“小安小安”后下拉 wake GPIO。
- wake GPIO 短按或等效 active-low 事件。
- 头部短触可做轻唤醒。
- NFC、姿态事件可唤醒 UI 和表情，但不一定进入完整语音监听。

板级约束：

- `config/boards/MUSE-Pi-Pro.json` 中 `role=wake` 的 GPIO 为硬件唤醒输入。
- 当前硬件唤醒触发为下拉 GPIO，因此 wake 输入应配置为 active-low，即 `active_high=false`。
- `examples/peripherals.c` 的 `misc_io watch` 可用于独立验证 wake GPIO 的 ACTIVE/INACTIVE 事件，产品主进程应复用同一套 active level 语义。
- 主 ASR 不把“小安小安”作为产品命令处理，避免硬件唤醒和软件唤醒双入口造成误触发或状态不一致。

### 6.2 唤醒流程

```text
Idle/Sleep --wake GPIO ACTIVE--> Awake -> Listening -> Thinking -> Speaking/Acting -> Idle
```

唤醒反馈：

- UI 切换醒来或注意表情。
- LED 切换聆听色。
- 播放短提示音，例如“嗯？”。
- 如有 DOA 角度，头部轻微转向用户。

### 6.3 唤醒边界

- 唤醒不是配网入口，配网入口仍为唤醒窗口内的语音指令和三触摸长按 10 秒。
- 唤醒后不默认执行大动作，默认只做轻量反馈。
- 唤醒后应有 8 到 15 秒 Listening 窗口，超时回到 Idle。
- 任意播放或动作中，wake 可作为打断入口，切换到 Listening。
- 未收到 wake GPIO ACTIVE 事件时，默认模式下不处理普通 ASR 指令，包括动作指令、配网指令和进入持续对话指令。

## 7. 语音交互模式

支持两种语音交互模式：

| 模式 | 默认性 | 说明 |
| --- | --- | --- |
| `WakeTriggered` | 默认 | 日常使用，省电，降低误触发 |
| `ContinuousConversation` | 用户主动进入 | 陪聊、讲故事、教育互动 |

### 7.1 默认唤醒触发对话

开机后进入 `Idle/WakeArmed`，主控监听硬件 wake GPIO。只有收到 wake GPIO ACTIVE 事件后，才进入 Listening 窗口，用户可以发出一次语音指令。指令处理完成后保留短暂 `SessionHold`，例如 8 到 15 秒，允许继续追问。超时无有效语音则回到 Idle。

在 `WakeTriggered` 默认模式下，未被硬件唤醒时，主程序可以持续采集音频并运行必要的前级检测，但不得调用 ASR 进行语音识别。ASR 调用必须在硬件 wake GPIO ACTIVE 后的 Listening/SessionHold 窗口内，或 `ContinuousConversation` 模式内发生。

### 7.2 持续对话模式

进入语音：

- “进入连续对话”
- “连续对话”
- “陪我聊天”
- “聊聊天”
- “开始聊天模式”

这些进入语音仅在硬件唤醒后的 Listening/SessionHold 窗口内生效。设备不应在开机后直接进入持续对话，也不应因为环境音中的普通语音误进入持续对话。

退出语音：

- “退出聊天”
- “休息一下”
- “去睡觉”

保护规则：

- 长时间无对话自动退出，例如 3 到 5 分钟。
- 低电量、过热、跌落保护时自动降级为默认唤醒触发模式。
- 持续对话模式下仍允许 wake GPIO 或触摸打断。

## 8. 其他外设功能定义

| 外设 | 产品用途 | 典型场景 |
| --- | --- | --- |
| NFC | 刷故事卡、角色卡、任务卡，触发内容、表情、动作 | 刷卡播放故事，刷角色卡切换人格 |
| 光感 | 暗光进入夜间模式，降低 LED/UI 亮度 | 夜间陪伴，减少刺眼亮度 |
| G-sensor | 检测抱起、摇晃、跌落、翻倒 | 抱起撒娇，摇晃惊讶，跌落保护 |
| LED | 表达联网、聆听、思考、说话、低电、睡眠等状态 | 配网蓝闪，低电红闪，睡眠呼吸 |
| 电机 | 点头、摇头、抬头、看向声源、摇尾巴等身体表达 | 语音动作，触摸反馈，情绪表达 |
| 风扇 | 根据温度和负载自动控制 | 高负载散热，安静模式降速 |
| 电源/PM | 低电提醒、充电动画、满电状态、低电限制大动作 | 低电提示，充电显示 |

## 9. 建议软件架构

建议在当前代码框架上抽象三层：

### 9.1 `PeripheralManager`

职责：

- 管理 Wi-Fi、NFC、触摸、wake GPIO、光感、G-sensor、风扇、电源状态。
- 将底层 SDK 回调或轮询结果转成统一事件。
- 屏蔽板级 GPIO、I2C、active level 等硬件差异；wake GPIO 必须按 active-low 配置归一化为 `WakeEvent(source=gpio)`。

输出事件示例：

```text
TouchEvent(role=head, type=short_press)
TouchEvent(role=nose, type=long_press)
TouchComboEvent(roles=head+nose+foot, duration=10s)
WakeEvent(source=gpio)
WifiEvent(state=connected)
NfcEvent(card_id=...)
LightEvent(level=dark)
MotionEvent(type=fall)
PowerEvent(level=low)
```

### 9.2 `ToyStateMachine`

职责：

- 统一处理语音、触摸、NFC、姿态、电源等事件。
- 决定当前产品状态和下一步动作。
- 管理模式切换，例如 `WakeTriggered` 和 `ContinuousConversation`。
- 管理优先级，例如跌落、低电、配网优先于普通娱乐动作。

### 9.3 `ActionController`

职责：

- 统一调度 UI DDS、音频、LED、电机、风扇策略。
- 管理动作队列、动作打断、动作限流和安全策略。
- 将产品动作抽象成可复用模板。

动作模板示例：

```text
Action::HappyTouchHead
Action::NoseReject
Action::FootPlay
Action::WakeUp
Action::StartProvisioning
Action::WifiConnected
Action::LowBattery
Action::FallDetected
```

当前 `CubpetMotorActions` 可逐步升级为 `ActionController` 的电机子模块。`examples/peripherals.c` 中外设能力可产品化迁移到 `PeripheralManager`。

## 10. MVP 开发优先级

第一阶段建议实现以下闭环：

1. Wi-Fi：语音进入配网，三触摸同时长按 10 秒进入配网兜底。
2. 触摸：头、鼻子、脚三区域交互，联动 UI、音效和电机。
3. 唤醒：硬件语音唤醒模块、active-low wake GPIO、唤醒状态机、Listening 窗口。
4. 语音模式：默认唤醒触发，支持进入和退出持续对话。
5. LED/电机：统一表达状态和情绪。
6. NFC/光感/G-sensor：扩展玩法和环境感知。

## 11. 测试方案

测试目标是验证外设接入后不只是单点可用，而是完整产品闭环可用，包括状态切换、动作联动、异常恢复和长期稳定性。

### 11.1 单元测试

配置解析：

- 验证 `MUSE-Pi-Pro.json` 可正确解析 motor、NFC、light sensor、g-sensor、touch/wake、fan、PM。
- 验证三个触摸 role 可映射为 `head`、`nose`、`foot`。
- 验证 `role=wake` 的 GPIO 配置为 active-low，即 `active_high=false`。
- 验证缺失可选外设时不会导致主程序崩溃。
- 验证无效 GPIO、I2C 地址、空 board name 等异常配置有明确错误返回。

状态机：

- `Idle -> Awake -> Listening -> Idle` 超时路径正确。
- `Idle -> Provisioning -> WiFiConnected -> Idle` 路径正确。
- `Listening/SessionHold -> ContinuousConversation -> Idle` 进入和退出路径正确。
- `Speaking/Acting` 中收到 wake 事件可打断并进入 Listening。
- `Safe/Error` 优先级高于普通触摸和娱乐动作。

触摸逻辑：

- 短触、长触、连续触摸识别正确。
- 三触摸同时长按 10 秒才触发配网。
- 三触摸未满 10 秒不触发配网。
- 三触摸组合触发期间不触发普通头、鼻子、脚反馈。
- 电机动作限流生效。

语音模式：

- 默认模式下只有唤醒后才处理完整对话指令。
- 未收到硬件 wake GPIO ACTIVE 时，语音片段不得送入 ASR，“进入连续对话”“陪我聊天”“点点头”等普通语音不触发动作或模式切换。
- 收到硬件 wake GPIO ACTIVE 后，在 8 到 15 秒 Listening/SessionHold 窗口内，“进入连续对话”“陪我聊天”等关键词才可进入持续对话。
- 持续对话模式下无需每轮唤醒。
- “退出聊天”“休息一下”“去睡觉”可退出持续对话。
- 低电或过热时持续对话可降级。

### 11.2 集成测试

Wi-Fi：

- 已保存网络的开机自动连接由系统网络服务完成，`ai-cubpet` 不阻塞启动、不重复保存或重连。
- 无已保存网络时不开启 SoftAP，仍进入离线可用状态。
- 硬件唤醒后的有效语音窗口内说“开始配网”可进入 SoftAP/linkd。
- 三触摸同时长按 10 秒可进入 SoftAP/linkd。
- 配网成功后关闭 AP 并切换 STA 模式。
- 配网失败、密码错误、超时后有 UI、LED、语音提示，并回到离线可用状态。

触摸和动作：

- 头部短触触发开心 UI、音效、轻微点头。
- 头部长触触发亲昵 UI、撒娇音、慢点头。
- 鼻子短触触发害羞或惊讶反馈。
- 鼻子长触触发拒绝反馈和摇头。
- 脚短触触发好奇或玩耍反馈。
- 脚长触触发兴奋互动。

唤醒：

- 喊“小安小安”后，硬件唤醒模块下拉 wake GPIO，主程序收到 ACTIVE 事件并从 Idle/Sleep 唤醒。
- 可通过 `examples/peripherals.c` 对应的 `ai-cubpet-peripherals misc_io watch` 独立确认 wake GPIO ACTIVE/INACTIVE 事件和 active-low 极性。
- 唤醒后进入 Listening 窗口。
- 未唤醒时普通语音不触发动作、配网或持续对话。
- 唤醒窗口内说“进入连续对话”或“陪我聊天”可进入 `ContinuousConversation`。
- Listening 超时后回到 Idle。
- 播放音频或执行电机动作时，wake 可打断当前行为。

NFC、光感、G-sensor：

- NFC 刷卡可识别 UID 并触发本地内容。
- 光感从亮到暗时，UI/LED 亮度策略切换。
- G-sensor 检测抱起、摇晃、跌落、翻倒时，触发对应状态或保护动作。

### 11.3 硬件验收测试

在 MUSE-Pi-Pro 实机上执行：

- 三个触摸点 active level 正确，无反相误判。
- 头、鼻子、脚触摸区域和产品语义一致。
- 三触摸同时长按 10 秒识别稳定。
- wake GPIO 为 active-low，喊“小安小安”时能稳定产生 ACTIVE 事件，静默和普通语音时无误触。
- 主程序占用 wake GPIO 时，外设 demo 不应同时读取该 GPIO；需要独立验证 wake GPIO 时先停止主程序。
- 电机动作角度、速度和回中动作安全。
- 连续触摸 100 次后电机不乱序、不堆积、不卡死。
- Wi-Fi 配网 20 次，成功、失败、超时路径均可恢复。
- NFC 连续刷卡 50 次无崩溃、无重复异常触发。
- 暗光、强光切换 20 次，亮度状态切换稳定。
- 跌落或翻倒事件触发后，电机停止大幅动作。
- 风扇策略在高负载下能启动，在安静状态下能降速或关闭。
- 低电状态下会提示，并限制高功耗动作。

### 11.4 性能和体验指标

| 指标 | 目标 |
| --- | --- |
| 触摸到 UI/音效反馈延迟 | 小于 150 ms |
| 触摸到电机动作开始延迟 | 小于 300 ms |
| 唤醒到 Listening 状态 | 小于 500 ms |
| NFC 刷卡到内容触发 | 小于 800 ms |
| 配网模式超时 | 默认 5 分钟 |
| Listening 无语音超时 | 8 到 15 秒 |
| 持续对话无交互退出 | 3 到 5 分钟 |
| 电机明显动作限流 | 建议 2 秒内最多一次 |

### 11.5 稳定性测试

- 8 小时待机测试，验证无内存持续增长、无外设线程退出。
- 2 小时持续对话测试，验证音频、UI、动作队列稳定。
- 2 小时触摸/NFC 混合压力测试，验证事件优先级和动作限流。
- Wi-Fi 断开、恢复、弱信号测试，验证离线功能不受影响。
- 配网中断电重启测试，验证启动后不自动进入 SoftAP，仍保持离线可用。

### 11.6 安全和隐私测试

- 开机无已保存 Wi-Fi 时，不自动开启 SoftAP。
- SoftAP 只在硬件唤醒后的有效语音窗口内收到用户配网语音，或三触摸长按 10 秒后开启。
- SoftAP 超时后自动关闭。
- 配网失败不泄露密码到普通日志。
- 持续对话必须在硬件唤醒后的 Listening 窗口内由用户主动进入，并可通过语音或 wake GPIO 退出/打断。
- 默认 `WakeTriggered` 模式下，未收到硬件 wake GPIO ACTIVE 时不调用 ASR，不处理普通语音指令。
- Sleep 状态下只保留必要唤醒能力，不执行无关录音处理和大幅动作。

## 12. 开发完成定义

一个功能闭环完成需要同时满足：

- 外设底层调用可用。
- 状态机路径正确。
- UI、音频、LED、电机至少完成一种产品反馈。
- 异常路径可恢复。
- 有单元测试或集成测试覆盖关键逻辑。
- 在 MUSE-Pi-Pro 实机完成基础验收。

## 13. 总结

AI Cubpet 的产品核心不是把外设都接上，而是让每个外设都服务于宠物的感知、情绪、状态和动作表达。平时它安静待命，用户喊“小安小安”后由硬件唤醒模块下拉 wake GPIO，主控再进入短暂 Listening 窗口；想联网时用户主动配网；摸头会亲近，碰鼻子会害羞，摸脚会玩耍；在唤醒窗口内主动进入持续对话后可以长时间陪伴。外设能力最终应通过统一状态机和动作控制器形成可维护、可扩展、可测试的产品体验。
