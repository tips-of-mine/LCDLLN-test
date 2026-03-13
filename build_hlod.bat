@echo off
:: build_hlod.bat
:: Run from the repo root after a successful CMake build.
::
:: Builds chunk packages + HLOD pak for zone 0 using auto-scan mode.
:: Outputs land in build\zone_0\ next to the repo.
::
:: Usage:
::   build_hlod.bat                          (uses default SHA below)
::   build_hlod.bat <windows-release-dir>    (e.g. C:\Users\...\windows-release-abc123)

setlocal enabledelayedexpansion

:: --- Locate binaries ----------------------------------------------------
if "%~1"=="" (
    :: Try to find the most recent windows-release-* folder in Downloads
    set "SEARCH_DIR=%USERPROFILE%\Downloads"
    set "BIN_DIR="
    for /f "delims=" %%D in ('dir /b /ad /o-d "%SEARCH_DIR%\windows-release-*" 2^>nul') do (
        if "!BIN_DIR!"=="" set "BIN_DIR=%SEARCH_DIR%\%%D"
    )
    if "!BIN_DIR!"=="" (
        echo [build_hlod] ERROR: no windows-release-* folder found in %SEARCH_DIR%
        echo [build_hlod] Pass the binary directory as argument: build_hlod.bat ^<dir^>
        exit /b 1
    )
) else (
    set "BIN_DIR=%~1"
)

set "ZONE_BUILDER=%BIN_DIR%\zone_builder.exe"
set "HLOD_BUILDER=%BIN_DIR%\hlod_builder.exe"

if not exist "%ZONE_BUILDER%" (
    echo [build_hlod] ERROR: zone_builder.exe not found in %BIN_DIR%
    exit /b 1
)
if not exist "%HLOD_BUILDER%" (
    echo [build_hlod] ERROR: hlod_builder.exe not found in %BIN_DIR%
    exit /b 1
)

:: --- Paths --------------------------------------------------------------
set "REPO_ROOT=%~dp0"
:: Remove trailing backslash
if "%REPO_ROOT:~-1%"=="\" set "REPO_ROOT=%REPO_ROOT:~0,-1%"

set "CONTENT_DIR=%REPO_ROOT%\game\data"
set "CHUNKS_DIR=%REPO_ROOT%\build\zone_0\chunks"
set "HLOD_OUT=%REPO_ROOT%\build\zone_0\hlod.pak"

echo [build_hlod] Binaries : %BIN_DIR%
echo [build_hlod] Content  : %CONTENT_DIR%
echo [build_hlod] Output   : %REPO_ROOT%\build\zone_0\

:: --- Step 1: zone_builder — chunk packages ------------------------------
echo.
echo [build_hlod] Step 1/2 — writing chunk package (chunk 0,0) ...
"%ZONE_BUILDER%" --output "%CHUNKS_DIR%" --chunk 0 0
if errorlevel 1 (
    echo [build_hlod] ERROR: zone_builder failed.
    exit /b 1
)
echo [build_hlod] chunk_0_0 OK

:: --- Step 2: hlod_builder — scan + merge --------------------------------
echo.
echo [build_hlod] Step 2/2 — building HLOD pak (scan mode) ...
"%HLOD_BUILDER%" --scan --content "%CONTENT_DIR%" --output "%HLOD_OUT%"
if errorlevel 1 (
    echo [build_hlod] ERROR: hlod_builder failed.
    exit /b 1
)

echo.
echo [build_hlod] Done. Outputs:
echo   %CHUNKS_DIR%\chunk_0_0\  (chunk.meta, geo.pak, tex.pak, instances.bin, navmesh.bin, probes.bin)
echo   %HLOD_OUT%

endlocal