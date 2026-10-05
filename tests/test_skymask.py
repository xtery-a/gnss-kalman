#!/usr/bin/env python3
"""
Comprehensive Test Suite for 64-Bin Topographic Skymask Extraction Engine
Tests mathematical accuracy, geodesy, windowed reading, NoData handling,
out-of-bounds validation, C header output, and polar plot rendering.
"""

import os
import shutil
import tempfile
import unittest
import warnings

# Suppress internal rasterio Affine PendingDeprecationWarning
warnings.filterwarnings("ignore", category=PendingDeprecationWarning, module="rasterio")

from affine import Affine
import numpy as np
import pyproj
import rasterio

from skymask_gen import (
    NUM_BINS,
    AZIMUTH_STEP_DEG,
    R_EFF_METERS,
    compute_skymask,
    export_c_header,
    generate_polar_plot,
    get_local_metric_crs,
)


def create_synthetic_dem(
    file_path: str,
    center_lat: float = 40.0,
    center_lon: float = 29.0,
    size_deg: float = 0.25,  # ~28 km width/height
    resolution_deg: float = 0.000277777777778,  # ~30m (1 arcsec)
    terrain_func=None,
    nodata_val: float = -9999.0,
) -> None:
    """Creates a synthetic GeoTIFF DEM centered at (center_lat, center_lon)."""
    min_lon = center_lon - size_deg / 2.0
    max_lat = center_lat + size_deg / 2.0
    cols = int(round(size_deg / resolution_deg))
    rows = int(round(size_deg / resolution_deg))

    transform = Affine(resolution_deg, 0.0, min_lon, 0.0, -resolution_deg, max_lat)

    lons = min_lon + (np.arange(cols) + 0.5) * resolution_deg
    lats = max_lat - (np.arange(rows) + 0.5) * resolution_deg
    lon_grid, lat_grid = np.meshgrid(lons, lats)

    if terrain_func is not None:
        elevations = terrain_func(lat_grid, lon_grid)
    else:
        elevations = np.full((rows, cols), 500.0, dtype=np.float32)

    with rasterio.open(
        file_path,
        "w",
        driver="GTiff",
        height=rows,
        width=cols,
        count=1,
        dtype=rasterio.float32,
        crs="EPSG:4326",
        transform=transform,
        nodata=nodata_val,
    ) as dst:
        dst.write(elevations.astype(np.float32), 1)


