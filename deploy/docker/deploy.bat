@echo off
REM ============================================================
REM LCDLLN — deploy.bat
REM Script de déploiement rapide depuis un artifact CI Windows
REM ============================================================
REM Usage :
REM   deploy.bat <chemin_vers_artifact>
REM   deploy.bat C:\Users\thedj\Downloads\windows-release-abc123\
REM
REM   Ou sans argument pour utiliser le binaire déjà dans docker/bin/
REM   deploy.bat
REM ============================================================

setlocal EnableDelayedExpansion

echo.
echo ============================================================
echo  LCDLLN Server Deploy
echo ============================================================

REM --- Aller dans le dossier docker/ -------------------------
cd /d "%~dp0"

REM --- Vérifier que Docker est disponible --------------------
docker --version >nul 2>&1
if errorlevel 1 (
    echo [ERREUR] Docker n'est pas installe ou pas dans le PATH.
    echo          Installer Docker Desktop : https://www.docker.com/products/docker-desktop
    exit /b 1
)

docker-compose --version >nul 2>&1
if errorlevel 1 (
    echo [ERREUR] docker-compose n'est pas disponible.
    exit /b 1
)

REM --- Créer le dossier bin/ si nécessaire -------------------
if not exist "bin" mkdir bin

REM --- Copier le binaire si un chemin d'artifact est fourni --
if not "%~1"=="" (
    echo [INFO] Artifact source : %~1
    set "ARTIFACT_DIR=%~1"

    REM Chercher le binaire Linux dans l'artifact
    REM Le binaire Linux est dans le sous-dossier linux-release-*/
    for /d %%D in ("%ARTIFACT_DIR%linux-release-*") do (
        if exist "%%D\lcdlln_server" (
            echo [INFO] Binaire Linux trouve : %%D\lcdlln_server
            copy /Y "%%D\lcdlln_server" "bin\lcdlln_server"
            goto :binary_ok
        )
    )

    REM Chercher directement dans le dossier fourni
    if exist "%ARTIFACT_DIR%lcdlln_server" (
        echo [INFO] Binaire Linux trouve : %ARTIFACT_DIR%lcdlln_server
        copy /Y "%ARTIFACT_DIR%lcdlln_server" "bin\lcdlln_server"
        goto :binary_ok
    )

    echo [ERREUR] Binaire Linux lcdlln_server introuvable dans : %~1
    echo          Verifiez que l'artifact CI contient bien le build Linux.
    exit /b 1
)

:binary_ok
REM --- Vérifier que le binaire existe ------------------------
if not exist "bin\lcdlln_server" (
    echo [ERREUR] bin\lcdlln_server introuvable.
    echo.
    echo  Solutions :
    echo    1. Fournir le chemin de l'artifact CI en argument :
    echo       deploy.bat C:\chemin\vers\artifact\
    echo.
    echo    2. Copier manuellement le binaire Linux :
    echo       cp <artifact>/lcdlln_server docker/bin/lcdlln_server
    echo.
    echo    3. Builder depuis les sources dans Docker :
    echo       docker-compose --profile build up --build
    exit /b 1
)

echo [INFO] Binaire present : bin\lcdlln_server
for %%F in ("bin\lcdlln_server") do echo [INFO] Taille : %%~zF bytes

REM --- Créer .env si absent ----------------------------------
if not exist ".env" (
    echo [INFO] Creation de .env depuis .env.example
    copy ".env.example" ".env"
    echo [WARN] .env cree avec les valeurs par defaut DEV.
    echo        Editer docker\.env pour configurer les secrets.
)

REM --- Arrêter les conteneurs existants ----------------------
echo.
echo [INFO] Arret des conteneurs existants...
docker-compose down --remove-orphans 2>nul

REM --- Rebuild l'image serveur -------------------------------
echo.
echo [INFO] Build de l'image serveur...
docker-compose build server
if errorlevel 1 (
    echo [ERREUR] docker-compose build a echoue.
    exit /b 1
)

REM --- Démarrer les services ---------------------------------
echo.
echo [INFO] Demarrage des services (MySQL + Server)...
docker-compose up -d
if errorlevel 1 (
    echo [ERREUR] docker-compose up a echoue.
    exit /b 1
)

REM --- Attendre et afficher le statut ------------------------
echo.
echo [INFO] Attente du demarrage (15s)...
timeout /t 15 /nobreak >nul

echo.
echo [INFO] Statut des conteneurs :
docker-compose ps

echo.
echo [INFO] Logs du serveur (10 dernieres lignes) :
docker-compose logs --tail=20 server

echo.
echo ============================================================
echo  Serveur deploy !
echo  Port : 3840
echo  MySQL : localhost:3306 (base: lcdlln)
echo.
echo  Commandes utiles :
echo    Voir les logs en direct : docker-compose logs -f server
echo    Voir les logs MySQL     : docker-compose logs -f mysql
echo    Arreter tout            : docker-compose down
echo    Redemarrer le serveur   : docker-compose restart server
echo    Console MySQL           : docker-compose exec mysql mysql -u lcdlln_user -p lcdlln
echo ============================================================

endlocal
