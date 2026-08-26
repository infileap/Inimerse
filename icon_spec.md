# Inimerse 应用图标设计规格（供生图 AI 使用）
> **核对提示（2026-08-16）**：本文件为设计/规范文档，实现状态可能已变化；权威总览以 docs/API_REFERENCE.md 为准。


## 概念
- 主题：**深空中的服务器节点网络**（代表"联机"）+ **方块空间站**（代表 inimerse 引擎的世界/方块引擎）
- 中心元素：一个发光的**方块状空间站**（3D 等轴或扁平风格，金青色），周围 3~4 个**光点节点**用细线相连成网络
- 背景：深蓝紫色渐变星空 + 极淡星点

## 配色
- 背景：`#0d1130` → `#1a1d3f`（深蓝紫渐变）
- 主色（方块/空间站）：青色 `#4dd0e1` 发光
- 强调（节点高光）：金色 `#ffd75e`
- 连线：半透明白青色 `rgba(120,220,255,0.55)`

## 构图
- 圆角方形（rounded square，四角圆角约 20%），现代应用图标风格
- 中央方块空间站约占 45%，网络连线贯穿画面
- 简洁扁平 + 柔和辉光（soft glow），边缘清晰
- **不要文字**（图标内无字母/数字）

## 英文提示词（可直接粘贴给生图 AI）
```
App icon, rounded square, dark deep-blue space gradient background with subtle
stars, center: a small glowing block-shaped space station (cyan #4dd0e1, isometric
or flat), connected by thin glowing lines to 3-4 smaller network nodes (gold #ffd75e),
soft neon glow, modern flat design, crisp edges, high contrast, no text, 512x512 png
```

## 输出规格
1. 512×512 PNG（用于生成 .ico 的源图）
2. 需要转成多尺寸 ICO：16/24/32/48/64/128/256
   - 转换工具：ImageMagick（`magick icon.png -define icon:auto-resize=256,128,64,48,32,16 icon.ico`）
   - 或在线转换后放入 `D:\inimerse_stable\icon.ico`
3. 生成后请将 `icon.ico` 放到 `D:\inimerse_stable\icon.ico`，重新编译安装包即可带上图标
   - inimerse.exe / hl_bridge.exe 的资源图标可用 `windres` 或 Resource Hacker 替换
