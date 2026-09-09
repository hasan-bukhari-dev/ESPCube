use std::fs::{self, OpenOptions};
use std::io::Write;
use std::path::PathBuf;
use std::sync::{Mutex, OnceLock};
use std::time::{SystemTime, UNIX_EPOCH};

static LOG_PATH: OnceLock<PathBuf> = OnceLock::new();
static LOCK: Mutex<()> = Mutex::new(());

pub fn init() {
    let Some(local) = std::env::var_os("LOCALAPPDATA") else {
        return;
    };

    let root = PathBuf::from(local).join("ESPCube").join("logs");
    let _ = fs::create_dir_all(&root);

    let path = root.join("companion.log");

    if let Ok(meta) = fs::metadata(&path) {
        if meta.len() > 2 * 1024 * 1024 {
            let old = root.join("companion.previous.log");
            let _ = fs::remove_file(&old);
            let _ = fs::rename(&path, &old);
        }
    }

    let _ = LOG_PATH.set(path);
    write("[START] ESPCube Companion");
}

pub fn write(message: &str) {
    let Some(path) = LOG_PATH.get() else {
        return;
    };

    let _guard = LOCK.lock().ok();

    let ts = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|v| v.as_secs())
        .unwrap_or(0);

    if let Ok(mut file) = OpenOptions::new().create(true).append(true).open(path) {
        let _ = writeln!(file, "{ts} {message}");
    }
}

pub fn open_folder() {
    let Some(path) = LOG_PATH.get() else {
        return;
    };

    if let Some(parent) = path.parent() {
        let _ = std::process::Command::new("explorer").arg(parent).spawn();
    }
}
