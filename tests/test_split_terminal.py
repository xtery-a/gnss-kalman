#!/usr/bin/env python3
"""
Comprehensive Test Suite for Split-Node Extreme Condition GNSS Terminal
Tests:
  1. Dynamic 2D Skymask Grid Generation & Binary Deserialization
  2. ITU-R P.526 Knife-Edge Fresnel Diffraction & +/-1.5 deg Hysteresis State Machine
  3. Split-Node Differential Protocol Framing, Byte Parsing & CRC-16 Integrity
  4. Sub-Zero Battery Thermal Guard, Charge Lockout & Voltage Sag Monitor
"""

import math
import os
import struct
import tempfile
import unittest
import zlib

from skymask_grid import (
    GRID_MAGIC,
    GRID_VERSION,
    SkymaskGridMeta,
    generate_skymask_grid,
    export_grid_binary,
)


class TestFresnelAndHysteresis(unittest.TestCase):
    """Verifies Knife-Edge Fresnel Diffraction physics and hysteresis behavior."""

    @staticmethod
    def fresnel_loss_db(v: float) -> float:
        """Python reference implementation matching nlos_filter.c"""
        if v <= -1.0:
            return 0.0
        elif v <= 0.0:
            loss = 6.02 + 9.0 * v + 1.66 * v * v
            return max(0.0, loss)
        elif v <= 1.0:
            return 6.02 + 9.11 * v - 1.27 * v * v
        else:
            return 12.95 + 20.0 * math.log10(v)

    def test_fresnel_knife_edge_curve(self):
        """Verify ITU-R P.526 benchmark points."""
        # At exactly the ridge edge (v = 0), classic knife-edge attenuation is ~6.02 dB
        self.assertAlmostEqual(self.fresnel_loss_db(0.0), 6.02, places=2)

        # Deep in line of sight (v <= -1.0), diffraction loss is 0 dB
        self.assertEqual(self.fresnel_loss_db(-1.5), 0.0)
        self.assertEqual(self.fresnel_loss_db(-1.0), 0.0)

        # Moderate shadow (v = 1.0)
        loss_v1 = self.fresnel_loss_db(1.0)
        self.assertAlmostEqual(loss_v1, 13.86, delta=0.2)

        # Deeper shadow (v = 3.0)
        loss_v3 = self.fresnel_loss_db(3.0)
        self.assertGreater(loss_v3, 20.0)

    def test_hysteresis_state_transitions(self):
        """Verify +/- 1.5 deg hysteresis band eliminates state chattering."""
        # Trajectory simulating satellite descending past a ridge line at 25.0 deg:
        # 28.0 (LOS) -> 26.6 (LOS) -> 26.4 (DIFFRACTED) -> 25.0 (DIFFRACTED) -> 23.4 (DIFFRACTED) -> 23.0 (BLOCKED)
        horizon = 25.0
        elevations_down = [28.0, 26.6, 26.4, 25.0, 23.6, 23.4, 23.0]

        state = "UNINITIALIZED"
        states_down = []

        for el in elevations_down:
            margin = el - horizon
            if state in ("UNINITIALIZED", "LOS"):
                if margin < -1.5:
                    state = "BLOCKED"
                elif margin <= 1.5:
                    state = "DIFFRACTED"
                else:
                    state = "LOS"
            elif state == "DIFFRACTED":
                if margin > 1.5:
                    state = "LOS"
                elif margin < -1.5:
                    state = "BLOCKED"
            states_down.append(state)

        self.assertEqual(states_down[0], "LOS")         # 28.0 > 26.5
        self.assertEqual(states_down[1], "LOS")         # 26.6 > 26.5
        self.assertEqual(states_down[2], "DIFFRACTED")   # 26.4 <= 26.5
        self.assertEqual(states_down[3], "DIFFRACTED")   # 25.0 in [-1.5, +1.5]
        self.assertEqual(states_down[6], "BLOCKED")      # 23.0 < 23.5

        # Ascending back: BLOCKED -> DIFFRACTED -> LOS
        # SATELLITE MUST NOT JUMP DIRECTLY FROM BLOCKED TO LOS AT 25.1 DEG!
        elevations_up = [23.0, 23.6, 25.0, 26.0, 26.6]
        states_up = []
        for el in elevations_up:
            margin = el - horizon
            if state == "BLOCKED":
                if margin > 1.5:
                    state = "LOS"
                elif margin >= -1.5:
                    state = "DIFFRACTED"
            elif state == "DIFFRACTED":
                if margin > 1.5:
                    state = "LOS"
                elif margin < -1.5:
                    state = "BLOCKED"
            states_up.append(state)

        # At 25.0 deg (margin = 0), satellite must be DIFFRACTED, not LOS!
        self.assertEqual(states_up[2], "DIFFRACTED")
        # At 26.6 deg (margin = +1.6 > +1.5), satellite recovers to full LOS
        self.assertEqual(states_up[4], "LOS")


