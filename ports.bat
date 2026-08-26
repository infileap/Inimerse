@echo off
rem ports.bat - one-command Inimerse server status check
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0ports.ps1"
pause
