# Publishing `dsh-inimerse` to the dsh-m marketplace

This directory holds the two artifacts that put the plugin into the
[`dsh-m`](https://github.com/iasiv5/dsh-m) marketplace, plus the commands that
were run to verify them.

| File | What it is |
| --- | --- |
| `entry.json` | The registry entry for `dsh-inimerse`, exactly as it would be merged upstream. |
| `registry.json` | A local preview registry = the official default list **plus** `entry.json` (24 entries). Used for local verification only. |

## What is already done

- `package.json` is marketplace-ready: no `private`, with `description`,
  `keywords`, `author`, `repository` (`directory: tools/dsh-inimerse`),
  `homepage`, `bugs`, `engines`, `publishConfig` (npmjs, public) and `license: MIT`
  (matching the repository `LICENSE`; the earlier `BSD-3-Clause` was wrong).
- `LICENSE` is present in the package directory.
- `npm pack --dry-run` produces a clean tarball: 8 files, 14.9 kB packed /
  51.3 kB unpacked — `index.js`, `cordis.patch.yml`, `README.md`, `icon.svg`,
  `locale/*.json`, `package.json`, `LICENSE`. No `node_modules`, no lockfile,
  no dev-only files.
- The **real** tarball was packed, extracted and imported outside the
  repository: `dsh-inimerse-0.1.0.tgz`, 14,905 bytes, whose `index.js` loads
  standalone and exports exactly `{ apply, inject, name }`. It imports only
  `node:child_process`, `node:fs` and `node:path`, so there is no dependency
  that could be missing from the published artifact.
- Both `entry.json` alone and the full `registry.json` pass dsh-m's **own**
  validator (`validateRegistry` from `dsh-m/lib/core/registry.js`) with zero
  errors.
- The local preview registry was served through dsh-m's CLI and listed
  correctly (`来源：本地文件源 · 共 24 条`; `Inimerse Bridge (dsh-inimerse)· 工具 · npm`).
- `registry.json` here is **regenerated from upstream `HEAD`** (`f18fc81`),
  not from the copy bundled with the installed dsh-m: the bundled snapshot was
  already stale (its `dsh-skins` description stopped at `0.2.0-rc.1`, upstream
  says `0.2.0-rc.1/rc.2`). `diff` against upstream is exactly one added entry.

## Status

`dsh-inimerse@0.1.0` is **published** — the first blocker is gone:

```console
$ npm publish --registry=https://registry.npmjs.org/ --access public
+ dsh-inimerse@0.1.0

$ npm view dsh-inimerse version --registry=https://registry.npmjs.org/
0.1.0
$ npm view dsh-inimerse dist.shasum license --registry=https://registry.npmjs.org/
dist.shasum = '9038702626b5501c2908f743f31ccd6fef047aa5'
license = 'MIT'
```

The only remaining step is the upstream PR, which needs the fork
`infileap/dsh-m` — it does not exist yet, and `git push` cannot create a
repository.

### Why the entry carries no `icon`

An earlier draft pointed `icon` at
`https://raw.githubusercontent.com/infileap/Inimerse/main/tools/dsh-inimerse/icon.svg`.
That file really is public — GitHub's contents API returns it (`icon.svg`,
297 bytes) and jsDelivr serves it with `200` — but `raw.githubusercontent.com`
is unreachable from this machine by every route tried (`curl --noproxy '*'`,
Node `fetch`, and the harness's own fetcher all fail with a connection reset),
so the URL could not be verified before handing the PR over.

No entry in the upstream list uses `icon`, and dsh-m falls back to the GitHub
owner's avatar (`lib/client.js`: `entry.icon || https://github.com/<owner>.png?size=64`),
so the card still shows an image. Re-adding the field is the only change needed
once someone can check it from a network that reaches `raw.githubusercontent.com`.

### Only npm is a workable source

dsh-m's registry v1 schema only accepts `source: "npm"` or `source: "github"`,
and a GitHub-source entry installs the **repository root** as the package —
which for this repository is the engine, not the plugin. dsh-m explicitly
skips monorepo subpackages without an npm package.

### For anyone repeating the publish: the token trap

The first attempts failed with `E403 … Two-factor authentication or granular
access token with bypass 2fa enabled is required to publish packages.` It was
the registry, not the CLI, doing the refusing:

| Probe | Result | What it proves |
| --- | --- | --- |
| `GET /-/npm/v1/user` | `{"tfa":false, …}` | the account had **2FA off** |
| `GET /-/npm/v1/tokens` | `200` `{"objects":[],"total":0}` | there were zero classic tokens, so the credential was **Granular** |
| `PUT /dsh-inimerse` (raw `curl`, `Bearer`, no CLI) | `403` with the message above | the refusal is the registry's |
| that same `PUT` plus `npm-otp: 000000` | byte-identical `403` | an OTP can never satisfy it — a dead end, not a prompt for a code |

The fix was to re-create the token as a **Granular Access Token with *Bypass
2FA* checked**, `Read and write (publish and stage)` — **not** `(stage only)`,
which only permits `npm stage publish` — and *All packages*.

Do **not** use `npm login` on this machine: npm 12.0.2's web-login flow polls
`/-/v1/done` for ~30 s, gets a `404` when the browser grant is not completed,
falls back to `web login not supported, trying couch`, and then dies with
`Exit handler never called!` at the `Username:` prompt (non-interactive TTY).
The debug log is unambiguous about that sequence.

## Step 1 — publish to npm (done)

```bash
cd tools/dsh-inimerse
npm publish --registry=https://registry.npmjs.org/ --access public
```

`publishConfig` already pins `registry.npmjs.org` and `access: public`, so the
plain `npm publish` is equivalent. The tarball is 8 files, `14.9 kB` packed /
`51.3 kB` unpacked, shasum `9038702626b5501c2908f743f31ccd6fef047aa5` — the same
shasum the registry now serves.

## Step 2 — get the entry into the curated list

`registry.json` upstream is hand-curated; a listing is a pull request to
`iasiv5/dsh-m` that appends `entry.json` to its `registry.json`. Step 1 has
landed, so the CI check that fetches
`https://registry.npmjs.org/<npm>/latest` now resolves.

The PR is **two** files, not one — upstream also pins the curated-list length
in its own test suite:

| File | Change |
| --- | --- |
| `registry.json` | append the entry (byte-minimal, 19 lines) |
| `tests/registry.test.mjs` | `parsed.registry.plugins.length` `23` → `24` |

Without the second hunk `npm test` fails even when the validator passes.

A branch carrying both changes is prepared at
`/home/sakiko/inimerse/.dshm-pr` (clone of upstream `f18fc81`, branch
`add-dsh-inimerse`, commit `67a16c9`; the clone is listed in
`.git/info/exclude` so it stays out of `git status`). `git diff f18fc81` there
is exactly `registry.json | 19 +` and `tests/registry.test.mjs | 2 +-`.

It reproduces upstream CI locally:

- `npm ci` → `npm run build` → `[dsh-m] build ok: lib/host.js + lib/client.js`.
- `node scripts/validate-registry.mjs` → for `dsh-inimerse` the npm, GitHub and
  homepage checks all pass. Run unauthenticated it eventually reports
  `✗ … GitHub API 限额用尽（设置 GITHUB_TOKEN 可解）` for the tail of the list,
  because the whole script shares one unauthenticated budget and upstream CI
  supplies `secrets.GITHUB_TOKEN`. `infileap/Inimerse` was confirmed **public**
  independently (`api.github.com` → `200`, `private: false`, default branch
  `main`).
- `npm test` → the only failures are two pre-existing, environment-dependent
  cases in `tests/run-command-error-digest.test.mjs` (`leader close… 实际
  112ms`); they reproduce identically on pristine `f18fc81` and never read
  `registry.json`.

Pushing needs a fork: `infileap/dsh-m` does not exist, and `git push` does not
create repositories — so the fork is a click on GitHub, after which the branch
only has to be pushed and the PR opened.

The entry deliberately uses Chinese display text and tags because dsh-m renders
the curated list Chinese-first (every existing entry does the same):

```json
{
  "id": "dsh-inimerse",
  "name": "Inimerse Bridge",
  "description": "DSH 里的 Inimerse 引擎接口：构建、测试、运行 .im 脚本、驱动 Verse 层。已适配 0.2.0-rc.2，详见仓库。",
  "category": "tools",
  "tags": ["引擎", "构建", "测试", "Verse"],
  "verified": ["0.2.0-rc.2"],
  "source": "npm",
  "npm": "dsh-inimerse",
  "github": "infileap/Inimerse",
  "homepage": "https://github.com/infileap/Inimerse/tree/main/tools/dsh-inimerse",
  "icon": "https://raw.githubusercontent.com/infileap/Inimerse/main/tools/dsh-inimerse/icon.svg"
}
```

`verified` records the DSH runtime versions this plugin was actually tested
against — declarative, never a gate for installation.

### What upstream CI actually enforces

Read from `scripts/validate-registry.mjs` (141 lines) in `iasiv5/dsh-m`:

**Hard failures** — schema via dsh-m's own `validateRegistry`; duplicate `id`s;
the `npm` package must exist on npmjs; the `github` repo must exist; every
`homepage` and `icon` URL must answer a `HEAD` request (200 or 405, 2 attempts).

**Soft warnings** (`docs/registry-copy-guide.md`; they do not fail the build,
but the PR reviewer asks for a reason per warning) — description over
**60 full-width units** (CJK glyph = 1, ASCII = 0.5; the card clamps at two
lines); a tag starting with `需`/`推荐`/`require`; a full-width-parenthesised
compatibility tail `（…适配/需/依赖/推荐…）`; and, for any description
containing `适配`, every `0.x.y(-rc.N)` version it names must appear in
`verified`.

The first draft of this entry failed the width rule at **62.5 units**. The
current description is **49.5**, keeps the §4 compatibility sentence
(`已适配 0.2.0-rc.2，详见仓库。`), and trims tags to the guide's ≤4 budget —
the `0.2.0-rc.2` it names is present in `verified`, so the version-subset
warning stays silent.

Key order follows the upstream house style
(`id, name, description, category, tags, verified, source, npm, github,
homepage`) so the patch is a byte-minimal append; `icon` sits last because no
existing entry uses it yet.

## Preview locally without waiting for the PR

dsh-m supports a custom registry address that **overrides** the default list
wholesale (HTTPS URL, loopback HTTP URL, or a local absolute path / `file://`):

```console
$ DSHM_REGISTRY_URL=$PWD/tools/dsh-inimerse/marketplace/registry.json \
    node ~/.dsh/profiles/web/node_modules/dsh-m/lib/cli.js search inimerse --source primary
  ...
  来源：本地文件源 · 更新：2026-10-01T10:31:13.800Z · 共 24 条
```

`DSHM_REGISTRY_URL` only affects the standalone CLI process. The GUI and the
`dshm_*` agent tools read the same setting from dsh-m's config
(`registryUrl`), which the settings page writes — point it at this file only
if you accept that it is a frozen snapshot and will not follow upstream
updates; remove it (or hit "恢复默认") when the real listing lands.

**Note:** an entry can be listed before its npm package exists, but dsh-m's
install action resolves the package from npm, so installing from the local
preview registry will fail until step 1 is done. Use
`plugin_manager { action: "install_bundle" }` (or the local bundle install
already performed) until then.
