# future/ — 研究性愿景与设计草案

> **本目录保存设计与愿景，不代表实现状态。**
> 实现状态、路线图与版本裁定以 [../docs/STATUS.md](../docs/STATUS.md) 为唯一权威；
> 语言、API 与平台事实见 [../docs/API.md](../docs/API.md)。

仓库约定：`future/` 保存研究性愿景和设计草案；**已进入交付承诺的内容必须同步到版本路线图**（[`docs/STATUS.md`](../docs/STATUS.md)）**并补充可重复构建/测试命令**。

---

## 保留的 4 份指导文件

| 文件 | 性质 | 说明 |
| --- | --- | --- |
| [infiverse-inim-os-summary.md](infiverse-inim-os-summary.md) | 概念白皮书（§1–§101，21 503 行） | Infiverse 与 Inim OS 的完整概念与技术路线。**必须遵守其自身声明**：「除非特别注明，"规划"不代表已经实现」。其中 **§64 的证据等级（E0–E6）** 与 **§67 的需求追踪格式** 是全仓库诚实化口径的来源 |
| [愿景.md](愿景.md) | 愿景总纲 | 「Infiverse 完全构想」——宇宙常数宣言与多元宇宙协议的价值主张 |
| [优化路线pro.md](优化路线pro.md) | 性能设计 | 「Inimerse 极致优化计划书（完整版）」——算法层、编译器、JIT、汇编接口、基准标准、安全与性能平衡 |
| [multi-agent-coordination-bridge.md](multi-agent-coordination-bridge.md) | 设计草案（624 行，**design only**） | 「多智能体协调桥接层设计：Slipstream / G²CP ↔ CRP · Verse Layer · UPP」——把 G²CP 的言语行为信封与社会承诺**投影**到既有的 CRP（能力令牌 / 会话 / 事件环）、Verse Layer（哈希链 / 外部锚点）、UPP（宿主生命周期）之上，用 SLIP 作线上词法。**不新增第五套机制、不含引擎代码改动**；状态 = `设计未实现` / E1，路线图条目见 [`docs/STATUS.md`](../docs/STATUS.md) §9.3 |

## 已归档

其余 7 份（`Inim OS总纲.md`、`Inim OS特性.md`、`优化路线.md`、`函数式和错误处理.md`、`前沿.md`、`集合化.md`、`面对对象.md`）已移入 [archive/](archive/)，见 [archive/README.md](archive/README.md)。

## 使用约定

1. **不得**把本目录的内容表述为已实现功能。白皮书 §64.7 明确：「白皮书不得把讨论中方案写成已具备功能」。
2. 白皮书的章节编号（`§N`）在讨论中可直接引用，但引用时必须区分**规范要求**与**现状描述**。
3. 若要推动其中一项进入交付，先写成 [`docs/STATUS.md`](../docs/STATUS.md) 的路线图条目，并附上可重复的验收命令。
4. 白皮书 §77 的要求仍然有效：**下一阶段不再增加概念数量**，先选「一个最小 Layer、一个临时副本、一个服务器插件、一个客户端模组、一个训练沙盒」，验证创建·进入·同步·排空·恢复·撤销·回放。
