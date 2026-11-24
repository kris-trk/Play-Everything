param(
    [string]$OutDir = (Join-Path (Split-Path -Parent $PSScriptRoot) "third_party\libmpv")
)

$ErrorActionPreference = "Stop"

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$dllPath = Join-Path $OutDir "libmpv-2.dll"
$headerPath = Join-Path $OutDir "include\mpv\client.h"
$libPath = Join-Path $OutDir "mpv.lib"

if ((Test-Path $dllPath) -and (Test-Path $headerPath) -and (Test-Path $libPath)) {
    Write-Host "libmpv is already set up in $OutDir"
    exit 0
}

if (-not (Test-Path $dllPath) -or -not (Test-Path $headerPath)) {
    Write-Host "Querying latest shinchiro/mpv-winbuild-cmake release..."
    $headers = @{ "User-Agent" = "PlayEverythingSetup/1.0" }
    $release = Invoke-RestMethod -Uri "https://api.github.com/repos/shinchiro/mpv-winbuild-cmake/releases/latest" -Headers $headers

    $asset = $release.assets | Where-Object {
        $_.name -like "mpv-dev-x86_64-*.7z" -and $_.name -notlike "*-v3-*"
    } | Select-Object -First 1

    if (-not $asset) {
        throw "Could not find mpv-dev-x86_64-*.7z asset in latest release."
    }

    $archivePath = Join-Path $OutDir $asset.name
    if (-not (Test-Path $archivePath)) {
        Write-Host "Downloading $($asset.name) ($([math]::Round($asset.size / 1MB, 1)) MB)..."
        Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $archivePath -Headers $headers
    }

    Write-Host "Extracting $($asset.name) with tar..."
    & tar -xf $archivePath -C $OutDir
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to extract $archivePath"
    }

    Remove-Item -Force $archivePath -ErrorAction SilentlyContinue
}

if (-not (Test-Path $dllPath)) {
    throw "Expected $dllPath after extraction."
}
if (-not (Test-Path $headerPath)) {
    throw "Expected $headerPath after extraction."
}

# Locate MSVC lib.exe and dumpbin.exe via vswhere
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$libExe = $null
$dumpbinExe = $null
if (Test-Path $vswhere) {
    $libExe = & $vswhere -all -products * -find "VC\Tools\MSVC\**\bin\Hostx64\x64\lib.exe" | Select-Object -First 1
    $dumpbinExe = & $vswhere -all -products * -find "VC\Tools\MSVC\**\bin\Hostx64\x64\dumpbin.exe" | Select-Object -First 1
}
if (-not $libExe) {
    $cmd = Get-Command lib.exe -ErrorAction SilentlyContinue
    if ($cmd) { $libExe = $cmd.Source }
}
if (-not $dumpbinExe) {
    $cmd = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
    if ($cmd) { $dumpbinExe = $cmd.Source }
}
if (-not $libExe -or -not $dumpbinExe) {
    throw "Could not locate MSVC lib.exe / dumpbin.exe to generate mpv.lib"
}

$defPath = Join-Path $OutDir "mpv.def"
if (-not (Test-Path $defPath)) {
    Write-Host "Generating mpv.def from $dllPath exports..."
    $dumpOut = & $dumpbinExe /nologo /exports $dllPath
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin.exe failed on $dllPath"
    }
    $exports = New-Object System.Collections.Generic.List[string]
    foreach ($line in $dumpOut) {
        if ($line -match '^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(mpv_[A-Za-z0-9_]+)\s*$') {
            $exports.Add($Matches[1])
        }
    }
    if ($exports.Count -lt 10) {
        throw "Found too few mpv_* exports ($($exports.Count)) in $dllPath"
    }
    $defLines = @("LIBRARY libmpv-2.dll", "EXPORTS") + ($exports | ForEach-Object { "    $_" })
    Set-Content -Path $defPath -Value $defLines -Encoding ASCII
    Write-Host "Wrote $($exports.Count) exports to $defPath"
}

Write-Host "Generating mpv.lib using $libExe..."
& $libExe "/nologo" "/def:$defPath" "/machine:x64" "/out:$libPath"
if ($LASTEXITCODE -ne 0) {
    throw "lib.exe failed to generate mpv.lib"
}

Write-Host "Successfully prepared libmpv in $OutDir"
Get-ChildItem $OutDir | Select-Object Name, Length | Format-Table -AutoSize
