#!/usr/bin/env python3
"""
open_weather_lab.py - Zero-Config Hardware Bridge & Launcher for MET-SURV OPS:
Auto-detects ESP32 on COM4, streams raw NMEA to web UI via /api/gps,
and provides seamless, popup-free live monitoring in any browser.
"""

import os
import sys
import json
import socket
import threading
import time
import webbrowser
from collections import deque
from http.server import HTTPServer, SimpleHTTPRequestHandler
import serial
import serial.tools.list_ports

PORT = 8080
BAUD = 115200

# Shared telemetry state
serial_lines = deque(maxlen=60)
serial_lock = threading.Lock()
serial_connected = False
active_port = None

def find_esp32_port():
    ports = serial.tools.list_ports.comports()
    for p in ports:
        # CH340, CP210x, or ESP32 USB CDC
        desc = (p.description or "").lower()
        if "ch340" in desc or "cp210" in desc or "serial" in desc or "usb" in desc:
            return p.device
    return "COM4"

LOGS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "logs")
os.makedirs(LOGS_DIR, exist_ok=True)
LATEST_LOG_PATH = os.path.join(LOGS_DIR, "gnss_session_latest.csv")
log_sample_idx = 0
last_logged_utc = ""

def parse_nmea_coord(raw, direction):
    if not raw or len(raw) < 4:
        return None
    try:
        dot = raw.find('.')
        deg_len = 2 if (dot == 4 or dot == -1 and len(raw) <= 4) else 3
        deg = float(raw[:deg_len])
        mins = float(raw[deg_len:])
        val = deg + (mins / 60.0)
        if direction in ('S', 'W'):
            val = -val
        return round(val, 7)
    except Exception:
        return None

def auto_log_nmea_fix(line):
    global log_sample_idx, last_logged_utc
    if not line.startswith('$GNGGA') and not line.startswith('$GPGGA'):
        return
    parts = line.split(',')
    if len(parts) < 10:
        return
    utc = parts[1]
    if utc == last_logged_utc:
        return
    last_logged_utc = utc
    lat = parse_nmea_coord(parts[2], parts[3])
    lon = parse_nmea_coord(parts[4], parts[5])
    qual = parts[6]
    sats = parts[7]
    hdop = parts[8]
    alt = parts[9]
    if lat is None or lon is None or qual == '0':
        return

    # Check if header needs to be written
    write_header = not os.path.exists(LATEST_LOG_PATH) or os.path.getsize(LATEST_LOG_PATH) == 0
    try:
        with open(LATEST_LOG_PATH, 'a', encoding='utf-8') as f:
            if write_header:
                f.write("Index,Timestamp_UTC,Latitude,Longitude,Altitude_m,HDOP,Satellites,FixQuality\n")
            log_sample_idx += 1
            f.write(f"{log_sample_idx},{utc},{lat:.7f},{lon:.7f},{alt},{hdop},{sats},{qual}\n")
    except Exception:
        pass

active_serial = None
serial_write_lock = threading.Lock()
current_ubx_profile = "stationary"

def ubx_checksum(payload_bytes):
    ck_a = 0
    ck_b = 0
    for b in payload_bytes:
        ck_a = (ck_a + b) & 0xFF
        ck_b = (ck_b + ck_a) & 0xFF
    return ck_a, ck_b

def build_ubx_packet(msg_class, msg_id, payload):
    length = len(payload)
    header = bytes([msg_class, msg_id, length & 0xFF, (length >> 8) & 0xFF])
    ck_a, ck_b = ubx_checksum(header + payload)
    return b'\xb5\x62' + header + payload + bytes([ck_a, ck_b])

def send_ubx_profile(model_name):
    global current_ubx_profile
    import struct
    dyn_codes = {
        "portable": 0,
        "stationary": 2,
        "pedestrian": 3,
        "automotive": 4,
        "airborne": 6
    }
    code = dyn_codes.get(model_name.lower(), 0)

    # 1. UBX-CFG-NAV5 (Legacy/Universal across all u-blox series)
    payload_nav5 = bytearray(36)
    struct.pack_into('<H', payload_nav5, 0, 0x0001)  # mask: dynModel only
    payload_nav5[2] = code
    pkt_nav5 = build_ubx_packet(0x06, 0x24, bytes(payload_nav5))

    # 2. UBX-CFG-VALSET (Native u-blox M10 generation key 0x20110021)
    valset_payload = struct.pack('<BBHI B', 0, 1, 0, 0x20110021, code)
    pkt_valset = build_ubx_packet(0x06, 0x8A, valset_payload)

    sent = False
    with serial_write_lock:
        if active_serial and active_serial.is_open:
            try:
                active_serial.write(pkt_nav5)
                active_serial.write(pkt_valset)
                active_serial.flush()
                sent = True
                current_ubx_profile = model_name.lower()
                print(f"[UBX] TBS M10Q Dinamik Modu Guncellendi: {model_name.upper()} (Kod: {code})")
            except Exception as e:
                print(f"[!] UBX gonderim hatasi: {e}")
    return sent

