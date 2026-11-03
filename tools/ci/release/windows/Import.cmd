@echo off
rem Imports your own copy of Beach Life (README.txt): drop the game folder, the CD drive or the .iso file on this file,
rem or start it and drag the copy into the window. The work is done by scripts\import.ps1.
rem   Import.cmd [copy] [language]
setlocal
chcp 65001 >nul
set "OBL_SOURCE=%~1"
rem "E:\" would reach PowerShell as E:" (a backslash before a quote escapes it): the script adds the backslash back.
if defined OBL_SOURCE if "%OBL_SOURCE:~-1%"=="\" set "OBL_SOURCE=%OBL_SOURCE:~0,-1%"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\import.ps1" -Source "%OBL_SOURCE%" -Language "%~2"
set "OBL_EXIT=%ERRORLEVEL%"
echo.
pause
exit /b %OBL_EXIT%
