#!/usr/bin/env python3
"""
cgpx_tool.py - Production-grade GPX Lossless Compression, AES-128-CTR Encryption,
and Host Visualizer / MIP Simulator Toolchain.

Features:
  - Ingests arbitrary .gpx files (tracks, routes, waypoints, multi-segment)
  - Quantizes to 1e7 fixed-point (~1.11 cm resolution)
  - DPCM differential encoding + signed 32-bit ZigZag + LEB128 Varint
  - AES-128-CTR encryption + IEEE 802.3 CRC-32 integrity
  - Generates standalone .cgpx binary and compile-ready <output>_data.h
  - view command: Dual-panel Matplotlib UI with cos(mean_lat) aspect ratio
  - sim-mip command: 400x240 1-bit monochrome MIP display emulator (PNG + ASCII)
"""

import argparse
import binascii
import math
import os
import struct
import sys
import xml.etree.ElementTree as ET
from datetime import datetime
from typing import List, Tuple, Optional, Dict, Any

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.backends import default_backend

# ---------------------------------------------------------------------------
# Binary Container Constants & Struct Specification
# ---------------------------------------------------------------------------
CGPX_MAGIC = 0x58504743        # "CGPX" in little-endian uint32
CGPX_VERSION = 0x0100          # v1.0
CGPX_FLAG_ENCRYPTED = 0x0001   # Bit 0 = AES-128-CTR
CGPX_HEADER_SIZE = 68          # 68 bytes total (0x44)

# Format:
# 0x00: uint32 magic
# 0x04: uint16 version
# 0x06: uint16 flags
# 0x08: uint32 point_count
# 0x0C: int32  ref_lat
# 0x10: int32  ref_lon
# 0x14: int16  ref_ele
# 0x16: int32  min_lat
# 0x1A: int32  max_lat
# 0x1E: int32  min_lon
# 0x22: int32  max_lon
# 0x26: int16  min_ele
# 0x28: int16  max_ele
# 0x2A: 16s    nonce
# 0x3A: uint32 payload_size
# 0x3E: uint32 crc32
# 0x42: 2s     reserved
HEADER_STRUCT = struct.Struct("<IHHIiih iiii hh 16s II 2s")
assert HEADER_STRUCT.size == CGPX_HEADER_SIZE, f"Header size must be 68, got {HEADER_STRUCT.size}"


# ---------------------------------------------------------------------------
# ZigZag & Varint (LEB128) Codecs
# ---------------------------------------------------------------------------
def zigzag_encode(n: int) -> int:
    """Signed 32-bit integer to unsigned ZigZag representation."""
    if n >= 0:
        return n << 1
    else:
        return ((-n) << 1) - 1


def zigzag_decode(z: int) -> int:
    """Unsigned ZigZag representation to signed 32-bit integer."""
    if (z & 1) == 0:
        return z >> 1
    else:
        return -((z + 1) >> 1)


def encode_varint(value: int) -> bytes:
    """Encodes an unsigned integer into Variable-Byte LEB128."""
    assert value >= 0, "Varint only encodes non-negative integers"
    buf = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        if value:
            buf.append(byte | 0x80)
        else:
            buf.append(byte)
            break
    return bytes(buf)


def decode_varint_from_bytes(data: bytes, offset: int) -> Tuple[int, int]:
    """
    Decodes an unsigned LEB128 varint from data starting at offset.
    Returns (value, new_offset).
    """
    res = 0
    shift = 0
    while True:
        if offset >= len(data):
            raise EOFError("Unexpected end of stream while reading varint")
        byte = data[offset]
        offset += 1
        res |= (byte & 0x7F) << shift
        if not (byte & 0x80):
            break
        shift += 7
    return res, offset


# ---------------------------------------------------------------------------
# Cryptographic Kernel (AES-128-CTR)
# ---------------------------------------------------------------------------
def parse_key(key_input: Optional[str]) -> bytes:
    """Normalizes a 16-byte cryptographic key from hex string or text."""
    if not key_input:
        # Deterministic default tactical key if none provided
        return b"\x01\x23\x45\x67\x89\xAB\xCD\xEF\xFE\xDC\xBA\x98\x76\x54\x32\x10"
    
    # Try hex parsing
    clean_hex = key_input.strip().replace(" ", "").replace("0x", "")
    if len(clean_hex) == 32:
        try:
            return bytes.fromhex(clean_hex)
        except ValueError:
            pass
            
    # UTF-8 text fallback
    key_bytes = key_input.encode("utf-8")
    if len(key_bytes) == 16:
        return key_bytes
    elif len(key_bytes) < 16:
        return key_bytes.ljust(16, b"\x00")
    else:
        return key_bytes[:16]


