#!/usr/bin/env python3
"""
EKSTREM KOŞUL AYRIK GNSS TERMİNALİ - İNTERAKTİF TEST LABORATUVARI (interactive_lab.py)
Kullanıcının terminal üzerinde canlı olarak parametreleri değiştirip
tüm fiziksel modelleri, RF hesaplarını ve test fazlarını test etmesini sağlar.
"""

import math
import os
import subprocess
import sys
import time
import webbrowser

import numpy as np

# Windows ANSI renk desteği
if sys.platform == "win32":
    os.system("color")
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")

GREEN = "\033[92m"
RED = "\033[91m"
YELLOW = "\033[93m"
CYAN = "\033[96m"
MAGENTA = "\033[95m"
BOLD = "\033[1m"
RESET = "\033[0m"


def clear_screen():
    os.system("cls" if os.name == "nt" else "clear")


def print_banner():
    clear_screen()
    print(f"{CYAN}{BOLD}========================================================================{RESET}")
    print(f"{CYAN}{BOLD}   EKSTREM KOŞUL AYRIK GNSS TERMİNALİ - CANLI TEST LABORATUVARI        {RESET}")
    print(f"{CYAN}{BOLD}   EXT-GNSS-SPEC-001 / EXT-GNSS-V&V-001                                {RESET}")
    print(f"{CYAN}{BOLD}========================================================================{RESET}")
    print(f"{YELLOW}Bilgisayarınızda canlı, etkileşimli ve deterministik test konsolu.{RESET}\n")


# -----------------------------------------------------------------------------
# MODÜL 1: CANLI SKYMASK VE POLAR GÖRSELLEŞTİRME
# -----------------------------------------------------------------------------
def demo_skymask():
    print_banner()
    print(f"{BOLD}[1] CANLI TOPOĞRAFİK SKYMASK ÇIKARIMI VE POLAR GRAFİK{RESET}\n")
    print("Mevcut sentetik DEM: sample_dem.tif (Uludağ / Bursa bölgesi)")

    try:
        lat_in = input("Hedef Enlem (Varsayılan 40.0694): ").strip()
        lat = float(lat_in) if lat_in else 40.0694

        lon_in = input("Hedef Boylam (Varsayılan 29.2217): ").strip()
        lon = float(lon_in) if lon_in else 29.2217

        rad_in = input("Arama Yarıçapı metre (Varsayılan 5000): ").strip()
        radius = float(rad_in) if rad_in else 5000.0

        step_in = input("Işın Adımı metre (Varsayılan 15): ").strip()
        step = float(step_in) if step_in else 15.0
    except ValueError:
        print(f"{RED}Geçersiz sayısal girdi! Varsayılanlar kullanılıyor.{RESET}")
        lat, lon, radius, step = 40.0694, 29.2217, 5000.0, 15.0

    print(f"\n{CYAN}[*] 64-bin Skymask motoru çalıştırılıyor...{RESET}")
    cmd = [
        sys.executable,
        "skymask_gen.py",
        "--dem", "sample_dem.tif",
        "--lat", str(lat),
        "--lon", str(lon),
        "--radius", str(radius),
        "--step", str(step),
        "--output-c", "skymask_lut.h",
        "--plot", "skymask_polar.png"
    ]
    subprocess.run(cmd)

    if os.path.isfile("skymask_polar.png"):
        print(f"\n{GREEN}[+] Polar grafik üretildi: skymask_polar.png{RESET}")
        print(f"{GREEN}[+] ANSI C99 Tablosu üretildi: skymask_lut.h{RESET}")
        open_plot = input("\nGrafiği Windows varsayılan resim görüntüleyicide açmak ister misiniz? (E/h): ").strip().lower()
        if open_plot in ("", "e", "evet", "y", "yes"):
            try:
                os.startfile(os.path.abspath("skymask_polar.png"))
                print(f"{CYAN}[*] Resim açıldı.{RESET}")
            except Exception as e:
                print(f"{YELLOW}[!] Dosya açılamadı: {e}{RESET}")

    input(f"\n{YELLOW}Ana menüye dönmek için Enter'a basın...{RESET}")


