# examples/scripts/

Runnable `.im` demos, moved verbatim out of the repository root
(`docs/HYGIENE.md` §4, batch 2 of `repo-hygiene`). Byte-identical moves.

| File | What it is |
| --- | --- |
| `canvas_demo.im` | canvas / procedural-drawing demo |
| `verse_biome_demo.im` | verse biome demo |
| `im2d_test.im` | immediate-mode 2D drawing walkthrough (`on_load` entry) |
| `block_edit.im` | CLI tool: list / extract / replace / strip / rename named code blocks in a source file. Takes the target file as an argument, so it carries no repository-relative path |
| `textbox.im` | text-box widget sample; the header comment documents `import "textbox.im"`, i.e. it is meant to be imported by another script |

Run one from the repository root:

```bash
build/inimerse run examples/scripts/canvas_demo.im
```

`block_edit.im` needs its own arguments, e.g.:

```bash
build/inimerse run examples/scripts/block_edit.im list some_file.im
```