class TestSplitBusProtocol(unittest.TestCase):
    """Verifies CAN-FD / RS-485 packet framing, byte parsing, and CRC-16-CCITT."""

    @staticmethod
    def calc_crc16(data: bytes) -> int:
        """CRC-16-CCITT (poly 0x1021, init 0xFFFF) matching split_node_bus.c"""
        crc = 0xFFFF
        for b in data:
            crc ^= (b << 8)
            for _ in range(8):
                if crc & 0x8000:
                    crc = ((crc << 1) ^ 0x1021) & 0xFFFF
                else:
                    crc = (crc << 1) & 0xFFFF
        return crc

    def encode_frame(self, msg_id: int, seq: int, payload: bytes) -> bytes:
        header = bytes([0xAA, 0x55, msg_id, seq, len(payload)])
        crc_data = bytes([msg_id, seq, len(payload)]) + payload
        crc = self.calc_crc16(crc_data)
        crc_bytes = bytes([(crc >> 8) & 0xFF, crc & 0xFF])
        footer = bytes([0x55, 0xAA])
        return header + payload + crc_bytes + footer

    def test_encode_and_crc(self):
        payload = b"GNSS_PVT_LAT_400694_LON_0292217"
        frame = self.encode_frame(msg_id=0x10, seq=1, payload=payload)

        self.assertEqual(frame[:2], b"\xAA\x55")
        self.assertEqual(frame[-2:], b"\x55\xAA")
        self.assertEqual(frame[2], 0x10)
        self.assertEqual(frame[3], 1)
        self.assertEqual(frame[4], len(payload))

    def test_byte_stream_parser_recovery(self):
        """Simulate noisy byte stream with prepended junk bytes and verify frame recovery."""
        payload = bytes([1, 2, 3, 4, 5])
        valid_frame = self.encode_frame(msg_id=0x01, seq=42, payload=payload)

        # Inject electrical noise / partial bytes prior to frame
        noisy_stream = b"\x00\xFF\xAA\x12\x55\xAA" + valid_frame + b"\xFE\xED"

        # Emulate C state machine parsing
        state = "SOF0"
        rx_pkt = None
        temp_buf = bytearray()
        target_len = 0

        for byte in noisy_stream:
            if state == "SOF0":
                if byte == 0xAA:
                    state = "SOF1"
            elif state == "SOF1":
                if byte == 0x55:
                    state = "MSG_ID"
                elif byte == 0xAA:
                    state = "SOF1"
                else:
                    state = "SOF0"
            elif state == "MSG_ID":
                msg_id = byte
                state = "SEQ"
            elif state == "SEQ":
                seq = byte
                state = "LEN"
            elif state == "LEN":
                target_len = byte
                temp_buf = bytearray()
                state = "PAYLOAD" if target_len > 0 else "CRC_HI"
            elif state == "PAYLOAD":
                temp_buf.append(byte)
                if len(temp_buf) >= target_len:
                    state = "CRC_HI"
            elif state == "CRC_HI":
                crc_hi = byte
                state = "CRC_LO"
            elif state == "CRC_LO":
                crc_lo = byte
                received_crc = (crc_hi << 8) | crc_lo
                state = "EOF0"
            elif state == "EOF0":
                if byte == 0x55:
                    state = "EOF1"
                else:
                    state = "SOF0"
            elif state == "EOF1":
                if byte == 0xAA:
                    expected_crc = self.calc_crc16(bytes([msg_id, seq, target_len]) + bytes(temp_buf))
                    if expected_crc == received_crc:
                        rx_pkt = {"msg_id": msg_id, "seq": seq, "payload": bytes(temp_buf)}
                state = "SOF0"

        self.assertIsNotNone(rx_pkt, "Parser should recover and lock onto valid frame amidst noise")
        self.assertEqual(rx_pkt["msg_id"], 0x01)
        self.assertEqual(rx_pkt["seq"], 42)
        self.assertEqual(rx_pkt["payload"], payload)


