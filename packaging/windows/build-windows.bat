@echo off
rem Double-click to build Virtual-On for Windows:
rem  1. installs MSYS2 into C:\VON-PC\msys64 if it is not there (from installers\ in the kit
rem     folder; it asks first; MSYS2 must be on NTFS: exFAT has no hard links, which git and
rem     LLVM are installed as),
rem  2. reuses packages already downloaded into BuildEnv\msys64 (an earlier install in the kit),
rem  3. updates MSYS2, installs the build packages, builds and packages the port
rem     (build-windows.sh in the MSYS2 CLANG64 shell).
rem Every download or install asks for confirmation first.
rem The kit folder holds installers\, BuildEnv\, Game\ and the logs. It defaults to the folder
rem that contains the repo (Virtual-On-Windows\ beside virtualon_pc\); VO_KIT overrides it.
rem MSYS2_ROOT overrides the MSYS2 folder. The folder paths must not contain spaces.
setlocal
for %%I in ("%~dp0..\..") do set "REPO=%%~fI"
if not defined VO_KIT for %%I in ("%REPO%\..") do set "VO_KIT=%%~fI"
if not defined MSYS2_ROOT set "MSYS2_ROOT=C:\VON-PC\msys64"
set "MSYS2=%MSYS2_ROOT%\msys2_shell.cmd"

if not exist "%MSYS2%" (
    echo Installing MSYS2 into %MSYS2_ROOT% ...
    "%VO_KIT%\installers\msys2-x86_64-20260927.exe" in --root "%MSYS2_ROOT:\=/%"
    if not exist "%MSYS2%" (
        echo MSYS2 could not be installed into %MSYS2_ROOT%.
        pause
        exit /b 1
    )
)

rem Packages downloaded by the earlier install: pacman checks each one's signature before use.
set "OLDCACHE=%VO_KIT%\BuildEnv\msys64\var\cache\pacman\pkg"
if exist "%OLDCACHE%" (
    echo Copying already downloaded packages ...
    robocopy "%OLDCACHE%" "%MSYS2_ROOT%\var\cache\pacman\pkg" *.zst *.sig /XO /NJH /NJS /NDL /NFL /NP >nul
)

echo Using MSYS2 in %MSYS2_ROOT%
rem pacman -Syu twice: the first run may update MSYS2's core and stop.
call "%MSYS2%" -clang64 -defterm -no-start -here -c "pacman -Syu; pacman -Syu"
call "%MSYS2%" -clang64 -defterm -no-start -here -c "export MSYS2_ROOT=$(cygpath -u '%MSYS2_ROOT%') VO_KIT=$(cygpath -u '%VO_KIT%'); bash $(cygpath -u '%~dp0')build-windows.sh"
echo.
echo Finished. The log is in %VO_KIT%\build-log.txt
pause
