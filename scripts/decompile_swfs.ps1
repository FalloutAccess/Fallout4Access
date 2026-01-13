param(
    [Parameter(Mandatory=$true)]
    [string]$InterfaceDir,

    [Parameter(Mandatory=$false)]
    [string]$FFDecPath = "ffdec-cli.exe"
)

# Verify paths
if (-not (Test-Path $InterfaceDir)) {
    Write-Error "Interface directory not found: $InterfaceDir"
    exit 1
}

if (-not (Get-Command $FFDecPath -ErrorAction SilentlyContinue)) {
    if (-not (Test-Path $FFDecPath)) {
        Write-Error "FFDEC CLI not found: $FFDecPath"
        Write-Host "Install JPEXS Free Flash Decompiler and provide path via -FFDecPath"
        exit 1
    }
}

# Skip directories that only contain graphics/animations without scripts
$skipDirs = @(
    "Components"
)

$swfFiles = Get-ChildItem -Path $InterfaceDir -Filter "*.swf" -Recurse | Where-Object {
    $skip = $false
    foreach ($dir in $skipDirs) {
        if ($_.FullName -like "*\$dir\*") {
            $skip = $true
            break
        }
    }
    -not $skip
}

Write-Host "Total SWF files to process: $($swfFiles.Count) (excluding graphics-only directories)"

$count = 0
$extracted = 0
foreach ($swf in $swfFiles) {
    $count++
    $outputDir = Join-Path $swf.DirectoryName ($swf.BaseName + "-extracted")

    if (Test-Path $outputDir) {
        Write-Host "[$count/$($swfFiles.Count)] Skipping (already extracted): $($swf.Name)"
        continue
    }

    Write-Host "[$count/$($swfFiles.Count)] Extracting: $($swf.FullName)"
    & $FFDecPath -export script $outputDir $swf.FullName 2>&1 | Out-Null
    $extracted++
}

Write-Host "Done! Extracted scripts from $extracted SWF files."
