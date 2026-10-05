@echo off
chcp 65001 > nul
title MET-SURV OPS - TAKTIK METEOROLOJI VE BIVAK KALINABILIRLIK TERMINALI
color 0B
cls

echo ===============================================================================
echo   MET-SURV OPS: TAKTIK METEOROLOJI VE BIVAK KALINABILIRLIK TERMINALI
echo   High-Altitude Thermodynamic Lapse Rate and Bivouac Habitability Engine
echo ===============================================================================
echo.
echo [*] Taktik Meteoroloji ve Kalinabilirlik Arayuzu Baslatiliyor...
echo [*] Hedef: %~dp0web\tactical_survival_meteorology.html
echo.

start "" "%~dp0web\tactical_survival_meteorology.html"

echo [+] Arayuz varsayilan web tarayicinizda basariyla acildi!
echo ===============================================================================
exit /b 0
