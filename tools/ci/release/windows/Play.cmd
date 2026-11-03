@echo off
rem Starts OpenBeach on your imported copy (README.txt). Settings, saves and the log are in userdata\.
setlocal
if not exist "%~dp0userdata\packs\base\pack.json" goto :no_pack
rem The game's console stays minimized while it runs and closes with it.
start "OpenBeach" /min "%~dp0OpenBeach.exe" --game-dir "%~dp0userdata"
exit /b 0

:no_pack
echo Your copy of Beach Life is not imported yet: start Import.cmd first, see README.txt.
echo To try OpenBeach without the original game, start Demo.cmd.
echo.
pause
exit /b 1
