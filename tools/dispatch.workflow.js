// tools/dispatch.workflow.js — 一次把多条流的活派出去。
//
// 这是给 DSH 的 `workflow` 工具用的脚本体（不是 Node 模块，不能 require）。
// 用法：把本文件内容整段作为 workflow 工具的 script 参数传入。
//
// 设计要点（都是踩出来的）：
//   1. 实现阶段并行——编辑/思考是廉价的，且各自在独立 worktree 里，互不干扰。
//   2. 门禁阶段串行——`tools/gate.sh` 的 ctest 在高并发下会因端口竞争假失败
//      （见 docs/STATUS.md §2.9），四条流同时跑 gate 必然误报。
//   3. 每条流只 commit 不 push；合并 main 由协调者审查后做。
//
// 加一条新流：往 STREAMS 里加一项即可。

const REPO = '/home/sakiko/inimerse';

const STREAMS = [
  {
    slug: 'repo-hygiene',
    task: [
      '按作业单第 2 节做**第 1 批**：删掉 24 个 `CHANGES_*.txt` 与 `CMakeLists.txt.bak`。',
      '用 `git rm` 删已跟踪的；`CMakeLists.txt.bak` 未跟踪，直接 `rm`。',
      '然后做**第 2 批的分类方案**：用作业单第 3 节的全仓库扫描法，把 135 个零引用文件',
      '按「删除 / 移到 examples/ / 保留」三档分类，写成 `docs/HYGIENE.md`，**只写方案，不要执行第 2 批的删除**。',
      '跑 `python3 tools/check_links.py` 确认删文件没打断文档链接。',
    ].join(''),
  },
  {
    slug: 'docs-audit',
    task: [
      '按作业单第 3 节修掉 `docs/REQUIREMENTS_ANALYSIS.md` 里 **22 条失效路径**',
      '（`docs/<X>.md` → `docs/archive/<X>.md`，逐条用 `test -e` 核对；',
      '`docs/AI_LAYOUT.md` 是例外，它移到了仓库根 `AI_LAYOUT.md`）。',
      '然后写一个**独立的反引号路径检查**（作业单 §3.2 说明了为什么 `check_links.py` 看不见这类失效），',
      '放在 `tools/` 下并接进 `tools/gate.sh` 成为一个新阶段。',
      '再核对 `README.md:25` 的 `0 failed out of 85` 与 JS 套件的 `11/11` 是否属实。',
      '**不要改 `docs/archive/` 里的内容，不要改 `docs/STATUS.md` §1 的四标记定义。**',
    ].join(''),
  },
  {
    slug: 'upp-in-engine',
    task: [
      '按作业单实现**引擎侧 UPP 状态机**：线上格式照抄 `tools/upp_reference.js`，',
      '状态机与边界条件照抄作业单第 4 节的逐条规则（start 幂等、从 crashed 起 start 报错、',
      '心跳 seq/timestamp 非递减、15s 超时转 crashed、recover 只允许从 crashed/stopped）。',
      '写一个 C 探针（如 `src/verse/upp_probe.c`）覆盖全部边界，注册进 `CMakeLists.txt` 并加进 ctest。',
      '目标是一个**能提交的增量**：状态机跑通 + 边界断言齐全 + 定向测试过。',
      '如果逐事件对照 JS 参考实现这一项没做完，如实写进 notDone。',
      '**`tools/upp_reference.js` 与 `tools/upp_session.js` 是只读参考实现，不要改。**',
    ].join(''),
  },
  {
    slug: 'vverse-produce',
    task: [
      '按作业单实现**引擎侧 `.vverse` 打包器**：格式照抄作业单第 3、4 节',
      '（gzip 的 JSON、`format: "vverse-1"`、`files` 是 base64 映射、**`mtime: 0`**、',
      '`signatures/sha256.json` 的覆盖范围规则、ed25519 用 DER SPKI 且签名对象是排序后排除 `signatures/` 的 JSON）。',
      '关键判据：**同一目录连续打包两次，产物 sha256 相同**。',
      '并做双向交叉验证（引擎打的包能用 `tools/vverse_pack.js unpack` 解开；',
      'JS 打的包能被引擎的读取路径装载）。',
      '长消息哈希必须用 `Sha512Ctx` + `sha512_init/update/final`，**不要**用一次性 `sha512_buf`（>8192 字节会静默截断）。',
      '**`tools/vverse_pack.js` 与 `tools/vverse_validate.js` 是只读参考实现，不要改。**',
    ].join(''),
  },
];

