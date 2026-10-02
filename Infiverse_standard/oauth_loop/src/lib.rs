//! Tauri-free extraction of the OAuth loop-back logic that lives in
//! `Infiverse_standard/src-tauri/src/lib.rs:918-980`:
//! `oauth_status`, `oauth_authorize`, `oauth_open`, `oauth_start_callback`,
//! `oauth_poll_callback`.
//!
//! WHY THIS CRATE EXISTS
//! ---------------------
//! The Tauri shell cannot be built on this machine (`webkit2gtk-4.1`,
//! `javascriptcoregtk-4.1`, `libsoup-3.0`, `gtk+-3.0` and even `pkg-config`
//! are all missing, and there is no passwordless sudo), so BOARD row 100
//! (`oauth-bind`) had no way to show a REAL loop-back.  The OAuth loop itself
//! is ordinary Rust and needs nothing from Tauri, so it lives here and can be
//! exercised over a real `127.0.0.1` socket by `cargo test`.
//!
//! FULL `std` ONLY: no tauri, no serde, no serde_json, no reqwest.  The one
//! JSON value the original produced with `serde_json::json!` is emitted by a
//! hand-written emitter below (see `status`).
//!
//! WHAT THIS CRATE DOES **NOT** PROVE
//! ----------------------------------
//! Read this before citing `cargo test` as evidence that OAuth works.  What is
//! demonstrated is *transport*: a TCP connection to a loop-back port is
//! accepted, the query string of the request-target is extracted and kept in
//! memory, it can be read back, and the connection is answered `200 OK`.
//!
//! It does NOT demonstrate any of the following, and the gaps are preserved
//! from the original rather than invented here:
//!
//! 1. NO TOKEN EXCHANGE.  Nothing anywhere turns the `code` into an access
//!    token.  There is no HTTP client, no token endpoint, no `client_secret`,
//!    no `grant_type` — in this crate or anywhere under `Infiverse_standard/`.
//!    A `code` is received and stored, nothing more.
//! 2. NO `oauth_bind`.  That name exists only in front-end prose
//!    (`Infiverse_standard/src/ui/app.js:285`); no such command exists.  And
//!    `linked_accounts.json` is only ever READ (original `lib.rs:920`) and is
//!    written nowhere in the repository, so `oauth_status` cannot report
//!    `linked: true` through this UI.  `app.js:285`'s promise that the code is
//!    exchanged and the profile saved is false in both halves.
//! 3. NO `state` VERIFICATION.  The caller generates `state`
//!    (`app.js:359`) and it is interpolated into the authorize URL, but no code
//!    path anywhere compares the returned `state` against the sent one.  Since
//!    the private `serve_once` accepts the first connection on the port and serves only
//!    one, any local process — or any page the user's browser loads — can
//!    deliver a forged callback that is indistinguishable from the real one,
//!    and it also consumes the single slot, destroying the genuine callback.
//! 4. NO `redirect_uri` BINDING.  The request method, path and `Host` are never
//!    checked; only the substring after `?` is used.  This is intentional
//!    fidelity to the original (`lib.rs:964-970`), not an oversight.
//! 5. ONE CONNECTION ONLY, WITH A TIMING DIVERGENCE.  See [`CALLBACK_WAIT`] —
//!    this crate is NOT faithful to the original here, and the case it changes
//!    is the one real users hit.
//!
//! Items 1, 3 and 4 are what BOARD row 100 is *titled* about ("token 交换与资料
//! 绑定") even though its stated criterion asks only for loop-back evidence.
//! The transactional half of that title is tracked as a separate row.
//!
//! WHAT THE TAURI COMMANDS BECOME
//! ------------------------------
//! The original `#[tauri::command]` wrappers returned `serde_json::Value`
//! envelopes (`{"ok":true}` / `{"ok":false,"error":...}`) because that is what
//! the JS front-end (`Infiverse_standard/src/ui/app.js:346-372`) reads.  The
//! extracted functions return plain Rust values instead and leave the envelope
//! to the shell; the guard conditions and side effects are unchanged.

use std::io::{Read, Write};
use std::net::{SocketAddr, TcpListener};
use std::sync::{Arc, Condvar, Mutex, OnceLock};
use std::thread;
use std::time::{Duration, Instant};

/// Exact body text the loop-back listener replies with (lib.rs:971).
pub const CALLBACK_BODY: &str = "Authorization received. You can return to Infiverse.";

/// The fixed address `oauth_start_callback` bound (lib.rs:963).
pub const DEFAULT_CALLBACK_ADDR: &str = "127.0.0.1:8765";

/// How long a [`CallbackHandle`] waits for its single connection before giving
/// up and reporting the empty string.
///
/// `None` MEANS WAIT FOREVER, AND THAT IS THE FAITHFUL VALUE.
/// The original's listener thread called `listener.incoming().flatten().next()`
/// (`lib.rs:964`), which blocks *indefinitely* until a connection arrives.
/// What was non-blocking was the **JS** side, which polled `oauth_poll_callback`
/// once a second (`app.js:366`) — conflating the two is what made an earlier
/// version of this crate cap the wait at 5 seconds and look harmless.
///
/// It was not harmless.  Because [`start_callback`] routes through
/// [`CallbackHandle::query`], a callback arriving after the cap was still
/// answered `200 OK` but its query went into a slot nobody would read again —
/// silently discarded.  The consumer's own window is about 60 seconds
/// (`app.js:366` polls 60 times at 1 Hz), so the discarded case was a real
/// human typing a password or completing 2FA.  Measured with
/// `--test-threads=1`: a callback at T+5s returned the query; a callback at
/// T+6s returned `""` while still receiving `200 OK`.
///
/// [`start_callback_on`] callers that want a bounded wait should use
/// [`CallbackHandle::query_timeout`] instead of changing this constant.
pub const CALLBACK_WAIT: Option<Duration> = None;

/// A bounded wait, for callers that cannot block a thread forever (tests, and
/// any future UI that wants to give up).  Not used by [`start_callback`], whose
/// behaviour must match the original.
pub const CALLBACK_TEST_WAIT: Duration = Duration::from_secs(5);

/// The read size the original used (lib.rs:965).
const READ_BUF: usize = 4096;

// ---------------------------------------------------------------------------
// authorize
// ---------------------------------------------------------------------------

/// Byte-for-byte reproduction of `oauth_authorize` (lib.rs:929-936).
///
/// The emptiness test trims, but the interpolated value does NOT: a
/// `client_id` of `"  CID  "` is passed through verbatim.  That asymmetry is
/// the original's, and it is preserved here on purpose.
pub fn oauth_authorize(provider: &str, client_id: &str, redirect_uri: &str, state: &str) -> String {
    if client_id.trim().is_empty() || redirect_uri.trim().is_empty() {
        return String::new();
    }
    if provider.eq_ignore_ascii_case("github") {
        format!(
            "https://github.com/login/oauth/authorize?client_id={}&redirect_uri={}&scope=read:user%20user:email&state={}",
            client_id, redirect_uri, state
        )
    } else if provider.eq_ignore_ascii_case("bilibili") {
        format!(
            "https://passport.bilibili.com/oauth2/authorize?client_id={}&response_type=code&redirect_uri={}&state={}",
            client_id, redirect_uri, state
        )
    } else {
        String::new()
    }
}

