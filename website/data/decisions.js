/* 裁定台账 —— 规范页与台账页**共用**的同一份数据。
 *
 * ⚠ **今天没有任何页面引用这个文件。** 规范页（`website/spec/`）与台账页
 *   （`website/decisions/`）还没建，所以它此刻**进了产物、但页面上不生效** ——
 *   在产物里它和不存在是一样的，只是它会被上传。这一行是给下一个读者的：
 *   不要以为有页面在用它在渲染什么。
 *
 * 为什么两个页面读同一份：同一件事写两遍，下一轮就会有两份不一致。
 * 规范页按「文档」分组看它，台账页按「一条一条裁定」看它。
 *
 * ⚠ 三条纪律（都来自本仓库自己的教训）：
 *   1. 每一条 `source.quote` 必须是**逐字**的，且能在 `ref` 那棵树里按
 *      「文件 + 节号」找到。守卫是 `website/scratch/check_citations.js`（S1 引用钉）：
 *      改一个字、或写一个不存在的节号，它必须红。
 *   2. `alternative` 为 null 时，页面必须写「docs 未记录备选」，**并且**写出
 *      `altSearch` 里那份「查过哪些文件、用什么筛法」—— 「未记录」是一个零，
 *      而一个零需要被归因。**我不替它编。**
 *   3. 凡是引 `docs/PLAN_V07.md` 的数字，页面上必须同时显示它 §0.3 那句
 *      「这份文件里的数字都不是读数」—— 否则会把一次没有观测点的量读成读数。
 *
 * 本文件是**手写**的（它是一份索引与策展，不是生成物）；但它里面的每一句引文
 * 都受上面第 1 条的守卫。**手写的读数会漂，所以这里只放引文，不放我复述的数字。**
 */
