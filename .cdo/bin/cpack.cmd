@echo off
set "SCRIPT_DIR=%~dp0"
"%SCRIPT_DIR%..\tools\cmake\cmake-4.3.4-windows-x86_64\bin\cpack.exe" %*