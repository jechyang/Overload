@echo off
setlocal DisableDelayedExpansion
if not defined OV_CSC_DOTNET exit /b 1
if not defined OV_CSC_DLL exit /b 1
"%OV_CSC_DOTNET%" "%OV_CSC_DLL%" %*
exit /b %errorlevel%
