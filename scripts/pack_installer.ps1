param(
    [Parameter(Mandatory=$true)][string]$StubExe,
    [Parameter(Mandatory=$true)][string]$AppExe,
    [Parameter(Mandatory=$true)][string]$MpvDll,
    [Parameter(Mandatory=$true)][string]$UninstallExe,
    [Parameter(Mandatory=$true)][string]$OutputExe
)

$ErrorActionPreference = "Stop"

$stageDir = Join-Path ([System.IO.Path]::GetTempPath()) ("PE_Stage_" + [guid]::NewGuid().ToString("N"))
$zipPath = Join-Path ([System.IO.Path]::GetTempPath()) ("PE_Payload_" + [guid]::NewGuid().ToString("N") + ".zip")

try {
    New-Item -ItemType Directory -Force -Path $stageDir | Out-Null
    Copy-Item -Force $AppExe (Join-Path $stageDir "PlayEverything.exe")
    Copy-Item -Force $MpvDll (Join-Path $stageDir "libmpv-2.dll")
    Copy-Item -Force $UninstallExe (Join-Path $stageDir "Uninstall.exe")

    Write-Host "Compressing payload archive..."
    & tar.exe -a -c -f $zipPath -C $stageDir PlayEverything.exe libmpv-2.dll Uninstall.exe
    if ($LASTEXITCODE -ne 0) {
        throw "tar.exe failed to create payload zip."
    }

    $outDir = Split-Path -Parent $OutputExe
    if ($outDir) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }

    $stubBytes = [System.IO.File]::ReadAllBytes($StubExe)
    $zipBytes = [System.IO.File]::ReadAllBytes($zipPath)

    $fs = [System.IO.File]::Open($OutputExe, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write)
    $bw = New-Object System.IO.BinaryWriter($fs)
    try {
        $bw.Write($stubBytes)
        $payloadOffset = [uint64]$stubBytes.Length
        $payloadSize = [uint64]$zipBytes.Length
        $bw.Write($zipBytes)
        $bw.Write([System.Text.Encoding]::ASCII.GetBytes("PESETUP1"))
        $bw.Write($payloadOffset)
        $bw.Write($payloadSize)
    } finally {
        $bw.Close()
        $fs.Close()
    }

    $finalInfo = Get-Item $OutputExe
    Write-Host ("Packaged single-file installer: {0} ({1:N1} MB)" -f $finalInfo.FullName, ($finalInfo.Length / 1MB))
} finally {
    Remove-Item -Recurse -Force $stageDir -ErrorAction SilentlyContinue
    Remove-Item -Force $zipPath -ErrorAction SilentlyContinue
}
