@echo off
setlocal
chcp 65001 >nul
set "OV_REPAIR_NODE=node.exe"
where node.exe >nul 2>&1
if not errorlevel 1 goto run
set "OV_REPAIR_NODE=%USERPROFILE%\.cache\codex-runtimes\codex-primary-runtime\dependencies\node\bin\node.exe"
if exist "%OV_REPAIR_NODE%" goto run
echo Node.js was not found. Install Node.js 20 or newer, then run this file again.
if "%~1"=="" pause
exit /b 1
:run
"%OV_REPAIR_NODE%" "%~dp0ResolveCSharpBuild.mjs" %*
set "OV_REPAIR_EXIT=%ERRORLEVEL%"
if "%~1"=="" pause
exit /b %OV_REPAIR_EXIT%
