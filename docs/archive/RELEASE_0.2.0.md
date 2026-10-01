# Infiverse 0.2.0

## 亮点

- 完成 UPP v1 控制帧：心跳、启动、停止、日志、崩溃和版本协商。
- 完成 CRP 本地参考协议与 HTTP 中继：Verse 注册、发现、连接、信号和 SHA-256 内容寻址。
- 增加 CRP 客户端指数退避、取消、多源下载和哈希校验。
- 工作台支持查找、替换、错误行定位，并避免重复运行。
- 完整重写 Inimerse API 文档，纳入 future 规划文档。
- 清理构建缓存、测试二进制和源码备份文件。

## 验证

```text
node tools/regression.js
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
```

Windows 安装包由 Inno Setup 生成，文件名为 `InfiverseSetup.exe`。

