@echo off
chcp 65001 > nul
title EKSTREM KOSUL AYRIK TAKTIK GNSS VE UYDU TERMINALI - MASTER KONTROL PANELI
color 0A

:MENU
cls
echo ===============================================================================
echo   EKSTREM ORTAM AYRIK GOVDE TAKTIK GNSS VE UYDU TERMINALI - KONTROL PANELI
echo   Calisma Zarfi: -30 C ... +50 C ^| Sharp 2.7" MIP ^| 1W LoRa + Iridium SBD
echo ===============================================================================
echo.
echo   [1] MET-SURV OPS: Taktik Meteoroloji ve Bivak Kalinabilirlik Arayuzunu Baslat
echo   [2] METEOROLOJI RAPORU: Taktik Hava ve Bivak Analiz Raporunu Ac (PDF)
echo   [3] CGPX STUDIO: Sharp 2.7" MIP Harita, Kripto ve Rota Inceleme Studyosu
echo   [4] SPONSORLUK DOSYASI: Malzeme Listesi (BOM) ve Butce Planini Ac (PDF)
echo   [5] SISTEM SUNUMU: 10 Slaytlik Yonetici Sunumunu Ac (PDF)
echo   [6] SARTNAME VE KOD BLOKLARI: Uretim Seviyesi C99 Kod Dokumanini Ac (PDF)
echo   [7] MASTER TEST: 6 Fazli C99 ve Sistem Dogrulama Surecini Calistir
echo   [0] Cikis
echo.
echo ===============================================================================
set /p choice="Lutfen calistirmak istediginiz islemi secin [0-7]: "

if "%choice%"=="1" goto OP_WEATHER
if "%choice%"=="2" goto OP_WEATHER_PDF
if "%choice%"=="3" goto OP_STUDIO
if "%choice%"=="4" goto OP_SPONSOR_PDF
if "%choice%"=="5" goto OP_PRESENT_PDF
if "%choice%"=="6" goto OP_CODE_PDF
if "%choice%"=="7" goto OP_TEST
if "%choice%"=="0" exit /b 0
goto MENU

:OP_WEATHER
echo.
echo [*] Taktik Meteoroloji ve Kalinabilirlik Terminali baslatiliyor...
start "" "%~dp0web\tactical_survival_meteorology.html"
echo [+] Tarayici acildi!
goto MENU

:OP_WEATHER_PDF
echo.
echo [*] Taktik Meteoroloji ve Kalinabilirlik Raporu (PDF) aciliyor...
start "" "%~dp0docs\TACTICAL_WEATHER_AND_SURVIVAL_ANALYSIS.pdf"
goto MENU

:OP_STUDIO
echo.
echo [*] CGPX Harita ve Kripto Studyosu baslatiliyor...
start "" "%~dp0web\cgpx_studio.html"
echo [+] Tarayici acildi!
goto MENU

:OP_SPONSOR_PDF
echo.
echo [*] Sponsorluk Malzeme Listesi ve Butce PDF'i aciliyor...
start "" "%~dp0docs\SPONSORLUK_MALZEME_LISTESI_VE_BUTCESI.pdf"
goto MENU

:OP_PRESENT_PDF
echo.
echo [*] Master Sistem Sunum PDF'i aciliyor...
start "" "%~dp0docs\TACTICAL_GNSS_SYSTEM_PRESENTATION.pdf"
goto MENU

:OP_CODE_PDF
echo.
echo [*] Malzeme Listesi ve C99 Kod Bloklari Sartnamesi aciliyor...
start "" "%~dp0docs\TACTICAL_GNSS_BOM_AND_CODEBLOCKS.pdf"
goto MENU

:OP_TEST
echo.
echo ===============================================================================
echo   6 FAZLI MASTER DOGRULAMA TESTI CALISTIRILIYOR...
echo ===============================================================================
echo.
py -3.10 test_phases_all.py
if errorlevel 1 (
    python test_phases_all.py
)
echo.
echo ===============================================================================
echo   Test tamamlandi. Menuye donmek icin Enter'a basin.
echo ===============================================================================
pause
goto MENU