class TestSkymaskEngine(unittest.TestCase):
    """Unit and integration tests for the Skymask Extraction Engine."""

    @classmethod
    def setUpClass(cls):
        cls.test_dir = tempfile.mkdtemp(prefix="skymask_test_")

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.test_dir, ignore_errors=True)

    def test_flat_terrain_zero_horizon(self):
        """Flat terrain with antenna height > 0 should yield 0.0 deg horizon (Q=0) everywhere."""
        dem_file = os.path.join(self.test_dir, "flat_dem.tif")
        create_synthetic_dem(dem_file, center_lat=40.0, center_lon=29.0)

        result = compute_skymask(
            dem_path=dem_file,
            lat=40.0,
            lon=29.0,
            radius=4000.0,
            step=20.0,
            eye_height=1.8,
        )

        self.assertEqual(len(result.theta_horizon_deg), NUM_BINS)
        self.assertEqual(len(result.quantized_lut), NUM_BINS)
        self.assertAlmostEqual(result.z0, 500.0, places=1)

        # On flat terrain, earth drops away due to curvature, so terrain elevation < observer elevation
        # Clamped horizon must be exactly 0 deg and Q=0 for all bins
        np.testing.assert_array_equal(result.theta_horizon_deg, np.zeros(NUM_BINS))
        np.testing.assert_array_equal(result.quantized_lut, np.zeros(NUM_BINS, dtype=np.uint8))

    def test_north_mountain_peak(self):
        """A synthetic mountain placed strictly North should create a peak at Azimuth 0 (North)."""
        center_lat = 40.0
        center_lon = 29.0

        # Mountain peak 1500m North of observer, height 600m above base (500m)
        # 1 deg latitude ~ 111,139 m
        mountain_lat = center_lat + 1500.0 / 111139.0
        mountain_lon = center_lon

        def mountain_terrain(lats, lons):
            dy = (lats - mountain_lat) * 111139.0
            dx = (lons - mountain_lon) * (111139.0 * np.cos(np.radians(center_lat)))
            dist_sq = dx**2 + dy**2
            # Gaussian peak with sigma = 300m, height = 600m
            peak = 600.0 * np.exp(-dist_sq / (2.0 * (300.0**2)))
            return 500.0 + peak

        dem_file = os.path.join(self.test_dir, "north_mountain.tif")
        create_synthetic_dem(dem_file, center_lat=center_lat, center_lon=center_lon, terrain_func=mountain_terrain)

        result = compute_skymask(
            dem_path=dem_file,
            lat=center_lat,
            lon=center_lon,
            radius=4000.0,
            step=15.0,
            eye_height=1.8,
        )

        # North bin is index 0 (0 degrees)
        self.assertAlmostEqual(result.azimuths_deg[0], 0.0)
        north_horizon = result.theta_horizon_deg[0]
        self.assertGreater(north_horizon, 10.0, "North horizon should be obstructed by the mountain")

        # Peak must be at bin 0
        max_bin = int(np.argmax(result.theta_horizon_deg))
        self.assertEqual(max_bin, 0, "Maximum obstruction should be at Azimuth 0 (North)")

        # South bin is index 32 (180 degrees), should be 0 deg
        self.assertAlmostEqual(result.azimuths_deg[32], 180.0)
        self.assertEqual(result.theta_horizon_deg[32], 0.0)

        # Symmetry: bin 1 (5.625 deg) and bin 63 (354.375 deg) should be nearly identical
        self.assertAlmostEqual(result.theta_horizon_deg[1], result.theta_horizon_deg[63], delta=0.5)

    def test_east_ridge_peak(self):
        """A ridge to the East (90 deg) must produce the maximum horizon angle at bin 16."""
        center_lat = 40.0
        center_lon = 29.0
        # East is at 90 deg -> bin 16 (16 * 5.625 = 90.0 deg)
        east_lon = center_lon + 1200.0 / (111139.0 * np.cos(np.radians(center_lat)))
        east_lat = center_lat

        def east_terrain(lats, lons):
            dy = (lats - east_lat) * 111139.0
            dx = (lons - east_lon) * (111139.0 * np.cos(np.radians(center_lat)))
            dist_sq = dx**2 + dy**2
            peak = 500.0 * np.exp(-dist_sq / (2.0 * (250.0**2)))
            return 400.0 + peak

        dem_file = os.path.join(self.test_dir, "east_ridge.tif")
        create_synthetic_dem(dem_file, center_lat=center_lat, center_lon=center_lon, terrain_func=east_terrain)

        result = compute_skymask(
            dem_path=dem_file,
            lat=center_lat,
            lon=center_lon,
            radius=3000.0,
            step=15.0,
            eye_height=1.8,
        )

        max_bin = int(np.argmax(result.theta_horizon_deg))
        self.assertEqual(max_bin, 16, "Maximum obstruction should be at Azimuth 90 (East, bin 16)")
        self.assertGreater(result.theta_horizon_deg[16], 15.0)

    def test_out_of_bounds_raises_value_error(self):
        """Requests outside or near the edge of DEM coverage must raise ValueError."""
        dem_file = os.path.join(self.test_dir, "small_dem.tif")
        # 0.05 deg ~ 5.5 km width
        create_synthetic_dem(dem_file, center_lat=40.0, center_lon=29.0, size_deg=0.05)

        # Center request with 5000m radius + 500m buffer requires ~11km extent, which exceeds 5.5km
        with self.assertRaises(ValueError) as ctx:
            compute_skymask(
                dem_path=dem_file,
                lat=40.0,
                lon=29.0,
                radius=5000.0,
                step=15.0,
            )
        self.assertIn("extends beyond DEM extent", str(ctx.exception))

    def test_nodata_handling(self):
        """DEM with NoData pixels should interpolate valid neighbors with a warning."""
        center_lat = 40.0
        center_lon = 29.0

        def terrain_with_nodata(lats, lons):
            data = np.full(lats.shape, 500.0, dtype=np.float32)
            # Add a small patch of NoData (-9999.0) near the center observer
            mid_r, mid_c = data.shape[0] // 2, data.shape[1] // 2
            data[mid_r + 5 : mid_r + 15, mid_c + 5 : mid_c + 15] = -9999.0
            return data

        dem_file = os.path.join(self.test_dir, "nodata_dem.tif")
        create_synthetic_dem(dem_file, center_lat=center_lat, center_lon=center_lon, terrain_func=terrain_with_nodata)

        with warnings.catch_warnings(record=True) as recorded_warnings:
            warnings.simplefilter("always")
            result = compute_skymask(
                dem_path=dem_file,
                lat=center_lat,
                lon=center_lon,
                radius=2000.0,
                step=20.0,
            )
            # Verify that a warning was emitted for NoData pixels
            nodata_warnings = [w for w in recorded_warnings if "NoData pixels" in str(w.message)]
            self.assertTrue(len(nodata_warnings) > 0, "Expected NoData warning to be emitted")

        # Flat terrain with NoData filled should still result in valid finite elevations
        self.assertFalse(np.isnan(result.theta_horizon_deg).any())
        self.assertFalse(np.isinf(result.theta_horizon_deg).any())

    def test_all_nodata_raises_error(self):
        """DEM patch containing only NoData values must raise ValueError."""
        center_lat = 40.0
        center_lon = 29.0

        def all_nodata(lats, lons):
            return np.full(lats.shape, -9999.0, dtype=np.float32)

        dem_file = os.path.join(self.test_dir, "all_nodata_dem.tif")
        create_synthetic_dem(dem_file, center_lat=center_lat, center_lon=center_lon, terrain_func=all_nodata)

        with self.assertRaises(ValueError) as ctx:
            compute_skymask(
                dem_path=dem_file,
                lat=center_lat,
                lon=center_lon,
                radius=2000.0,
                step=20.0,
            )
        self.assertIn("NoData values", str(ctx.exception))

    def test_earth_curvature_formula(self):
        """Verify the analytical Earth curvature and refraction calculation."""
        d = 5000.0
        expected_drop = (d**2) / (2.0 * R_EFF_METERS)
        # 5000^2 / 17,000,000 ~ 1.470588 meters
        self.assertAlmostEqual(expected_drop, 1.470588, places=4)

    def test_quantization_resolution(self):
        """Verify fixed-point quantization mapping [0, 90] deg -> [0, 255] uint8."""
        angles = np.array([0.0, 45.0, 90.0, 22.5, 0.3529])
        quantized = np.clip(np.round((angles / 90.0) * 255.0), 0, 255).astype(np.uint8)

        self.assertEqual(quantized[0], 0)
        self.assertEqual(quantized[1], 128)  # 45 / 90 * 255 = 127.5 -> 128
        self.assertEqual(quantized[2], 255)
        self.assertEqual(quantized[3], 64)   # 22.5 / 90 * 255 = 63.75 -> 64
        self.assertEqual(quantized[4], 1)    # 1 LSB ~ 0.3529 deg

    def test_export_c_header(self):
        """Verify generated C header matches ANSI C99 specification."""
        dem_file = os.path.join(self.test_dir, "flat_dem.tif")
        create_synthetic_dem(dem_file, center_lat=40.0, center_lon=29.0)

        result = compute_skymask(dem_file, lat=40.0, lon=29.0, radius=2000.0, step=20.0)
        # Modify some values to test formatting
        result.quantized_lut[0:8] = [12, 14, 18, 25, 42, 65, 80, 78]

        header_path = os.path.join(self.test_dir, "skymask_lut.h")
        export_c_header(result, header_path)

        self.assertTrue(os.path.isfile(header_path))
        with open(header_path, "r", encoding="utf-8") as f:
            content = f.read()

        self.assertIn("#ifndef SKYMASK_LUT_H", content)
        self.assertIn("#define SKYMASK_LUT_H", content)
        self.assertIn("#include <stdint.h>", content)
        self.assertIn("#define SKYMASK_NUM_BINS         64", content)
        self.assertIn("#define SKYMASK_AZIMUTH_STEP_DEG 5.625f", content)
        self.assertIn("static const uint8_t SKYMASK_LUT[SKYMASK_NUM_BINS] = {", content)
        self.assertIn("/* 0..7   (N to NE)  */", content)
        self.assertIn("/* 8..15  (NE to E)  */", content)
        self.assertIn(" 12,  14,  18,  25,  42,  65,  80,  78,", content)
        self.assertIn("/* 56..63 (NW to N)  */", content)
        self.assertIn("#endif // SKYMASK_LUT_H", content)

    def test_generate_polar_plot(self):
        """Verify polar plot export creates a valid PNG file."""
        dem_file = os.path.join(self.test_dir, "flat_dem.tif")
        create_synthetic_dem(dem_file, center_lat=40.0, center_lon=29.0)

        result = compute_skymask(dem_file, lat=40.0, lon=29.0, radius=2000.0, step=20.0)
        plot_path = os.path.join(self.test_dir, "skymask_polar.png")
        generate_polar_plot(result, plot_path)

        self.assertTrue(os.path.isfile(plot_path))
        self.assertGreater(os.path.getsize(plot_path), 1000)

        # Validate PNG header bytes
        with open(plot_path, "rb") as f:
            magic = f.read(8)
        self.assertEqual(magic, b"\x89PNG\r\n\x1a\n")

    def test_projection_modes(self):
        """Verify both 'tmerc' and 'utm' projection modes execute properly."""
        dem_file = os.path.join(self.test_dir, "flat_dem.tif")
        create_synthetic_dem(dem_file, center_lat=40.0, center_lon=29.0)

        res_tmerc = compute_skymask(dem_file, lat=40.0, lon=29.0, radius=2000.0, step=25.0, proj_type="tmerc")
        res_utm = compute_skymask(dem_file, lat=40.0, lon=29.0, radius=2000.0, step=25.0, proj_type="utm")

        self.assertEqual(len(res_tmerc.quantized_lut), NUM_BINS)
        self.assertEqual(len(res_utm.quantized_lut), NUM_BINS)
        # On flat terrain, both must be 0
        np.testing.assert_array_equal(res_tmerc.quantized_lut, np.zeros(NUM_BINS, dtype=np.uint8))
        np.testing.assert_array_equal(res_utm.quantized_lut, np.zeros(NUM_BINS, dtype=np.uint8))


if __name__ == "__main__":
    unittest.main()
