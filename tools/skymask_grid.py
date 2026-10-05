#!/usr/bin/env python3
"""
Dynamic 2D Grid Topographic Skymask Cache Generator (skymask_grid.py)
Generates a multi-tile spatial LUT cache (50m - 100m grid cells) from a DEM GeoTIFF
to eliminate single-point static skymask assumptions during field navigation.
Exports both binary (.bin) and ANSI C99 header formats for zero-heap embedded retrieval.
"""

from dataclasses import dataclass
import argparse
import os
import struct
import sys
import zlib

import numpy as np
import pyproj
import rasterio

from skymask_gen import (
    NUM_BINS,
    AZIMUTH_STEP_DEG,
    R_EFF_METERS,
    read_dem_window,
    create_dem_interpolator,
    get_local_metric_crs,
)

GRID_MAGIC = 0x534D4752  # "SMGR" (SkyMask GRid)
GRID_VERSION = 1


@dataclass
class SkymaskGridMeta:
    """Metadata describing the 2D Skymask Grid."""
    origin_lat: float
    origin_lon: float
    rows: int
    cols: int
    grid_step_m: float
    search_radius_m: float
    eye_height_m: float
    num_bins: int = NUM_BINS


def generate_skymask_grid(
    dem_path: str,
    center_lat: float,
    center_lon: float,
    grid_width_m: float = 1000.0,
    grid_height_m: float = 1000.0,
    grid_step_m: float = 50.0,
    search_radius_m: float = 5000.0,
    ray_step_m: float = 15.0,
    eye_height_m: float = 1.8,
) -> tuple[SkymaskGridMeta, np.ndarray, np.ndarray]:
    """
    Computes a 2D spatial grid of 64-bin skymask LUTs over a specified area.

    Returns:
        meta (SkymaskGridMeta): Grid geometry metadata.
        grid_lut (np.ndarray): Shape (rows, cols, 64), dtype=uint8.
        elev_grid (np.ndarray): Shape (rows, cols), dtype=float32 (ground elevations MSL).
    """
    if not os.path.isfile(dem_path):
        raise FileNotFoundError(f"DEM GeoTIFF not found: {dem_path}")
    if grid_step_m <= 0:
        raise ValueError(f"Grid step must be positive, got: {grid_step_m}")

    cols = int(np.ceil(grid_width_m / grid_step_m)) + 1
    rows = int(np.ceil(grid_height_m / grid_step_m)) + 1

    # Establish local Transverse Mercator centered at area center
    metric_crs = get_local_metric_crs(center_lat, center_lon, proj_type="tmerc")
    trans_wgs_to_metric = pyproj.Transformer.from_crs("EPSG:4326", metric_crs, always_xy=True)
    trans_metric_to_wgs = pyproj.Transformer.from_crs(metric_crs, "EPSG:4326", always_xy=True)

    # Center is at (0, 0) in local TM
    half_w = (cols - 1) * grid_step_m / 2.0
    half_h = (rows - 1) * grid_step_m / 2.0

    # Grid cell coordinates in local metric space
    x_metric_1d = np.linspace(-half_w, half_w, cols)
    y_metric_1d = np.linspace(-half_h, half_h, rows)

    # Origin (bottom-left cell [0, 0]) in WGS84
    orig_lon, orig_lat = trans_metric_to_wgs.transform(x_metric_1d[0], y_metric_1d[0])

    meta = SkymaskGridMeta(
        origin_lat=orig_lat,
        origin_lon=orig_lon,
        rows=rows,
        cols=cols,
        grid_step_m=grid_step_m,
        search_radius_m=search_radius_m,
        eye_height_m=eye_height_m,
    )

    # Windowed DEM reading around entire grid plus search radius + buffer
    total_search_radius = max(half_w, half_h) + search_radius_m
    with rasterio.open(dem_path) as dem_dataset:
        patch_data, win_transform, _ = read_dem_window(
            dem_dataset, center_lat, center_lon, total_search_radius, metric_crs, buffer_m=500.0
        )
        dem_crs = dem_dataset.crs

    interp = create_dem_interpolator(patch_data, win_transform)
    trans_metric_to_dem = pyproj.Transformer.from_crs(metric_crs, dem_crs, always_xy=True)

    # Pre-generate 64 azimuth rays and radial distances
    azimuths_rad = np.radians(np.arange(NUM_BINS, dtype=np.float64) * AZIMUTH_STEP_DEG)
    r_samples = np.arange(ray_step_m, search_radius_m + 0.5 * ray_step_m, ray_step_m, dtype=np.float64)
    r_samples = r_samples[r_samples > 0.0]

    R_mesh, AZ_mesh = np.meshgrid(r_samples, azimuths_rad)
    dx_rays = R_mesh * np.sin(AZ_mesh)
    dy_rays = R_mesh * np.cos(AZ_mesh)

    delta_h_corr = (R_mesh ** 2) / (2.0 * R_EFF_METERS)

    grid_lut = np.zeros((rows, cols, NUM_BINS), dtype=np.uint8)
    elev_grid = np.zeros((rows, cols), dtype=np.float32)

    # March rays for each grid cell
    for r_idx in range(rows):
        y_obs = y_metric_1d[r_idx]
        for c_idx in range(cols):
            x_obs = x_metric_1d[c_idx]

            # Ground elevation at grid cell
            x_dem_obs, y_dem_obs = trans_metric_to_dem.transform(x_obs, y_obs)
            z0 = float(interp(np.array([[y_dem_obs, x_dem_obs]]))[0])
            elev_grid[r_idx, c_idx] = z0
            p0_z = z0 + eye_height_m

            # Coordinates of all ray sample points
            x_pts = x_obs + dx_rays
            y_pts = y_obs + dy_rays

            x_dem_pts, y_dem_pts = trans_metric_to_dem.transform(x_pts.ravel(), y_pts.ravel())
            eval_pts = np.column_stack([y_dem_pts, x_dem_pts])
            z_pts = interp(eval_pts).reshape(R_mesh.shape)

            z_eff = z_pts - delta_h_corr
            dz = z_eff - p0_z
            alpha_deg = np.degrees(np.arctan(dz / R_mesh))
            theta_max = np.max(alpha_deg, axis=1)
            theta_horizon = np.clip(theta_max, 0.0, 90.0)

            # Fixed-point quantization
            q = np.clip(np.round((theta_horizon / 90.0) * 255.0), 0, 255).astype(np.uint8)
            grid_lut[r_idx, c_idx, :] = q

    return meta, grid_lut, elev_grid


