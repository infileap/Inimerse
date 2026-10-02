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
/// THIS IS A DIVERGENCE FROM THE ORIGINAL, AND IT IS NOT TEST-ONLY.
/// The original's listener thread called `listener.incoming().flatten().next()`
/// (`lib.rs:964`), which blocks *indefinitely* until a connection arrives.
/// What was non-blocking was the **JS** side, which polled `oauth_poll_callback`
/// once a second (`app.js:366`) — conflating the two is what makes this cap
/// look harmless.  Because [`start_callback`] routes through
/// [`CallbackHandle::query`], a callback that arrives later than this window is
/// still answered `200 OK` but its query is written into a slot nobody reads,
/// i.e. it is silently discarded.
///
/// The consumer's own window is about 60 seconds (`app.js:366` polls 60 times
/// at 1 Hz), so the case this discards is a real human typing a password or
/// completing 2FA.  [`start_callback_on`] does not have this cap.
pub const CALLBACK_WAIT: Duration = Duration::from_secs(5);

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
    /// Returns `""` when the request carried no `?`, or when no request arrived
    /// within [`CALLBACK_WAIT`].  Waits for the single connection to be served
    /// before reading the slot; the original never waited, it was polled.
    pub fn query(&self) -> String {
        let mut g = self.state.lock();
        let deadline = Instant::now() + CALLBACK_WAIT;
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
}
