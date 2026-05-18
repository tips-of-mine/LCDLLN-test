# Converts a Mixamo FBX (in tools/asset_pipeline/inbox/) to glTF binary (.glb)
# in game/data/models/<Category>/<EntityName>/<EntityName>.glb.

param(
    [Parameter(Mandatory=$true)][string]$EntityName,
    [Parameter(Mandatory=$true)][string]$Category,
    [string]$SourceFbx = ""  # Optional. Defaults to "<EntityName>.fbx" in inbox/.
)

$ErrorActionPreference = "Stop"

$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$converter = Join-Path $PSScriptRoot "bin\FBX2glTF.exe"

# Resolve source FBX name. If -SourceFbx not given, assume "<EntityName>.fbx".
if ([string]::IsNullOrWhiteSpace($SourceFbx)) {
    $SourceFbx = "$EntityName.fbx"
}
# Allow caller to pass either bare name ("Standard Walk") or with .fbx suffix.
if (-not $SourceFbx.EndsWith(".fbx")) {
    $SourceFbx = "$SourceFbx.fbx"
}

$srcFbx = Join-Path $PSScriptRoot "inbox\$SourceFbx"
$outDir = Join-Path $root "game\data\models\$Category\$EntityName"
# FBX2glTF v0.13.0 appends ".glb" when --binary is set, so pass output WITHOUT suffix.
$outBase = Join-Path $outDir $EntityName

if (-not (Test-Path -LiteralPath $converter)) {
    throw "FBX2glTF.exe not found at $converter. Run download_fbx2gltf.ps1 first."
}
if (-not (Test-Path -LiteralPath $srcFbx)) {
    throw "Source FBX not found at $srcFbx. Drop your Mixamo .fbx in tools/asset_pipeline/inbox/ first."
}

New-Item -ItemType Directory -Force -Path $outDir | Out-Null

Write-Host "[fbx_to_gltf] Converting $srcFbx ..."
& $converter `
    --input $srcFbx `
    --output $outBase `
    --binary `
    --khr-materials-unlit `
    --skinning-weights 4
if ($LASTEXITCODE -ne 0) {
    throw "FBX2glTF failed with exit code $LASTEXITCODE"
}

$outGlb = "$outBase.glb"
if (-not (Test-Path -LiteralPath $outGlb)) {
    throw "Expected output $outGlb not produced"
}

Write-Host "[fbx_to_gltf] Produced $outGlb ($((Get-Item $outGlb).Length) bytes)"
