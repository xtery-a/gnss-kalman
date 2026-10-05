#!/usr/bin/env python3
"""
Verification script for Hybrid Tactical Communications & Blue Force Tracking (BFT).
Compiles and executes test_harness_hybrid_comms.c with MinGW GCC.
"""

import os
import subprocess
import sys
import unittest

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TESTS_DIR = os.path.join(ROOT_DIR, "tests")
SRC_DIR = os.path.join(ROOT_DIR, "src")
INC_DIR = os.path.join(ROOT_DIR, "include")
BUILD_DIR = os.path.join(TESTS_DIR, "build")
GCC_PATH = r"C:\msys64\mingw64\bin"


class TestHybridComms(unittest.TestCase):
    def setUp(self):
        import shutil
        os.makedirs(BUILD_DIR, exist_ok=True)
        self.env = os.environ.copy()
        if os.path.exists(GCC_PATH):
            self.env["PATH"] = GCC_PATH + os.pathsep + self.env.get("PATH", "")
        self.gcc_bin = shutil.which("gcc", path=self.env.get("PATH")) or "gcc"

    def test_hybrid_comms_c_execution(self):
        c_exe = os.path.join(BUILD_DIR, "test_hybrid_comms.exe")
        harness_c = os.path.join(TESTS_DIR, "test_harness_hybrid_comms.c")
        comms_c = os.path.join(SRC_DIR, "hybrid_comms.c")
        crypto_c = os.path.join(SRC_DIR, "crypto_hal.c")

        compile_cmd = f'"{self.gcc_bin}" -O2 -Wall -Wextra -I"{INC_DIR}" "{harness_c}" "{comms_c}" "{crypto_c}" -lm -o "{c_exe}"'
        res_comp = subprocess.run(compile_cmd, shell=True, env=self.env, capture_output=True, text=True)
        self.assertEqual(res_comp.returncode, 0, f"Compilation error: {res_comp.stderr}")

        res_exec = subprocess.run(f'"{c_exe}"', shell=True, env=self.env, capture_output=True, text=True)
        self.assertEqual(res_exec.returncode, 0, f"Execution failed:\n{res_exec.stderr}\n{res_exec.stdout}")
        self.assertIn("ALL HYBRID COMMS & BFT TESTS PASSED PERFECTLY", res_exec.stdout)
        print("\n" + res_exec.stdout)


if __name__ == "__main__":
    unittest.main()
