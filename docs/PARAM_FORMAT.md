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
