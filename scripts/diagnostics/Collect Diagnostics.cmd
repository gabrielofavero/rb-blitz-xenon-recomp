@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0collect_diagnostics.ps1" %*
if errorlevel 1 echo Diagnostic collection failed. The message above identifies the problem.
pause
