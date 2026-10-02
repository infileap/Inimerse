# 流简报：`vverse-produce`

> 这是给**一个 DSH 对话**的作业单。开工前先读 [BOARD.md](../BOARD.md) §1–§4。
> 分支 `stream/vverse-produce`，冲突域 `src/`、`vtest/`。

## 1. 要交付什么

**引擎能自己产出 `.vverse` 包**，产出的包能通过 `tools/vverse_validate.js` 校验，并且能被 `inim-server` 装载。

现状：打包器只存在于 `tools/vverse_pack.js`（60 行 JS）；引擎不会产出 `.vverse`，只会**读**。

## 2. 现有什么

- `tools/vverse_pack.js`（60 行）—— `pack(root, output)` / `unpack(input, destination)` / `preview(input)`
- `tools/vverse_validate.js`（95 行）—— `validate(base, { strictStructure, requireSignature, requireCompleteSignature })`、`writeSignature(base)`
- 断言：`tools/vverse_pack.test.js`（5 条）、`tools/vverse_validate.test.js`（10 条），均已纳入门禁
- 引擎侧读取路径：`src/mod/verse_dist_mod.c`（`hub_body` 从 `%s/%s.vverse` 读文件）、`src/platform/http_posix.c` 的 `hub_body`
- 已有可复用的密码学：`src/common/ed25519.{h,c}`、`src/common/sha256.{h,c}`

## 3. 格式铁律（照抄 `tools/vverse_pack.js`，别自己发明）

包体是 **gzip 压缩的 JSON**：

```js
const raw = JSON.stringify({ format: 'vverse-1', files: entries });
fs.writeFileSync(output, zlib.gzipSync(raw, { mtime: 0 }));
```

- `format` 必须是字面量 `'vverse-1'`
- `files` 是 `{ 路径: base64(原始字节) }` 的映射，路径用 `/` 分隔
- **`mtime: 0` 是确定性要求**——同一目录两次打包必须产出逐字节相同的文件。别漏
- 目录结构固定四件套：`laws/`、`assets/`、`mods/`、`signatures/`
- **`signatures/sha256.json` 必须在 `files` 里，同时也必须在签名表覆盖范围内之外**（见下）
- `signatures/ed25519.json` 可选

## 4. 签名语义（最容易搞错的地方）

`signatures/sha256.json` 是 `{ 路径: sha256hex }`：

- 每个条目是 **base64 解码后原始字节** 的 sha256 十六进制
- **`signatures/` 开头的条目不参与摘要校验**（`if (name.startsWith('signatures/')) continue;`）
- 反向检查同样严：`files` 里除 `signatures/sha256.json`、`signatures/ed25519.json` 之外，**每个文件都必须出现在签名表里**，否则报 `unsigned file: <name>`

`signatures/ed25519.json` 形状 `{ algorithm: 'ed25519', publicKey, signature }`，两个都是 base64：

- `publicKey` 是 **DER SPKI** 格式
- 被签名的字节是：把签名表里**排除 `signatures/` 的条目按文件名排序**后 `JSON.stringify` 的结果（UTF-8 字节）
- 验签用 `crypto.verify(null, msg, key, sig)`（Ed25519 内部自带哈希，`null` 表示不额外哈希）

## 5. 判据（量化）

1. 引擎产出的 `.vverse` 能通过 `node tools/vverse_validate.js`，且 `strictStructure: true` + `requireSignature: true` + `requireCompleteSignature: true` 三个开关都开着
2. 同一个源目录**连续打包两次，产物 sha256 相同**（证明 `mtime: 0` 和排序都做对了）
3. 引擎产出的包能被 `inim-server` 装载并服务（`GET /v/<id>` 拿到内容）
4. **交叉验证**：引擎打的包用 `tools/vverse_pack.js unpack` 能解开；`tools/vverse_pack.js` 打的包用引擎的读取路径能装载。两个方向都要有证据
5. 新增探针或测试注册进 `CMakeLists.txt`，进 `ctest`
6. `tools/gate.sh` 全绿

## 6. 已知的坑

- **`unpack` 有路径逃逸检查**：`if (target !== base && !target.startsWith(base + path.sep)) throw new Error('path escapes package: ' + name)`。引擎侧解包/打包同样要防 `../`
- **`preview` 的必填三项**：`manifest.json`、`blueprint.json`、`signatures/sha256.json` 缺一不可，报 `package missing required metadata`
- **长消息要用流式 SHA-512**：`src/common/ed25519.c` 里 `ed25519_sign`/`ed25519_verify` 已改成流式哈希 `k`。旧的 `sha512_buf` 是**一次性**接口，对超过 8192 字节的输入会静默截断——如果你要哈希大文件，用 `Sha512Ctx` + `sha512_init/update/final`，**不要**用 `sha512_buf`
- **别动 `.vverse` 的既有读取路径**：`src/mod/verse_dist_mod.c` 和 `src/platform/http_posix.c` 的 `hub_body` 已被 `hub_dist_regression` 等套件覆盖，改之前先看测试
- **`tools/*.js` 是参考实现，只读**。如果发现参考实现有 bug，报告给我，别顺手改——它同时是别的流的判据

## 7. 交回时给我

按 [BOARD.md](../BOARD.md) §4 的五项，第 4 项「没做什么 / 已知没解决什么」不能漏。
