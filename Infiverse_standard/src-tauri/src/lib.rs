use serde::Serialize;
use std::fs;
use std::net::UdpSocket;
use std::path::PathBuf;
use std::sync::{Arc, Mutex};
use std::sync::atomic::{AtomicBool, Ordering};
static OAUTH_RESULT: std::sync::OnceLock<Arc<Mutex<String>>> = std::sync::OnceLock::new();
fn oauth_result() -> Arc<Mutex<String>> { OAUTH_RESULT.get_or_init(|| Arc::new(Mutex::new(String::new()))).clone() }
static WORKBENCH_PID: std::sync::OnceLock<Arc<Mutex<Option<u32>>>> = std::sync::OnceLock::new();
static WORKBENCH_STOPPED: AtomicBool = AtomicBool::new(false);
fn workbench_pid() -> Arc<Mutex<Option<u32>>> { WORKBENCH_PID.get_or_init(|| Arc::new(Mutex::new(None))).clone() }

fn app_root() -> PathBuf {
    let exe = std::env::current_exe().unwrap_or_default();
    if let Some(parent) = exe.parent() {
        if parent.join("plugins").is_dir() || parent.join("userdata").is_dir() || parent.join("src").join("ui").is_file() {
            return parent.to_path_buf();
        }
    }
    let source_root = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .parent().map(PathBuf::from).unwrap_or_default();
    if source_root.join("src").is_dir() && source_root.join("src-tauri").is_dir() {
        return source_root;
    }
    /* Debug executable is <root>/src-tauri/target/debug/app.exe. */
    exe.parent().and_then(|p| p.parent()).and_then(|p| p.parent()).and_then(|p| p.parent())
        .map(PathBuf::from).unwrap_or_else(|| std::env::current_dir().unwrap_or_default())
}
fn user_data(name: &str) -> PathBuf { app_root().join("userdata").join(name) }
fn engine_root() -> PathBuf {
    if let Ok(root) = std::env::var("INIMERSE_ROOT") { let p = PathBuf::from(root); if p.is_dir() { return p; } }
    app_root().parent().map(PathBuf::from).unwrap_or_else(app_root)
}

fn detect_engine() -> String {
    if let Ok(p) = std::env::var("INIMERSE_ENGINE") { if !p.trim().is_empty() && fs::metadata(p.trim()).is_ok() { return p.trim().to_string(); } }
    if let Ok(sel) = fs::read_to_string(user_data("default_engine.txt")) {
        let p = sel.trim(); if !p.is_empty() && fs::metadata(p).is_ok() { return p.to_string(); }
    }
    let local_names = if cfg!(windows) { ["inimerse.exe", "inimerse"] } else { ["inimerse", "inimerse.exe"] };
    for name in local_names { let local = app_root().join(name); if local.exists() { return local.to_string_lossy().into_owned(); } }
    if let Ok(c) = std::env::var("INIMERSE_ENGINE") { if fs::metadata(&c).is_ok() { return c; } }
    "inimerse.exe".to_string()
}

fn engine_candidates() -> Vec<PathBuf> {
    let mut out = Vec::new();
    for root in [app_root()] {
        for name in ["inimerse", "inimerse.exe"] { let direct = root.join(name); if direct.exists() { out.push(direct); } }
        for sub in ["engines", "versions"] {
            let dir = root.join(sub);
            if let Ok(rd) = fs::read_dir(dir) { for e in rd.flatten() { for name in ["inimerse", "inimerse.exe"] { let p=e.path().join(name); if p.exists() { out.push(p); } } } }
        }
    }
    out.sort(); out.dedup(); out
}

#[tauri::command]
fn engine_versions() -> serde_json::Value {
    let selected = fs::read_to_string(user_data("default_engine.txt")).unwrap_or_default();
    serde_json::Value::Array(engine_candidates().into_iter().map(|p| {
        let path=p.to_string_lossy().into_owned(); let size=fs::metadata(&p).map(|m|m.len()).unwrap_or(0);
        serde_json::json!({ "path": path, "size": size, "selected": selected.trim() == path })
    }).collect())
}

#[tauri::command]
fn engine_select(path: String) -> serde_json::Value {
    let allowed = engine_candidates().iter().any(|p| p.to_string_lossy() == path);
    if !allowed { return serde_json::json!({ "ok": false, "error": "Engine was not discovered" }); }
    let p=user_data("default_engine.txt"); if let Some(d)=p.parent(){let _=fs::create_dir_all(d);} 
    match fs::write(p, path) { Ok(()) => serde_json::json!({ "ok": true }), Err(e) => serde_json::json!({ "ok": false, "error": e.to_string() }) }
}

#[tauri::command]
fn update_channel_get() -> String { fs::read_to_string(user_data("update_channel.txt")).unwrap_or_else(|_| "stable".into()).trim().to_lowercase() }

#[tauri::command]
fn update_channel_set(channel: String) -> serde_json::Value {
    if !["stable", "preview", "source"].contains(&channel.as_str()) { return serde_json::json!({ "ok": false, "error": "Unknown channel" }); }
    let p=user_data("update_channel.txt"); if let Some(d)=p.parent(){let _=fs::create_dir_all(d);} 
    match fs::write(p, &channel) { Ok(()) => serde_json::json!({ "ok": true, "channel": channel }), Err(e) => serde_json::json!({ "ok": false, "error": e.to_string() }) }
}

fn component_defs() -> Vec<(&'static str, &'static str, u64, bool)> {
    vec![("engine", "引擎核心", 600_000, true), ("standard", "标准扩展（GUI/联机）", 1_800_000, true), ("ai", "AI 编程支持", 64_000, false), ("examples", "示例项目", 120_000, false)]
}
#[tauri::command]
fn components_list() -> serde_json::Value {
    let saved: serde_json::Value = fs::read_to_string(user_data("components.json")).ok().and_then(|s| serde_json::from_str(&s).ok()).unwrap_or_else(|| serde_json::json!({}));
    serde_json::Value::Array(component_defs().into_iter().map(|(id,name,size,required)| {
        let installed = required || saved.get(id).and_then(|v| v.as_bool()).unwrap_or(false);
        serde_json::json!({ "id": id, "name": name, "size": size, "required": required, "installed": installed })
    }).collect())
}
#[tauri::command]
fn component_set(id: String, installed: bool) -> serde_json::Value {
    let Some((_,_,_,required)) = component_defs().into_iter().find(|(i,_,_,_)| *i == id) else { return serde_json::json!({ "ok": false, "error": "Unknown component" }); };
    if required && !installed { return serde_json::json!({ "ok": false, "error": "Required component" }); }
    let mut saved: serde_json::Map<String,serde_json::Value> = fs::read_to_string(user_data("components.json")).ok().and_then(|s| serde_json::from_str(&s).ok()).unwrap_or_default();
    saved.insert(id, serde_json::json!(installed)); let p=user_data("components.json"); if let Some(d)=p.parent(){let _=fs::create_dir_all(d);}
    match fs::write(p, serde_json::to_string_pretty(&saved).unwrap_or_default()) { Ok(()) => serde_json::json!({ "ok": true }), Err(e) => serde_json::json!({ "ok": false, "error": e.to_string() }) }
}

