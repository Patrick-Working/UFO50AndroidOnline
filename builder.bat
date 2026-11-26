@echo off
rem UFO 50 Android Online - the builder: a window for the quick build (build.bat without the window: same build).
start "" powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0builder.ps1" %*
