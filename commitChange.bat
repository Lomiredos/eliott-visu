@echo off
setlocal enabledelayedexpansion

REM ==========================================================================
REM  commitChange.bat
REM  1) Commit dans chaque submodule (message commun ou par-submodule).
REM     - bascule auto sur la branche par defaut si le submodule est detache.
REM  2) Push optionnel des submodules.
REM  3) Bump des pointeurs dans EE-Visu.
REM  4) Push optionnel de EE-Visu.
REM  A lancer depuis la racine de EE-Visu.
REM ==========================================================================

REM --- on doit etre a la racine d'un repo git ---
git rev-parse --is-inside-work-tree >nul 2>&1
if errorlevel 1 (
    echo [ERREUR] Pas dans un repo git.
    exit /b 1
)

REM --- message commun a tous ? ---
set "SAME="
set /p "SAME=Meme message de commit pour tous les submodules ? (o/n) : "

set "COMMONMSG="
if /i "!SAME!"=="o" (
    set /p "COMMONMSG=Message de commit commun : "
)

REM --- commit dans chaque submodule declare dans .gitmodules ---
for /f "tokens=2" %%p in ('git config --file .gitmodules --get-regexp path') do (
    call :handleSub "%%p"
)

REM --- push ? ---
set "PUSH="
set /p "PUSH=Pusher les submodules puis EE-Visu a la fin ? (o/n) : "

if /i "!PUSH!"=="o" (
    echo.
    echo === Push des submodules ===
    for /f "tokens=2" %%p in ('git config --file .gitmodules --get-regexp path') do (
        call :pushSub "%%p"
    )
)

REM --- retour dans EE-Visu : bump des pointeurs ---
echo.
echo === Bump des pointeurs dans EE-Visu ===
set "BUMPMSG="
set /p "BUMPMSG=Message pour le bump (defaut: bump submodules) : "
if "!BUMPMSG!"=="" set "BUMPMSG=bump submodules"

git add .gitmodules
for /f "tokens=2" %%p in ('git config --file .gitmodules --get-regexp path') do git add "%%p"

git diff --cached --quiet
if errorlevel 1 (
    git commit -m "!BUMPMSG!"
    echo Bump commit OK.
) else (
    echo Aucun pointeur modifie, rien a bumper.
)

REM --- push EE-Visu ? ---
if /i "!PUSH!"=="o" (
    echo.
    echo === Push EE-Visu ===
    git remote | findstr /r "." >nul
    if errorlevel 1 (
        echo [EE-Visu] aucun remote configure, push ignore.
    ) else (
        git push origin HEAD
    )
)

echo.
echo Termine.
exit /b 0

REM ==========================================================================
REM  :handleSub  %~1 = chemin du submodule  -> checkout branche + commit
REM ==========================================================================
:handleSub
set "SUB=%~1"
pushd "!SUB!" 2>nul
if errorlevel 1 (
    echo [!SUB!] dossier introuvable, ignore.
    goto :eof
)

REM y a-t-il quelque chose a committer ?
set "HAS="
for /f "delims=" %%L in ('git status --porcelain') do set "HAS=1"
if not defined HAS (
    echo [!SUB!] aucun changement, ignore.
    popd
    goto :eof
)

REM si HEAD est detache, basculer sur la branche par defaut du remote
git symbolic-ref -q HEAD >nul
if errorlevel 1 (
    set "BR="
    for /f "tokens=2 delims=/" %%b in ('git rev-parse --abbrev-ref origin/HEAD 2^>nul') do set "BR=%%b"
    if "!BR!"=="" set "BR=main"
    git checkout !BR! 2>nul
    if errorlevel 1 (
        echo [!SUB!] impossible de passer sur une branche ^(!BR!^), ignore.
        popd
        goto :eof
    )
    echo [!SUB!] bascule sur !BR!.
)

REM message : commun ou demande pour ce submodule
if /i "!SAME!"=="o" (
    set "MSG=!COMMONMSG!"
) else (
    set "MSG="
    set /p "MSG=[!SUB!] Message de commit : "
)

git add -A
git commit -m "!MSG!"
echo [!SUB!] commit OK.
popd
goto :eof

REM ==========================================================================
REM  :pushSub  %~1 = chemin du submodule  -> push si sur une branche
REM ==========================================================================
:pushSub
set "SUB=%~1"
pushd "!SUB!" 2>nul
if errorlevel 1 goto :eof

git symbolic-ref -q HEAD >nul
if errorlevel 1 (
    echo [!SUB!] detache, push ignore.
    popd
    goto :eof
)

echo [!SUB!] push...
git push origin HEAD
popd
goto :eof
