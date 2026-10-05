@echo off
chcp 65001 > nul
title MET-SURV OPS - TACTICAL WEATHER AND BIVOUAC TERMINAL
color 0B
cls

echo ===============================================================================
echo   MET-SURV OPS: TACTICAL METEOROLOGY AND BIVOUAC HABITABILITY TERMINAL
echo ===============================================================================
echo.
echo [*] Launching Tactical Weather and Survival Interface in your default browser...
echo.

start "" "%~dp0web\tactical_survival_meteorology.html"

echo [+] Interface opened successfully!
exit /b 0