#[tauri::command]
fn package_list() -> serde_json::Value { plugin_list() }

#[tauri::command]
fn verse_local_packages() -> serde_json::Value {
    let root = app_root().join("projects");
    let mut out = Vec::new();
    fn walk(dir: &std::path::Path, out: &mut Vec<serde_json::Value>) {
        let Ok(rd) = fs::read_dir(dir) else { return; };
        for e in rd.flatten() { let p = e.path(); if p.is_dir() { walk(&p, out); } else if p.extension().and_then(|x| x.to_str()) == Some("vverse") { if let Ok(m) = fs::metadata(&p) { out.push(serde_json::json!({"id": p.file_stem().and_then(|x| x.to_str()).unwrap_or(""), "path": p, "size": m.len()})); } } }
    }
    walk(&root, &mut out);
    out.sort_by(|a, b| a["id"].as_str().cmp(&b["id"].as_str()));
    serde_json::Value::Array(out)
}

#[tauri::command]
fn verse_package_preview(file: String) -> serde_json::Value {
    let Ok(root) = app_root().canonicalize() else { return serde_json::json!({"ok": false, "error": "workspace unavailable"}); };
    let Ok(path) = std::path::Path::new(&file).canonicalize() else { return serde_json::json!({"ok": false, "error": "package not found"}); };
    if !path.starts_with(&root) || path.extension().and_then(|x| x.to_str()) != Some("vverse") { return serde_json::json!({"ok": false, "error": "package outside workspace"}); }
    match fs::metadata(&path) { Ok(m) => serde_json::json!({"ok": true, "path": path, "size": m.len(), "readOnly": true}), Err(e) => serde_json::json!({"ok": false, "error": e.to_string()}) }
}

#[tauri::command]
fn package_remove(name: String) -> serde_json::Value {
    if name.contains('/') || name.contains('\\') || name.contains("..") { return serde_json::json!({ "ok": false, "error": "Invalid package name" }); }
    let p=app_root().join("plugins").join(&name);
    if p.extension().and_then(|s|s.to_str()) != Some("im") { return serde_json::json!({ "ok": false, "error": "Only .im packages can be removed" }); }
    match fs::remove_file(p) { Ok(()) => serde_json::json!({ "ok": true }), Err(e) => serde_json::json!({ "ok": false, "error": e.to_string() }) }
}

#[tauri::command]
fn repair_scan() -> serde_json::Value {
    let engine = detect_engine();
    WORKBENCH_STOPPED.store(false, Ordering::SeqCst);
    let checks = vec![
        ("engine", "当前引擎", fs::metadata(&engine).map(|m| m.len()).unwrap_or(0) > 100_000),
        ("userdata", "用户数据目录", app_root().join("userdata").is_dir()),
        ("plugins", "插件目录", app_root().join("plugins").is_dir()),
        ("ui", "UI 资源", app_root().join("src/ui/index.html").is_file()),
    ];
    let missing: Vec<serde_json::Value> = checks.iter().filter(|(_,_,ok)| !*ok).map(|(id,name,_)| serde_json::json!({ "id": id, "name": name })).collect();
    serde_json::json!({ "ok": missing.is_empty(), "engine": engine, "checks": checks.into_iter().map(|(id,name,ok)| serde_json::json!({"id":id,"name":name,"ok":ok})).collect::<Vec<_>>(), "missing": missing })
}

#[tauri::command]
fn tool_files() -> serde_json::Value {
    let mut arr: Vec<String> = Vec::new();
    let dirs = [app_root(), app_root().join("projects")];
    for d in dirs {
        if let Ok(rd) = std::fs::read_dir(&d) {
            for e in rd.flatten() {
                let p = e.path();
                if p.extension().map(|x| x == "im").unwrap_or(false) {
                    arr.push(p.to_string_lossy().to_string());
                }
            }
        }
    }
    serde_json::Value::Array(arr.iter().map(|s| serde_json::json!(s)).collect())
}

#[tauri::command]
fn tool_lint(path: String) -> String {
    let engine = detect_engine();
    if let Ok(o) = std::process::Command::new(&engine).args(["--lint", &path]).output() {
        let mut s = String::from_utf8_lossy(&o.stdout).to_string();
        s.push_str(&String::from_utf8_lossy(&o.stderr));
        return s;
    }
    "无法运行引擎".to_string()
}

#[tauri::command]
fn tool_stats(path: String) -> serde_json::Value {
    if let Ok(src) = std::fs::read_to_string(&path) {
        let lines = src.lines().count();
        let code = src.lines().filter(|l| {
            let t = l.trim();
            !t.is_empty() && !t.starts_with('#') && !t.starts_with("//")
        }).count();
        let fns = src.matches("func ").count() + src.matches("task ").count() + src.matches("thread ").count();
        return serde_json::json!({ "lines": lines, "code": code, "comments": lines.saturating_sub(code), "fns": fns });
    }
    serde_json::json!({ "lines": 0, "code": 0, "comments": 0, "fns": 0 })
}

#[tauri::command]
fn tool_desugar(path: String) -> String {
    let out = user_data("desugar_out.im");
    let engine = detect_engine();
    let out_s = out.to_string_lossy().into_owned();
    if let Ok(o) = std::process::Command::new(&engine).args(["--desugar", &path, &out_s]).output() {
        if o.status.success() {
            return std::fs::read_to_string(out).unwrap_or_default();
        }
        return format!("失败：{}", String::from_utf8_lossy(&o.stderr));
    }
    "无法运行引擎".to_string()
}

