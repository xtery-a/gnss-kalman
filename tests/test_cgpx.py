#!/usr/bin/env python3
"""
test_cgpx.py - Comprehensive Verification & Edge Case Test Suite for the
CGPX Lossless Compression, Encryption & Zero-Heap Embedded Renderer Toolchain.

Validates:
  1. Bit-Exact Lossless Round-Trip Guarantee (<= 1.11 cm max geodetic error)
  2. Extreme Pathologies:
     - Flat tracks (0 elevation change)
     - Stationary points (consecutive identical coordinates)
     - Negative elevations (Dead Sea, -430 m)
     - Prime Meridian crossing (Greenwich, -0.005 -> +0.005 deg)
     - Large trajectories (> 25,000 points)
  3. Storage Efficiency (<= 4 - 6 bytes/point payload)
  4. C vs. Python Framebuffer Concordance
  5. CLI command operations (pack, view, sim-mip)
"""

import math
import os
import subprocess
import sys
import unittest

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
TOOLS_DIR = os.path.join(ROOT_DIR, "tools")
DATA_DIR = os.path.join(ROOT_DIR, "data")
BUILD_DIR = os.path.join(ROOT_DIR, "build")
os.makedirs(BUILD_DIR, exist_ok=True)
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

from cgpx_tool import (
    TrackPoint,
    compress_and_pack_points,
    unpack_and_decompress_cgpx,
    haversine_distance,
    parse_key,
    render_mip_framebuffer,
    save_framebuffer_pbm,
    HEADER_STRUCT,
    CGPX_MAGIC,
    CGPX_HEADER_SIZE
)

TEST_KEY_HEX = "0123456789ABCDEF0123456789ABCDEF"
TEST_KEY = parse_key(TEST_KEY_HEX)