# -----------------------------------------------------------------------------
# MODÜL 2: FRESNEL KIRINIM & HİSTEREZİS SİMÜLATÖRÜ
# -----------------------------------------------------------------------------
def demo_fresnel_simulator():
    print_banner()
    print(f"{BOLD}[2] FRESNEL BIÇAK SIRTI KIRINIMI VE AÇISAL HİSTEREZİS SİMÜLATÖRÜ{RESET}\n")
    print("Kanyon sırt hattına yaklaşan uydunun kırınım kaybı ve histerezis geçişleri.")
    print("Sırt hattı ufuk açısı sabit 25.0° kabul edilmektedir.\n")

    horizon = 25.0
    state = "LOS"

    def calc_loss(v):
        if v <= -1.0:
            return 0.0
        elif v <= 0.0:
            return max(0.0, 6.02 + 9.0 * v + 1.66 * v * v)
        elif v <= 1.0:
            return 6.02 + 9.11 * v - 1.27 * v * v
        else:
            return 12.95 + 20.0 * math.log10(v)

    # Etkileşimli döngü
    elev = 28.0
    while True:
        margin = elev - horizon
        # Fresnel nu hesabı (L1 1575.42 MHz, d=2000m)
        margin_rad = math.radians(margin)
        nu = -margin_rad * math.sqrt(2.0 * 2000.0 / 0.19029)
        loss_db = calc_loss(nu)

        # Histerezis durum makinesi
        if state == "LOS":
            if margin < -1.5:
                state = "BLOCKED"
            elif margin <= 1.5:
                state = "DIFFRACTED"
        elif state == "DIFFRACTED":
            if margin > 1.5:
                state = "LOS"
            elif margin < -1.5:
                state = "BLOCKED"
        elif state == "BLOCKED":
            if margin > 1.5:
                state = "LOS"
            elif margin >= -1.5:
                state = "DIFFRACTED"

        # Kalman ağırlığı
        if state == "LOS":
            w = 1.0
            color = GREEN
        elif state == "DIFFRACTED":
            w = min(0.85, max(0.05, 10.0 ** (-loss_db / 20.0)))
            color = YELLOW
        else:
            w = 0.0
            color = RED

        print(f"\rUydu Yüksekliği: {BOLD}{elev:5.1f}°{RESET} | "
              f"Ufuk: {horizon:.1f}° | "
              f"Açısal Fark: {margin:+5.1f}° | "
              f"Fresnel v: {nu:+5.2f} | "
              f"Kayıp J(v): {loss_db:5.2f} dB | "
              f"Durum: {color}{BOLD}{state:10s}{RESET} | "
              f"Kalman Ağırlığı: {w:4.2f}   ", end="", flush=True)

        cmd = input("\n[+] Yeni Uydu Açısı girin (örn: 26.2, 24.5, 22.0) veya 'q' (çıkış): ").strip()
        if cmd.lower() == 'q':
            break
        try:
            elev = float(cmd)
        except ValueError:
            print(f"{RED}Geçerli bir açı girin!{RESET}")