// ---------------------------------------------------------------------------
// PKCE (RFC 7636)
// ---------------------------------------------------------------------------

/// A PKCE verifier and its S256 challenge.
///
/// WHY PKCE, AND NOT A `client_secret`: this desktop app is a public client —
/// there is no backend to hold a secret and no place in the repository to store
/// one.  A shipped `client_secret` is not a secret, so the transactional layer
/// uses the flow designed for exactly this situation.  See the crate-level
/// notes for what that does and does not buy.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Pkce {
    /// 43–128 chars of unreserved ASCII (`RFC 7636` §4.1).
    pub verifier: String,
    /// `BASE64URL-ENCODE(SHA256(ASCII(verifier)))` (`RFC 7636` §4.2).
    pub challenge: String,
}

impl Pkce {
    /// Build from an explicit verifier.  Kept separate from [`Pkce::generate`] so
    /// the challenge derivation can be pinned against the RFC's own test vector
    /// without depending on a random source.
    pub fn from_verifier(verifier: &str) -> Self {
        Self {
            verifier: verifier.to_string(),
            challenge: s256_challenge(verifier),
        }
    }

    /// A fresh verifier from OS entropy: 32 random bytes, base64url-unpadded —
    /// 43 characters, the minimum `RFC 7636` allows and comfortably inside the
    /// unreserved alphabet.
    pub fn generate() -> Self {
        let mut raw = [0u8; 32];
        fill_os_random(&mut raw).expect("OS entropy unavailable");
        Self::from_verifier(&base64_url(&raw))
    }

    /// The `code_challenge_method` value this challenge was built with.
    pub fn method(&self) -> &'static str {
        "S256"
    }
}

/// `BASE64URL-ENCODE(SHA256(ASCII(verifier)))`, without padding (`RFC 7636` §4.2).
pub fn s256_challenge(verifier: &str) -> String {
    base64_url(&sha256(verifier.as_bytes()))
}

/// Fill `out` from the operating system's entropy source.
///
/// Reads the platform device directly rather than taking a dependency, so the
/// crate stays std-only.  `/dev/urandom` never blocks on Linux and macOS;
/// Windows uses the documented `BCryptGenRandom` via the `windows-sys`
/// declarations already linked by the shell, but to keep this crate portable
/// and dependency-free the Windows path is deliberately reported as
/// unavailable rather than guessed at — the caller decides what to do, and on
/// Windows `Pkce::generate` documents that it needs entropy supplied another
/// way.  Failing loudly beats deriving a "random" verifier from a clock.
pub fn fill_os_random(out: &mut [u8]) -> Result<(), String> {
    #[cfg(unix)]
    {
        use std::io::Read;
        let mut f = std::fs::File::open("/dev/urandom")
            .map_err(|e| format!("cannot open /dev/urandom: {e}"))?;
        f.read_exact(out)
            .map_err(|e| format!("cannot read /dev/urandom: {e}"))?;
        Ok(())
    }
    #[cfg(not(unix))]
    {
        let _ = out;
        Err("no OS entropy source wired up for this platform".to_string())
    }
}

/// base64url without padding, the encoding `RFC 7636` specifies.
fn base64_url(bytes: &[u8]) -> String {
    const ALPHABET: &[u8; 64] =
        b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    let mut out = String::with_capacity((bytes.len() + 2) / 3 * 4);
    for chunk in bytes.chunks(3) {
        let b0 = chunk[0] as u32;
        let b1 = *chunk.get(1).unwrap_or(&0) as u32;
        let b2 = *chunk.get(2).unwrap_or(&0) as u32;
        let n = (b0 << 16) | (b1 << 8) | b2;
        out.push(ALPHABET[(n >> 18) as usize & 63] as char);
        out.push(ALPHABET[(n >> 12) as usize & 63] as char);
        if chunk.len() > 1 {
            out.push(ALPHABET[(n >> 6) as usize & 63] as char);
        }
        if chunk.len() > 2 {
            out.push(ALPHABET[n as usize & 63] as char);
        }
    }
    out
}

/// The exact `sha256(verifier)` hex, for reproducing the derivation by hand
/// (`openssl dgst -sha256`) when auditing.  Not used by the flow itself.
pub fn s256_hex(verifier: &str) -> String {
    sha256(verifier.as_bytes())
        .iter()
        .map(|b| format!("{b:02x}"))
        .collect()
}

/// SHA-256 (FIPS 180-4), implemented here to keep this crate dependency-free.
///
/// WHY A HAND-ROLLED HASH IS ACCEPTABLE *HERE*: the only input is the PKCE
/// verifier, which is public by construction — the verifier is sent to the
/// provider in the token request and the challenge is sent in the authorize URL.
/// Nothing secret is ever hashed with this, so the risk a hand-rolled
/// implementation usually carries (side channels, key handling) does not apply.
///
/// It is nonetheless pinned by the published vectors rather than trusted:
/// `sha256_known_answer_vectors` checks the empty string, `"abc"`, the
/// 56-byte and 1,000,000-byte NIST messages, and the RFC 7636 §B vector.
/// If you need a hash for anything else, use a real library instead.
pub fn sha256(data: &[u8]) -> [u8; 32] {
    const K: [u32; 64] = [
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
        0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
        0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
        0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
        0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2,
    ];
    let mut h: [u32; 8] = [
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab,
        0x5be0cd19,
    ];

    let mut msg = data.to_vec();
    let bit_len = (data.len() as u64).wrapping_mul(8);
    msg.push(0x80);
    while msg.len() % 64 != 56 {
        msg.push(0);
    }
    msg.extend_from_slice(&bit_len.to_be_bytes());

    let mut w = [0u32; 64];
    for block in msg.chunks_exact(64) {
        for i in 0..16 {
            w[i] = u32::from_be_bytes([
                block[i * 4],
                block[i * 4 + 1],
                block[i * 4 + 2],
                block[i * 4 + 3],
            ]);
        }
        for i in 16..64 {
            let s0 = w[i - 15].rotate_right(7) ^ w[i - 15].rotate_right(18) ^ (w[i - 15] >> 3);
            let s1 = w[i - 2].rotate_right(17) ^ w[i - 2].rotate_right(19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16]
                .wrapping_add(s0)
                .wrapping_add(w[i - 7])
                .wrapping_add(s1);
        }

        let (mut a, mut b, mut c, mut d, mut e, mut f, mut g, mut hh) =
            (h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7]);

        for i in 0..64 {
            let s1 = e.rotate_right(6) ^ e.rotate_right(11) ^ e.rotate_right(25);
            let ch = (e & f) ^ ((!e) & g);
            let t1 = hh
                .wrapping_add(s1)
                .wrapping_add(ch)
                .wrapping_add(K[i])
                .wrapping_add(w[i]);
            let s0 = a.rotate_right(2) ^ a.rotate_right(13) ^ a.rotate_right(22);
            let maj = (a & b) ^ (a & c) ^ (b & c);
            let t2 = s0.wrapping_add(maj);
            hh = g;
            g = f;
            f = e;
            e = d.wrapping_add(t1);
            d = c;
            c = b;
            b = a;
            a = t1.wrapping_add(t2);
        }

        h[0] = h[0].wrapping_add(a);
        h[1] = h[1].wrapping_add(b);
        h[2] = h[2].wrapping_add(c);
        h[3] = h[3].wrapping_add(d);
        h[4] = h[4].wrapping_add(e);
        h[5] = h[5].wrapping_add(f);
        h[6] = h[6].wrapping_add(g);
        h[7] = h[7].wrapping_add(hh);
    }

    let mut out = [0u8; 32];
    for (i, word) in h.iter().enumerate() {
        out[i * 4..i * 4 + 4].copy_from_slice(&word.to_be_bytes());
    }
    out
}

