@echo off
set "PROJECT=%~dp0Examples\CardBattle\CardBattle.local.ovproject"

if not exist "%PROJECT%" (
    echo Building CardBattle project first...
    node "%~dp0Examples\CardBattle\build.mjs" "C:/Users/Administrator/AppData/Local/Programs/Python/Python312/python.exe"
)

if exist "%~dp0Build\Debug\OvEditor.exe" (
    cd /d "%~dp0Build\Debug"
    start "" "%~dp0Build\Debug\OvEditor.exe" "%PROJECT%"
) else (
    echo OvEditor.exe not found in Build\Debug!
)
