@echo off
rem UFO 50 Android Online - quick build of the Android APK. Double-click, or run with -Install to
rem install over USB. The same as build.bat.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
if not "%NOPAUSE%"=="1" pause
