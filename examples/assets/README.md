# examples/assets/

`monster8.bmp` — the shared texture used by the entity-stress benchmarks. Moved
here verbatim out of the repository root (`docs/HYGIENE.md` §6.2, batch 2 of
`repo-hygiene`; byte-identical, 246 B).

It is referenced by the **bare filename** `monster8.bmp` from:

- `examples/bench/es_200.im`, `es_400.im`, `es_800.im`, `es_1600.im` — left
  unchanged on purpose, so those four scripts resolve the texture only when the
  process cwd can see it (see [../bench/README.md](../bench/README.md));
- `entity_stress2.im` in the repository root — updated to the new path,
  `examples/assets/monster8.bmp`.

That string is a *cwd-relative filesystem path*, not an engine resource id:
`src/mod/gui_mod.c` hands it to `LoadImageA` / WIC unchanged.
