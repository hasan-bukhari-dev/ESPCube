use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream};
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};
use std::thread;
use std::time::Duration;

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
                                    thread::sleep(Duration::from_millis(150));
                                }

                                Err(_) => {
                                    thread::sleep(Duration::from_millis(500));
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
        if let Ok(mut stream) = TcpStream::connect(CONTROL_ADDR) {
            let _ = stream.write_all(b"SHOW\n");
            let _ = stream.flush();
        }
    }
}
