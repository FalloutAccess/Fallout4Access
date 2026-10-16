#Requires -Version 5.1
<#
.SYNOPSIS
    Fail when a committed Papyrus .pex no longer matches its .psc source.

.DESCRIPTION
    The compiled scripts are committed rather than built in CI: the Papyrus
    compiler ships only with the Creation Kit and cannot be redistributed, and
    compiling with a different one (Caprica) would mean releasing bytes nobody
    tested. The cost is that a .psc can be edited and committed without
    recompiling, and the archive would then ship the old behaviour silently --
    no crash, just auto-walk or the combat sounds quietly not working.

    Two checks, because neither alone is enough:

    1. Every function and event the .psc declares must appear in the .pex string
       table. The plugin calls these scripts by name (DispatchStaticCall /
       DispatchMethodCall), so a function added or renamed without recompiling
       is the failure that matters most, and this catches it from content alone.

    2. The .pex must not be older than its .psc. On a clean checkout that means
       commit time, because git does not preserve file mtimes; for a file with
       uncommitted changes it means mtime, which is meaningful on the machine
       that just compiled it.

    It also rejects a .pex with no source, and a source with no .pex.

    Run it locally before committing script changes:
        pwsh tools/check-papyrus.ps1
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$RepoRoot = Split-Path -Parent $PSScriptRoot
$SourceRoot = 'Data/Scripts/Source/User'
$PexRoot = 'Data/Scripts'

function Get-TrackedFiles([string]$Path, [string]$Extension) {
    # Tracked files only, so the check sees what CI sees. A .pex that exists
    # here but was never `git add`ed would pass locally and be absent in CI.
    @(git -C $RepoRoot ls-files -- $Path | Where-Object { $_ -like "*$Extension" })
}

function Test-Dirty([string]$RelPath) {
    [bool](git -C $RepoRoot status --porcelain -- $RelPath)
}

function Get-CommitTime([string]$RelPath) {
    [long](git -C $RepoRoot log -1 --format=%ct -- $RelPath)
}

$sources = Get-TrackedFiles $SourceRoot '.psc'
$compiled = Get-TrackedFiles "$PexRoot/F4A" '.pex'
if ($sources.Count -eq 0) { throw "No tracked .psc files under $SourceRoot." }

$problems = New-Object System.Collections.Generic.List[string]
$expected = @{}

foreach ($psc in $sources) {
    # Data/Scripts/Source/User/F4A/AutoWalk.psc -> Data/Scripts/F4A/AutoWalk.pex
    $rel = $psc.Substring($SourceRoot.Length + 1)
    $pex = "$PexRoot/" + [System.IO.Path]::ChangeExtension($rel, '.pex')
    $expected[$pex] = $true

    if ($compiled -notcontains $pex) {
        $problems.Add("$psc has no tracked $pex (compile it, then git add it).")
        continue
    }

    # Check 1: every declared name is in the compiled string table. Papyrus
    # identifiers are case-insensitive, and the table stores them as ASCII.
    $text = Get-Content -LiteralPath (Join-Path $RepoRoot $psc) -Raw
    $declared = [regex]::Matches($text, '(?im)^\s*(?:\w+(?:\[\])?\s+)?(?:Function|Event)\s+(\w+)') |
                ForEach-Object { $_.Groups[1].Value }
    $bytes = [System.IO.File]::ReadAllBytes((Join-Path $RepoRoot $pex))
    $table = [System.Text.Encoding]::ASCII.GetString($bytes)
    foreach ($name in $declared) {
        if ($table.IndexOf($name, [System.StringComparison]::OrdinalIgnoreCase) -lt 0) {
            $problems.Add("$pex does not contain '$name', which $psc declares -- recompile it.")
        }
    }

    # Check 2: the compiled file is not older than its source.
    if ((Test-Dirty $psc) -or (Test-Dirty $pex)) {
        $srcTime = (Get-Item -LiteralPath (Join-Path $RepoRoot $psc)).LastWriteTimeUtc
        $pexTime = (Get-Item -LiteralPath (Join-Path $RepoRoot $pex)).LastWriteTimeUtc
        $basis = 'modified'
        $stale = $pexTime -lt $srcTime
    } else {
        $basis = 'committed'
        $stale = (Get-CommitTime $pex) -lt (Get-CommitTime $psc)
    }
    if ($stale) {
        $problems.Add("$pex was $basis before $psc -- recompile and commit it with the source.")
    } else {
        Write-Host ("  ok  {0}  ({1} declarations, compared by {2} time)" -f $pex, @($declared).Count, $basis)
    }
}

foreach ($pex in $compiled) {
    if (-not $expected.ContainsKey($pex)) {
        $problems.Add("$pex has no source under $SourceRoot; the archive would ship a script nobody can rebuild.")
    }
}

if ($problems.Count -gt 0) {
    foreach ($p in $problems) {
        if ($env:GITHUB_ACTIONS) { Write-Host "::error::$p" } else { Write-Host "  FAIL  $p" }
    }
    throw "$($problems.Count) Papyrus problem(s); see above."
}
Write-Host "$($sources.Count) Papyrus script(s) match their compiled .pex"
