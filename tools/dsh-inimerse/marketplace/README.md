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

## Why the listing is not live yet

dsh-m's registry v1 schema only accepts `source: "npm"` or `source: "github"`,
and a GitHub-source entry installs the **repository root** as the package —
which for this repository is the engine, not the plugin. dsh-m explicitly
skips monorepo subpackages without an npm package. So the only workable
source is **npm**.

An npmjs.org token now exists in `~/.npmrc` and authenticates correctly
(`npm whoami --registry=https://registry.npmjs.org/` → `infileap`), but it
cannot publish:

```console
$ npm publish --registry=https://registry.npmjs.org/ --access public
npm notice Publishing to https://registry.npmjs.org/ with tag latest and public access
npm error code E403
npm error 403 403 Forbidden - PUT https://registry.npmjs.org/dsh-inimerse
npm error Two-factor authentication or granular access token with bypass 2fa
npm error enabled is required to publish packages.
```

The token is therefore either a **Classic "Publish"** token or a **Granular**
token without *Bypass 2FA*. Either of these works instead:

- **Classic Token → Automation** (bypasses 2FA by design), or
- **Granular Access Token** with **Bypass 2FA** checked, *Read and write*
  permission, *All packages*.

Do **not** try `npm login`: on this machine npm 12.0.2's web-login flow polls
`/-/v1/done` for ~30 s, gets a `404` when the browser grant is not completed,
falls back to `web login not supported, trying couch`, and then dies with
`Exit handler never called!` at the `Username:` prompt (non-interactive TTY).
The debug log is unambiguous about that sequence.

The name `dsh-inimerse` is still free on npmjs (`404` from the registry API) —
the 403 above published nothing.

## Step 1 — publish to npm

With a token that can publish (see above):

```bash
cd tools/dsh-inimerse
npm publish --registry=https://registry.npmjs.org/ --access public
```

`publishConfig` already pins `registry.npmjs.org` and `access: public`, so the
plain `npm publish` is equivalent. `--dry-run` is green today: 8 files,
`14.9 kB` packed / `51.3 kB` unpacked, shasum
`9038702626b5501c2908f743f31ccd6fef047aa5`.

## Step 2 — get the entry into the curated list

`registry.json` upstream is hand-curated; a listing is a pull request to
`iasiv5/dsh-m` that appends `entry.json` to its `registry.json`. **Step 1 must
land first**: upstream CI (`.github/workflows/registry.yml` →
`scripts/validate-registry.mjs`) fetches
`https://registry.npmjs.org/<npm>/latest` and fails the build if it 404s.

The PR is **two** files, not one — upstream also pins the curated-list length
in its own test suite:

| File | Change |
| --- | --- |
| `registry.json` | append the entry (byte-minimal, 20 lines) |
| `tests/registry.test.mjs` | `parsed.registry.plugins.length` `23` → `24` |

Without the second hunk `npm test` fails even when the validator passes.

A branch carrying both changes is prepared and verified at
`/home/sakiko/inimerse/.dshm-pr` (clone of upstream `f18fc81`, branch
`add-dsh-inimerse`, commit `1f13859`; the clone is listed in
`.git/info/exclude` so it stays out of `git status`). It reproduces upstream CI
locally:

- `npm ci` → `npm run build` → `[dsh-m] build ok: lib/host.js + lib/client.js`.
- `node scripts/validate-registry.mjs` → every check green for all 23 existing
  entries; the run then stops at
  `✗ [dsh-inimerse] npm 查询 dsh-inimerse → HTTP 404`, which is exactly the
  check step 1 unblocks. This entry's `github` / `homepage` / `icon` checks sit
  behind the same `try` block and so are re-verified on the next run;
  `infileap/Inimerse` is confirmed **public** (`api.github.com` → `200`,
  `private: false`, default branch `main`) and `icon.svg` is on `main`.
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
