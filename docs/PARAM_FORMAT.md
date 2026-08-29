# Inimerse `.param` / `.params` 参数与生态组合规范

本文区分两类文件：

- `.params`：运行时参数覆盖，保持当前兼容语法（每行一个赋值）。
- `.param`：项目清单，描述 profile、包组合、资源 RID 和可审计的解析策略；不执行代码，也不发起网络请求。

## 1. `.params`（兼容格式）

```text
player.max_hp = 300
player.speed = 9.9
level.name = "cave"
debug = false
spawn = [10, 20, 30]
```

右侧使用 Inimerse 表达式；支持整数、浮点、布尔、字符串、数组和已有二元运算。`#` 到行尾是注释，语句可用换行或 `;` 分隔。加载器只接受顶层赋值，文件大小为 1 MiB 以内；解析失败时整文件拒绝，不产生部分状态。

运行时 API：

```im
load_params("game.params")       # 成功 1，失败 0
names = list_params()             # 用户命名空间（排除 u.*）
save_params("game.params")
save_params("game.params", "player.speed")
```

## 2. `.param` 项目清单（v0.4 提案）

`.param` 使用 YAML 子集，根键固定为 `version`、`project`、`sources`、`bundles`、`profiles` 和 `overrides`。未知键必须报错，防止拼写错误被静默忽略。

```yaml
version: 1
project:
  name: my-game
  engine: ">=0.3,<0.4"
  entry: src/main.im
sources:
  hub: https://hub.infiverse.dev
  policy: signed-only          # signed-only | allow-unsigned
bundles:
  game-2d:
    - package: renderer-2d
      version: "~2.1"
      rid: sha256:ab12...
    - package: bettergui
      version: ">=1.4,<2.0"
    - resource: sha256:8f3a...
      as: assets/ui/theme
profiles:
  default:
    use: [game-2d]
  server:
    use: [game-2d]
    disable: [bettergui/editor]
overrides:
  render.scale: 1.0
```

### 2.1 资源、包与 bundle

- **RID** 是 `sha256:<hex>` 内容摘要。同一 RID 在不同 bundle 或别名下只存一份。
- **package** 是带名称、版本、ABI、平台能力、依赖和导出符号的可发布单元；包内部通过 RID 引用文件。
- **bundle** 是有序依赖图，不是目录复制。解析器先展开 bundle，再按 RID 去重、求解版本交集并报告冲突。
- `resource ... as` 只建立逻辑挂载路径；同一逻辑路径映射不同 RID 时必须显式 `override`，且覆盖顺序可审计。

### 2.2 profile 与覆盖层

解析顺序固定为：内置默认值 → `default` profile → 命令行 profile → `overrides` → 环境变量白名单。`disable` 优先于依赖自动启用。环境变量采用双下划线映射，例如 `INIM_PLAYER__SPEED=10.5`，敏感字段只显示来源，不打印值。

推荐的环境变量白名单：

```yaml
overrides:
  env_allow: [INIM_PLAYER__SPEED, INIM_LOG_LEVEL]
```

## 3. 锁定文件 `.param.lock`

`inim bundle resolve project.param --lock project.param.lock` 生成锁定文件。锁文件只读提交，记录每个包的精确版本、RID、签名摘要、来源 Hub、目标平台和解析器版本：

```yaml
lockfile: 1
resolver: 0.4.0
project: my-game
platform: linux-x86_64
packages:
  renderer-2d:
    version: 2.1.3
    rid: sha256:ab12...
    source: https://hub.infiverse.dev
    signature: ed25519:...
    dependencies: []
```

发布/离线模式必须有锁文件；缺失、摘要不匹配、签名或 ABI 不匹配时，在替换锁文件前回滚全部安装操作。

## 4. 导入边界

`import` 用于模块符号（独立编译、可缓存、可检查 ABI）；`include` 用于源码/模板文本展开（仅允许本地、显式路径）。`.param` 只能选择已发布 package/bundle，不能隐式执行 `include`、调用 `say` 或运行任意表达式。

## 5. CLI 与诊断

```text
inim bundle resolve project.param --profile server --lock project.param.lock
inim bundle graph project.param --profile game-2d
inim bundle verify project.param.lock --offline
inim bundle gc --dry-run
inim params validate game.params
inim params print --resolved --redact
```

诊断至少包含：键路径、依赖链、冲突版本、RID、签名者、缓存命中/下载、最终挂载路径和修复建议。AI 可以生成候选清单，但不能绕过锁文件、签名或权限检查。

## 6. 生态演进路线

- **v0.4**：实现 YAML 子集解析、bundle 展开、RID 去重、semver 求解、锁文件和离线验证；保留 `.params` 原有 ABI。
- **v0.5**：包签名信任根、平台能力约束、增量更新、可复现构建和 `import/include` 依赖图可视化。
- **v3.1**：将 bundle/resource 与集合类型、虚拟文件系统统一；支持多挂载、内容寻址缓存和可证明的权限集合。

该分层让“整合包组合”可复现、可回滚、可离线审计，同时保持启动参数文件简单快速。