#[tauri::command]
fn backup_list() -> serde_json::Value {
    let mut arr: Vec<String> = Vec::new();
    if let Ok(rd) = std::fs::read_dir(user_data("backups")) {
        for e in rd.flatten() {
            let n = e.file_name().to_string_lossy().to_string();
            if n.starts_with("inimerse_") { arr.push(n); }
        }
    }
    arr.sort();
    arr.reverse();
    serde_json::Value::Array(arr.iter().map(|s| serde_json::json!(s)).collect())
}

#[tauri::command]
fn plugin_list() -> serde_json::Value {
    let mut arr = Vec::new();
    let dirs = [app_root().join("plugins"), app_root().join("src"), app_root().join("projects"), app_root().join("mods")];
    for d in dirs {
        if let Ok(rd) = std::fs::read_dir(&d) {
            for e in rd.flatten() {
                let p = e.path();
                if p.extension().map(|x| x == "im").unwrap_or(false) {
                    arr.push(serde_json::json!({ "path": p.to_string_lossy().to_string(), "name": p.file_name().unwrap_or_default().to_string_lossy().to_string() }));
                }
            }
        }
    }
    serde_json::Value::Array(arr)
}

#[tauri::command]
fn verse_save_node(friend_id: String, node: String) -> bool {
    if let Ok(s) = std::fs::read_to_string(user_data("friends.json")) {
        if let Ok(mut v) = serde_json::from_str::<serde_json::Value>(&s) {
            if let Some(fs) = v["friends"].as_array_mut() {
                for f in fs {
                    if f["id"].as_str() == Some(&friend_id) {
                        f["node"] = serde_json::Value::String(node.clone());
                        return std::fs::write(user_data("friends.json"), serde_json::to_string_pretty(&v).unwrap_or_default()).is_ok();
                    }
                }
            }
        }
    }
    false
}

#[derive(Serialize, Default)]
struct Identity {
    id: String,
    name: String,
    avatar: String,
    bio: String,
    created: u64,
    verse: String,
}

#[tauri::command]
fn get_identity() -> Identity {
    let mut id = Identity {
        id: "u_0000000000000000".into(),
        name: "Infiverse 用户".into(),
        ..Default::default()
    };
    let local = user_data("profile.json");
    let candidates = [local, user_data("profile.json")];
    for p in candidates {
        if let Ok(s) = fs::read_to_string(&p) {
            if let Ok(v) = serde_json::from_str::<serde_json::Value>(&s) {
                id.id = v["id"].as_str().unwrap_or(&id.id).to_string();
                id.name = v["name"].as_str().unwrap_or(&id.name).to_string();
                id.avatar = v["avatar"].as_str().unwrap_or("").to_string();
                id.bio = v["bio"].as_str().unwrap_or("").to_string();
                id.created = v["created"].as_u64().unwrap_or(0);
            }
            break;
        }
    }
    id.verse = format!("verse://hub/{}", id.id);
    id
}

#[tauri::command]
fn update_identity(name: String, avatar: String, bio: String) -> serde_json::Value {
    let clean = |s: String| s.chars().take(160).collect::<String>();
    let old = get_identity();
    let value = serde_json::json!({"id":old.id,"name":clean(name),"avatar":clean(avatar),"bio":clean(bio),"created":old.created});
    let path = user_data("profile.json");
    if let Some(parent) = path.parent() { let _ = fs::create_dir_all(parent); }
    match fs::write(path, serde_json::to_string_pretty(&value).unwrap_or_default()) {
        Ok(()) => serde_json::json!({"ok":true}),
        Err(e) => serde_json::json!({"ok":false,"error":e.to_string()}),
    }
}

#[tauri::command]
fn get_local_ip() -> Vec<String> {
    let mut ips = Vec::new();
    if let Ok(sock) = UdpSocket::bind("0.0.0.0:0") {
        if sock.connect("8.8.8.8:80").is_ok() {
            if let Ok(addr) = sock.local_addr() {
                let ip = addr.ip().to_string();
                if ip != "0.0.0.0" {
                    ips.push(ip);
                }
            }
        }
    }
    ips
}

#[tauri::command]
fn get_engine_info() -> serde_json::Value {
    let local = app_root().join("inimerse.exe");
    let mut path = if local.exists() { local.to_string_lossy().into_owned() } else { String::new() };
    if path.is_empty() { path = detect_engine(); }
    let size = if !path.is_empty() {
        fs::metadata(&path).map(|m| m.len()).unwrap_or(0)
    } else {
        0
    };
    serde_json::json!({ "path": path, "size": size })
}

#[tauri::command]
fn get_qr_svg(text: String) -> String {
    match qrcode::QrCode::new(text.as_bytes()) {
        Ok(code) => code
            .render::<qrcode::render::svg::Color>()
            .min_dimensions(180, 180)
            .build(),
        Err(_) => String::new(),
    }
}

fn stats_path() -> std::path::PathBuf {
    user_data("stats.json")
}

fn chrono_now() -> String {
    use std::time::{SystemTime, UNIX_EPOCH};
    let secs = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs();
    let days = (secs / 86400) as i64;
    let h = (secs % 86400) / 3600;
    let m = (secs % 3600) / 60;
    let s = secs % 60;
    let (y, mo, d) = civil_from_days(days);
    format!("{}-{:02}-{:02} {:02}:{:02}:{:02}", y, mo, d, h, m, s)
}

fn civil_from_days(z: i64) -> (i64, i64, i64) {
    let z = z + 719468;
    let era = if z >= 0 { z } else { z - 146096 } / 146097;
    let doe = (z - era * 146097) as u64;
    let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    let y = yoe as i64 + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = (doy - (153 * mp + 2) / 5 + 1) as i64;
    let m = if mp < 10 { mp + 3 } else { mp - 9 } as i64;
    (if m <= 2 { y + 1 } else { y }, m, d)
}

#[tauri::command]
fn record_run() -> serde_json::Value {
    let p = stats_path();
    if let Some(dir) = p.parent() {
        let _ = fs::create_dir_all(dir);
    }
    let mut runs = 0u64;
    let mut first = String::new();
    if let Ok(s) = fs::read_to_string(&p) {
        if let Ok(v) = serde_json::from_str::<serde_json::Value>(&s) {
            runs = v["runs"].as_u64().unwrap_or(0);
            first = v["first"].as_str().unwrap_or("").to_string();
        }
    }
    runs += 1;
    if first.is_empty() {
        first = chrono_now();
    }
    let v = serde_json::json!({ "runs": runs, "first": first, "last": chrono_now() });
    let _ = fs::write(&p, serde_json::to_string_pretty(&v).unwrap_or_default());
    v
}

