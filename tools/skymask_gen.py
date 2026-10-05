#!/usr/bin/env python3
"""
64-Bin Topographic Skymask Extraction Engine
Extracts an azimuthal horizon mask (Skymask) from a Digital Elevation Model (DEM GeoTIFF)
for a given WGS84 coordinate to generate an on-device Look-Up Table (LUT) for embedded
GNSS NLOS (Non-Line-of-Sight) filtering on ARM Cortex-M33 MCUs.
"""

from dataclasses import dataclass
import argparse
import os
import sys
import warnings

# Ensure UTF-8 output on Windows consoles
if sys.platform == "win32" and hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")

# Suppress internal rasterio Affine PendingDeprecationWarning
warnings.filterwarnings("ignore", category=PendingDeprecationWarning, module="rasterio")

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pyproj
import rasterio
from rasterio.windows import from_bounds, Window
from scipy.interpolate import RegularGridInterpolator
from scipy.ndimage import distance_transform_edt

# Physical and Mathematical Constants
NUM_BINS = 64
AZIMUTH_STEP_DEG = 360.0 / NUM_BINS  # 5.625 degrees
R_EFF_METERS = 8_500_000.0  # Effective Earth radius (4/3 R_E)
BUFFER_METERS = 500.0  # Bounding box buffer for windowed DEM reads


@dataclass
class SkymaskResult:
    """Stores the computed skymask results and metadata."""
    lat: float
    lon: float
    radius: float
    step: float
    eye_height: float
    dem_filename: str
    z0: float
    azimuths_deg: np.ndarray  # Shape: (64,)
    theta_horizon_deg: np.ndarray  # Shape: (64,) in [0.0, 90.0]
    quantized_lut: np.ndarray  # Shape: (64,) uint8 in [0, 255]


