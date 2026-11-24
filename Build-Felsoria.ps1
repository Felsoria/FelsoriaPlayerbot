#Requires -Version 5.1
[CmdletBinding()]
param(
    [string]$BoostRoot = $env:BOOST_ROOT,
    [string]$BoostLibraryDir = $env:BOOST_LIBRARYDIR,
    [string]$MySqlRoot = $env:MYSQL_ROOT,
    [string]$OpenSslRoot = $env:OPENSSL_ROOT_DIR,
    [string]$InstallPrefix,
    [switch]$ConfigureOnly
)

$ErrorActionPreference = 'Stop'
$preset = 'developer-v2'

function Resolve-DependencyDirectory {
    param([string]$Path, [string[]]$DefaultPaths, [string]$ParameterName)
    if (!$Path) {
        $Path = $DefaultPaths | Where-Object { Test-Path -LiteralPath $_ -PathType Container } | Select-Object -First 1
    }
    if (!$Path -or !(Test-Path -LiteralPath $Path -PathType Container)) {
        throw "Dependency directory not found. Pass -$ParameterName with its full path."
    }
    (Get-Item -LiteralPath $Path).FullName
}

$BoostRoot = Resolve-DependencyDirectory $BoostRoot @('D:\Dependencies\boost_1_78_0') 'BoostRoot'
$MySqlRoot = Resolve-DependencyDirectory $MySqlRoot @('D:\Dependencies\mysql-5.7.44-winx64') 'MySqlRoot'
$OpenSslRoot = Resolve-DependencyDirectory $OpenSslRoot @('D:\OpenSSL-Win64', "$env:ProgramFiles\OpenSSL-Win64", "$env:ProgramFiles\OpenSSL") 'OpenSslRoot'

if ((Get-Content -LiteralPath (Join-Path $BoostRoot 'boost\version.hpp') -Raw) -notmatch '(?m)^\s*#\s*define\s+BOOST_VERSION\s+107800\b') {
    throw "Expected Boost 1.78.0 headers at $BoostRoot."
}
$BoostLibraryDir = Resolve-DependencyDirectory $BoostLibraryDir @(
    (Join-Path $BoostRoot 'boost\boost_1_78_0\lib64-msvc-14.3'),
    (Join-Path $BoostRoot 'lib64-msvc-14.3'),
    (Join-Path $BoostRoot 'stage\lib')
) 'BoostLibraryDir'
foreach ($component in 'system', 'filesystem', 'thread', 'program_options', 'iostreams', 'regex') {
    if (!(Get-ChildItem -LiteralPath $BoostLibraryDir -Filter "libboost_$component-*.lib" -File)) {
        throw "Missing static Boost $component library at $BoostLibraryDir. Build the x64 v143 libraries or pass -BoostLibraryDir."
    }
}

$mysqlInclude = Join-Path $MySqlRoot 'include'
$mysqlLibrary = Join-Path $MySqlRoot 'lib\libmysql.lib'
if (!(Test-Path -LiteralPath (Join-Path $mysqlInclude 'mysql.h') -PathType Leaf) -or
    !(Test-Path -LiteralPath $mysqlLibrary -PathType Leaf)) {
    throw "Expected MySQL C headers and libmysql.lib at $MySqlRoot."
}
if ((Get-Content -LiteralPath (Join-Path $mysqlInclude 'mysql_version.h') -Raw) -notmatch '(?m)^\s*#\s*define\s+MYSQL_VERSION_ID\s+50744\b') {
    throw "Expected MySQL 5.7.44 headers at $MySqlRoot."
}

$sslHeader = Join-Path $OpenSslRoot 'include\openssl\opensslv.h'
if ((Get-Content -LiteralPath $sslHeader -Raw) -notmatch '(?m)^\s*#\s*define\s+OPENSSL_VERSION_STR\s+"3\.5\.1"') {
    throw "Expected OpenSSL 3.5.1 headers at $OpenSslRoot."
}
$runtime = @(
    (Join-Path $MySqlRoot 'lib\libmysql.dll'),
    (Join-Path $OpenSslRoot 'bin\libssl-3-x64.dll'),
    (Join-Path $OpenSslRoot 'bin\libcrypto-3-x64.dll')
)
foreach ($dll in $runtime) {
    if (!(Test-Path -LiteralPath $dll -PathType Leaf)) {
        throw "Missing runtime file: $dll"
    }
}

$cmake = (Get-Command cmake.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1).Source
if (!$cmake) {
    $programFilesX86 = [Environment]::GetFolderPath('ProgramFilesX86')
    $vswhere = Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $cmake = & $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' | Select-Object -First 1
    }
}
if (!$cmake) {
    throw 'CMake 3.31 was not found. Install CMake or add the Visual Studio CMake tools.'
}

$configure = @(
    '--preset', $preset, '-UOPENSSL_*', '-U_OPENSSL_*', '-UBoost_*', '-U_Boost_*',
    "-DBOOST_ROOT:PATH=$BoostRoot", "-DBOOST_LIBRARYDIR:PATH=$BoostLibraryDir",
    "-DOPENSSL_ROOT_DIR:PATH=$OpenSslRoot",
    "-DMYSQL_INCLUDE_DIR:PATH=$mysqlInclude", "-DMYSQL_LIBRARY:FILEPATH=$mysqlLibrary"
)
if ($InstallPrefix) {
    $configure += "-DCMAKE_INSTALL_PREFIX:PATH=$InstallPrefix"
}

Push-Location $PSScriptRoot
try {
    & $cmake @configure
    if ($LASTEXITCODE -ne 0) { throw 'Felsoria CMake configuration failed.' }
    if (!$ConfigureOnly) {
        & $cmake --build --preset $preset
        if ($LASTEXITCODE -ne 0) { throw 'Felsoria build failed.' }
        $output = Join-Path $PSScriptRoot 'build\developer-v2\bin\RelWithDebInfo'
        Copy-Item -LiteralPath $runtime -Destination $output -Force
        Write-Host "Developer V2 build ready: $output"
    }
} finally {
    Pop-Location
}
