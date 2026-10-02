# examples/ — scripts re-homed out of the repository root

This tree holds files that used to sit in the repository root. They were moved
here by the `repo-hygiene` stream, batch 2 (**move only, delete nothing**), under
the classification in [docs/HYGIENE.md](../docs/HYGIENE.md) §4 and §6.2.

Every file below is a **byte-identical** move: each was relocated with `git mv`
and verified before/after with `git hash-object`. No content was edited while
moving. The before/after blob hashes are tabulated in
[docs/HYGIENE.md](../docs/HYGIENE.md) §10.

| Directory | Contents | Source |
| --- | --- | --- |
| [scripts/](scripts/) | runnable demos (`canvas_demo.im`, `verse_biome_demo.im`, `im2d_test.im`, `block_edit.im`, `textbox.im`) | HYGIENE.md §4 |
| [regressions/](regressions/) | reproduction scripts for already-fixed crashes / races | HYGIENE.md §4 |
| [legacy-ui/](legacy-ui/) | 8 standalone HTML prototypes of the UI that predate `Infiverse_standard/src/ui/` | HYGIENE.md §4 |
| [bench/](bench/) | measurement entry points plus the `.im` inputs they drive | HYGIENE.md §4, §6.2 |
| [assets/](assets/) | `monster8.bmp`, the shared texture used by the entity-stress benchmarks | HYGIENE.md §6.2 |

Run a script with the engine binary at the repository root:

```bash
build/inimerse run examples/scripts/canvas_demo.im
```

`build.gradle`, `pom.xml`, `interface.def` and the `*_bridge.im` / `cpp_native.im`
files in this directory were **already here** (cross-language bridge samples);
they are unrelated to the batch-2 move and were not touched.
