# Inimerse 参数文件（`.param` / `.params`）

参数文件不是另一种运行时语言，而是由 `parse_program` 解析、再由 `vm_params_load` 校验的“赋值子集”。文件扩展名不参与解析；命令行默认读取 `params.params`，也可用 `--params <文件>` 指定，脚本中可调用 `load_params(path)`。

## 当前语法

每条语句必须是赋值：

```text
name = expression
namespace.key = expression
```

`name` 是标识符；成员参数当前推荐使用一层命名空间（例如 `player.speed`）。`=` 右侧使用 Inimerse 普通表达式，支持：

- 整数、浮点数、布尔值（`true`/`false`）和字符串（双引号）；
- 一元/二元算术与比较表达式；
- 数组字面量及由已有值组成的表达式（以当前编译器支持的表达式为准）。

空白可自由使用；行尾 `#` 后内容是注释；语句可用换行或 `;` 分隔。示例：

```text
# game.params
player.max_hp = 300
player.speed = 9.9       # comment
level.name = "cave"
debug = false
spawn = [10, 20, 30]
```

加载器限制：文件大小必须大于 0 且不超过 1 MiB；顶层只能出现赋值，不能出现 `if`、循环、函数、`say`、`include` 或副作用调用；赋值目标只能是标识符或成员表达式。解析或校验失败时整个文件不应用，`load_params` 返回 `0`，成功返回 `1`。启动时若默认文件存在，会在主程序编译前加载，使参数名可直接作为全局变量使用。

## 运行时 API

```im
load_params("game.params")       # -> 1 / 0
names = list_params()             # 返回命名空间参数名（排除 u.*）
save_params("game.params")        # 保存全部命名空间参数
save_params("game.params", "player.speed")
```

`save_params` 尽量保留原有注释和布局，只替换选定参数的值；未列出名称时保存所有带 `.` 的用户全局量。

## 面向 YAML 的升级建议（规划，不改变当前兼容性）

建议增加可选的 `.param.yaml` 格式，而不是直接改变现有格式：

```yaml
version: 1
profiles:
  default:
    player:
      max_hp: 300
      speed: 9.9
    level:
      name: cave
    debug: false
```

1. **严格类型与 schema**：允许 `type`、`min`、`max`、`enum`、`secret` 元数据；加载时拒绝未知字段和越界值。
2. **配置档案**：`--profile dev` 选择 `profiles.dev`，并以 `default` 为基线合并；合并规则固定为“深度键覆盖”，避免数组隐式拼接。
3. **环境变量覆盖**：仅允许显式白名单，例如 `INIM_PLAYER__SPEED=10.5`，并在诊断输出中标明来源，不打印 `secret` 值。
4. **安全与原子性**：YAML 只接受 JSON-compatible 标量、数组和映射；禁用锚点、标签和自定义构造器；写回采用临时文件 + 原子替换并保留备份。
5. **诊断**：错误包含文件、行列、键路径和期望类型；提供 `params validate`、`params print --resolved`，默认对秘密字段脱敏。
6. **兼容迁移**：提供 `params convert old.params --to yaml` 与反向导出；旧语法继续作为稳定 ABI，不允许 YAML 功能反向改变普通 `.params` 的求值语义。

该设计能获得 YAML 的层级与工具生态，同时保留当前参数文件“无副作用、可审计、启动前确定”的特性。

## 面向 Infiverse 生态的整合包组合（提案）

DeepSeek 分享讨论了以内容寻址 RID 管理资源、让同一资源出现在多个整合包中，以及“分化/逆分化”演化链。`.param` 适合描述“本项目如何选择这些资源”，不应把包文件本身复制进配置。建议在 v0.4 先加入一个受限的组合声明层，v3.1 再与虚拟文件系统和集合类型统一：

```text
# project.param（提案语法，当前版本不会解析）
bundle "bettergui" = [
  package "bettergui" version ">=1.4,<2.0",
  rid "sha256:8f3a..."
]
bundle "game-2d" = [
  include "bettergui",
  package "renderer-2d" version "~2.1",
  resource "rid:sha256:ab12..." as "assets/ui/theme"
]
profile "server" = {
  use "game-2d"
  disable "bettergui/editor"
}
```

### 组合模型

- **资源层**：每个 `.im`、skill、插件、图片或数据文件拥有不可变 `rid = sha256(content)`；RID 相同即同一资源，不因所在整合包或别名不同而重复占用磁盘。
- **包层**：包声明名称、版本、ABI、平台、依赖和导出符号；包只引用资源 RID，可独立更新。
- **组合层**：`bundle` 是有序依赖图，不是复制目录。解析器先展开组合，再做 RID 去重、版本求交和冲突报告。
- **项目层**：`.param` 只保存选择的 bundle、profile、开关和参数覆盖；解析结果应生成锁文件（例如 `.param.lock`），记录确切版本、RID、签名和来源 Hub。
- **挂载层**：`resource ... as` 只建立逻辑路径映射。一个 RID 可挂载到多个路径，写入时必须拒绝同一逻辑路径的不同 RID，除非显式 `override` 并通过优先级规则。

### 解析与更新规则

1. 读取 `default` profile，再按命令行选择的 profile 深度覆盖；`disable` 优先于依赖自动启用。
2. 依赖图按包 ID 拓扑排序；同一包多个版本只有在 semver 范围交集非空时合并，否则报出完整依赖链。
3. RID 内容先校验 SHA-256，再校验签名、ABI 和平台能力；任何失败都在提交新锁文件前回滚。
4. 更新包时只替换受影响 RID；引用该 RID 的所有 bundle 自动看到新内容，但锁文件必须显式刷新，避免不可重复构建。
5. 删除资源前检查反向引用；仍被任一锁文件或缓存索引引用时只做垃圾标记，不直接删除。

### 与现有 `.params` 的兼容边界

当前 `.params` 继续只接受赋值，例如 `render.scale = 1.0`。组合声明建议使用独立 `.param.yaml` 或未来的 `bundle` 专用清单，避免把依赖解析、网络访问和签名验证偷偷引入启动参数求值。兼容加载器可按以下优先级读取：

```text
project.param.lock  >  project.param.yaml  >  params.params  >  内置默认值
```

其中 `.param.lock` 只读、可提交版本控制；未锁定的远程包在严格/发布模式下应拒绝运行。这样既支持“一键导入整合包”，又保留参数文件的快速覆盖和离线可审计性。

### 建议的 CLI 与诊断

```text
inim bundle resolve project.param.yaml --lock project.param.lock
inim bundle graph --profile game-2d
inim bundle verify project.param.lock --offline
inim bundle gc --dry-run
```

诊断至少显示：冲突键、依赖链、RID、签名者、缓存命中/下载、最终逻辑挂载路径。AI 可生成组合建议，但只能写入候选清单，不能绕过锁文件、签名或权限检查。
