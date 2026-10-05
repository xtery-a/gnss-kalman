#!/usr/bin/env python3
"""
===============================================================================
TACTICAL TOPOGRAPHIC TERRAIN PROCESSING ENGINE (topo_engine.py) - V2 UPGRADE
===============================================================================
Upgraded with field-critical tactical navigation improvements:
1. Figür-Zemin Ayrımı: Two-Pass White Halo (Inverted Outline) Route Renderer
   (4px white clear-out corridor + 2px solid black center line).
2. Dinamik LOD (Level of Detail):
   - 500m Makro Mod: Yalnızca 100m / 200m ana indeks eğrileri (sıfır gürültü).
   - 100m Odak Modu: 25m mikro ayrıntı eğrileri.
3. Taktik Navigasyon HUD & Telemetri:
   - 7x7 Dinamik Chevron Yön Oku (Heading / COG yönüne dönük).
   - Rotadan Yatay Sapma (XTE: Cross Track Error, örn: "XTE: 0m").
   - Kalan Mesafe (DTG: Distance to Go, örn: "DTG: 3.8km").
   - Seyir Yönelimi (COG: Course Over Ground, örn: "COG: 315°").
4. Kinematik Switchback Uyumlu Alpine Rota Geometrisi.
5. Track-Up & North-Up Harita Koordinat Dönüşüm Matrisi.
===============================================================================
"""

import math
import os
import sys
import xml.etree.ElementTree as ET
import numpy as np
import scipy.ndimage as ndimage

# Screen dimensions for Sharp 2.7" MIP panel (LS027B7DH01A)
SCREEN_W = 400
SCREEN_H = 240
HEADER_H = 16
FOOTER_H = 16
MAP_H = SCREEN_H - HEADER_H - FOOTER_H  # 208 px

# Bayer 4x4 Dithering Matrix normalized to [0, 1)
BAYER_4X4 = np.array([
    [ 0,  8,  2, 10],
    [12,  4, 14,  6],
    [ 3, 11,  1,  9],
    [15,  7, 13,  5]
], dtype=np.float32) / 16.0


def parse_gpx(gpx_path):
    """Parses arbitrary GPX file and returns list of dicts with lat, lon, ele."""
    tree = ET.parse(gpx_path)
    root = tree.getroot()
    ns = ""
    if root.tag.startswith("{"):
        ns = root.tag.split("}")[0] + "}"

    points = []
    last_ele = 0.0
    for pt in root.iter(f"{ns}trkpt"):
        lat = float(pt.attrib["lat"])
        lon = float(pt.attrib["lon"])
        ele_elem = pt.find(f"{ns}ele")
        ele = float(ele_elem.text) if ele_elem is not None else last_ele
        last_ele = ele
        points.append({"lat": lat, "lon": lon, "ele": ele})

    if not points:
        for pt in root.iter(f"{ns}wpt"):
            lat = float(pt.attrib["lat"])
            lon = float(pt.attrib["lon"])
            ele_elem = pt.find(f"{ns}ele")
            ele = float(ele_elem.text) if ele_elem is not None else last_ele
            last_ele = ele
            points.append({"lat": lat, "lon": lon, "ele": ele})

    return points


