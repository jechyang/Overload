[CmdletBinding()]
param(
    [string]$Destination,
    [switch]$ShowPaths
)
$ErrorActionPreference = 'Stop'
$scriptDirectory = $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($scriptDirectory)) {
    if ($MyInvocation.MyCommand.Path) {
        $scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
    } else {
        throw 'Run the saved .ps1 file with powershell -File; do not paste the script body.'
    }
}
if ([string]::IsNullOrWhiteSpace($Destination)) {
    $Destination = Join-Path $scriptDirectory '../../Build/Toolchains'
}
$repoRoot = [IO.Path]::GetFullPath((Join-Path $scriptDirectory '../..'))
$lock = Get-Content -LiteralPath (Join-Path $repoRoot 'Tools/OvReflectGen/toolchain.lock.json') -Raw | ConvertFrom-Json
$destinationPath = [IO.Path]::GetFullPath($Destination)
if ($ShowPaths) {
    Write-Host ('Repository: ' + $repoRoot)
    Write-Host ('Destination: ' + $destinationPath)
    Write-Host ('URL: ' + $lock.download_url)
    exit 0
}
New-Item -ItemType Directory -Force -Path $destinationPath | Out-Null
$archive = Join-Path $destinationPath $lock.archive
$partial = $archive + '.partial'
$install = Join-Path $destinationPath ('llvm-' + $lock.llvm_version)
$required = @('bin/clang.exe', 'include/clang/Tooling/Tooling.h', 'lib/clangTooling.lib', 'lib/cmake/llvm/LLVMConfig.cmake', 'lib/cmake/clang/ClangConfig.cmake')
if (Test-Path -LiteralPath (Join-Path $install '.reflection-toolchain-complete')) {
    foreach ($item in $required) {
        if (!(Test-Path -LiteralPath (Join-Path $install $item))) { throw "Incomplete LLVM package: $item" }
    }
    Write-Host "LLVM is ready: $install"
    exit 0
}
if (!(Test-Path -LiteralPath $archive)) {
    Write-Host ('Downloading official LLVM release: ' + $lock.download_url)
    Write-Host 'The full development archive is large. Downloads resume after interruption.'
    & curl.exe --fail --location --retry 5 --retry-delay 3 --continue-at - --output $partial $lock.download_url
    if ($LASTEXITCODE -ne 0) { throw 'Download failed. Run this script again to resume.' }
    Move-Item -LiteralPath $partial -Destination $archive
}
$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Host "Downloaded archive SHA256 (local receipt): $hash"
Write-Host "Extracting to $install"
New-Item -ItemType Directory -Force -Path $install | Out-Null
& tar.exe -xf $archive -C $install --strip-components 1
if ($LASTEXITCODE -ne 0) { throw 'Extraction failed; keep the archive and inspect available disk space.' }
foreach ($item in $required) {
    if (!(Test-Path -LiteralPath (Join-Path $install $item))) {
        throw "The archive is missing a required development file: $item"
    }
}
$version = (& (Join-Path $install 'bin/clang.exe') --version | Out-String)
if ($LASTEXITCODE -ne 0 -or $version -notmatch ('clang version ' + [regex]::Escape($lock.llvm_version) + '\b')) {
    throw 'Unexpected clang version in extracted package.'
}
@{ version = $lock.llvm_version; url = $lock.download_url; sha256 = $hash } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $install '.reflection-toolchain-complete') -Encoding UTF8
Write-Host "LLVM development package is ready: $install"
Write-Host 'Copy this path back to the Codex task to continue the LibTooling build.'
