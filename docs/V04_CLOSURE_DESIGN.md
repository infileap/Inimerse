# V0.4 闭包环境设计

当前函数值保存函数索引，尚不携带外层环境。闭包实现采用独立环境对象，避免把捕获值伪装成全局变量。

## 运行时模型

```text
FunctionValue {
    function_index
    Environment* environment   // 可为空
}

Environment {
    slot_count
    Value slots[]
}
```

`OP_MAKE_FUNC` 在创建时复制捕获槽；`OP_CALL_VALUE` 将环境指针挂入调用帧。函数体访问捕获变量使用新增的 `OP_LOAD_CAPTURE`/`OP_STORE_CAPTURE`，普通参数和局部变量仍走现有寄存器路径。

## 生命周期与 GC

- 函数值和调用帧都是环境根；
- GC 标记函数值的环境，再递归标记环境槽中的对象；
- 返回闭包后环境继续存活，最后一个引用释放时回收；
- `@mut_borrow` 捕获在 V0.4 暂不允许，先实现值捕获。

## 编译器阶段

1. lambda 编译前收集外层局部符号，生成捕获表；
2. lambda 字节码将捕获引用编译为 `OP_LOAD_CAPTURE`；
3. 创建指令携带捕获寄存器列表；
4. 无捕获 lambda 保持旧 `OP_MAKE_FUNC` 快路径和 ABI。

## 兼容与验收

旧函数值序列化格式保持不变；带环境的函数使用扩展记录。验收脚本：

```im
func make_adder(base) { return x -> x + base }
add = make_adder(2)
say add(3)  # 5
```
