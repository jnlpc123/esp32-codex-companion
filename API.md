# Companion 协议 v1

固件版本1.2.0；协议字段 `v` 保持1。

## 传输

USB 原生 Serial/JTAG，115200波特率，UTF-8单行JSON；响应为 `VIBE ` 前缀加JSON，按 `id` 关联。

局域网HTTP使用 `POST /api/companion`，JSON请求/响应；`GET /api/companion` 读取当前状态，`GET /api/sysinfo` 读取版本、存储与内存。管理网页嵌入应用，不存放在SPIFFS。

```json
{"v":1,"id":1,"op":"hello"}
```

电脑每2秒发心跳。USB优先，Wi-Fi同步使用 `sync` 并续租；HTTP普通 `state` 仅用于约6秒临时测试。局域网接口按可信网络使用，不提供公网账户认证。

## 常用操作

| op | 用途 |
| --- | --- |
| hello / get / ping | 读取状态；仅 ping 续租，hello/get 不续租 |
| state | USB同步；HTTP临时状态测试 |
| sync | HTTP电脑同步与续租 |
| config | 合并并保存配置 |
| activate | 校验目录后保存某屏素材选择 |
| ack | 确认当前范围已有完成记录，由桥接同步实际清除 |
| tone | 合成提示音，kind为waiting/done |
| prune_cache | 串行清理未使用派生缓存，保留素材和当前播放文件 |
| catalog / get_catalog | 写入/读取项目及会话目录 |
| sysinfo / storage_info / storage_list | USB 系统信息、异步空间分类、文件分页 |
| clear_media / cleanup_status | 确认清理范围后启动任务、轮询结果 |
| import_begin / import_commit / import_abort | USB 素材目录事务 |
| file_begin / file_chunk / file_status / file_end / file_read | 有界 USB 文件读写，详见下节 |

## 1.1 USB 管理

`hello.capabilities` 声明 `storage_v1`、`usb_media_v1`、`storage_async_v1`；旧固件不应接受新上传。状态 `state/sync/ping` 按规定续租，管理操作不续租。

USB 的 import_begin/commit/abort、file_begin/chunk/end 返回 pending:true 和 storage_job，客户端以 storage_job_status(job_token=storage_job) 每 20ms 查询至完成；状态查询不持有串口，也不重发写请求。每次仅一个 I/O 任务，完成结果保留至下一次写任务；未知或重启失效 token 拒绝。电脑桥接在管理线程中轮询，串口所有者每次只处理一条请求。后台垃圾回收耗时不会阻塞串口状态命令。旧 HTTP 导入路径保持原行为。

sysinfo 额外返回 spiffs_reliable_free、spiffs_upload_budget：以分区约 75% 利用范围作保守估算，并预留 128 KiB。它们不是剩余 Flash 的替代计量或写入成功保证。网页和设备都在写入前检查；名义剩余空间大于预算时仍可能拒绝导入，不自动删除用户素材。

`storage_info` 首次返回 `pending:true`，80ms 后重试，建议总超时 8 秒；扫描在独立任务执行，修订号变化则拒绝不一致结果。完成返回 `storage.categories` 六类：current/uploaded/incomplete/cache/legacy_named/unknown。文件逻辑字节数不等于 SPIFFS 分配占用。

`storage_list` 请求 `cursor`（初始 0）与 `limit`（1–8）；返回 files、next_cursor、complete、revision。扫描期间 revision 变化须从头刷新。

`clear_media` 范围与确认值：unused/DELETE_UNUSED_MEDIA、all/DELETE_ALL_MEDIA、cache/DELETE_UNUSED_CACHE。返回 cleanup 对象（job_id、pending 及统计）；以 `cleanup_status` 的 job_id 轮询。删除部分失败会返回错误和已处理统计，不宣称全部成功。all 先持久化空选择，再由动画线程分批停止并清理。未知文件、根配置、Wi-Fi 和非媒体设置均保留；cache 只删除未使用派生缓存。

USB 上传：import_begin(directory=v加8位十六进制)→token；file_begin(token,name,length,crc32)→file_token；file_chunk(file_token,offset,data=Base64,crc32)；file_end(file_token)；activate 沿用原操作；import_commit(token)。失败用 import_abort(token)。只允许白名单文件名、单事务、单文件≤256 KiB、每块≤768 原始字节；完整请求/响应≤2048字节。重复相同块/结束可重试，冲突重复、坏 CRC、乱序与跨目录写拒绝。file_status 返回已收 offset；file_end 校验全文件 CRC。重启后重新开始，不支持跨重启续传。

`file_read` 请求 path=/data/目录/文件、offset、size≤768；返回 length、offset、data、eof。禁止读取根配置或电脑文件。

电脑端默认 `http://127.0.0.1:17831/`，只监听 IPv4 loopback。`GET /api/local` 返回当前设备、boot_id、能力与本次启动 token。写操作带 X-Vibe-Key、X-Vibe-Device，Host 与 Origin/Referer 必须同源。上传 token/请求绑定设备与启动代次，换板不转移写入；后台不将管理请求转发到设备 Wi-Fi。状态同步的原 Wi-Fi 备用保留。可在 companion_host/config.json 设置 management_port（端口冲突会报告，不终止状态桥接）。

