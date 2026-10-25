# 项目开发说明

ESP32-C3三屏Codex伴侣，ESP-IDF（已验证6.1）、C、原生HTML/JS、Windows Python桥接。

- `main/companion`：协议、任务计数、设置及提示音。
- `main/display`、`main/core`：透明缓存、绘制及动画调度。
- `main/storage`、`main/file_server`：素材事务与存储保护。
- `web`：五项嵌入式管理资源，不运行旧前端打包脚本。
- `companion_host`：非策略Hook、桥接及只读Windows检测器。

构建使用 `idf.py build`；默认终端未必已导出IDF环境。回归使用 `npm test`（需要Node与Python3，不需要npm依赖）。硬件脚本需先暂停桥接释放串口，不能通过泛化unittest发现直接运行所有设备脚本。

C使用snake_case，前端ES6。更新只刷应用分区，不擦除NVS/SPIFFS。不得将运行队列、账号、信任记录、config.json、review-hooks.json或私人Flash备份提交发布。修改Hook路径后重新审阅信任，不绕过审批或读取聊天正文。
