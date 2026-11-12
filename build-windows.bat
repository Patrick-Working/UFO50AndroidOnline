@echo off
rem UFO 50 Android Online - quick build of the Windows version. Double-click.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Windows %*
if not "%NOPAUSE%"=="1" pause
