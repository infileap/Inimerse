# dsh-session-handoff 车道图与归档建议

- **认领者**：`ivory-zephyr`（协调会话）
- **分支**：`stream/board-status-refresh`
- **位置说明**：初版写在 `/home/sakiko/handoff-lane-map.md`（**仓库之外、无版本控制**），移入这里的理由同 [dispatch-incident-and-eight-same-shape-errors.md](dispatch-incident-and-eight-same-shape-errors.md) §9 第 8 例。

# dsh-session-handoff 车道图与归档建议

生成时间：2026-10-19 21:40 CST。所有数字都是本机实测（投影缓存 `~/.dsh/storages/session_projcache/`、`conversation_list`、`GET /api/handoff/status`）。

## 0. 已落地的止血配置（已验证热生效）

`/home/sakiko/.dsh/profiles/web/cordis.patch.yml`：

```yaml
- id: dsh-session-handoff
  name: dsh-session-handoff
  config:
    handoffDir: /home/sakiko/.dsh/handoff
    cooldownMs: 3600000      # 原默认 300000（5 分钟）
    thresholdRatio: 0.8      # 原默认 0.6
```

验证：`dsh --profile web --dump-config` 与 `GET /api/handoff/status` 都返回 `thresholdRatio: 0.8` / `cooldownMs: 3600000`，**无需重启**（patch 层被运行时重新应用）。
备份：同目录 `cordis.patch.yml.before-handoff-dir.bak`。

- 压力口径（`node_modules/dsh-session-handoff/lib/index.js:281-293`）= `contextPressure.surfaceTokens / contextWindow`。kestrel 当前 `683400 / 1000000 = 68.3%`：旧阈值 60% 会触发，新阈值 80% 不会。
- **仍留一手**：`maxCompactions: 2` 没动。`compactionsSince`（`:127-137`）数的是累计 summary 数，`byCompaction = maxCompactions > 0 && count >= 2`（`:817`）与压力无关 —— 也就是说压缩重的会话仍会触发交接，只是被 1 小时冷却限流。要彻底关掉这条触发条件，得设 `maxCompactions: 0`。

## 1. 车道当前谁在跑（21:37 实测）

| 车道 | 现在唯一 live 的会话 | pending | 在做什么 |
|---|---|---|---|
| 规则台账（headagent 链） | `lively-kestrel` headagent（接 18-50）`[已交接]` idle | 2 | 台账 §1.N 增补标题审计；21:18 提交 `46cc039`（§1.179，作者 月识） |
| agent4 链 | `fair-lantern` agent4（接 18-54）`[已交接]` idle | 2 | 收到 amber-badger「停写通知」，已只读处置并回信 |
| TEPS | `mellow-forge` TEPS（接 17-45） | 0 | running |
| agent3 / agent2 / agent1 | `honest-island` / `lucid-badger` / `ivory-ember` | 0 | idle |

**结论：两条链的正主都是带 `[已交接]` 的父会话。** 5 个 headagent 后继（20-45/20-50/20-55/21-06/21-13）与 2 个 agent4 后继（20-45/20-50）现在全部 `inactive`。所以「只认不带 `[已交接]` 那条」是错的规则，应改成「**只认 live 那条**」。

## 2. 那 pending 2 到底是什么

从投影 `rows.inbox` 读出（queue 只存在于 web 进程内存里，磁盘上读不到）：

- **kestrel**：① `[advisor:concern]` —— §1.102/§1.103 用粗体段落排版，要求补一次正向检查 `grep '^\*\*§1\.'`，并把给写者的结论限定为「已证两例」；② 第 4 节那条链接通知。
- **lantern**：① `[advisor:nit]` —— 冻结记录只留了 sha256 前 16 位，要求落全程 64 位摘要并保留 bytes/mtime；② 第 4 节那条链接通知。
- **amber-badger**（inactive，**勿归档**）：持有 `goal-b4dd7762-f336-4696-81c7-c1f85356cf2a` revision 2，phase **paused**，roundsStarted 4 —— 目标是「作为接 20-50 的后继头，继续那条『规则台账』审计链：保持只读第二读者角色……写者停摆时接笔落一节」。

## 3. 归档建议

### 可直接归档（inactive、无 goal、无 pending、轮次很少）