#[tauri::command]
fn get_achievements() -> serde_json::Value {
    let mut runs = 0u64;
    let mut first = String::new();
    if let Ok(s) = fs::read_to_string(stats_path()) {
        if let Ok(v) = serde_json::from_str::<serde_json::Value>(&s) {
            runs = v["runs"].as_u64().unwrap_or(0);
            first = v["first"].as_str().unwrap_or("").to_string();
        }
    }
    let items = vec![
        serde_json::json!({ "icon": "R", "name": "首次运行", "desc": "启动 Inimerse 桌面应用", "done": runs >= 1 }),
        serde_json::json!({ "icon": "P", "name": "常驻用户", "desc": "累计运行 10 次", "done": runs >= 10 }),
        serde_json::json!({ "icon": "S", "name": "深度用户", "desc": "累计运行 50 次", "done": runs >= 50 }),
        serde_json::json!({ "icon": "T", "name": "骨灰玩家", "desc": "累计运行 100 次", "done": runs >= 100 }),
        serde_json::json!({ "icon": "W", "name": "联机先锋", "desc": "完成一次 verse 联机", "done": false }),
    ];
    serde_json::json!({ "runs": runs, "first": first, "items": items })
}


fn run_cmd(prog: &str, args: &[&str], out: &mut String) -> i32 {
    use std::process::Command;
    match Command::new(prog).args(args).output() {
        Ok(o) => {
            out.push_str(&String::from_utf8_lossy(&o.stdout));
            out.push_str(&String::from_utf8_lossy(&o.stderr));
            o.status.code().unwrap_or(-1)
        }
        Err(e) => {
            out.push_str(&format!("[err] {}\n", e));
            -1
        }
    }
}

fn update_log_path() -> std::path::PathBuf {
    user_data("update.log")
}

#[tauri::command]
fn update_engine() -> bool {
    std::thread::spawn(move || {
        let log = update_log_path();
        if let Some(dir) = log.parent() {
            let _ = fs::create_dir_all(dir);
        }
        let mut out = String::new();
        macro_rules! step {
            ($s:expr) => {
                out.push_str(&format!("[{}] {}\n", chrono_now(), $s));
                let _ = fs::write(&log, &out);
            };
        }
        step!("== 一键更新开始 ==");
        let root = engine_root(); let root_s = root.to_string_lossy().into_owned();
        step!("1/5 git pull");
        if root.join(".git").is_dir() {
            let _ = run_cmd("git", &["-C", &root_s, "pull"], &mut out);
        } else {
            out.push_str("[warn] 非 git 仓库，跳过 git pull（直接本地构建）\n");
        }
        step!("2/5 构建引擎 build.ps1");
        let build = root.join("build.ps1"); let build_s = build.to_string_lossy().into_owned();
        if build.is_file() { let _ = run_cmd("powershell", &["-NoProfile", "-ExecutionPolicy", "Bypass", "-File", &build_s], &mut out); }
        step!("3/5 备份旧引擎");
        let stamp = chrono_now().replace([':', ' ', '-'], "");
        let backup_dir = user_data(&format!("backups/inimerse_{}_auto", stamp));
        let _ = fs::create_dir_all(&backup_dir);
        let engine = detect_engine();
        if fs::metadata(&engine).is_ok() {
            let _ = fs::copy(&engine, backup_dir.join(if cfg!(windows) { "inimerse.exe" } else { "inimerse" }));
            out.push_str(&format!("备份到 {}\n", backup_dir.to_string_lossy()));
        }
        step!("4/5 部署（build.ps1 已部署到 %USERPROFILE%/Infiverse）");
        step!("5/5 完成。运行中的服务需重启才能使用新引擎。");
        let _ = fs::write(&log, &out);
    });
    true
}

#[tauri::command]
fn get_update_log() -> String {
    fs::read_to_string(update_log_path()).unwrap_or_default()
}

fn json_escape(s: &str) -> String {
    let mut o = String::new();
    for c in s.chars() {
        match c {
            '"' => o.push_str("\\\""),
            '\\' => o.push_str("\\\\"),
            '\n' => o.push_str("\\n"),
            '\r' => o.push_str("\\r"),
            '\t' => o.push_str("\\t"),
            _ => o.push(c),
        }
    }
    o
}
fn unescape_json(s: &str) -> String {
    s.replace("\\n", "\n").replace("\\\"", "\"").replace("\\\\", "\\")
}
#[tauri::command]
fn ai_chat(text: String) -> String {
    
use std::io::{Read, Write};
    use std::net::TcpStream;
    let body = format!(
        "{{\"model\":\"Qwen2.5-7B-Instruct:latest\",\"messages\":[{{\"role\":\"user\",\"content\":\"{}\"}}],\"stream\":false,\"options\":{{\"temperature\":0.7}}}}",
        json_escape(&text)
    );
    let req = format!(
        "POST /api/chat HTTP/1.1\r\nHost: 127.0.0.1:11434\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}",
        body.len(), body
    );
    match TcpStream::connect("127.0.0.1:11434") {
        Ok(mut s) => {
            let _ = s.set_read_timeout(Some(std::time::Duration::from_secs(120)));
            let _ = s.write_all(req.as_bytes());
            let mut buf = Vec::new();
            let _ = s.read_to_end(&mut buf);
            let resp = String::from_utf8_lossy(&buf);
            if let Some(pos) = resp.rfind("\\\"content\\\":\\\"") {
                let rest = &resp[pos + 11..];
                if let Some(end) = rest.find('"') {
                    return unescape_json(&rest[..end]);
                }
            }
            "（Ollama 无有效响应）".to_string()
        }
        Err(_) => "（Ollama 未启动：127.0.0.1:11434）".to_string(),
    }
}

#[tauri::command]
fn chat_save(session: String, messages: String) -> bool {
    let dir = app_root().join("userdata").join("messages");
    let _ = std::fs::create_dir_all(&dir);
    std::fs::write(dir.join(format!("{}.json", session)), messages).is_ok()
}

#[tauri::command]
fn chat_load(session: String) -> String {
    std::fs::read_to_string(app_root().join("userdata").join("messages").join(format!("{}.json", session))).unwrap_or_default()
}