// ---------------------------------------------------------------------------
// token exchange
// ---------------------------------------------------------------------------

/// Endpoint each provider's `code` must be exchanged at.
pub fn token_endpoint(provider: &str) -> Option<&'static str> {
    if provider.eq_ignore_ascii_case("github") {
        Some("https://github.com/login/oauth/access_token")
    } else if provider.eq_ignore_ascii_case("bilibili") {
        Some("https://passport.bilibili.com/oauth2/token")
    } else {
        None
    }
}

/// The `application/x-www-form-urlencoded` body for the token request, built and
/// **percent-encoded here** so that no caller has to concatenate query strings.
///
/// Every value is encoded, including `code`, `state` and the verifier: all three
/// arrive from outside and a raw `&` or `=` in any of them would otherwise
/// inject extra form fields into a credentialed request.
pub fn token_request_body(
    provider: &str,
    code: &str,
    redirect_uri: &str,
    code_verifier: &str,
) -> Option<String> {
    let _ = token_endpoint(provider)?;
    Some(format!(
        "grant_type=authorization_code&code={}&redirect_uri={}&code_verifier={}",
        form_encode(code),
        form_encode(redirect_uri),
        form_encode(code_verifier),
    ))
}

/// Percent-encode one form value (`RFC 3986` unreserved set kept literal).
pub fn form_encode(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    for b in s.bytes() {
        match b {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'-' | b'.' | b'_' | b'~' => {
                out.push(b as char)
            }
            _ => out.push_str(&format!("%{b:02X}")),
        }
    }
    out
}

/// What a successful exchange yields, normalised across the two providers.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Token {
    pub access_token: String,
    /// GitHub returns `token_type=Bearer`; bilibili reports it differently, so
    /// it is optional rather than assumed.
    pub token_type: Option<String>,
    /// Present when the provider issued one; never invented.
    pub refresh_token: Option<String>,
    pub scope: Option<String>,
}

/// Parse a token endpoint's response body.
///
/// GitHub answers `application/json` for the device flow and form-encoded
/// otherwise (and `application/x-www-form-urlencoded` is also what bilibili
/// returns unless asked otherwise), so both shapes are accepted.  A body that
/// carries an `error` is a failure **even though the HTTP status was 200** —
/// both providers report bad codes that way, and treating 200 as success is how
/// a dead token gets stored as a live account.
pub fn parse_token_response(body: &str) -> Result<Token, String> {
    let (status, fields) = parse_kv_body(body);
    if let Some(err) = status.get("error") {
        let desc = status
            .get("error_description")
            .map(|d| format!(": {d}"))
            .unwrap_or_default();
        return Err(format!("provider refused the code ({err}{desc})"));
    }
    let access = fields
        .get("access_token")
        .or_else(|| status.get("access_token"))
        .filter(|t| !t.is_empty())
        .ok_or_else(|| "token response carried no access_token".to_string())?;
    // Read from `top` FIRST: a flat JSON object puts everything there, and
    // `fields` only ever holds bilibili's nested `data` payload. Looking in
    // `fields` alone silently lost token_type/refresh_token/scope for GitHub.
    let field = |k: &str| fields.get(k).or_else(|| status.get(k)).cloned();
    Ok(Token {
        access_token: access.clone(),
        token_type: field("token_type"),
        refresh_token: field("refresh_token"),
        scope: field("scope"),
    })
}

/// Flatten either a JSON object or a form-encoded body into one key/value map.
///
/// A hand-rolled JSON reader and a form body produce the same shape here, which
/// is what lets [`parse_token_response`] handle both without caring which the
/// provider chose.  This crate has NO dependencies on purpose (see the crate
/// docs), so `serde_json` is not available and pulling it in for one shallow
/// object read would undo the property the whole extraction exists to have.
///
/// Only the subset a token endpoint actually sends is understood: a flat object
/// of string/number/bool/null members, optionally with a nested `data` object.
/// Anything it cannot read yields no fields, which surfaces as
/// "carried no access_token" rather than as a wrong token.
fn parse_kv_body(
    body: &str,
) -> (
    std::collections::HashMap<String, String>,
    std::collections::HashMap<String, String>,
) {
    let mut top = std::collections::HashMap::new();
    let mut fields = std::collections::HashMap::new();

    if let Some(obj) = json_object(body) {
        for (k, v) in &obj {
            if let Some(s) = json_scalar(v) {
                top.insert(k.clone(), s);
            }
        }
        let mut data_pairs: Option<Vec<(String, String)>> = None;
        for (k, v) in obj.iter() {
            if k == "data" {
                data_pairs = json_object(v);
                break;
            }
        }
        if let Some(pairs) = data_pairs {
            for (dk, dv) in pairs {
                if let Some(sv) = json_scalar(&dv) {
                    fields.insert(dk, sv);
                }
            }
        }
        if top.is_empty() {
            for (k, v) in form_decode(body) {
                top.insert(k, v);
            }
        }
        return (top, fields);
    }

    for (k, v) in form_decode(body) {
        top.insert(k, v);
    }
    (top, fields)
}

