# 作业单：`citations.pin.json` 的 `tree` 字段没有任何东西校验

**状态**：待办（协调层开的独立一笔，未写代码）
**流**：另开一支，base `main @ 502c04c`
**母题**：`docs/BOARD.md` §14 里讲两份 pin 的那一行现在写着「headings 换假 sha ⇒ rc=4 红（活的）／citations 换假 sha ⇒ 仍 rc=0（缺口）」。这一笔把那个缺口补上，把那行从「缺口」改成**两条都红**的实测读数。

---

## 1. 现场（本树复现，命令与输出逐字）

```
$ python3 - <<'PY'
import json, pathlib
p = pathlib.Path("tools/citations.pin.json")
d = json.loads(p.read_text()); d["tree"] = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeef"
p.write_text(json.dumps(d, indent=2, sort_keys=True) + "\n")
PY
$ python3 tools/check_line_refs.py; echo "rc=$?"
… CITATIONS|pin|tree=deadbee|citations=1199|rows=1199
check_line_refs: pins -- explicit delta +12 (expect +12), unanchored delta +9 (ceiling +12).
VERDICT|assertions=0|causes=0|consequential=0|measured_cells=10|environment_cells=0|cause=none
check_line_refs: every pin held. …
rc=0
```

同一个伪造在 `check_docs_headings.py` 上是红的（`rc=4`，报 `the pin names tree '…' and this repository cannot resolve it`）。**两份 pin 的可靠性不对等，这就是缺口。**

## 2. 要改的地方（符号名从 `grep -n` 输出粘，不凭记忆）

```
$ grep -n "CITATIONS_PIN\|CITATIONS_EXEMPT" tools/check_line_refs.py
535:CITATIONS_PIN = REPO_ROOT / "tools" / "citations.pin.json"
536:CITATIONS_EXEMPT = REPO_ROOT / "tools" / "citations.exempt.json"

$ grep -n "_pin = read_json_or(CITATIONS_PIN" tools/check_line_refs.py
1469:    _pin = read_json_or(CITATIONS_PIN, "the citation pin")

$ grep -n "CITATIONS|pin|tree=" tools/check_line_refs.py
1472:            f"CITATIONS|pin|tree={str(_pin.get('tree', '<unpinned>'))[:7]}"
```

现在 `_pin` 只被**读数**，从不被**校验**：`tree` 字段印出来就完事，`rows`/`citations` 也是从 pin 里直接抄。

## 3. 判据

1. 读到 `_pin` 之后，把 `tree` 解析成这个仓库里真实存在的对象（建议 `git cat-file -e <sha>^{tree}`，或 `git rev-parse --verify --quiet <sha>^{object}`；用哪条以实测为准，写进提交信息）。
2. 解析失败 ⇒ **一个 assertion**、`rc != 0`，且行上印出失败的 sha 前 7 位（`tree-unresolved=<sha7>`），不要只说「pin 有问题」。
3. `tree` 缺失或为 `None` 也要红（`<unpinned>` 现在是印出来的，但它应该是一个判词）。
4. **不许动计数**：`explicit` 增量仍须 `+12`、`unanchored` 仍须 `+9`。这一笔改的是校验，不是秤。

## 4. 反向判据（必须逐条跑，贴读数）

1. 把 `tree` 换成 `deadbeef…` ⇒ 红、`rc != 0`、点名该 sha。
2. 换成真 sha（或删掉字段）⇒ 分别给出红/绿的读数，说明两个分支都被走过了。
3. 把 `tree` 指到一棵**存在但与计数无关**的树 ⇒ **必须绿**，并写明这是已知边界（见 §6）。
4. `--self-check` 里加格：一个能解析的 sha ⇒ 无问题；一个拼错的 sha ⇒ 有问题。格数变化要在输出里（`ok (N checks)` 的 N 必须真的动）。

## 5. 交付时报告

- `python3 tools/check_line_refs.py --self-check` 的 `ok (N checks)`；
- 反向判据 1/2/3 的逐字读数；
- 新的 `docs/BOARD.md` §14 那行文本（要能读成「两条都红」的实测，不是「将红」）。

## 6. 诚实边界（要写进页面，不只写进提交信息）

解析 `tree` 只证明**那棵对象在这个仓库里存在**，不证明 pin 里那些计数是**在它上面**读的 —— 后者要重算一遍才能证明，那是另一笔（重钉时顺手算一次即可，但不能拿解析成功冒充它）。这句话必须进 `docs/streams/checker-scope.md` 的同一节，因为那里正是「读数管不到什么」的页面。

## 7. 写域

`tools/check_line_refs.py`、`tools/citations.pin.json`（重钉）、`docs/BOARD.md`（仅 §14 那行）、`docs/streams/checker-scope.md`（仅补一节边界）。**禁碰** `src/**`、`CMakeLists.txt`、`tools/gate.sh`、其余检查器。