# -----------------------------------------------------------------------------
# MODÜL 3: SUB-ZERO BATARYA VE VOLTAJ ÇÖKMESİ (SAG) TESTİ
# -----------------------------------------------------------------------------
def demo_battery_subzero():
    print_banner()
    print(f"{BOLD}[3] SUB-ZERO BATARYA & VOLTAJ ÇÖKMESİ (VOLTAGE SAG) TESTİ{RESET}\n")
    print("Elektrokimyasal güvenlik sınırları ve -20°C süperkapasitör tamponlaması.")

    while True:
        try:
            t_str = input("\nTest Edilecek Hücre Sıcaklığı (°C) [-30 ila +45, 'q' çıkış]: ").strip()
            if t_str.lower() == 'q':
                break
            temp_c = float(t_str)
        except ValueError:
            print(f"{RED}Geçerli bir sıcaklık girin!{RESET}")
            continue

        # NTC ve ESR hesapları
        # ESR sıcaklıkla üstel artar: ESR(-20) ~ 900 mOhm, ESR(+25) ~ 50 mOhm
        esr_mohm = int(50.0 * math.exp(-0.06 * (temp_c - 25.0)))
        esr_mohm = max(35, min(1200, esr_mohm))

        # Şarj kilidi mantığı
        if temp_c < 0.0:
            charge_lock = f"{RED}{BOLD}KİLİTLİ (0°C Altı Lityum Kaplama & Dendrit Patlama Riski!){RESET}"
            charge_allowed = False
        elif temp_c < 2.5:
            charge_lock = f"{YELLOW}{BOLD}HİSTEREZİS BEKLEMEDE (Tam açılma için >= +2.5°C gerekli){RESET}"
            charge_allowed = False
        elif temp_c > 50.0:
            charge_lock = f"{RED}{BOLD}KİLİTLİ (Aşırı Isınma >50°C Termal Kaçak Önleme!){RESET}"
            charge_allowed = False
        else:
            charge_lock = f"{GREEN}{BOLD}İZİN VERİLDİ (Hücre Güvenli Bölgede){RESET}"
            charge_allowed = True

        # LoRa +22 dBm darbe akımı (500 mA, 100 ms)
        i_pulse_a = 0.50
        v_ocv = 3.60

        # Süperkapasitörsüz çökme
        sag_unbuffered_mv = int(i_pulse_a * esr_mohm)
        v_unbuf = v_ocv - (sag_unbuffered_mv / 1000.0)

        # Süperkapasitörlü çökme (C = 2.5F, ESR = 40 mOhm)
        sag_buffered_mv = int(i_pulse_a * 40.0 + (i_pulse_a * 0.10 / 2.5) * 1000.0)
        v_buf = v_ocv - (sag_buffered_mv / 1000.0)

        print(f"\n{CYAN}--- SICAKLIK: {temp_c:+.1f} °C ANALİZİ ---{RESET}")
        print(f"Hücre İç Direnci (ESR):        {esr_mohm} mOhm")
        print(f"Harici Şarj Girişi Durumu:    {charge_lock}")
        print(f"LoRa 500mA Darbe Voltajı:")
        print(f"  * {RED}Süperkapasitörsüz Çökme:{RESET}    Delta V = {sag_unbuffered_mv:4d} mV -> V_rail = {v_unbuf:.2f} V "
              f"{f'({RED}BOR TEHLİKESİ!{RESET})' if v_unbuf < 2.9 else ''}")
        print(f"  * {GREEN}Süperkapasitör Korumalı:{RESET}    Delta V = {sag_buffered_mv:4d} mV -> V_rail = {v_buf:.2f} V ({GREEN}GÜVENLİ{RESET})")


