# PowerShell installer for Windows
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location "$scriptDir\.."

New-Item -ItemType Directory -Force -Path "bin" | Out-Null

Write-Host "=== Installing Agent Pet Herdr Plugin ==="

if (Get-Command go -ErrorAction SilentlyContinue) {
    Write-Host "Compiling herdr-agent-pet.exe..."
    $env:CGO_ENABLED = "0"
    go build -trimpath -ldflags "-s -w" -o bin/herdr-agent-pet.exe ./cmd/herdr-agent-pet
    Write-Host "✓ Built bin/herdr-agent-pet.exe"
} else {
    Write-Error "Error: 'go' is required to compile herdr-agent-pet."
}

if (Get-Command agent-pet -ErrorAction SilentlyContinue) {
    Write-Host "✓ Found agent-pet in PATH"
} elseif (Test-Path "$env:LOCALAPPDATA\Programs\Agent Pet\agent-pet.exe") {
    Write-Host "✓ Found agent-pet in LocalAppData"
} else {
    Write-Host "Note: agent-pet was not found in PATH or standard install directories."
    Write-Host "Download the setup installer from: https://github.com/WindyWin/vpet-agent-pet/releases"
}

Write-Host "✓ Agent Pet Herdr plugin installed successfully."
