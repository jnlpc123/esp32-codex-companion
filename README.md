# ESP32 Codex Companion · v1.4.0

> 原版硬件开源项目：[ESP32-C3 多功能信号灯（嘉立创开源硬件平台）](https://oshwhub.com/qq605581427/project_rxaoehqc)
>
> 原版 B 站视频：[BV1cUud6nE45](https://www.bilibili.com/video/BV1cUud6nE45/)

感谢原项目作者公开硬件设计、软件源码及演示视频，为本项目提供了基础。本仓库在原项目基础上开发 Codex 状态同步与三屏动画功能；原版硬件资料请访问上方开源项目，相关署名与许可说明见文末“来源与许可”。

把 Codex 的任务状态放到桌面上：工作绿灯、等待回应黄灯、完成红灯。支持多个任务独立计数，USB 优先连接，Wi-Fi 备用。

支持“静态 / 动态”灯效，以及“原版 / 8-bit”提示音，可在后台分别设置和保存。

| 屏幕 | 默认含义 |
| --- | --- |
| 上屏 · 红 | 有已完成、尚未确认的任务 |
| 中屏 · 黄 | 等待回答或人工授权 |
| 下屏 · 绿 | 正在工作 |

支持导入自己的 GIF / PNG / JPG，素材保存在设备中，重启后继续使用。**无素材时显示黑底加号**；需要配网时中屏优先显示提示。颜色、亮度、音量和空闲动画可在后台调整。

## 第一次拿到设备

1. 准备已预装本项目固件的设备和 USB 数据线，接到 Windows 电脑；首次使用无需刷机或恢复出厂。
2. 安装 Python 3 和 Codex 桌面，登录自己的账号。从 [最新正式版](https://github.com/jnlpc123/esp32-codex-companion/releases/latest) 下载 Windows 工具包，解压到固定目录。
3. 把下方 [安装提示词](#first-use-prompt) 复制给 Codex，让它完成配置。喜欢手动操作可读 [快速开始](distribution/先读我_快速开始.md)。

配好桥接后，打开 **http://127.0.0.1:17831/** 管理设备。首次可选择连接自己的 2.4GHz Wi-Fi，或“暂不配网，离线使用”；图片可以随后导入。

## 固件下载与烧录

每个 [发行版本](https://github.com/jnlpc123/esp32-codex-companion/releases) 同时提供两种镜像：

| 文件后缀 | 用途 | 烧录地址 |
| --- | --- | --- |
| `-esp32c3.bin` | 已有兼容固件的设备升级，保留素材和设置 | `0x10000` |
| `-esp32c3-fullflash.bin` | 空白芯片首次烧录，或完整重新初始化 | `0x0` |

**完整镜像会清除 Wi-Fi、设置和全部上传素材。** 仅适用于同款 ESP32-C3、4MB Flash 板；拿到已预装固件的设备通常无需刷机。操作和校验方式见 [烧录说明](docs/flashing.md)，各版本附件也附有 `fullflash-README.txt`。

## 日常操作

在工具安装目录的 PowerShell 中运行：

```powershell
python .\companion_host\bridge.py --resume  # 启动或恢复
python .\companion_host\bridge.py --status  # 查看连接状态
python .\companion_host\bridge.py --pause   # 暂停同步
```

桥接运行时，管理网页可以关闭。Wi-Fi 备用需要电脑与设备处于同一局域网，断开 USB 后设备仍有独立供电。

“清除内存”保留当前三屏素材；“全部清除”删除全部上传素材；“恢复出厂”还会清除 Wi-Fi 和自定义设置。删除前请确认范围。

<a id="first-use-prompt"></a>

## 安装提示词

面向**首次拿到预装固件设备的 Windows 用户**。提示词包含对本项目 Codex 只读状态检测的授权，请阅读后再使用。

<details>
<summary>展开并复制：首次安装与连接</summary>

```text
我第一次使用 ESP32 Codex Companion，拿到的是一台预装本项目固件的三屏设备。请在我的 Windows 电脑上实际完成首次安装、连接和验证，让它同步我自己的 Codex 任务状态，不要只给教程。

项目：https://github.com/jnlpc123/esp32-codex-companion
工具包：https://github.com/jnlpc123/esp32-codex-companion/releases/latest

设备可能未配网，也可能没有图片。我授权安装本项目电脑端工具、合并本项目 Hooks，并启用只针对 Codex 的 Windows 只读检测（授权控件、侧栏等待状态和前台会话标题元数据），不读取聊天正文，不点击或代替我批准。

1. 检查 Windows、Python、Codex 桌面及用于审阅 Hooks 的完整 Codex CLI；缺依赖时按官方方式安装。下载最新正式版 Windows 工具包与 SHA256SUMS.txt，校验后完整解压到当前用户可写的固定目录，例如用户目录下的 CodexCompanion。核对安装脚本的实际参数。默认不刷机、不恢复出厂、不删除设备已有内容；若固件不兼容，先说明原因。
2. 让我用 USB 数据线连接目标设备，自动识别串口和身份，多块设备时先让我选择。用 config-templates/desktop.json 创建 companion_host/config.json；已有配置先备份并合并。运行 companion_host/install.py 生成预览，检查带 --vibe-companion 的八项事件和本机命令路径，再运行 install.py --install。让我在同一用户的 Codex CLI 中执行 /hooks，审阅、信任并启用这些定义，随后重新打开日常使用的 Codex 桌面。登录、信任或人工授权必须由我完成；写入配置不等于已生效。
3. 运行 companion_host/bridge.py --resume 和 --status，确认 connected:true、transport:usb、approval_source:uia、ui_probe_available:true，打开 http://127.0.0.1:17831/。未配网时引导我填写自己的 2.4GHz Wi-Fi，或选择“暂不配网，离线使用”。不要预设串口或设备 IP。
4. 询问是否导入我自己的 GIF / PNG / JPG，需要时让我选择文件，再从对应屏幕导入并确认保存。没有素材可跳过，黑底加号是正常占位，不要擅自选取电脑中的文件。
5. 用真实 Codex 任务检查工作、等待和完成的状态与计数；已导入素材时观察绿、黄、红灯。无素材时先看后台状态/计数，把灯色目视验收标为待导入后完成。用自然出现的问答或人工授权验证黄灯，普通自动审查不应误亮；无法触发的项目标为待验证，网页测试按钮不等于任务联动通过。只有已配网且设备可独立供电时再测 USB/Wi-Fi 切换。
6. 保持一个桥接进程，不自动创建开机计划任务。最后给我实际安装目录、Python 路径、设备版本与串口、后台网址、已验证/待验证项，以及启动、暂停、恢复、查看状态的可复制命令。不要索取密码、令牌或验证码，不公开本机配置和日志。

除必须由我完成的交互外，请持续执行到当前条件下可做的安装和验证完成；遇到阻塞说明我需要完成的具体一步，不要假报成功。
```

</details>

<a id="uninstall-prompt"></a>

## 卸载电脑端工具

卸载只解除这台电脑与信号灯的绑定，**不影响设备中的固件、素材、Wi-Fi 或设置，也不需要卸载 Codex 和 Python**。

1. 在工具安装目录运行 `python .\companion_host\bridge.py --pause`，等待桥接释放串口。
2. 在 Codex 的 `/hooks` 中停用本工具条目；彻底移除时，先备份实际 Hooks 配置，仅删除同时匹配 `--vibe-companion` 和本安装目录的命令，保留其他 Hooks。
3. 重新打开 Codex，确认本工具桥接已退出。备份需要保留的工具配置后，可删除专用工具目录；若从源码仓库运行，保留源码及其他工作文件，只清理本工具的本地配置和运行数据。

没有一键卸载脚本。也可以把下方提示词交给 Codex 执行：

<details>
<summary>展开并复制：卸载电脑端工具</summary>

```text
请帮我卸载这台 Windows 电脑上的 ESP32 Codex Companion 电脑端工具，保留 Codex、Python、其他 Hooks，以及设备上的固件、素材、Wi-Fi 和设置。

项目：https://github.com/jnlpc123/esp32-codex-companion

1. 先检查安装目录、正在运行的桥接命令路径和本项目 Hooks 来源，确定本次卸载的对象；存在多个安装目录而无法确定时先让我选择。不要仅凭 Python 进程名或 --vibe-companion 标记删除所有内容。
2. 使用目标安装目录的 companion_host/bridge.py --pause 持续暂停桥接，避免 Hook 再次拉起；根据进程命令路径确认该实例退出、串口已释放，不影响其他 Python 程序。核对脚本支持的参数，项目当前没有 --uninstall 一键命令。
3. 把即将修改的 Hooks 配置和需要保留的本工具配置备份到安装目录之外。检查实际加载的 Hooks 来源，仅移除命令同时属于本安装目录且带 --vibe-companion 的本项目条目；保留同组其他命令、其他 Hooks、插件和配置。使用审慎的合并写入，不用整份空配置覆盖，不修改全局安全策略或无关信任记录。若来源受组织管理，说明需要管理员处理的具体条目。
4. 引导我重新打开 Codex 以卸载已加载的 Hook 定义，再确认本项目条目与目标桥接不再生效。若有自启项或计划任务，仅在命令路径确认属于本次安装时移除；不要卸载 Codex、CLI、Python 或共享依赖。
5. 清理该工具的专用安装目录及其中本项目 runtime。若它同时是源码仓库或包含其他工作文件，保留这些文件，仅清理本工具的本地配置和运行数据；没有活动进程、Hooks 或自启项时，保留源码不影响解除绑定。删除前核对绝对路径，备份不要放在将被删除的目录内。不访问或清空设备存储，不执行恢复出厂。
6. 最后告诉我移除了哪些条目/文件、备份在哪里，以及退出进程和 Hooks 清理的检查结果。不能完成的部分如实说明，并给出我需要完成的一步；不上传个人配置或日志。

请实际执行卸载，除需要我手动重开客户端或选择目标外，不要只给教程。
```

</details>

## 更多说明

- [使用说明与 Codex 配置](使用说明与Codex配置.md)：配网、导入素材、任务范围、卸载和排障。
- [USB 后台与存储管理](docs/usb-management.md) · [更新记录](CHANGELOG.md) · [技术说明](COMPANION_README.md) · [协议](API.md)。
- [OpenAI Hooks 官方文档](https://learn.chatgpt.com/docs/hooks)：安装后仍需本人审阅与信任。

当前方案主要验证 Windows Codex 桌面的本地任务。纯 CLI 的授权判断可能误判，macOS/Linux/WSL 和云任务不属于等效支持范围。

<details>
<summary>开发与构建</summary>

构建使用 ESP-IDF 6.1、ESP32-C3、4MB Flash。网页嵌入应用，不覆盖设备素材。回归测试运行 `npm test`，需要 Node.js、Python 和 C 编译器（Windows 可设置 `ZIG`）。详细内容见技术说明；首次使用无需安装 ESP-IDF。

</details>

## 来源与许可

基于陈沛林的 [ESP32C3 多功能交互显示系统](https://gitee.com/pei_lin_chen/esp32c3duogongnengxinhao) 改造，保留原 MIT 许可证及署名。硬件参考 [原开源项目](https://oshwhub.com/qq605581427/project_rxaoehqc)，本仓库不重新发布其硬件设计文件。

[LICENSE](LICENSE) · [第三方许可](THIRD_PARTY_NOTICES.md)。
