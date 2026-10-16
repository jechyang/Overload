@echo off
set "PROJECT=%~1"
if "%PROJECT%"=="" set "PROJECT=%~dp0Examples\FrameGraphLab"

if exist "%~dp0Build\Debug\OvEditor.exe" (
    cd /d "%~dp0Build\Debug"
    start "" "%~dp0Build\Debug\OvEditor.exe" "%PROJECT%"
) else (
    cd /d "%~dp0Build\FrameGraphLab\Debug\Runtime"
    start "" "%~dp0Build\FrameGraphLab\Debug\FrameGraphLabEditor.exe" "%PROJECT%"
)
