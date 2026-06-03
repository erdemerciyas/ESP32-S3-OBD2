# ESP32-S3 OBD2 Monitor - Build and Flash Script (PowerShell)
# Usage: .\build_flash.ps1 -Action [build|flash|monitor|clean|all] -Port COM3

param(
    [ValidateSet("clean", "build", "flash", "monitor", "all")]
    [string]$Action = "build",

    [string]$Port = "COM3"
)

$ErrorActionPreference = "Stop"
$ProjectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ProjectDir

function Write-Step {
    param([string]$Message)
    Write-Host "==> " -ForegroundColor Green -NoNewline
    Write-Host $Message
}

function Write-Warn {
    param([string]$Message)
    Write-Host "WARNING: " -ForegroundColor Yellow -NoNewline
    Write-Host $Message
}

function Write-Err {
    param([string]$Message)
    Write-Host "ERROR: " -ForegroundColor Red -NoNewline
    Write-Host $Message
}

function Test-IDF {
    if (-not $env:IDF_PATH) {
        Write-Err "ESP-IDF environment not set. Run ESP-IDF PowerShell environment first."
        exit 1
    }
}

switch ($Action) {
    "clean" {
        Write-Step "Cleaning build artifacts..."
        idf.py clean
        Remove-Item -Recurse -Force -ErrorAction SilentlyContinue build, dependencies.lock
    }
    "build" {
        Test-IDF
        Write-Step "Setting target to ESP32-S3..."
        idf.py set-target esp32s3
        Write-Step "Building project..."
        idf.py build
        Write-Step "Build complete!"
    }
    "flash" {
        Test-IDF
        Write-Step "Flashing to ESP32-S3 on $Port..."
        idf.py -p $Port flash
    }
    "monitor" {
        Test-IDF
        Write-Step "Starting serial monitor on $Port..."
        idf.py -p $Port monitor
    }
    "all" {
        & $MyInvocation.MyCommand.Path -Action clean
        & $MyInvocation.MyCommand.Path -Action build
        & $MyInvocation.MyCommand.Path -Action flash
        & $MyInvocation.MyCommand.Path -Action monitor
    }
}

Write-Host ""
Write-Host "Done!" -ForegroundColor Green
