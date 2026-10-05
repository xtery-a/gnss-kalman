#!/usr/bin/env python3
"""
tests/test_gnss_tracking_filter.py
Ponytail Self-Check: Validates 2D Adaptive Kalman Filter, ZVU, and Multipath Outlier Gate
under simulated window-edge multi-GNSS reflection conditions.
"""

import math
import random

class GnssTrackingFilter:
    def __init__(self):
        self.enabled = True
        self.is_initialized = False
        self.origin_lat = None
        self.origin_lon = None
        self.x = 0.0
        self.y = 0.0
        self.vx = 0.0
        self.vy = 0.0
        self.p = [
            [10.0, 0.0, 0.0, 0.0],
            [0.0, 10.0, 0.0, 0.0],
            [0.0, 0.0, 4.0, 0.0],
            [0.0, 0.0, 0.0, 4.0]
        ]
        self.outliers_rejected = 0
        self.stationary_count = 0

    def update(self, raw_lat, raw_lon, speed_kmh, hdop, dt=1.0):
        if not self.is_initialized:
            self.origin_lat = raw_lat
            self.origin_lon = raw_lon
            self.x = 0.0
            self.y = 0.0
            self.vx = 0.0
            self.vy = 0.0
            self.is_initialized = True
            return raw_lat, raw_lon, False

        phi_rad = math.radians(self.origin_lat)
        r_lat = 111319.9
        r_lon = 111319.9 * math.cos(phi_rad)
        zx = (raw_lon - self.origin_lon) * r_lon
        zy = (raw_lat - self.origin_lat) * r_lat

        # Prediction
        x_pred = self.x + self.vx * dt
        y_pred = self.y + self.vy * dt

        is_stationary = (speed_kmh is not None and speed_kmh < 0.8)
        q_pos = 0.01 * dt if is_stationary else 0.8 * dt * dt
        q_vel = 0.01 * dt if is_stationary else 1.2 * dt

        self.p[0][0] += q_pos + dt * (self.p[2][0] + self.p[0][2] + dt * self.p[2][2])
        self.p[1][1] += q_pos + dt * (self.p[3][1] + self.p[1][3] + dt * self.p[3][3])
        self.p[2][2] += q_vel
        self.p[3][3] += q_vel

        # Innovation
        dx = zx - x_pred
        dy = zy - y_pred
        innov_dist = math.hypot(dx, dy)

        # Outlier Gate
        is_outlier = False
        if innov_dist > 14.0 and (speed_kmh is None or speed_kmh < 8.0):
            is_outlier = True
            self.outliers_rejected += 1
            filtered_lat = self.origin_lat + (self.y / r_lat)
            filtered_lon = self.origin_lon + (self.x / r_lon)
            return filtered_lat, filtered_lon, True

        # ZVU
        if is_stationary:
            self.stationary_count += 1
            self.vx *= 0.1
            self.vy *= 0.1
            if innov_dist < 2.5:
                filtered_lat = self.origin_lat + (self.y / r_lat)
                filtered_lon = self.origin_lon + (self.x / r_lon)
                return filtered_lat, filtered_lon, False

        # Kalman Update
        safe_hdop = hdop if (hdop and hdop > 0) else 1.5
        sigma = max(1.5, safe_hdop * 2.2)
        r = sigma * sigma

        kx = self.p[0][0] / (self.p[0][0] + r)
        ky = self.p[1][1] / (self.p[1][1] + r)
        kvx = self.p[2][0] / (self.p[0][0] + r)
        kvy = self.p[3][1] / (self.p[1][1] + r)

        self.x = x_pred + kx * dx
        self.y = y_pred + ky * dy
        self.vx = self.vx + kvx * dx
        self.vy = self.vy + kvy * dy

        self.p[0][0] = (1.0 - kx) * self.p[0][0]
        self.p[1][1] = (1.0 - ky) * self.p[1][1]
        self.p[2][2] = (1.0 - kvx) * self.p[2][2]
        self.p[3][3] = (1.0 - kvy) * self.p[3][3]

        filtered_lat = self.origin_lat + (self.y / r_lat)
        filtered_lon = self.origin_lon + (self.x / r_lon)
        return filtered_lat, filtered_lon, False


def run_filter_benchmark():
    random.seed(42)
    truth_lat = 41.450042
    truth_lon = 31.758731

    r_lat = 111319.9
    r_lon = 111319.9 * math.cos(math.radians(truth_lat))

    kf = GnssTrackingFilter()

    raw_errors = []
    filtered_errors = []
    num_samples = 400
    spikes_injected = 0

    for i in range(num_samples):
        # Base gaussian noise (sigma = 3m)
        err_x = random.gauss(0, 3.0)
        err_y = random.gauss(0, 3.0)

        # Inject 10 multipath spikes (> 20m)
        if i in (50, 90, 140, 180, 220, 260, 310, 340, 370, 390):
            err_x += 24.0
            err_y += 18.0
            spikes_injected += 1

        raw_lat = truth_lat + (err_y / r_lat)
        raw_lon = truth_lon + (err_x / r_lon)

        raw_dist = math.hypot(err_x, err_y)
        raw_errors.append(raw_dist)

        f_lat, f_lon, outlier = kf.update(raw_lat, raw_lon, speed_kmh=0.1, hdop=1.8, dt=1.0)
        f_dx = (f_lon - truth_lon) * r_lon
        f_dy = (f_lat - truth_lat) * r_lat
        f_dist = math.hypot(f_dx, f_dy)
        filtered_errors.append(f_dist)

    raw_errors.sort()
    filtered_errors.sort()

    raw_cep = raw_errors[int(num_samples * 0.50)]
    raw_2drms = raw_errors[int(num_samples * 0.95)]
    raw_max = max(raw_errors)

    f_cep = filtered_errors[int(num_samples * 0.50)]
    f_2drms = filtered_errors[int(num_samples * 0.95)]
    f_max = max(filtered_errors)

    print("=" * 65)
    print(" GNSS TRACKING FILTER & MULTIPATH SUPPRESSION BENCHMARK")
    print("=" * 65)
    print(f"Toplam Ornek Sayisi : {num_samples}")
    print(f"Enjekte Multipath  : {spikes_injected} adet (> 25m sicrama)")
    print(f"Yakalanan Spike    : {kf.outliers_rejected} adet")
    print("-" * 65)
    print(f"Metrik          | Ham (Raw) NMEA    | Kalman + ZVU Filtreli")
    print("-" * 65)
    print(f"CEP (%50)       | {raw_cep:6.2f} metre      | {f_cep:6.2f} metre")
    print(f"2DRMS (%95)     | {raw_2drms:6.2f} metre      | {f_2drms:6.2f} metre")
    print(f"Maksimum Sapma  | {raw_max:6.2f} metre      | {f_max:6.2f} metre")
    print("=" * 65)

    assert raw_max > 25.0, "Ham veride multipath sicramasi olmali"
    assert f_max < 10.0, f"Filtrelenmis maksimum sapma 10m altinda olmali, olculen: {f_max:.2f}m"
    assert f_2drms < 5.0, f"Filtrelenmis 2DRMS 5.0m altinda olmali, olculen: {f_2drms:.2f}m"
    assert kf.outliers_rejected == spikes_injected, "Tum multipath sicramalari yakalanmali"
    print(">> [TEST PASSED]: Tum filtre ve stabilite kontrolleri %100 basarili!")

if __name__ == "__main__":
    run_filter_benchmark()