def serial_reader_thread():
    global serial_connected, active_port, active_serial
    while True:
        port_name = active_port or find_esp32_port()
        try:
            with serial.Serial(port_name, BAUD, timeout=1.0) as ser:
                active_port = port_name
                active_serial = ser
                serial_connected = True
                print(f"[+] TBS M10Q GPS baglantisi aktif: {port_name} @ {BAUD}")
                send_ubx_profile(current_ubx_profile)
                while True:
                    line = ser.readline().decode('ascii', errors='ignore').strip()
                    if line and line.startswith('$'):
                        with serial_lock:
                            serial_lines.append(line)
                        auto_log_nmea_fix(line)
        except Exception:
            active_serial = None
            serial_connected = False
            time.sleep(2.0)

class GpsBridgeHandler(SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path.startswith('/api/set_profile'):
            import urllib.parse
            query = urllib.parse.urlparse(self.path).query
            params = urllib.parse.parse_qs(query)
            target_profile = params.get('profile', ['stationary'])[0]
            success = send_ubx_profile(target_profile)
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()
            resp = {
                "status": "ok" if success else "queued_or_disconnected",
                "profile": target_profile,
                "ubx_sent": success,
                "hardware": "TBS M10Q (u-blox MAX-M10S)"
            }
            self.wfile.write(json.dumps(resp).encode('utf-8'))
            return

        if self.path == '/api/gps':
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Access-Control-Allow-Origin', '*')
            self.send_header('Cache-Control', 'no-cache')
            self.end_headers()

            with serial_lock:
                current_batch = list(serial_lines)
                serial_lines.clear()

            payload = {
                "connected": serial_connected,
                "port": active_port,
                "lines": current_batch,
                "total_logged_samples": log_sample_idx,
                "ubx_profile": current_ubx_profile
            }
            self.wfile.write(json.dumps(payload).encode('utf-8'))
            return

        if self.path == '/api/download_csv':
            if os.path.exists(LATEST_LOG_PATH):
                self.send_response(200)
                self.send_header('Content-Type', 'text/csv; charset=utf-8')
                self.send_header('Content-Disposition', 'attachment; filename="gnss_live_session.csv"')
                self.send_header('Access-Control-Allow-Origin', '*')
                self.end_headers()
                with open(LATEST_LOG_PATH, 'rb') as f:
                    self.wfile.write(f.read())
            else:
                self.send_response(404)
                self.end_headers()
                self.wfile.write(b"No log data recorded yet")
            return

        if self.path == '/api/session_stats':
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()
            file_size = os.path.getsize(LATEST_LOG_PATH) if os.path.exists(LATEST_LOG_PATH) else 0
            stats = {
                "samples": log_sample_idx,
                "file_size_bytes": file_size,
                "file_path": LATEST_LOG_PATH
            }
            self.wfile.write(json.dumps(stats).encode('utf-8'))
            return

        # Serve static web files
        super().do_GET()

    def log_message(self, format, *args):
        pass # Keep output clean

def is_port_in_use(port):
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        return s.connect_ex(('127.0.0.1', port)) == 0

def main():
    if sys.platform == "win32":
        try:
            sys.stdout.reconfigure(encoding="utf-8")
        except Exception:
            pass

    curr_dir = os.path.dirname(os.path.abspath(__file__))
    os.chdir(curr_dir)

    # Start hardware reader in background thread
    t_serial = threading.Thread(target=serial_reader_thread, daemon=True)
    t_serial.start()

    target_url = f"http://127.0.0.1:{PORT}/tactical_terminal.html"

    print("=" * 70)
    print(" [MET-SURV OPS] TBS M10Q Canli GNSS Harita & Telemetri Terminali")
    print("=" * 70)

    try:
        if not is_port_in_use(PORT):
            server = HTTPServer(('127.0.0.1', PORT), GpsBridgeHandler)
            print(f"[+] Yerel Kopru & Sunucu: http://127.0.0.1:{PORT}")
            print(f"[+] Canli Harita Terminali:     http://127.0.0.1:{PORT}/gnss_tracker.html")
            print(f"[+] Garmin Taktik Terminali:    http://127.0.0.1:{PORT}/tactical_terminal.html\n")
            try:
                webbrowser.open(target_url)
            except Exception:
                pass
            server.serve_forever()
        else:
            print(f"[i] Port {PORT} uzerinde sunucu zaten calisiyor.")
            while True:
                time.sleep(1)
    except (KeyboardInterrupt, SystemExit):
        print("\n[!] Kopru kapatildi.")
    except Exception as e:
        print(f"\n[!] Sunucu istisnasi: {e}")

if __name__ == "__main__":
    main()
