@echo off
setlocal

if not exist src/xsys35c.cfg goto decompile
set /p answer="src/xsys35c.cfg already exists. Do you want to continue? (y/n): "
if /i not "%answer%"=="y" (
    echo Operation aborted.
    pause
    exit /b 1
)

:decompile
xsys35dc . --outdir=src
if errorlevel 1 (
    echo.
    pause
)
