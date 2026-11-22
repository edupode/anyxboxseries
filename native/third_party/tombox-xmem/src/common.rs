use std::io::Write;
use std::sync::{Mutex, OnceLock};
use std::time::{SystemTime, UNIX_EPOCH};

fn log_path() -> &'static std::path::PathBuf {
    static PATH: OnceLock<std::path::PathBuf> = OnceLock::new();
    PATH.get_or_init(|| {
        if let Ok(path) = std::env::var("WSX_COMPAT_LOG") {
            return path.into();
        }
        if let Ok(path) = std::env::var("TOMBOX_LOG") {
            return path.into();
        }
        std::env::temp_dir().join("wsx_compat.log")
    })
}

pub fn record(module: &str, function: &str, detail: &str) {
    static WRITE_LOCK: OnceLock<Mutex<()>> = OnceLock::new();
    let _guard = WRITE_LOCK.get_or_init(|| Mutex::new(())).lock().unwrap_or_else(|e| e.into_inner());
    let timestamp = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|value| value.as_millis())
        .unwrap_or_default();
    if let Ok(mut file) = std::fs::OpenOptions::new().create(true).append(true).open(log_path()) {
        let _ = writeln!(file, "{timestamp} [{module}] {function} {detail}");
    }
}

pub fn default_os_build() -> u32 {
    std::env::var("TOMBOX_GI_OSBUILD")
        .ok()
        .and_then(|value| value.trim().parse().ok())
        .unwrap_or(22000)
}
