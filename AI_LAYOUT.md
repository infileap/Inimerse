# Inimerse AI 排版助手 - 使用说明
> **核对提示（2026-08-16）**：本文件为设计/规范文档，实现状态可能已变化；权威总览以 docs/API_REFERENCE.md 为准。


## 概述
通过本地 OLLAMA 视觉模型,自动分析界面截图并给出排版改进建议。
无需联网,模型在本地运行。

## 架构
```
工作台/联机大厅 → [AI排版] → ai_layout_gbk.im (AI助手窗口)
    │
    ├─ gui_screenshot(path)  引擎截图当前界面 → PNG
    ├─ gui_layout_desc()     生成布局描述(文本块坐标/窗口尺寸)
    ├─ ai_ask(prompt, img, model)  → OLLAMA API (localhost:11434)
    └─ 显示 AI 建议到日志框
```

## 新增引擎内置
| 函数 | 说明 |
|------|------|
| `gui_screenshot(path)` | 保存当前 GUI 窗口为 PNG(返回 1/0)|
| `gui_layout_desc()` | 返回布局描述文本(窗口尺寸+所有文本块坐标)|
| `ai_ask(prompt, image_path, model)` | 调本地 OLLAMA,返回模型文本响应 |
| `round_rect(x,y,w,h,color,radius)` | 圆角矩形 |
| `gradient(x,y,w,h,c1,c2,horiz)` | 渐变填充 |
| `shadow(x,y,w,h,color)` | 阴影层 |
| `copy_file(src,dst)` | 复制文件(修复:之前未实现)|

## 使用步骤
1. 安装并启动 OLLAMA: `ollama pull llava:7b` (视觉模型, ~4.1GB)
2. 启动 Inimerse → 工作台 → [联机] → [AI排版]
3. 点 [AI分析当前界面] → 等待模型分析(约 5-30 秒)
4. 排版建议显示在日志框,可直接抄录修改

## 其他模型
- `llava:7b` — 默认,视觉+通用(推荐)
- `qwen2.5vl:7b` — 中文更强,体积更大
- 在 ai_layout.im 中修改 ai_ask 的 model 参数即可切换

## 布局描述格式示例
```
window=900x640 texts=12 shapes=8
text[0]=(330,18) "Infiverse 工作台 v7"
text[1]=(20,54) "项目名:"
...
```

## 依赖
- OLLAMA 0.32+ (用户已安装)
- 视觉模型 llava:7b (下载中)
- 引擎 inimerse.exe (已含 ai_ask/screenshot/layout_desc)
