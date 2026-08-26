# Inimerse 帧协议 v1 + 传输抽象（三模式联机基础）
> **核对提示（2026-08-16）**：本文件为设计/规范文档，实现状态可能已变化；权威总览以 docs/API_REFERENCE.md 为准。


## 1. 帧协议 v1（headless 热联机）
服务器 → 客户端：**一行 JSON**（`\n` 分隔），每渲染帧一条。

```json
{
  "f": 42,            // 帧号
  "w": 800, "h": 600, // 视口尺寸
  "t": [ {"x":10,"y":20,"s":"文本"}, ... ],      // 文本（屏幕坐标）
  "r": [ {"t":0,"x":..,"y":..,"w":..,"h":..,"c":0xFF0000,"s":".."}, ... ],
  "c": [ ... ],       // 控件（input/button/log 展开为形状）
  "sp": [ {"n":"hero","x":100,"y":100,"w":50,"h":40,"c":"hero.bmp"}, ... ]  // 精灵元数据
}
```

- `r[].t` 形状类型：0 矩形填充 / 1 边框 / 2 横线 / 3 竖线 / 4 文本 / 5 面板 / 6 圆角
- `sp[].c` 素材路径（客户端用本地素材缓存 sha256 引用解析，见 §5 verse）
- **关键帧语义**：每帧全量（天然可断线重连/接续）；慢客户端丢帧（非阻塞 send + WSAEWOULDBLOCK 丢弃），不积压

客户端 → 服务器：一行 JSON
```json
{"key":"left","down":1}
{"mouse":"move","x":123,"y":45}
{"mouse":"click","x":123,"y":45,"btn":1}
```

## 2. 传输后端接口（抽象层，预留扩展）
| 后端 | 状态 | 说明 |
|---|---|---|
| TCP | ✅ 现役（headless_server.c） | 逐帧 send，非阻塞+丢帧保护；端口 11490/11500/11510/11530 |
| HTTP | ✅ 现役（verse hub /v/id） | 冷联机下载 .vverse；headless http api（11470/11520/11540） |
| WebSocket | ⬜ 预留 | 网页客户端帧流（浏览器需 WS） |
| UDP | ⬜ 预留 | 低延迟帧流（丢包容忍），NAT 打洞用于 12.2 公网直连 |
| IPFS | ⬜ 预留 | verse://ipfs/<cid> 内容寻址（CID=SHA256 对齐 ref://） |

统一语义：`send_frame(json)` / `poll_input()` / `open_uri(verse://)`——新后端实现这三者即可接入。

## 3. 三模式总结
- **单机**：本地运行（✅ 已实现）
- **热联机**：服务器渲染推送（帧协议 v1 已就绪：文本/形状/控件/精灵全量帧 + 输入上行）
- **冷联机**：下载 .vverse 本地运行（✅ verse_open 校验/解包/启动；ref://sha256 素材复用）

## 4. 远程调试安全边界（文档化）
- 本地调试控制台（stdin）无网络面；`--safe` 已拦截注入（危险 builtin 清单见 CHANGES_20260815_safety）
- **未来远程 attach**（verse 控制通道）必须：`--debug-token <token>` + 只读命令 + safe_mode 强制；
  控制通道建议走独立端口 + token 握手（HMAC），禁止与游戏帧流复用
- 注入入口：`vm_exec` / `dbg_exec` 已注册为危险 builtin（safe_mode 下拒绝）

## 5. 素材引用链（跨层复用）
verse 素材缓存（universe/_cache/<sha256>，内容寻址）→ ref://sha256 引用（manifest.files）→
帧流 sp[].c 路径 → 客户端素材缓存解析 → im2d 精灵渲染。同一 sha256 全域唯一。