def aes128_ctr_crypt(data: bytes, key: bytes, nonce: bytes) -> bytes:
    """Encrypts or decrypts data using AES-128 in Counter Mode."""
    assert len(key) == 16, "AES-128 requires exactly 16-byte key"
    assert len(nonce) == 16, "AES-128-CTR requires 16-byte initial counter"
    cipher = Cipher(algorithms.AES(key), modes.CTR(nonce), backend=default_backend())
    encryptor = cipher.encryptor()
    return encryptor.update(data) + encryptor.finalize()


# ---------------------------------------------------------------------------
# GPX Parsing & Data Models
# ---------------------------------------------------------------------------
class TrackPoint:
    __slots__ = ("lat", "lon", "ele", "time_s")

    def __init__(self, lat: float, lon: float, ele: float = 0.0, time_s: int = 0):
        self.lat = lat
        self.lon = lon
        self.ele = ele
        self.time_s = time_s

    @property
    def lat_int(self) -> int:
        return int(round(self.lat * 1e7))

    @property
    def lon_int(self) -> int:
        return int(round(self.lon * 1e7))

    @property
    def ele_int(self) -> int:
        # Clamped to [-500, +9000] meters per spec
        return max(-500, min(9000, int(round(self.ele))))


def parse_iso_time(time_str: Optional[str]) -> Optional[int]:
    """Parses ISO 8601 time string to epoch seconds."""
    if not time_str:
        return None
    time_str = time_str.strip()
    try:
        # Replace Z with UTC offset
        if time_str.endswith("Z"):
            time_str = time_str[:-1] + "+00:00"
        dt = datetime.fromisoformat(time_str)
        return int(dt.timestamp())
    except Exception:
        # Try basic formats
        for fmt in ("%Y-%m-%dT%H:%M:%SZ", "%Y-%m-%dT%H:%M:%S", "%Y-%m-%d %H:%M:%S"):
            try:
                dt = datetime.strptime(time_str, fmt)
                return int(dt.timestamp())
            except Exception:
                pass
    return None


def parse_gpx_file(file_path: str) -> List[TrackPoint]:
    """
    Robust GPX XML parser handling trkpt, rtept, and wpt with graceful fallbacks
    for missing timestamps, missing elevations, and multi-segment tracks.
    """
    if not os.path.exists(file_path):
        raise FileNotFoundError(f"GPX file not found: {file_path}")

    tree = ET.parse(file_path)
    root = tree.getroot()

    # Namespace stripping for robust matching
    ns = ""
    if root.tag.startswith("{"):
        ns = root.tag.split("}")[0] + "}"

    raw_points: List[TrackPoint] = []
    
    # Check trkpt, rtept, wpt in priority
    pt_tags = [f"{ns}trkpt", f"{ns}rtept", f"{ns}wpt"]
    found_nodes = []
    for tag in pt_tags:
        nodes = root.iter(tag)
        found_nodes.extend(nodes)

    if not found_nodes:
        # Generic scan without namespace
        for elem in root.iter():
            local_tag = elem.tag.split("}")[-1] if "}" in elem.tag else elem.tag
            if local_tag in ("trkpt", "rtept", "wpt"):
                found_nodes.append(elem)

    if not found_nodes:
        raise ValueError(f"No valid track/route/waypoint elements found in {file_path}")

    last_time = 0
    last_ele = 0.0

    for elem in found_nodes:
        lat_attr = elem.attrib.get("lat")
        lon_attr = elem.attrib.get("lon")
        if lat_attr is None or lon_attr is None:
            continue

        try:
            lat = float(lat_attr)
            lon = float(lon_attr)
        except ValueError:
            continue

        # Sanity check WGS84 range
        if not (-90.0 <= lat <= 90.0 and -180.0 <= lon <= 180.0):
            continue

        # Elevation
        ele_elem = elem.find(f"{ns}ele") if ns else elem.find("ele")
        if ele_elem is None:
            ele_elem = elem.find("ele")
        if ele_elem is not None and ele_elem.text:
            try:
                ele = float(ele_elem.text.strip())
                last_ele = ele
            except ValueError:
                ele = last_ele
        else:
            ele = last_ele

        # Time
        time_elem = elem.find(f"{ns}time") if ns else elem.find("time")
        if time_elem is None:
            time_elem = elem.find("time")
        parsed_t = parse_iso_time(time_elem.text if time_elem is not None else None)
        if parsed_t is not None:
            time_s = parsed_t
            last_time = time_s
        else:
            # Fallback to 1-second increment
            last_time += 1
            time_s = last_time

        raw_points.append(TrackPoint(lat=lat, lon=lon, ele=ele, time_s=time_s))

    if not raw_points:
        raise ValueError(f"No valid coordinates could be extracted from {file_path}")

    return raw_points