| 标题 | handle | session id | 规模 |
|---|---|---|---|
| headagent（接 21-13） | witty-summit | `session-c4b28b06-7c5b-4e24-a7c7-32394ad8378a` | 1 轮 / 33 步，基本没接活 |
| headagent（接 21-06） | glad-garnet | `session-1d869e70-69fa-478d-ac84-de8d696d2ce3` | 6 轮 |
| headagent（接 20-55） | jolly-summit | `session-4985030d-3e5e-4909-acfa-a6738e517ef0` | 3 轮 |
| headagent（接 20-45） | noble-orchid | `session-876bbc10-7b85-493b-93e9-f0ae3feba57a` | 3 轮 |
| agent4（接 20-50） | vivid-otter | `session-ff636f18-2070-4d24-bac8-3804770f9b2d` | 2 轮 |

### 暂不归档

| 标题 | handle | 为什么留 |
|---|---|---|
| headagent（接 18-50）`[已交接]` | lively-kestrel | 该车道唯一 live；台账审计在办，pending 里有 advisor 待办 |
| agent4（接 18-54）`[已交接]` | fair-lantern | agent4 车道唯一 live；正在执行停写/冻结合规 |
| headagent（接 20-50） | amber-badger | 持有 paused goal（车道第二读者授权），归档会把这条授权埋掉 |
| agent4（接 20-45） | plain-maple | amber-badger 指定的 agent4 车道唯一写者（现已 inactive）→ 先确认车道归属再决定 |

注：归档只是把它从默认列表里挪走，可逆且不动磁盘；它发给别人的消息仍留在对方记录里（例如 noble-orchid → kestrel 的消息在 kestrel 的 transcript 里）。

## 4. 我（ivory-zephyr）造成的一条队列污染

为了读 pending，我对 kestrel 与 lantern 各做了一次 `conversation_link`。链接会自动往对端队列塞一条「ivory-zephyr 介绍自己」的通知，且它要求对端回信 —— 于是两个会话的 pending 从各 1 条变成各 2 条，其中一条是我造成的。

消除办法：给两者发 `mode="inject"` 的一句话「此条为 peer 链接通知，无需回复、不占任务」（inject 不唤醒会话）；或者不管它，让它们回一条即可（代价是各走一个 turn）。

## 5. 操作步骤

1. 打开 `dsh-session-manager` 的批量面板，按工作区 `/home/sakiko/inimerse` 过滤；
2. 勾选第 3 节「可直接归档」那 5 条（用标题或 session id 认）；
3. Archive（可逆、不动磁盘）。要连磁盘一起清再走 Delete（不可逆、需确认）。

另：包里「本文件：…」那行仍写旧的 `/home/sakiko/inimerse/.dsh/handoff/` 路径，属历史记录，未回填。

---

## 6. 21:55 更新（上面 1–5 节是 21:40 的快照，车队已重新跑起来，归档建议缩小）

### 6.1 止血配置最终态（已验证热生效）
`thresholdRatio: 0.8`、`cooldownMs: 3600000`、`maxCompactions: 0`、`handoffDir: /home/sakiko/.dsh/handoff`。
`maxCompactions: 0` 是 advisor 指出的关键一条：`compactionsSince`（`lib/index.js:127-137`）数的是累计 summary 数、永不复位，与压力阈值无关，只有置 0 才真正停掉「压缩触发」这条腿。

### 6.2 goal 的硬阻断（待人类一句话）
amber-badger 的 `goal-b4dd7762` 仍是 revision 2 / phase **paused**。`update_goal(edit)` 与 `create_goal` 都被拒绝，逐字：
`Error: this goal operation requires a direct human turn on a top-level agent`
—— 经 `conversation_send` 转述的授权不满足该判定。**必须由人类在 amber-badger 会话里直接发一句**才能恢复为 active。

### 6.3 车队现状（21:55，live 的会话）
| 车道 | live 会话 | 状态 |
|---|---|---|
| 规则台账 | noble-orchid（接 20-45） | **running，现在是持笔者**（§1.181 `12dd901`、§1.182 `4e95db4` 都是它落的） |
| 规则台账 | amber-badger（接 20-50） | running，manager，已退回只读第二读者（不与持笔者抢同一份台账） |
| 规则台账 | lively-kestrel（接 18-50）[已交接] | running，结 `[advisor:concern]` + 复核 95/85 |
| 规则台账 | witty-summit（接 21-13） / jolly-summit（接 20-55） | idle，各 pending 1 —— **已复活，不再是无价值的空壳** |
| agent4 | fair-lantern（接 18-54）[已交接] | running，按 plain-maple 的 `marks129` 判据写脚本/跑/自检 |
| agent4 | plain-maple（接 20-45） | running（曾是 inactive，现已被唤起） |
| TEPS / agent1-3 | mellow-forge / ivory-ember / lucid-badger / honest-island | running / idle |

