# Infiverse 桌面应用（B0 骨架）

Inimerse 桌面应用 —— **Tauri（Rust + WebView2）**。气质：VS Code 骨架（左侧活动栏 8 模块）+ Steam 内容（成就/库/联机）+ Obsidian 皮肤（极简本地优先）。

## 当前状态：B0 骨架完成（Tauri 壳 + UI，可运行）

**运行**（已编译）：`src-tauri\target\debug\app.exe`（12.5MB debug，窗口已弹出）
**重新编译**：`cd src-tauri && cargo build`（cargo/rustc 1.97.1，用户级安装，rsproxy 镜像）
**结构**：`src-tauri/`（Rust 壳：Cargo.toml + tauri.conf.json + src/）+ `src/ui/`（Web UI，frontendDist 指向此处）

### 环境（已装）
- Rust 工具链：rustup 用户级（%USERPROFILE%\.cargo、\.rustup），清华镜像
- tauri-cli 2.11.4（npm 用户级 %APPDATA%\npm）
- cargo 镜像：%USERPROFILE%\.cargo\config.toml（rsproxy-sparse）

---

## 开发记录：B0 UI 骨架（纯前端，可预览）

```
D:\Infiverse_standard\
└─ src\ui\
   ├─ index.html   # 活动栏 8 模块 + 侧边栏 + 主视图
   ├─ app.css      # Obsidian 皮肤（浅/深主题）
   └─ app.js       # 模块路由 + 三屏闭环 + 占位数据
```

**预览**：浏览器直接打开 `src\ui\index.html`。

**已实现**：
- 8 模块活动栏：主页 / 聊天 / 资源联机 / 工作台 / Inimerse 管理 / 工具箱 / 插件库 / 设置
- **一期三屏闭环**：主页 → 工作台（declare 参数面板）→ Inimerse 管理（一键更新流水线）
- 主题切换、hash 路由、占位数据（身份/成就/下载/项目/组件）

## 下一步（按序）

1. **安装 Rust 工具链**（rustup：`winget install Rustlang.Rustup` 或 rustup-init.exe）——Tauri 必需，当前环境未装
2. **Tauri init**：`cargo install tauri-cli` → `cargo tauri init`（web assets 指向 `src/ui`）→ 窗口/托盘/权限配置
3. **引擎对接**（Tauri 命令层）：
   - `inimerse where` 路径发现（B0）
   - source 一键更新真实流水线：`git pull → build.ps1 → 备份旧 exe → 替换 → 重启`
   - CHANGES.txt 变更日志解析
4. **B1 主页**：身份对接（identity_mod：头像/UID/二维码）、成就墙、我的下载

## 与引擎的路径约定

- 源码：`D:\inimerse_stable`（build.ps1 → `inimerse.exe` → 部署 `%USERPROFILE%\Infiverse\`）
- 引擎运行/服务：`D:\inimerse`（start_all.ps1：4 引擎 + 4 桥 + 1 app，8 端口）
- 模型：`D:\models\qwen2.5-7b-instruct-q3_k_m.gguf`（Ollama：Qwen2.5-7B-Instruct）