#[tauri::command]
fn friends_list() -> serde_json::Value {
    let mut arr: Vec<serde_json::Value> = Vec::new();
    if let Ok(s) = std::fs::read_to_string(user_data("friends.json")) {
        if let Ok(v) = serde_json::from_str::<serde_json::Value>(&s) {
            if let Some(fs) = v["friends"].as_array() {
                for f in fs {
                    let is_ai = f["is_ai"].as_i64().unwrap_or(0) == 1;
                    arr.push(serde_json::json!({
                        "id": f["id"].as_str().unwrap_or(""),
                        "name": f["name"].as_str().unwrap_or("好友"),
                        "is_ai": is_ai,
                        "avatar": if is_ai { "🤖" } else { "👤" },
                        "hint": if is_ai { "AI 好友（Qwen2.5-7B）" } else { "verse 好友" },
                        "node": f["node"].as_str().unwrap_or(""),
                    }));
                }
            }
        }
    }
    serde_json::Value::Array(arr)
}

#[tauri::command]
fn browse_list() -> serde_json::Value {
    serde_json::json!([
        { "name": "Inimerse 标准版", "ver": "0.9.6", "size": "7.2 MB", "state": "已安装", "desc": "引擎 + 标准模组 + 契约测试" },
        { "name": "Inimerse 桌面应用", "ver": "0.1.0-B2", "size": "12.5 MB", "state": "运行中", "desc": "Tauri 壳 + 8 模块（当前项目）" },
        { "name": "示例项目·贪吃蛇", "ver": "0.2.0", "size": "1.1 MB", "state": "可下载", "desc": "精灵 + 实体 + 键盘输入教学" },
        { "name": "示例项目·平台跳跃", "ver": "0.1.0", "size": "1.8 MB", "state": "可下载", "desc": "物理 + 关卡 + 音效教学" },
        { "name": "AI 编译辅助规则包", "ver": "1.0.0", "size": "64 KB", "state": "可下载", "desc": "--lint 规则 + ai_code_check 提示词" }
    ])
}


const SNAKE_IM: &str = "# 贪吃蛇简化示例（教学）\n# 实体系统 + 空间网格 + 批量渲染\nentity_spawn(16, 12, \"snake_head\")\nentity_set(\"snake_head\", \"hp\", 3)\nsay \"snake: at \" + entity_get(\"snake_head\", \"x\") + \",\" + entity_get(\"snake_head\", \"y\")\nsay \"snake: neighbors=\" + len(entity_neighbors(16, 12, 2))\nsay \"snake demo ok\"\n";

const PLATFORM_IM: &str = "# 平台跳跃简化示例（教学）\n# 实体 + 重力/弹跳\nentity_spawn(10, 10, \"player\")\nentity_set(\"player\", \"hp\", 100)\nentity_set(\"player\", \"vy\", 0)\nsay \"platform: player hp=\" + entity_get(\"player\", \"hp\")\nsay \"platform demo ok\"\n";

#[tauri::command]
fn browse_download(name: String) -> serde_json::Value {
    let (dir, im) = match name.as_str() {
        "示例项目·贪吃蛇" => ("snake", SNAKE_IM),
        "示例项目·平台跳跃" => ("platform", PLATFORM_IM),
        _ => return serde_json::json!({ "name": name, "state": "未知项目", "out": "" }),
    };
    let base = app_root().join("projects").join(&dir).to_string_lossy().into_owned();
    let _ = std::fs::create_dir_all(&base);
    let _ = std::fs::write(format!("{}/main.im", base), im);
    let _ = std::fs::write(format!("{}/README.md", base), format!("# {}（桌面应用下载）\n\n隔离校验通过即视为可运行。\n", name));
    // isolate_run 校验：--safe --low-config --time-limit 5
    let engine = detect_engine();
    let out = if engine.is_empty() {
        "未找到引擎".to_string()
    } else {
        match std::process::Command::new(&engine)
            .args(["--safe", "--low-config", "--time-limit", "5", &format!("{}/main.im", base)])
            .output() {
            Ok(o) => {
                let code = o.status.code().unwrap_or(-1);
                let mut s = String::from_utf8_lossy(&o.stdout).to_string();
                let se = String::from_utf8_lossy(&o.stderr);
                if !se.trim().is_empty() { s.push_str(&format!(" [stderr] {}", &se.chars().take(200).collect::<String>())); }
                s = s.chars().take(300).collect();
                format!("exit={} out={}", code, s.trim())
            }
            Err(e) => format!("启动引擎失败: {}", e),
        }
    };
    let ok = out.starts_with("exit=0");
    serde_json::json!({ "name": name, "state": if ok { "已下载" } else { "校验失败" }, "out": out, "dir": base })
}

#[tauri::command]
fn verse_ping(addr: String) -> serde_json::Value {
    use std::net::{TcpStream, ToSocketAddrs};
    use std::time::{Duration, Instant};
    let addr = if addr.is_empty() { "127.0.0.1:11440".to_string() } else { addr };
    let start = Instant::now();
    let ok = if let Ok(mut addrs) = addr.to_socket_addrs() {
        if let Some(sa) = addrs.next() {
            TcpStream::connect_timeout(&sa, Duration::from_secs(2)).is_ok()
        } else { false }
    } else { false };
    serde_json::json!({ "addr": addr, "ok": ok, "ms": start.elapsed().as_millis() })
}


#[tauri::command]
fn get_public_ip() -> String {
    for url in ["http://ifconfig.me/ip", "http://icanhazip.com", "http://ip.3322.net"] {
        if let Ok(o) = std::process::Command::new("curl.exe").args(["-s", "-m", "5", url]).output() {
            let s = String::from_utf8_lossy(&o.stdout).trim().to_string();
            if !s.is_empty() { return s; }
        }
    }
    String::new()
}



#[tauri::command]
fn inimerse_where() -> serde_json::Value {
    let mut found: Vec<serde_json::Value> = Vec::new();
    let mut seen = std::collections::HashSet::new();
    let candidates = [app_root().join("inimerse.exe")];
    for c in candidates {
        if fs::metadata(&c).is_ok() {
            let path = c.to_string_lossy().into_owned();
            let size = fs::metadata(&c).map(|m| m.len()).unwrap_or(0);
            found.push(serde_json::json!({ "path": path, "size": size }));
            seen.insert(path);
        }
    }
    if let Ok(path) = std::env::var("PATH") {
        for d in path.split(';') {
            if d.is_empty() { continue; }
            let p = format!("{}/inimerse.exe", d.trim_end_matches('\\'));
            if fs::metadata(&p).is_ok() && !seen.contains(&p) {
                let size = fs::metadata(&p).map(|m| m.len()).unwrap_or(0);
                found.push(serde_json::json!({ "path": p, "size": size }));
                seen.insert(p);
            }
        }
    }
    serde_json::json!(found)
}