window.INFIVERSE_DECISIONS = {
  ref: 'cd29feb4fed34b6a8cc6f9eafd202afbf6165f04',

  /* 「docs 未记录备选」的归因：查过哪些文件、用的什么筛法、命中多少。
     没有这一栏，「我查过这四处」与「我没找到」在页面上长得一样。 */
  altSearch: {
    files: [
      'docs/PLAN_V06.md',
      'docs/PLAN_V07.md',
      'docs/EIDOS_V06.md',
      'docs/DECFY_DESIGN.md',
      'docs/ERROR_CODES_V06.md',
      'docs/SYNTAX.md',
    ],
    terms: ['裁定', '备选', '否决', '放弃', '另一条', '没有走', '不选'],
    method: "grep -cE '裁定' <file> 与 grep -cE '备选|否决|放弃|另一条|没有走|不选' <file>",
    counts: {
      'docs/PLAN_V06.md': { ruled: 38, alt: 2 },
      'docs/PLAN_V07.md': { ruled: 60, alt: 7 },
      'docs/EIDOS_V06.md': { ruled: 17, alt: 2 },
      'docs/DECFY_DESIGN.md': { ruled: 72, alt: 13 },
      'docs/ERROR_CODES_V06.md': { ruled: 12, alt: 3 },
      'docs/SYNTAX.md': { ruled: 8, alt: 0 },
    },
    note: '计数是「这几份文件里出现过这些词多少次」，不是「有多少条裁定有备选记录」—— 它只说明筛过，不说明筛出了什么。',
  },

  /* 状态词表。★ 台账页必须能显示【已裁】与【提案】的差别 ——
     否则它会把一条没裁的东西读成裁过的。 */
  statuses: {
    ruled: { label: '已裁', cls: 'badge--live', note: '人类已经裁定过；docs 只是把它记下来并补上边界。' },
    proposal: { label: '提案', cls: 'badge--partial', note: '提出的建议，尚未裁定。' },
    undecided: { label: '待裁', cls: 'badge--partial', note: '已经发现两条路都说得通，需要人类选一条。' },
    notdone: { label: '未做', cls: 'badge--muted', note: '文档没有回答的问题，写在这里以免被读成已经回答。' },
    notdoing: { label: '明确不做', cls: 'badge--muted', note: '已经决定这一版不做。' },
    discipline: { label: '纪律', cls: 'badge--muted', note: '不是裁定，是一条约束做法本身的要求。' },
  },

  entries: [
    /* ================= docs/EIDOS_V06.md ================= */
    {
      id: 'EIDOS-1.1',
      status: 'ruled',
      title: 'Eidos 是「进引擎」还是「永远外部」',
      decided: '变成「内糖」—— 编译器认识它（声明、成员、继承、super、字段引用由编译器解析并检查；开放与封闭在编译期判定），VM 不需要认识它（不新增 opcode、不新增运行时类型信息）。',
      alternative: '① 保持外糖；② 完整内建',
      whyNot: '保持外糖 ⇒「开放与封闭」只能在脱糖期擦掉、不能在编译期守住；完整内建 ⇒ 会让 Eidos 成为 VM 的一个新决定点，与去 C 化的方向直接对撞。',
      overturn: null,
      source: {
        file: 'docs/EIDOS_V06.md',
        section: '1.1',
        quote: '**被否掉的两条**：① **保持外糖**（「开放与封闭」只能在脱糖期**擦掉**、不能在编译期**守住**）；② **完整内建**（会让 Eidos 成为 VM 的一个新决定点，与去 C 化的方向直接对撞）。',
      },
    },
    {
      id: 'EIDOS-1.3',
      status: 'ruled',
      title: '`@unfold eidos extend A, B` 与归档 `eidos C: A + B` 是同一件事的两种拼写',
      decided: '保留 `@unfold eidos extend A, B`；归档的 `eidos C: A + B` 作为一种【记录】留在归档原文里，不进入现行语法。',
      alternative: '归档的 `eidos C: A + B`（多重继承用 `+`）',
      whyNot: '`+` 只能表达「从谁那里取」，不能表达「取哪一个成员」；而歧义诊断需要点名来源（`keep one: bark from animal`），`+` 拼写里没有那个名字的位置。',
      overturn: null,
      source: {
        file: 'docs/EIDOS_V06.md',
        section: '1.3',
        quote: '★★ **【已裁】保留 `@unfold eidos extend A, B`；归档的 `eidos C: A + B` 作为一种【记录】留在归档原文里，不进入现行语法。**',
      },
    },
    {
      id: 'EIDOS-3.1',
      status: 'ruled',
      title: '合并规则第一条：同名且相同 ⇒ 只保留一份',
      decided: '静默合并（不报错），但编译器必须把「合并了哪两份」报出来，并且这个报告本身要能被一条测试钉住。',
      alternative: '(a) 纯静默合并；(b) 相同也必须显式（写 `bark from animal`，文本一样也不自动合）',
      whyNot: '本仓库的家法：**值 + 守卫 = 安全；值 + 无守卫 = 病**。合并是一个值，而它在 (a) 里没有守卫。',
      overturn: '改 `animal.bark` 的一个字 ⇒ 那条「merged」的钉必须红；**把那条钉删掉、其余一个字不动 ⇒ 什么都不会红** —— 这一对读数才是「可钉住」这个词的载荷。',
      source: {
        file: 'docs/EIDOS_V06.md',
        section: '3.1',
        quote: '**(c) 静默合并，但【必须报出来】，且这个报告本身可被钉住。**',
      },
    },
    {
      id: 'EIDOS-3.3',
      status: 'ruled',
      title: '参数不同不算同名 ——「保证不存在重叠」有两个相反的答案',
      decided: '取「最特化优先」：允许相交，更特化的赢；只有互不更特化才算歧义。',
      alternative: '不相交（任意两个签名不得相交）；另有第三条路本轮未取：编译器报出重叠的那块类型（是报，不是拒）',
      whyNot: '三条独立理由：① 人类自己举的 `run{}` 只有在最特化优先下才有意义；② 证明义务要尽量小（不相交是对所有配对的证明）；③ 不相交会把「我想给 `int` 一个特例、其余走通用」这条最常见的需求变成写不出来。',
      overturn: '传一个 `int` 实参 ⇒ 必须走 `run a:int`；对照是把规则改回不相交 ⇒ 同一个程序必须编译不过。',
      source: {
        file: 'docs/EIDOS_V06.md',
        section: '3.3',
        quote: '★★ **【已裁】取「最特化优先」。** 人类在 `ask_user_question` 上选的是它，三条独立的理由：',
      },
    },
    {
      id: 'EIDOS-6.1',
      status: 'ruled',
      title: '`@template` 这个名字会被读错 —— 已改名 `@mixin`',
      decided: '改名 `@mixin`。',
      alternative: '`@trait` / `@abstract`（没有被否，它们只是没有被选）',
      whyNot: '在几乎所有语言里 `template` 意思是泛型（C++ templates、Java generics），而这里它意思是抽象 mixin；将来真的想要泛型时，`@template` 已经被占了。',
      overturn: null,
      source: {
        file: 'docs/EIDOS_V06.md',
        section: '6.1',
        quote: '★★ **【已裁】改名 `@mixin`。** 人类在 `ask_user_question` 上选的是它。★ 理由：**将来真的想要泛型时，`@template` 已经被占了。**',
      },
    },
    {
      id: 'EIDOS-4',
      status: 'proposal',
      title: '`super` 必须限定',
      decided: '【提案】`super` 必须带名字 —— `super pet.play`（或 `super(pet)`）。',
      alternative: '位置式 `super`（归档 §2.5 逐字：「`super` 调用最近的父类实现」）',
      whyNot: '这个语言的全部卖点是「编译期可判定」，而位置式 `super` 把这个卖点直接交出去 —— 它让「这段代码调用的是哪个实现」取决于父列表的顺序，而那个顺序在展开之后看不见了。',
      overturn: '交换 `extend` 里两个父的顺序 ⇒ 若走位置式，行为必须变；若走限定式，行为必须不变。「顺序无关」这条性质本身，就是这个方案要买的东西。',
      flag: '这是本文里唯一一处「现行提案与归档规范直接矛盾、且尚未裁定」的地方。',
      source: {
        file: 'docs/EIDOS_V06.md',
        section: '4',
        quote: '**本条今天仍是【提案】。** §3.1、§3.3、§6.1 三条已经裁定，**而这一条没有**。',
      },
    },
    {
      id: 'EIDOS-1.5',
      status: 'proposal',
      title: '`invariant` 是唯一一条与「VM 不需要认识它」有张力的',
      decided: '【提案】走 (a)：编译器把它降到已有的构造（在每次赋值后插入一次调用 + 一个失败分支）⇒ VM 仍然不需要认识 Eidos。',
      alternative: '(b) VM 认识 `invariant`',
      whyNot: '(b) 与「VM 不需要认识它」的裁定冲突。',
      overturn: '构造一个能绕过检查的写入路径（容器元素、嵌套字段、`frozen` 之外的第二条路）⇒ 若它能绕过，说明写入点的枚举不全。「每次」是一个全称量词，而全称量词只能靠枚举来验。',
      source: {
        file: 'docs/EIDOS_V06.md',
        section: '1.5',
        quote: '⇒ **【提案】走 (a)。**',
      },
    },
    {
      id: 'EIDOS-2.2',
      status: 'proposal',
      title: '`@unfold` 由编译器执行，不是 `tools/` 里的一个脚本',
      decided: '【提案】`@unfold` 由编译器执行。',
      alternative: '由 `tools/` 里的一个脚本执行（外部工具做展开）',
      whyNot: '否则封闭规则（`final`/`sealed`/`frozen`）永远只能在展开期擦掉，而这正是被否掉的那条路。',
      overturn: null,
      source: {
        file: 'docs/EIDOS_V06.md',
        section: '2.2',
        quote: '⇒ **【提案】`@unfold` 由编译器执行，不是 `tools/` 里的一个脚本。**',
      },
    },
    {
      id: 'EIDOS-3.2',
      status: 'proposal',
      title: '歧义诊断必须点名来源',
      decided: '【提案】诊断必须点名来源，并给出「只取一份」的写法（`keep one: bark from animal`）。',
      alternative: '只报一句「你必须覆写 `bark`」',
      whyNot: '「另一条路是什么」必须写出来，否则读者以为只有一条路 —— 而这一覆写同时丢掉了 `animal.bark` 与 `pet.bark` 两份实现，且没有任何东西告诉他丢的是哪两份。',
      overturn: '删掉诊断里那句 `keep one: bark from animal` ⇒ 必须有一条断言红（否则「诊断会点名来源」这句话没有守卫）。',
      source: {
        file: 'docs/EIDOS_V06.md',
        section: '3.2',
        quote: '★ 理由：**「另一条路是什么」必须写出来，否则读者以为只有一条路。**',
      },
    },
    {
      id: 'EIDOS-3.4',
      status: 'proposal',
      title: '字段的合并规则与方法不同，而今天没有写',
      decided: '【提案】单列一条规则，且报错时把两个默认值都印出来。',
      alternative: null,
      whyNot: '字段「同名不同默认值」不是覆写，是一个没有自然解的冲突 —— 取哪个默认值？',
      overturn: null,
      source: {
        file: 'docs/EIDOS_V06.md',
        section: '3.4',
        quote: '★ **字段「同名不同默认值」不是覆写，是一个没有自然解的冲突** —— 取哪个默认值？',
      },
    },
    {
      id: 'EIDOS-7.3',
      status: 'proposal',
      title: '循环展开检测',
      decided: '【提案】必须有环检测，且报出那个环。',
      alternative: '报「深度超限」',
      whyNot: '「深度超限」是一个关于实现的读数，「这是一个环」是一个关于设计的读数 —— 前者让作者去调一个上限，后者让他去改结构。',
      overturn: '构造一个 `A → B → A` 的展开链 ⇒ 报错必须指出那个环，而不是「深度超限」。',
      source: {
        file: 'docs/EIDOS_V06.md',
        section: '7.3',
        quote: '★ 理由：**「深度超限」是一个关于实现的读数，「这是一个环」是一个关于设计的读数** —— 前者让作者去调一个上限，后者让他去改结构。',
      },
    },
    {
      id: 'EIDOS-8',
      status: 'proposal',
      title: '与其他语言的关系（缺的那一半）',
      decided: '【提案】每一类封闭手段，给出【至少三种语言怎么做】+【Eidos 为什么这样选】+【另一条路为什么没走】。',
      alternative: null,
      whyNot: '归档全文提到语言名只有两处，都不是比较 —— 它回答了「Eidos 怎么封闭」，没有回答「其他语言怎么封闭、Eidos 为什么这样选」。',
      overturn: null,
      flag: '这一节与网站「跨语言比对」那一页是同一份内容的两个渲染 —— 只做语法差异时它是一份说明书；把「别的语言怎么解决封闭性」也做进去，它就是本文缺的那一章。',
      source: {
        file: 'docs/EIDOS_V06.md',
        section: '8',
        quote: '★ **这一节与 `website/compare/` 那一页是同一份内容的两个渲染**',
      },
    },
    {
      id: 'EIDOS-1.6',
      status: 'notdone',
      title: '未判下落的归档节',
      decided: '【未做】本文没有逐条判完归档的全部 15 节。其余（`on` 的三种用途、Sprite 重构、虚函数调用优化、注释规范）尚未判下落。',
      alternative: null,
      whyNot: null,
      overturn: null,
      source: {
        file: 'docs/EIDOS_V06.md',
        section: '1.6',
        quote: '**本文没有逐条判完归档的全部 15 节。**',
      },
    },

    /* ================= docs/PLAN_V06.md §7 ================= */
    {
      id: 'V06-13',
      status: 'ruled',
      title: 'Eidos 是「进引擎」还是「永远外部」',
      decided: '变成「内糖」—— 编译器认识它，VM 不需要。现状不变、方向变了；实施排在决定点清单与 IR 收敛之后。',
      alternative: '「永远外部」（今天的形状看起来像它已被选中，但那是一个形状，不是一次裁决）',
      whyNot: null,
      overturn: null,
      source: {
        file: 'docs/PLAN_V06.md',
        section: '7',
        quote: '**已裁（人类）：变成「内糖」—— 编译器认识它，VM 不需要。现状不变、方向变了；实施排在决定点清单与 IR 收敛之后。详见 §3.3**',
      },
    },
    {
      id: 'V06-15',
      status: 'ruled',
      title: '`=>` 改成 `:`（先下决定又自己重新开放）',
      decided: '改成 `:` —— 而这已经是今天的现状，不是一次改动。',
      alternative: '保留 `=>`',
      whyNot: '取证三条：`=>` 作为运算符在语言里不存在（词法器只有 `->`，`.im` 里零命中，`src/` 里那 5 处全在 C 注释里）；`case` 分支今天就用 `:`；这条在 v0.4 就裁过了。',
      overturn: null,
      flag: '本行原写「阻塞型」是错的：它既已裁、也已实现，不阻塞任何东西。',
      source: {
        file: 'docs/PLAN_V06.md',
        section: '7',
        quote: '**已裁（人类）：改成 `:` —— 而这已经是今天的现状，不是一次改动。**',
      },
    },
    {
      id: 'V06-16',
      status: 'ruled',
      title: '`loop{}` 与 `for i in range` 两套拼写',
      decided: '两套都做，按有无索引分工：`loop { }` 管无索引的无限循环，`for i in range(a,b,)`（不给步长）管带索引的无限循环。',
      alternative: '只做一套',
      whyNot: '两套的用法不同（有无索引），合并会丢掉其中一种需求。',
      overturn: '附一条要求：这两种写法必须能被一条测试彼此分辨（不给步长的那种要有一条能从有限写法里区分出来的断言）。',
      source: {
        file: 'docs/PLAN_V06.md',
        section: '7',
        quote: '**已裁（人类）：两套都做，按有无索引分工（选项 C）**',
      },
    },
    {
      id: 'V06-17',
      status: 'ruled',
      title: '函数组合 `>>` 与管道 `|>` 不能混用',
      decided: '两个都留，把两个解析循环合成一个统一的优先级表。',
      alternative: '只留一个（或合成一个运算符）',
      whyNot: '它们做的是两件不同的事（`|>` 是施加，`>>` 是组合），所以这不是重复；病在解析器的形状，不在语言的设计。',
      overturn: '`3 |> inc >> dbl` 要有确定含义，且报错指向真正的冲突位置。',
      source: {
        file: 'docs/PLAN_V06.md',
        section: '7',
        quote: '**已裁（人类裁定 A）：两个都留，把两个解析循环合成一个统一的优先级表。**',
      },
    },
    {
      id: 'V06-18',
      status: 'ruled',
      title: '`->` 的双重身份（lambda / 类型转换）',
      decided: '`->` 只做 lambda；类型转换一律用函数形式。',
      alternative: '保留后置 `->` 做类型转换',
      whyNot: '取证三条：真实树里用后置 `->` 做转换的 `.im` 是 0 个（第一遍 grep 出的 8 处全在字符串字面量里）；它是重复的（`int(x)`/`float(x)`/`str(x)`/`bool(x)` 两个平台都注册着）；lambda 是有人用的那一半。',
      overturn: '`->` 只剩一个意思；`x -> int` 不再产生一个与 lambda 无关的报错；转换的写法在文档里只有一种。',
      source: {
        file: 'docs/PLAN_V06.md',
        section: '7',
        quote: '**已裁（人类裁定 A）：`->` 只做 lambda；类型转换一律用函数形式。**',
      },
    },
    {
      id: 'V06-19',
      status: 'ruled',
      title: '高阶函数：核心只加三个，还是加一整族',
      decided: '核心只加 `map`/`filter`/`reduce` 三个内建；其余扩展（`zip`/`each`/`find`/`any`/`all`/`sort_by`/`group_by`/`compose`/`curry`）进标准库。',
      alternative: '核心加一整族（扩张）',
      whyNot: '收窄的理由是用户自己的话：过多的函数式风格可能导致华而不实、难以维护，且学习成本上升。实测背景：今天没有任何内建接收函数实参，但函数值已经能存能调 ⇒ 缺的是库，不是机制。',
      overturn: null,
      flag: '一个还没被回答的问题：这三条为什么进核心而不是也进库 —— 计划书明说不假装它已经被回答。',
      source: {
        file: 'docs/PLAN_V06.md',
        section: '7',
        quote: '**已裁（人类裁定 A + 一条追加）：核心只加 `map`/`filter`/`reduce` 三个内建；其余扩展（`zip`/`each`/`find`/`any`/`all`/`sort_by`/`group_by`/`compose`/`curry`）进标准库**（新增 §6.3）。',
      },
    },
    {
      id: 'V06-20',
      status: 'ruled',
      title: '位运算',
      decided: '以函数形式提供，拼写与 Inim 汇编助记符一致（`bit_and`/`bit_or`/`bit_xor`/`bit_not`/`shl`/`shr`），归 v0.7，不进 v0.6。',
      alternative: '运算符形式',
      whyNot: '四个可能的拼写今天全部不可用 —— `|` 与 `>>` 是已经在用的记号（`case` 守卫、函数组合），把它们改成位运算等于改掉那两处语法；`&`/`<<`/`^` 可以新造，但那样同一个概念会有两种拼法。跟汇编助记符走是因为两层同名，从 `.im` 落到汇编时不需要一次名字翻译。',
      overturn: null,
      source: {
        file: 'docs/PLAN_V06.md',
        section: '7',
        quote: '**已裁（人类）：以函数形式提供，拼写与 Inim 汇编助记符一致（`bit_and`/`bit_or`/`bit_xor`/`bit_not`/`shl`/`shr`），归 v0.7，不进 v0.6。**',
      },
    },
    {
      id: 'V06-21',
      status: 'ruled',
      title: '错误码的表示',
      decided: '① 语法不动（D）—— 保持今天的守卫式 `err(e) | e in FileError:`，只把成员从字符串换成整数码；②「每域 ≤ 16」两条守卫都要（C）；③ 旧的 1001–2307 不留；④ 追加一条：需要速查途径。',
      alternative: '语法也要改（把守卫式换成别的形状）',
      whyNot: '这一条不是新造需求 ——「小集合 + 编译器证穷尽」今天已经在跑（`vtest/case_try_v04.im` 与 `--lint` 的缺成员报错），缺口只有一步：成员是字符串，不是整数码。',
      overturn: null,
      flag: '射程边界：「编译器检测到所有都有处理就能安全过关」只在 `--lint` 下成立，不在默认编译路径下成立。',
      source: {
        file: 'docs/PLAN_V06.md',
        section: '7',
        quote: '**已裁（人类，三条逐条回字母 + 一条追加）：① 语法不动（D）—— 保持今天的守卫式 `err(e) | e in FileError:`，只把成员从字符串换成整数码；②「每域 ≤ 16」两条守卫都要（C）—— 一条 CTest 数成员、`im_enum_create` 按 `IM_ENUM_U8` 拒绝越界；③ 旧的 1001–2307 不留（表里不设 `legacy_code` 一栏）；④ 追加一条：需要速查途径。**',
      },
    },
    {
      id: 'V06-6',
      status: 'ruled',
      title: '宏系统：实现、简化、还是与多分派统一',
      decided: 'v0.6 不实现宏系统，但把它写成一条设计约束 —— 约束的内容就是「宏与多分派必须是同一个机制，不是两套」。',
      alternative: '现在实现宏系统；或只做简化',
      whyNot: '现在实现会在类型系统定稿前钉死它的形状，而多分派先做则必然返工。写成约束的作用：做多分派的人一开始就知道这个形状要求。',
      overturn: null,
      flag: '本行第一版把一条关于 `proof` 的话误引成第二条「太复杂」，那是一处误引，已改回。',
      source: {
        file: 'docs/PLAN_V06.md',
        section: '7',
        quote: '**已裁（人类裁定 A）：v0.6 不实现宏系统，但把它写成一条设计约束**',
      },
    },
    {
      id: 'V06-3',
      status: 'ruled',
      title: 'workbench 口径：两说「不一致」',
      decided: '两说不是「不一致」，它们量的不是同一个东西 —— 记成两条读数，并把真问题写出来。',
      alternative: '判其中一说为错',
      whyNot: '一条是行为读数，今天逐条成立；另一条是存在性读数，作为树上的事实今天也成立。真正的未决是「预置模板该不该被 `workbench.im` 读」。',
      overturn: null,
      flag: '纪律：「两说不一致」有时是「两说量的不是同一个东西」，而它们在表里长得一样。',
      source: {
        file: 'docs/PLAN_V06.md',
        section: '7',
        quote: '**已裁（人类裁定 A）：两说不是「不一致」，它们量的不是同一个东西 —— 记成两条读数，并把真问题写出来。**',
      },
    },
    {
      id: 'V06-12',
      status: 'ruled',
      title: '元格式的射程',
      decided: 'v0.6 只做与它重叠的那一半（一种类型可以声明自己的表示与宽度，并在编译期校验），元格式本身另立一条线。',
      alternative: '六件全做（声明占若干字节/比特、可扩增、可提取中间的位、运算符重载、自带加密解密、工具类声明式构造）',
      whyNot: '理由不是省事，是依赖：六件里有三件依赖还没定稿的东西 —— 运算符重载属 Eidos 线、零成本降低属去 C 化线、而「声明占若干比特」正是那一半本身。',
      overturn: null,
      source: {
        file: 'docs/PLAN_V06.md',
        section: '7',
        quote: '**已裁（人类裁定 A）：v0.6 只做与它重叠的那一半，元格式本身另立一条线。**',
      },
    },

    /* ================= docs/PLAN_V06.md §8 / §9 ================= */
    {
      id: 'V06-8.2',
      status: 'notdoing',
      title: '不用「用 `.im` 重写」当修法',
      decided: '明确不做：换成 `.im` 不消除分歧，只改变分歧发生在哪两个文件之间。',
      alternative: '用 `.im` 重写有分歧的那部分',
      whyNot: '它是 `docs/DECFY_DESIGN.md` 的结论：换语言不消除分歧，只改变分歧发生在哪两个文件之间。',
      overturn: null,
      source: {
        file: 'docs/PLAN_V06.md',
        section: '8',
        quote: '2. **不用「用 `.im` 重写」当修法** —— `docs/DECFY_DESIGN.md` 的结论；换成 `.im` 不消除分歧，只改变分歧发生在哪两个文件之间。',
      },
    },
    {
      id: 'V06-9.1',
      status: 'discipline',
      title: '验收：断言输出的值，不是退出码',
      decided: '每个交付项要有一条能在 `main` 上失败的进树回归，并且断言输出的值，不是退出码。',
      alternative: null,
      whyNot: null,
      overturn: null,
      source: {
        file: 'docs/PLAN_V06.md',
        section: '9',
        quote: '1. **每个交付项要有一条能在 `main` 上失败的进树回归**，并且**断言输出的值，不是退出码**（逐字依据见 §3.2）。',
      },
    },
    {
      id: 'V06-9.2',
      status: 'discipline',
      title: '验收：每个读数带观测点',
      decided: '数字旁边要么有 ref，要么它只是一个 delta。绝对现值永远非法。',
      alternative: null,
      whyNot: '写这句话会不会改变它量的那个数 —— 会，就不能写成现值。',
      overturn: null,
      source: {
        file: 'docs/PLAN_V06.md',
        section: '9',
        quote: '2. **每个读数带观测点**：数字旁边要么有 ref，要么它只是一个 delta。**绝对现值永远非法**',
      },
    },

    /* ================= docs/PLAN_V07.md ================= */
    {
      id: 'V07-V1',
      status: 'ruled',
      title: '位运算的形式',
      decided: '函数形式，六个名字 `bit_and` `bit_or` `bit_xor` `bit_not` `shl` `shr`；拼写跟汇编助记符走。',
      alternative: '运算符形式',
      whyNot: '四个可能拼写今天在词法上全部不可用，见 §1.2 —— 这不是取舍，是被测量逼出来的。',
      overturn: null,
      source: {
        file: 'docs/PLAN_V07.md',
        section: '0.1',
        quote: '| `V1` | 位运算的形式 | **函数形式**，六个名字 `bit_and` `bit_or` `bit_xor` `bit_not` `shl` `shr`；拼写**跟汇编助记符走** | §1.1–§1.3 |',
      },
    },
    {
      id: 'V07-V2',
      status: 'ruled',
      title: '位运算的位宽与溢出',
      decided: '`Z` 任意精度；位运算按补码语义定义到无穷位；溢出问题不存在。',
      alternative: null,
      whyNot: null,
      overturn: null,
      source: {
        file: 'docs/PLAN_V07.md',
        section: '0.1',
        quote: '| `V2` | 位运算的位宽与溢出 | **`Z` 任意精度**；位运算按**补码语义定义到无穷位**；★ **溢出问题不存在** | §1.5 |',
      },
    },
    {
      id: 'V07-V4',
      status: 'ruled',
      title: '层二的名字',
      decided: '`@asm.phys`',
      alternative: '`raw`（被否）',
      whyNot: '它已经是 `src/` 里一个类型的名字，而且读起来像「不安全」。',
      overturn: null,
      source: {
        file: 'docs/PLAN_V07.md',
        section: '0.1',
        quote: '| `V4` | 层二的名字 | **`@asm.phys`**（`raw` 被否：它已经是 `src/` 里一个类型的名字，而且读起来像「不安全」） | §2.3 |',
      },
    },
    {
      id: 'V07-V12',
      status: 'ruled',
      title: '实施顺序：v0.7 内部先做位运算，还是先做汇编线',
      decided: '先位运算。',
      alternative: '先做汇编线',
      whyNot: '位运算那条路自己开始、自己结束，不欠任何人一个问题；汇编线那条路的第一个动作是「等一个问题被回答」。⇒ 先选位运算，等于让 v0.7 从一个小版本开始，而汇编线一步都没少，它只是晚一点开始。',
      overturn: null,
      source: {
        file: 'docs/PLAN_V07.md',
        section: '0.1',
        quote: '| `V12` | **实施顺序** | **先位运算**（那条路自己开始、自己结束，不欠任何人一个问题；汇编线的第一个动作是等一个问题被回答） | §0.4 第 1 条、§4 |',
      },
    },
    {
      id: 'V07-0.4-4',
      status: 'ruled',
      title: '标准库、宏系统、元格式这三件进不进 v0.7',
      decided: '三件都归 v0.6，v0.7 不碰。',
      alternative: '归 v0.7',
      whyNot: '三件的共同点是「都不在 v0.7 已经裁下的两族里」，而 v0.7 已经有十二条裁定落在不存在的东西上 —— 再加三件不会让它更完整，只会让它的边界变成「v0.6 剩下的加 v0.7 新的」。',
      overturn: null,
      source: {
        file: 'docs/PLAN_V07.md',
        section: '0.4',
        quote: '**已裁定 A：三件都归 v0.6，v0.7 不碰**（`V15`）',
      },
    },
    {
      id: 'V07-0.3',
      status: 'discipline',
      title: '这份文件里的数字都不是读数',
      decided: '「零注册」「零用户」「零命中」都是在写这份文件时量的一次，没有观测点、没有门禁盯着 —— 它们会随着下一笔提交变假，而变假时没有任何东西会说话。',
      alternative: null,
      whyNot: null,
      overturn: null,
      flag: '凡是引 `docs/PLAN_V07.md` 的数字，页面上必须同时显示这一条。',
      source: {
        file: 'docs/PLAN_V07.md',
        section: '0.3',
        quote: '这一节里的「零注册」「零用户」「零命中」都是**在写这份文件时量的一次**，**没有观测点、没有门禁盯着**。',
      },
    },
  ],
};
