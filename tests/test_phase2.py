#!/usr/bin/env python3
"""
test_phase2.py - Automated Python Verification Test for Phase 2:
MIP Display Driver, Dirty-Line DMA Engine & Chording Input State Machine.
"""

import os
import subprocess
import sys
import unittest


class TestPhase2DisplayAndChording(unittest.TestCase):

    def test_01_c_harness_execution(self):
        """Compiles and executes test_display.exe."""
        env = os.environ.copy()
        env["PATH"] = "C:\\msys64\\mingw64\\bin;" + env.get("PATH", "")

        # Compile
        compile_cmd = [
            "gcc.exe", "-O2", "-Wall", "-Wextra",
            "test_harness_display.c", "mip_display.c", "chord_fsm.c",
            "-o", "test_display.exe"
        ]
        res_comp = subprocess.run(compile_cmd, env=env, capture_output=True, text=True)
        self.assertEqual(res_comp.returncode, 0, f"Compilation failed: {res_comp.stderr}")

        # Execute
        res_exec = subprocess.run(["./test_display.exe"], env=env, capture_output=True, text=True)
        self.assertEqual(res_exec.returncode, 0, f"Execution failed: {res_exec.stderr}\n{res_exec.stdout}")

        # Check stdout assertions
        self.assertIn("[PASS] Full frame initialization: 240 lines packed", res_exec.stdout)
        self.assertIn("Saved: 99.55% bandwidth!", res_exec.stdout)
        self.assertIn("VCOM 1 Hz polarity toggle verified", res_exec.stdout)
        self.assertIn("100-stroke 2-pixel vector trajectory rendered", res_exec.stdout)
        self.assertIn("Mechanical contact bounce (< 50 ms) successfully filtered", res_exec.stdout)
        self.assertIn("Short Chord (SW1+SW2 = 0x03): Action = ZOOM_IN", res_exec.stdout)
        self.assertIn("Layer Toggle (SW2+SW3 = 0x06): Action = LAYER_TOGGLE", res_exec.stdout)
        self.assertIn("Long Chord Hold (SW4 = 0x08, >=800ms): Action = PAN_DOWN / NAV_PREV", res_exec.stdout)
        self.assertIn("Tactical SOS Beacon Hold (SW1+SW4 = 0x09, >=3000ms): Action = EMERGENCY_BEACON", res_exec.stdout)
        self.assertIn("ALL PHASE 2 TESTS PASSED PERFECTLY", res_exec.stdout)

    def test_02_pbm_output_conformance(self):
        """Validates that display_phase2.pbm adheres to Netpbm P4 standard."""
        pbm_path = "display_phase2.pbm"
        self.assertTrue(os.path.exists(pbm_path), "PBM file must exist")

        with open(pbm_path, "rb") as f:
            magic = f.readline().strip()
            self.assertEqual(magic, b"P4", "Must be binary P4 format")

            dims = f.readline().strip().split()
            w = int(dims[0])
            h = int(dims[1])
            self.assertEqual(w, 400)
            self.assertEqual(h, 240)

            raw_bytes = f.read()
            self.assertEqual(len(raw_bytes), 12000, "Must be exactly 12,000 bytes for 400x240/8")

            # Check that pixels are actually drawn (not blank)
            active_pixels = sum(bin(b).count("1") for b in raw_bytes)
            print(f"\n[Phase 2 Test] Active Drawn Pixels in PBM: {active_pixels:,} px")
            self.assertGreater(active_pixels, 1000, "Should have rendered status bars and trajectory")


if __name__ == "__main__":
    unittest.main()