#[tauri::command]
fn read_changes() -> String {
    let root = app_root();
    let files = [root.join("docs/CHANGES.txt"), root.join("CHANGES.txt"), root.join("ROADMAP.md")];
    for f in files {
        if fs::metadata(&f).is_ok() {
            if let Ok(s) = fs::read_to_string(f) { return s; }
        }
    }
    if let Ok(rd) = fs::read_dir("D:/backup") {
        let mut b: Vec<String> = Vec::new();
        for e in rd.flatten() { let n = e.file_name().to_string_lossy().to_string(); if n.starts_with("inimerse_") { b.push(n); } }
        b.sort();
        if let Some(n) = b.pop() {
            let f = format!("D:/backup/{}/CHANGES.txt", n);
            if fs::metadata(&f).is_ok() {
                if let Ok(s) = fs::read_to_string(&f) { return s; }
            }
        }
    }
    "（无变更日志）".into()
}

#[tauri::command]
fn plugin_install(path: String) -> serde_json::Value {
    let dir = app_root().join("plugins");
    let _ = fs::create_dir_all(&dir);
    let name = std::path::Path::new(&path).file_name().map(|n| n.to_string_lossy().to_string()).unwrap_or_else(|| "plugin.im".into());
    let dst = dir.join(&name);
    match fs::copy(&path, &dst) {
        Ok(_) => serde_json::json!({ "ok": true, "name": name, "dst": dst.to_string_lossy() }),
        Err(e) => serde_json::json!({ "ok": false, "err": e.to_string() }),
    }
}

#[tauri::command]
fn plugin_toggle(name: String) -> bool {
    let dir = app_root().join("plugins");
    let base = dir.join(&name);
    let dis = dir.join(format!("{}.disabled", name));
    if fs::metadata(&dis).is_ok() {
        return fs::rename(&dis, &base).is_ok();
    }
    if fs::metadata(&base).is_ok() {
        return fs::rename(&base, &dis).is_ok();
    }
    false
}

#[tauri::command]
fn clear_userdata_cache() -> serde_json::Value {
    let mut removed = 0usize;
    let dirs = [app_root().join("userdata")];
    for dir in dirs {
        if let Ok(rd) = fs::read_dir(&dir) {
            for e in rd.flatten() {
                let p = e.path();
                let n = e.file_name().to_string_lossy().to_string();
                if n.starts_with("chat_") || n.ends_with(".disabled") || n.starts_with("desugar_") {
                    if fs::remove_file(&p).is_ok() { removed += 1; }
                }
            }
        }
    }
    serde_json::json!({ "removed": removed })
}

fn parse_declares(src: &str) -> Vec<serde_json::Value> {
    let mut out = Vec::new();
    let mut rest = src;
    while let Some(start) = rest.find("declare") {
        let after = &rest[start + 7..];
        let mut s = after;
        while s.starts_with(|c: char| c.is_whitespace()) { s = &s[1..]; }
        if s.starts_with('{') {
            if let Some(end) = s.find('}') {
                let body = &s[1..end];
                for item in body.split(|c| c == ',' || c == ';' || c == '\n') {
                    let item = item.trim();
                    if item.is_empty() { continue; }
                    let bytes = item.as_bytes();
                    let mut k = 0;
                    while k < bytes.len() && !bytes[k].is_ascii_digit() { k += 1; }
                    let kind = &item[..k];
                    let mut d = k;
                    while d < bytes.len() && bytes[d].is_ascii_digit() { d += 1; }
                    let num: i64 = item[k..d].parse().unwrap_or(0);
                    let unit = &item[d..];
                    out.push(serde_json::json!({ "key": kind, "val": num, "unit": unit }));
                }
                rest = &s[end + 1..];
                continue;
            }
        }
        rest = after;
    }
    out
}

#[tauri::command]
fn workbench_load() -> serde_json::Value {
    let mut files: Vec<serde_json::Value> = Vec::new();
    let mut push_file = |path: &str| {
        if let Ok(src) = std::fs::read_to_string(path) {
            let declares = parse_declares(&src);
            files.push(serde_json::json!({ "file": path, "declares": declares }));
        }
    };
    if let Ok(entries) = std::fs::read_dir(app_root().join("plugins")) {
        for e in entries.flatten() {
            let path = e.path();
            if path.extension().and_then(|x| x.to_str()) == Some("im") {
                push_file(path.to_str().unwrap_or(""));
            }
        }
    }
    if let Ok(entries) = std::fs::read_dir(app_root().join("projects")) {
        for e in entries.flatten() {
            let path = e.path();
            if path.is_dir() {
                let main = path.join("main.im");
                if main.exists() { push_file(main.to_str().unwrap_or("")); }
            }
        }
    }
    files.sort_by(|a, b| a["file"].as_str().cmp(&b["file"].as_str()));
    serde_json::json!(files)
}

#[tauri::command]
fn workbench_root() -> String { app_root().to_string_lossy().into_owned() }

#[tauri::command]
fn workbench_create_project(name: String) -> serde_json::Value {
    let valid = !name.is_empty() && name.len() <= 48 && name.chars().all(|c| c.is_ascii_alphanumeric() || c == '_' || c == '-');
    if !valid { return serde_json::json!({ "ok": false, "error": "Use 1-48 letters, numbers, _ or -" }); }
    let dir = app_root().join("projects").join(&name);
    let file = dir.join("main.im");
    if file.exists() { return serde_json::json!({ "ok": false, "error": "Project already exists" }); }
    let template = format!("# {}\n# Infiverse project\n\ndeclare {{ mem64MB, time10s }}\n\nsay \"Hello from {}\"\n", name, name);
    match fs::create_dir_all(&dir).and_then(|_| fs::write(&file, template)) {
        Ok(()) => serde_json::json!({ "ok": true, "file": file.to_string_lossy() }),
        Err(e) => serde_json::json!({ "ok": false, "error": e.to_string() }),
    }
}

