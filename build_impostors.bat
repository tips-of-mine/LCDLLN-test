@echo off
:: build_impostors.bat
:: Run from the repo root OR the windows-release-* folder after a CMake build.
::
:: Construit un atlas d'impostors pour un mesh d'exemple sous
:: game/data/meshes/props/.
::
:: Usage:
::   build_impostors.bat                          (auto-détecte les binaires)
::   build_impostors.bat <windows-release-dir>    (dossier binaire explicite)
::   build_impostors.bat <bin-dir> <mesh.gltf>    (mesh explicite)

setlocal enabledelayedexpansion

:: --- Localiser les binaires ---------------------------------------------
set "BIN_DIR="
if not "%~1"=="" (
    set "BIN_DIR=%~1"
    goto :check_bins
)

:: 1) Répertoire courant
if exist "%CD%\impostor_builder.exe" (
    set "BIN_DIR=%CD%"
    goto :check_bins
)

:: 2) Même dossier que ce .bat
if exist "%~dp0impostor_builder.exe" (
    set "BIN_DIR=%~dp0"
    goto :check_bins
)

:: 3) Scan Downloads sur les racines de profil courantes
for %%R in ("%USERPROFILE%" "%HOMEDRIVE%%HOMEPATH%" "C:\Users\%USERNAME%" "D:\Users\%USERNAME%") do (
    if exist "%%~R\Downloads" (
        for /f "delims=" %%D in ('dir /b /ad /o-d "%%~R\Downloads\windows-release-*" 2^>nul') do (
            if "!BIN_DIR!"=="" set "BIN_DIR=%%~R\Downloads\%%D"
        )
    )
)

if "!BIN_DIR!"=="" (
    echo [build_impostors] ERROR: impossible de localiser impostor_builder.exe.
    echo [build_impostors] Passez le dossier binaire en argument:
    echo [build_impostors]   build_impostors.bat ^<windows-release-dir^>
    exit /b 1
)

:check_bins
set "IMPOSTOR_BUILDER=%BIN_DIR%\impostor_builder.exe"
if not exist "%IMPOSTOR_BUILDER%" (
    echo [build_impostors] ERROR: impostor_builder.exe introuvable dans %BIN_DIR%
    exit /b 1
)

:: --- Racine du dépôt = dossier de ce .bat -------------------------------
set "REPO_ROOT=%~dp0"
if "%REPO_ROOT:~-1%"=="\" set "REPO_ROOT=%REPO_ROOT:~0,-1%"

set "PROPS_DIR=%REPO_ROOT%\game\data\meshes\props"

:: --- Mesh d'exemple (argument 2 sinon Anvil.gltf par défaut) -------------
if not "%~2"=="" (
    set "MESH=%~2"
) else (
    set "MESH=%PROPS_DIR%\Anvil.gltf"
)

if not exist "%MESH%" (
    echo [build_impostors] ERROR: mesh introuvable: %MESH%
    echo [build_impostors] Passez un mesh: build_impostors.bat ^<bin-dir^> ^<mesh.gltf^>
    exit /b 1
)

set "OUT_DIR=%REPO_ROOT%\build\impostors"
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

for %%F in ("%MESH%") do set "MESH_NAME=%%~nF"
set "OUT_FILE=%OUT_DIR%\%MESH_NAME%_impostor.bin"

echo [build_impostors] Binaire : %IMPOSTOR_BUILDER%
echo [build_impostors] Mesh    : %MESH%
echo [build_impostors] Sortie  : %OUT_FILE%
echo.

"%IMPOSTOR_BUILDER%" --input "%MESH%" --output "%OUT_FILE%" --views 8 --tile 64
if errorlevel 1 (
    echo [build_impostors] ERROR: la construction a échoué.
    exit /b 1
)

echo.
echo [build_impostors] OK -- %OUT_FILE%
endlocal
