"""procon_probe の検証 (実機不要。hidをモックする)。"""

import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))

import procon_probe as P


class FakeDev:
    """hid.deviceの代役。0x81/0x21/0x30を返す。"""

    def __init__(self) -> None:
        self.written: list[bytes] = []
        self.timer = 0
        self.spi = {0x6020: bytes(range(0x1D))}

    def open(self, vid: int, pid: int) -> None:
        self.vid, self.pid = vid, pid

    def set_nonblocking(self, flag: bool) -> None:
        pass

    def close(self) -> None:
        pass

    def write(self, data: bytes) -> None:
        self.written.append(bytes(data))

    def read(self, size: int, timeout_ms: int = 100):
        if not self.written:
            return []
        last = self.written[-1]
        if last[0] == 0x80:
            # 81 <sub> で返す。
            return [0x81, last[1]] + [0] * 62
        if last[0] == 0x01:
            # [ID][振動8B][sub][args...] のためsubはindex 9。
            sub = last[9]
            rep = [0x21] + [0] * 63
            rep[13], rep[14] = 0x80, sub
            if sub == 0x10:
                addr = last[10] | (last[11] << 8)
                want = last[14]
                rep[13], rep[14] = 0x90, 0x10
                rep[15:20] = [last[10], last[11], 0, 0, want]
                data = self.spi.get(addr, bytes([0xFF]) * want)
                rep[20:20 + want] = list(data[:want])
            self.written.clear()
            return rep[:size]
        return []


class FakeHid:
    def __init__(self, dev: FakeDev) -> None:
        self._dev = dev

    def device(self) -> FakeDev:
        return self._dev


def _probe() -> tuple[P.Probe, FakeDev]:
    dev = FakeDev()
    pr = P.Probe.__new__(P.Probe)
    pr._hid = FakeHid(dev)  # type: ignore[attr-defined]
    pr.dev = dev  # type: ignore[assignment]
    pr.vid, pr.pid = 0x057E, 0x2009
    return pr, dev


def test_handshake_order() -> None:
    pr, dev = _probe()
    out = pr.handshake_80()
    subs = [bytes(w)[1] for w in dev.written]
    assert subs == [0x05, 0x01, 0x02, 0x04]
    assert out["80_04"] is not None


def test_subcmd_roundtrip() -> None:
    pr, _ = _probe()
    r = pr.subcmd(0x40, b"\x01")
    assert r is not None and r[0] == 0x21 and r[14] == 0x40


def test_spi_read() -> None:
    pr, _ = _probe()
    rep, data = pr.spi_read(0x6020, 0x1D)
    assert rep is not None and rep[13] == 0x90
    assert data == bytes(range(0x1D))


def test_collect_stats_shape() -> None:
    pr, dev = _probe()
    # 0x30を流す代役: _read_reportsを差し替える。
    reports = []
    for i in range(10):
        r = bytearray(64)
        r[0], r[1] = 0x30, (i * 5) & 0xFF
        reports.append(bytes(r))
    pr._read_reports = lambda secs, want_id=None: reports  # type: ignore[method-assign]
    st = P.collect_030_stats(pr, n=10, timeout=1.0)
    assert st["count"] == 10
    assert st["timer_diff_hist"] == {"5": 9}
    assert st["report_lens"] == [64]


def test_collect_imu_csv() -> None:
    pr, _ = _probe()
    r = bytearray(64)
    r[0] = 0x30
    # slot0: ax=1 (01 00)。
    r[13], r[14] = 0x01, 0x00
    # slot0: gz=-2 (FE FF)。
    r[23], r[24] = 0xFE, 0xFF
    pr._read_reports = lambda secs, want_id=None: [bytes(r)]  # type: ignore[method-assign]
    rows = P.collect_imu_csv(pr, n=1, timeout=1.0)
    assert len(rows) == 1
    assert rows[0][0] == 1
    assert rows[0][5] == -2
    assert len(rows[0]) == 18


def test_diff_runs(tmp_path) -> None:
    a = {"handshake80": {"80_04": "aa"}, "sub40": "bb", "report030": {"count": 1},
         "spi": {"00006020": {"data": "00"}}}
    b = {"handshake80": {"80_04": "aa"}, "sub40": "cc", "report030": {"count": 1},
         "spi": {"00006020": {"data": "ff"}}}
    pa, pb = tmp_path / "a.json", tmp_path / "b.json"
    pa.write_text(json.dumps(a), encoding="utf-8")
    pb.write_text(json.dumps(b), encoding="utf-8")
    import argparse

    ns = argparse.Namespace(a=str(pa), b=str(pb))
    assert P.cmd_diff(ns) == 0
