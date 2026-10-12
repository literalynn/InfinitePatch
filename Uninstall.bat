@echo off
title InfinitePatch
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0InfinitePatch.ps1" -Uninstall
echo.
pause
