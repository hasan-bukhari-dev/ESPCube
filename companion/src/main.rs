#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod app;
mod audio;
mod autostart;
mod ble;
mod dsp;
mod logging;
mod settings;
mod single_instance;
mod speaker;
mod speech;
mod state;
mod wifi;

use std::sync::Arc;

use anyhow::Result;
use parking_lot::RwLock;

use crate::app::CompanionApp;
use crate::settings::Settings;
use crate::state::SharedState;

fn main() -> Result<()> {
    logging::init();

    std::panic::set_hook(Box::new(|info| {
        logging::write(&format!("[PANIC] {info}"));
    }));

    let instance = single_instance::SingleInstance::acquire()?;

    if !instance.is_primary() {
        instance.request_show_existing();
        return Ok(());
    }

    let background = std::env::args().any(|arg| arg == "--background");

    let settings = Arc::new(RwLock::new(Settings::load().unwrap_or_default()));

    let shared = Arc::new(RwLock::new(SharedState::new()));

    let show_requested = instance.show_requested_flag();

    ble::spawn_unified_manager(Arc::clone(&shared), Arc::clone(&settings));

    let native_options = eframe::NativeOptions {
        viewport: eframe::egui::ViewportBuilder::default()
            .with_inner_size([400.0, 590.0])
            .with_min_inner_size([370.0, 520.0])
            .with_resizable(true)
            .with_title("ESPCube Companion"),
        ..Default::default()
    };

    let app_shared = Arc::clone(&shared);
    let app_settings = Arc::clone(&settings);

    eframe::run_native(
        "ESPCube Companion",
        native_options,
        Box::new(move |cc| {
            Ok(Box::new(CompanionApp::new(
                cc,
                app_shared,
                app_settings,
                show_requested,
                background,
            )))
        }),
    )
    .map_err(|e| anyhow::anyhow!(e.to_string()))
}
