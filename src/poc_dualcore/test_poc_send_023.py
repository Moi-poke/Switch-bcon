#!/usr/bin/env python3
"""Self-check for T_PLAYER_INFO (0x23) decoder in poc_send.py.

Run: python src/poc_dualcore/test_poc_send_023.py
(pytest is not required; stdlib unittest only)

spec/protocol_v3.md / src/proto/protocol.h: T_PLAYER_INFO = 0x23, Pico->PC,
LEN=2, payload = [lamp][flags]; flags bit0 = IMU, bit1 = vibration.
Frame shape: [SYNC=0xAB][TYPE][LEN][PAYLOAD][SEQ][CRC8/SMBUS over TYPE..SEQ].
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import poc_send as m  # noqa: E402


def make_frame(typ: int, payload: bytes, seq: int = 0) -> bytes:
    body = bytes((typ, len(payload))) + payload + bytes((seq & 0xFF,))
    return bytes((m.SYNC,)) + body + bytes((m.crc8_smbus(body),))


class FakeSer:
    """Minimal serial stub: delivers a fixed byte string to scan_frames."""

    def __init__(self, data: bytes):
        self._d = bytes(data)

    def read(self, n: int) -> bytes:
        d, self._d = self._d[:n], self._d[n:]
        return d


class TestPlayerInfo(unittest.TestCase):
    # ---- group [23]: T_PLAYER_INFO decoder ----

    def test_type_id_is_0x23(self):
        self.assertEqual(m.T_PLAYER_INFO, 0x23)

    def test_decode_lamp_and_flags(self):
        # lamp=0x03 (P1), flags=0x01 (IMU on, vibration off)
        frame = make_frame(m.T_PLAYER_INFO, bytes((0x03, 0x01)))
        self.assertEqual(m.decode_player_info(frame), (0x03, 0x01))

    def test_decode_arbitrary_values_and_seq(self):
        # flags=0x03 (IMU + vibration), nonzero SEQ must not affect payload
        frame = make_frame(m.T_PLAYER_INFO, bytes((0x05, 0x03)), seq=0x7E)
        self.assertEqual(m.decode_player_info(frame), (0x05, 0x03))

    def test_truncated_frame_raises_valueerror(self):
        frame = make_frame(m.T_PLAYER_INFO, bytes((0x03, 0x01)))[:4]
        with self.assertRaises(ValueError):
            m.decode_player_info(frame)

    def test_garbage_crc_raises_valueerror(self):
        frame = bytearray(make_frame(m.T_PLAYER_INFO, bytes((0x03, 0x01))))
        frame[-1] ^= 0xFF
        with self.assertRaises(ValueError):
            m.decode_player_info(bytes(frame))

    def test_wrong_type_raises_valueerror(self):
        frame = make_frame(m.T_STATE, bytes((0x03, 0x01)))
        with self.assertRaises(ValueError):
            m.decode_player_info(frame)

    def test_wrong_len_raises_valueerror(self):
        frame = make_frame(m.T_PLAYER_INFO, bytes((0x03,)))
        with self.assertRaises(ValueError):
            m.decode_player_info(frame)

    # ---- passthrough: scan_frames must not filter by TYPE ----

    def test_scan_frames_unknown_type_passthrough_unchanged(self):
        frames = (make_frame(m.T_PLAYER_INFO, bytes((1, 2)), seq=1) +
                  make_frame(0x99, bytes((9,)), seq=2))
        got = list(m.scan_frames(FakeSer(frames), 0.05))
        self.assertEqual(got, [(m.T_PLAYER_INFO, 1, bytes((1, 2))),
                               (0x99, 2, bytes((9,)))])


if __name__ == "__main__":
    unittest.main(verbosity=2)
