use std::fs;
use std::path::PathBuf;
use std::process::Command;

use anyhow::{Context, Result, bail};
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TrustedNetwork {
    pub ssid: String,
    pub password: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
struct TrustedNetworksFile {
    networks: Vec<TrustedNetwork>,
}

fn trusted_path() -> Result<PathBuf> {
    let local = std::env::var_os("LOCALAPPDATA").context("LOCALAPPDATA unavailable")?;

    Ok(PathBuf::from(local)
        .join("ESPCube")
        .join("trusted_wifi.json"))
}

pub fn current_windows_wifi_ssid() -> Result<String> {
    let output = Command::new("netsh")
        .args(["wlan", "show", "interfaces"])
        .output()
        .context("Could not run 'netsh wlan show interfaces'")?;

    if !output.status.success() {
        bail!("Windows could not report the current Wi-Fi network.");
    }

    let text = String::from_utf8_lossy(&output.stdout);

    for raw_line in text.lines() {
        let line = raw_line.trim();

        if line.starts_with("SSID") && !line.starts_with("BSSID") {
            if let Some((_, value)) = line.split_once(':') {
                let ssid = value.trim();

                if !ssid.is_empty() {
                    return Ok(ssid.to_string());
                }
            }
        }
    }

    bail!("Windows is not currently connected to Wi-Fi.")
}

fn load_all() -> Result<Vec<TrustedNetwork>> {
    let path = trusted_path()?;

    if !path.exists() {
        return Ok(Vec::new());
    }

    let text =
        fs::read_to_string(&path).with_context(|| format!("Could not read {}", path.display()))?;

    let value: serde_json::Value = serde_json::from_str(text.trim_start_matches('\u{feff}'))?;

    let mut out = Vec::new();

    if value.get("ssid").is_some() && value.get("password").is_some() {
        out.push(serde_json::from_value(value.clone())?);
    }

    if let Some(arr) = value.get("networks").and_then(|v| v.as_array()) {
        for item in arr {
            if item.get("ssid").is_some() && item.get("password").is_some() {
                out.push(serde_json::from_value(item.clone())?);
            }
        }
    }

    Ok(out)
}

pub fn load_trusted_current_network() -> Result<TrustedNetwork> {
    let current_ssid = current_windows_wifi_ssid()?;

    for net in load_all()? {
        if net.ssid == current_ssid {
            return Ok(net);
        }
    }

    bail!(
        "Current Windows Wi-Fi '{}' is not trusted yet.",
        current_ssid
    )
}

pub fn save_current_network(password: &str) -> Result<String> {
    if password.is_empty() {
        bail!("Wi-Fi password cannot be empty.");
    }

    let ssid = current_windows_wifi_ssid()?;
    let mut networks = load_all()?;

    if let Some(existing) = networks.iter_mut().find(|n| n.ssid == ssid) {
        existing.password = password.to_string();
    } else {
        networks.push(TrustedNetwork {
            ssid: ssid.clone(),
            password: password.to_string(),
        });
    }

    let path = trusted_path()?;

    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)?;
    }

    let wrapped = TrustedNetworksFile { networks };
    let text = serde_json::to_string_pretty(&wrapped)?;
    fs::write(&path, text)?;

    // Preserve compatibility with the existing trusted_wifi.json format
    // while restricting the file to the current Windows user where possible.
    if let Ok(user) = std::env::var("USERNAME") {
        let grant = format!("{user}:(R,W)");

        let _ = Command::new("icacls")
            .arg(&path)
            .args(["/inheritance:r", "/grant:r", &grant])
            .output();
    }

    Ok(ssid)
}

pub fn trusted_current_ssid() -> Option<String> {
    load_trusted_current_network().ok().map(|n| n.ssid)
}
