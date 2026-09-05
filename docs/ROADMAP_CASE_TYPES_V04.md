# Inimerse V0.4 集合类型与模式匹配路线图

本路线图将集合化类型、结构化错误、`case`/`case try` 和事件分发视为同一条语言主线。目标是让类型、错误和模式共享同一个约束模型，同时保持运行时值的高效原生表示。

## 设计不变量

1. 类型在语义上是值的集合；`x be T` 表示 `x in T`。
2. 集合表达式使用 `+` 并集、`*` 交集、`-` 差集，谓词约束统一使用 `|`。
3. `case` 分支使用 `:`，不再使用 `=>`。
4. `case try` 是原生 `Result(T, E)` 分解和模式匹配，不是外糖脱糖。
5. 错误是结构化值，错误类型是集合；VM 错误和业务错误共享值模型但属于不同集合。
6. 类型集合是编译期/运行时元数据，不要求每个运行时值物理包装成集合对象。

## V0.4-A：集合类型内核

### 交付

- `TypeSet` 描述符：枚举、基础类型、区间、并集、交集、差集、谓词和记录集合。
- 类型注册表：内置集合与用户 `type Name = expression` 别名。
- 成员和子集 API：`contains`、`subset`、`intersects`、规范化与缓存。
- `be` 声明接入成员校验；静态可证明时不插入运行时检查。

### 验收

```im
type Byte = Z * [0~255]
type Positive = {x in Z | x > 0}
x be Byte = 255
```

编译期常量必须通过；越界常量必须诊断；动态值必须生成明确的集合约束错误。

## V0.4-B：预设错误集合

### 有限集合自动枚举（枚举系统化）

任意有限集合都可以作为枚举使用，不限于错误或线程状态：
源码中保留符号名，运行时根据成员数自动选择 `uint8`\uff08≤256）、`uint16`\uff08≤65536）或普通编码。枚举描述符保留名称↔编码双向映射，并为 `case` 穷尽性、调试和序列化提供元数据。未知成员必须拒绝，序列化优先使用类型名+成员名以保持跨版本稳定。

### 预设集合

```im
type FileError = {not_found, permission_denied, disk_full, invalid_path}
type ArithmeticVMError = {division_by_zero, numeric_overflow, invalid_numeric_operation}
type MemoryVMError = {index_out_of_range, allocation_failed, invalid_reference}
type TypeVMError = {value_not_callable, type_mismatch, invalid_conversion}
type VMError = ArithmeticVMError + MemoryVMError + TypeVMError
```

### 交付

- 错误构造器携带 `kind`、消息和可选上下文。
- `err(e)` 的 `e` 必须可以是任意已注册错误集合的成员。
- VM `throw` 从字符串升级为结构化 VM 错误值，同时保留可读消息。
- 业务 `Result(T, E)` 与 VM 异常边界明确：业务错误可匹配，未捕获 VM 错误才终止执行。

## V0.4-C：原生模式匹配

### 第一阶段语法

```im
case value {
    0: say "zero"
    n in Z+: say "positive"
    n | n < 0: say "negative"
    _: say "other"
}
```

AST 应统一保存 `pattern`、可选 `guard` 和分支体。集合模式复用 `TypeSet` 成员检查，不另写一套比较逻辑。

### `case try`

```im
case try read_file(path) {
    ok(content): use(content)
    err(not_found): create_default()
    err(e) | e in FileError: handle_file(e)
    err(e): handle_unknown(e)
}
```

编译器必须直接生成 Result 判别、构造器匹配、集合成员检查和守卫跳转。

## V0.4-D：事件统一分发

事件监听复用同一套模式 AST：

```im
on Player: damaged: update_hud(Player.hp)

on Player {
    case event {
        "damaged": update_hud(Player.hp)
        "healed": say "healed"
        _: nil
    }
}
```

单行监听和块监听最终都编译为事件处理函数；事件过滤使用集合成员和 `|` 守卫。

## V0.5：结构与安全扩展

- 元组、数组、字典和 Eidos 对象解构。
- 嵌套模式和 `as` 整体绑定。
- `or`/`and` 模式组合与模式别名。
- 基于集合的穷尽性、不可达分支和无限集合通配检查。
- 与所有权/借用模式、效应标注集成。
- `case` 作为表达式并接入管道。

## 实施顺序与当前状态

当前仓库已有集合字面量、区间、集合推导、Result 原语、用户类型注册表、预设 VM 错误集合、结构化普通 `case` 和原生 `case try` 路径；深层结构模式、完整穷尽诊断及异常边界语义仍在 V0.4 迭代。

实现顺序固定为：

1. `TypeSet` 元数据和成员 API。
2. `type` 别名与 `be` 校验。
3. 预设错误集合和结构化 VM 错误。
4. 模式 AST、`|` 守卫和 `:` 分支语法。
5. 原生 `case try`。
6. 事件监听复用模式引擎。
7. 结构解构、穷尽检查、所有权和效应扩展。

每一步都必须有独立回归测试、编译器诊断测试和运行时行为测试；文档状态只有在对应证据存在后才能标记为“已实现”。