状态使用 `idle` / `working` / `waiting` / `done`。详细多任务计数应与状态一致，优先级等待、工作、完成、空闲。

```json
{"v":1,"id":2,"op":"state","state":"working","notify":false,"counts":{"working":2,"waiting":0,"done":1},"completion_seq":5,"scope":{"mode":"all","target":""}}
```

详细计数中工作/等待/完成取0–9999；有完成记录时 `completion_seq` 为正数。通知由桥接去抖合并，重复通知ID不会重放；重连同步不重放旧通知。

状态额外返回 `frame_indexes`（当前帧序号）及 `backgrounds`（状态背景颜色）。系统信息增加 `minimum_free_heap`、`largest_free_block`、`animation_stack_free_min`、本次启动的 `legacy_reclaimed_files/bytes`。精确遗留资源回收不含未知文件或用户素材。

## USB Wi-Fi 配网与提示

`hello.capabilities` 包含 `wifi_management_v1` 时，USB 可使用 `wifi_status`、`wifi_configure`（ssid/password）和 `wifi_ignore`（ignored 布尔值）。这些命令不续租工作状态。

返回 `wifi`：ready、connected（真实 STA 取得 IP）、ssid、ip、ap_mode、ignored、prompt、displayed_prompt，以及 setup.job_id/state/saved/error。密码不出现在回复或日志。连接在后台执行，状态轮询不会等待认证/DHCP完成；失败不覆盖保存的凭据，新记录作为一个版本化 NVS blob 写入，旧格式可读取。

SSID 最大32字节；空密码表示开放网络，WPA密码8–63字节，或64位十六进制PSK。忙碌/初始化未完成拒绝新请求；错误密码/无IP返回失败，可重试。取得IP并保存后成功提示约2秒，关闭AP并恢复动画，不重启设备或网页服务。短暂掉线保留自动重连及延后AP收尾。

USB后台提供 GET `/api/wifi/status`、POST `/api/wifi/configure` 和 POST `/api/wifi/ignore`，沿用本机令牌/Origin/设备校验。Wi-Fi后台同名HTTP接口共用设备逻辑。忽略选项跨重启保持，只跳过屏幕提示，不删除凭据或关闭配网入口。

中屏提示图嵌入应用，按AP、连接中、成功三种状态显示；不占SPIFFS。1.2起移除背景灯效，旧配置规范化时清除失效字段，动画联动和状态颜色保留。

## 配置

`config` 操作中的 `config` 对象可部分更新，其他设置保留。成功后写入NVS；不替换用户素材存储。

| 字段 | 范围/值 |
| --- | --- |
| colors | 3个RGB565整数，依次完成、等待、工作 |
| volume | 0–80 |
| muted | 布尔值 |
| sound_style | original / 8bit；新设备默认 8bit，旧配置迁移保留 original |
| display_rotation | 0 / 90 / 180 / 270；保存后立即应用并持久化；旧配置继承原 config.json 方向 |
| signal_pattern | steady / task_cycle；默认 steady，task_cycle 使用各屏独立布局 |
| brightness | 5–100 |
| foreground_size | 80–144；圆点避让时实际前景最大适配132px |
| layout | traffic / all |
| idle_playback | off / colors / black |
| idle_done_delay | -1–300秒；-1等待完成确认，0立即，默认10 |
| animation_link | independent / sync / stagger；默认独立 |
| media | 3个设备目录路径，格式 `/data/...` |
| scope | mode为all/project/session，target为对应ID；all使用空字符串 |

```json
{"v":1,"id":3,"op":"config","config":{"idle_playback":"black","idle_done_delay":10}}
```

响应含 `ok`、`state`、`counts`、`transport`、`config`、`idle_playing`、`backgrounds`、`active`、`frames` 等。`frames` 是累计渲染次数；长停顿及状态重画会影响测得帧率。`sound_available` 不代表喇叭已经安装。

## 素材导入

网页把GIF/PNG/JPG转换成独立目录下的透明VAF1缓存，保留逐帧时序。`POST /api/import` 的begin/commit/abort配合 `POST /upload/<目录>/<文件>`；每帧上传后校验，`activate`确认完整目录，成功才替换选择。失败回滚，断电半成品在启动时回收。

文件限额/命名及CRC由服务器校验，禁止覆盖已有帧及 `_upload.txt` 保留标记。不要直接删除NVS、SPIFFS或当前选中目录。内置 `/audio`、旧 `/display`、WebSocket录音与浏览器画面推送接口不在1.0中提供。

Windows电脑配置与真实任务验证详见 [完整使用说明](使用说明与Codex配置.md)。

动态灯效每步 500ms，红→黄→绿→黄共两轮；完成循环后才开始 idle_done_delay 计时。新工作、等待或清空完成标记会中断循环。config 支持部分更新，音效与灯效字段可分别保存。
