@echo off
chcp 65001 > nul
title EKSTREM KOSUL AYRIK GNSS TERMINALI - CANLI TEST LABORATUVARI
cls
echo ========================================================================
echo   EKSTREM KOSUL AYRIK GNSS TERMINALI CANLI TEST LABORATUVARI BASLATILIYOR
echo ========================================================================
echo.
py -3.10 "%~dp0..\tools\interactive_lab.py"
if errorlevel 1 (
    python "%~dp0..\tools\interactive_lab.py"
)
pause