const preamble = (s) => [
  `你是 Inimerse 仓库里「${s.slug}」这条工作流的执行者。你没有别的上下文，以下就是全部。\n\n`,
  `工作目录：${REPO}/.worktrees/${s.slug}\n`,
  `分支：stream/${s.slug}（已存在，已基于 main 最新提交）\n\n`,
  `**第一步**：读 ${REPO}/.worktrees/${s.slug}/docs/streams/${s.slug}.md —— 那是你的作业单。\n`,
  `再读 ${REPO}/.worktrees/${s.slug}/docs/BOARD.md 的 §1–§4。\n\n`,
  `环境事实（都实测过，别自己摸索）：\n`,
  `- 构建：cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j4\n`,
  `  **用 -j4，不要用 nproc**：这台机器上会同时跑别的流，抢满核心会让测试假失败。\n`,
  `- **不要跑完整的 tools/gate.sh**：它的 ctest 阶段在高并发下会因端口竞争假失败（见 docs/STATUS.md §2.9）。\n`,
  `  你只跑与你改动相关的定向检查。协调者稍后会串行跑 gate。\n`,
  `- git 身份已配好，直接 commit 即可。**不要 push**，只 commit 到本地分支。\n`,
  `- **不要合并 main，不要动别的流的冲突域。**\n`,
  `- 文件沙箱是 danger-full-access，**不要传 sandbox_permissions 参数**。\n`,
  `- 若需 npm，必须加 --cache ${REPO}/.npm-tmp\n\n`,
  `你的任务：\n${s.task}\n\n`,
  `要求：\n`,
  `- 干活，然后 commit（一个或多个都行），把 commit SHA 报回来。\n`,
  `- **诚实**：没做完的、已知没解决的，写进 notDone，不要粉饰。这一项比 summary 更重要。\n`,
  `- 不要为了让数字好看而删测试、放宽断言或改判据。\n`,
  `- 如果你判断这条任务在你的能力/时间范围内做不完，就做一个**能提交、能解释**的增量，然后如实报告。\n`,
].join('');

const workSchema = {
  type: 'object',
  properties: {
    slug: { type: 'string' },
    status: { type: 'string', enum: ['done', 'partial', 'blocked', 'failed'] },
    commits: { type: 'string' },
    summary: { type: 'string' },
    notDone: { type: 'string' },
    filesChanged: { type: 'string' },
  },
  required: ['slug', 'status', 'summary', 'notDone'],
  additionalProperties: false,
};

const gateSchema = {
  type: 'object',
  properties: {
    slug: { type: 'string' },
    passed: { type: 'boolean' },
    table: { type: 'string' },
    failures: { type: 'string' },
  },
  required: ['slug', 'passed', 'table'],
  additionalProperties: false,
};

phase('implement');
log(`并行派发 ${STREAMS.length} 条流`);

const work = await parallel(
  STREAMS.map((s) => () =>
    agent(preamble(s), {
      label: `work:${s.slug}`,
      phase: 'implement',
      schema: workSchema,
    })
  )
);

phase('gate');
log('串行跑门禁——并行会因端口竞争假失败');

const gates = [];
for (const s of STREAMS) {
  log(`gate: ${s.slug}`);
  gates.push(
    await agent(
      [
        `在 ${REPO}/.worktrees/${s.slug} 里跑一次完整门禁：\n`,
        `  cd ${REPO}/.worktrees/${s.slug} && tools/gate.sh\n\n`,
        `只跑这一次，**不要修改任何文件**。把结论表原样抄进 table 字段（贴输出，不要复述）。\n`,
        `如果有阶段失败，把失败阶段的日志尾部（最多 40 行）放进 failures。\n`,
        `passed 表示是否全绿。\n`,
      ].join(''),
      { label: `gate:${s.slug}`, phase: 'gate', schema: gateSchema }
    )
  );
}

return {
  work: work.filter(Boolean),
  gates: gates.filter(Boolean),
};