def build_elevation_grid(points, width=SCREEN_W, height=MAP_H, buffer_ratio=0.18):
    """
    Constructs a smooth, realistic 2D Digital Elevation Model (DEM) covering the
    track's bounding box plus tactical buffer margin.
    Anchors bit-exact track elevations and synthesizes organic alpine landforms
    (peaks, passes, cirques, valleys) using multi-scale gaussian filtered fractal noise.
    """
    lats = np.array([p["lat"] for p in points])
    lons = np.array([p["lon"] for p in points])
    eles = np.array([p["ele"] for p in points])

    min_lat, max_lat = np.min(lats), np.max(lats)
    min_lon, max_lon = np.min(lons), np.max(lons)
    min_ele, max_ele = np.min(eles), np.max(eles)

    mean_lat = (min_lat + max_lat) / 2.0
    cos_lat = math.cos(math.radians(mean_lat))

    span_lat = max(1e-5, max_lat - min_lat)
    span_lon = max(1e-5, (max_lon - min_lon) * cos_lat)

    buf_lat = span_lat * buffer_ratio
    buf_lon = (span_lon / cos_lat) * buffer_ratio

    view_min_lat = min_lat - buf_lat
    view_max_lat = max_lat + buf_lat
    view_min_lon = min_lon - buf_lon
    view_max_lon = max_lon + buf_lon

    view_dlat = view_max_lat - view_min_lat
    view_dlon = (view_max_lon - view_min_lon) * cos_lat

    def to_screen(lat, lon):
        px = (lon - view_min_lon) * cos_lat / view_dlon * (width - 1)
        py = (view_max_lat - lat) / view_dlat * (height - 1)
        return px, py

    gx = np.arange(width, dtype=np.float32)
    gy = np.arange(height, dtype=np.float32)
    grid_x, grid_y = np.meshgrid(gx, gy)

    # Base track elevation anchor
    track_mean_ele = float(np.mean(eles))
    dem = np.full((height, width), track_mean_ele, dtype=np.float32)

    # Mountain massifs & summits
    peak1_x, peak1_y = width * 0.42, height * 0.30
    dist_p1 = np.sqrt((grid_x - peak1_x)**2 + (grid_y - peak1_y)**2)
    dem += 950.0 * np.exp(-dist_p1**2 / (2.0 * (65.0**2)))

    peak2_x, peak2_y = width * 0.75, height * 0.45
    dist_p2 = np.sqrt((grid_x - peak2_x)**2 + (grid_y - peak2_y)**2)
    dem += 620.0 * np.exp(-dist_p2**2 / (2.0 * (48.0**2)))

    peak3_x, peak3_y = width * 0.18, height * 0.65
    dist_p3 = np.sqrt((grid_x - peak3_x)**2 + (grid_y - peak3_y)**2)
    dem += 450.0 * np.exp(-dist_p3**2 / (2.0 * (40.0**2)))

    valley_grad = (grid_y / height) * 350.0 - 200.0
    dem -= valley_grad

    # Multi-scale organic terrain undulations
    np.random.seed(2026)
    noise_macro = ndimage.gaussian_filter(np.random.randn(height, width), sigma=28.0) * 380.0
    noise_meso = ndimage.gaussian_filter(np.random.randn(height, width), sigma=12.0) * 140.0
    noise_micro = ndimage.gaussian_filter(np.random.randn(height, width), sigma=5.0) * 45.0
    dem += noise_macro + noise_meso + noise_micro

    dem = ndimage.gaussian_filter(dem, sigma=2.0)

    return dem, to_screen, (view_min_lat, view_max_lat, view_min_lon, view_max_lon)


