# ESP32-S3 OBD2 Monitor — Windows build / flash helper
# Kullanım: .\build_flash.ps1 -Action all -Port COM3
# Ayrıntılar: UPLOAD.md

param(
    [ValidateSet('build', 'flash', 'monitor', 'all', 'reconfigure')]
    [string]$Action = 'build',
    [string]$Port = 'COM3'
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = $PSScriptRoot

function Ensure-IdfEnvironment {
    if (Get-Command idf.py -ErrorAction SilentlyContinue) {
        return
    }

    if (-not $env:IDF_TOOLS_PATH) {
        $env:IDF_TOOLS_PATH = 'C:\Espressif'
    }

    $pyScripts = 'C:\Espressif\python_env\idf5.3_py3.11_env\Scripts'
    if (Test-Path $pyScripts) {
        $env:Path = "$pyScripts;" + $env:Path
    }

    $exportCandidates = @(
        $env:IDF_PATH + '\export.ps1',
        'C:\Espressif\frameworks\esp-idf-v5.3.5\export.ps1',
        'C:\esp\esp-idf\export.ps1'
    )

    foreach ($export in $exportCandidates) {
        if ($export -and (Test-Path $export)) {
            Write-Host "Loading IDF: $export"
            . $export
            return
        }
    }

    throw "idf.py not found. Run export.ps1 or see UPLOAD.md (Geliştirme ortamı)."
}

Push-Location $ProjectRoot
try {
    Ensure-IdfEnvironment

    switch ($Action) {
        'build' {
            idf.py build
        }
        'flash' {
            idf.py -p $Port flash
        }
        'monitor' {
            idf.py -p $Port monitor
        }
        'all' {
            idf.py build
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
            idf.py -p $Port flash monitor
        }
        'reconfigure' {
            Remove-Item -Recurse -Force build, sdkconfig -ErrorAction SilentlyContinue
            idf.py set-target esp32s3
            idf.py build
        }
    }

    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}
finally {
    Pop-Location
}
