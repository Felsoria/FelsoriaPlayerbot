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
    $log = Join-Path (Join-Path $WorkingDirectory 'logs') $LogName
    if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log }

    # Keep the native process handle so a short-lived child's exit code is available in Windows PowerShell 5.1.
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Executable
    $start.WorkingDirectory = $WorkingDirectory
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = [Diagnostics.Process]::Start($start)
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    if (!$process.WaitForExit(30000)) {
        throw "Server still running after 30 seconds while testing: $ExpectedError"
    }
    $exitCode = $process.ExitCode
    $messages = @($stdout.GetAwaiter().GetResult(), $stderr.GetAwaiter().GetResult())
    if (Test-Path -LiteralPath $log) { $messages += Get-Content -LiteralPath $log -Raw }
    $output = $messages -join [Environment]::NewLine
    if ($exitCode -ne 1 -or !$output.Contains($ExpectedError)) {
        throw "Expected exit code 1 and '$ExpectedError'. Actual exit code: $exitCode. Server output:`n$output"
    }
} finally {
    if ($process) {
        try {
            if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
        } finally {
            $process.Dispose()
        }
    }
    if ($holder) { $holder.Stop() }
}
