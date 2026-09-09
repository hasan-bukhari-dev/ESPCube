use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::Duration;

use eframe::egui::{self, Align, Color32, Layout, RichText, TextEdit};
use parking_lot::RwLock;

use crate::autostart;
use crate::logging;
use crate::settings::Settings;
use crate::state::{PresenceState, SharedState};
use crate::wifi;

pub struct CompanionApp {
    shared: Arc<RwLock<SharedState>>,
    settings: Arc<RwLock<Settings>>,
    show_requested: Arc<AtomicBool>,
    previous_generation: u64,
    previous_presence: PresenceState,
    background_start: bool,
    background_hidden_once: bool,
    allow_close: bool,
    wifi_password: String,
    wifi_message: String,
    current_ssid: String,
    save_message: String,
}

impl CompanionApp {
    pub fn new(
        cc: &eframe::CreationContext<'_>,
        shared: Arc<RwLock<SharedState>>,
        settings: Arc<RwLock<Settings>>,
        show_requested: Arc<AtomicBool>,
        background_start: bool,
    ) -> Self {
        let mut visuals = egui::Visuals::dark();

        visuals.panel_fill = Color32::from_rgb(18, 18, 24);

        visuals.window_fill = visuals.panel_fill;

        visuals.selection.bg_fill = Color32::from_rgb(105, 82, 145);

        cc.egui_ctx.set_visuals(visuals);

        Self {
            shared,
            settings,
            show_requested,
            previous_generation: 0,
            previous_presence: PresenceState::Dormant,
            background_start,
            background_hidden_once: false,
            allow_close: false,
            wifi_password: String::new(),
            wifi_message: wifi::trusted_current_ssid()
                .map(|s| format!("Trusted: {s}"))
                .unwrap_or_else(|| "Current network not trusted yet".to_string()),
            current_ssid: wifi::current_windows_wifi_ssid()
                .unwrap_or_else(|_| "Not connected".to_string()),
            save_message: String::new(),
        }
    }

    fn save(&mut self, settings: &Settings) {
        match settings.save() {
            Ok(_) => {
                self.save_message = "Saved".to_string();
            }
            Err(err) => {
                self.save_message = format!("Save failed: {err}");
            }
        }
    }
}

impl eframe::App for CompanionApp {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        ctx.request_repaint_after(Duration::from_millis(300));

        let snapshot = self.shared.read().clone();

        if self.background_start && !self.background_hidden_once {
            ctx.send_viewport_cmd(egui::ViewportCommand::Visible(false));

            self.background_hidden_once = true;
        }

        if self.show_requested.swap(false, Ordering::SeqCst) {
            ctx.send_viewport_cmd(egui::ViewportCommand::Visible(true));

            ctx.send_viewport_cmd(egui::ViewportCommand::Focus);
        }

        let show_on_connect = self.settings.read().show_on_connect;

        if snapshot.connection_generation != self.previous_generation {
            self.previous_generation = snapshot.connection_generation;

            if show_on_connect {
                ctx.send_viewport_cmd(egui::ViewportCommand::Visible(true));

                ctx.send_viewport_cmd(egui::ViewportCommand::Focus);
            }
        }

        if self.previous_presence == PresenceState::Grace
            && snapshot.presence == PresenceState::Dormant
        {
            ctx.send_viewport_cmd(egui::ViewportCommand::Visible(false));
        }

        self.previous_presence = snapshot.presence;

        if ctx.input(|input| input.viewport().close_requested()) && !self.allow_close {
            ctx.send_viewport_cmd(egui::ViewportCommand::CancelClose);

            ctx.send_viewport_cmd(egui::ViewportCommand::Visible(false));
        }

        egui::CentralPanel::default().show(ctx, |ui| {
            ui.add_space(8.0);

            ui.horizontal(|ui| {
                ui.heading(
                    RichText::new("ESPCube")
                        .size(28.0)
                        .color(Color32::from_rgb(221, 202, 255))
                        .strong(),
                );

                ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                    ui.label(
                        RichText::new(presence_label(snapshot.presence))
                            .color(presence_color(snapshot.presence))
                            .strong(),
                    );
                });
            });

            ui.label(RichText::new("Companion").color(Color32::from_gray(145)));

            ui.add_space(14.0);

            ui.group(|ui| {
                ui.set_min_width(ui.available_width());

                ui.label(RichText::new(&snapshot.ble_detail).size(17.0).strong());

                if let Some(device) = &snapshot.device_name {
                    ui.label(RichText::new(device).color(Color32::from_gray(160)));
                }

                if snapshot.presence == PresenceState::Grace {
                    if let Some(remaining) = snapshot.grace_remaining() {
                        let secs = remaining.as_secs();

                        ui.label(
                            RichText::new(format!("Sleeping in {}:{:02}", secs / 60, secs % 60))
                                .color(Color32::from_rgb(230, 191, 255)),
                        );
                    }
                }
            });

            ui.add_space(12.0);

            section(ui, "Speech", &snapshot.speech_status);

            ui.label(
                RichText::new(&snapshot.speech_detail)
                    .small()
                    .color(Color32::from_gray(150)),
            );

            if !snapshot.last_transcript.is_empty() {
                ui.label(
                    RichText::new(format!("“{}”", snapshot.last_transcript))
                        .italics()
                        .color(Color32::from_rgb(210, 197, 230)),
                );
            }

            ui.add_space(12.0);

