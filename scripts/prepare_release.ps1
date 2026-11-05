# Prepares the files of a release from the engine installed by `cmake --install`, and with -Draft makes the release on
# GitHub as a draft, to read and publish by hand. The CI runs it for a tag v<version>; see docs/publier-une-version.md.
#
#   scripts/prepare_release.ps1 -Version 0.9.0 -Windows <installed folder> [-Linux <devex-linux-x64.tar.gz>]
#                               -Output <folder> [-Iscc <ISCC.exe>] [-Draft]
param(
    [Parameter(Mandatory)] [string] $Version,
    [Parameter(Mandatory)] [string] $Windows,
    [string] $Linux,
    [Parameter(Mandatory)] [string] $Output,
    [string] $Iscc = "ISCC.exe",
    [switch] $Draft
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

# A tag that does not say what the engine says would make a release that lies about its version.
$project = Select-String -Path (Join-Path $root "CMakeLists.txt") -Pattern '^\s*VERSION\s+(\d+\.\d+\.\d+)' | Select-Object -First 1
if ($project.Matches[0].Groups[1].Value -ne $Version) {
    throw "CMakeLists.txt says version $($project.Matches[0].Groups[1].Value), not $Version"
}
$sandbox = Get-Content -Raw (Join-Path $root "samples/sandbox/Sandbox.dvxproj")
if ($sandbox -notmatch "engine=`"$([regex]::Escape($Version))`"") {
    throw "samples/sandbox/Sandbox.dvxproj does not say engine=`"$Version`""
}
if (-not (Test-Path (Join-Path $root "tests/data/formats/$Version"))) {
    throw "tests/data/formats/$Version is missing: see docs/compatibilite.md"
}

# The notes are what the changelog says of this version.
$changelog = Get-Content -Raw (Join-Path $root "CHANGELOG.md")
$section = [regex]::Match(($changelog -replace "`r`n", "`n"), "(?ms)^## $([regex]::Escape($Version))\b[^`n]*`n(.*?)(?=^## |\z)")
if (-not $section.Success) {
    throw "CHANGELOG.md has no section for $Version"
}

New-Item -ItemType Directory -Force $Output | Out-Null
$Output = (Resolve-Path $Output).Path
$notes = Join-Path $Output "notes.md"
[IO.File]::WriteAllText($notes, $section.Groups[1].Value.Trim())

$windowsZip = Join-Path $Output "devex-$Version-windows-x64.zip"
Remove-Item -Force $windowsZip -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $Windows "*") -DestinationPath $windowsZip -CompressionLevel Optimal

& $Iscc /Q "/DAppVersion=$Version" "/DSource=$((Resolve-Path $Windows).Path)" "/DOutputFolder=$Output" (Join-Path $root "packaging/windows/devex.iss")
if ($LASTEXITCODE -ne 0) {
    throw "The installer did not build"
}

$files = @($windowsZip, (Join-Path $Output "devex-$Version-windows-x64-setup.exe"))
if ($Linux) {
    $linuxArchive = Join-Path $Output "devex-$Version-linux-x64.tar.gz"
    Copy-Item -Force $Linux $linuxArchive
    $files += $linuxArchive
}

$sums = Join-Path $Output "SHA256SUMS.txt"
[IO.File]::WriteAllLines($sums, [string[]]($files | ForEach-Object {
    "$((Get-FileHash -Algorithm SHA256 $_).Hash.ToLowerInvariant())  $(Split-Path -Leaf $_)"
}))
$files += $sums

foreach ($file in $files) {
    "{0,8:N1} MB  {1}" -f ((Get-Item $file).Length / 1MB), (Split-Path -Leaf $file)
}

if ($Draft) {
    # Before 1.0, a release says it is a preview.
    $preview = if ([version]$Version -lt [version]"1.0.0") { @("--prerelease") } else { @() }
    & gh release create "v$Version" --draft --verify-tag --title "Devex $Version" --notes-file $notes @preview @files
    if ($LASTEXITCODE -ne 0) {
        throw "The release was not created"
    }
}
