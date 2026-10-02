# examples/legacy-ui/

Eight single-file HTML prototypes of the desktop UI, kept because home.html in
particular has content that the embedded root-level copies no longer carry. The
current UI is `Infiverse_standard/src/ui/` (`index.html` + `app.js` + `app.css`).
Moved verbatim out of the repository root (`docs/HYGIENE.md` §4, batch 2 of
`repo-hygiene`); the moves are byte-identical.

| Prototype | Relation to the root-level `*_embed.h` (measured, not assumed) |
| --- | --- |
| `chat.html` | identical to the markup embedded in `chat_embed.h`, modulo one trailing newline |
| `desktop.html` | identical to `desktop_embed.h`, modulo one trailing newline; frames the chat page via `<iframe src="/home">` |
| `netplay.html` | identical to `netplay_embed.h`, modulo one trailing newline |
| `wb.html` | identical to `wb_embed.h`, modulo one trailing newline |
| `verse_forge.html` | same markup as `forge_embed.h`, except that the generator inserted a space before every newline (26 449 B vs 26 894 B decoded) |
| `home.html` | **not** the source of `home_embed.h`: the header decodes to 3 900 B of an older, smaller page while `home.html` is 5 537 B. The extra markup (e.g. `.profile-head`) exists only here |
| `hl_renderer.html` | no embedded counterpart in the repository root |
| `workbench_web.html` | no embedded counterpart in the repository root |

Method: the C string arrays in the root `*_embed.h` files were decoded (octal
escapes → bytes) and compared byte-for-byte with the `.html` files. Full result
and its limits are recorded in `docs/HYGIENE.md` §10.

**Cost of this move.** The six generated headers stay in the repository root
because `hl_bridge.c` includes them, and no generator script exists anywhere in
the checkout that would rebuild them from HTML. Nothing in the build reads the
`.html` files, so the move breaks no build input. What it does do is separate the
human-readable source from the embedded copy: for the four identical pairs the
root header remains an exact snapshot, for `forge_embed.h` the snapshot matches
modulo whitespace, and for **`home_embed.h` the root copy is already stale** — the
newer markup now lives only under `examples/legacy-ui/`.

These pages are not opened from disk by the current build; they are rendered from
the embedded copies.
