@echo off
cd /d "%~dp0"
node run.mjs
if errorlevel 1 pause
