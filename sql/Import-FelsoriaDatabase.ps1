#Requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$MySqlExecutable,
    [Parameter(Mandatory)][ValidateRange(1,65535)][int]$Port,
    [Parameter(Mandatory)][string]$DataDirectory,
    [Parameter(Mandatory)][string]$SqlZip,
    [Parameter(Mandatory)][ValidatePattern('\A[a-fA-F0-9]{64}\z')][string]$Sha256,
    [Parameter(Mandatory)][ValidatePattern('\A[a-fA-F0-9]{40}\z')][string]$SourceCommit,
    [ValidateNotNullOrEmpty()][string]$User = 'root',
    [switch]$SkipPassword
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$mysql = Get-Item -LiteralPath $MySqlExecutable
$data = Get-Item -LiteralPath $DataDirectory
$zip = Get-Item -LiteralPath $SqlZip
if ($mysql.PSIsContainer -or $mysql.Name -ine 'mysql.exe' -or !$data.PSIsContainer -or $zip.PSIsContainer) {
    throw 'Specify mysql.exe, the dedicated MySQL data directory, and the generated SQL ZIP.'
}
if ((Get-FileHash -LiteralPath $zip.FullName -Algorithm SHA256).Hash -ine $Sha256) {
    throw 'The SQL ZIP does not match the published SHA-256.'
}

$schemas = 'auth', 'characters', 'world', 'hotfixes'
$temporary = Join-Path $zip.DirectoryName ('felsoria-sql-import-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporary | Out-Null
try {
    Expand-Archive -LiteralPath $zip.FullName -DestinationPath $temporary
    $manifest = Get-Content -LiteralPath (Join-Path $temporary 'database-manifest.json') -Raw | ConvertFrom-Json
    if ($manifest.source_commit -ine $SourceCommit) {
        throw 'The SQL ZIP was generated for a different source commit.'
    }
    foreach ($schema in $schemas) {
        if (!(Test-Path -LiteralPath (Join-Path $temporary "$schema.sql") -PathType Leaf)) {
            throw "The SQL ZIP is missing $schema.sql."
        }
    }

    $expectedDirectory = $data.FullName.Replace('\', '/').TrimEnd('/')
    $directoryHex = [BitConverter]::ToString([Text.Encoding]::UTF8.GetBytes($expectedDirectory)).Replace('-', '')

    # A failed guard aborts the noninteractive client before any CREATE or dump statement.
    # The connection keeps the advisory lock until the client exits.
    $commands = @"
SELECT IF(GET_LOCK('felsoria.fresh_database_import', 0) = 1,
  'SELECT 1 INTO @verified', 'SELECT FELSORIA_ANOTHER_IMPORT_IS_RUNNING') INTO @guard;
PREPARE import_guard FROM @guard;
EXECUTE import_guard;
DEALLOCATE PREPARE import_guard;
SELECT IF(@@port = $Port AND @@version = '5.7.44' AND
  LOWER(TRIM(TRAILING '/' FROM REPLACE(@@datadir, CHAR(92), '/'))) =
  LOWER(CONVERT(0x$directoryHex USING utf8mb4)),
  'SELECT 1 INTO @verified', 'SELECT FELSORIA_WRONG_MYSQL_PORT_VERSION_OR_DATA_DIRECTORY') INTO @guard;
PREPARE import_guard FROM @guard;
EXECUTE import_guard;
DEALLOCATE PREPARE import_guard;
SELECT IF(COUNT(*) = 0, 'SELECT 1 INTO @verified', 'SELECT FELSORIA_TARGET_DATABASES_ALREADY_EXIST') INTO @guard
  FROM information_schema.SCHEMATA WHERE LOWER(SCHEMA_NAME) IN ('auth', 'characters', 'world', 'hotfixes');
PREPARE import_guard FROM @guard;
EXECUTE import_guard;
DEALLOCATE PREPARE import_guard;
SELECT @@datadir AS verified_datadir, @@port AS verified_port;
"@
    foreach ($schema in $schemas) {
        $commands += " CREATE DATABASE $schema CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;" + [Environment]::NewLine
    }

    # Feed the entire import to the client as batch input. Errors in SOURCE files
    # can be ignored by some mysql-compatible clients even in batch mode.
    $userArg = ($User -replace '(\\*)"', '$1$1\"') -replace '(\\+)$', '$1$1'
    $client = New-Object Diagnostics.ProcessStartInfo
    $client.FileName = $mysql.FullName
    $client.Arguments = '--no-defaults --protocol=TCP --host=127.0.0.1 --port={0} --user="{1}" --batch --binary-mode --unbuffered --skip-reconnect --default-character-set=utf8mb4' -f $Port, $userArg
    $client.UseShellExecute = $false
    $client.RedirectStandardInput = $true
    $client.EnvironmentVariables.Remove('MYSQL_PWD')
    if ($SkipPassword) {
        $client.Arguments += ' --skip-password'
    } else {
        $password = Read-Host -Prompt "MySQL password for $User at 127.0.0.1 port $Port" -AsSecureString
        $encodedPassword = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($password)
        try {
            $client.EnvironmentVariables['MYSQL_PWD'] = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($encodedPassword)
        } finally {
            [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($encodedPassword)
            $password.Dispose()
        }
    }

    $process = $null
    try {
        try {
            $process = [Diagnostics.Process]::Start($client)
        } finally {
            $client.EnvironmentVariables.Remove('MYSQL_PWD')
        }
        $stdin = $process.StandardInput.BaseStream
        $writeError = $null
        try {
            $bytes = [Text.Encoding]::UTF8.GetBytes($commands)
            $stdin.Write($bytes, 0, $bytes.Length)
            foreach ($schema in $schemas) {
                $header = [Text.Encoding]::UTF8.GetBytes("USE $schema; SELECT 'Import: $schema.sql' AS progress;" + [Environment]::NewLine)
                $stdin.Write($header, 0, $header.Length)
                $dumpStream = [IO.File]::OpenRead((Join-Path $temporary "$schema.sql"))
                try {
                    $dumpStream.CopyTo($stdin)
                    $stdin.WriteByte(10)
                } finally {
                    $dumpStream.Dispose()
                }
            }
        } catch [IO.IOException] {
            $writeError = $_
        } finally {
            $stdin.Dispose()
        }
        $process.WaitForExit()
        if ($process.ExitCode -ne 0) {
            throw 'The import stopped. See the MySQL error above. A partial import requires a fresh test instance before retrying.'
        }
        if ($writeError) {
            throw $writeError
        }
    } finally {
        if ($process) { $process.Dispose() }
    }
    Write-Host 'Imported auth, characters, world, and hotfixes.'
} finally {
    Remove-Item -LiteralPath $temporary -Recurse -Force
}