#[tauri::command]
fn oauth_status(provider: String) -> serde_json::Value {
    let path = user_data("linked_accounts.json");
    let Ok(text) = fs::read_to_string(path) else { return serde_json::json!({ "linked": false, "provider": provider }); };
    let Ok(v) = serde_json::from_str::<serde_json::Value>(&text) else { return serde_json::json!({ "linked": false, "provider": provider }); };
    let same = v["provider"].as_str().unwrap_or("").eq_ignore_ascii_case(&provider);
    if same { serde_json::json!({ "linked": true, "provider": provider, "user_id": v["user_id"], "display_name": v["display_name"], "avatar": v["avatar"] }) }
    else { serde_json::json!({ "linked": false, "provider": provider }) }
}

#[tauri::command]
fn oauth_authorize(provider: String, client_id: String, redirect_uri: String, state: String) -> String {
    if client_id.trim().is_empty() || redirect_uri.trim().is_empty() { return String::new(); }
    if provider.eq_ignore_ascii_case("github") {
        format!("https://github.com/login/oauth/authorize?client_id={}&redirect_uri={}&scope=read:user%20user:email&state={}", client_id, redirect_uri, state)
    } else if provider.eq_ignore_ascii_case("bilibili") {
        format!("https://passport.bilibili.com/oauth2/authorize?client_id={}&response_type=code&redirect_uri={}&state={}", client_id, redirect_uri, state)
    } else { String::new() }
}

#[tauri::command]
fn oauth_open(url: String) -> serde_json::Value {
    if !(url.starts_with("https://github.com/") || url.starts_with("https://passport.bilibili.com/oauth2/authorize")) {
        return serde_json::json!({ "ok": false, "error": "Unsupported authorization URL" });
    }
    match std::process::Command::new("cmd").args(["/C", "start", "", &url]).spawn() {
        Ok(_) => serde_json::json!({ "ok": true }),
        Err(e) => serde_json::json!({ "ok": false, "error": e.to_string() }),
    }
}

#[tauri::command]
fn oauth_start_callback() -> bool {
    let result = oauth_result();
    if result.lock().map(|mut s| s.clear()).is_err() { return false; }
    std::thread::spawn(move || {
        use std::io::{Read, Write};
        use std::net::TcpListener;
        let Ok(listener) = TcpListener::bind("127.0.0.1:8765") else { return; };
        if let Some(mut stream) = listener.incoming().flatten().next() {
            let mut buf = [0u8; 4096]; let n = stream.read(&mut buf).unwrap_or(0);
            let req = String::from_utf8_lossy(&buf[..n]);
            let target = req.split_whitespace().nth(1).unwrap_or("/");
            if let Some(q) = target.split('?').nth(1) {
                if let Ok(mut out) = result.lock() { *out = q.to_string(); }
            }
            let body = "Authorization received. You can return to Infiverse.";
            let resp = format!("HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}", body.len(), body);
            let _ = stream.write_all(resp.as_bytes());
        }
    });
    true
}

#[tauri::command]
fn oauth_poll_callback() -> String { oauth_result().lock().map(|s| s.clone()).unwrap_or_default() }

fn is_workspace_file(file: &str) -> bool {
    let Ok(root) = app_root().canonicalize() else { return false; };
    let Ok(path) = std::path::Path::new(file).canonicalize() else { return false; };
    path.starts_with(root) && path.extension().and_then(|s| s.to_str()) == Some("im")
}

#[tauri::command]
fn workbench_read(file: String) -> serde_json::Value {
    if !is_workspace_file(&file) { return serde_json::json!({ "ok": false, "error": "File is outside the workspace" }); }
    match fs::read_to_string(&file) {
        Ok(content) => serde_json::json!({ "ok": true, "content": content }),
        Err(e) => serde_json::json!({ "ok": false, "error": e.to_string() }),
    }
}

#[tauri::command]
fn workbench_save(file: String, content: String) -> serde_json::Value {
    if !is_workspace_file(&file) { return serde_json::json!({ "ok": false, "error": "File is outside the workspace" }); }
    match fs::write(&file, content) {
        Ok(()) => serde_json::json!({ "ok": true }),
        Err(e) => serde_json::json!({ "ok": false, "error": e.to_string() }),
    }
}

#[tauri::command]
fn workbench_run(file: String) -> serde_json::Value {
    if !is_workspace_file(&file) { return serde_json::json!({ "ok": false, "error": "File is outside the workspace" }); }
    if workbench_pid().lock().ok().and_then(|g| *g).is_some() {
        return serde_json::json!({ "ok": false, "error": "A workbench task is already running" });
    }
    let engine = detect_engine();
    match std::process::Command::new(engine)
        .args(["--err-json", "--safe", "--time-limit", "10", &file]).spawn() {
        Ok(child) => {
            let pid = child.id();
            if let Ok(mut slot) = workbench_pid().lock() { *slot = Some(pid); }
            let result = child.wait_with_output();
            let stopped = WORKBENCH_STOPPED.load(Ordering::SeqCst);
            if let Ok(mut slot) = workbench_pid().lock() { *slot = None; }
            match result {
                Ok(out) => {
                    let stdout = String::from_utf8_lossy(&out.stdout).chars().take(8000).collect::<String>();
                    let stderr = String::from_utf8_lossy(&out.stderr).chars().take(8000).collect::<String>();
                    serde_json::json!({ "ok": out.status.success(), "stopped": stopped, "code": out.status.code().unwrap_or(-1), "stdout": stdout, "stderr": stderr })
                }
                Err(e) => serde_json::json!({ "ok": false, "error": e.to_string() }),
            }
        }
        Err(e) => serde_json::json!({ "ok": false, "error": e.to_string() }),
    }
}

#[tauri::command]
fn workbench_stop() -> serde_json::Value {
    let pid = workbench_pid().lock().ok().and_then(|g| *g);
    let Some(pid) = pid else { return serde_json::json!({ "ok": false, "error": "No workbench task is running" }); };
    WORKBENCH_STOPPED.store(true, Ordering::SeqCst);
    let ok = if cfg!(windows) {
        std::process::Command::new("taskkill").args(["/PID", &pid.to_string(), "/T", "/F"]).status().map(|s| s.success()).unwrap_or(false)
    } else {
        std::process::Command::new("kill").args(["-TERM", &pid.to_string()]).status().map(|s| s.success()).unwrap_or(false)
    };
    serde_json::json!({ "ok": ok, "pid": pid })
}

