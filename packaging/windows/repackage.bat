@echo off
rem Re-runs only the packaging step (packaging\windows\package.sh): the launcher is frozen again
rem and dist\virtualon-windows refreshed. Nothing is downloaded, installed or recompiled.
rem Close Virtual-On.exe first: a running launcher keeps its folder from being refreshed.
rem The kit folder (BuildEnv\python.exe, the log) defaults to the folder that contains the repo;
rem VO_KIT overrides it. MSYS2_ROOT overrides the MSYS2 folder.
setlocal
for %%I in ("%~dp0..\..") do set "REPO=%%~fI"
if not defined VO_KIT for %%I in ("%REPO%\..") do set "VO_KIT=%%~fI"
if not defined MSYS2_ROOT set "MSYS2_ROOT=C:\VON-PC\msys64"
set "MSYS2=%MSYS2_ROOT%\msys2_shell.cmd"
if not exist "%MSYS2%" (
    echo MSYS2 not found in %MSYS2_ROOT%. Run build-windows.bat first.
    pause
    exit /b 1
)
call "%MSYS2%" -clang64 -defterm -no-start -here -c "export MSYS2_ROOT=$(cygpath -u '%MSYS2_ROOT%') WINPYTHON=$(cygpath -u '%VO_KIT%')/BuildEnv/python.exe; cd $(cygpath -u '%REPO%') && bash packaging/windows/package.sh 2>&1 | tee $(cygpath -u '%VO_KIT%')/repackage-log.txt"
echo.
echo Finished. The log is in %VO_KIT%\repackage-log.txt
pause