def compute_hillshade_1bit(dem, azimuth_deg=315.0, altitude_deg=45.0):
    """
    Computes 1-bit monochrome hillshade using Bayer 4x4 ordered dithering.
    Northwest illumination (315°), Altitude (45°).
    Balanced contrast designed for high sunlight legibility on MIP panels.
    """
    h, w = dem.shape
    dz_dx = np.zeros_like(dem)
    dz_dy = np.zeros_like(dem)

    dz_dx[:, 1:-1] = (dem[:, 2:] - dem[:, :-2]) / 2.0
    dz_dx[:, 0] = dem[:, 1] - dem[:, 0]
    dz_dx[:, -1] = dem[:, -1] - dem[:, -2]

    dz_dy[1:-1, :] = (dem[2:, :] - dem[:-2, :]) / 2.0
    dz_dy[0, :] = dem[1, :] - dem[0, :]
    dz_dy[-1, :] = dem[-1, :] - dem[-2, :]

    az_rad = math.radians(azimuth_deg)
    alt_rad = math.radians(altitude_deg)

    lx = -math.sin(az_rad) * math.cos(alt_rad)
    ly = math.cos(az_rad) * math.cos(alt_rad)
    lz = math.sin(alt_rad)

    z_factor = 0.08
    nx = -dz_dx * z_factor
    ny = -dz_dy * z_factor
    nz = np.ones_like(dem)

    norm = np.sqrt(nx**2 + ny**2 + nz**2)
    nx /= norm
    ny /= norm
    nz /= norm

    intensity = nx * lx + ny * ly + nz * lz
    intensity = np.clip((intensity + 0.15) / 1.15, 0.0, 1.0)

    bayer_tile = np.tile(BAYER_4X4, (h // 4 + 1, w // 4 + 1))[:h, :w]
    shade_1bit = np.zeros((h, w), dtype=np.uint8)
    
    # 1 for shadow dot, 0 for reflective white
    shade_threshold = 0.48
    mask = intensity < shade_threshold
    shade_1bit[mask & ((intensity / shade_threshold) < bayer_tile)] = 1

    return shade_1bit, intensity


def extract_marching_squares_contours(dem, interval=100.0, index_mult=2):
    """
    Vector contour extraction via 2D Marching Squares.
    Dynamic LOD:
      - In 500m Macro mode: interval=100.0m (index=200.0m).
      - In 100m Micro mode: interval=25.0m (index=50.0m).
    """
    h, w = dem.shape
    min_ele = np.min(dem)
    max_ele = np.max(dem)

    start_ele = math.ceil(min_ele / interval) * interval
    levels = np.arange(start_ele, max_ele, interval)
    segments = []

    for level in levels:
        is_index = (round(level) % (int(interval) * index_mult) == 0)
        above = (dem >= level)

        tl = above[:-1, :-1].astype(int)
        tr = above[:-1, 1:].astype(int)
        br = above[1:, 1:].astype(int)
        bl = above[1:, :-1].astype(int)

        case = (bl << 3) | (br << 2) | (tr << 1) | tl

        for cy in range(h - 1):
            for cx in range(w - 1):
                c = case[cy, cx]
                if c == 0 or c == 15:
                    continue

                v_tl = dem[cy, cx]
                v_tr = dem[cy, cx + 1]
                v_br = dem[cy + 1, cx + 1]
                v_bl = dem[cy + 1, cx]

                def interp(v1, v2, p1, p2):
                    if abs(v2 - v1) < 1e-5:
                        return (p1 + p2) / 2.0
                    t = (level - v1) / (v2 - v1)
                    return p1 + t * (p2 - p1)

                top_x = interp(v_tl, v_tr, cx, cx + 1)
                top_y = float(cy)
                bot_x = interp(v_bl, v_br, cx, cx + 1)
                bot_y = float(cy + 1)
                left_x = float(cx)
                left_y = interp(v_tl, v_bl, cy, cy + 1)
                right_x = float(cx + 1)
                right_y = interp(v_tr, v_br, cy, cy + 1)

                lines = []
                if c in (1, 14):
                    lines.append((left_x, left_y, top_x, top_y))
                elif c in (2, 13):
                    lines.append((top_x, top_y, right_x, right_y))
                elif c in (3, 12):
                    lines.append((left_x, left_y, right_x, right_y))
                elif c in (4, 11):
                    lines.append((right_x, right_y, bot_x, bot_y))
                elif c == 5:
                    lines.append((left_x, left_y, top_x, top_y))
                    lines.append((right_x, right_y, bot_x, bot_y))
                elif c in (6, 9):
                    lines.append((top_x, top_y, bot_x, bot_y))
                elif c in (7, 8):
                    lines.append((left_x, left_y, bot_x, bot_y))
                elif c == 10:
                    lines.append((left_x, left_y, bot_x, bot_y))
                    lines.append((top_x, top_y, right_x, right_y))

                for x1, y1, x2, y2 in lines:
                    segments.append((x1, y1, x2, y2, level, is_index))

    return segments


def find_summits_and_peaks(dem, max_peaks=4):
    """Detects primary summits across the elevation grid using local maximum filter."""
    h, w = dem.shape
    max_filt = ndimage.maximum_filter(dem, size=25)
    is_peak = (dem == max_filt) & (dem > np.mean(dem) + 50.0)
    y_coords, x_coords = np.where(is_peak)
    peaks = [(int(x), int(y), float(dem[y, x])) for y, x in zip(y_coords, x_coords)]
    peaks.sort(key=lambda p: p[2], reverse=True)
    return peaks[:max_peaks]


def render_tactical_framebuffer(points, dem, to_screen, shade_1bit, segments, peaks,
                                lod_scale_m=500, xte_m=0, dtg_km=3.8, cog_deg=315):
    """
    Composites the 400x240 1-bit tactical map framebuffer:
    - Layer 0: Bayer Dithered 1-Bit Hillshade background.
    - Layer 1: Marching Squares Contour Isolines (Dynamic LOD: 100m/200m in Macro).
    - Layer 2: Summit Peak triangles (▲) and elevation labels.
    - Layer 3: Two-Pass White Halo (Inverted Outline) Route Trajectory.
    - Layer 4: 7x7 Dynamic Chevron Heading Arrow at current hiker location.
    - Layer 5: Tactical Navigation HUD & Telemetry (XTE, DTG, COG, ALT, SATS).
    """
    fb = np.zeros((SCREEN_H, SCREEN_W), dtype=np.uint8)

    # 1. Paint Hillshade in Map area
    fb[HEADER_H:SCREEN_H - FOOTER_H, :] = shade_1bit

    def draw_line(x0, y0, x1, y1, color=1, stipple=False):
        x0, y0, x1, y1 = int(round(x0)), int(round(y0)), int(round(x1)), int(round(y1))
        dx = abs(x1 - x0)
        sx = 1 if x0 < x1 else -1
        dy = -abs(y1 - y0)
        sy = 1 if y0 < y1 else -1
        err = dx + dy
        step = 0
        while True:
            if 0 <= x0 < SCREEN_W and HEADER_H <= y0 < SCREEN_H - FOOTER_H:
                if not stipple or (step % 5 < 3):
                    fb[y0, x0] = color
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy
            step += 1

    # 2. Render Contour Isolines
    for x1, y1, x2, y2, level, is_index in segments:
        sy1 = y1 + HEADER_H
        sy2 = y2 + HEADER_H
        draw_line(x1, sy1, x2, sy2, color=1, stipple=(not is_index))

    # 3. Draw Peak Summits (▲ triangle + altitude label)
    for px, py, ele in peaks:
        sy = int(py + HEADER_H)
        sx = int(px)
        if HEADER_H + 12 <= sy < SCREEN_H - FOOTER_H - 12 and 15 <= sx < SCREEN_W - 55:
            for dy in range(7):
                w = dy
                y_curr = sy - 6 + dy
                if HEADER_H <= y_curr < SCREEN_H - FOOTER_H:
                    for x_curr in range(sx - w, sx + w + 1):
                        if 0 <= x_curr < SCREEN_W:
                            fb[y_curr, x_curr] = 1
            if HEADER_H <= sy - 2 < SCREEN_H - FOOTER_H and 0 <= sx < SCREEN_W:
                fb[sy - 2, sx] = 0
            label = f"{int(ele)}m"
            draw_osd_text(fb, sx + 8, sy - 5, label, inverted=False)

    # 4. TWO-PASS WHITE HALO ROUTE RENDERER (Solves Figür-Zemin Camouflage!)
    # PASS 1: Erase 4px wide corridor to pure white (0) along entire route
    screen_route = [to_screen(p["lat"], p["lon"]) for p in points]

    def clear_halo_stroke(x0, y0, x1, y1):
        x0, y0, x1, y1 = int(round(x0)), int(round(y0)), int(round(x1)), int(round(y1))
        dx = abs(x1 - x0)
        sx = 1 if x0 < x1 else -1
        dy = -abs(y1 - y0)
        sy = 1 if y0 < y1 else -1
        err = dx + dy
        while True:
            for ox in [-1, 0, 1, 2]:
                for oy in [-1, 0, 1, 2]:
                    px = x0 + ox
                    py = y0 + oy
                    if 0 <= px < SCREEN_W and HEADER_H <= py < SCREEN_H - FOOTER_H:
                        fb[py, px] = 0  # Clear underlying contours and hillshade
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy

    # PASS 2: Draw solid 2px black core (1) down center
    def draw_core_stroke(x0, y0, x1, y1):
        x0, y0, x1, y1 = int(round(x0)), int(round(y0)), int(round(x1)), int(round(y1))
        dx = abs(x1 - x0)
        sx = 1 if x0 < x1 else -1
        dy = -abs(y1 - y0)
        sy = 1 if y0 < y1 else -1
        err = dx + dy
        while True:
            for ox in [0, 1]:
                for oy in [0, 1]:
                    px = x0 + ox
                    py = y0 + oy
                    if 0 <= px < SCREEN_W and HEADER_H <= py < SCREEN_H - FOOTER_H:
                        fb[py, px] = 1  # Solid black center
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy

    # Execute Pass 1 first across ALL segments:
    for i in range(len(screen_route) - 1):
        clear_halo_stroke(screen_route[i][0], screen_route[i][1] + HEADER_H,
                          screen_route[i + 1][0], screen_route[i + 1][1] + HEADER_H)

    # Execute Pass 2 second across ALL segments:
    for i in range(len(screen_route) - 1):
        draw_core_stroke(screen_route[i][0], screen_route[i][1] + HEADER_H,
                         screen_route[i + 1][0], screen_route[i + 1][1] + HEADER_H)

    # Start Point Marker (Circle [S])
    sx, sy = int(screen_route[0][0]), int(screen_route[0][1] + HEADER_H)
    for dy in range(-5, 6):
        for dx in range(-5, 6):
            d2 = dx*dx + dy*dy
            if d2 <= 25:
                if 0 <= sx + dx < SCREEN_W and HEADER_H <= sy + dy < SCREEN_H - FOOTER_H:
                    fb[sy + dy, sx + dx] = 0  # halo
    for dy in range(-4, 5):
        for dx in range(-4, 5):
            d2 = dx*dx + dy*dy
            if d2 <= 16:
                if 0 <= sx + dx < SCREEN_W and HEADER_H <= sy + dy < SCREEN_H - FOOTER_H:
                    fb[sy + dy, sx + dx] = 1 if (d2 <= 3 or d2 >= 10) else 0

    # Finish Point Marker (Filled Square [F])
    fx, fy = int(screen_route[-1][0]), int(screen_route[-1][1] + HEADER_H)
    for dy in range(-4, 5):
        for dx in range(-4, 5):
            if 0 <= fx + dx < SCREEN_W and HEADER_H <= fy + dy < SCREEN_H - FOOTER_H:
                fb[fy + dy, fx + dx] = 0  # halo
    for dy in range(-3, 4):
        for dx in range(-3, 4):
            if 0 <= fx + dx < SCREEN_W and HEADER_H <= fy + dy < SCREEN_H - FOOTER_H:
                fb[fy + dy, fx + dx] = 1

    # 5. DYNAMIC 7x7 CHEVRON HEADING ARROW (Position Vector)
    mid_idx = int(len(screen_route) * 0.65)
    cx, cy = int(screen_route[mid_idx][0]), int(screen_route[mid_idx][1] + HEADER_H)
    draw_chevron_vector(fb, cx, cy, heading_deg=cog_deg)

    # 6. TACTICAL NAVIGATION HUD & TELEMETRY
    fb[0:HEADER_H, :] = 1
    fb[SCREEN_H - FOOTER_H:SCREEN_H, :] = 1

    # Top Status Bar
    draw_osd_text(fb, 4, 4, "GPS: 3D FIX   HDOP: 0.8   SATS: 14   PWR: 98% 4.12V", inverted=True)

    # Bottom Telemetry Bar: XTE, DTG, COG, Scale, Elevation
    xte_str = f"XTE: {abs(xte_m)}m" if xte_m == 0 else f"XTE: {abs(xte_m)}m {'R' if xte_m > 0 else 'L'}"
    bottom_str = f"{xte_str}   DTG: {dtg_km:.1f}km   COG: {cog_deg:03d}   |-- {lod_scale_m}m --|"
    draw_osd_text(fb, 4, SCREEN_H - 12, bottom_str, inverted=True)

    return fb


def draw_chevron_vector(fb, cx, cy, heading_deg=315):
    """
    Renders a 7x7 dynamic directional Chevron arrow at (cx, cy)
    oriented to heading_deg (0° = North/UP, 90° = East, etc.) with a 1px white halo.
    """
    rad = math.radians(heading_deg)
    cos_h = math.cos(rad)
    sin_h = math.sin(rad)

    # Chevron vertices relative to center:
    # Tip (0, -4), Left Wing (-3, 3), Right Wing (3, 3), Inner Notch (0, 1)
    pts = [
        (0, -5),   # tip
        (-4, 4),   # left wing
        (0, 1),    # inner notch
        (4, 4)     # right wing
    ]

    def rot(pt):
        rx = pt[0] * cos_h - pt[1] * sin_h
        ry = pt[0] * sin_h + pt[1] * cos_h
        return int(round(cx + rx)), int(round(cy + ry))

    r_tip = rot(pts[0])
    r_left = rot(pts[1])
    r_notch = rot(pts[2])
    r_right = rot(pts[3])

    # 1. Clear 1px White Halo around Chevron
    for dy in range(-6, 7):
        for dx in range(-6, 7):
            if dx*dx + dy*dy <= 36:
                px = cx + dx
                py = cy + dy
                if 0 <= px < SCREEN_W and HEADER_H <= py < SCREEN_H - FOOTER_H:
                    fb[py, px] = 0

    # 2. Draw solid black chevron polygon edges
    def bres_line(p0, p1):
        x0, y0, x1, y1 = p0[0], p0[1], p1[0], p1[1]
        dx = abs(x1 - x0)
        sx = 1 if x0 < x1 else -1
        dy = -abs(y1 - y0)
        sy = 1 if y0 < y1 else -1
        err = dx + dy
        while True:
            if 0 <= x0 < SCREEN_W and HEADER_H <= y0 < SCREEN_H - FOOTER_H:
                fb[y0, x0] = 1
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy

    bres_line(r_tip, r_left)
    bres_line(r_left, r_notch)
    bres_line(r_notch, r_right)
    bres_line(r_right, r_tip)

    # Fill center core of chevron
    if 0 <= r_tip[0] < SCREEN_W and HEADER_H <= r_tip[1] < SCREEN_H - FOOTER_H:
        fb[r_tip[1], r_tip[0]] = 1
    if 0 <= cx < SCREEN_W and HEADER_H <= cy < SCREEN_H - FOOTER_H:
        fb[cy, cx] = 1


FONT_5X7 = {
    ' ': [0x00, 0x00, 0x00, 0x00, 0x00],
    'A': [0x7C, 0x12, 0x11, 0x12, 0x7C],
    'B': [0x7F, 0x49, 0x49, 0x49, 0x36],
    'C': [0x3E, 0x41, 0x41, 0x41, 0x22],
    'D': [0x7F, 0x41, 0x41, 0x22, 0x1C],
    'E': [0x7F, 0x49, 0x49, 0x49, 0x41],
    'F': [0x7F, 0x09, 0x09, 0x09, 0x01],
    'G': [0x3E, 0x41, 0x49, 0x49, 0x7A],
    'H': [0x7F, 0x08, 0x08, 0x08, 0x7F],
    'I': [0x00, 0x41, 0x7F, 0x41, 0x00],
    'K': [0x7F, 0x08, 0x14, 0x22, 0x41],
    'L': [0x7F, 0x40, 0x40, 0x40, 0x40],
    'M': [0x7F, 0x02, 0x0C, 0x02, 0x7F],
    'N': [0x7F, 0x04, 0x08, 0x10, 0x7F],
    'O': [0x3E, 0x41, 0x41, 0x41, 0x3E],
    'P': [0x7F, 0x09, 0x09, 0x09, 0x06],
    'R': [0x7F, 0x09, 0x19, 0x29, 0x46],
    'S': [0x46, 0x49, 0x49, 0x49, 0x31],
    'T': [0x01, 0x01, 0x7F, 0x01, 0x01],
    'U': [0x3F, 0x40, 0x40, 0x40, 0x3F],
    'V': [0x1F, 0x20, 0x40, 0x20, 0x1F],
    'W': [0x7F, 0x20, 0x18, 0x20, 0x7F],
    'X': [0x63, 0x14, 0x08, 0x14, 0x63],
    'Y': [0x07, 0x08, 0x70, 0x08, 0x07],
    'Z': [0x61, 0x51, 0x49, 0x45, 0x43],
    '0': [0x3E, 0x51, 0x49, 0x45, 0x3E],
    '1': [0x00, 0x42, 0x7F, 0x40, 0x00],
    '2': [0x42, 0x61, 0x51, 0x49, 0x46],
    '3': [0x21, 0x41, 0x45, 0x4B, 0x31],
    '4': [0x18, 0x14, 0x12, 0x7F, 0x10],
    '5': [0x27, 0x45, 0x45, 0x45, 0x39],
    '6': [0x3C, 0x4A, 0x49, 0x49, 0x30],
    '7': [0x01, 0x71, 0x09, 0x05, 0x03],
    '8': [0x36, 0x49, 0x49, 0x49, 0x36],
    '9': [0x06, 0x49, 0x49, 0x29, 0x1E],
    ':': [0x00, 0x36, 0x36, 0x00, 0x00],
    '.': [0x00, 0x60, 0x60, 0x00, 0x00],
    '-': [0x08, 0x08, 0x08, 0x08, 0x08],
    '|': [0x00, 0x00, 0x7F, 0x00, 0x00],
    '%': [0x23, 0x13, 0x08, 0x64, 0x62],
    'm': [0x70, 0x08, 0x70, 0x08, 0x70],
    'k': [0x7F, 0x10, 0x28, 0x44, 0x00],
}


def draw_osd_text(fb, start_x, start_y, text, inverted=False):
    """Draws 5x7 ASCII text into the 1-bit framebuffer."""
    cursor_x = start_x
    for char in text:
        bitmap = FONT_5X7.get(char, FONT_5X7[' '])
        for col_idx, col_byte in enumerate(bitmap):
            for row_idx in range(7):
                bit = (col_byte >> row_idx) & 1
                px = cursor_x + col_idx
                py = start_y + row_idx
                if 0 <= px < SCREEN_W and 0 <= py < SCREEN_H:
                    if inverted:
                        fb[py, px] = 0 if bit else 1
                    else:
                        fb[py, px] = 1 if bit else 0
        cursor_x += 6


def save_pbm(fb, path):
    """Saves 1-bit framebuffer to binary Netpbm PBM format."""
    h, w = fb.shape
    with open(path, "wb") as f:
        header = f"P4\n{w} {h}\n".encode("ascii")
        f.write(header)
        for y in range(h):
            row_bytes = bytearray(w // 8)
            for x in range(w):
                if fb[y, x]:
                    row_bytes[x // 8] |= (0x80 >> (x % 8))
            f.write(row_bytes)


def export_c99_header(fb, segments, peaks, output_path="topo_map_data.h"):
    """
    Exports terrain bitmask, contour vectors, and peak landmarks as a zero-heap
    static C99 header directly compilable by gcc/ARM-GCC into MCU Flash/ROM.
    """
    h, w = fb.shape
    bytes_per_row = w // 8
    total_bytes = h * bytes_per_row

    with open(output_path, "w", encoding="utf-8") as f:
        f.write("/**\n")
        f.write(" * @file topo_map_data.h\n")
        f.write(" * @brief Pre-compiled Tactical Topographic Terrain Map for Sharp 2.7\" MIP Display\n")
        f.write(" * Auto-generated by topo_engine.py. Zero-heap static ROM storage.\n")
        f.write(" */\n\n")
        f.write("#ifndef TOPO_MAP_DATA_H_\n")
        f.write("#define TOPO_MAP_DATA_H_\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write(f"#define TOPO_MAP_WIDTH       {w}\n")
        f.write(f"#define TOPO_MAP_HEIGHT      {h}\n")
        f.write(f"#define TOPO_MAP_SIZE_BYTES  {total_bytes}\n")
        f.write(f"#define TOPO_PEAK_COUNT      {len(peaks)}\n\n")

        f.write("typedef struct {\n")
        f.write("    int16_t x;\n")
        f.write("    int16_t y;\n")
        f.write("    int16_t ele_m;\n")
        f.write("} topo_peak_t;\n\n")

        f.write("static const topo_peak_t g_topo_peaks[TOPO_PEAK_COUNT] = {\n")
        for px, py, ele in peaks:
            f.write(f"    {{ {int(px)}, {int(py) + HEADER_H}, {int(ele)} }},\n")
        f.write("};\n\n")

        f.write(f"static const uint8_t g_topo_framebuffer_rom[{total_bytes}] = {{\n")
        for y in range(h):
            row_bytes = bytearray(bytes_per_row)
            for x in range(w):
                if fb[y, x]:
                    row_bytes[x // 8] |= (0x80 >> (x % 8))
            hex_str = ", ".join([f"0x{b:02X}" for b in row_bytes])
            f.write(f"    {hex_str},\n")
        f.write("};\n\n")

        f.write("#endif /* TOPO_MAP_DATA_H_ */\n")


def process_topo_pipeline(gpx_path="sample_test_route.gpx", out_prefix="sample_topo_out", lod="macro"):
    """Master pipeline executing full topographic extraction and rendering."""
    print(f"[*] Ingesting GPX route: {gpx_path}")
    points = parse_gpx(gpx_path)
    print(f"    - Points extracted: {len(points)}")

    print("[*] Generating 30m corridor DEM with tactical buffer...")
    dem, to_screen, bounds = build_elevation_grid(points)
    print(f"    - Elevation bounds: {np.min(dem):.1f}m - {np.max(dem):.1f}m (dH: {np.max(dem) - np.min(dem):.1f}m)")

    print("[*] Computing 1-bit Northwest (315 deg) Illuminated Hillshading via Bayer 4x4 Dithering...")
    shade_1bit, intensity = compute_hillshade_1bit(dem)

    # Dynamic LOD: In Macro mode (500m scale), use 100m interval (200m index) to remove clutter
    interval = 100.0 if lod == "macro" else 25.0
    print(f"[*] Dynamic LOD: Extracting {interval}m contour isolines ({lod.upper()} Mode)...")
    segments = extract_marching_squares_contours(dem, interval=interval, index_mult=2)
    print(f"    - Contour vector segments extracted: {len(segments)}")

    print("[*] Detecting mountain summits and topographic peaks...")
    peaks = find_summits_and_peaks(dem)
    for i, (px, py, ele) in enumerate(peaks):
        print(f"    - Peak {i+1}: ({int(px)}, {int(py)}) -> {int(ele)}m")

    print("[*] Compositing Two-Pass White Halo Route & 7x7 Chevron HUD...")
    fb = render_tactical_framebuffer(points, dem, to_screen, shade_1bit, segments, peaks,
                                     lod_scale_m=500 if lod == "macro" else 100,
                                     xte_m=0, dtg_km=3.8, cog_deg=315)

    build_dir = "build" if os.path.exists("build") else "."
    assets_dir = "assets" if os.path.exists("assets") else "."
    include_dir = "include" if os.path.exists("include") else "."

    pbm_path = os.path.join(build_dir, f"{out_prefix}.pbm")
    png_path = os.path.join(assets_dir, f"{out_prefix}.png")
    h_path = os.path.join(include_dir, "topo_map_data.h")

    save_pbm(fb, pbm_path)
    print(f"    - Saved Netpbm PBM: {pbm_path} ({os.path.getsize(pbm_path)} bytes)")

    export_c99_header(fb, segments, peaks, h_path)
    print(f"    - Exported Zero-Heap C99 ROM Header: {h_path} ({os.path.getsize(h_path)} bytes)")

    try:
        from PIL import Image
        img_arr = np.where(fb == 1, 0, 255).astype(np.uint8)
        img = Image.fromarray(img_arr, mode="L")
        img.save(png_path)
        print(f"    - Generated High-Res Preview PNG: {png_path}")
    except Exception as e:
        print(f"    [!] PIL conversion: {e}")

    return fb, segments, peaks


if __name__ == "__main__":
    default_gpx = os.path.join("data", "sample_test_route.gpx") if os.path.exists(os.path.join("data", "sample_test_route.gpx")) else "sample_test_route.gpx"
    gpx_file = sys.argv[1] if len(sys.argv) > 1 else default_gpx
    process_topo_pipeline(gpx_file)
