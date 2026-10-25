# Companion 协议 v1

固件发行版本1.0.0；协议字段 `v` 保持1。

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
| hello / get / ping | 读取状态；USB读操作也保持心跳 |
| state | USB同步；HTTP临时状态测试 |
| sync | HTTP电脑同步与续租 |
| config | 合并并保存配置 |
| activate | 校验目录后保存某屏素材选择 |
| ack | 确认当前范围已有完成记录，由桥接同步实际清除 |
| tone | 合成提示音，kind为waiting/done |
| prune_cache | 串行清理未使用派生缓存，保留素材和当前播放文件 |
| catalog / get_catalog | 写入/读取项目及会话目录 |

状态使用 `idle` / `working` / `waiting` / `done`。详细多任务计数应与状态一致，优先级等待、工作、完成、空闲。

```json
{"v":1,"id":2,"op":"state","state":"working","notify":false,"counts":{"working":2,"waiting":0,"done":1},"completion_seq":5,"scope":{"mode":"all","target":""}}
```

详细计数中工作/等待/完成取0–9999；有完成记录时 `completion_seq` 为正数。通知由桥接去抖合并，重复通知ID不会重放；重连同步不重放旧通知。

## 配置

`config` 操作中的 `config` 对象可部分更新，其他设置保留。成功后写入NVS；不替换用户素材存储。

| 字段 | 范围/值 |
| --- | --- |
| colors | 3个RGB565整数，依次完成、等待、工作 |
| volume | 0–80 |
| muted | 布尔值 |
| brightness | 5–100 |
| foreground_size | 80–144；圆点避让时实际前景最大适配132px |
| layout | traffic / all |
| idle_playback | off / colors / black |
| idle_done_delay | -1–300秒；-1等待完成确认，0立即，默认10 |
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
