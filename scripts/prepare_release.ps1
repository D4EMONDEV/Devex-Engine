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

# The log of the CI asks to sign in: an annotation says why to anyone.
trap {
    if ($env:GITHUB_ACTIONS) {
        Write-Host "::error::$_"
    }
    break
}

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

# Each system's editor exports for the other with its engine, which comes along: only what a game takes.
function Copy-GameEngine([string] $From, [string] $To) {
    New-Item -ItemType Directory -Force (Join-Path $To "bin") | Out-Null
    Get-ChildItem (Join-Path $From "bin") -File | Where-Object { $_.Name -notlike "libcurl*" -and ($_.Name -in "devex-player", "devex-player.exe" -or $_.Extension -in ".dll", ".so") } |
        ForEach-Object { Copy-Item $_.FullName (Join-Path $To "bin") }
    foreach ($folder in "bin/managed", "bin/shaders", "bin/redist", "cmake") {
        $source = Join-Path $From $folder
        if (Test-Path $source) {
            Copy-Item -Recurse -Force $source (Join-Path $To $folder)
        }
    }
    $library = Join-Path $From "lib/libdevex-engine.so"
    if (Test-Path $library) {
        New-Item -ItemType Directory -Force (Join-Path $To "lib") | Out-Null
        Copy-Item $library (Join-Path $To "lib")
    }
}
if ($Linux) {
    # The tar of the system, which reads a whole archive with @ and keeps what its entries say is executable.
    $tar = Join-Path $env:SystemRoot "System32/tar.exe"
    $work = Join-Path $Output "other-engines"
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
    $forLinux = Join-Path $work "windows"
    # First, while the Windows engine does not hold the one for Linux yet.
    Copy-GameEngine $Windows (Join-Path $forLinux "devex-linux-x64/platforms/windows-x64")
    $extracted = Join-Path $work "linux"
    New-Item -ItemType Directory -Force $extracted | Out-Null
    & $tar -xzf $Linux -C $extracted
    if ($LASTEXITCODE -ne 0) {
        throw "The Linux archive does not extract"
    }
    Remove-Item -Recurse -Force (Join-Path $Windows "platforms") -ErrorAction SilentlyContinue
    Copy-GameEngine (Join-Path $extracted "devex-linux-x64") (Join-Path $Windows "platforms/linux-x64")
    foreach ($needed in (Join-Path $Windows "platforms/linux-x64/bin/devex-player"), (Join-Path $Windows "platforms/linux-x64/lib/libdevex-engine.so"),
                        (Join-Path $forLinux "devex-linux-x64/platforms/windows-x64/bin/devex-player.exe")) {
        if (-not (Test-Path $needed)) {
            throw "$needed is missing from the engine for the other system"
        }
    }
    $linuxArchive = Join-Path $Output "devex-$Version-linux-x64.tar.gz"
    # In this format: left to itself, this tar adds headers that the tar of Linux warns about, file after file.
    & $tar --format=gnutar -czf $linuxArchive "@$Linux" -C $forLinux "devex-linux-x64/platforms"
    if ($LASTEXITCODE -ne 0) {
        throw "The Linux archive did not take the engine for Windows"
    }
    Remove-Item -Recurse -Force $work
}

$windowsZip = Join-Path $Output "devex-$Version-windows-x64.zip"
Remove-Item -Force $windowsZip -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $Windows "*") -DestinationPath $windowsZip -CompressionLevel Optimal

& $Iscc /Q "/DAppVersion=$Version" "/DSource=$((Resolve-Path $Windows).Path)" "/DOutputFolder=$Output" (Join-Path $root "packaging/windows/devex.iss")
if ($LASTEXITCODE -ne 0) {
    throw "The installer did not build"
}

$files = @($windowsZip, (Join-Path $Output "devex-$Version-windows-x64-setup.exe"))
if ($Linux) {
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
    # Before 1.0, a release says it is a preview. An if alone gives back a string, which @preview would spell letter by letter.
    $preview = @(if ([version]$Version -lt [version]"1.0.0") { "--prerelease" })
    $created = & gh release create "v$Version" --draft --verify-tag --title "Devex $Version" --notes-file $notes @preview @files 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "The release was not created: $created"
    }
    "Draft: $created"
}
