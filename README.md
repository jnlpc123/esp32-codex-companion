# ESP32 Codex Companion · v1.0.0

ESP32-C3 三屏 Codex 状态灯：工作亮绿灯，等待回答或人工授权亮黄灯，完成亮红灯。素材在设备本地播放，USB 优先同步，Wi-Fi 自动备用。

## 功能

- 三屏独立任务计数、项目/会话筛选，11px 对比色圆点按弧形内移排列。
- Windows 桌面只读授权检测；查看已完成会话约1秒后自动确认该会话，保留其他未查看完成任务。
- GIF / PNG / JPG 导入，透明缓存、断电恢复、失败上传回滚及安全缓存清理。
- 无工作或等待任务后播放三屏动画，黑底/全亮可选；默认完成红灯保留10秒。
- 生成不同提示音；保留缺失音频芯片时跳过初始化的兼容处理。

## 使用

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

桌面模式应显示 `connected:true`、`approval_source:uia`、`ui_probe_available:true`。发布包的迁移目录可能是 `config-templates/`；以包内快速开始为准。不要复制旧电脑的账号或信任记录。

完整说明：[使用与 Codex 配置](使用说明与Codex配置.md) · [技术说明](COMPANION_README.md) · [协议](API.md) · [更新记录](CHANGELOG.md)。

## 构建与测试

已验证 ESP-IDF 6.1、ESP32-C3、4MB Flash；组件清单及锁文件随源码提供。网页直接嵌入应用，不需要 npm 安装、旧版前端压缩或覆盖 SPIFFS。

```sh
idf.py set-target esp32c3
idf.py build
```

电脑端及网页回归需要 Node.js、Python 3，无 npm 第三方依赖：

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
