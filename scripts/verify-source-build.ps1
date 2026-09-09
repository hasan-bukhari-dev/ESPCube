$ErrorActionPreference="Stop"

$Repo=Split-Path -Parent $PSScriptRoot
$Firmware=Join-Path $Repo "firmware"
$Companion=Join-Path $Repo "companion"

Write-Host "=== FIRMWARE ==="
Set-Location $Firmware

if(Get-Command platformio -ErrorAction SilentlyContinue){
    $PIO="platformio"
}
elseif(Test-Path "$HOME\.platformio\penv\Scripts\platformio.exe"){
    $PIO="$HOME\.platformio\penv\Scripts\platformio.exe"
}
else{
    throw "PlatformIO not found."
}

& $PIO run -e espcube
if($LASTEXITCODE -ne 0){ throw "Canonical firmware build failed." }

$FirmwareBin=Join-Path $Firmware ".pio\build\espcube\firmware.bin"
if(-not(Test-Path $FirmwareBin)){ throw "firmware.bin missing." }

Write-Host ""
Write-Host "CANONICAL FIRMWARE BUILD PASS"
Get-FileHash $FirmwareBin -Algorithm SHA256

Write-Host ""
Write-Host "=== COMPANION ==="
Set-Location $Companion

cargo check
if($LASTEXITCODE -ne 0){ throw "cargo check failed." }

cargo test
if($LASTEXITCODE -ne 0){ throw "cargo test failed." }

cargo build --release
if($LASTEXITCODE -ne 0){ throw "cargo build --release failed." }

$Exe=Join-Path $Companion "target\release\espcube-companion.exe"
if(-not(Test-Path $Exe)){ throw "Companion EXE missing." }

Write-Host ""
Write-Host "CANONICAL COMPANION BUILD PASS"
Get-FileHash $Exe -Algorithm SHA256

$Model=Join-Path $Companion "models\ggml-tiny.en.bin"
$Hash=(Get-FileHash $Model -Algorithm SHA256).Hash
if($Hash -ne "921E4CF8686FDD993DCD081A5DA5B6C365BFDE1162E72B08D75AC75289920B1F"){
    throw "Whisper model mismatch."
}

Write-Host ""
Write-Host "CANONICAL WHISPER MODEL PASS"
Write-Host "SHA256: $Hash"

Write-Host ""
Write-Host "=============================================="
Write-Host " CANONICAL SOURCE BUILD TEST COMPLETE"
Write-Host "=============================================="