            section(ui, "Speaker", &snapshot.speaker_status);

            ui.label(
                RichText::new(&snapshot.speaker_detail)
                    .small()
                    .color(Color32::from_gray(150)),
            );

            if let Some(ssid) = &snapshot.wifi_ssid {
                tiny_row(ui, "Wi-Fi", ssid);
            }

            if let Some(device) = &snapshot.audio_device {
                tiny_row(ui, "Audio", device);
            }

            ui.add_space(14.0);
            ui.separator();
            ui.add_space(10.0);

            ui.label(RichText::new("Settings").strong());

            let mut current = self.settings.read().clone();

            let original = current.clone();

            ui.checkbox(&mut current.show_on_connect, "Show when ESPCube connects");

            ui.checkbox(
                &mut current.start_with_windows,
                "Start quietly with Windows",
            );

            ui.checkbox(&mut current.speech_enabled, "Speech typing");

            ui.checkbox(&mut current.speaker_enabled, "Windows Speaker mirror");

            ui.horizontal(|ui| {
                ui.label("Sleep after disconnect");

                egui::ComboBox::from_id_salt("disconnect_grace")
                    .selected_text(format!("{} min", current.disconnect_grace_secs / 60))
                    .show_ui(ui, |ui| {
                        for (label, secs) in [
                            ("1 min", 60),
                            ("3 min", 180),
                            ("5 min", 300),
                            ("10 min", 600),
                        ] {
                            ui.selectable_value(&mut current.disconnect_grace_secs, secs, label);
                        }
                    });
            });

            if current.start_with_windows != original.start_with_windows {
                if let Err(err) = autostart::set_enabled(current.start_with_windows) {
                    current.start_with_windows = original.start_with_windows;

                    self.save_message = format!("Autostart failed: {err}");
                }
            }

            let changed = current.show_on_connect != original.show_on_connect
                || current.start_with_windows != original.start_with_windows
                || current.disconnect_grace_secs != original.disconnect_grace_secs
                || current.speech_enabled != original.speech_enabled
                || current.speaker_enabled != original.speaker_enabled;

            if changed {
                *self.settings.write() = current.clone();

                self.save(&current);
            }

            ui.add_space(10.0);
            ui.separator();
            ui.add_space(8.0);

            ui.label(RichText::new("Trusted Wi-Fi").strong());

            ui.label(
                RichText::new(format!("Current: {}", self.current_ssid))
                    .small()
                    .color(Color32::from_gray(150)),
            );

            ui.horizontal(|ui| {
                ui.add(
                    TextEdit::singleline(&mut self.wifi_password)
                        .password(true)
                        .hint_text("Wi-Fi password"),
                );

                if ui.button("Trust").clicked() {
                    match wifi::save_current_network(&self.wifi_password) {
                        Ok(ssid) => {
                            self.wifi_message = format!("Trusted: {ssid}");

                            self.current_ssid = ssid;
                            self.wifi_password.clear();
                        }

                        Err(err) => {
                            self.wifi_message = format!("Could not save: {err}");
                        }
                    }
                }
            });

            ui.label(
                RichText::new(&self.wifi_message)
                    .small()
                    .color(Color32::from_gray(145)),
            );

            ui.add_space(12.0);

            if let Some(err) = &snapshot.last_error {
                ui.label(
                    RichText::new(err)
                        .small()
                        .color(Color32::from_rgb(245, 158, 158)),
                );
            }

            ui.horizontal(|ui| {
                if ui.button("Hide").clicked() {
                    ctx.send_viewport_cmd(egui::ViewportCommand::Visible(false));
                }

                if ui.button("Open logs").clicked() {
                    logging::open_folder();
                }

                if ui.button("Quit").clicked() {
                    self.allow_close = true;

                    ctx.send_viewport_cmd(egui::ViewportCommand::Close);
                }

                ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                    if !self.save_message.is_empty() {
                        ui.label(
                            RichText::new(&self.save_message)
                                .small()
                                .color(Color32::from_gray(125)),
                        );
                    }
                });
            });

            ui.add_space(8.0);

            ui.label(
                RichText::new("Windows-only • BLE + Speech + Speaker")
                    .small()
                    .color(Color32::from_gray(95)),
            );
        });
    }
}

fn section(ui: &mut egui::Ui, name: &str, status: &str) {
    ui.horizontal(|ui| {
        ui.label(RichText::new(name).size(16.0).strong());

        ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
            ui.label(RichText::new(status).color(Color32::from_rgb(198, 177, 235)));
        });
    });
}

fn tiny_row(ui: &mut egui::Ui, name: &str, value: &str) {
    ui.horizontal(|ui| {
        ui.label(RichText::new(name).small().color(Color32::from_gray(120)));

        ui.label(RichText::new(value).small().color(Color32::from_gray(170)));
    });
}

fn presence_label(state: PresenceState) -> &'static str {
    match state {
        PresenceState::Dormant => "Dormant",
        PresenceState::Activating => "Connecting",
        PresenceState::Ready => "Connected",
        PresenceState::Grace => "Reconnecting",
    }
}

fn presence_color(state: PresenceState) -> Color32 {
    match state {
        PresenceState::Dormant => Color32::from_rgb(155, 155, 170),

        PresenceState::Activating => Color32::from_rgb(194, 169, 255),

        PresenceState::Ready => Color32::from_rgb(151, 224, 177),

        PresenceState::Grace => Color32::from_rgb(245, 196, 123),
    }
}
