#[cfg(target_os = "windows")]
use winreg::RegKey;
#[cfg(target_os = "windows")]
use winreg::enums::HKEY_CURRENT_USER;

const RUN_KEY: &str = r"Software\Microsoft\Windows\CurrentVersion\Run";
const VALUE_NAME: &str = "ESPCube Companion";

#[cfg(target_os = "windows")]
pub fn set_enabled(enabled: bool) -> anyhow::Result<()> {
    let hkcu = RegKey::predef(HKEY_CURRENT_USER);
    let (key, _) = hkcu.create_subkey(RUN_KEY)?;

    if enabled {
        let exe = std::env::current_exe()?;
        let command = format!("\"{}\" --background", exe.display());
        key.set_value(VALUE_NAME, &command)?;
    } else {
        let _ = key.delete_value(VALUE_NAME);
    }

    Ok(())
}

#[cfg(not(target_os = "windows"))]
pub fn set_enabled(_enabled: bool) -> anyhow::Result<()> {
    Ok(())
}
