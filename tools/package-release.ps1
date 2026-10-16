#Requires -Version 5.1
<#
.SYNOPSIS
    Assemble the distributable fallout4access mod archive.

.DESCRIPTION
    Builds the archive that gets attached to a GitHub release and uploaded to
    Nexus Mods. The zip is rooted at the game's Data folder -- F4SE\, Scripts\,
    MCM\ and fallout4access.esp sit at the top level with no wrapper directory --
    which is what Mod Organizer 2 and Vortex expect for a plain (non-FOMOD) mod.

    Data\ content comes from `git ls-files`, not from the directory, so the
    archive is exactly what a fresh clone holds. Packaging the working tree is
    how earlier releases shipped ~10 MB of unreferenced sound drafts and .bak
    files; the ESP only points at the four UI_Target*.wav cues.

    Run it locally to reproduce exactly what CI ships:
        xmake build
        pwsh tools/package-release.ps1

.PARAMETER Version
    Version embedded in the archive filename, e.g. "0.2.0". Defaults to
    F4A_VERSION in xmake.lua. A leading "v" is stripped.

.PARAMETER OutDir
    Where the staging tree and the finished zip are written. Relative paths are
    resolved against the repo root, not the caller's working directory.
#>
[CmdletBinding()]
param(
    [string]$Version = '',
    [string]$OutDir = 'dist'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$RepoRoot = Split-Path -Parent $PSScriptRoot

if (-not $Version) {
    $xmake = Get-Content -LiteralPath (Join-Path $RepoRoot 'xmake.lua') -Raw
    if ($xmake -notmatch 'local\s+F4A_VERSION\s*=\s*"([^"]+)"') { throw 'F4A_VERSION not found in xmake.lua.' }
    $Version = $Matches[1]
}
$Version = $Version -replace '^v', ''

if (-not [System.IO.Path]::IsPathRooted($OutDir)) {
    $OutDir = Join-Path $RepoRoot $OutDir
}
$Stage = Join-Path $OutDir 'stage'
$ZipName = "fallout4access-$Version.zip"
$ZipPath = Join-Path $OutDir $ZipName

function Assert-Exists([string]$Path, [string]$What) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Missing $What`: $Path"
    }
}

# Fallout 4 is 64-bit on every runtime we support. Check the PE header rather
# than trusting the build configuration. Machine lives 4 bytes past the PE
# signature, whose offset is the e_lfanew field at 0x3C of the DOS header.
function Assert-X64Dll([string]$Path) {
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
    if ([System.Text.Encoding]::ASCII.GetString($bytes, $peOffset, 2) -ne 'PE') {
        throw "Not a PE image: $Path"
    }
    $machine = [BitConverter]::ToUInt16($bytes, $peOffset + 4)
    if ($machine -ne 0x8664) {
        throw ("$Path is machine 0x{0:X4}, expected 0x8664 (x64)." -f $machine)
    }
    Write-Host "  verified x64 PE (machine 0x8664)"
}

Write-Host "Packaging fallout4access $Version"

# A stale .pex ships old script behaviour with no error anywhere, so this gates
# the archive rather than living only in CI.
& (Join-Path $PSScriptRoot 'check-papyrus.ps1')

$Dll = Join-Path $RepoRoot 'build\windows\x64\release\fallout4access.dll'
Assert-Exists $Dll 'built plugin DLL (run xmake build first)'
Assert-X64Dll $Dll

if (Test-Path -LiteralPath $Stage) { Remove-Item -LiteralPath $Stage -Recurse -Force }
New-Item -ItemType Directory -Path $Stage -Force | Out-Null

$pluginDir = Join-Path $Stage 'F4SE\Plugins'
New-Item -ItemType Directory -Path $pluginDir -Force | Out-Null
Copy-Item -LiteralPath $Dll -Destination $pluginDir
Write-Host "  1   plugin DLL -> F4SE\Plugins\"

# Data\X in the repo is X in the archive.
$dataFiles = @(git -C $RepoRoot ls-files -- Data)
if ($dataFiles.Count -eq 0) { throw 'git ls-files returned nothing under Data/.' }
foreach ($rel in $dataFiles) {
    $dest = Join-Path $Stage ($rel.Substring('Data/'.Length))
    New-Item -ItemType Directory -Path (Split-Path -Parent $dest) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $RepoRoot $rel) -Destination $dest
}
Write-Host ("  {0,-3} tracked Data files -> archive root" -f $dataFiles.Count)

# Each entry is a way the mod breaks without a load error: no ESP means no
# sound descriptors or auto-walk quest; no .pex means auto-walk and combat sounds
# do nothing; no MCM config means no keybinding page.
$Required = @(
    'fallout4access.esp'
    'Scripts\F4A\AutoWalk.pex'
    'Scripts\F4A\AutoWalkHelper.pex'
    'Scripts\F4A\SoundHelper.pex'
    'MCM\Config\fallout4access\config.json'
    'MCM\Config\fallout4access\settings.ini'
    'Interface\Translations\fallout4access_en.txt'
    'Sound\FX\F4A\UI\UI_TargetHit.wav'
    'Sound\FX\F4A\UI\UI_TargetHitCrit.wav'
    'Sound\FX\F4A\UI\UI_TargetHitKill.wav'
    'Sound\FX\F4A\UI\UI_TargetLOS.wav'
)
foreach ($r in $Required) { Assert-Exists (Join-Path $Stage $r) 'required mod file in the staged archive' }

# Documentation and licences sit in the archive root so they are visible in the
# mod folder. Prism is MPL-2.0 and statically linked, so shipping its licence and
# NOTICE is a legal obligation, not a courtesy -- the build must fail without them.
$Docs = @(
    @{ Source = 'CHANGELOG.md';      Dest = 'CHANGELOG.md' }
    @{ Source = 'README.md';         Dest = 'README.md' }
    @{ Source = 'LICENSE';           Dest = 'LICENSE.txt' }
    @{ Source = 'EXCEPTIONS';        Dest = 'EXCEPTIONS.txt' }
    @{ Source = 'lib\prism\LICENSE'; Dest = 'PRISM-LICENSE.txt' }
    @{ Source = 'lib\prism\NOTICE';  Dest = 'PRISM-NOTICE.txt' }
)
foreach ($doc in $Docs) {
    $src = Join-Path $RepoRoot $doc.Source
    Assert-Exists $src "documentation/licence file (is the prism submodule checked out?)"
    Copy-Item -LiteralPath $src -Destination (Join-Path $Stage $doc.Dest)
}
Write-Host ("  {0,-3} documentation and licence files -> archive root" -f $Docs.Count)

# Prism's NOTICE points at its LICENSES directory for the third-party terms it
# statically pulls in, so that tree ships whole rather than as a summary.
$PrismLicenses = Join-Path $RepoRoot 'lib\prism\LICENSES'
Assert-Exists $PrismLicenses "Prism third-party licences (git submodule update --init)"
Copy-Item -LiteralPath $PrismLicenses -Destination (Join-Path $Stage 'PRISM-LICENSES') -Recurse
$licenseCount = @(Get-ChildItem -Path (Join-Path $Stage 'PRISM-LICENSES') -Recurse -File).Count
if ($licenseCount -eq 0) { throw 'PRISM-LICENSES is empty -- refusing to ship without third-party licences.' }
Write-Host ("  {0,-3} Prism third-party licences -> PRISM-LICENSES\" -f $licenseCount)

if (Test-Path -LiteralPath $ZipPath) { Remove-Item -LiteralPath $ZipPath -Force }
Add-Type -AssemblyName System.IO.Compression.FileSystem
# includeBaseDirectory:$false keeps F4SE\ and the ESP at the zip root.
[System.IO.Compression.ZipFile]::CreateFromDirectory(
    $Stage, $ZipPath, [System.IO.Compression.CompressionLevel]::Optimal, $false)

$sha = [System.Security.Cryptography.SHA256]::Create()
try {
    $fs = [System.IO.File]::OpenRead($ZipPath)
    try   { $hash = ($sha.ComputeHash($fs) | ForEach-Object { $_.ToString('x2') }) -join '' }
    finally { $fs.Dispose() }
} finally { $sha.Dispose() }

$sizeMB = [math]::Round((New-Object System.IO.FileInfo $ZipPath).Length / 1MB, 2)
# Explicit LF: `sha256sum -c` treats a trailing CR as part of the filename.
[System.IO.File]::WriteAllText("$ZipPath.sha256", "$hash  $ZipName`n",
                               (New-Object System.Text.ASCIIEncoding))

Write-Host ""
Write-Host "$ZipName  ($sizeMB MB)"
Write-Host "sha256: $hash"

if ($env:GITHUB_OUTPUT) {
    "zip-path=$ZipPath" | Add-Content -LiteralPath $env:GITHUB_OUTPUT
    "zip-name=$ZipName" | Add-Content -LiteralPath $env:GITHUB_OUTPUT
    "sha256=$hash"      | Add-Content -LiteralPath $env:GITHUB_OUTPUT
}
