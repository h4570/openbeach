@echo off
rem Starts OpenBeach on the L0 pack, the project's own small scenario: no original game data needed (README.txt).
setlocal
set "OBL_PACK=l0"
start "OpenBeach demo" /min "%~dp0OpenBeach.exe" --game-dir "%~dp0l0"