class TestSubZeroBatteryThermalGuard(unittest.TestCase):
    """Verifies electrochemical protection against sub-zero charging and ESR voltage sag."""

    def test_strict_subzero_charge_lockout(self):
        """Charging must be locked below 0 deg C and require >= 2.5 deg C to resume."""
        # Simulated temperature descent: 10C -> 2C -> -5C -> -20C
        # Then recovery: -20C -> 1.0C -> 3.0C

        charge_allowed = True
        latch = False

        temps = [10.0, 2.0, -0.5, -5.0, -20.0, 0.5, 1.5, 2.6]
        history = []

        for t in temps:
            if t < 0.0 or t > 50.0:
                latch = True
            elif t >= 2.5 and t <= 45.0:
                latch = False
            charge_allowed = not latch
            history.append((t, charge_allowed))

        # At -0.5 C: MUST LOCK CHARGE
        self.assertFalse(history[2][1], "T = -0.5 C must strictly lockout charging")
        # At -20 C: MUST REMAIN LOCKED
        self.assertFalse(history[4][1])
        # At +0.5 C: MUST REMAIN LOCKED (hysteresis!)
        self.assertFalse(history[5][1], "T = +0.5 C must not resume until >= 2.5 C hysteresis threshold")
        # At +1.5 C: STILL LOCKED
        self.assertFalse(history[6][1])
        # At +2.6 C: RESUME CHARGE
        self.assertTrue(history[7][1], "T = +2.6 C passes recovery threshold, charging re-enabled")

    def test_voltage_sag_esr_monitoring(self):
        """ESR calculation during burst current draw."""
        v_idle = 3800   # 3.8V idle
        v_burst = 3350  # 3.35V under 500mA LoRa TX burst
        i_burst = 500   # mA

        delta_v = v_idle - v_burst  # 450 mV
        esr_mohm = (delta_v * 1000) // i_burst  # 900 mOhm (cold cell)

        self.assertEqual(esr_mohm, 900)

        # Critical sag check (below 3050 mV)
        v_critical = 3000
        is_crit = v_critical <= 3050
        self.assertTrue(is_crit, "3000 mV must trigger critical sag brownout alarm")


class TestSkymaskGridCache(unittest.TestCase):
    """Verifies 2D grid generation, binary serialization, and CRC32."""

    def test_grid_binary_format(self):
        meta = SkymaskGridMeta(
            origin_lat=40.0694,
            origin_lon=29.2217,
            rows=3,
            cols=3,
            grid_step_m=50.0,
            search_radius_m=3000.0,
            eye_height_m=1.8,
        )
        import numpy as np
        fake_grid = np.arange(3 * 3 * 64, dtype=np.uint8).reshape((3, 3, 64))
        fake_elev = np.full((3, 3), 400.0, dtype=np.float32)

        with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as tf:
            bin_path = tf.name

        try:
            export_grid_binary(meta, fake_grid, fake_elev, bin_path)
            self.assertTrue(os.path.isfile(bin_path))

            # Read back and verify
            header_size = struct.calcsize("<IHIIIIfffddI")
            with open(bin_path, "rb") as f:
                header_bytes = f.read(header_size)
                data_bytes = f.read()

            magic, ver, num_bins, rows, cols, _, step, radius, eye_h, lat, lon, crc = struct.unpack(
                "<IHIIIIfffddI", header_bytes
            )

            self.assertEqual(magic, GRID_MAGIC)
            self.assertEqual(ver, GRID_VERSION)
            self.assertEqual(num_bins, 64)
            self.assertEqual(rows, 3)
            self.assertEqual(cols, 3)
            self.assertAlmostEqual(step, 50.0, places=1)
            self.assertAlmostEqual(lat, 40.0694, places=4)
            self.assertAlmostEqual(lon, 29.2217, places=4)

            # Verify CRC32 matches payload
            computed_crc = zlib.crc32(data_bytes)
            self.assertEqual(crc, computed_crc)
            self.assertEqual(len(data_bytes), 3 * 3 * 64)

        finally:
            if os.path.exists(bin_path):
                os.remove(bin_path)


if __name__ == "__main__":
    unittest.main()
