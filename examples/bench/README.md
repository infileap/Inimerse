# examples/bench/

Measurement entry points (`*.ps1`) together with the `.im` inputs they drive.
They were moved out of the repository root as one unit, because the scripts name
their inputs by bare filename (`cpu1.im`, `es_200.im`, …) and split them apart
would have broken that relationship. Moved verbatim (`docs/HYGIENE.md` §4 and
§6.2, batch 2 of `repo-hygiene`); all moves are byte-identical.

| Entry point | Inputs | What it measures |
| --- | --- | --- |
| `cpu_measure.ps1` | `cpu1.im`, `cpu8.im`, `cpu8s.im` | wall / CPU time per run: one thread, eight independent threads, eight threads on one shared global |
| `es_bench.ps1` | `es_200.im`, `es_400.im`, `es_800.im`, `es_1600.im` | sprite-count scaling (200 → 1 600 shared-texture sprites) |

Both scripts are **Windows-only dev tooling**: they hard-code
`D:\inimerse_stable\inimerse.exe`, launch it with `Start-Process`, and assume the
current working directory contains the `.im` inputs — which is why the entries and
their inputs share this directory. They are not referenced by `CMakeLists.txt`,
`vtest/` or `tools/gate.sh`.

**Known dangling asset reference (unfixed on purpose).** `es_200/400/800/1600.im`
ask for a texture by the bare name `monster8.bmp`. That string is a
*cwd-relative filesystem path*: `src/mod/gui_mod.c` passes it to `LoadImageA` /
WIC unchanged. The texture now lives at `examples/assets/monster8.bmp`, so these
four scripts only resolve it when the process cwd contains that file (e.g. run
with the cwd set to `examples/assets`, or copy the `.bmp` next to them). The same
bare name is still written in `entity_stress2.im` in the repository root, which
was updated to the new path (`examples/assets/monster8.bmp`).

The reference was left as-is here because batch 2's acceptance rule is that every
*relocated* file stays byte-identical, and editing the texture name inside these
four scripts would have broken that. Recorded in `docs/HYGIENE.md` §10.