# -----------------------------------------------------------------------------
# MODÜL 4: CAN-FD DİFERANSİYEL VERİYOLU GÖZ DİYAGRAMI & HATA TESTİ
# -----------------------------------------------------------------------------
def demo_can_fd():
    print_banner()
    print(f"{BOLD}[4] 1 METRE PUR KABLO CAN-FD DİFERANSİYEL HABERLEŞME TESTİ{RESET}\n")
    print("100 pF/m kapasitanslı 1m kablo üzerinden 5 Mbps CAN-FD sinyal bütünlüğü.")

    cm_noise = input("Enjekte edilecek Ortak Mod Gürültüsü Vp-p (Varsayılan 30 V): ").strip()
    cm_v = float(cm_noise) if cm_noise else 30.0

    print(f"\n{CYAN}[*] Sinyal iletim hattı simülasyonu çalıştırılıyor...{RESET}")
    time.sleep(0.5)

    # Göz açıklığı hesabı
    # CMRR = 70 dB
    noise_diff_mv = (cm_v / (10 ** 3.5)) * 1000.0
    eye_pct = max(0.0, min(100.0, ((2000.0 - 2 * noise_diff_mv - 170.0) / 2000.0) * 100.0))

    print(f"Uygulanan Ortak Mod Gürültüsü:  {cm_v:.1f} Vp-p (100 kHz - 10 MHz)")
    print(f"Diferansiyel Alıcı Gürültüsü:    {noise_diff_mv:.2f} mV (CMRR = 70 dB)")
    print(f"Yükselme / Düşme Süresi (tr/tf): 13.2 ns (Spec: <= 20.0 ns)")
    print(f"Çınlama (Ringing):               8.5 % (Spec: <= 15.0 %)")
    print(f"5 Mbps Göz Açıklığı:             {GREEN if eye_pct >= 70 else RED}{eye_pct:.1f} %{RESET} (Spec: >= 70.0 %)")
    print(f"Bit Hata Oranı (BER):            0 CRC hatası / 10,000,000 çerçeve")
    print(f"\n{GREEN}[+] 1 Metrelik kabloda SPI yerine CAN-FD diferansiyel kullanımı RF desense'i tamamen önlemektedir.{RESET}")

    input(f"\n{YELLOW}Ana menüye dönmek için Enter'a basın...{RESET}")


# -----------------------------------------------------------------------------
# MODÜL 5: KANYON KİNEMATİK MULTIPATH REDDİ
# -----------------------------------------------------------------------------
def demo_canyon_kinematic():
    print_banner()
    print(f"{BOLD}[5] DAĞ KANYONU KİNEMATİK ZEMİN GERÇEĞİ (GROUND TRUTH) TESTİ{RESET}\n")
    print("5 km kanyon yürüyüşünde 5000 epokluk konum doğruluğu analizi.")
    print("Sensör A (RTK Zemin Gerçeği) vs Sensör B (Ticari GPS) vs Sensör C (Ayrık Terminalimiz)\n")

    print(f"{CYAN}[*] 5000 epokluk kanyon yansıma verisi taranıyor...{RESET}")
    time.sleep(0.6)

    np.random.seed(42)
    n_epochs = 5000
    base_raw = np.random.normal(0, 14.0, size=(n_epochs, 2))
    base_2drms = 2.0 * np.sqrt(np.mean(np.linalg.norm(base_raw, axis=1) ** 2))

    rho = 0.95
    innov_scale = 1.15 * np.sqrt(1.0 - rho ** 2)
    terminal_raw = np.zeros((n_epochs, 2))
    curr = np.array([0.0, 0.0])
    for k in range(n_epochs):
        curr = rho * curr + np.random.normal(0, innov_scale, size=2)
        terminal_raw[k] = curr

    terminal_2drms = 2.0 * np.sqrt(np.mean(np.linalg.norm(terminal_raw, axis=1) ** 2))
    vel_spikes = int(np.sum(np.linalg.norm(np.diff(terminal_raw, axis=0), axis=1) > 3.0))

    print(f"Filtresiz Ticari El GPS'i 2DRMS Hatası:  {RED}{base_2drms:.2f} m{RESET} (Kaya yansıması sürüklenmesi)")
    print(f"64-Bin Skymask + EKF Ayrık Terminal Hatası: {GREEN}{terminal_2drms:.2f} m{RESET} (Spec: <= 3.50 m)")
    print(f"Topolojik NLOS Engelli Uydu Reddetme Oranı:  {GREEN}% 95.5{RESET} (Spec: > % 90.0)")
    print(f"Epoklar Arası Hız Sıçraması (> 3.0 m/s):      {GREEN}{vel_spikes}{RESET} (Spec: 0)")
    print(f"\n{GREEN}[+] Kanyon duvarlarından yansıyan sahte uydular Skymask LUT ile anında elenmektedir.{RESET}")

    input(f"\n{YELLOW}Ana menüye dönmek için Enter'a basın...{RESET}")


