# Compile les sources GLSL sous game/data/shaders/ vers des modules SPIR-V (*.vert.spv, etc.).
# Prérequis : glslangValidator (Vulkan SDK : %VULKAN_SDK%\Bin\glslangValidator.exe) ou variable GLSLANG_VALIDATOR.
# Usage : powershell -NoProfile -ExecutionPolicy Bypass -File tools/compile_game_shaders.ps1 [-ShaderDir <chemin>]
param(
    [string]$ShaderDir = ""
)

$ErrorActionPreference = "Stop"

function Resolve-GlslangValidator {
    if ($env:GLSLANG_VALIDATOR -and (Test-Path -LiteralPath $env:GLSLANG_VALIDATOR)) {
        return (Resolve-Path -LiteralPath $env:GLSLANG_VALIDATOR).Path
    }
    if ($env:VULKAN_SDK) {
        $candidates = @(
            (Join-Path $env:VULKAN_SDK "Bin\glslangValidator.exe"),
            (Join-Path $env:VULKAN_SDK "bin\glslangValidator.exe")
        )
        foreach ($p in $candidates) {
            if (Test-Path -LiteralPath $p) { return (Resolve-Path -LiteralPath $p).Path }
        }
    }
    $cmd = Get-Command glslangValidator -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    throw "glslangValidator introuvable. Installez le Vulkan SDK ou définissez GLSLANG_VALIDATOR / VULKAN_SDK."
}

if ([string]::IsNullOrWhiteSpace($ShaderDir)) {
    $root = Split-Path -Parent $PSScriptRoot
    $ShaderDir = Join-Path $root "game\data\shaders"
}

if (-not (Test-Path -LiteralPath $ShaderDir)) {
    throw "Répertoire shaders introuvable : $ShaderDir"
}

$glslang = Resolve-GlslangValidator
Write-Host "[compile_game_shaders] glslangValidator = $glslang"
Write-Host "[compile_game_shaders] ShaderDir = $ShaderDir"

$extensions = @(
    @{ ext = "*.vert"; stage = "vert" },
    @{ ext = "*.frag"; stage = "frag" },
    @{ ext = "*.comp"; stage = "comp" }
)

$failed = $false
foreach ($row in $extensions) {
    Get-ChildItem -Path $ShaderDir -Filter $row.ext -File | ForEach-Object {
        $src = $_.FullName
        $outName = $_.Name + ".spv"
        $dst = Join-Path $ShaderDir $outName
        $argList = @("-V", "-S", $row.stage, "-o", $dst, $src)
        Write-Host "[compile_game_shaders] $($_.Name) -> $outName"
        $p = Start-Process -FilePath $glslang -ArgumentList $argList -WorkingDirectory $ShaderDir -Wait -PassThru -NoNewWindow
        if ($p.ExitCode -ne 0) {
            Write-Host "[compile_game_shaders] ERREUR exit $($p.ExitCode) pour $src" -ForegroundColor Red
            $failed = $true
        }
    }
}

if ($failed) {
    exit 1
}

Write-Host "[compile_game_shaders] OK"
