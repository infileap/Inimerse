# examples/regressions/

Reproduction scripts for defects that are **already fixed**. They were kept
rather than deleted because each one is the only script of its kind: the
regression they pin down is named in the file itself. Moved verbatim out of the
repository root (`docs/HYGIENE.md` §4, batch 2 of `repo-hygiene`); the moves are
byte-identical.

None of these scripts is registered as a CTest case — `vtest/` holds the
registered cases. These are run by hand:

```bash
build/inimerse run examples/regressions/<file>.im
```

| File | Defect it reproduces |
| --- | --- |
| `atomic_test.im` | lost updates on a shared counter with 8 threads bumping it (`n = n + 1` used to lose ~75 %); exercises `atomic_set` / `atomic_add` / `atomic_get` |
| `cpu8m.im` | 8 threads each writing its own global — sharded-lock parallelism, `join` waits |
| `label_test.im` | `label` / `continue` / `restart` / thread-goto semantics |
| `restart_stress.im` | infinite-loop `task` plus repeated `stop` / `restart` (minimal reproduction) |
| `string_nested_crash_test.im` | 4-line nested-dict / task-result case that used to crash; expects `NESTED_DICT_OK` |
| `task_loop_crash.im` | task repeated `start` / `join` in a loop → `0xC0000005` (probabilistic, 10–20 iterations) |
| `thread_test.im` | `restart` on a thread plus `thread to A` jumps |

The defect table is drawn from each file's own header comment, not from an
independent bisect — `docs/HYGIENE.md` §9 records that classification as a human
judgement.