#[tauri::command]
fn workbench_apply(file: String, items: serde_json::Value) -> serde_json::Value {
    let mut ok = false;
    let mut msg = String::new();
    if !is_workspace_file(&file) {
        return serde_json::json!({ "ok": false, "msg": "File is outside the workspace" });
    }
    if let Ok(mut src) = std::fs::read_to_string(&file) {
        if let Some(start) = src.find("declare") {
            let after = &src[start + 7..];
            let mut s = after;
            while s.starts_with(|c: char| c.is_whitespace()) { s = &s[1..]; }
            if s.starts_with('{') {
                if let Some(end_rel) = s.find('}') {
                    let mut parts: Vec<String> = Vec::new();
                    if let Some(arr) = items.as_array() {
                        for it in arr {
                            let key = it["key"].as_str().unwrap_or("mem").to_string();
                            let val = it["val"].as_i64().unwrap_or(0);
                            let unit = it["unit"].as_str().unwrap_or("").to_string();
                            parts.push(format!("{}{}{}", key, val, unit));
                        }
                    }
                    let body_new = parts.join(", ");
                    let abs = start + 7 + (after.len() - s.len());
                    let end_abs = abs + 1 + end_rel;
                    src.replace_range(abs..end_abs, &format!("{{ {} }}", body_new));
                    match std::fs::write(&file, &src) {
                        Ok(()) => { ok = true; msg = "已写回".to_string(); }
                        Err(e) => msg = format!("写入失败: {}", e),
                    }
                } else { msg = "declare 块未闭合".to_string(); }
            } else { msg = "declare 后不是 {".to_string(); }
        } else { msg = "文件无 declare 块".to_string(); }
    } else { msg = "读取失败".to_string(); }
    serde_json::json!({ "ok": ok, "msg": msg })
}


use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream};
static VERSE_RUNNING: AtomicBool = AtomicBool::new(false);

fn handle_verse(mut s: TcpStream) -> std::io::Result<()> {
    s.set_read_timeout(Some(std::time::Duration::from_secs(5)))?;
    let mut buf = Vec::new();
    let mut tmp = [0u8; 1024];
    loop {
        let n = s.read(&mut tmp)?;
        if n == 0 { break; }
        buf.extend_from_slice(&tmp[..n]);
        if buf.windows(4).any(|w| w == b"\r\n\r\n") { break; }
        if buf.len() > 65536 { break; }
    }
    let req = String::from_utf8_lossy(&buf);
    let mut body = String::new();
    if let Some(pos) = req.find("\r\n\r\n") { body = req[pos + 4..].to_string(); }
    let mut resp = "HTTP/1.1 400 Bad Request\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}";
    if let Ok(v) = serde_json::from_str::<serde_json::Value>(&body) {
        let from = v["from"].as_str().unwrap_or("").to_string();
        let text = v["text"].as_str().unwrap_or("").to_string();
        let ts = v["ts"].as_i64().unwrap_or(0);
        if !from.is_empty() && !text.is_empty() {
            let path = user_data(&format!("messages/{}.json", from));
            let mut arr: Vec<serde_json::Value> = Vec::new();
            if let Ok(ex) = std::fs::read_to_string(&path) {
                if let Ok(ev) = serde_json::from_str::<serde_json::Value>(&ex) {
                    if let Some(a) = ev.as_array() { arr = a.clone(); }
                }
            }
            arr.push(serde_json::json!({ "who": "other", "text": text, "ts": ts }));
            let _ = std::fs::write(&path, serde_json::to_string(&arr).unwrap_or_default());
            resp = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}";
        }
    }
    let _ = s.write_all(resp.as_bytes());
    Ok(())
}

#[tauri::command]
fn verse_start() -> bool {
    if VERSE_RUNNING.swap(true, Ordering::SeqCst) { return true; }
    std::thread::spawn(|| {
        if let Ok(listener) = TcpListener::bind("127.0.0.1:11480") {
            for stream in listener.incoming().flatten() {
                let _ = handle_verse(stream);
            }
        }
        VERSE_RUNNING.store(false, Ordering::SeqCst);
    });
    true
}

#[tauri::command]
fn verse_send(addr: String, text: String) -> serde_json::Value {
    let mut from = String::new();
    if let Ok(s) = std::fs::read_to_string(user_data("profile.json")) {
        if let Ok(v) = serde_json::from_str::<serde_json::Value>(&s) {
            from = v["id"].as_str().unwrap_or("").to_string();
        }
    }
    let ts = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_secs() as i64).unwrap_or(0);
    let body = serde_json::json!({ "from": from, "text": text, "ts": ts }).to_string();
    let host = if addr.contains(':') { addr.clone() } else { format!("{}:11480", addr) };
    let parsed = host.parse::<std::net::SocketAddr>().unwrap_or_else(|_| "127.0.0.1:11480".parse().unwrap());
    let ok = if let Ok(mut s) = TcpStream::connect_timeout(&parsed, std::time::Duration::from_secs(2)) {
        let req = format!("POST /verse/msg HTTP/1.1\r\nHost: {}\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}", host, body.len(), body);
        let _ = s.write_all(req.as_bytes());
        let mut buf = [0u8; 128];
        let _ = s.read(&mut buf);
        String::from_utf8_lossy(&buf).contains("200 OK")
    } else { false };
    serde_json::json!({ "ok": ok, "from": from })
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![
            get_identity,
            update_identity,
            get_local_ip,
            get_engine_info,
            get_qr_svg,
            record_run,
            get_achievements,
            update_engine,
            get_update_log,
            ai_chat,
            chat_save,
            chat_load,
            friends_list,
            browse_list,
            browse_download,
            verse_ping,
            get_public_ip,
            workbench_load,
            workbench_root,
            workbench_create_project,
            oauth_status,
            oauth_authorize,
            oauth_open,
            oauth_start_callback,
            oauth_poll_callback,
            engine_versions,
            engine_select,
            update_channel_get,
            update_channel_set,
            components_list,
            component_set,
            package_list,
            verse_local_packages,
            verse_package_preview,
            package_remove,
            repair_scan,
            workbench_read,
            workbench_save,
            workbench_run,
            workbench_stop,
            workbench_apply,
            verse_start,
            verse_send,
            tool_files,
            tool_lint,
            tool_stats,
            tool_desugar,
            backup_list,
            plugin_list,
            verse_save_node,
            inimerse_where,
            read_changes,
            plugin_install,
            plugin_toggle,
            clear_userdata_cache
        ])
        .setup(|app| {
            if cfg!(debug_assertions) {
                app.handle().plugin(
                    tauri_plugin_log::Builder::default()
                        .level(log::LevelFilter::Info)
                        .build(),
                )?;
            }
            Ok(())
        })
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
// touch
// touch
// touch2
