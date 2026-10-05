#!/usr/bin/env python3
"""
Generates a realistic synthetic DEM GeoTIFF for testing skymask_gen.py
Centered around Bursa / Uludag region (Lat: 40.0694, Lon: 29.2217).
Features mountainous terrain to the south (Uludag range) and lower plains to the north.
"""

from affine import Affine
import numpy as np
import rasterio

def generate_dem(output_path="sample_dem.tif", center_lat=40.0694, center_lon=29.2217, size_deg=0.3):
    res_deg = 0.000277777777778  # ~30m (Copernicus GLO-30 / SRTM 1-arcsec)
    cols = int(round(size_deg / res_deg))
    rows = int(round(size_deg / res_deg))

    min_lon = center_lon - size_deg / 2.0
    max_lat = center_lat + size_deg / 2.0

    transform = Affine(res_deg, 0.0, min_lon, 0.0, -res_deg, max_lat)

    lons = min_lon + (np.arange(cols) + 0.5) * res_deg
    lats = max_lat - (np.arange(rows) + 0.5) * res_deg
    lon_grid, lat_grid = np.meshgrid(lons, lats)

    # Convert offset to meters
    dy = (lat_grid - center_lat) * 111139.0
    dx = (lon_grid - center_lon) * (111139.0 * np.cos(np.radians(center_lat)))

    # Base elevation 250m
    elev = np.full_like(dx, 250.0)

    # Major mountain ridge to the South / Southeast (Uludag mountain massif)
    # Peak at (dx=2000m, dy=-2500m), height 1800m
    dist_ridge = np.sqrt((dx - 2000.0)**2 + (dy - (-2500.0))**2)
    elev += 1400.0 * np.exp(-dist_ridge**2 / (2.0 * (1500.0**2)))

    # Secondary hill to the East (dx=3000m, dy=500m), height 400m
    dist_east = np.sqrt((dx - 3000.0)**2 + (dy - 500.0)**2)
    elev += 400.0 * np.exp(-dist_east**2 / (2.0 * (800.0**2)))

    # Small hill to the Northwest (dx=-2500m, dy=2000m), height 250m
    dist_nw = np.sqrt((dx - (-2500.0))**2 + (dy - 2000.0)**2)
    elev += 250.0 * np.exp(-dist_nw**2 / (2.0 * (1000.0**2)))

    # Rolling terrain undulations
    elev += 30.0 * np.sin(dx / 400.0) * np.cos(dy / 400.0)

    with rasterio.open(
        output_path,
        "w",
        driver="GTiff",
        height=rows,
        width=cols,
        count=1,
        dtype=rasterio.float32,
        crs="EPSG:4326",
        transform=transform,
        nodata=-9999.0,
    ) as dst:
        dst.write(elev.astype(np.float32), 1)

    print(f"Successfully generated realistic DEM: {output_path} ({cols}x{rows} pixels, ~{size_deg*111:.1f}x{size_deg*111:.1f} km)")

if __name__ == "__main__":
    generate_dem()
