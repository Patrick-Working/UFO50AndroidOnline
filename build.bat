@echo off
rem UFO 50 Android Online - quick build. Double-click, or run with -Install to install over USB.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
if not "%NOPAUSE%"=="1" pause