/// Parse a JSON object at the top level into a `key -> raw value text` map.
///
/// Returns `None` unless the body really is an object.  Depth is tracked by
/// scanning strings and escapes so that a `}` inside a value cannot end the
/// object early.
fn json_object(body: &str) -> Option<Vec<(String, String)>> {
    let b = body.trim();
    if !b.starts_with('{') {
        return None;
    }
    let b = &b[1..];
    let mut out = Vec::new();
    let mut i = 0;
    let bytes = b.as_bytes();

    loop {
        while i < bytes.len() && (bytes[i] as char).is_whitespace() {
            i += 1;
        }
        if i >= bytes.len() {
            return None;
        }
        if bytes[i] == b'}' {
            return Some(out);
        }
        if bytes[i] == b',' {
            i += 1;
            continue;
        }
        if bytes[i] != b'"' {
            return None;
        }
        let (key, next) = read_json_string(b, i)?;
        i = next;
        while i < bytes.len() && (bytes[i] as char).is_whitespace() {
            i += 1;
        }
        if i >= bytes.len() || bytes[i] != b':' {
            return None;
        }
        i += 1;
        while i < bytes.len() && (bytes[i] as char).is_whitespace() {
            i += 1;
        }
        let start = i;
        let mut depth = 0i32;
        let mut in_str = false;
        let mut esc = false;
        while i < bytes.len() {
            let c = bytes[i];
            if in_str {
                if esc {
                    esc = false;
                } else if c == b'\\' {
                    esc = true;
                } else if c == b'"' {
                    in_str = false;
                }
            } else if c == b'"' {
                in_str = true;
            } else if c == b'{' || c == b'[' {
                depth += 1;
            } else if c == b'}' || c == b']' {
                if depth == 0 {
                    break;
                }
                depth -= 1;
            } else if c == b',' && depth == 0 {
                break;
            }
            i += 1;
        }
        out.push((key, b[start..i].trim().to_string()));
    }
}

/// Read a JSON string literal starting at `i` (which must be the opening quote).
/// Returns the unescaped value and the index just past the closing quote.
fn read_json_string(s: &str, i: usize) -> Option<(String, usize)> {
    let bytes = s.as_bytes();
    debug_assert_eq!(bytes[i], b'"');
    let mut out = String::new();
    let mut j = i + 1;
    while j < bytes.len() {
        match bytes[j] {
            b'"' => return Some((out, j + 1)),
            b'\\' => {
                j += 1;
                let e = *bytes.get(j)?;
                match e {
                    b'n' => out.push('\n'),
                    b't' => out.push('\t'),
                    b'r' => out.push('\r'),
                    b'b' => out.push('\u{8}'),
                    b'f' => out.push('\u{c}'),
                    b'u' => {
                        let hex = s.get(j + 1..j + 5)?;
                        let cp = u32::from_str_radix(hex, 16).ok()?;
                        out.push(char::from_u32(cp)?);
                        j += 4;
                    }
                    other => out.push(other as char),
                }
                j += 1;
            }
            c => {
                out.push(c as char);
                j += 1;
            }
        }
    }
    None
}

/// Turn a raw JSON value text into its string form, or `None` if it is not a
/// scalar.  Strings are unquoted and unescaped; numbers/bools pass through; null
/// is treated as absent so a `null` field never becomes the literal `"null"`.
fn json_scalar(raw: &str) -> Option<String> {
    let t = raw.trim();
    if t.is_empty() || t == "null" {
        return None;
    }
    if t.starts_with('"') {
        return read_json_string(t, 0).map(|(v, _)| v);
    }
    Some(t.to_string())
}

/// `application/x-www-form-urlencoded` decoding, including `+` as space.
fn form_decode(body: &str) -> Vec<(String, String)> {
    let mut out = Vec::new();
    for pair in body.split('&') {
        if pair.is_empty() {
            continue;
        }
        let (k, v) = pair.split_once('=').unwrap_or((pair, ""));
        out.push((percent_decode(k), percent_decode(v)));
    }
    out
}

fn percent_decode(s: &str) -> String {
    let b = s.as_bytes();
    let mut out = Vec::with_capacity(b.len());
    let mut i = 0;
    while i < b.len() {
        match b[i] {
            b'+' => {
                out.push(b' ');
                i += 1;
            }
            b'%' if i + 2 < b.len() => {
                let hex = std::str::from_utf8(&b[i + 1..i + 3]).unwrap_or("");
                match u8::from_str_radix(hex, 16) {
                    Ok(byte) => {
                        out.push(byte);
                        i += 3;
                    }
                    Err(_) => {
                        out.push(b'%');
                        i += 1;
                    }
                }
            }
            c => {
                out.push(c);
                i += 1;
            }
        }
    }
    String::from_utf8_lossy(&out).into_owned()
}

/// What the flow has to satisfy before an account may be written to disk.
///
/// Returning a `Result` rather than a bool makes the *reason* visible in the UI;
/// "linked failed" with no reason is what makes the original's silent paths
/// (bind failure returning `true`, callbacks dropped after 5s) so hard to
/// diagnose from a bug report.
pub fn verify_callback(expected_state: &str, query: &str) -> Result<String, String> {
    let fields = form_decode(query);
    let get = |k: &str| {
        fields
            .iter()
            .find(|(key, _)| key == k)
            .map(|(_, v)| v.clone())
    };

    if let Some(err) = get("error") {
        let desc = get("error_description")
            .map(|d| format!(": {d}"))
            .unwrap_or_default();
        return Err(format!("provider reported an error ({err}{desc})"));
    }

    let state = get("state").ok_or_else(|| "callback carried no state".to_string())?;
    if state != expected_state {
        return Err("state mismatch — this callback did not come from the request we started".to_string());
    }

    let code = get("code").ok_or_else(|| "callback carried no code".to_string())?;
    if code.is_empty() {
        return Err("callback carried an empty code".to_string());
    }
    Ok(code)
}

// ---------------------------------------------------------------------------
// open
// ---------------------------------------------------------------------------

/// The original allow-list guard (lib.rs:940).
pub fn is_allowed_authorize_url(url: &str) -> bool {
    url.starts_with("https://github.com/")
        || url.starts_with("https://passport.bilibili.com/oauth2/authorize")
}

/// `oauth_open` (lib.rs:938-954) with the browser launch made injectable.
///
/// The guard is evaluated first and identically; `opener` is called only for an
/// allow-listed URL, and receives the URL bytes unchanged.
pub fn oauth_open_with(
    url: &str,
    opener: &dyn Fn(&str) -> std::io::Result<()>,
) -> Result<(), String> {
    if !is_allowed_authorize_url(url) {
        return Err("Unsupported authorization URL".to_string());
    }
    opener(url).map_err(|e| e.to_string())
}

/// The real launcher: `oauth_open`'s platform command, verbatim (lib.rs:943-949).
///
/// This is the ONLY function in the crate that can actually start a process.
/// It is deliberately not exercised by any test.
pub fn oauth_open(url: &str) -> Result<(), String> {
    oauth_open_with(url, &|u: &str| {
        let mut cmd = if cfg!(windows) {
            let mut c = std::process::Command::new("cmd");
            c.args(["/C", "start", "", u]);
            c
        } else if cfg!(target_os = "macos") {
            let mut c = std::process::Command::new("open");
            c.arg(u);
            c
        } else {
            let mut c = std::process::Command::new("xdg-open");
            c.arg(u);
            c
        };
        cmd.spawn().map(|_| ())
    })
}

// ---------------------------------------------------------------------------
// callback listener
// ---------------------------------------------------------------------------

#[derive(Default)]
struct CallbackInner {
    query: String,
    served: bool,
}

struct CallbackState {
    inner: Mutex<CallbackInner>,
    cv: Condvar,
}

impl CallbackState {
    fn new() -> Self {
        CallbackState {
            inner: Mutex::new(CallbackInner::default()),
            cv: Condvar::new(),
        }
    }

