# Downloads FBX2glTF.exe (Godot fork) into tools/asset_pipeline/bin/.
# Verifies SHA256 against a pinned value. Re-running is idempotent.

param([switch]$Force)

$ErrorActionPreference = "Stop"

$Version = "0.13.0"
$Url = "https://github.com/godotengine/FBX2glTF/releases/download/v$Version/FBX2glTF.exe"
$ExpectedSha256 = "27409D3358DD76ABCFDC73E0FAE25BD7CCBC36725D8004E329F9A1A9851DF953"

$binDir = Join-Path $PSScriptRoot "bin"
$dst = Join-Path $binDir "FBX2glTF.exe"

if ((Test-Path -LiteralPath $dst) -and -not $Force) {
    $existingSha = (Get-FileHash -Algorithm SHA256 -LiteralPath $dst).Hash
    if ($existingSha -ieq $ExpectedSha256) {
        Write-Host "[download_fbx2gltf] Already present at $dst (SHA matches v$Version)."
        exit 0
    }
    Write-Host "[download_fbx2gltf] Existing binary SHA mismatch; re-downloading." -ForegroundColor Yellow
}

New-Item -ItemType Directory -Force -Path $binDir | Out-Null

Write-Host "[download_fbx2gltf] Downloading FBX2glTF v$Version..."
Invoke-WebRequest -Uri $Url -OutFile $dst -UseBasicParsing

$actualSha = (Get-FileHash -Algorithm SHA256 -LiteralPath $dst).Hash
if ($actualSha -ine $ExpectedSha256) {
    Write-Host "[download_fbx2gltf] SHA256 mismatch!" -ForegroundColor Red
    Write-Host "  Expected: $ExpectedSha256"
    Write-Host "  Actual:   $actualSha"
    Remove-Item -LiteralPath $dst -Force
    exit 1
}

Write-Host "[download_fbx2gltf] OK ($dst, SHA matches v$Version)"