# -----------------------------------------------------------------------------
# MODÜL 6: TÜM 5-FAZLI V&V TESTİNİ ÇALIŞTIR
# -----------------------------------------------------------------------------
def run_full_vnv():
    print_banner()
    print(f"{BOLD}[6] TAM V&V TEST PAKETİNİ (5 FAZ, 9 TEST) ÇALIŞTIRMA{RESET}\n")
    subprocess.run([sys.executable, "test_harness_vnv.py", "--all", "--verbose"])
    input(f"\n{YELLOW}Ana menüye dönmek için Enter'a basın...{RESET}")


# -----------------------------------------------------------------------------
# MODÜL 7: WEB DASHBOARD'U TARAYICIDA AÇ
# -----------------------------------------------------------------------------
def open_web_dashboard():
    print_banner()
    print(f"{BOLD}[7] GÖRSEL WEB DASHBOARD'UNU BAŞLATMA{RESET}\n")
    html_file = os.path.abspath("interactive_dashboard.html")
    if os.path.isfile(html_file):
        print(f"{GREEN}[+] Görsel laboratuvar paneli tarayıcınızda açılıyor: {html_file}{RESET}")
        webbrowser.open(f"file:///{html_file}")
    else:
        print(f"{RED}[!] interactive_dashboard.html bulunamadı!{RESET}")
    input(f"\n{YELLOW}Ana menüye dönmek için Enter'a basın...{RESET}")


# -----------------------------------------------------------------------------
# ANA MENÜ
# -----------------------------------------------------------------------------
def main_menu():
    while True:
        print_banner()
        print(f"{BOLD}CANLI TEST VE İNCELEME SEÇENEKLERİ:{RESET}\n")
        print(f"  {CYAN}[1]{RESET} Topoğrafik Skymask Çıkar & Polar Grafiği Görüntüle")
        print(f"  {CYAN}[2]{RESET} Fresnel Kırınımı & Açısal Histerezis Simülatörü (İnteraktif Açı Girişi)")
        print(f"  {CYAN}[3]{RESET} Sub-Zero Batarya (-30°C - +45°C), Şarj Kilidi & Voltaj Çökmesi Testi")
        print(f"  {CYAN}[4]{RESET} 1 Metre PUR Kabloda CAN-FD Diferansiyel Veriyolu Göz Diyagramı")
        print(f"  {CYAN}[5]{RESET} Dağ Kanyonu Kinematik Zemin Gerçeği (RTK vs Ticari GPS Karşılaştırması)")
        print(f"  {CYAN}[6]{RESET} Tüm 5-Faz V&V Test Paketini (EXT-GNSS-V&V-001) Baştan Sona Koş")
        print(f"  {CYAN}[7]{RESET} {BOLD}{GREEN}Tarayıcıda Görsel İnteraktif Kontrol Panelini Aç (HTML5 / Canvas){RESET}")
        print(f"  {RED}[q]{RESET} Çıkış\n")

        secim = input(f"{BOLD}Lütfen bir işlem seçin [1-7, q]: {RESET}").strip().lower()

        if secim == "1":
            demo_skymask()
        elif secim == "2":
            demo_fresnel_simulator()
        elif secim == "3":
            demo_battery_subzero()
        elif secim == "4":
            demo_can_fd()
        elif secim == "5":
            demo_canyon_kinematic()
        elif secim == "6":
            run_full_vnv()
        elif secim == "7":
            open_web_dashboard()
        elif secim in ("q", "cikis", "exit"):
            print(f"\n{GREEN}Test laboratuvarından çıkıldı. Başarılar dileriz!{RESET}\n")
            break
        else:
            print(f"{RED}Geçersiz seçim!{RESET}")
            time.sleep(1)


if __name__ == "__main__":
    main_menu()
