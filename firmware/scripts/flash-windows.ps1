param(
    [string]$Port = ""
)

$ErrorActionPreference = "Stop"
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$Firmware = Split-Path -Parent $Here

Set-Location $Firmware

if(Get-Command platformio -ErrorAction SilentlyContinue){
    $PIO = "platformio"
}
elseif(Test-Path "$HOME\.platformio\penv\Scripts\platformio.exe"){
    $PIO = "$HOME\.platformio\penv\Scripts\platformio.exe"
}
else{
    throw "PlatformIO was not found. Install PlatformIO first."
}

if([string]::IsNullOrWhiteSpace($Port)){
    & $PIO run -e espcube -t upload
}
else{
    & $PIO run -e espcube -t upload --upload-port $Port
}

if($LASTEXITCODE -ne 0){
    throw "Firmware upload failed."
}

Write-Host ""
Write-Host "ESPCube firmware upload complete."
