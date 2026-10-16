@echo off
set "PROJECT=%~1"
if "%PROJECT%"=="" set "PROJECT=%~dp0Examples\FrameGraphLab"
start "" "%~dp0Build\FrameGraphLab\Debug\FrameGraphLabEditor.exe" "%PROJECT%"
