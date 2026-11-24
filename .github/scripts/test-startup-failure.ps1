param(
    [Parameter(Mandatory)][string]$Executable,
    [Parameter(Mandatory)][string]$WorkingDirectory,
    [Parameter(Mandatory)][ValidateRange(0, 65535)][int]$OccupiedPort,
    [Parameter(Mandatory)][string]$ExpectedError,
    [Parameter(Mandatory)][string]$LogName
)

$ErrorActionPreference = 'Stop'
$holder = $null
$process = $null
try {
    if ($OccupiedPort) {
        $holder = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, $OccupiedPort)
        $holder.ExclusiveAddressUse = $true
        $holder.Start()
    }
    $stdout = Join-Path $WorkingDirectory "startup-$OccupiedPort.out"
    $stderr = Join-Path $WorkingDirectory "startup-$OccupiedPort.err"
    $log = Join-Path (Join-Path $WorkingDirectory 'logs') $LogName
    if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log }
    $process = Start-Process -FilePath $Executable -WorkingDirectory $WorkingDirectory -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    if (!$process.WaitForExit(30000) -or $process.ExitCode -ne 1) {
        throw "Expected exit code 1 while testing: $ExpectedError"
    }
    $messages = Get-Content -LiteralPath $stdout,$stderr,$log -ErrorAction SilentlyContinue
    if (!($messages | Select-String -SimpleMatch $ExpectedError)) {
        throw "Missing startup diagnostic ($ExpectedError): $($messages -join [Environment]::NewLine)"
    }
} finally {
    if ($process -and !$process.HasExited) { Stop-Process -InputObject $process -Force }
    if ($holder) { $holder.Stop() }
}
