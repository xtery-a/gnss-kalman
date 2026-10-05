#ifndef SKYMASK_LUT_H
#define SKYMASK_LUT_H

#include <stdint.h>

/**
 * @brief Topographic Skymask 64-Bin LUT
 * Generated for: LAT: 40.069400, LON: 29.221700, ALT_AGL: 1.8m
 * Ground Elevation (DEM): 395.31m MSL, Search Radius: 5000.0m
 * Source DEM: sample_dem.tif
 * Quantization: uint8_t [0-255] mapped to [0.0 - 90.0] degrees
 * Resolution: 5.625 deg/bin (Azimuth), ~0.353 deg/LSB (Elevation)
 */

#define SKYMASK_NUM_BINS         64
#define SKYMASK_AZIMUTH_STEP_DEG 5.625f

static const uint8_t SKYMASK_LUT[SKYMASK_NUM_BINS] = {
    /* 0..7   (N to NE)  */   0,   0,   0,   0,   0,   0,   0,   0,
    /* 8..15  (NE to E)  */   2,   6,  10,  14,  18,  21,  25,  28,
    /* 16..23 (E to SE)  */  31,  34,  37,  42,  49,  55,  61,  65,
    /* 24..31 (SE to S)  */  67,  68,  67,  64,  60,  54,  47,  40,
    /* 32..39 (S to SW)  */  34,  29,  25,  20,  16,  12,   7,   3,
    /* 40..47 (SW to W)  */   0,   0,   0,   0,   0,   0,   0,   0,
    /* 48..55 (W to NW)  */   0,   0,   0,   0,   2,   4,   5,   5,
    /* 56..63 (NW to N)  */   5,   4,   2,   0,   0,   0,   0,   0
};

#endif // SKYMASK_LUT_H
