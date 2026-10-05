@echo off
chcp 65001 > nul
title Tactical Weather and Bivouac Terminal
cls

echo ===============================================================================
echo   MET-SURV OPS: TACTICAL METEOROLOGY AND SURVIVAL TERMINAL
echo ===============================================================================
echo.
echo [*] Launching interface in your default browser...
echo.

start "" "%~dp0..\web\tactical_survival_meteorology.html"

exit /b 0
