@echo off
:: build_hlod.bat
:: Run from the repo root OR the windows-release-* folder after a CMake build.
::
:: Scans game/data/zones/ automatically and builds every zone found.
::
:: Usage:
::   build_hlod.bat                          (auto-detects binaries)
::   build_hlod.bat <windows-release-dir>    (explicit binary dir)

setlocal enabledelayedexpansion

:: --- Locate binaries ----------------------------------------------------
if not "%~1"=="" (
    set "BIN_DIR=%~1"
    goto :check_bins
)

:: 1) Current working directory
if exist "%CD%\zone_builder.exe" (
    set "BIN_DIR=%CD%"
    goto :check_bins
)

:: 2) Same directory as the bat file
if exist "%~dp0zone_builder.exe" (
    set "BIN_DIR=%~dp0"
    goto :check_bins
)

:: 3) Scan Downloads across common profile roots
for %%R in ("%USERPROFILE%" "%HOMEDRIVE%%HOMEPATH%" "C:\Users\%USERNAME%" "D:\Users\%USERNAME%") do (
    if exist "%%~R\Downloads" (
        for /f "delims=" %%D in ('dir /b /ad /o-d "%%~R\Downloads\windows-release-*" 2^>nul') do (
            if "!BIN_DIR!"=="" set "BIN_DIR=%%~R\Downloads\%%D"
        )
    )
)

if "!BIN_DIR!"=="" (
    echo [build_hlod] ERROR: could not locate zone_builder.exe automatically.
    echo [build_hlod] Pass the binary directory as argument:
    echo [build_hlod]   build_hlod.bat ^<windows-release-dir^>
    exit /b 1
)

:check_bins
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

:: --- Repo root = directory containing this bat --------------------------
set "REPO_ROOT=%~dp0"
if "%REPO_ROOT:~-1%"=="\" set "REPO_ROOT=%REPO_ROOT:~0,-1%"

set "CONTENT_DIR=%REPO_ROOT%\game\data"
set "ZONES_DIR=%CONTENT_DIR%\zones"

echo [build_hlod] Binaries : %BIN_DIR%
echo [build_hlod] Repo     : %REPO_ROOT%
echo [build_hlod] Content  : %CONTENT_DIR%

:: --- Scan zones ---------------------------------------------------------
echo.
echo [build_hlod] Scanning %ZONES_DIR% for zones ...

set "ZONE_COUNT=0"
set "FAIL_COUNT=0"

cd /d "%REPO_ROOT%"

for /d %%Z in ("%ZONES_DIR%\*") do (
    set "ZONE_NAME=%%~nxZ"
    set "LAYOUT=zones/!ZONE_NAME!/layout.json"
    set "LAYOUT_FULL=%ZONES_DIR%\!ZONE_NAME!\layout.json"
    set "ZONE_OUT=%REPO_ROOT%\build\!ZONE_NAME!"
    set "HLOD_OUT=!ZONE_OUT!\hlod.pak"

    if exist "!LAYOUT_FULL!" (
        set /a ZONE_COUNT+=1
        echo.
        echo [build_hlod] ---- Zone !ZONE_NAME! ----------------------------------------
        echo [build_hlod]   layout  : !LAYOUT!
        echo [build_hlod]   output  : !ZONE_OUT!

        :: Step 1: zone_builder (stderr → log file, terminal reste propre)
        echo [build_hlod]   Step 1/2 -- zone_builder ...
        if not exist "!ZONE_OUT!" mkdir "!ZONE_OUT!"
        "%ZONE_BUILDER%" --layout "!LAYOUT!" --output "build/!ZONE_NAME!" --config config.json 2>"!ZONE_OUT!\zone_builder.log"
        if errorlevel 1 (
            echo [build_hlod]   ERROR: zone_builder failed for !ZONE_NAME!
            set /a FAIL_COUNT+=1
        ) else (
            echo [build_hlod]   zone_builder OK

            :: Step 2: hlod_builder
            echo [build_hlod]   Step 2/2 -- hlod_builder ...
            "%HLOD_BUILDER%" --scan --content "%CONTENT_DIR%" --output "!HLOD_OUT!"
            if errorlevel 1 (
                echo [build_hlod]   ERROR: hlod_builder failed for !ZONE_NAME!
                set /a FAIL_COUNT+=1
            ) else (
                echo [build_hlod]   hlod_builder OK -- !HLOD_OUT!
            )
        )
    )
)

:: --- Summary ------------------------------------------------------------
echo.
if %ZONE_COUNT%==0 (
    echo [build_hlod] WARNING: no zones found in %ZONES_DIR%
    echo [build_hlod] Expected at least one folder with a layout.json inside.
    exit /b 1
)

if %FAIL_COUNT%==0 (
    echo [build_hlod] Done. %ZONE_COUNT% zone^(s^) built successfully.
) else (
    echo [build_hlod] Done with errors: %FAIL_COUNT% zone^(s^) failed out of %ZONE_COUNT%.
    exit /b 1
)

endlocal