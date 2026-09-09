# ============================================================
# ESPCube v1 - ACTIVATE LATEST WINDOWS BUILD
#
# Purpose:
#   1) Stop any running Companion instance.
#   2) Build/test the Companion and production installer.
#   3) Install/upgrade the canonical Companion instance.
#   4) Reset stale autostart registration (startup is opt-in).
#   5) Launch the canonical installed Companion normally.
#   6) Delete older ESPCube-v1-* variants from Downloads,
#      preserving only this ESPCube-v1-LATEST tree/archive.
#
# Run from PowerShell:
#   Set-ExecutionPolicy -Scope Process Bypass
#   .\scripts\activate-latest-windows.ps1
#
# The script intentionally does NOT flash firmware automatically.
# Flash/test the authoritative firmware separately before push.
# ============================================================

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$Companion = Join-Path $Root "companion"
$BuildInstaller = Join-Path $Companion "scripts\build-installer-windows.ps1"
$Installer = Join-Path $Companion "installer\output\ESPCube-Companion-v1.0.0-Setup.exe"
$InstalledExe = Join-Path $env:LOCALAPPDATA "Programs\ESPCube Companion\ESPCube Companion.exe"
$Downloads = Join-Path $HOME "Downloads"

Write-Host ""
Write-Host "============================================================"
Write-Host " ESPCube v1 - ACTIVATE LATEST"
Write-Host "============================================================"
Write-Host "Root: $Root"
Write-Host ""

# ------------------------------------------------------------
# 1. Stop any currently running ESPCube Companion
# ------------------------------------------------------------

Write-Host "[1/6] Stopping existing Companion processes..."

Get-Process -ErrorAction SilentlyContinue |
    Where-Object {
        $_.ProcessName -match '^(espcube-companion|ESPCube Companion)$' -or
        $_.ProcessName -match 'ESPCube|Companion'
    } |
    Stop-Process -Force -ErrorAction SilentlyContinue

Start-Sleep -Milliseconds 500

# ------------------------------------------------------------
# 2. Build/test the canonical Companion + installer
# ------------------------------------------------------------

Write-Host "[2/6] Building/testing Companion and installer..."

if (-not (Test-Path $BuildInstaller)) {
    throw "Installer build script not found: $BuildInstaller"
}

& $BuildInstaller

if ($LASTEXITCODE -ne 0) {
    throw "Companion installer build failed."
}

if (-not (Test-Path $Installer)) {
    throw "Expected installer not found: $Installer"
}

# ------------------------------------------------------------
# 3. Install/upgrade canonical Companion
# ------------------------------------------------------------

Write-Host "[3/6] Installing/upgrading canonical Companion..."

& $Installer /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS /RESTARTAPPLICATIONS
if ($LASTEXITCODE -ne 0) {
    throw "Companion installer returned exit code $LASTEXITCODE."
}

if (-not (Test-Path $InstalledExe)) {
    throw "Installed Companion executable not found: $InstalledExe"
}

# ------------------------------------------------------------
# 4. Reset stale Windows autostart registration
#
# Startup is intentionally opt-in. The Companion UI owns this
# preference and will create/remove the Run entry when the user
# toggles "Start quietly with Windows (optional)".
# ------------------------------------------------------------

Write-Host "[4/6] Resetting stale autostart entry (startup is opt-in)..."

$RunKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run"
Remove-ItemProperty -Path $RunKey -Name "ESPCube Companion" -ErrorAction SilentlyContinue

# ------------------------------------------------------------
# 5. Launch canonical installed Companion
# ------------------------------------------------------------

Write-Host "[5/6] Launching canonical Companion..."

Start-Process -FilePath $InstalledExe
Start-Sleep -Seconds 2

$Running = Get-Process -ErrorAction SilentlyContinue |
    Where-Object {
        $_.Path -eq $InstalledExe -or
        $_.ProcessName -eq "espcube-companion"
    } |
    Select-Object -First 1

if ($null -eq $Running) {
    Write-Warning "Companion launch was requested, but the process was not found after 2 seconds."
} else {
    Write-Host "Running PID: $($Running.Id)"
}

# ------------------------------------------------------------
# 6. Remove obsolete Downloads variants
#
# Preserve:
#   Downloads\ESPCube-v1-LATEST\
#   Downloads\ESPCube-v1-LATEST.zip
#
# If this tree was extracted under another wrapper directory,
# preserve the directory containing the current project root too.
# ------------------------------------------------------------

Write-Host "[6/6] Removing obsolete ESPCube-v1 variants from Downloads..."

$PreservePaths = @(
    (Join-Path $Downloads "ESPCube-v1-LATEST"),
    (Join-Path $Downloads "ESPCube-v1-LATEST.zip"),
    $Root
)

$RootParent = Split-Path -Parent $Root
if ($RootParent -like "$Downloads*") {
    $PreservePaths += $RootParent
}

function Is-PreservedPath {
    param([string]$Path)

    $ResolvedCandidate = [System.IO.Path]::GetFullPath($Path).TrimEnd('\')

    foreach ($Preserve in $PreservePaths) {
        if ([string]::IsNullOrWhiteSpace($Preserve)) {
            continue
        }

        $ResolvedPreserve = [System.IO.Path]::GetFullPath($Preserve).TrimEnd('\')

        if ($ResolvedCandidate -ieq $ResolvedPreserve) {
            return $true
        }
    }

    return $false
}

$Obsolete = Get-ChildItem -Path $Downloads -Force -ErrorAction SilentlyContinue |
    Where-Object {
        $_.Name -like "ESPCube-v1-*" -and
        -not (Is-PreservedPath $_.FullName)
    }

if ($Obsolete) {
    Write-Host ""
    Write-Host "Deleting obsolete variants:"
    $Obsolete | ForEach-Object { Write-Host "  $($_.FullName)" }

    foreach ($Item in $Obsolete) {
        Remove-Item -LiteralPath $Item.FullName -Recurse -Force
    }
} else {
    Write-Host "No obsolete ESPCube-v1-* variants found."
}

Write-Host ""
Write-Host "============================================================"
Write-Host " LATEST ESPCUBE INSTANCE ACTIVE"
Write-Host "============================================================"
Write-Host "Installed Companion:"
Write-Host "  $InstalledExe"
Write-Host ""
Write-Host "Project source of truth:"
Write-Host "  $Root"
Write-Host ""
Write-Host "Next: flash/test this firmware, then push only after the"
Write-Host "physical regression suite passes."
Write-Host ""