# ---------------------------------------------------------------------------
# Compression & Packaging Core
# ---------------------------------------------------------------------------
def compress_and_pack_points(points: List[TrackPoint], key: bytes, encrypt: bool = True) -> Tuple[bytes, Dict[str, Any]]:
    """
    Compresses a list of TrackPoints into a CGPX binary stream.
    Returns (cgpx_bytes, metadata_dict).
    """
    assert len(points) > 0, "Cannot pack 0 points"

    n_points = len(points)
    p0 = points[0]

    ref_lat = p0.lat_int
    ref_lon = p0.lon_int
    ref_ele = p0.ele_int

    min_lat = min(p.lat_int for p in points)
    max_lat = max(p.lat_int for p in points)
    min_lon = min(p.lon_int for p in points)
    max_lon = max(p.lon_int for p in points)
    min_ele = min(p.ele_int for p in points)
    max_ele = max(p.ele_int for p in points)

    # Encode differential payload
    payload_buf = bytearray()

    prev_lat = ref_lat
    prev_lon = ref_lon
    prev_ele = ref_ele
    prev_time = p0.time_s

    for i in range(1, n_points):
        p = points[i]
        d_lat = p.lat_int - prev_lat
        d_lon = p.lon_int - prev_lon
        d_ele = p.ele_int - prev_ele
        d_time = max(0, p.time_s - prev_time)

        # ZigZag for signed deltas
        z_lat = zigzag_encode(d_lat)
        z_lon = zigzag_encode(d_lon)
        z_ele = zigzag_encode(d_ele)

        # Varint LEB128
        payload_buf.extend(encode_varint(z_lat))
        payload_buf.extend(encode_varint(z_lon))
        payload_buf.extend(encode_varint(z_ele))
        payload_buf.extend(encode_varint(d_time))

        prev_lat = p.lat_int
        prev_lon = p.lon_int
        prev_ele = p.ele_int
        prev_time = p.time_s

    raw_payload = bytes(payload_buf)
    payload_size = len(raw_payload)

    # Nonce generation
    nonce = os.urandom(16)

    # Encryption
    flags = CGPX_FLAG_ENCRYPTED if encrypt else 0
    if encrypt:
        ciphertext = aes128_ctr_crypt(raw_payload, key, nonce)
    else:
        ciphertext = raw_payload

    # CRC-32 over encrypted payload
    crc32_val = binascii.crc32(ciphertext) & 0xFFFFFFFF

    # Construct 68-byte packed header
    header_bytes = HEADER_STRUCT.pack(
        CGPX_MAGIC,
        CGPX_VERSION,
        flags,
        n_points,
        ref_lat,
        ref_lon,
        ref_ele,
        min_lat,
        max_lat,
        min_lon,
        max_lon,
        min_ele,
        max_ele,
        nonce,
        payload_size,
        crc32_val,
        b"\x00\x00"
    )

    full_cgpx = header_bytes + ciphertext

    meta = {
        "point_count": n_points,
        "ref_lat": ref_lat,
        "ref_lon": ref_lon,
        "ref_ele": ref_ele,
        "min_lat": min_lat,
        "max_lat": max_lat,
        "min_lon": min_lon,
        "max_lon": max_lon,
        "min_ele": min_ele,
        "max_ele": max_ele,
        "payload_size": payload_size,
        "crc32": crc32_val,
        "nonce": nonce,
        "key": key,
        "flags": flags,
        "total_size": len(full_cgpx),
        "bytes_per_point": (payload_size / n_points) if n_points else 0
    }

    return full_cgpx, meta


