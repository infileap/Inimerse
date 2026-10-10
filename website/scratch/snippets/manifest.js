/* 片段语料的清单 —— 只放「问题」和「注意事项」，**不放任何输出**。
 *
 * 为什么输出不在这里：输出必须来自真跑（见 run_snippets.js）。
 * 一份手写的输出和一份真跑的输出在页面上长得一模一样 —— 这正是 S2 那条钉子要防的事。
 * 所以本文件里只允许出现：这个片段在问什么、它和别的语言那一格是不是同一个构造。
 */
module.exports = {
  /* 语言标识 → 页面上的列名。顺序即页面上的列序。 */
  languages: ['inimerse', 'python', 'javascript', 'rust', 'c'],
  labels: {
    inimerse: 'Inimerse',
    python: 'Python',
    javascript: 'JavaScript',
    rust: 'Rust',
    c: 'C',
  },

  topics: [
    {
      id: 'div-zero-int',
      title: '整数除以零',
      question: '整数除以零，失败发生在哪一步 —— 编译期、运行期，还是根本不失败？',
      cells: { inimerse: {}, python: {}, javascript: {}, rust: {}, c: {} },
    },
    {
      id: 'div-zero-float',
      title: '浮点除以零',
      question: '浮点除以零和整数除以零，是同一件事吗？',
      cells: { inimerse: {}, python: {}, javascript: {}, rust: {}, c: {} },
    },
    {
      id: 'mod-zero',
      title: '取模零',
      question: '取模零与整除零，抛出来的东西一样吗？',
      cells: { inimerse: {}, python: {}, javascript: {}, rust: {}, c: {} },
    },
    {
      id: 'int-div-truncation',
      title: '整数除法的结果类型',
      question: '两个整数相除，结果是整数还是浮点？',
      cells: { inimerse: {}, python: {}, javascript: {}, rust: {}, c: {} },
    },
    {
      id: 'index-out-of-range',
      title: '越界读',
      question: '读一个超过末尾的下标，会怎样？',
      cells: { inimerse: {}, python: {}, javascript: {}, rust: {} },
      excluded: {
        c: '越界读在 C 里是未定义行为：同一份二进制在不同机器、不同优化级别下读到的字节都可能不同。'
         + '一条不能复现的读数不能当比对读数，所以这一格空着 —— 空着是「不写」，不是「没测」。',
      },
    },
    {
      id: 'annotation-unknown-type',
      title: '标注一个不存在的类型',
      question: '给变量标注一个不存在的类型，谁会把它拦下来？',
      cells: { inimerse: {}, python: {}, rust: {}, c: {} },
      excluded: {
        javascript: 'JavaScript 没有类型标注语法，写不出同一个构造。',
      },
      cellNotes: {
        inimerse: 'Inimerse 的写法是「声明形状」：`名字: 集合 [= 初值]`。',
        python: 'Python 这里是 PEP 526 的变量标注。',
      },
    },
    {
      id: 'caught-error-payload',
      title: '捕获到的错误里有什么',
      question: '把错误捕获下来之后，从它身上能拿到什么？',
      cells: { inimerse: {}, python: {}, javascript: {}, rust: {} },
      excluded: {
        c: 'C 没有异常机制。`errno` 不是同一个构造，硬摆在一行会让人以为它们可比。',
      },
      cellNotes: {
        javascript: '★ 这一格的触发方式与其它格不同：JavaScript 在除以零时不报错，'
          + '所以这里用读 `null` 的属性来触发一个 TypeError。',
        rust: '★ 这一格不是「捕获」：Rust 没有异常，这里写的是 `Result` 的形状。',
      },
    },
  ],
};
