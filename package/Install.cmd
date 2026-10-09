@echo off
rem MHFZ Plus setup: opens the setup window (PowerShell + Windows Forms, both part of Windows).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup\Install.ps1" %*
if errorlevel 1 pause
