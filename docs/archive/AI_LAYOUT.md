# Inimerse AI 排版助手 - 使用说明
> **核对提示（2026-08-16）**：本文件为设计/规范文档，实现状态可能已变化；权威总览以 `docs/API_REFERENCE.md` 为准。

## 概述

通过本地 Ollama 视觉模型自动分析界面截图，并给出排版改进建议。无需联网，模型在本地运行。

## 架构

工作台/联机大厅 → AI 排版助手 → `ai_layout_gbk.im`

```text
gui_screenshot（截图）
    → ai_vision（llava:7b）
    → 改进建议
    → 日志显示
```

## 依赖

1. Ollama 已安装并运行（`localhost:11434`）。
2. 视觉模型：`llava:7b`（`ollama pull llava:7b`）。
3. 文本模型：`Qwen2.5-7B-Instruct`（可通过 Modelfile 自建）。
4. 引擎内置接口：`ai_ask`、`ai_vision`、`ai_text`、`ai_code`、`screenshot`。

## 使用

1. 启动联机大厅或工作台。
2. 点击“AI 排版”按钮。
3. 窗口打开后点击“分析当前界面”。
4. 等待模型响应；7B 模型通常需要约 30–90 秒。
5. 改进建议会显示在日志框中，可复制到剪贴板。

## 模型管理

```powershell
ollama list
ollama pull llava:7b
ollama create qwen2.5-7b-instruct -f Modelfile
```

## 防误操作

- `ai_start()` 异步启动，避免 UI 卡顿。
- `ai_progress()` 提供进度条显示。
- `ai_cancel()` 可随时取消任务。
- `ai_lock()` / `ai_unlock()` 用于 AI 请求的权限占用。
