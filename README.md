# ESP32 Codex Companion · v1.3.1

> 原版硬件开源项目：[ESP32-C3 多功能信号灯（嘉立创开源硬件平台）](https://oshwhub.com/qq605581427/project_rxaoehqc)
>
> 原版 B 站视频：[BV1cUud6nE45](https://www.bilibili.com/video/BV1cUud6nE45/)

感谢原项目作者公开硬件设计、软件源码及演示视频，为本项目提供了基础。本仓库在原项目基础上开发 Codex 状态同步与三屏动画功能；原版硬件资料请访问上方开源项目，相关署名与许可说明见文末“来源与许可”。

ESP32-C3 三屏 Codex 状态灯：工作亮绿灯，等待回答或人工授权亮黄灯，完成亮红灯。素材在设备本地播放，USB 优先同步，Wi-Fi 自动备用。

## 功能

- 无素材屏幕显示黑底居中灰色加号；导入后恢复播放，配网提示优先。
- 三屏独立任务计数、项目/会话筛选，11px 对比色圆点按弧形内移排列。
- Windows 桌面只读授权检测；查看已完成会话约1秒后自动确认该会话，保留其他未查看完成任务。
- GIF / PNG / JPG 导入，透明缓存、断电恢复、失败上传回滚及安全缓存清理。
- 无工作或等待任务后播放三屏动画，黑底/全亮可选；默认完成红灯保留10秒。
- USB 后台配网与真实连接状态；恢复原中屏配网提示图，可记住“忽略配网提示”离线使用。
- 三屏动画支持独立、同步与错峰循环；任务圆点与前景布局随0/90/180/270度旋转，背景灯效已移除。
- 原地缩放/旋转、分块缓存解码和压缩网页；精确回收原版遗留资源，保留用户配置。
- 完整恢复出厂：后台输入 RESET 确认，或正常运行后长按 BOOT 至少10秒并松开；清除素材、Wi-Fi及自定义设置，重新进入AP配网。
- 生成不同提示音；保留缺失音频芯片时跳过初始化的兼容处理。

## 使用

1.2正式版恢复中屏配网提示，支持USB后台配网与离线忽略。首次/换网进入热点时打开独立 `/wifi` 配网页；联网后访问设备IP直接进入管理页，顶部“网络设置”可再次配网。配网成功提示结束后直接恢复动画，无需重启，Wi-Fi后台可直接访问。背景灯效已移除。

1.1 正式版新增 USB 本机后台：更新固件与电脑桥接后，执行 `python .\companion_host\bridge.py --resume`，打开 `http://127.0.0.1:17831/`。查询、设置、素材导入及预览均通过 USB，不要求设备连接 Wi-Fi；原设备 Wi-Fi 后台继续保留。桥接重启后请刷新页面。完整操作与限制见 [USB 后台与存储管理](docs/usb-management.md)。正式版 v1.0.0 不具备此后台，请勿只替换网页。

“清除内存”保留当前三屏选择的整组素材，删除其他上传素材及闲置缓存；“全部清除”停止三屏并删除全部上传素材。两者都会先确认，保留 Wi-Fi、亮度、颜色及系统文件。界面中的容量指 Flash 文件系统容量，RAM 单独显示。

已安装固件的板子换电脑不需要重新烧录。设备网页：`http://esp32.local/` 或实际局域网IP；首次配网连接 `ESP32_...` 热点，访问 `http://192.168.4.1/`。

Windows 安装 Python 3（已验证3.12）和 Codex 后，将发布包解压到固定目录。阅读只读检测说明并同意后，按以下顺序执行：

```powershell
Copy-Item .\distribution\config-templates\desktop.json .\companion_host\config.json
python .\companion_host\install.py
# 审阅生成的 review-hooks.json，确认路径正确后：
python .\companion_host\install.py --install
```

在 Codex CLI 内运行 `/hooks`，审阅、信任并启用带 `--vibe-companion` 的八项定义。重新打开桌面客户端后运行：

```powershell
python .\companion_host\bridge.py --resume
python .\companion_host\bridge.py --status
```

下载仓库源码时，以上模板位于 `distribution/config-templates/`；Windows 发行包中位于 `config-templates/`。已有配置请合并 `ui_approval_detection:true`，不要覆盖其他字段。仅下载新源码不会更新旧目录里正在运行的桥接；先暂停旧桥接，再从新目录安装和恢复。

桌面模式应显示 `connected:true`、`approval_source:uia`、`ui_probe_available:true`。发布包的迁移目录可能是 `config-templates/`；以包内快速开始为准。不要复制旧电脑的账号或信任记录。

完整说明：[使用与 Codex 配置](使用说明与Codex配置.md) · [技术说明](COMPANION_README.md) · [协议](API.md) · [更新记录](CHANGELOG.md)。

## 复制给 Codex：在另一台电脑部署

适用于**已经烧入本项目固件的设备 + Windows 本地 Codex 桌面客户端**。在新电脑的 Codex 中打开一个可写目录，把下面整个提示词复制进去即可；无需填写旧电脑的用户名、COM10 或 IP。设备用 USB 数据线接入；旧电脑先暂停桥接，避免两台电脑同时控制。

```text
请直接帮我在这台 Windows 电脑部署 ESP32 Codex Companion，让已经刷好固件的三屏设备自动同步本机 Codex 任务状态，不要只给安装教程。

项目：https://github.com/jnlpc123/esp32-codex-companion
当前正式版本：https://github.com/jnlpc123/esp32-codex-companion/releases/tag/v1.3.1

设备已有固件和素材。本次只部署电脑端，保留设备图片、Wi-Fi、亮度、颜色和动画设置，不刷机、不擦除存储、不上传新素材。
我同意本项目针对 Codex 的 Windows 只读检测：检测真实授权控件、侧栏等待状态和前台会话标题元数据，用于黄灯和查看完成会话自动确认；不读取聊天正文，不点击或代替我批准。

请按以下步骤实际操作：
1. 检查 Windows、当前用户、Python（项目已验证 3.12）、Codex 桌面与 CLI、USB 串口及已有桥接。复用可用环境；如缺依赖，按官方方式安装。不要把桌面安装目录里单独复制出的内部 codex.exe 当成完整 CLI，不复制其他电脑的账号、.codex、Hook 信任记录或 runtime。若不是 Windows 本地环境，先说明未验证的限制，不宣称已完成部署。
2. 从上面的 v1.3.1 Release 下载 codex-companion-v1.3.1-windows.zip 和 SHA256SUMS.txt，核对 ZIP 的 SHA-256 后，完整解压到当前用户可写的固定目录，例如用户目录下的 CodexCompanion。不要从压缩包或临时目录运行。已有安装时先检查并备份配置，避免重复桥接或覆盖用户设置。先阅读包内“先读我_快速开始.md”和“使用说明与Codex配置.md”，核对实际脚本参数。
3. 使用包内 config-templates/desktop.json 配置 companion_host/config.json。已有配置则合并 ui_approval_detection:true，保留其他字段。使用本机真实 Python 和安装路径；串口自动发现，不写死 COM10，设备 IP 不沿用其他电脑的地址。Wi-Fi 备用优先让 USB 握手学习设备地址；确需指定时再询问本设备 IP。
4. 运行 companion_host/install.py 生成 review-hooks.json，检查其中带 --vibe-companion 的 8 项事件及命令路径，再运行 install.py --install。合并时保留原有其他 Hooks，并备份原配置；不要修改安全策略或绕过 Hook 信任。
5. 需要我登录、审阅信任、重启客户端或操作数据线时，给出简短明确的操作并等我完成。请让我在同一用户的 Codex CLI 中运行 /hooks，审阅、信任并启用本项目的八项定义；不能代我批准，也不能把“已写入 hooks.json”当成“已激活”。不要索取密码、令牌或验证码。
6. 提醒我重新打开日常使用的 Codex 桌面后，运行 companion_host/bridge.py --resume 和 --status。检查 connected:true、transport:usb、approval_source:uia、ui_probe_available:true。若失败，检查 bridge.log、串口占用、原生 USB 识别及客户端检测兼容性；只修复本项目相关问题。不要启动多个桥接进程，也不要自动添加开机计划任务。
7. 验证真实 Codex 任务：运行中绿灯；真实问答或人工授权出现时黄灯，普通自动审查不应误亮；完成时红灯，查看对应完成会话约 1 秒后确认其完成标记，其他未查看任务保留。使用自然出现的授权，不制造危险命令或测试授权。无法触发的场景明确标为待验证，网页测试按钮不能代替真实任务验收。
8. 验证 USB 优先和 Wi-Fi 备用：需同一局域网，拔掉 USB 后板子仍要有独立供电。只有供电条件具备时，再引导我拔插检查 transport 变化；断电不算切换失败，也不算 Wi-Fi 备用通过。迁移时提醒旧电脑暂停桥接。
9. 最后告诉我安装目录、Python 路径、实际串口、设备网址、检查结果，以及启动、暂停、恢复和查看状态的可复制命令。区分已验证与待验证项目，不上传本机配置或日志到公开仓库。

除上述必须由我完成的交互外，请持续执行到连接和可执行的验证完成。若权限或硬件条件阻塞，说明具体阻塞及我需要做的一步，不要假报成功。
```

提示词配套 v1.3.1 正式版电脑工具；设备固件保持不变。USB 配网需要同版本固件。实机验证范围及已知限制见 [正式版验证记录](docs/validation/release-1.3.1.md)。当前正式版为 [v1.3.1](https://github.com/jnlpc123/esp32-codex-companion/releases/tag/v1.3.1)。它部署的是电脑桥接，不需要编译固件或安装 ESP-IDF。纯 CLI 的授权判断、macOS/Linux/WSL 和云端任务不在此桌面方案的等效验证范围内。

Hook 信任步骤依据 [OpenAI 官方 Hooks 文档](https://learn.chatgpt.com/docs/hooks)；USB/Wi-Fi 桥接及 Windows 只读检测属于本项目实现。

## 构建与测试

已验证 ESP-IDF 6.1、ESP32-C3、4MB Flash；组件清单及锁文件随源码提供。网页直接嵌入应用，不需要 npm 安装、旧版前端压缩或覆盖 SPIFFS。

```sh
idf.py set-target esp32c3
idf.py build
```

电脑端及网页回归需要 Node.js、Python 3，以及 C 编译器；原生 C 清理测试可用 Zig（`ZIG` 指向 zig.exe）或设置 `CC`。无 npm 第三方依赖：

```sh
npm test
```

更新同款已有兼容引导程序/分区表的设备时，只写应用到 `0x10000`，先暂停桥接，保留 NVS/SPIFFS。发布的应用文件不是空白芯片完整量产镜像。

## 支持边界

- 精确人工授权和查看确认针对 Windows Codex 桌面本地任务；CLI 模式可接入事件，但审批推断可能误判慢速自动审查。
- 同名无法唯一匹配、检测失效、后台或最小化时不会自动确认完成。客户端UI或本地元数据格式变化后需重新验证。
- 一块板子同一时间由一台电脑控制；Wi-Fi 备用需要同局域网且板子保持供电。
- 默认112px、合适素材以约30FPS为目标；保留GIF原始停顿，不补帧。无扬声器时实际音效尚未验收。
- 本包不提供录音、普通音频播放或浏览器画面推送。

## 来源与许可

基于陈沛林的 [ESP32C3 多功能交互显示系统](https://gitee.com/pei_lin_chen/esp32c3duogongnengxinhao) 改造，保留原 MIT 许可证及署名。硬件参考 [原开源项目](https://oshwhub.com/qq605581427/project_rxaoehqc)，本仓库不重新发布其硬件设计文件。

[LICENSE](LICENSE) · [第三方许可](THIRD_PARTY_NOTICES.md)。
