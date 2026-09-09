param(
    [switch]$SkipCargo
)

$ErrorActionPreference = "Stop"

$Companion = Split-Path -Parent $PSScriptRoot
$Root = Split-Path -Parent $Companion
$Model = Join-Path $Companion "models\ggml-tiny.en.bin"
$Iss = Join-Path $Companion "installer\ESPCubeCompanion.iss"
$Stage = Join-Path $Companion "installer\release"
$Output = Join-Path $Companion "installer\output"
$Exe = Join-Path $Companion "target\release\espcube-companion.exe"

if(-not(Test-Path $Model)){ throw "Whisper model missing: $Model" }
if(-not(Test-Path $Iss)){ throw "Installer script missing: $Iss" }

$ModelHash=(Get-FileHash $Model -Algorithm SHA256).Hash
if($ModelHash -ne "921E4CF8686FDD993DCD081A5DA5B6C365BFDE1162E72B08D75AC75289920B1F"){
    throw "Whisper model hash mismatch."
}

if(-not $SkipCargo){
    Set-Location $Companion
    cargo check
    if($LASTEXITCODE -ne 0){ throw "cargo check failed" }

    cargo test
    if($LASTEXITCODE -ne 0){ throw "cargo test failed" }

    cargo build --release
    if($LASTEXITCODE -ne 0){ throw "cargo build --release failed" }
}

if(-not(Test-Path $Exe)){ throw "Companion release EXE missing: $Exe" }

if(Test-Path $Stage){ Remove-Item $Stage -Recurse -Force }
if(Test-Path $Output){ Remove-Item $Output -Recurse -Force }

New-Item -ItemType Directory -Force -Path `
    $Stage, `
    (Join-Path $Stage "models") | Out-Null

Copy-Item $Exe (Join-Path $Stage "ESPCube Companion.exe") -Force
Copy-Item $Model (Join-Path $Stage "models\ggml-tiny.en.bin") -Force

$SearchRoots=@(
    "$env:LOCALAPPDATA\Programs",
    "$env:LOCALAPPDATA\Microsoft\WinGet\Packages",
    "C:\Program Files",
    "C:\Program Files (x86)"
)

$ISCC=$null
foreach($SearchRoot in $SearchRoots){
    if(Test-Path $SearchRoot){
        $Found=Get-ChildItem $SearchRoot -Filter ISCC.exe -Recurse -ErrorAction SilentlyContinue |
            Select-Object -First 1 -ExpandProperty FullName
        if($Found){ $ISCC=$Found; break }
    }
}

if(-not $ISCC){
    Write-Host "Inno Setup compiler not found. Attempting installation..."
    winget install --id JRSoftware.InnoSetup -e --accept-package-agreements --accept-source-agreements
    if($LASTEXITCODE -ne 0){ throw "Inno Setup installation failed." }

    foreach($SearchRoot in $SearchRoots){
        if(Test-Path $SearchRoot){
            $Found=Get-ChildItem $SearchRoot -Filter ISCC.exe -Recurse -ErrorAction SilentlyContinue |
                Select-Object -First 1 -ExpandProperty FullName
            if($Found){ $ISCC=$Found; break }
        }
    }
}

if(-not $ISCC){ throw "ISCC.exe still not found after installation." }

Write-Host "Using Inno Setup:"
Write-Host $ISCC
Write-Host ""

& $ISCC $Iss
if($LASTEXITCODE -ne 0){ throw "Inno Setup compilation failed." }

$Installer=Join-Path $Output "ESPCube-Companion-v1.0.0-Setup.exe"
if(-not(Test-Path $Installer)){ throw "Installer was not created." }

Write-Host ""
Write-Host "=============================================="
Write-Host " ESPCUBE COMPANION INSTALLER BUILD PASS"
Write-Host "=============================================="
Write-Host $Installer
Write-Host ""

Write-Host "Companion EXE SHA256:"
Get-FileHash $Exe -Algorithm SHA256

Write-Host ""
Write-Host "Whisper model SHA256:"
Get-FileHash $Model -Algorithm SHA256

Write-Host ""
Write-Host "Installer SHA256:"
Get-FileHash $Installer -Algorithm SHA256
