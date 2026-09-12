@echo off
REM Build script - calls PowerShell build

powershell -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