    /// Mutex poisoning is not a reason to lose the answer, so both accessors
    /// recover the guard instead of unwrapping.
    fn lock(&self) -> std::sync::MutexGuard<'_, CallbackInner> {
        match self.inner.lock() {
            Ok(g) => g,
            Err(p) => p.into_inner(),
        }
    }
}

/// Handle to one loop-back listener.
///
/// `addr` is the address the listener actually bound, so a test can use
/// `start_callback_on("127.0.0.1:0")` and connect to the kernel-assigned port
/// instead of fighting over the fixed 8765.
pub struct CallbackHandle {
    pub addr: SocketAddr,
    state: Arc<CallbackState>,
}

impl CallbackHandle {
    /// The query string the accepted request carried, i.e. everything after the
    /// first `?` in the request-target (lib.rs:967-970).
    ///
    /// Returns `""` when the request carried no `?`.  Blocks until the single
    /// connection has been served, matching the original's indefinite
    /// `incoming().flatten().next()` (lib.rs:964); see [`CALLBACK_WAIT`].
    ///
    /// Use [`CallbackHandle::query_timeout`] when you must not block forever.
    pub fn query(&self) -> String {
        match CALLBACK_WAIT {
            None => self.query_forever(),
            Some(d) => self.query_timeout(d),
        }
    }

    /// [`CallbackHandle::query`] with a caller-chosen upper bound.  Returns `""`
    /// if no connection was served in time.
    pub fn query_timeout(&self, wait: Duration) -> String {
        let mut g = self.state.lock();
        let deadline = Instant::now() + wait;
        while !g.served {
            let now = Instant::now();
            if now >= deadline {
                break;
            }
            let waited = self
                .state
                .cv
                .wait_timeout(g, deadline - now)
                .unwrap_or_else(|p| p.into_inner());
            g = waited.0;
            if waited.1.timed_out() {
                break;
            }
        }
        g.query.clone()
    }

    /// Wait with no deadline at all.
    fn query_forever(&self) -> String {
        let mut g = self.state.lock();
        while !g.served {
            g = self
                .state
                .cv
                .wait(g)
                .unwrap_or_else(|p| p.into_inner());
        }
        g.query.clone()
    }

    /// Non-blocking read, matching `oauth_poll_callback` (lib.rs:980).
    pub fn poll(&self) -> String {
        self.state.lock().query.clone()
    }
}

/// The listener body shared by `start_callback_on` and `start_callback`.
///
/// Mirrors lib.rs:960-975 exactly: accept the FIRST connection, read up to 4096
/// bytes once, take token #1 of the request as the target, take everything after
/// the first `?` as the query, store it, then answer 200 text/plain with the
/// fixed body and `Connection: close`.
fn serve_once(listener: TcpListener, state: Arc<CallbackState>) {
    if let Some(mut stream) = listener.incoming().flatten().next() {
        let mut buf = [0u8; READ_BUF];
        let n = stream.read(&mut buf).unwrap_or(0);
        let req = String::from_utf8_lossy(&buf[..n]);
        let target = req.split_whitespace().nth(1).unwrap_or("/");
        let q = target.split('?').nth(1).map(|s| s.to_string());
        {
            let mut g = state.lock();
            if let Some(q) = q {
                g.query = q;
            }
            g.served = true;
        }
        state.cv.notify_all();
        let body = CALLBACK_BODY;
        let resp = format!(
            "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}",
            body.len(),
            body
        );
        let _ = stream.write_all(resp.as_bytes());
    } else {
        // No connection will ever arrive (listener closed).  Release any waiter.
        let mut g = state.lock();
        g.served = true;
        drop(g);
        state.cv.notify_all();
    }
}

/// `oauth_start_callback`'s listener, on a caller-chosen address.
///
/// Pass `"127.0.0.1:0"` to get a kernel-assigned port; read it back from
/// [`CallbackHandle::addr`].
pub fn start_callback_on(addr: &str) -> std::io::Result<CallbackHandle> {
    let listener = TcpListener::bind(addr)?;
    let local = listener.local_addr()?;
    let state = Arc::new(CallbackState::new());
    let child = Arc::clone(&state);
    thread::spawn(move || serve_once(listener, child));
    Ok(CallbackHandle { addr: local, state })
}

static OAUTH_RESULT: OnceLock<Arc<Mutex<String>>> = OnceLock::new();

/// The process-wide slot the original kept in `OAUTH_RESULT` (lib.rs:8).
fn oauth_result() -> Arc<Mutex<String>> {
    OAUTH_RESULT
        .get_or_init(|| Arc::new(Mutex::new(String::new())))
        .clone()
}

/// `oauth_start_callback` (lib.rs:957-977).
///
/// Faithful in an important, easily-missed way: it returns `true` even when the
/// bind later fails inside the spawned thread.  The original could not observe
/// the bind result either — `let Ok(listener) = ... else { return; }` ran on the
/// spawned thread and `true` was returned regardless.  Use
/// [`start_callback_on`] when you need the bind result.
pub fn start_callback() -> bool {
    let result = oauth_result();
    if result.lock().map(|mut s| s.clear()).is_err() {
        return false;
    }
    thread::spawn(move || {
        let Ok(handle) = start_callback_on(DEFAULT_CALLBACK_ADDR) else {
            return;
        };
        let q = handle.query();
        if let Ok(mut out) = result.lock() {
            *out = q;
        }
    });
    true
}

/// `oauth_poll_callback` (lib.rs:980).
pub fn poll_callback() -> String {
    oauth_result().lock().map(|s| s.clone()).unwrap_or_default()
}

// ---------------------------------------------------------------------------
// status
// ---------------------------------------------------------------------------

/// `serde_json`'s string escaping, which the original got for free from
/// `serde_json::json!`.
fn json_string(s: &str) -> String {
    let mut out = String::with_capacity(s.len() + 2);
    out.push('"');
    for c in s.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\u{08}' => out.push_str("\\b"),
            '\u{0c}' => out.push_str("\\f"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if (c as u32) < 0x20 => {
                out.push_str(&format!("\\u{:04x}", c as u32));
            }
            c => out.push(c),
        }
    }
    out.push('"');
    out
}

/// `oauth_status`'s JSON shape (lib.rs:919-926), without serde.
///
/// `linked` is the ALREADY-RESOLVED record — `Some((user_id, display_name,
/// avatar))` when `linked_accounts.json` held a matching provider — because a
/// 3-tuple cannot also carry the provider it matched on.  See
/// [`status_from_stored`] for the case-insensitive provider comparison that the
/// original did inline.
///
/// Keys are emitted in serde_json's default order: `serde_json::Map` is a
/// `BTreeMap` unless the `preserve_order` feature is on, so the original's
/// objects came out alphabetically sorted regardless of the order written in
/// the `json!` macro.  That ordering is reproduced here.
pub fn status(provider: &str, linked: Option<(String, String, String)>) -> String {
    match linked {
        None => format!(
            "{{\"linked\":false,\"provider\":{}}}",
            json_string(provider)
        ),
        Some((user_id, display_name, avatar)) => format!(
            "{{\"avatar\":{},\"display_name\":{},\"linked\":true,\"provider\":{},\"user_id\":{}}}",
            json_string(&avatar),
            json_string(&display_name),
            json_string(provider),
            json_string(&user_id)
        ),
    }
}