仓库实测：`git log` 头部 `4e95db4`(§1.182) → `12dd901`(§1.181) → `6aac364`(§1.180) → `46cc039`(§1.179)。

关于「有没有推上去」，判定必须用能区分来源的证据 —— 只看 `origin/main` 不行（本地跟踪引用任何一次成功 fetch 都会前进）：
- `git reflog show origin/main` 的最近三条都是 **`update by push`**（`4e95db4`/`12dd901`/`6aac364`）；
- `git ls-remote origin refs/heads/main` 直接问到远端，返回 `4e95db4d43561a764af4f83e66974dffed80d0e8`；
- `.git/FETCH_HEAD` 为空。
⇒ 三个提交**确实在远端**。但注意：amber-badger 自述它自己的 fetch/push 全部失败（`Bad owner or permissions on …ssh_config.d/20-systemd-ssh-proxy.conf`、`Permission denied (publickey)`），所以那些 push 是**别的会话**做的 —— 它的保守措辞是对的，不是过度谨慎。

`docs/AUDIT.md` 此刻是 `git status` 里的 ` M docs/AUDIT.md`（有写者正在落笔），所以它的行数（读到的 17138、amber 报的 17124）都是**撕裂读**，各自可能是各自时刻的真值，不能用来互相印证或证伪。

### 6.4 归档建议（缩小到 2 条）
仍然 inert、可以归档的只剩：
| 标题 | handle | session id |
|---|---|---|
| headagent（接 21-06） | glad-garnet | `session-1d869e70-69fa-478d-ac84-de8d696d2ce3` |
| agent4（接 20-50） | vivid-otter | `session-ff636f18-2070-4d24-bac8-3804770f9b2d` |

原先列的 `noble-orchid`、`witty-summit`、`jolly-summit`、`plain-maple` 现已全部复活（见 6.3），**不要归档**。

### 6.5 工时估算（2026-10-20 18:05 实测口径）
- 写节速率：10-20 `17:42→18:00` 落 5 节（`6aac364`/`12dd901`/`4e95db4`/`487ad58`/`3bf9b85`）≈ **3.6 分钟/节**；10-19 `20:40→21:18` 落 9 节 ≈ 4.2 分钟/节。
- 欠账不收敛：最后 6 节（§1.180–§1.185）的戊栏固定 7 格里，仍欠的格数为 **4 / 7 / 4 / 5 / 5**，没有下降趋势；每节还新派 1–3 件 ⇒ 按现行规则是**净增待办**。
- 三个不同量级：① 清当前积压（戊栏 4 件 + kestrel 的 `advisor:concern` + lantern 的 64 位摘要）≈ **2–4 h**；② 引擎自动轮天花板上界 = `maxGoalRounds 256 − roundsStarted 4` = 252 轮 × 3.6–10 分钟 ≈ **17–42 h**（到点自己停，但不是「做完」）；③ `docs/STATUS.md` 路线图 70 条大件（构建矩阵/POSIX 拆分/ABI v1…）**近 48 h 零勾选** ⇒ 天/周量级。
- 停摆变量：10-19 21:18 → 10-20 17:42 空了 20 小时，因为 goal 是 paused。**goal 不 active，墙钟不计。**

### 6.6 给人类的两条操作约束（advisor 提醒）
1. 那句话要等 amber-badger **回到 idle** 再贴；turn 还在 running 时排队进去的人类消息可能被当成 follow-up，仍不满足 direct human turn 的判定。
2. 贴完让它立刻用 `conversation_send` 回一句 goal 是否 active，再由我方从 projcache 的 `goal` 行独立核实，不要只信自述。
3. Plan B：若 GUI 里直接发仍被同一条策略拒绝，就在该会话里**新建**一条 goal（而不是 edit 旧的 paused goal），绕开 revision/phase 相关守卫。
