# 固件烧录

在 [发行页面](https://github.com/jnlpc123/esp32-codex-companion/releases) 选择版本，日常使用建议选最新正式版。以下仅适用于原项目同款 **ESP32-C3、4MB Flash** 硬件，不适用于 ESP32-S3 或其他分区布局。

## 选择镜像

| 用途 | 文件 | 地址 | 设备数据 |
| --- | --- | --- | --- |
| 已有兼容固件的设备升级 | `codex-companion-v版本号-esp32c3.bin` | `0x10000` | 保留素材、Wi-Fi 和设置 |
| 空白芯片首次烧录，或完整重新初始化 | `codex-companion-v版本号-esp32c3-fullflash.bin` | `0x0` | **全部清除** |

完整镜像固定为 4MB，包含兼容的启动程序、分区表、该版本原始应用及已格式化的空素材文件系统。Wi-Fi 和设置区为空，不含上传素材或个人数据。它不是从某台设备读取的备份。

烧录完整镜像前，请自行备份需要保留的内容。单独下载 `fullflash.bin` 即可完成烧录，不需要再分别写入应用、分区表或素材文件系统。

## 完整烧录

以 v1.3.1 为例，下载以下附件到同一个文件夹：

- `codex-companion-v1.3.1-esp32c3-fullflash.bin`
- `fullflash-SHA256SUMS.txt`
- `fullflash-manifest.json`（组成及校验信息）
- `fullflash-README.txt`（该版本的操作说明）

1. 停止电脑端桥接（`python .\companion_host\bridge.py --pause`），关闭串口监视器。只连接要刷的设备，在设备管理器中确认其串口。
2. 在下载目录打开 PowerShell，校验镜像；把输出与 `fullflash-SHA256SUMS.txt` 中同名文件的值比较，必须一致。

```powershell
Get-FileHash .\codex-companion-v1.3.1-esp32c3-fullflash.bin -Algorithm SHA256
```

3. 安装官方 esptool。以下命令中的 `COMx` 必须替换为目标设备的实际串口：

```powershell
python -m pip install esptool
python -m esptool --chip esp32c3 --port COMx --baud 460800 write-flash --flash-mode dio --flash-freq 80m --flash-size 4MB 0x0 .\codex-companion-v1.3.1-esp32c3-fullflash.bin
```

无需额外执行整片擦除。无法自动进入下载模式时，按住 BOOT、短按 RESET、再松开 BOOT 后重试。烧录完成按 RESET；按该版本的使用说明配网、导入自己的素材，并启动对应的 Windows 工具包。全新设备没有动画素材是正常情况。

也可使用 Espressif Flash Download Tool：选择 ESP32-C3，将完整镜像地址设为 `0x0`，选择 DIO、80MHz、4MB，只勾选这一份文件。

## 保留数据升级

已安装兼容固件、只需要升级时，继续使用原有应用镜像和 `SHA256SUMS.txt`，**不要使用完整镜像**：

```powershell
python -m esptool --chip esp32c3 --port COMx --baud 460800 write-flash --flash-mode dio --flash-freq 80m --flash-size 4MB 0x10000 .\codex-companion-v1.3.1-esp32c3.bin
```

升级后恢复桥接：`python .\companion_host\bridge.py --resume`。跨版本时同时阅读更新日志，电脑端工具建议与所选固件版本配套。

## 历史版本说明

完整镜像是为历史发行版补充的安装附件，其中应用与该版本原有应用附件逐字节一致。原应用、Windows 包、源码、标签和原 `SHA256SUMS.txt` 均保持不变；新增附件使用独立的 `fullflash-SHA256SUMS.txt`，不替换旧校验文件。

候选版仍属于测试版本，不因新增完整镜像而变成正式版。v1.0.0 不支持 USB 本机管理后台；其他功能也以对应版本为准，不要把最新版的操作说明套用到所有旧版本。