def export_grid_binary(meta: SkymaskGridMeta, grid_lut: np.ndarray, elev_grid: np.ndarray, output_path: str) -> None:
    """
    Exports the grid cache to a compact binary file format:
      - 32-byte header with magic, version, geometry, CRC32
      - Binary LUT payload: rows * cols * 64 bytes
    """
    # Header format:
    # uint32 magic, uint16 version, uint16 num_bins, uint32 rows, uint32 cols,
    # float32 grid_step_m, float32 radius_m, float32 eye_height_m,
    # float64 origin_lat, float64 origin_lon, uint32 data_crc32
    lut_bytes = grid_lut.tobytes()
    data_crc = zlib.crc32(lut_bytes)

    header = struct.pack(
        "<IHIIIIfffddI",
        GRID_MAGIC,
        GRID_VERSION,
        meta.num_bins,
        meta.rows,
        meta.cols,
        0,  # reserved
        meta.grid_step_m,
        meta.search_radius_m,
        meta.eye_height_m,
        meta.origin_lat,
        meta.origin_lon,
        data_crc,
    )

    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    with open(output_path, "wb") as f:
        f.write(header)
        f.write(lut_bytes)


def export_grid_c_header(meta: SkymaskGridMeta, grid_lut: np.ndarray, elev_grid: np.ndarray, output_path: str) -> None:
    """
    Exports the grid LUT as a compile-ready ANSI C99 static array and fast lookup macro.
    """
    c_header = f"""#ifndef SKYMASK_GRID_LUT_H
#define SKYMASK_GRID_LUT_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Dynamic Topographic Skymask 2D Grid LUT
 * Grid Dimension: {meta.rows} rows x {meta.cols} cols ({meta.rows * meta.cols} cells)
 * Cell Resolution: {meta.grid_step_m:.1f} meters
 * Origin (Cell [0,0]): LAT: {meta.origin_lat:.8f}, LON: {meta.origin_lon:.8f}
 * Search Radius: {meta.search_radius_m:.1f}m, Antenna AGL: {meta.eye_height_m:.1f}m
 * Total LUT Payload: {meta.rows * meta.cols * NUM_BINS} bytes
 */

#define SKYMASK_GRID_ROWS        {meta.rows}U
#define SKYMASK_GRID_COLS        {meta.cols}U
#define SKYMASK_GRID_NUM_BINS    {NUM_BINS}U
#define SKYMASK_GRID_STEP_M      {meta.grid_step_m:.2f}f
#define SKYMASK_GRID_ORIGIN_LAT  {meta.origin_lat:.8f}
#define SKYMASK_GRID_ORIGIN_LON  {meta.origin_lon:.8f}

typedef struct {{
    uint32_t rows;
    uint32_t cols;
    float step_m;
    double origin_lat;
    double origin_lon;
    const uint8_t *lut_data;
}} skymask_grid_t;

/* Compact flattened 3D array [ROWS][COLS][64] */
static const uint8_t SKYMASK_GRID_DATA[SKYMASK_GRID_ROWS][SKYMASK_GRID_COLS][SKYMASK_GRID_NUM_BINS] = {{
"""

    rows_str = []
    for r in range(meta.rows):
        cols_str = []
        for c in range(meta.cols):
            vals = ", ".join(str(v) for v in grid_lut[r, c, :])
            cols_str.append(f"        /* cell [{r},{c}] */ {{{vals}}}")
        cell_block = ",\n".join(cols_str)
        rows_str.append(f"    /* --- ROW {r} --- */\n    {{\n{cell_block}\n    }}")

    c_header += ",\n".join(rows_str)
    c_header += """
};

/**
 * @brief Fast zero-heap inline lookup for local 64-bin skymask
 * @param row Grid row index
 * @param col Grid column index
 * @return Pointer to 64-byte LUT for cell, or NULL if out of bounds
 */
static inline const uint8_t* skymask_grid_get_cell(uint32_t row, uint32_t col) {
    if (row >= SKYMASK_GRID_ROWS || col >= SKYMASK_GRID_COLS) {
        return 0; // NULL
    }
    return SKYMASK_GRID_DATA[row][col];
}

#endif // SKYMASK_GRID_LUT_H
"""
    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    with open(output_path, "w", encoding="utf-8") as f:
        f.write(c_header)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Dynamic 2D Grid Topographic Skymask Cache Generator for Extreme Condition Terminals."
    )
    parser.add_argument("--dem", required=True, type=str, help="Path to input DEM GeoTIFF")
    parser.add_argument("--lat", required=True, type=float, help="Center latitude (WGS84)")
    parser.add_argument("--lon", required=True, type=float, help="Center longitude (WGS84)")
    parser.add_argument("--width", type=float, default=500.0, help="Grid width in meters (default: 500m)")
    parser.add_argument("--height", type=float, default=500.0, help="Grid height in meters (default: 500m)")
    parser.add_argument("--grid-step", type=float, default=50.0, help="Grid resolution step in meters (default: 50m)")
    parser.add_argument("--radius", type=float, default=3000.0, help="Horizon search radius in meters (default: 3000m)")
    parser.add_argument("--step", type=float, default=15.0, help="Ray step in meters (default: 15m)")
    parser.add_argument("--eye-height", type=float, default=1.8, help="Antenna AGL in meters (default: 1.8m)")
    parser.add_argument("--output-bin", type=str, default=None, help="Path to export binary grid file (.bin)")
    parser.add_argument("--output-c", type=str, default=None, help="Path to export C header file (.h)")

    args = parser.parse_args()

    print(f"[*] Generating 2D Skymask Grid centered at ({args.lat:.6f}, {args.lon:.6f})")
    print(f"[*] Area: {args.width:.0f}m x {args.height:.0f}m with cell step: {args.grid_step:.0f}m")
    meta, grid_lut, elev = generate_skymask_grid(
        dem_path=args.dem,
        center_lat=args.lat,
        center_lon=args.lon,
        grid_width_m=args.width,
        grid_height_m=args.height,
        grid_step_m=args.grid_step,
        search_radius_m=args.radius,
        ray_step_m=args.step,
        eye_height_m=args.eye_height,
    )
    print(f"[+] Successfully computed {meta.rows}x{meta.cols} grid ({meta.rows * meta.cols} cells, {meta.rows * meta.cols * NUM_BINS} bytes)")

    if args.output_bin:
        export_grid_binary(meta, grid_lut, elev, args.output_bin)
        print(f"[+] Exported binary cache to: {args.output_bin}")

    if args.output_c:
        export_grid_c_header(meta, grid_lut, elev, args.output_c)
        print(f"[+] Exported C header to: {args.output_c}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
