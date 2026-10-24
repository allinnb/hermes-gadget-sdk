$ErrorActionPreference = "Stop"

$BridgeHost = "100.110.7.45"
$BridgePort = "18901"
$WatchSerial = "COM9"
$Python = "C:\Python314\python.exe"
$Script = Join-Path $PSScriptRoot "stopwatch-bridge.py"

if (-not (Test-Path $Python)) {
    throw "Python not found: $Python"
}

& $Python -c "import serial" 2>$null
if ($LASTEXITCODE -ne 0) {
    throw "pyserial is not installed. Run: $Python -m pip install pyserial"
}

$boundAddress = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
    Where-Object { $_.IPAddress -eq $BridgeHost }
if (-not $boundAddress) {
    throw "This PC is not bound to $BridgeHost. Start Tailscale first."
}

$portInUse = Get-NetTCPConnection -LocalPort ([int]$BridgePort) -State Listen -ErrorAction SilentlyContinue
if ($portInUse) {
    throw "Port $BridgePort is already used by process $($portInUse.OwningProcess)."
}

$secureToken = Read-Host "Enter BRIDGE_TOKEN (at least 32 characters; input is hidden)" -AsSecureString
$ptr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secureToken)
try {
    $token = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($ptr)
} finally {
    [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($ptr)
}

if ($token.Length -lt 32) {
    $token = $null
    throw "BRIDGE_TOKEN must contain at least 32 characters."
}

$env:BRIDGE_HOST = $BridgeHost
$env:BRIDGE_PORT = $BridgePort
$env:WATCH_SERIAL = $WatchSerial
$env:BRIDGE_TOKEN = $token
$token = $null

Write-Host "Starting StopWatch debug bridge. Press Ctrl+C to stop." -ForegroundColor Green
try {
    & $Python $Script
} finally {
    Remove-Item Env:BRIDGE_TOKEN -ErrorAction SilentlyContinue
    Remove-Item Env:BRIDGE_HOST -ErrorAction SilentlyContinue
    Remove-Item Env:BRIDGE_PORT -ErrorAction SilentlyContinue
    Remove-Item Env:WATCH_SERIAL -ErrorAction SilentlyContinue
}
