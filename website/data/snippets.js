/* 由 website/scratch/run_snippets.js 生成 —— 不要手改这个文件。
 * 每一条输出都是真跑出来的；页面上印的东西只能是这里的值。
 * 重新生成：node website/scratch/run_snippets.js
 */
window.INFIVERSE_SNIPPETS = {
  "ref": "d78a0a3215034ad30ccbd569e153cbcf6e5534df",
  "ref_date": "2026-10-09T20:17:26+08:00",
  "generated_by": "website/scratch/run_snippets.js",
  "filter": {
    "noise": [
      "/^\\[mod\\] /",
      "/^\\[infiverse mod\\] /",
      "/^\\[verse_dist mod\\] /"
    ],
    "note": "每次运行固定的模块装载信息已从 stdout / stderr 中滤除，被滤掉的行逐字记在 noise 字段里（没有丢，只是折叠了）。首尾空行也已去掉。",
    "stabilize": [
      {
        "id": "repo-path",
        "langs": null,
        "why": "把检出位置去掉：同一个片段在别人的机器上会打印他自己的路径。"
      },
      {
        "id": "rust-panic-thread-id",
        "langs": [
          "rust"
        ],
        "why": "rustc 1.99 的 panic 消息里带着 OS 线程号（实测每次运行 +1：26 / 27 / 28）。它是进程属性，不是语言的语义，所以按声明的规则换成 <tid>。"
      }
    ],
    "stabilizeNote": "stabilized 字段记的是这一格施加过哪些稳定化规则（含命中次数与理由）；verbatim=true 表示 stdout / stderr 就是进程原样输出，一个字都没动过。stable 判的是稳定化之后的文本，raw_stable 判的是施加规则之前 —— 「stable=true 而 raw_stable=false」的形状，正是稳定化救回来的那一格。★ 不保存「未施加规则的逐字原文」：带 OS 线程号的 panic 消息两次运行不是同一个字符串，存一个样本会把一次偶然写成事实，也会让这份产物每次都不一样。"
  },
  "toolchain": {
    "Inimerse": {
      "cmd": "build/inimerse --version",
      "out": "inimerse 0.5.2"
    },
    "Python": {
      "cmd": "python3 --version",
      "out": "Python 3.14.4"
    },
    "JavaScript": {
      "cmd": "node --version",
      "out": "v24.20.0"
    },
    "Rust": {
      "cmd": "~/.rustup/toolchains/stable-x86_64-unknown-linux-gnu/bin/rustc --version",
      "out": "rustc 1.99.0 (b940084d7 2026-09-28)",
      "note": "★ 必须写全路径：这台机器上 `rustc` / `cargo` 走 rustup shim 是坏的（cannot create transient scope: DBus error ... FileNotFound），照抄 `rustc` 的人会看到一个 DBus 报错，而他会以为是 Rust 的问题。"
    },
    "C": {
      "cmd": "cc --version",
      "out": "cc (Ubuntu 15.2.0-16ubuntu1) 15.2.0"
    }
  },
  "languages": [
    "inimerse",
    "python",
    "javascript",
    "rust",
    "c"
  ],
  "labels": {
    "inimerse": "Inimerse",
    "python": "Python",
    "javascript": "JavaScript",
    "rust": "Rust",
    "c": "C"
  },
  "topics": [
    {
      "id": "div-zero-int",
      "title": "整数除以零",
      "question": "整数除以零，失败发生在哪一步 —— 编译期、运行期，还是根本不失败？",
      "cells": {
        "inimerse": {
          "lang": "inimerse",
          "file": "website/scratch/snippets/div-zero-int.im",
          "source": "say 1 / 0\n",
          "stable": true,
          "raw_stable": true,
          "cellNote": null,
          "build": null,
          "run": {
            "cmd": "build/inimerse run website/scratch/snippets/div-zero-int.im",
            "rc": 1,
            "signal": null,
            "stdout": "",
            "stderr": "[exception] uncaught: division_by_zero\n  at ip=3 frames=0",
            "noise": [
              "[mod] utils loaded",
              "[mod] self-check init=42",
              "[infiverse mod] loaded (16 worlds, world 0 active)",
              "[verse_dist mod] VDP loaded (verse://<hub>/<id>)"
            ],
            "stabilized": [],
            "verbatim": false
          },
          "status": "ran"
        },
        "python": {
          "lang": "python",
          "file": "website/scratch/snippets/div-zero-int.py",
          "source": "print(1 / 0)\n",
          "stable": true,
          "raw_stable": true,
          "cellNote": null,
          "build": null,
          "run": {
            "cmd": "python3 website/scratch/snippets/div-zero-int.py",
            "rc": 1,
            "signal": null,
            "stdout": "",
            "stderr": "Traceback (most recent call last):\n  File \"<repo>/website/scratch/snippets/div-zero-int.py\", line 1, in <module>\n    print(1 / 0)\n          ~~^~~\nZeroDivisionError: division by zero",
            "noise": [],
            "stabilized": [
              {
                "id": "repo-path",
                "why": "把检出位置去掉：同一个片段在别人的机器上会打印他自己的路径。",
                "count": 1
              }
            ],
            "verbatim": false
          },
          "status": "ran"
        },
        "javascript": {
          "lang": "javascript",
          "file": "website/scratch/snippets/div-zero-int.js",
          "source": "console.log(1 / 0);\n",
          "stable": true,
          "raw_stable": true,
          "cellNote": null,
          "build": null,
          "run": {
            "cmd": "node website/scratch/snippets/div-zero-int.js",
            "rc": 0,
            "signal": null,
            "stdout": "Infinity",
            "stderr": "",
            "noise": [],
            "stabilized": [],
            "verbatim": true
          },
          "status": "ran"
        },
        "rust": {
          "lang": "rust",
          "file": "website/scratch/snippets/div-zero-int.rs",
          "source": "fn main() { println!(\"{}\", 1 / 0); }\n",
          "stable": true,
          "raw_stable": true,
          "cellNote": "★ 这一格没有运行输出：编译没有通过。",
          "build": {
            "cmd": "~/.rustup/toolchains/stable-x86_64-unknown-linux-gnu/bin/rustc --edition 2021 -O website/scratch/snippets/div-zero-int.rs -o website/scratch/.build/div-zero-int",
            "rc": 1,
            "signal": null,
            "stdout": "",
            "stderr": "error: this operation will panic at runtime\n --> website/scratch/snippets/div-zero-int.rs:1:28\n  |\n1 | fn main() { println!(\"{}\", 1 / 0); }\n  |                            ^^^^^ attempt to divide `1_i32` by zero\n  |\n  = note: `#[deny(unconditional_panic)]` on by default\n\nerror: aborting due to 1 previous error",
            "noise": [],
            "stabilized": [],
            "verbatim": true
          },
          "run": null,
          "status": "build-failed"
        },
        "c": {
          "lang": "c",
          "file": "website/scratch/snippets/div-zero-int.c",
          "source": "#include <stdio.h>\nint main(void) { printf(\"%d\\n\", 1 / 0); return 0; }\n",
          "stable": true,
          "raw_stable": true,
          "cellNote": null,
          "build": {
            "cmd": "cc -O2 website/scratch/snippets/div-zero-int.c -o website/scratch/.build/div-zero-int",
            "rc": 0,
            "signal": null,
            "stdout": "",
            "stderr": "website/scratch/snippets/div-zero-int.c: In function ‘main’:\nwebsite/scratch/snippets/div-zero-int.c:2:35: warning: division by zero [-Wdiv-by-zero]\n    2 | int main(void) { printf(\"%d\\n\", 1 / 0); return 0; }\n      |                                   ^",
            "noise": [],
            "stabilized": [],
            "verbatim": true
          },
          "run": {
            "cmd": "website/scratch/.build/div-zero-int",
            "rc": null,
            "signal": "SIGILL",
            "stdout": "",
            "stderr": "",
            "noise": [],
            "stabilized": [],
            "verbatim": true
          },
          "status": "ran"
        }
      }
    },
    {
      "id": "div-zero-float",
      "title": "浮点除以零",
      "question": "浮点除以零和整数除以零，是同一件事吗？",
      "cells": {
        "inimerse": {
          "lang": "inimerse",
          "file": "website/scratch/snippets/div-zero-float.im",
          "source": "say 1.0 / 0\n",
          "stable": true,
          "raw_stable": true,
          "cellNote": null,
          "build": null,
          "run": {
            "cmd": "build/inimerse run website/scratch/snippets/div-zero-float.im",
            "rc": 0,
            "signal": null,
            "stdout": "inf",
            "stderr": "",
            "noise": [
              "[mod] utils loaded",
              "[mod] self-check init=42",
              "[infiverse mod] loaded (16 worlds, world 0 active)",
              "[verse_dist mod] VDP loaded (verse://<hub>/<id>)"
            ],
            "stabilized": [],
            "verbatim": false
          },
          "status": "ran"
        },
        "python": {
          "lang": "python",
          "file": "website/scratch/snippets/div-zero-float.py",
          "source": "print(1.0 / 0)\n",
          "stable": true,
          "raw_stable": true,
          "cellNote": null,
          "build": null,
          "run": {
            "cmd": "python3 website/scratch/snippets/div-zero-float.py",
            "rc": 1,
            "signal": null,
            "stdout": "",
            "stderr": "Traceback (most recent call last):\n  File \"<repo>/website/scratch/snippets/div-zero-float.py\", line 1, in <module>\n    print(1.0 / 0)\n          ~~~~^~~\nZeroDivisionError: division by zero",
            "noise": [],
            "stabilized": [
              {
                "id": "repo-path",
                "why": "把检出位置去掉：同一个片段在别人的机器上会打印他自己的路径。",
                "count": 1
              }
            ],
            "verbatim": false
          },
          "status": "ran"
        },
        "javascript": {
          "lang": "javascript",
          "file": "website/scratch/snippets/div-zero-float.js",
          "source": "console.log(1.0 / 0);\n",
          "stable": true,
          "raw_stable": true,
          "cellNote": null,
          "build": null,
          "run": {
            "cmd": "node website/scratch/snippets/div-zero-float.js",
            "rc": 0,
            "signal": null,
            "stdout": "Infinity",
            "stderr": "",
            "noise": [],
            "stabilized": [],
            "verbatim": true
          },
          "status": "ran"
        },
        "rust": {
          "lang": "rust",
          "file": "website/scratch/snippets/div-zero-float.rs",
          "source": "fn main() { println!(\"{}\", 1.0_f64 / 0.0_f64); }\n",
          "stable": true,
          "raw_stable": true,
          "cellNote": null,
          "build": {
            "cmd": "~/.rustup/toolchains/stable-x86_64-unknown-linux-gnu/bin/rustc --edition 2021 -O website/scratch/snippets/div-zero-float.rs -o website/scratch/.build/div-zero-float",
            "rc": 0,
            "signal": null,
            "stdout": "",
            "stderr": "",
            "noise": [],
            "stabilized": [],
            "verbatim": true
          },
          "run": {
            "cmd": "website/scratch/.build/div-zero-float",
            "rc": 0,
            "signal": null,
            "stdout": "inf",
            "stderr": "",
            "noise": [],
            "stabilized": [],
            "verbatim": true
          },
          "status": "ran"
        },
        "c": {
          "lang": "c",
          "file": "website/scratch/snippets/div-zero-float.c",
          "source": "#include <stdio.h>\nint main(void) { printf(\"%f\\n\", 1.0 / 0.0); return 0; }\n",
          "stable": true,
          "raw_stable": true,
          "cellNote": null,
          "build": {
            "cmd": "cc -O2 website/scratch/snippets/div-zero-float.c -o website/scratch/.build/div-zero-float",
            "rc": 0,
            "signal": null,
            "stdout": "",
            "stderr": "",
            "noise": [],
            "stabilized": [],
            "verbatim": true
          },
          "run": {
            "cmd": "website/scratch/.build/div-zero-float",
            "rc": 0,
            "signal": null,
            "stdout": "inf",
            "stderr": "",
            "noise": [],
            "stabilized": [],
            "verbatim": true
          },
          "status": "ran"
        }
      }
    },
    {
      "id": "mod-zero",
      "title": "取模零",
      "question": "取模零与整除零，抛出来的东西一样吗？",
      "cells": {
        "inimerse": {
          "lang": "inimerse",
          "file": "website/scratch/snippets/mod-zero.im",
          "source": "say 1 % 0\n",
          "stable": true,
          "raw_stable": true,
          "cellNote": null,
          "build": null,
          "run": {
            "cmd": "build/inimerse run website/scratch/snippets/mod-zero.im",
            "rc": 1,
            "signal": null,
            "stdout": "",
            "stderr": "[exception] uncaught: division_by_zero\n  at ip=3 frames=0",
            "noise": [
              "[mod] utils loaded",
              "[mod] self-check init=42",
              "[infiverse mod] loaded (16 worlds, world 0 active)",
              "[verse_dist mod] VDP loaded (verse://<hub>/<id>)"
            ],
            "stabilized": [],
            "verbatim": false
          },
          "status": "ran"
        },
        "python": {
          "lang": "python",
          "file": "website/scratch/snippets/mod-zero.py",
          "source": "print(1 % 0)\n",
          "stable": true,
          "raw_stable": true,
          "cellNote": null,
          "build": null,
          "run": {
            "cmd": "python3 website/scratch/snippets/mod-zero.py",
            "rc": 1,
            "signal": null,
            "stdout": "",
            "stderr": "Traceback (most recent call last):\n  File \"<repo>/website/scratch/snippets/mod-zero.py\", line 1, in <module>\n    print(1 % 0)\n          ~~^~~\nZeroDivisionError: division by zero",
            "noise": [],
            "stabilized": [
              {
                "id": "repo-path",
                "why": "把检出位置去掉：同一个片段在别人的机器上会打印他自己的路径。",
                "count": 1
              }
            ],
            "verbatim": false
          },
          "status": "ran"
        },
        "javascript": {
          "lang": "javascript",
          "file": "website/scratch/snippets/mod-zero.js",
          "source": "console.log(1 % 0);\n",
          "stable": true,
          "raw_stable": true,
          "cellNote": null,
          "build": null,
          "run": {
            "cmd": "node website/scratch/snippets/mod-zero.js",
            "rc": 0,
            "signal": null,
            "stdout": "NaN",
            "stderr": "",
            "noise": [],
            "stabilized": [],
            "verbatim": true
          },
          "status": "ran"
        },
        "rust": {
          "lang": "rust",
          "file": "website/scratch/snippets/mod-zero.rs",
          "source": "fn main() { println!(\"{}\", 1 % 0); }\n",
          "stable": true,
          "raw_stable": true,
          "cellNote": "★ 这一格没有运行输出：编译没有通过。",
          "build": {
            "cmd": "~/.rustup/toolchains/stable-x86_64-unknown-linux-gnu/bin/rustc --edition 2021 -O website/scratch/snippets/mod-zero.rs -o website/scratch/.build/mod-zero",
            "rc": 1,
            "signal": null,
            "stdout": "",
            "stderr": "error: this operation will panic at runtime\n --> website/scratch/snippets/mod-zero.rs:1:28\n  |\n1 | fn main() { println!(\"{}\", 1 % 0); }\n  |                            ^^^^^ attempt to calculate the remainder of `1_i32` with a divisor of zero\n  |\n  = note: `#[deny(unconditional_panic)]` on by default\n\nerror: aborting due to 1 previous error",
            "noise": [],
            "stabilized": [],
            "verbatim": true
          },
          "run": null,
          "status": "build-failed"
        },
        "c": {
          "lang": "c",
          "file": "website/scratch/snippets/mod-zero.c",
          "source": "#include <stdio.h>\nint main(void) { printf(\"%d\\n\", 1 % 0); return 0; }\n",
          "stable": true,
          "raw_stable": true,
          "cellNote": null,
          "build": {
            "cmd": "cc -O2 website/scratch/snippets/mod-zero.c -o website/scratch/.build/mod-zero",
            "rc": 0,
            "signal": null,
            "stdout": "",
            "stderr": "website/scratch/snippets/mod-zero.c: In function ‘main’:\nwebsite/scratch/snippets/mod-zero.c:2:35: warning: division by zero [-Wdiv-by-zero]\n    2 | int main(void) { printf(\"%d\\n\", 1 % 0); return 0; }\n      |                                   ^",
            "noise": [],
            "stabilized": [],
            "verbatim": true
          },
          "run": {
            "cmd": "website/scratch/.build/mod-zero",
            "rc": null,
            "signal": "SIGILL",
            "stdout": "",
            "stderr": "",
            "noise": [],
            "stabilized": [],
            "verbatim": true
          },
          "status": "ran"
        }
      }
    },
    {
      "id": "int-div-truncation",
      "title": "整数除法的结果类型",
      "question": "两个整数相除，结果是整数还是浮点？",
      "cells": {
        "inimerse": {
          "lang": "inimerse",
          "file": "website/scratch/snippets/int-div-truncation.im",
          "source": "say 7 / 2\n",
          "stable": true,
          "raw_stable": true,
          "cellNote": null,
          "build": null,
          "run": {
            "cmd": "build/inimerse run website/scratch/snippets/int-div-truncation.im",
            "rc": 0,
            "signal": null,
            "stdout": "3.5",
            "stderr": "",
            "noise": [
              "[mod] utils loaded",
              "[mod] self-check init=42",
              "[infiverse mod] loaded (16 worlds, world 0 active)",
              "[verse_dist mod] VDP loaded (verse://<hub>/<id>)"
            ],
            "stabilized": [],
            "verbatim": false
          },
          "status": "ran"
        },
        "python": {
          "lang": "python",
          "file": "website/scratch/snippets/int-div-truncation.py",
          "source": "print(7 / 2)\n",
          "stable": true,
          "raw_stable": true,
          "cellNote": null,
          "build": null,
          "run": {
            "cmd": "python3 website/scratch/snippets/int-div-truncation.py",
            "rc": 0,
            "signal": null,
            "stdout": "3.5",
            "stderr": "",
            "noise": [],
            "stabilized": [],
            "verbatim": true
          },
          "status": "ran"
        },
        "javascript": {
          "lang": "javascript",
          "file": "website/scratch/snippets/int-div-truncation.js",
          "source": "console.log(7 / 2);\n",
          "stable": true,
          "raw_stable": true,
          "cellNote": null,
          "build": null,
          "run": {
            "cmd": "node website/scratch/snippets/int-div-truncation.js",
            "rc": 0,
            "signal": null,
            "stdout": "3.5",
            "stderr": "",
            "noise": [],
            "stabilized": [],
            "verbatim": true
          },
          "status": "ran"
        },
        "rust": {
          "lang": "rust",
          "file": "website/scratch/snippets/int-div-truncation.rs",
          "source": "fn main() { println!(\"{}\", 7 / 2); }\n",
          "stable": true,
          "raw_stable": true,
          "cellNote": null,
          "build": {
            "cmd": "~/.rustup/toolchains/stable-x86_64-unknown-linux-gnu/bin/rustc --edition 2021 -O website/scratch/snippets/int-div-truncation.rs -o website/scratch/.build/int-div-truncation",
            "rc": 0,
            "signal": null,
            "stdout": "",
            "stderr": "",
            "noise": [],
            "stabilized": [],
            "verbatim": true
          },
          "run": {
            "cmd": "website/scratch/.build/int-div-truncation",
            "rc": 0,
            "signal": null,
            "stdout": "3",
            "stderr": "",
            "noise": [],
            "stabilized": [],
            "verbatim": true
          },
          "status": "ran"
        },
        "c": {
          "lang": "c",
          "file": "website/scratch/snippets/int-div-truncation.c",
          "source": "#include <stdio.h>\nint main(void) { printf(\"%d\\n\", 7 / 2); return 0; }\n",
          "stable": true,
          "raw_stable": true,
          "cellNote": null,
          "build": {
            "cmd": "cc -O2 website/scratch/snippets/int-div-truncation.c -o website/scratch/.build/int-div-truncation",
            "rc": 0,
            "signal": null,
            "stdout": "",
            "stderr": "",
            "noise": [],
            "stabilized": [],
            "verbatim": true
          },
          "run": {
            "cmd": "website/scratch/.build/int-div-truncation",
            "rc": 0,
            "signal": null,
            "stdout": "3",
            "stderr": "",
            "noise": [],
            "stabilized": [],
            "verbatim": true
          },
          "status": "ran"
        }
      }
    },
    {
      "id": "index-out-of-range",
      "title": "越界读",
      "question": "读一个超过末尾的下标，会怎样？",
      "cells": {
        "inimerse": {
          "lang": "inimerse",
          "file": "website/scratch/snippets/index-out-of-range.im",
          "source": "a = [1, 2, 3]\nsay a[9]\n",
          "stable": true,
          "raw_stable": true,
          "cellNote": null,
          "build": null,
          "run": {
            "cmd": "build/inimerse run website/scratch/snippets/index-out-of-range.im",
            "rc": 0,
            "signal": null,
            "stdout": "nil",
            "stderr": "",
            "noise": [
              "[mod] utils loaded",
              "[mod] self-check init=42",
              "[infiverse mod] loaded (16 worlds, world 0 active)",
              "[verse_dist mod] VDP loaded (verse://<hub>/<id>)"
            ],
            "stabilized": [],
            "verbatim": false
          },
          "status": "ran"
        },
        "python": {
          "lang": "python",
          "file": "website/scratch/snippets/index-out-of-range.py",
          "source": "a = [1, 2, 3]\nprint(a[9])\n",
          "stable": true,
          "raw_stable": true,
          "cellNote": null,
          "build": null,
          "run": {
            "cmd": "python3 website/scratch/snippets/index-out-of-range.py",
            "rc": 1,
            "signal": null,
            "stdout": "",
            "stderr": "Traceback (most recent call last):\n  File \"<repo>/website/scratch/snippets/index-out-of-range.py\", line 2, in <module>\n    print(a[9])\n          ~^^^\nIndexError: list index out of range",
            "noise": [],
            "stabilized": [
              {
                "id": "repo-path",
                "why": "把检出位置去掉：同一个片段在别人的机器上会打印他自己的路径。",
                "count": 1
              }
            ],
            "verbatim": false
          },
          "status": "ran"
        },
        "javascript": {
          "lang": "javascript",
          "file": "website/scratch/snippets/index-out-of-range.js",
          "source": "const a = [1, 2, 3];\nconsole.log(a[9]);\n",
          "stable": true,
          "raw_stable": true,
          "cellNote": null,
          "build": null,
          "run": {
            "cmd": "node website/scratch/snippets/index-out-of-range.js",
            "rc": 0,
            "signal": null,
            "stdout": "undefined",
            "stderr": "",
            "noise": [],
            "stabilized": [],
            "verbatim": true
          },
          "status": "ran"
        },
        "rust": {
          "lang": "rust",
          "file": "website/scratch/snippets/index-out-of-range.rs",
          "source": "fn main() {\n    let a = vec![1, 2, 3];\n    println!(\"{}\", a[9]);\n}\n",
          "stable": true,
          "raw_stable": false,
          "cellNote": null,
          "build": {
            "cmd": "~/.rustup/toolchains/stable-x86_64-unknown-linux-gnu/bin/rustc --edition 2021 -O website/scratch/snippets/index-out-of-range.rs -o website/scratch/.build/index-out-of-range",
            "rc": 0,
            "signal": null,
            "stdout": "",
            "stderr": "",
            "noise": [],
            "stabilized": [],
            "verbatim": true
          },
          "run": {
            "cmd": "website/scratch/.build/index-out-of-range",
            "rc": 101,
            "signal": null,
            "stdout": "",
            "stderr": "thread 'main' (<tid>) panicked at website/scratch/snippets/index-out-of-range.rs:3:21:\nindex out of bounds: the len is 3 but the index is 9\nnote: run with `RUST_BACKTRACE=1` environment variable to display a backtrace",
            "noise": [],
            "stabilized": [
              {
                "id": "rust-panic-thread-id",
                "why": "rustc 1.99 的 panic 消息里带着 OS 线程号（实测每次运行 +1：26 / 27 / 28）。它是进程属性，不是语言的语义，所以按声明的规则换成 <tid>。",
                "count": 1
              }
            ],
            "verbatim": false
          },
          "status": "ran"
        },
        "c": {
          "lang": "c",
          "status": "excluded",
          "reason": "越界读在 C 里是未定义行为：同一份二进制在不同机器、不同优化级别下读到的字节都可能不同。一条不能复现的读数不能当比对读数，所以这一格空着 —— 空着是「不写」，不是「没测」。"
        }
      }
    },
    {
      "id": "annotation-unknown-type",
      "title": "标注一个不存在的类型",
      "question": "给变量标注一个不存在的类型，谁会把它拦下来？",
      "cells": {
        "inimerse": {
          "lang": "inimerse",
          "file": "website/scratch/snippets/annotation-unknown-type.im",
          "source": "x: NoSuchSet = 5\nsay x\n",
          "stable": true,
          "raw_stable": true,
          "cellNote": "Inimerse 的写法是「声明形状」：`名字: 集合 [= 初值]`。",
          "build": null,
          "run": {
            "cmd": "build/inimerse run website/scratch/snippets/annotation-unknown-type.im",
            "rc": 0,
            "signal": null,
            "stdout": "5",
            "stderr": "",
            "noise": [
              "[mod] utils loaded",
              "[mod] self-check init=42",
              "[infiverse mod] loaded (16 worlds, world 0 active)",
              "[verse_dist mod] VDP loaded (verse://<hub>/<id>)"
            ],
            "stabilized": [],
            "verbatim": false
          },
          "status": "ran"
        },
        "python": {
          "lang": "python",
          "file": "website/scratch/snippets/annotation-unknown-type.py",
          "source": "x: NoSuchType = 5\nprint(x)\n",
          "stable": true,
          "raw_stable": true,
          "cellNote": "Python 这里是 PEP 526 的变量标注。",
          "build": null,
          "run": {
            "cmd": "python3 website/scratch/snippets/annotation-unknown-type.py",
            "rc": 0,
            "signal": null,
            "stdout": "5",
            "stderr": "",
            "noise": [],
            "stabilized": [],
            "verbatim": true
          },
          "status": "ran"
        },
        "javascript": {
          "lang": "javascript",
          "status": "excluded",
          "reason": "JavaScript 没有类型标注语法，写不出同一个构造。"
        },
        "rust": {
          "lang": "rust",
          "file": "website/scratch/snippets/annotation-unknown-type.rs",
          "source": "fn main() {\n    let x: NoSuchType = 5;\n    println!(\"{}\", x);\n}\n",
          "stable": true,
          "raw_stable": true,
          "cellNote": "★ 这一格没有运行输出：编译没有通过。",
          "build": {
            "cmd": "~/.rustup/toolchains/stable-x86_64-unknown-linux-gnu/bin/rustc --edition 2021 -O website/scratch/snippets/annotation-unknown-type.rs -o website/scratch/.build/annotation-unknown-type",
            "rc": 1,
            "signal": null,
            "stdout": "",
            "stderr": "error[E0425]: cannot find type `NoSuchType` in this scope\n --> website/scratch/snippets/annotation-unknown-type.rs:2:12\n  |\n2 |     let x: NoSuchType = 5;\n  |            ^^^^^^^^^^ not found in this scope\n\nerror: aborting due to 1 previous error\n\nFor more information about this error, try `rustc --explain E0425`.",
            "noise": [],
            "stabilized": [],
            "verbatim": true
          },
          "run": null,
          "status": "build-failed"
        },
        "c": {
          "lang": "c",
          "file": "website/scratch/snippets/annotation-unknown-type.c",
          "source": "#include <stdio.h>\nint main(void) { NoSuchType x = 5; printf(\"%d\\n\", x); return 0; }\n",
          "stable": true,
          "raw_stable": true,
          "cellNote": "★ 这一格没有运行输出：编译没有通过。",
          "build": {
            "cmd": "cc -O2 website/scratch/snippets/annotation-unknown-type.c -o website/scratch/.build/annotation-unknown-type",
            "rc": 1,
            "signal": null,
            "stdout": "",
            "stderr": "website/scratch/snippets/annotation-unknown-type.c: In function ‘main’:\nwebsite/scratch/snippets/annotation-unknown-type.c:2:18: error: unknown type name ‘NoSuchType’\n    2 | int main(void) { NoSuchType x = 5; printf(\"%d\\n\", x); return 0; }\n      |                  ^~~~~~~~~~",
            "noise": [],
            "stabilized": [],
            "verbatim": true
          },
          "run": null,
          "status": "build-failed"
        }
      }
    },
    {
      "id": "caught-error-payload",
      "title": "捕获到的错误里有什么",
      "question": "把错误捕获下来之后，从它身上能拿到什么？",
      "cells": {
        "inimerse": {
          "lang": "inimerse",
          "file": "website/scratch/snippets/caught-error-payload.im",
          "source": "try {\n  say 1 / 0\n} catch (e) {\n  say str(e)\n}\n",
          "stable": true,
          "raw_stable": true,
          "cellNote": null,
          "build": null,
          "run": {
            "cmd": "build/inimerse run website/scratch/snippets/caught-error-payload.im",
            "rc": 0,
            "signal": null,
            "stdout": "division_by_zero",
            "stderr": "",
            "noise": [
              "[mod] utils loaded",
              "[mod] self-check init=42",
              "[infiverse mod] loaded (16 worlds, world 0 active)",
              "[verse_dist mod] VDP loaded (verse://<hub>/<id>)"
            ],
            "stabilized": [],
            "verbatim": false
          },
          "status": "ran"
        },
        "python": {
          "lang": "python",
          "file": "website/scratch/snippets/caught-error-payload.py",
          "source": "try:\n    1 / 0\nexcept Exception as e:\n    print(type(e).__name__ + \" | \" + str(e))\n",
          "stable": true,
          "raw_stable": true,
          "cellNote": null,
          "build": null,
          "run": {
            "cmd": "python3 website/scratch/snippets/caught-error-payload.py",
            "rc": 0,
            "signal": null,
            "stdout": "ZeroDivisionError | division by zero",
            "stderr": "",
            "noise": [],
            "stabilized": [],
            "verbatim": true
          },
          "status": "ran"
        },
        "javascript": {
          "lang": "javascript",
          "file": "website/scratch/snippets/caught-error-payload.js",
          "source": "try {\n  null.foo;\n} catch (e) {\n  console.log(e.name + \" | \" + e.message);\n}\n",
          "stable": true,
          "raw_stable": true,
          "cellNote": "★ 这一格的触发方式与其它格不同：JavaScript 在除以零时不报错，所以这里用读 `null` 的属性来触发一个 TypeError。",
          "build": null,
          "run": {
            "cmd": "node website/scratch/snippets/caught-error-payload.js",
            "rc": 0,
            "signal": null,
            "stdout": "TypeError | Cannot read properties of null (reading 'foo')",
            "stderr": "",
            "noise": [],
            "stabilized": [],
            "verbatim": true
          },
          "status": "ran"
        },
        "rust": {
          "lang": "rust",
          "file": "website/scratch/snippets/caught-error-payload.rs",
          "source": "fn main() {\n    let r: Result<i32, String> = Err(\"division_by_zero\".into());\n    println!(\"{:?}\", r);\n}\n",
          "stable": true,
          "raw_stable": true,
          "cellNote": "★ 这一格不是「捕获」：Rust 没有异常，这里写的是 `Result` 的形状。",
          "build": {
            "cmd": "~/.rustup/toolchains/stable-x86_64-unknown-linux-gnu/bin/rustc --edition 2021 -O website/scratch/snippets/caught-error-payload.rs -o website/scratch/.build/caught-error-payload",
            "rc": 0,
            "signal": null,
            "stdout": "",
            "stderr": "",
            "noise": [],
            "stabilized": [],
            "verbatim": true
          },
          "run": {
            "cmd": "website/scratch/.build/caught-error-payload",
            "rc": 0,
            "signal": null,
            "stdout": "Err(\"division_by_zero\")",
            "stderr": "",
            "noise": [],
            "stabilized": [],
            "verbatim": true
          },
          "status": "ran"
        },
        "c": {
          "lang": "c",
          "status": "excluded",
          "reason": "C 没有异常机制。`errno` 不是同一个构造，硬摆在一行会让人以为它们可比。"
        }
      }
    }
  ],
  "problems": []
};