def unpack_and_decompress_cgpx(cgpx_bytes: bytes, key: bytes) -> Tuple[List[TrackPoint], Dict[str, Any]]:
    """
    Decompresses and decrypts a CGPX binary stream.
    Returns (points_list, header_metadata).
    """
    if len(cgpx_bytes) < CGPX_HEADER_SIZE:
        raise ValueError(f"Data too short for CGPX header ({len(cgpx_bytes)} < {CGPX_HEADER_SIZE})")

    (
        magic, version, flags, point_count,
        ref_lat, ref_lon, ref_ele,
        min_lat, max_lat, min_lon, max_lon,
        min_ele, max_ele,
        nonce, payload_size, crc32_val, reserved
    ) = HEADER_STRUCT.unpack(cgpx_bytes[:CGPX_HEADER_SIZE])

    if magic != CGPX_MAGIC:
        raise ValueError(f"Invalid CGPX Magic: 0x{magic:08X} (expected 0x{CGPX_MAGIC:08X})")

    ciphertext = cgpx_bytes[CGPX_HEADER_SIZE : CGPX_HEADER_SIZE + payload_size]
    if len(ciphertext) != payload_size:
        raise ValueError(f"Payload truncated: expected {payload_size} bytes, got {len(ciphertext)}")

    # Verify CRC32
    actual_crc = binascii.crc32(ciphertext) & 0xFFFFFFFF
    if actual_crc != crc32_val:
        raise ValueError(f"CRC-32 mismatch! Expected 0x{crc32_val:08X}, got 0x{actual_crc:08X}")

    # Decrypt if encrypted
    if flags & CGPX_FLAG_ENCRYPTED:
        raw_payload = aes128_ctr_crypt(ciphertext, key, nonce)
    else:
        raw_payload = ciphertext

    # Stream decode
    points: List[TrackPoint] = []
    
    # Point 0 from header
    p0 = TrackPoint(lat=ref_lat / 1e7, lon=ref_lon / 1e7, ele=float(ref_ele), time_s=0)
    points.append(p0)

    cur_lat = ref_lat
    cur_lon = ref_lon
    cur_ele = ref_ele
    cur_time = 0
    offset = 0

    for _ in range(1, point_count):
        z_lat, offset = decode_varint_from_bytes(raw_payload, offset)
        z_lon, offset = decode_varint_from_bytes(raw_payload, offset)
        z_ele, offset = decode_varint_from_bytes(raw_payload, offset)
        d_time, offset = decode_varint_from_bytes(raw_payload, offset)

        d_lat = zigzag_decode(z_lat)
        d_lon = zigzag_decode(z_lon)
        d_ele = zigzag_decode(z_ele)

        cur_lat += d_lat
        cur_lon += d_lon
        cur_ele += d_ele
        cur_time += d_time

        points.append(TrackPoint(
            lat=cur_lat / 1e7,
            lon=cur_lon / 1e7,
            ele=float(cur_ele),
            time_s=cur_time
        ))

    meta = {
        "point_count": point_count,
        "ref_lat": ref_lat,
        "ref_lon": ref_lon,
        "ref_ele": ref_ele,
        "min_lat": min_lat,
        "max_lat": max_lat,
        "min_lon": min_lon,
        "max_lon": max_lon,
        "min_ele": min_ele,
        "max_ele": max_ele,
        "payload_size": payload_size,
        "crc32": crc32_val,
        "nonce": nonce,
        "flags": flags
    }

    return points, meta


