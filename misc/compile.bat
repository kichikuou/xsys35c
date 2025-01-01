@echo off
xsys35c --project=src/xsys35c.cfg --outdir=.
if errorlevel 1 (
    echo.
    pause
)
