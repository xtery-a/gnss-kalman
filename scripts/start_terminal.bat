@echo off
chcp 65001 > nul
title Tactical GNSS Terminal and Bridge
cls

echo ===============================================================================
echo   TACTICAL MULTI-GNSS TERMINAL (GARMIN GPSMAP 67i ARCHITECTURE)
echo ===============================================================================
echo.
echo [*] Starting Python Hardware Bridge on port 8080...
echo.

python "%~dp0..\web\open_weather_lab.py"

pause
