# Helpers de packaging pour build-windows.yml (jobs parallèles client/editor/server).
# Factorise la collecte des artefacts, appelé via `. package-windows.ps1` puis
# les fonctions ci-dessous. Chaque job ne package que ce dont il a besoin.
#
# Principe : CMake déploie déjà les DLL runtime (glfw3, vulkan-1, libssl/libcrypto)
# À CÔTÉ de chaque exe (steps « deploying dependencies »). On copie donc l'exe + les
# DLL de son dossier, puis on ajoute les redistribuables MSVC + ucrt (que CMake ne
# déploie pas), et enfin les données/config selon le composant.

$ErrorActionPreference = "Stop"
$Artifacts = Join-Path $env:GITHUB_WORKSPACE "artifacts"

function Initialize-Artifacts {
    New-Item -ItemType Directory -Force -Path $Artifacts | Out-Null
}

# Copie les exécutables nommés (trouvés sous build/, hors vcpkg/Debug/tests) +
# les DLL déjà déployées par CMake dans le dossier de chaque exe.
function Copy-ArtifactExes {
    param([Parameter(Mandatory = $true)][string[]]$Names)
    Initialize-Artifacts
    foreach ($name in $Names) {
        $exe = Get-ChildItem -Recurse -Path "$env:GITHUB_WORKSPACE\build" -Filter $name -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -notlike "*\vcpkg\*" -and $_.FullName -notlike "*\Debug\*" -and $_.FullName -notlike "*\debug\*" } |
            Select-Object -First 1
        if (-not $exe) { throw "[package] Exécutable introuvable : $name" }
        Copy-Item $exe.FullName -Destination $Artifacts -Force
        # DLL runtime déployées par CMake à côté de l'exe.
        Get-ChildItem -Path $exe.DirectoryName -Filter *.dll -ErrorAction SilentlyContinue |
            Copy-Item -Destination $Artifacts -Force
        Write-Host "[package] exe : $($exe.FullName)"
    }
}

# Redistribuables MSVC + ucrt (non déployés par CMake) + filet de sécurité pour
# les DLL vcpkg (ssl/crypto/glfw/vulkan) au cas où un exe n'aurait rien à côté.
# Le switch -IncludeGraphics est accepté pour lisibilité mais les DLL graphiques
# sont copiées de toute façon (inoffensif pour le serveur).
function Copy-RuntimeDlls {
    param([switch]$IncludeGraphics)
    Initialize-Artifacts

    # MSVC redist (Release, x64).
    $vcRedistBase = "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Redist\MSVC"
    foreach ($dll in @("MSVCP140.dll", "VCRUNTIME140.dll", "VCRUNTIME140_1.dll")) {
        $found = Get-ChildItem -Recurse -Path $vcRedistBase -Filter $dll -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -like "*x64*" -and $_.FullName -notlike "*debug_nonredist*" -and $_.FullName -notlike "*onecore*" } |
            Select-Object -First 1
        if ($found) { Copy-Item $found.FullName -Destination $Artifacts -Force }
    }

    # ucrtbase (System32).
    $ucrt = "C:\Windows\System32\ucrtbase.dll"
    if (Test-Path $ucrt) { Copy-Item $ucrt -Destination $Artifacts -Force }

    # Filet de sécurité : DLL vcpkg (si absentes à côté de l'exe).
    $vcpkgBinCandidates = @(
        (Join-Path $env:VCPKG_ROOT "installed\$env:VCPKG_DEFAULT_TRIPLET\bin"),
        (Join-Path $env:GITHUB_WORKSPACE "build\vs2022-x64\vcpkg_installed\$env:VCPKG_DEFAULT_TRIPLET\bin"),
        (Join-Path $env:GITHUB_WORKSPACE "build\vs2022-x64\vcpkg_installed\x64-windows\bin")
    )
    $vcpkgDlls = @("libssl-3-x64.dll", "libcrypto-3-x64.dll", "glfw3.dll", "vulkan-1.dll")
    foreach ($bin in $vcpkgBinCandidates) {
        if (-not (Test-Path $bin)) { continue }
        foreach ($dll in $vcpkgDlls) {
            $src = Join-Path $bin $dll
            if ((Test-Path $src) -and -not (Test-Path (Join-Path $Artifacts $dll))) {
                Copy-Item $src -Destination $Artifacts -Force
            }
        }
    }
}

# game/data (assets, shaders SPIR-V, icônes LFS) -> artifacts/game/data.
function Copy-GameData {
    Initialize-Artifacts
    $src = Join-Path $env:GITHUB_WORKSPACE "game\data"
    if (Test-Path $src) {
        Copy-Item -Recurse -Path $src -Destination (Join-Path $Artifacts "game\data") -Force
    }
}

# Config côté client/éditeur : config.json racine + config/server.ini + build_hlod.bat.
function Copy-ClientConfig {
    Initialize-Artifacts
    $config = Join-Path $env:GITHUB_WORKSPACE "config.json"
    if (Test-Path $config) { Copy-Item $config -Destination $Artifacts -Force }
    Copy-ServerIni
    $buildHlod = Join-Path $env:GITHUB_WORKSPACE "build_hlod.bat"
    if (Test-Path $buildHlod) { Copy-Item $buildHlod -Destination $Artifacts -Force }
}

# Config côté serveur : config.json + config/server.ini.
function Copy-ServerConfig {
    Initialize-Artifacts
    $config = Join-Path $env:GITHUB_WORKSPACE "config.json"
    if (Test-Path $config) { Copy-Item $config -Destination $Artifacts -Force }
    Copy-ServerIni
}

# config/server.ini (endpoints master runtime) -> artifacts/config/.
function Copy-ServerIni {
    $serverIni = Join-Path $env:GITHUB_WORKSPACE "config\server.ini"
    if (Test-Path $serverIni) {
        New-Item -ItemType Directory -Force -Path (Join-Path $Artifacts "config") | Out-Null
        Copy-Item $serverIni -Destination (Join-Path $Artifacts "config") -Force
    }
}