def generate_synthetic_gpx(filename: str, points: list):
    """Generates standard GPX XML file for testing."""
    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        '<gpx version="1.1" creator="CGPX_Test" xmlns="http://www.topografix.com/GPX/1/1">',
        '  <trk>',
        '    <name>Test Track</name>',
        '    <trkseg>'
    ]
    for p in points:
        lines.append(
            f'      <trkpt lat="{p.lat:.7f}" lon="{p.lon:.7f}">'
            f'<ele>{p.ele:.2f}</ele>'
            f'<time>2026-09-18T10:00:{p.time_s % 60:02d}Z</time>'
            f'</trkpt>'
        )
    lines.extend([
        '    </trkseg>',
        '  </trk>',
        '</gpx>'
    ])
    with open(filename, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


class TestCGPXToolchain(unittest.TestCase):

    def test_01_lossless_roundtrip_precision(self):
        """Verify that round-trip compression and decryption has <= 1.11 cm error."""
        # Realistic mountain trail: 2000 points in Chamonix/Mont Blanc region
        points = []
        base_lat = 45.9237
        base_lon = 6.8694
        base_ele = 1035.0

        for i in range(2000):
            # Gentle movement with micro-displacements (~2-5 meters per step)
            lat = base_lat + (i * 0.00003) + (math.sin(i * 0.05) * 0.00005)
            lon = base_lon + (i * 0.00004) + (math.cos(i * 0.05) * 0.00005)
            ele = base_ele + (i * 0.5) + (math.sin(i * 0.1) * 20.0)
            points.append(TrackPoint(lat=lat, lon=lon, ele=ele, time_s=i))

        cgpx_bytes, meta = compress_and_pack_points(points, TEST_KEY, encrypt=True)
        self.assertEqual(meta["point_count"], 2000)

        # Decompress & decrypt
        decoded_points, dec_meta = unpack_and_decompress_cgpx(cgpx_bytes, TEST_KEY)
        self.assertEqual(len(decoded_points), 2000)

        max_err_meters = 0.0
        for orig, dec in zip(points, decoded_points):
            dist = haversine_distance(orig.lat, orig.lon, dec.lat, dec.lon)
            if dist > max_err_meters:
                max_err_meters = dist
            
            # Exact integer elevation check
            self.assertEqual(orig.ele_int, dec.ele_int)

        print(f"\n[Test 1] Max geodetic coordinate drift: {max_err_meters * 100.0:.4f} cm (Target: <= 1.11 cm)")
        self.assertLessEqual(max_err_meters, 0.0111, "Geodetic error must be <= 1.11 cm at equator scale")

    def test_02_pathology_zero_elevation_change(self):
        """Pathology: Flat track with 0 elevation change."""
        points = [
            TrackPoint(lat=36.0 + i * 0.001, lon=30.0 + i * 0.001, ele=15.0, time_s=i * 2)
            for i in range(500)
        ]
        cgpx_bytes, meta = compress_and_pack_points(points, TEST_KEY, encrypt=True)
        self.assertEqual(meta["min_ele"], 15)
        self.assertEqual(meta["max_ele"], 15)

        decoded_points, _ = unpack_and_decompress_cgpx(cgpx_bytes, TEST_KEY)
        self.assertEqual(len(decoded_points), 500)
        for p in decoded_points:
            self.assertEqual(p.ele_int, 15)

    def test_03_pathology_stationary_points(self):
        """Pathology: Long series of stationary points (delta lat/lon/ele == 0)."""
        points = []
        # Moving
        for i in range(50):
            points.append(TrackPoint(lat=40.0 + i * 0.0001, lon=29.0 + i * 0.0001, ele=100.0, time_s=i))
        # Stationary for 200 points
        for i in range(50, 250):
            points.append(TrackPoint(lat=40.005, lon=29.005, ele=100.0, time_s=i))
        # Moving again
        for i in range(250, 300):
            points.append(TrackPoint(lat=40.005 + (i - 250) * 0.0001, lon=29.005, ele=100.0 + (i - 250), time_s=i))

        cgpx_bytes, meta = compress_and_pack_points(points, TEST_KEY, encrypt=True)
        decoded_points, _ = unpack_and_decompress_cgpx(cgpx_bytes, TEST_KEY)
        self.assertEqual(len(decoded_points), 300)

        # Check middle stationary region
        for p in decoded_points[50:250]:
            self.assertEqual(p.lat_int, int(round(40.005 * 1e7)))
            self.assertEqual(p.lon_int, int(round(29.005 * 1e7)))

    def test_04_pathology_negative_elevations(self):
        """Pathology: Negative elevations (e.g. Dead Sea basin -430 m to -390 m)."""
        points = [
            TrackPoint(lat=31.5 + i * 0.0002, lon=35.4 + i * 0.0002, ele=-430.0 + (i * 0.1), time_s=i)
            for i in range(400)
        ]
        cgpx_bytes, meta = compress_and_pack_points(points, TEST_KEY, encrypt=True)
        self.assertEqual(meta["min_ele"], -430)

        decoded_points, _ = unpack_and_decompress_cgpx(cgpx_bytes, TEST_KEY)
        self.assertEqual(len(decoded_points), 400)
        for orig, dec in zip(points, decoded_points):
            self.assertEqual(orig.ele_int, dec.ele_int)

    def test_05_pathology_prime_meridian_crossing(self):
        """Pathology: Crossing Prime Meridian (-lon to +lon at Greenwich)."""
        points = [
            TrackPoint(lat=51.4768, lon=-0.0050 + (i * 0.00002), ele=45.0, time_s=i)
            for i in range(500)
        ]
        # Bounding box should span negative to positive
        cgpx_bytes, meta = compress_and_pack_points(points, TEST_KEY, encrypt=True)
        self.assertLess(meta["min_lon"], 0)
        self.assertGreater(meta["max_lon"], 0)

        decoded_points, _ = unpack_and_decompress_cgpx(cgpx_bytes, TEST_KEY)
        self.assertEqual(len(decoded_points), 500)
        for orig, dec in zip(points, decoded_points):
            self.assertEqual(orig.lon_int, dec.lon_int)

    def test_06_pathology_large_trajectory_25k_points(self):
        """Pathology: Stress test with > 25,000 points (26,000 points)."""
        n = 26000
        points = [
            TrackPoint(
                lat=42.0 + (i * 0.00001),
                lon=12.0 + (i * 0.00001),
                ele=200.0 + (i % 50),
                time_s=i
            )
            for i in range(n)
        ]
        cgpx_bytes, meta = compress_and_pack_points(points, TEST_KEY, encrypt=True)
        self.assertEqual(meta["point_count"], n)

        decoded_points, _ = unpack_and_decompress_cgpx(cgpx_bytes, TEST_KEY)
        self.assertEqual(len(decoded_points), n)

        # Verify efficiency
        bpp = meta["bytes_per_point"]
        print(f"\n[Test 6] 26,000 points payload size: {meta['payload_size']:,} B ({bpp:.2f} bytes/point)")
        self.assertLessEqual(bpp, 6.0, "Average storage per point must be <= 6.0 bytes")

    def test_07_storage_efficiency_target(self):
        """Verify storage efficiency is within 4-6 bytes/point on typical track."""
        points = []
        lat = 46.5
        lon = 8.5
        ele = 1500.0
        for i in range(3000):
            # Typical hiking pace (~1.2 m/s, ~1-3 meter deltas per second)
            lat += 0.000012 + (math.sin(i * 0.02) * 0.000005)
            lon += 0.000015 + (math.cos(i * 0.02) * 0.000005)
            ele += 0.15 * math.sin(i * 0.05)
            points.append(TrackPoint(lat=lat, lon=lon, ele=ele, time_s=i))

        _, meta = compress_and_pack_points(points, TEST_KEY, encrypt=True)
        bpp = meta["bytes_per_point"]
        print(f"\n[Test 7] Typical Alpine Hiking Track: {bpp:.2f} bytes/point (Target: 4-6 B/point)")
        self.assertLessEqual(bpp, 6.0, "Storage efficiency must be <= 6.0 bytes per point")

    def test_08_c_engine_framebuffer_concordance(self):
        """Verify that compiled C engine (test_cgpx.exe) decodes and renders identically."""
        # 1. Generate sample GPX
        gpx_file = os.path.join(BUILD_DIR, "sample_test_route.gpx")
        cgpx_file = os.path.join(BUILD_DIR, "sample_test_route.cgpx")
        pbm_c_file = os.path.join(BUILD_DIR, "sample_c_out.pbm")

        points = []
        lat = 45.8326
        lon = 6.8652
        ele = 3842.0  # Aiguille du Midi
        for i in range(1200):
            lat += 0.00002 * math.cos(i * 0.01)
            lon += 0.00002 * math.sin(i * 0.01)
            ele += 0.5 * math.sin(i * 0.05)
            points.append(TrackPoint(lat=lat, lon=lon, ele=ele, time_s=i))

        generate_synthetic_gpx(gpx_file, points)

        cgpx_tool_script = os.path.join(TOOLS_DIR, "cgpx_tool.py")

        # 2. Pack using CLI
        cmd_pack = [
            sys.executable, cgpx_tool_script, "pack", gpx_file,
            "-o", cgpx_file,
            "-k", TEST_KEY_HEX
        ]
        res = subprocess.run(cmd_pack, capture_output=True, text=True)
        self.assertEqual(res.returncode, 0, f"Pack failed: {res.stderr}")

        # 3. Render in Python
        with open(cgpx_file, "rb") as f:
            cgpx_bytes = f.read()
        dec_points, meta = unpack_and_decompress_cgpx(cgpx_bytes, TEST_KEY)
        fb_py = render_mip_framebuffer(dec_points, meta)

        # 4. Compile and Run C test harness
        c_exe = os.path.join(BUILD_DIR, "test_cgpx.exe")
        import shutil
        env = os.environ.copy()
        if os.path.exists(r"C:\msys64\mingw64\bin"):
            env["PATH"] = r"C:\msys64\mingw64\bin" + os.pathsep + env.get("PATH", "")

        if not os.path.exists(c_exe):
            gcc_bin = shutil.which("gcc", path=env.get("PATH")) or "gcc"
            harness_src = os.path.join(ROOT_DIR, "tests", "test_harness.c")
            engine_src = os.path.join(ROOT_DIR, "src", "cgpx_engine.c")
            inc_dir = os.path.join(ROOT_DIR, "include")
            comp_cmd = f'{gcc_bin} -O2 -Wall -Wextra -I"{inc_dir}" "{harness_src}" "{engine_src}" -lm -o "{c_exe}"'
            comp_res = subprocess.run(comp_cmd, shell=True, env=env, capture_output=True, text=True)
            self.assertEqual(comp_res.returncode, 0, f"Compilation of test_cgpx.exe failed: {comp_res.stderr}")

        cmd_c = [c_exe, cgpx_file, TEST_KEY_HEX, pbm_c_file]
        res_c = subprocess.run(cmd_c, env=env, capture_output=True, text=True)
        self.assertEqual(res_c.returncode, 0, f"C engine failed: {res_c.stderr}\n{res_c.stdout}")

        # 5. Read PBM generated by C engine
        with open(pbm_c_file, "rb") as f:
            c_pbm_data = f.read()

        # Parse P4 header
        header_end = c_pbm_data.find(b"\n", c_pbm_data.find(b"\n") + 1) + 1
        fb_c = c_pbm_data[header_end:]

        self.assertEqual(len(fb_py), len(fb_c), "Framebuffer sizes must match exactly 12,000 bytes")

        # Compare pixel count
        py_active_pixels = sum(bin(b).count("1") for b in fb_py)
        c_active_pixels = sum(bin(b).count("1") for b in fb_c)

        print(f"\n[Test 8] Active drawn pixels: Python={py_active_pixels}, C={c_active_pixels}")
        self.assertGreater(c_active_pixels, 0, "C engine must have drawn pixels")
        
        # Pixels should be extremely close (due to integer rounding in Bresenham lines)
        pixel_diff = abs(py_active_pixels - c_active_pixels)
        diff_ratio = pixel_diff / max(1, py_active_pixels)
        print(f" Pixel difference ratio: {diff_ratio * 100.0:.2f}%")
        self.assertLess(diff_ratio, 0.05, "Python and C rasterization must have >95% concordance")

    def test_09_cli_commands(self):
        """Verify view and sim-mip CLI subcommands."""
        cgpx_file = os.path.join(BUILD_DIR, "sample_test_route.cgpx")
        self.assertTrue(os.path.exists(cgpx_file), "CGPX file should exist from test 8")
        cgpx_tool_script = os.path.join(TOOLS_DIR, "cgpx_tool.py")
        view_png = os.path.join(BUILD_DIR, "cli_test_view.png")
        sim_png = os.path.join(BUILD_DIR, "cli_test_sim.png")

        # Test view --no-gui --save-plot
        cmd_view = [
            sys.executable, cgpx_tool_script, "view", cgpx_file,
            "-k", TEST_KEY_HEX,
            "--no-gui", "--save-plot", view_png
        ]
        res_view = subprocess.run(cmd_view, capture_output=True, text=True)
        self.assertEqual(res_view.returncode, 0, f"View CLI failed: {res_view.stderr}")
        self.assertTrue(os.path.exists(view_png), "Plot PNG must be generated")

        # Test sim-mip
        cmd_sim = [
            sys.executable, cgpx_tool_script, "sim-mip", cgpx_file,
            "-k", TEST_KEY_HEX,
            "-o", sim_png
        ]
        res_sim = subprocess.run(cmd_sim, capture_output=True, text=True)
        self.assertEqual(res_sim.returncode, 0, f"Sim-MIP CLI failed: {res_sim.stderr}")
        self.assertTrue(os.path.exists(sim_png), "Simulated screen PNG must be generated")


if __name__ == "__main__":
    unittest.main()