def get_local_metric_crs(lat: float, lon: float, proj_type: str = "tmerc") -> pyproj.CRS:
    """
    Returns a metric planar CRS (Transverse Mercator or UTM) centered on or containing the coordinates.

    'tmerc' sets the projection origin at (lat, lon). This ensures:
      1. Grid convergence at the observer is exactly 0.0 deg (True North is +y).
      2. Planar scale factor is 1.0000 at the observer.
      3. No UTM zone boundary seam or distortion.

    'utm' computes the appropriate standard UTM zone for (lat, lon).
    """
    if proj_type.lower() == "tmerc":
        proj_str = (
            f"+proj=tmerc +lat_0={lat:.8f} +lon_0={lon:.8f} "
            f"+k=1.0 +x_0=0 +y_0=0 +datum=WGS84 +units=m +no_defs"
        )
        return pyproj.CRS.from_proj4(proj_str)
    elif proj_type.lower() == "utm":
        utm_zone = int((lon + 180) // 6) + 1
        is_south = lat < 0
        epsg = (32700 if is_south else 32600) + utm_zone
        return pyproj.CRS.from_epsg(epsg)
    else:
        raise ValueError(f"Unsupported projection type: {proj_type}. Choose 'tmerc' or 'utm'.")


def read_dem_window(
    dataset: rasterio.DatasetReader,
    lat: float,
    lon: float,
    radius: float,
    metric_crs: pyproj.CRS,
    buffer_m: float = BUFFER_METERS,
) -> tuple[np.ndarray, rasterio.Affine, tuple[float, float, float, float]]:
    """
    Performs a memory-efficient windowed read of the DEM around (lat, lon) within (radius + buffer_m).
    Validates that the requested bounding box is within the DEM coverage, raising ValueError if not.
    Gracefully handles NoData pixels using nearest-neighbor interpolation with a warning.

    Returns:
        patch_data (2D np.ndarray float64): DEM elevation array.
        win_transform (Affine): Affine transform mapping pixel coords to DEM CRS.
        bounds_dem (tuple): (min_x, min_y, max_x, max_y) in DEM CRS.
    """
    transformer_to_metric = pyproj.Transformer.from_crs("EPSG:4326", metric_crs, always_xy=True)
    transformer_to_dem = pyproj.Transformer.from_crs(metric_crs, dataset.crs, always_xy=True)

    x0_metric, y0_metric = transformer_to_metric.transform(lon, lat)

    r_search = radius + buffer_m
    angles = np.linspace(0, 2 * np.pi, 36, endpoint=False)
    x_circle = x0_metric + r_search * np.sin(angles)
    y_circle = y0_metric + r_search * np.cos(angles)

    # Also include the observer center point
    x_points = np.append(x_circle, x0_metric)
    y_points = np.append(y_circle, y0_metric)

    x_dem, y_dem = transformer_to_dem.transform(x_points, y_points)

    min_x = float(np.min(x_dem))
    max_x = float(np.max(x_dem))
    min_y = float(np.min(y_dem))
    max_y = float(np.max(y_dem))

    dem_left, dem_bottom, dem_right, dem_top = dataset.bounds

    # Out of Bounds Check
    if min_x < dem_left or max_x > dem_right or min_y < dem_bottom or max_y > dem_top:
        raise ValueError(
            f"Requested bounding box [min_x={min_x:.6f}, min_y={min_y:.6f}, max_x={max_x:.6f}, max_y={max_y:.6f}] "
            f"extends beyond DEM extent [left={dem_left:.6f}, bottom={dem_bottom:.6f}, right={dem_right:.6f}, top={dem_top:.6f}]. "
            f"Latitude={lat:.6f}, Longitude={lon:.6f}, Radius={radius:.1f}m."
        )

    # Calculate integer window with safety padding
    raw_window = from_bounds(min_x, min_y, max_x, max_y, transform=dataset.transform)
    col_start = max(0, int(np.floor(raw_window.col_off)))
    row_start = max(0, int(np.floor(raw_window.row_off)))
    col_end = min(dataset.width, int(np.ceil(raw_window.col_off + raw_window.width)))
    row_end = min(dataset.height, int(np.ceil(raw_window.row_off + raw_window.height)))

    window = Window(
        col_off=col_start,
        row_off=row_start,
        width=max(1, col_end - col_start),
        height=max(1, row_end - row_start),
    )

    patch_data = dataset.read(1, window=window).astype(np.float64)
    win_transform = dataset.window_transform(window)

    # Handle No-Data values
    nodata_val = dataset.nodata
    if nodata_val is not None:
        nodata_mask = np.isclose(patch_data, nodata_val) | np.isnan(patch_data)
    else:
        nodata_mask = np.isnan(patch_data)

    num_nodata = int(np.sum(nodata_mask))
    if num_nodata > 0:
        total_pixels = patch_data.size
        if num_nodata == total_pixels:
            raise ValueError("All pixels in the requested DEM patch are NoData values.")
        warnings.warn(
            f"DEM patch contains {num_nodata}/{total_pixels} NoData pixels ({num_nodata / total_pixels * 100:.2f}%). "
            f"Filling via nearest valid neighbor interpolation.",
            UserWarning,
            stacklevel=2,
        )
        indices = distance_transform_edt(nodata_mask, return_distances=False, return_indices=True)
        patch_data = patch_data[tuple(indices)]

    return patch_data, win_transform, (min_x, min_y, max_x, max_y)


def create_dem_interpolator(
    patch_data: np.ndarray,
    win_transform: rasterio.Affine,
) -> RegularGridInterpolator:
    """
    Constructs a scipy RegularGridInterpolator for bilinear spatial interpolation
    on the DEM grid.
    """
    height, width = patch_data.shape

    # Pixel centers in DEM CRS
    x_centers = win_transform.c + (np.arange(width) + 0.5) * win_transform.a
    y_centers = win_transform.f + (np.arange(height) + 0.5) * win_transform.e

    # RegularGridInterpolator requires 1D coordinate arrays to be strictly monotonically increasing
    if len(y_centers) > 1 and y_centers[1] < y_centers[0]:
        y_centers = y_centers[::-1]
        patch_data = patch_data[::-1, :]

    if len(x_centers) > 1 and x_centers[1] < x_centers[0]:
        x_centers = x_centers[::-1]
        patch_data = patch_data[:, ::-1]

    interpolator = RegularGridInterpolator(
        (y_centers, x_centers),
        patch_data,
        method="linear",
        bounds_error=False,
        fill_value=None,  # Nearest edge extrapolation for boundary precision
    )
    return interpolator


def compute_skymask(
    dem_path: str,
    lat: float,
    lon: float,
    radius: float = 5000.0,
    step: float = 15.0,
    eye_height: float = 1.8,
    proj_type: str = "tmerc",
) -> SkymaskResult:
    """
    Computes the 64-bin Azimuthal Horizon Mask (Skymask) from a DEM GeoTIFF.

    Args:
        dem_path: Path to the GeoTIFF DEM file.
        lat: Target latitude in decimal degrees.
        lon: Target longitude in decimal degrees.
        radius: Radial search distance in meters (default: 5000.0).
        step: Ray sampling interval in meters (default: 15.0).
        eye_height: Height of GNSS antenna above ground in meters (default: 1.8).
        proj_type: Local metric projection type ('tmerc' or 'utm').

    Returns:
        SkymaskResult containing angles, quantized LUT, observer elevation, and metadata.
    """
    if not os.path.isfile(dem_path):
        raise FileNotFoundError(f"DEM GeoTIFF not found: {dem_path}")
    if step <= 0.0:
        raise ValueError(f"Ray step interval must be strictly positive, got: {step}")
    if radius <= step:
        raise ValueError(f"Radius ({radius}m) must be greater than step interval ({step}m)")

    # 1. Define Local Metric Projection & Geodesy
    metric_crs = get_local_metric_crs(lat, lon, proj_type=proj_type)

    with rasterio.open(dem_path) as dem_dataset:
        # 2. Windowed DEM Read & Out of Bounds Check
        patch_data, win_transform, _ = read_dem_window(
            dem_dataset, lat, lon, radius, metric_crs, buffer_m=BUFFER_METERS
        )
        dem_filename = os.path.basename(dem_path)
        dem_crs = dem_dataset.crs

    # 3. Setup Bilinear Interpolation
    interp = create_dem_interpolator(patch_data, win_transform)

    # Observer position in DEM CRS
    trans_wgs_to_dem = pyproj.Transformer.from_crs("EPSG:4326", dem_crs, always_xy=True)
    obs_x_dem, obs_y_dem = trans_wgs_to_dem.transform(lon, lat)

    # Bilinearly interpolate ground elevation z0 at observer position
    z0 = float(interp(np.array([[obs_y_dem, obs_x_dem]]))[0])
    p0_z = z0 + eye_height

    # 4. Ray-Marching Engine Setup
    # Exactly 64 discrete azimuth bins (Clockwise from True North: 0 = N, 90 = E)
    azimuths_deg = np.arange(NUM_BINS, dtype=np.float64) * AZIMUTH_STEP_DEG
    azimuths_rad = np.radians(azimuths_deg)

    # If using UTM projection, correct for meridian convergence so rays follow True North
    if proj_type.lower() == "utm":
        proj_obj = pyproj.Proj(metric_crs)
        factors = proj_obj.get_factors(lon, lat)
        meridian_conv_rad = np.radians(factors.meridian_convergence)
        grid_azimuths_rad = azimuths_rad - meridian_conv_rad
    else:
        # In local Transverse Mercator centered at (lat, lon), convergence is exactly 0.0
        grid_azimuths_rad = azimuths_rad

    # Radial range: r in [r_min, R_max] with step Delta r (strictly exclude r = 0)
    r_min = step
    r_samples = np.arange(r_min, radius + 0.5 * step, step, dtype=np.float64)
    r_samples = r_samples[r_samples > 0.0]  # Zero-division prevention

    # 5. Vectorized Ray Sampling & Coordinate Transformation
    trans_wgs_to_metric = pyproj.Transformer.from_crs("EPSG:4326", metric_crs, always_xy=True)
    x0_metric, y0_metric = trans_wgs_to_metric.transform(lon, lat)

    # Meshgrid: (64 bins, N_r distances)
    # X = Easting = x0 + r * sin(azimuth)
    # Y = Northing = y0 + r * cos(azimuth)
    R_grid, AZ_grid = np.meshgrid(r_samples, grid_azimuths_rad)
    X_metric = x0_metric + R_grid * np.sin(AZ_grid)
    Y_metric = y0_metric + R_grid * np.cos(AZ_grid)

    # Transform all ray sample points from metric CRS to DEM CRS
    trans_metric_to_dem = pyproj.Transformer.from_crs(metric_crs, dem_crs, always_xy=True)
    X_dem_pts, Y_dem_pts = trans_metric_to_dem.transform(X_metric.ravel(), Y_metric.ravel())

    # Bilinear interpolation of terrain elevations z along all rays
    eval_points = np.column_stack([Y_dem_pts, X_dem_pts])
    z_samples = interp(eval_points).reshape(R_grid.shape)

    # 6. Earth Curvature & Atmospheric Refraction Correction
    # delta_h_corr = d^2 / (2 * R_eff)
    # z_eff(d) = z(d) - delta_h_corr
    delta_h_corr = (R_grid ** 2) / (2.0 * R_EFF_METERS)
    z_eff = z_samples - delta_h_corr

    # 7. Horizon Angle Extraction
    # alpha_i(r) = arctan((z_eff(r) - (z0 + h_obs)) / r)
    dz = z_eff - p0_z
    alpha_deg = np.degrees(np.arctan(dz / R_grid))

    # theta_i = max_r(alpha_i(r))
    theta_max_deg = np.max(alpha_deg, axis=1)

    # theta_horizon,i = max(0, min(90, theta_i))
    theta_horizon_deg = np.clip(theta_max_deg, 0.0, 90.0)

    # 8. 8-Bit Fixed-Point Quantization
    # Q_i = round((theta_horizon,i / 90.0) * 255) in [0, 255]
    quantized_lut = np.clip(np.round((theta_horizon_deg / 90.0) * 255.0), 0, 255).astype(np.uint8)

    return SkymaskResult(
        lat=lat,
        lon=lon,
        radius=radius,
        step=step,
        eye_height=eye_height,
        dem_filename=dem_filename,
        z0=z0,
        azimuths_deg=azimuths_deg,
        theta_horizon_deg=theta_horizon_deg,
        quantized_lut=quantized_lut,
    )


def export_c_header(result: SkymaskResult, output_path: str) -> None:
    """
    Exports the computed skymask LUT to an ANSI C99 header file according to specification.
    """
    sector_names = [
        "N to NE",
        "NE to E",
        "E to SE",
        "SE to S",
        "S to SW",
        "SW to W",
        "W to NW",
        "NW to N",
    ]

    lut_rows = []
    for row_idx in range(8):
        start_bin = row_idx * 8
        end_bin = start_bin + 7
        values = result.quantized_lut[start_bin : end_bin + 1]
        val_str = ", ".join(f"{v:3d}" for v in values)
        bin_range = f"{start_bin}..{end_bin}"
        sector_comment = f"/* {bin_range:6s} ({sector_names[row_idx]:7s})  */"
        trailing_comma = "," if row_idx < 7 else ""
        lut_rows.append(f"    {sector_comment} {val_str}{trailing_comma}")

    lut_body = "\n".join(lut_rows)

    c_content = f"""#ifndef SKYMASK_LUT_H
#define SKYMASK_LUT_H

#include <stdint.h>

/**
 * @brief Topographic Skymask 64-Bin LUT
 * Generated for: LAT: {result.lat:.6f}, LON: {result.lon:.6f}, ALT_AGL: {result.eye_height:.1f}m
 * Ground Elevation (DEM): {result.z0:.2f}m MSL, Search Radius: {result.radius:.1f}m
 * Source DEM: {result.dem_filename}
 * Quantization: uint8_t [0-255] mapped to [0.0 - 90.0] degrees
 * Resolution: 5.625 deg/bin (Azimuth), ~0.353 deg/LSB (Elevation)
 */

#define SKYMASK_NUM_BINS         {NUM_BINS}
#define SKYMASK_AZIMUTH_STEP_DEG {AZIMUTH_STEP_DEG:.3f}f

static const uint8_t SKYMASK_LUT[SKYMASK_NUM_BINS] = {{
{lut_body}
}};

#endif // SKYMASK_LUT_H
"""
    output_dir = os.path.dirname(os.path.abspath(output_path))
    if output_dir:
        os.makedirs(output_dir, exist_ok=True)
    with open(output_path, "w", encoding="utf-8") as f:
        f.write(c_content)


def generate_polar_plot(result: SkymaskResult, output_path: str) -> None:
    """
    Renders a verification polar plot showing:
      - Azimuth (0 deg at top/North, clockwise to 360 deg)
      - Radial axis representing elevation angle from 0 deg (outer edge / horizon) to 90 deg (center / zenith)
      - Solid dark red/grey fill for terrain obstruction area (theta_horizon down to 0 deg)
      - Crisp line marking the LOS horizon threshold
    """
    fig, ax = plt.subplots(figsize=(9, 9), subplot_kw={"projection": "polar"})

    # True North (0 deg) at top, clockwise direction
    ax.set_theta_zero_location("N")
    ax.set_theta_direction(-1)

    # Inverted radial limits: 0 deg at outer boundary, 90 deg at center (Zenith)
    ax.set_rlim(90, 0)

    # Wrap azimuths to 360 to close the polar curve cleanly
    az_plot_rad = np.append(np.radians(result.azimuths_deg), np.radians(360.0))
    theta_plot_deg = np.append(result.theta_horizon_deg, result.theta_horizon_deg[0])

    # Fill the terrain obstruction area (from theta_horizon down to 0 deg horizon)
    ax.fill_between(
        az_plot_rad,
        theta_plot_deg,
        0,
        color="#8B1E1E",
        alpha=0.45,
        label="Terrain Obstruction (NLOS)",
    )

    # LOS Horizon Threshold curve
    ax.plot(
        az_plot_rad,
        theta_plot_deg,
        color="#E53935",
        linewidth=2.5,
        label="LOS Horizon Threshold",
    )

    # Cardinal & Intercardinal labels
    cardinal_angles = [0, 45, 90, 135, 180, 225, 270, 315]
    cardinal_labels = ["N\n(0°)", "NE\n(45°)", "E\n(90°)", "SE\n(135°)", "S\n(180°)", "SW\n(225°)", "W\n(270°)", "NW\n(315°)"]
    ax.set_xticks(np.radians(cardinal_angles))
    ax.set_xticklabels(cardinal_labels, fontsize=10, fontweight="bold")

    # Radial elevation ticks
    r_ticks = [0, 15, 30, 45, 60, 75, 90]
    r_labels = ["0° (Horizon)", "15°", "30°", "45°", "60°", "75°", "90° (Zenith)"]
    ax.set_rticks(r_ticks)
    ax.set_yticklabels(r_labels, fontsize=9)
    ax.set_rlabel_position(22.5)

    # Grid styling
    ax.grid(True, linestyle="--", alpha=0.5, color="#777777")
    ax.set_facecolor("#FAFAFA")

    # Titles & Metadata
    plt.title(
        f"Topographic Skymask (64 Bins)\n"
        f"Lat: {result.lat:.5f}°, Lon: {result.lon:.5f}° | DEM: {result.dem_filename}\n"
        f"Antenna AGL: {result.eye_height:.1f}m | Ground Elev: {result.z0:.1f}m | Radius: {result.radius:.0f}m",
        pad=24,
        fontsize=11,
        fontweight="semibold",
    )

    ax.legend(loc="lower right", bbox_to_anchor=(1.18, -0.05), frameon=True, shadow=True)

    output_dir = os.path.dirname(os.path.abspath(output_path))
    if output_dir:
        os.makedirs(output_dir, exist_ok=True)
    fig.savefig(output_path, dpi=180, bbox_inches="tight")
    plt.close(fig)


def build_parser() -> argparse.ArgumentParser:
    """Builds the CLI argument parser."""
    parser = argparse.ArgumentParser(
        description="64-Bin Topographic Skymask Extraction Engine for embedded GNSS NLOS filtering."
    )
    parser.add_argument(
        "--dem",
        required=True,
        type=str,
        help="Path to the input DEM GeoTIFF (e.g., Copernicus GLO-30 or SRTM 1-arcsec).",
    )
    parser.add_argument(
        "--lat",
        required=True,
        type=float,
        help="Target latitude in decimal degrees (WGS84).",
    )
    parser.add_argument(
        "--lon",
        required=True,
        type=float,
        help="Target longitude in decimal degrees (WGS84).",
    )
    parser.add_argument(
        "--radius",
        type=float,
        default=5000.0,
        help="Radial search distance in meters (default: 5000.0).",
    )
    parser.add_argument(
        "--step",
        type=float,
        default=15.0,
        help="Ray sampling interval in meters (default: 15.0).",
    )
    parser.add_argument(
        "--eye-height",
        type=float,
        default=1.8,
        help="Height of GNSS antenna above ground in meters (default: 1.8).",
    )
    parser.add_argument(
        "--output-c",
        type=str,
        default=None,
        help="Path to export the generated C header file (e.g., skymask_lut.h).",
    )
    parser.add_argument(
        "--plot",
        type=str,
        default=None,
        help="Path to export a validation polar PNG plot (e.g., skymask_polar.png).",
    )
    parser.add_argument(
        "--proj",
        type=str,
        choices=["tmerc", "utm"],
        default="tmerc",
        help="Local metric projection: 'tmerc' (Transverse Mercator at observer, default) or 'utm'.",
    )
    return parser


def main() -> int:
    """CLI main entry point."""
    parser = build_parser()
    args = parser.parse_args()

    try:
        print(f"[*] Processing coordinate: Lat={args.lat:.6f}, Lon={args.lon:.6f}")
        print(f"[*] DEM Source: {args.dem}")
        print(f"[*] Radius={args.radius:.1f}m, Step={args.step:.1f}m, Antenna AGL={args.eye_height:.1f}m")

        result = compute_skymask(
            dem_path=args.dem,
            lat=args.lat,
            lon=args.lon,
            radius=args.radius,
            step=args.step,
            eye_height=args.eye_height,
            proj_type=args.proj,
        )

        mean_obstruction = np.mean(result.theta_horizon_deg)
        max_obstruction = np.max(result.theta_horizon_deg)
        print(f"[+] Computed 64-bin Skymask successfully.")
        print(f"    Observer Ground Elevation: {result.z0:.2f} m MSL")
        print(f"    Mean Horizon Obstruction:  {mean_obstruction:.2f}°")
        print(f"    Max Horizon Obstruction:   {max_obstruction:.2f}°")

        if args.output_c:
            export_c_header(result, args.output_c)
            print(f"[+] C header exported to: {args.output_c}")

        if args.plot:
            generate_polar_plot(result, args.plot)
            print(f"[+] Polar plot exported to: {args.plot}")

        return 0

    except Exception as exc:
        print(f"[-] Error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
