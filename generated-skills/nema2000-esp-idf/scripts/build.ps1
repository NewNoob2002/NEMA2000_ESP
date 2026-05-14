param(
    [string]$ProjectRoot = (Resolve-Path "$PSScriptRoot\..\..\..").Path
)

$ErrorActionPreference = "Stop"

$profilePath = "C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1"
if (-not (Test-Path -LiteralPath $profilePath)) {
    throw "ESP-IDF PowerShell profile not found: $profilePath"
}

Set-Location -LiteralPath $ProjectRoot
. $profilePath
idf.py build
exit $LASTEXITCODE
