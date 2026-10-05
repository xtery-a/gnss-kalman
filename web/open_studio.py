#!/usr/bin/env python3
"""
open_studio.py - One-Click Launcher for the CGPX Crypto & Visualizer Studio.
Opens the interactive tactical interface directly in your default browser.
"""

import os
import sys
import webbrowser

def main():
    if sys.platform == "win32":
        try:
            sys.stdout.reconfigure(encoding="utf-8")
        except Exception:
            pass
    curr_dir = os.path.dirname(os.path.abspath(__file__))
    html_path = os.path.join(curr_dir, "cgpx_studio.html")
    if not os.path.exists(html_path):
        print(f"Error: {html_path} not found.")
        sys.exit(1)
        
    print("=" * 65)
    print(" [LAUNCH] CGPX Taktik Kripto ve Gorsel Inceleme Studyosu Baslatiliyor...")
    print("=" * 65)
    print(f" Dosya: {html_path}")
    print(" Tarayiciniz otomatik olarak aciliyor...\n")
    
    webbrowser.open(f"file:///{html_path.replace(os.sep, '/')}")

if __name__ == "__main__":
    main()
