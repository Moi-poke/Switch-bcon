#!/usr/bin/env python3
"""Self-check for T_PLAYER_INFO (0x23) decoder in poc_send.py.

Run: python src/poc_dualcore/test_poc_send_023.py
(pytest is not required; stdlib unittest only)

spec/protocol_v3.md / src/proto/protocol.h: T_PLAYER_INFO = 0x23, Pico->PC,
LEN=2, payload = [lamp][flags]; flags bit0 = IMU, bit1 = vibration.
Frame shape: [SYNC=0xAB][TYPE][LEN][PAYLOAD][SEQ][CRC8/SMBUS over TYPE..SEQ].
"""

import contextlib
import io
import os
import re
import sys
import time
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


class FakePongSer:
    """Fake serial emulating the pyserial blocking-read floor (no HW/COM3).

    PONG bytes (6B: SYNC+TYPE+LEN+PAY+SEQ+CRC) are scripted and instantly
    available in the buffer on write(), but read(n) emulates
    ``serial.Serial(port, baud, timeout=1)`` (poc_send.py open ~l.476):
    a short frame never fills the requested 64 B (ping_test ~l.197-198),
    so the real driver blocks the full ~1.0 s window before returning.
    This reproduces the ~1000 ms RTT floor via the REAL ping_test path.
    """

    READ_TIMEOUT = 1.0  # mirrors serial.Serial(..., timeout=1)

    def __init__(self) -> None:
        self._buf = bytearray()
        self.writes = 0
        self.timeout = 1.0  # mirrors serial.Serial(..., timeout=1) default

    @property
    def in_waiting(self) -> int:
        return len(self._buf)

    def reset_input_buffer(self) -> None:
        self._buf.clear()

    def write(self, data: bytes) -> int:
        self.writes += 1
        seq = data[3] if len(data) >= 4 else 0
        body = bytes((m.T_PONG, 1, seq & 0xFF, seq & 0xFF))
        self._buf += bytes((m.SYNC,)) + body + bytes((m.crc8_smbus(body),))
        return len(data)

    def read(self, n: int) -> bytes:
        if not self._buf:
            time.sleep(self.timeout)
            return b""
        # blocking-read floor: short 6B frame, full timeout window first
        time.sleep(self.timeout)
        out, self._buf = bytes(self._buf[:n]), self._buf[n:]
        return out

    def readline(self) -> bytes:
        return self.read(len(self._buf) or 1)


def _run_ping(count: int, per_ping: float = 2.0) -> tuple:
    """Run REAL m.ping_test against FakePongSer; return (rc, rtts, avg)."""
    ser = FakePongSer()
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        rc = m.ping_test(ser, count, timeout=per_ping)
    text = out.getvalue()
    rtts = [float(v) for v in re.findall(r"rtt=([\d.]+)ms", text)]
    mavg = re.search(r"avg=([\d.]+)", text)
    return rc, rtts, (float(mavg.group(1)) if mavg else None)


class TestPingRtt(unittest.TestCase):
    # ---- RED: fake-serial instant-PONG must arrive in <100ms ----

    def test_single_pong_rtt_under_100ms(self):
        rc, rtts, _ = _run_ping(1)
        self.assertEqual(rc, 0)
        self.assertEqual(len(rtts), 1)
        for r in rtts:
            self.assertLess(r, 100.0,
                            f"RTT floor {r:.2f}ms >= 100ms (read(64)/timeout=1)")

    def test_three_pongs_all_rtt_under_100ms(self):
        rc, rtts, _ = _run_ping(3)
        self.assertEqual(rc, 0)
        self.assertEqual(len(rtts), 3)
        for r in rtts:
            self.assertLess(r, 100.0,
                            f"RTT floor {r:.2f}ms >= 100ms (read(64)/timeout=1)")

    def test_ping_summary_avg_under_100ms(self):
        rc, rtts, avg = _run_ping(2)
        self.assertEqual(rc, 0)
        self.assertIsNotNone(avg)
        assert avg is not None
        self.assertLess(avg, 100.0,
                        f"avg RTT floor {avg:.2f}ms >= 100ms (read(64)/timeout=1)")


if __name__ == "__main__":
    unittest.main(verbosity=2)
