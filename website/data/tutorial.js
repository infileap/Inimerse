/* 由 website/scratch/run_tutorial.js 生成 —— 不要手改这个文件。
 * 每一步的 stdout / stderr / 退出码都是真跑出来的；页面上印的东西只能是这里的值。
 * ★ 每一步都带着它自己的 cwd —— 因为同一个脚本在两个 cwd 下的 stdout 不一样。
 * 重新生成：node website/scratch/run_tutorial.js
 */
window.INFIVERSE_TUTORIAL = {
  "ref": "b7253ee30fcda7b8ab12a75f872b6444f7e69639",
  "ref_date": "2026-10-17T18:48:05+08:00",
  "generated_by": "website/scratch/run_tutorial.js",
  "engine": "inimerse 0.5.2",
  "build": {
    "cmd": "cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j\"$(nproc)\"",
    "binary": "build/inimerse",
    "note": "README.md 的 \"Build the engine (Linux / POSIX)\" 一节。★ 用 `build/inimerse` 这个路径，不要用仓库根那份可能是旧的拷贝。"
  },
  "defaults": {
    "flags": [
      "--no-mods"
    ],
    "why": "★ 不加 `--no-mods` 时，从检出根跑会往 **stdout** 多印两行 `[mod] …`，从子目录跑就不印。模块装载是**环境**的读数，不是脚本的读数；`--no-mods` 是唯一让两个 cwd 给出同一份 stdout 的跑法。教程的每一步默认都用它，两个反例（t01b、以及 t00 之后的对照）单独列出来。"
  },
  "stabilize": {
    "rules": [
      {
        "id": "repo-path",
        "why": "把检出位置去掉：同一个命令在别人的机器上会打印他自己的路径。"
      }
    ],
    "note": "stabilized 字段记的是这一步施加过哪些规则；空数组表示 stdout / stderr 一个字都没动过。"
  },
  "failed": [],
  "diffProbe": {
    "pairs": [
      {
        "id": "t01 vs t01c",
        "stdoutSame": false,
        "stderrSame": false,
        "cwdSame": true,
        "flagsSame": false
      },
      {
        "id": "t01c vs t01b",
        "stdoutSame": false,
        "stderrSame": true,
        "cwdSame": false,
        "flagsSame": true
      }
    ],
    "allDifferent": true,
    "why": "★★ 两条反例步骤（t01c、t01b）必须**真的**与它们的对照在 stdout 上不同 —— 「跑通了」不蕴含「演示了它说要演示的那件事」。"
  },
  "dupProbe": {
    "duplicates": [],
    "clean": true,
    "why": "★ 四元组 (stdout, stderr, cwd, flags) 全表唯一。两笔读数完全相同 ⇒ 至少有一笔没在演示它说要演示的那个变量。"
  },
  "steps": [
    {
      "id": "t00",
      "teaches": "先确认引擎在不在，以及它是哪个版本",
      "kind": "argv",
      "cmd": "build/inimerse --version",
      "flags": [
        "--no-mods"
      ],
      "cwd": "root",
      "cwdNote": "检出根",
      "src": null,
      "stdout": "inimerse 0.5.2",
      "stderr": "",
      "rc": 0,
      "ok": true,
      "stabilized": [],
      "note": "这一条不带脚本。`--version` 的读数就是这一行。"
    },
    {
      "id": "t01",
      "teaches": "第一行输出：`say` 接一个表达式，**不需要括号**",
      "kind": "script",
      "cmd": "build/inimerse --no-mods website/scratch/.build/tut/t01.im",
      "flags": [
        "--no-mods"
      ],
      "cwd": "root",
      "cwdNote": "检出根",
      "src": "say \"hello\"\n",
      "stdout": "hello",
      "stderr": "",
      "rc": 0,
      "ok": true,
      "stabilized": [],
      "note": "`say` 是语句，不是函数调用 —— 写成 `say(\"hello\")` 是另一个东西。"
    },
    {
      "id": "t01c",
      "teaches": "★★ 同一个脚本、同一个 cwd、**去掉 `--no-mods`** ⇒ stdout 多出两行",
      "kind": "script",
      "cmd": "build/inimerse  website/scratch/.build/tut/t01.im",
      "flags": [],
      "cwd": "root",
      "cwdNote": "检出根",
      "src": "say \"hello\"\n",
      "stdout": "[mod] utils loaded\n[mod] self-check init=42\nhello",
      "stderr": "[infiverse mod] loaded (16 worlds, world 0 active)\n[verse_dist mod] VDP loaded (verse://<hub>/<id>)",
      "rc": 0,
      "ok": true,
      "stabilized": [],
      "note": "★ 与 t01 逐字对比：脚本一个字没改、cwd 一个字没改，只少了 `--no-mods`。多出来的 `[mod] …` 两行走 **stdout**（`[infiverse mod] …` 那两行走 stderr）。"
    },
    {
      "id": "t01b",
      "teaches": "★★ 同一个脚本、**换一个 cwd** ⇒ 那两行不见了（脚本没改）",
      "kind": "script",
      "cmd": "build/inimerse  t01.im",
      "flags": [],
      "cwd": "subdir",
      "cwdNote": "website/scratch/.build/tut/（一个子目录）",
      "src": "say \"hello\"\n",
      "stdout": "hello",
      "stderr": "[infiverse mod] loaded (16 worlds, world 0 active)\n[verse_dist mod] VDP loaded (verse://<hub>/<id>)",
      "rc": 0,
      "ok": true,
      "stabilized": [],
      "note": "★ 与 t01c 逐字对比：两边都不带 `--no-mods`，只有 cwd 变了。⇒ 那两行是**环境**的读数，不是这个脚本的读数。这就是每一步都要印 `cwd` 的理由。"
    },
    {
      "id": "t03",
      "teaches": "变量与算术：`=` 赋值，`str()` 把数字变成能印的东西",
      "kind": "script",
      "cmd": "build/inimerse --no-mods website/scratch/.build/tut/t03.im",
      "flags": [
        "--no-mods"
      ],
      "cwd": "root",
      "cwdNote": "检出根",
      "src": "x = 1 + 2\nsay str(x)\n",
      "stdout": "3",
      "stderr": "",
      "rc": 0,
      "ok": true,
      "stabilized": [],
      "note": "`say x` 也能跑，但印出来的东西与 `say str(x)` 不是同一份保证 —— 教程统一用 `str()`。"
    },
    {
      "id": "t04",
      "teaches": "字符串拼接用 `+`（不是 `..`，也不是模板串）",
      "kind": "script",
      "cmd": "build/inimerse --no-mods website/scratch/.build/tut/t04.im",
      "flags": [
        "--no-mods"
      ],
      "cwd": "root",
      "cwdNote": "检出根",
      "src": "name = \"inim\"\nsay \"hello, \" + name\n",
      "stdout": "hello, inim",
      "stderr": "",
      "rc": 0,
      "ok": true,
      "stabilized": [],
      "note": ""
    },
    {
      "id": "t05",
      "teaches": "`if` / `else`：块用花括号，条件**不用括号**",
      "kind": "script",
      "cmd": "build/inimerse --no-mods website/scratch/.build/tut/t05.im",
      "flags": [
        "--no-mods"
      ],
      "cwd": "root",
      "cwdNote": "检出根",
      "src": "x = 7\nif x > 5 { say \"big\" } else { say \"small\" }\n",
      "stdout": "big",
      "stderr": "",
      "rc": 0,
      "ok": true,
      "stabilized": [],
      "note": "首行与 `if` 不能拆行 —— 后缀条件是按**同一行**消歧的（`docs/SYNTAX.md` §4.2）。"
    },
    {
      "id": "t06",
      "teaches": "`while` 循环，以及 `+=`",
      "kind": "script",
      "cmd": "build/inimerse --no-mods website/scratch/.build/tut/t06.im",
      "flags": [
        "--no-mods"
      ],
      "cwd": "root",
      "cwdNote": "检出根",
      "src": "i = 0\nwhile i < 3 { say str(i) ; i += 1 }\n",
      "stdout": "0\n1\n2",
      "stderr": "",
      "rc": 0,
      "ok": true,
      "stabilized": [],
      "note": ""
    },
    {
      "id": "t07",
      "teaches": "`for x in range(a, b)` —— 左闭右开，且 `range()` 只在 `for … in` 里有效",
      "kind": "script",
      "cmd": "build/inimerse --no-mods website/scratch/.build/tut/t07.im",
      "flags": [
        "--no-mods"
      ],
      "cwd": "root",
      "cwdNote": "检出根",
      "src": "for i in range(1, 4) { say str(i) }\n",
      "stdout": "1\n2\n3",
      "stderr": "",
      "rc": 0,
      "ok": true,
      "stabilized": [],
      "note": "★ `range` 是 `for … in` 的特判，不是普通内建（`docs/SYNTAX.md` §4.2）。★★ 这里用 `range(1, 4)` 而不是 `range(3)`：后者与上一步 t06 印出来的东西**逐字相同**（`0/1/2`），于是读者无法从输出上区分这两种循环 —— 本生成器的四元组探针就是为这个存在的。"
    },
    {
      "id": "t08",
      "teaches": "`func` 定义函数，`return` 返回值",
      "kind": "script",
      "cmd": "build/inimerse --no-mods website/scratch/.build/tut/t08.im",
      "flags": [
        "--no-mods"
      ],
      "cwd": "root",
      "cwdNote": "检出根",
      "src": "func dbl(n) { return n * 2 }\nsay str(dbl(21))\n",
      "stdout": "42",
      "stderr": "",
      "rc": 0,
      "ok": true,
      "stabilized": [],
      "note": "★ **调用时参数个数完全不校验**：少传得 `nil`、多传被静默忽略，两个方向都不报错。"
    },
    {
      "id": "t09",
      "teaches": "列表、`len()`、`push()`",
      "kind": "script",
      "cmd": "build/inimerse --no-mods website/scratch/.build/tut/t09.im",
      "flags": [
        "--no-mods"
      ],
      "cwd": "root",
      "cwdNote": "检出根",
      "src": "xs = [1, 2, 3]\nsay str(len(xs))\npush(xs, 4)\nsay str(xs)\n",
      "stdout": "3\n[1, 2, 3, 4]",
      "stderr": "",
      "rc": 0,
      "ok": true,
      "stabilized": [],
      "note": "★ `[1, 2, 3]` 是**列表**，而 `(1, 2, 3)` 是**区间** —— 只差一个括号形状。"
    },
    {
      "id": "t10",
      "teaches": "★ 一个真的错误长什么样（退出码不是 0）",
      "kind": "script",
      "cmd": "build/inimerse --no-mods website/scratch/.build/tut/t10.im",
      "flags": [
        "--no-mods"
      ],
      "cwd": "root",
      "cwdNote": "检出根",
      "src": "type(5)\n",
      "stdout": "",
      "stderr": "Error at line 1: expected 'type name', but got '(' (type 93)",
      "rc": 1,
      "ok": true,
      "stabilized": [],
      "note": "★ 错误走 **stderr**、stdout 是**空的**、退出码 **1**。`type` 是保留字，`type(x)` 是解析错误；唯一的替代是成员访问 `x.type`。"
    },
    {
      "id": "t11",
      "teaches": "★★ 一个**不报错**的错：未声明的名字给 `nil`",
      "kind": "script",
      "cmd": "build/inimerse --no-mods website/scratch/.build/tut/t11.im",
      "flags": [
        "--no-mods"
      ],
      "cwd": "root",
      "cwdNote": "检出根",
      "src": "say str(undefined_thing)\n",
      "stdout": "nil",
      "stderr": "",
      "rc": 0,
      "ok": true,
      "stabilized": [],
      "note": "★ 与上一步并排看：这一步 stdout 有输出、stderr 空、**退出码 0** —— 而它读的是一个从来没被声明过的名字。本仓的语言在这里**不报错**。"
    },
    {
      "id": "t12",
      "teaches": "编译成 `.inim`，再跑那个产物",
      "kind": "compile",
      "cmd": "build/inimerse --no-mods compile website/scratch/.build/tut/t12.im <同一目录里的 .inim>",
      "flags": [
        "--no-mods"
      ],
      "cwd": "root",
      "cwdNote": "检出根",
      "src": "func dbl(n) { return n * 2 }\nsay str(dbl(21))\n",
      "stdout": "compiled: <repo>/website/scratch/.build/tut/t12.im -> <repo>/website/scratch/.build/tut/t12.inim",
      "stderr": "",
      "rc": 0,
      "ok": true,
      "stabilized": [
        {
          "id": "repo-path",
          "n": 2
        }
      ],
      "note": "★ 输出里的路径是**绝对路径**，已按声明的规则换成 `<repo>`（见 `stabilized`）。"
    }
  ]
};