/// The full `oauth_status` decision, given the raw stored record
/// `(provider, user_id, display_name, avatar)`.
///
/// This is where the original's `v["provider"].as_str().unwrap_or("")
/// .eq_ignore_ascii_case(&provider)` (lib.rs:923) lives; a non-matching or
/// missing record degrades to the `linked:false` shape.
pub fn status_from_stored(
    provider: &str,
    stored: Option<(String, String, String, String)>,
) -> String {
    match stored {
        Some((stored_provider, user_id, display_name, avatar))
            if stored_provider.eq_ignore_ascii_case(provider) =>
        {
            status(provider, Some((user_id, display_name, avatar)))
        }
        _ => status(provider, None),
    }
}

// ---------------------------------------------------------------------------
// tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;
    use std::net::TcpStream;
    use std::sync::Mutex as StdMutex;

    const CID: &str = "Iv1.abc123";
    const RED: &str = "http://127.0.0.1:8765/callback";
    const STATE: &str = "6f1a2b3c-4d5e-6f70-8192-a3b4c5d6e7f8";

    // -- (a) the four authorize byte strings -------------------------------

    #[test]
    fn authorize_github_is_byte_identical() {
        // Deliberately a hard-coded literal, NOT a re-run of the same `format!`:
        // an expectation recomputed from the implementation agrees with any
        // change to it, including a wrong one.
        assert_eq!(
            oauth_authorize("github", CID, RED, STATE),
            "https://github.com/login/oauth/authorize?client_id=Iv1.abc123&redirect_uri=http://127.0.0.1:8765/callback&scope=read:user%20user:email&state=6f1a2b3c-4d5e-6f70-8192-a3b4c5d6e7f8"
        );
    }

    #[test]
    fn authorize_bilibili_is_byte_identical() {
        assert_eq!(
            oauth_authorize("bilibili", CID, RED, STATE),
            "https://passport.bilibili.com/oauth2/authorize?client_id=Iv1.abc123&response_type=code&redirect_uri=http://127.0.0.1:8765/callback&state=6f1a2b3c-4d5e-6f70-8192-a3b4c5d6e7f8"
        );
    }

    #[test]
    fn authorize_empty_inputs_and_unknown_provider_give_empty() {
        assert_eq!(oauth_authorize("github", "", RED, STATE), "");
        assert_eq!(oauth_authorize("github", CID, "", STATE), "");
        // the emptiness test trims: whitespace-only counts as empty ...
        assert_eq!(oauth_authorize("github", "   ", RED, STATE), "");
        assert_eq!(oauth_authorize("bilibili", CID, "\t\n ", STATE), "");
        // ... but a non-empty value is interpolated untrimmed.
        assert!(oauth_authorize("github", "  CID  ", RED, STATE).contains("client_id=  CID  &"));
        assert_eq!(oauth_authorize("gitlab", CID, RED, STATE), "");
        assert_eq!(oauth_authorize("", CID, RED, STATE), "");
    }

    #[test]
    fn authorize_provider_match_ignores_ascii_case() {
        let want = oauth_authorize("github", CID, RED, STATE);
        assert_eq!(oauth_authorize("GitHub", CID, RED, STATE), want);
        assert_eq!(oauth_authorize("GITHUB", CID, RED, STATE), want);
        let want_b = oauth_authorize("bilibili", CID, RED, STATE);
        assert_eq!(oauth_authorize("BiliBili", CID, RED, STATE), want_b);
    }

    // -- (b) oauth_open's guard and the injected opener ---------------------

    #[test]
    fn open_refuses_non_allowlisted_urls() {
        let calls = Arc::new(StdMutex::new(Vec::<String>::new()));
        let c = Arc::clone(&calls);
        let opener = move |u: &str| -> std::io::Result<()> {
            c.lock().unwrap().push(u.to_string());
            Ok(())
        };

        for bad in [
            "http://github.com/login/oauth/authorize",     // http, not https
            "https://github.com.evil.example/login",       // suffix trick
            "https://evil.example/https://github.com/x",   // prefix inside
            "https://passport.bilibili.com/oauth2/authorize",  // no trailing path, still allowed? (checked below)
        ] {
            let r = oauth_open_with(bad, &opener);
            if bad.starts_with("https://passport.bilibili.com/oauth2/authorize") {
                assert!(r.is_ok(), "the bare authorize prefix IS allowed: {bad}");
            } else {
                assert_eq!(r, Err("Unsupported authorization URL".to_string()), "{bad}");
            }
        }
        assert_eq!(
            calls.lock().unwrap().len(),
            1,
            "only the allow-listed URL reached the opener"
        );
        assert_eq!(
            calls.lock().unwrap()[0],
            "https://passport.bilibili.com/oauth2/authorize"
        );
    }

    #[test]
    fn open_hands_the_exact_bytes_to_the_opener() {
        let url = oauth_authorize("github", CID, RED, STATE);
        let calls = Arc::new(StdMutex::new(Vec::<String>::new()));
        let c = Arc::clone(&calls);
        let opener = move |u: &str| -> std::io::Result<()> {
            c.lock().unwrap().push(u.to_string());
            Ok(())
        };

        assert_eq!(oauth_open_with(&url, &opener), Ok(()));
        let seen = calls.lock().unwrap().clone();
        assert_eq!(seen.len(), 1);
        assert_eq!(seen[0], url);
        assert_eq!(seen[0].as_bytes(), url.as_bytes());
    }

    #[test]
    fn open_propagates_the_opener_error_text() {
        let url = oauth_authorize("bilibili", CID, RED, STATE);
        let opener = |_: &str| -> std::io::Result<()> {
            Err(std::io::Error::new(std::io::ErrorKind::NotFound, "no xdg-open"))
        };
        assert_eq!(
            oauth_open_with(&url, &opener),
            Err("no xdg-open".to_string())
        );
    }

    // -- (c) the live loop, over a real socket -----------------------------

    #[test]
    fn callback_live_loop_over_real_socket() {
        let handle = start_callback_on("127.0.0.1:0").expect("bind kernel-assigned port");
        assert_ne!(handle.addr.port(), 0, "kernel assigned a real port");

        // The test plays the browser.
        let mut client = TcpStream::connect(handle.addr).expect("connect to listener");
        client
            .write_all(b"GET /callback?code=TESTCODE&state=TESTSTATE HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n")
            .expect("write request");
        client.flush().expect("flush");

        let mut resp = Vec::new();
        client.read_to_end(&mut resp).expect("read response");
        let resp = String::from_utf8_lossy(&resp);

        assert!(
            resp.starts_with("HTTP/1.1 200 OK\r\n"),
            "status line was {:?}",
            resp.lines().next()
        );
        assert!(resp.contains("Content-Type: text/plain; charset=utf-8\r\n"));
        assert!(resp.contains(&format!("Content-Length: {}\r\n", CALLBACK_BODY.len())));
        assert!(resp.contains("Connection: close\r\n"));
        assert!(
            resp.ends_with(CALLBACK_BODY),
            "body tail was {:?}",
            &resp[resp.len().saturating_sub(80)..]
        );

        assert_eq!(handle.query(), "code=TESTCODE&state=TESTSTATE");
    }

    /// The case the old fixed 5-second cap silently dropped: a real human
    /// typing a password or completing 2FA takes longer than the
    /// `CALLBACK_TEST_WAIT` window, and their callback must still be delivered.
    ///
    /// With the cap restored (`CALLBACK_WAIT = Some(CALLBACK_TEST_WAIT)`) this
    /// fails with `left: ""`.  Deliberately kept just over the window so the
    /// test stays fast; the mechanism, not the exact latency, is what is pinned.
    #[test]
    fn callback_arriving_after_the_old_cap_is_still_delivered() {
        let handle = start_callback_on("127.0.0.1:0").expect("bind");
        let addr = handle.addr;

        let late = thread::spawn(move || {
            thread::sleep(CALLBACK_TEST_WAIT + Duration::from_millis(500));
            let mut client = TcpStream::connect(addr).expect("late connect");
            client
                .write_all(b"GET /callback?code=LATECODE&state=LATESTATE HTTP/1.1\r\n\r\n")
                .expect("late write");
            let mut resp = Vec::new();
            let _ = client.read_to_end(&mut resp);
            resp
        });

        // `query()` must outwait the late client rather than give up on it.
        let got = handle.query();
        let resp = late.join().expect("late thread");

        assert_eq!(
            got, "code=LATECODE&state=LATESTATE",
            "a callback arriving after {}s was silently discarded",
            CALLBACK_TEST_WAIT.as_secs()
        );
        assert!(
            String::from_utf8_lossy(&resp).starts_with("HTTP/1.1 200 OK\r\n"),
            "late client was not answered 200 OK"
        );
    }

    /// `query_timeout` still bounds the wait, so the escape hatch stays usable.
    #[test]
    fn query_timeout_gives_up_and_returns_empty() {
        let handle = start_callback_on("127.0.0.1:0").expect("bind");
        let start = Instant::now();
        assert_eq!(handle.query_timeout(Duration::from_millis(300)), "");
        let waited = start.elapsed();
        assert!(
            waited >= Duration::from_millis(250),
            "returned before the timeout: {:?}",
            waited
        );
        assert!(waited < CALLBACK_TEST_WAIT, "did not honour the bound: {:?}", waited);
    }

    #[test]
    fn callback_without_query_yields_empty_string() {
        let handle = start_callback_on("127.0.0.1:0").expect("bind");
        let mut client = TcpStream::connect(handle.addr).expect("connect");
        client
            .write_all(b"GET /callback HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n")
            .expect("write");
        let mut resp = Vec::new();
        let _ = client.read_to_end(&mut resp);
        assert_eq!(handle.query(), "");
        // the response is still sent, exactly as the original did
        assert!(String::from_utf8_lossy(&resp).starts_with("HTTP/1.1 200 OK"));
    }

    #[test]
    fn callback_only_serves_one_connection() {
        let handle = start_callback_on("127.0.0.1:0").expect("bind");
        let mut first = TcpStream::connect(handle.addr).expect("connect 1");
        first
            .write_all(b"GET /cb?code=ONE HTTP/1.1\r\n\r\n")
            .expect("write 1");
        let mut r1 = Vec::new();
        let _ = first.read_to_end(&mut r1);
        assert_eq!(handle.query(), "code=ONE");

        // The listener thread returned after the first connection; a second
        // connect either gets refused or is never served.
        let second = TcpStream::connect(handle.addr);
        if let Ok(mut s2) = second {
            let _ = s2.write_all(b"GET /cb?code=TWO HTTP/1.1\r\n\r\n");
            let mut r2 = Vec::new();
            let _ = s2.read_to_end(&mut r2);
        }
        assert_eq!(handle.query(), "code=ONE", "the first answer stands");
    }

    #[test]
    fn start_callback_on_rejects_a_bad_address() {
        assert!(start_callback_on("not-an-address").is_err());
        assert!(start_callback_on("127.0.0.1:99999").is_err());
    }

    #[test]
    fn global_start_callback_returns_true_like_the_original() {
        // Faithful quirk: `true` even though the bind happens on the spawned
        // thread and its failure is unobservable to the caller.
        assert!(start_callback());
    }

    // -- status ------------------------------------------------------------

    #[test]
    fn status_unlinked_shape_is_serde_json_key_order() {
        assert_eq!(
            status("github", None),
            "{\"linked\":false,\"provider\":\"github\"}"
        );
    }

    #[test]
    fn status_linked_shape_is_serde_json_key_order() {
        assert_eq!(
            status(
                "github",
                Some(("42".into(), "Ada".into(), "https://avatars/x.png".into()))
            ),
            "{\"avatar\":\"https://avatars/x.png\",\"display_name\":\"Ada\",\"linked\":true,\"provider\":\"github\",\"user_id\":\"42\"}"
        );
    }

    #[test]
    fn status_escapes_like_serde_json() {
        assert_eq!(
            status("git\"hub\n", None),
            "{\"linked\":false,\"provider\":\"git\\\"hub\\n\"}"
        );
        // non-ASCII is passed through as UTF-8, not \u-escaped
        assert_eq!(
            status("bilibili\u{4e2d}", None),
            "{\"linked\":false,\"provider\":\"bilibili\u{4e2d}\"}"
        );
        assert_eq!(json_string("\u{1}"), "\"\\u0001\"");
    }

    #[test]
    fn status_from_stored_matches_provider_ignoring_ascii_case() {
        let rec = Some((
            "GitHub".to_string(),
            "42".to_string(),
            "Ada".to_string(),
            "https://avatars/x.png".to_string(),
        ));
        assert!(status_from_stored("github", rec.clone()).contains("\"linked\":true"));
        assert!(status_from_stored("BILIBILI", rec.clone()).contains("\"linked\":false"));
        assert!(status_from_stored("github", None).contains("\"linked\":false"));
        // a stored record for another provider never leaks its fields
        assert!(!status_from_stored("bilibili", rec)
            .contains("Ada"));
    }

    // -----------------------------------------------------------------------
    // PKCE: published vectors, not self-consistency
    // -----------------------------------------------------------------------
    //
    // A hand-rolled hash is only as good as the vectors it is checked against.
    // Every expected value below is from the relevant standard, NOT recomputed
    // by this crate — a test that derives its expectation from the same code it
    // is testing proves nothing.

    #[test]
    fn sha256_known_answer_vectors() {
        // FIPS 180-4 / NIST examples.
        assert_eq!(
            s256_hex(""),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
        );
        assert_eq!(
            s256_hex("abc"),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
        );
        assert_eq!(
            s256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"
        );
        // The 1,000,000-'a' vector exercises multi-block padding.
        assert_eq!(
            s256_hex(&"a".repeat(1_000_000)),
            "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"
        );
        // Exactly one block boundary case: 55 and 56 bytes.
        assert_eq!(
            s256_hex(&"a".repeat(55)),
            "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"
        );
        assert_eq!(
            s256_hex(&"a".repeat(56)),
            "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"
        );
    }

    #[test]
    fn pkce_rfc7636_appendix_b_vector() {
        // RFC 7636 Appendix B, verbatim. This is the vector that decides whether
        // our S256 challenge is the one an authorization server will accept.
        let verifier = "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk";
        assert_eq!(
            s256_challenge(verifier),
            "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"
        );
    }

    // -----------------------------------------------------------------------
    // ③ state verification
    // -----------------------------------------------------------------------

    #[test]
    fn verify_callback_accepts_a_matching_state() {
        assert_eq!(
            verify_callback("STATE123", "code=abc&state=STATE123"),
            Ok("abc".to_string())
        );
    }

    #[test]
    fn verify_callback_rejects_a_state_that_does_not_match() {
        // This is the login-CSRF primitive the audit flagged: a callback that
        // this client did not start must not be redeemable.
        let err = verify_callback("STATE123", "code=abc&state=ATTACKER").unwrap_err();
        assert!(err.contains("state mismatch"), "got {err:?}");
    }

    #[test]
    fn verify_callback_rejects_a_missing_state() {
        let err = verify_callback("STATE123", "code=abc").unwrap_err();
        assert!(err.contains("no state"), "got {err:?}");
    }

    #[test]
    fn verify_callback_reports_a_provider_error_instead_of_binding() {
        // GitHub answers 200 with error=... for a bad code; treating that as a
        // success is how a dead token gets stored as a live account.
        let err = verify_callback("S", "error=access_denied&error_description=user+said+no").unwrap_err();
        assert!(err.contains("access_denied"), "got {err:?}");
        assert!(err.contains("user said no"), "description not decoded: {err:?}");
    }

    #[test]
    fn verify_callback_requires_a_code() {
        let err = verify_callback("S", "state=S").unwrap_err();
        assert!(err.contains("no code"), "got {err:?}");
    }

    #[test]
    fn verify_callback_decodes_percent_and_plus_escapes() {
        assert_eq!(
            verify_callback("a b", "code=x%2By&state=a+b"),
            Ok("x+y".to_string())
        );
    }

    // -----------------------------------------------------------------------
    // ① token exchange
    // -----------------------------------------------------------------------

    #[test]
    fn token_request_body_is_correctly_encoded() {
        let body = token_request_body("github", "C&D", "http://127.0.0.1:8765/cb?x=1", "V/1+2")
            .expect("github is supported");
        // A raw & in the code would otherwise inject an extra form field into a
        // credentialed request.
        assert_eq!(
            body,
            "grant_type=authorization_code&code=C%26D&redirect_uri=http%3A%2F%2F127.0.0.1%3A8765%2Fcb%3Fx%3D1&code_verifier=V%2F1%2B2"
        );
        assert!(!body.contains("code=C&D"), "code was not encoded: {body}");
    }

    #[test]
    fn token_request_body_refuses_an_unknown_provider() {
        assert!(token_request_body("gitlab", "c", "r", "v").is_none());
        assert!(token_endpoint("gitlab").is_none());
    }

    #[test]
    fn form_encode_keeps_unreserved_and_escapes_the_rest() {
        assert_eq!(form_encode("AZaz09-._~"), "AZaz09-._~");
        assert_eq!(form_encode("a b"), "a%20b");
        assert_eq!(form_encode("&=%+/"), "%26%3D%25%2B%2F");
    }

    #[test]
    fn parse_token_response_reads_json() {
        let t = parse_token_response(r#"{"access_token":"gho_x","token_type":"bearer","scope":"read:user"}"#)
            .expect("valid");
        assert_eq!(t.access_token, "gho_x");
        assert_eq!(t.token_type.as_deref(), Some("bearer"));
        assert_eq!(t.refresh_token, None);
    }

    #[test]
    fn parse_token_response_reads_form_encoded() {
        let t = parse_token_response("access_token=gho_y&token_type=bearer&scope=read%3Auser")
            .expect("valid");
        assert_eq!(t.access_token, "gho_y");
        assert_eq!(t.scope.as_deref(), Some("read:user"));
    }

    #[test]
    fn parse_token_response_treats_a_200_error_body_as_failure() {
        // The failure this prevents: an error body parsed optimistically and a
        // garbage token written to linked_accounts.json.
        let err = parse_token_response(r#"{"error":"bad_verification_code","error_description":"expired"}"#)
            .unwrap_err();
        assert!(err.contains("bad_verification_code"), "got {err:?}");
        assert!(parse_token_response("access_token=").is_err());
        assert!(parse_token_response("{}").is_err());
    }

    #[test]
    fn parse_token_response_accepts_a_nested_data_object() {
        // bilibili wraps its payload in `data`.
        let t = parse_token_response(r#"{"code":0,"data":{"access_token":"bi_x","refresh_token":"bi_r"}}"#)
            .expect("valid");
        assert_eq!(t.access_token, "bi_x");
        assert_eq!(t.refresh_token.as_deref(), Some("bi_r"));
    }

    #[test]
    fn parse_token_response_does_not_choke_on_a_brace_inside_a_value() {
        let t = parse_token_response(r#"{"access_token":"a}b","scope":"x{y"}"#).expect("valid");
        assert_eq!(t.access_token, "a}b");
        assert_eq!(t.scope.as_deref(), Some("x{y"));
    }

    #[test]
    fn parse_token_response_keeps_null_fields_absent() {
        let t = parse_token_response(r#"{"access_token":"a","refresh_token":null}"#).expect("valid");
        assert_eq!(t.access_token, "a");
        assert_eq!(t.refresh_token, None, "null became the literal \"null\"");
    }

    // -----------------------------------------------------------------------
    // PKCE verifier shape
    // -----------------------------------------------------------------------

    #[test]
    fn generated_verifiers_are_well_formed_and_unique() {
        let a = Pkce::generate();
        let b = Pkce::generate();
        assert_ne!(a.verifier, b.verifier, "two verifiers collided");
        // RFC 7636 §4.1: 43-128 chars from the unreserved set.
        assert!((43..=128).contains(&a.verifier.len()), "len {}", a.verifier.len());
        assert!(a
            .verifier
            .chars()
            .all(|c| c.is_ascii_alphanumeric() || "-._~".contains(c)));
        assert_eq!(a.method(), "S256");
        assert_eq!(s256_challenge(&a.verifier), a.challenge);
        // A challenge is the unpadded base64url of 32 bytes -> 43 chars.
        assert_eq!(a.challenge.len(), 43);
        assert!(!a.challenge.contains('='), "challenge was padded");
        assert!(!a.challenge.contains('+') && !a.challenge.contains('/'), "not url-safe");
    }

}