# ---------------------------------------------------------------------------
# C Header Generator (<output>_data.h)
# ---------------------------------------------------------------------------
def generate_c_header(cgpx_bytes: bytes, meta: Dict[str, Any], symbol_prefix: str = "ROUTE") -> str:
    """Generates a clean, compile-ready ANSI C99 header."""
    hex_lines = []
    for i in range(0, len(cgpx_bytes), 16):
        chunk = cgpx_bytes[i : i + 16]
        hex_lines.append("    " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")

    key_hex = ", ".join(f"0x{b:02X}" for b in meta["key"])
    nonce_hex = ", ".join(f"0x{b:02X}" for b in meta["nonce"])

    header_code = f"""/**
 * @file {symbol_prefix.lower()}_data.h
 * @brief Auto-generated CGPX trajectory binary container and static metadata.
 * Generated by cgpx_tool.py on {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}.
 * Total Points: {meta['point_count']} | Payload Size: {meta['payload_size']} B | Header: {CGPX_HEADER_SIZE} B
 */

#ifndef {symbol_prefix.upper()}_DATA_H
#define {symbol_prefix.upper()}_DATA_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {{
#endif

#define {symbol_prefix.upper()}_POINT_COUNT    ({meta['point_count']}U)
#define {symbol_prefix.upper()}_REF_LAT        ({meta['ref_lat']})
#define {symbol_prefix.upper()}_REF_LON        ({meta['ref_lon']})
#define {symbol_prefix.upper()}_REF_ELE        ({meta['ref_ele']})
#define {symbol_prefix.upper()}_MIN_LAT        ({meta['min_lat']})
#define {symbol_prefix.upper()}_MAX_LAT        ({meta['max_lat']})
#define {symbol_prefix.upper()}_MIN_LON        ({meta['min_lon']})
#define {symbol_prefix.upper()}_MAX_LON        ({meta['max_lon']})
#define {symbol_prefix.upper()}_MIN_ELE        ({meta['min_ele']})
#define {symbol_prefix.upper()}_MAX_ELE        ({meta['max_ele']})
#define {symbol_prefix.upper()}_PAYLOAD_SIZE   ({meta['payload_size']}U)
#define {symbol_prefix.upper()}_CRC32          (0x{meta['crc32']:08X}U)
#define {symbol_prefix.upper()}_TOTAL_SIZE     ({meta['total_size']}U)

/* 16-byte AES-128 cryptographic key */
static const uint8_t {symbol_prefix.upper()}_KEY[16] = {{
    {key_hex}
}};

/* 16-byte AES-128-CTR Initial Counter / Nonce */
static const uint8_t {symbol_prefix.upper()}_NONCE[16] = {{
    {nonce_hex}
}};

/* Complete binary container (68-byte packed header + encrypted payload) */
static const uint8_t {symbol_prefix.upper()}_BLOB[{meta['total_size']}] = {{
{chr(10).join(hex_lines)}
}};

#ifdef __cplusplus
}}
#endif

#endif /* {symbol_prefix.upper()}_DATA_H */
"""
    return header_code


# ---------------------------------------------------------------------------
# Geodetic Computations & Matplotlib Visualizer (`view`)
# ---------------------------------------------------------------------------
def haversine_distance(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    """Computes great-circle distance between two coordinates in meters."""
    R = 6371000.0  # Earth radius in meters
    phi1 = math.radians(lat1)
    phi2 = math.radians(lat2)
    dphi = math.radians(lat2 - lat1)
    dlambda = math.radians(lon2 - lon1)

    a = math.sin(dphi / 2.0) ** 2 + math.cos(phi1) * math.cos(phi2) * math.sin(dlambda / 2.0) ** 2
    c = 2.0 * math.atan2(math.sqrt(a), math.sqrt(1.0 - a))
    return R * c


def render_view_plot(points: List[TrackPoint], meta: Dict[str, Any], output_png: Optional[str] = None, show: bool = True):
    """Renders dual-panel validation UI via Matplotlib."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    lats = [p.lat for p in points]
    lons = [p.lon for p in points]
    eles = [p.ele for p in points]

    # Cumulative distance
    cum_dist_km = [0.0]
    total_d = 0.0
    for i in range(1, len(points)):
        d = haversine_distance(points[i - 1].lat, points[i - 1].lon, points[i].lat, points[i].lon)
        total_d += d
        cum_dist_km.append(total_d / 1000.0)

    mean_lat = (min(lats) + max(lats)) / 2.0
    cos_lat = math.cos(math.radians(mean_lat))
    aspect_ratio = 1.0 / max(1e-5, cos_lat)

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 8), gridspec_kw={"height_ratios": [2, 1]})
    fig.patch.set_facecolor("#181B20")

    # Panel 1: Topographic 2D track plot
    ax1.set_facecolor("#101216")
    ax1.plot(lons, lats, color="#00FFCC", linewidth=1.5, label="Trajectory Path", zorder=3)
    ax1.scatter([lons[0]], [lats[0]], color="#00FF00", s=60, edgecolors="white", label="Start Point", zorder=4)
    ax1.scatter([lons[-1]], [lats[-1]], color="#FF3366", s=60, edgecolors="white", label="End Point", zorder=4)

    ax1.set_aspect(aspect_ratio)
    ax1.set_title(f"CGPX Trajectory Track ({meta['point_count']} points, {cum_dist_km[-1]:.2f} km)",
                  color="#FFFFFF", fontsize=12, fontweight="bold", pad=10)
    ax1.set_xlabel("Longitude (deg)", color="#A0A5B0")
    ax1.set_ylabel("Latitude (deg)", color="#A0A5B0")
    ax1.tick_params(colors="#A0A5B0")
    ax1.grid(True, linestyle="--", alpha=0.3, color="#505560")
    ax1.legend(facecolor="#20242C", edgecolor="#404550", labelcolor="#FFFFFF", loc="upper right")

    # Panel 2: Elevation profile
    ax2.set_facecolor("#101216")
    ax2.plot(cum_dist_km, eles, color="#FFCC00", linewidth=1.5)
    ax2.fill_between(cum_dist_km, eles, min(eles) - 10, color="#FFCC00", alpha=0.15)
    ax2.set_title("Elevation Profile", color="#FFFFFF", fontsize=11, fontweight="bold", pad=8)
    ax2.set_xlabel("Cumulative Distance (km)", color="#A0A5B0")
    ax2.set_ylabel("Elevation (m)", color="#A0A5B0")
    ax2.tick_params(colors="#A0A5B0")
    ax2.grid(True, linestyle="--", alpha=0.3, color="#505560")

    plt.tight_layout()
    if output_png:
        plt.savefig(output_png, dpi=150, facecolor=fig.get_facecolor(), edgecolor="none")
        print(f"[OK] View plot saved to: {output_png}")
    if show:
        plt.show()
    plt.close()


# ---------------------------------------------------------------------------
# 400x240 Memory-in-Pixel (MIP) Simulator Core (`sim-mip`)
# ---------------------------------------------------------------------------
SCREEN_WIDTH = 400
SCREEN_HEIGHT = 240
FB_SIZE = (SCREEN_WIDTH * SCREEN_HEIGHT) // 8  # 12,000 bytes

# Cohen-Sutherland outcodes
INSIDE = 0  # 0000
LEFT = 1    # 0001
RIGHT = 2   # 0010
BOTTOM = 4  # 0100
TOP = 8     # 1000


def compute_outcode(x: int, y: int, xmin: int, ymin: int, xmax: int, ymax: int) -> int:
    code = INSIDE
    if x < xmin:
        code |= LEFT
    elif x > xmax:
        code |= RIGHT
    if y < ymin:
        code |= TOP
    elif y > ymax:
        code |= BOTTOM
    return code


def cohen_sutherland_clip(x0: int, y0: int, x1: int, y1: int,
                          xmin: int = 0, ymin: int = 0,
                          xmax: int = SCREEN_WIDTH - 1, ymax: int = SCREEN_HEIGHT - 1) -> Optional[Tuple[int, int, int, int]]:
    """Clips a 2D line segment against the screen boundary."""
    code0 = compute_outcode(x0, y0, xmin, ymin, xmax, ymax)
    code1 = compute_outcode(x1, y1, xmin, ymin, xmax, ymax)

    while True:
        if not (code0 | code1):
            # Both endpoints inside
            return x0, y0, x1, y1
        elif code0 & code1:
            # Both endpoints share an outside zone
            return None
        else:
            # Pick endpoint outside
            code_out = code0 if code0 else code1
            if code_out & TOP:
                x = x0 + (x1 - x0) * (ymin - y0) // (y1 - y0) if y1 != y0 else x0
                y = ymin
            elif code_out & BOTTOM:
                x = x0 + (x1 - x0) * (ymax - y0) // (y1 - y0) if y1 != y0 else x0
                y = ymax
            elif code_out & RIGHT:
                y = y0 + (y1 - y0) * (xmax - x0) // (x1 - x0) if x1 != x0 else y0
                x = xmax
            elif code_out & LEFT:
                y = y0 + (y1 - y0) * (xmin - x0) // (x1 - x0) if x1 != x0 else y0
                x = xmin

            if code_out == code0:
                x0, y0 = x, y
                code0 = compute_outcode(x0, y0, xmin, ymin, xmax, ymax)
            else:
                x1, y1 = x, y
                code1 = compute_outcode(x1, y1, xmin, ymin, xmax, ymax)


def draw_bresenham_line(fb: bytearray, x0: int, y0: int, x1: int, y1: int,
                        width: int = SCREEN_WIDTH, height: int = SCREEN_HEIGHT):
    """Draws a 1-pixel line into the 1-bit monochrome framebuffer."""
    clipped = cohen_sutherland_clip(x0, y0, x1, y1, 0, 0, width - 1, height - 1)
    if not clipped:
        return
    x0, y0, x1, y1 = clipped

    dx = abs(x1 - x0)
    dy = abs(y1 - y0)
    sx = 1 if x0 < x1 else -1
    sy = 1 if y0 < y1 else -1
    err = dx - dy

    row_bytes = width // 8

    while True:
        if 0 <= x0 < width and 0 <= y0 < height:
            byte_idx = y0 * row_bytes + (x0 >> 3)
            bit_mask = 0x80 >> (x0 & 7)
            fb[byte_idx] |= bit_mask

        if x0 == x1 and y0 == y1:
            break
        e2 = 2 * err
        if e2 > -dy:
            err -= dy
            x0 += sx
        if e2 < dx:
            err += dx
            y0 += sy


def project_points_to_viewport(points: List[TrackPoint], meta: Dict[str, Any],
                               width: int = SCREEN_WIDTH, height: int = SCREEN_HEIGHT,
                               margin: int = 16) -> List[Tuple[int, int]]:
    """
    Maps WGS84 bounding box to [margin, width - margin] x [margin, height - margin]
    while strictly preserving geographic aspect ratio with cos(mean_lat) correction.
    """
    min_lat = meta["min_lat"]
    max_lat = meta["max_lat"]
    min_lon = meta["min_lon"]
    max_lon = meta["max_lon"]

    mean_lat_rad = math.radians((min_lat + max_lat) / 2.0 / 1e7)
    cos_lat = math.cos(mean_lat_rad)

    span_lat = max(1, max_lat - min_lat)
    span_lon = max(1, max_lon - min_lon)

    # Metric-proportional coordinate span
    geo_w = span_lon * cos_lat
    geo_h = float(span_lat)

    avail_w = width - 2 * margin
    avail_h = height - 2 * margin

    scale = min(avail_w / max(1e-6, geo_w), avail_h / max(1e-6, geo_h))

    # Center bounding box in viewport
    rendered_w = geo_w * scale
    rendered_h = geo_h * scale

    x_offset = margin + int((avail_w - rendered_w) / 2.0)
    y_offset = margin + int((avail_h - rendered_h) / 2.0)

    coords: List[Tuple[int, int]] = []
    for p in points:
        rel_lon = (p.lon_int - min_lon) * cos_lat
        rel_lat = (p.lat_int - min_lat)

        px = int(round(x_offset + rel_lon * scale))
        # Y is inverted (screen 0 is top, max latitude is top)
        py = int(round((height - 1) - (y_offset + rel_lat * scale)))

        coords.append((px, py))

    return coords


def render_mip_framebuffer(points: List[TrackPoint], meta: Dict[str, Any]) -> bytearray:
    """Rasterizes trajectory into a 12,000-byte 1-bit static framebuffer."""
    fb = bytearray(FB_SIZE)
    pixel_coords = project_points_to_viewport(points, meta)

    for i in range(1, len(pixel_coords)):
        x0, y0 = pixel_coords[i - 1]
        x1, y1 = pixel_coords[i]
        draw_bresenham_line(fb, x0, y0, x1, y1)

    return fb


def save_framebuffer_pbm(fb: bytearray, output_pbm: str, width: int = SCREEN_WIDTH, height: int = SCREEN_HEIGHT):
    """Saves raw 1-bit framebuffer as binary Netpbm P4 (PBM) image."""
    header = f"P4\n{width} {height}\n".encode("ascii")
    with open(output_pbm, "wb") as f:
        f.write(header)
        f.write(fb)


def save_framebuffer_png(fb: bytearray, output_png: str, width: int = SCREEN_WIDTH, height: int = SCREEN_HEIGHT):
    """Saves 1-bit framebuffer as standard PNG using Pillow or basic raster."""
    try:
        from PIL import Image
        rgb_img = Image.new("RGB", (width, height), (220, 228, 220))  # MIP paper-like background
        pixels = rgb_img.load()
        row_bytes = width // 8
        for y in range(height):
            for x in range(width):
                byte_idx = y * row_bytes + (x >> 3)
                if fb[byte_idx] & (0x80 >> (x & 7)):
                    pixels[x, y] = (15, 25, 18)  # Sharp dark pixel
        rgb_img.save(output_png)
    except ImportError:
        pbm_path = output_png.rsplit(".", 1)[0] + ".pbm"
        save_framebuffer_pbm(fb, pbm_path, width, height)
        print(f"[NOTE] Pillow not found, wrote PBM fallback: {pbm_path}")


def print_ascii_canvas(fb: bytearray, width: int = SCREEN_WIDTH, height: int = SCREEN_HEIGHT,
                       cols: int = 80, rows: int = 24):
    """Prints a terminal-friendly ASCII preview of the 400x240 screen."""
    row_bytes = width // 8
    x_step = width / cols
    y_step = height / rows

    print("+" + "-" * cols + "+")
    for r in range(rows):
        line = []
        y_start = int(r * y_step)
        y_end = int((r + 1) * y_step)
        for c in range(cols):
            x_start = int(c * x_step)
            x_end = int((c + 1) * x_step)

            # Sample region
            active = False
            for y in range(y_start, min(height, y_end)):
                for x in range(x_start, min(width, x_end)):
                    byte_idx = y * row_bytes + (x >> 3)
                    if fb[byte_idx] & (0x80 >> (x & 7)):
                        active = True
                        break
                if active:
                    break
            line.append("#" if active else " ")
        print("|" + "".join(line) + "|")
    print("+" + "-" * cols + "+")


# ---------------------------------------------------------------------------
# CLI Command Implementations
# ---------------------------------------------------------------------------
def cmd_pack(args):
    """Execute pack command."""
    points = parse_gpx_file(args.input_gpx)
    key = parse_key(args.key)

    cgpx_bytes, meta = compress_and_pack_points(points, key, encrypt=not args.no_encrypt)

    # Write .cgpx binary
    output_cgpx = args.output
    if not output_cgpx:
        base_name = os.path.splitext(args.input_gpx)[0]
        output_cgpx = f"{base_name}.cgpx"

    with open(output_cgpx, "wb") as f:
        f.write(cgpx_bytes)

    # Write C header
    base_stem = os.path.splitext(output_cgpx)[0]
    output_h = f"{base_stem}_data.h"
    c_header_content = generate_c_header(cgpx_bytes, meta, symbol_prefix=args.symbol)
    with open(output_h, "w", encoding="utf-8") as f:
        f.write(c_header_content)

    print("=" * 65)
    print(" CGPX PACKING COMPLETED SUCCESSFULLY")
    print("=" * 65)
    print(f" Source GPX File:       {args.input_gpx}")
    print(f" Points Processed:      {meta['point_count']:,}")
    print(f" Bounding Box Lat:      [{meta['min_lat']/1e7:.6f}, {meta['max_lat']/1e7:.6f}]")
    print(f" Bounding Box Lon:      [{meta['min_lon']/1e7:.6f}, {meta['max_lon']/1e7:.6f}]")
    print(f" Elevation Range:       [{meta['min_ele']} m, {meta['max_ele']} m]")
    print(f" Header Size:           {CGPX_HEADER_SIZE} bytes (fixed packed)")
    print(f" Compressed Payload:    {meta['payload_size']:,} bytes")
    print(f" Total Container Size:  {meta['total_size']:,} bytes")
    print(f" Storage Efficiency:    {meta['bytes_per_point']:.2f} bytes/point (payload)")
    print(f" AES-128-CTR Encrypted: {'YES' if meta['flags'] & CGPX_FLAG_ENCRYPTED else 'NO'}")
    print(f" Payload CRC-32:        0x{meta['crc32']:08X}")
    print("-" * 65)
    print(f" Output Binary:         {output_cgpx}")
    print(f" Output C Header:       {output_h}")
    print("=" * 65)


def cmd_view(args):
    """Execute view command."""
    with open(args.input_cgpx, "rb") as f:
        cgpx_bytes = f.read()

    key = parse_key(args.key)
    points, meta = unpack_and_decompress_cgpx(cgpx_bytes, key)

    print(f"[OK] Decrypted and verified {meta['point_count']} points from {args.input_cgpx}")
    print(f"[OK] CRC-32 Verified: 0x{meta['crc32']:08X}")

    render_view_plot(points, meta, output_png=args.save_plot, show=not args.no_gui)


def cmd_sim_mip(args):
    """Execute sim-mip command."""
    with open(args.input_cgpx, "rb") as f:
        cgpx_bytes = f.read()

    key = parse_key(args.key)
    points, meta = unpack_and_decompress_cgpx(cgpx_bytes, key)

    fb = render_mip_framebuffer(points, meta)

    out_png = args.output_png or "sim_screen.png"
    save_framebuffer_png(fb, out_png)
    print(f"[OK] MIP 400x240 screen exported to: {out_png}")

    out_pbm = os.path.splitext(out_png)[0] + ".pbm"
    save_framebuffer_pbm(fb, out_pbm)
    print(f"[OK] Netpbm binary PBM exported to:  {out_pbm}")

    print("\n--- [400x240 Memory-in-Pixel ASCII Simulation] ---")
    print_ascii_canvas(fb, cols=args.cols, rows=args.rows)


# ---------------------------------------------------------------------------
# CLI Argument Parser
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(
        description="CGPX Lossless Compression, Encryption & Embedded Renderer Toolchain",
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    # pack command
    p_pack = subparsers.add_parser("pack", help="Compress and encrypt a .gpx file to .cgpx and .h")
    p_pack.add_argument("input_gpx", help="Path to input .gpx file")
    p_pack.add_argument("-o", "--output", help="Path to output .cgpx file (default: <input>.cgpx)")
    p_pack.add_argument("-k", "--key", help="16-byte key as hex (32 chars) or text (16 chars)")
    p_pack.add_argument("-s", "--symbol", default="ROUTE", help="C symbol prefix for header macros (default: ROUTE)")
    p_pack.add_argument("--no-encrypt", action="store_true", help="Disable AES-128-CTR encryption")
    p_pack.set_defaults(func=cmd_pack)

    # view command
    p_view = subparsers.add_parser("view", help="View track and elevation profile via Matplotlib")
    p_view.add_argument("input_cgpx", help="Path to input .cgpx file")
    p_view.add_argument("-k", "--key", help="16-byte key as hex (32 chars) or text (16 chars)")
    p_view.add_argument("--save-plot", help="Save plot image to specified path (e.g. plot.png)")
    p_view.add_argument("--no-gui", action="store_true", help="Do not open GUI window (use with --save-plot)")
    p_view.set_defaults(func=cmd_view)

    # sim-mip command
    p_sim = subparsers.add_parser("sim-mip", help="Simulate 400x240 1-bit Memory-in-Pixel display")
    p_sim.add_argument("input_cgpx", help="Path to input .cgpx file")
    p_sim.add_argument("-k", "--key", help="16-byte key as hex (32 chars) or text (16 chars)")
    p_sim.add_argument("-o", "--output-png", default="sim_screen.png", help="Output PNG path (default: sim_screen.png)")
    p_sim.add_argument("--cols", type=int, default=80, help="Terminal ASCII width (default: 80)")
    p_sim.add_argument("--rows", type=int, default=24, help="Terminal ASCII height (default: 24)")
    p_sim.set_defaults(func=cmd_sim_mip)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
