use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream};
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};
use std::thread;
use std::time::{Duration, Instant};

use anyhow::Result;

const CONTROL_ADDR: &str = "127.0.0.1:47823";

pub struct SingleInstance {
    primary: bool,
    show_requested: Arc<AtomicBool>,
}

impl SingleInstance {
    pub fn acquire() -> Result<Self> {
        let show_requested = Arc::new(AtomicBool::new(false));

        match TcpListener::bind(CONTROL_ADDR) {
            Ok(listener) => {
                listener.set_nonblocking(true)?;

                let flag = Arc::clone(&show_requested);

                thread::Builder::new()
                    .name("espcube-instance-control".to_string())
                    .spawn(move || {
                        loop {
                            match listener.accept() {
                                Ok((mut stream, _)) => {
                                    let mut buf = [0u8; 32];

                                    if let Ok(n) = stream.read(&mut buf) {
                                        let text = String::from_utf8_lossy(&buf[..n]);

                                        if text.trim() == "SHOW" {
                                            flag.store(true, Ordering::SeqCst);
                                        }
                                    }
                                }

                                Err(err) if err.kind() == std::io::ErrorKind::WouldBlock => {
                                    thread::sleep(Duration::from_millis(40));
                                }

                                Err(_) => {
                                    thread::sleep(Duration::from_millis(200));
                                }
                            }
                        }
                    })?;

                Ok(Self {
                    primary: true,
                    show_requested,
                })
            }

            Err(_) => Ok(Self {
                primary: false,
                show_requested,
            }),
        }
    }

    pub fn is_primary(&self) -> bool {
        self.primary
    }

    pub fn show_requested_flag(&self) -> Arc<AtomicBool> {
        Arc::clone(&self.show_requested)
    }

    pub fn request_show_existing(&self) {
        // The user explicitly launched the app, so make the hand-off robust to
        // short startup races instead of sending a single best-effort packet.
        let deadline = Instant::now() + Duration::from_millis(900);

        while Instant::now() < deadline {
            if let Ok(mut stream) = TcpStream::connect(CONTROL_ADDR) {
                if stream.write_all(b"SHOW\n").is_ok() {
                    let _ = stream.flush();
                    break;
                }
            }

            thread::sleep(Duration::from_millis(50));
        }

        // Native Windows fallback: a user-launched secondary process is
        // allowed to restore/focus the already-running hidden window directly.
        // This is independent of machine model, Bluetooth adapter and install
        // location; it only uses the product window title.
        restore_existing_window();
    }
}

#[cfg(target_os = "windows")]
fn restore_existing_window() {
    use std::ffi::OsStr;
    use std::iter::once;
    use std::os::windows::ffi::OsStrExt;

    type Hwnd = isize;
    const SW_SHOW: i32 = 5;
    const SW_RESTORE: i32 = 9;

    #[link(name = "user32")]
    unsafe extern "system" {
        fn FindWindowW(class_name: *const u16, window_name: *const u16) -> Hwnd;
        fn IsIconic(hwnd: Hwnd) -> i32;
        fn ShowWindow(hwnd: Hwnd, command: i32) -> i32;
        fn SetForegroundWindow(hwnd: Hwnd) -> i32;
    }

    let title: Vec<u16> = OsStr::new("ESPCube Companion")
        .encode_wide()
        .chain(once(0))
        .collect();

    unsafe {
        let hwnd = FindWindowW(std::ptr::null(), title.as_ptr());

        if hwnd != 0 {
            if IsIconic(hwnd) != 0 {
                ShowWindow(hwnd, SW_RESTORE);
            } else {
                ShowWindow(hwnd, SW_SHOW);
            }

            SetForegroundWindow(hwnd);
        }
    }
}

#[cfg(not(target_os = "windows"))]
fn restore_existing_window() {}
