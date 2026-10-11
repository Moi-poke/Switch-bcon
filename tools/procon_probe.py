"""procon_probe.py - 純正ProCon / Pico のUSB応答比較プローブ (§2)。

使い方:
  python tools/procon_probe.py --vid 0x057E --pid 0x2009 --out log/probe-procon.json [--yaw]
  python tools/procon_probe.py --diff log/probe-procon.json log/probe-pico.json

依存は hidapi (Cython版、import hid) のみ。実行前にSteamを終了すること
(SteamがProConを掴むと開けない)。

参照 (dekuNukem/Nintendo_Switch_Reverse_Engineering):
- USB-HID-Notes.md: "80 01 ... 80 02 ... 80 03 ... 80 04 / Forces the
  Joy-Con or Pro Controller to only talk over USB HID without any timeouts."
  ハンドシェイク順 05→01→02→04 は Switch接続時の実測 (UHB h80=05010204)。
- bluetooth_hid_subcommands_notes.md:
  - 0x10 "Little-endian int32 address, int8 size, max size is x1D.
    Replies with x9010 ack and echoes the request info"
  - 0x40 "One argument of x00 Disable or x01 Enable."
  - 0x41 "4 uint8_t"、既定 "03 00 01 01" 相当
    (gyro ±2000dps / accel ±8G / gyro 208Hz / accel 100Hz)。
    本スクリプトは Switch2実測値 "03 00 01 01" を送る。
  - 0x03 "x30 Standard full mode. Pushes current state @60Hz"。
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import sys
import time

# Nintendo ProCon USB (dekuNukem USB-HID-Notes.md 前提)。
NINTENDO_VID = 0x057E
PROCON_PID = 0x2009

# 0x41引数: Switch2実測値 (FWログ IMUD sub41 args=03 00 01 01)。
IMU_SENS_DEFAULT = bytes((0x03, 0x00, 0x01, 0x01))


def _hex(b: bytes) -> str:
    return b.hex()


class Probe:
    def __init__(self, vid: int, pid: int) -> None:
        import hid  # Cython hidapi

        self._hid = hid
        self.dev = hid.device()
        self.dev.open(vid, pid)
        self.dev.set_nonblocking(True)
        self.vid = vid
        self.pid = pid

    def close(self) -> None:
        try:
            self.dev.close()
        except Exception:
            pass

    def _write(self, rid: int, payload: bytes, total: int = 64) -> None:
        # hidapi: 先頭にreport ID。不足は0埋め。
        data = bytes((rid,)) + bytes(payload)
        data = data + bytes(max(0, total - len(data)))
        self.dev.write(data)

    def _read_reports(self, secs: float, want_id: int | None = None) -> list[bytes]:
        """secs秒だけ入力レポートを集める。want_id指定で絞る。"""
        out: list[bytes] = []
        end = time.time() + secs
        while time.time() < end:
            try:
                r = self.dev.read(64, timeout_ms=100)
            except Exception:
                r = None
            if r:
                b = bytes(r)
                if want_id is None or (b and b[0] == want_id):
                    out.append(b)
        return out

    def handshake_80(self) -> dict[str, str | None]:
        """0x80系ハンドシェイク (Switch実測順 05→01→02→04)。応答を記録。"""
        out: dict[str, str | None] = {}
        for sub in (0x05, 0x01, 0x02, 0x04):
            self._write(0x80, bytes((sub,)))
            time.sleep(0.3)
            reps = self._read_reports(0.5)
            # 0x81応答を探す。
            hit = next((r for r in reps if len(r) >= 2 and r[0] == 0x81 and r[1] == sub), None)
            out[f"80_{sub:02X}"] = _hex(hit) if hit else None
        return out

    def subcmd(self, sub: int, args: bytes = b"", reply_wait: float = 0.5) -> bytes | None:
        """0x01経由でsubcommandを送り、0x21応答を返す。

        出力0x01形式: [01][振動8B=0][sub][args...]。振動は中立で埋める
        (dekuNukem通り 00 01 40 40 ×2 ではなく0埋めでも受理される。
        不受理なら中立8Bに切替えること)。
        """
        out = bytes((sub,)) + bytes(args)
        self._write(0x01, bytes(8) + out, total=49)
        time.sleep(0.15)
        reps = self._read_reports(reply_wait, want_id=0x21)
        # sub一致の応答を探す (0x21の[14]=sub)。
        for r in reversed(reps):
            if len(r) >= 15 and r[14] == sub:
                return r
        return reps[-1] if reps else None

    def spi_read(self, addr: int, size: int) -> tuple[bytes | None, bytes]:
        """0x10 SPI読出し。戻値は(0x21応答全体, data部)。"""
        req = bytes((addr & 0xFF, (addr >> 8) & 0xFF, (addr >> 16) & 0xFF,
                     (addr >> 24) & 0xFF, size & 0xFF))
        rep = self.subcmd(0x10, req)
        if rep is None or len(rep) < 20 + size:
            return rep, b""
        return rep, rep[20:20 + size]


def collect_030_stats(dev: Probe, n: int = 500, timeout: float = 30.0) -> dict:
    """0x30レポートn件のtimer差分ヒストグラム・長・間隔。"""
    reps: list[bytes] = []
    stamps: list[float] = []
    end = time.time() + timeout
    while time.time() < end and len(reps) < n:
        for r in dev._read_reports(0.5, want_id=0x30):
            reps.append(r)
            stamps.append(time.time())
            if len(reps) >= n:
                break
    timers = [r[1] for r in reps if len(r) >= 2]
    diffs: dict[int, int] = {}
    for a, b in zip(timers, timers[1:]):
        d = (b - a) & 0xFF
        diffs[d] = diffs.get(d, 0) + 1
    gaps: list[float] = [round((b - a) * 1000.0, 2) for a, b in zip(stamps, stamps[1:])]
    lens = sorted({len(r) for r in reps})
    return {"count": len(reps), "timer_diff_hist": {str(k): v for k, v in sorted(diffs.items())},
            "report_lens": lens,
            "gap_ms": {"avg": round(sum(gaps) / len(gaps), 2) if gaps else 0.0,
                       "min": min(gaps) if gaps else 0.0, "max": max(gaps) if gaps else 0.0,
                       "n": len(gaps)}}


def collect_imu_csv(dev: Probe, n: int = 100, timeout: float = 15.0) -> list[list[int]]:
    """IMU領域byte13-48を3サンプル×6 int16に展開。行=[ax,ay,az,g1,g2,g3]*3。"""
    rows: list[list[int]] = []
    end = time.time() + timeout
    while time.time() < end and len(rows) < n:
        for r in dev._read_reports(0.5, want_id=0x30):
            if len(r) < 49:
                continue
            row: list[int] = []
            for off in (13, 25, 37):
                for i in range(6):
                    v = r[off + i * 2] | (r[off + i * 2 + 1] << 8)
                    row.append(v - 65536 if v >= 0x8000 else v)
            rows.append(row)
            if len(rows) >= n:
                break
    return rows


def scan_spi(dev: Probe, addrs: list[int], size: int = 0x1D) -> dict[str, dict]:
    """指定アドレスを0x10で読む。戻値は {hex: {data, reply}}。"""
    out: dict[str, dict] = {}
    for a in addrs:
        rep, data = dev.spi_read(a, size)
        out[f"{a:08X}"] = {"size": size, "data": _hex(data),
                           "reply": _hex(rep) if rep else None}
        time.sleep(0.05)
    return out


def cmd_probe(args: argparse.Namespace) -> int:
    print("注意: Steamを終了してから実行してください (ProConを掴まれます)。")
    dev = Probe(args.vid, args.pid)
    try:
        result: dict = {"device": {"vid": f"{args.vid:04X}", "pid": f"{args.pid:04X}"},
                        "at": dt.datetime.now().isoformat(timespec="seconds")}
        print("0x80ハンドシェイク...")
        result["handshake80"] = dev.handshake_80()
        print("0x40(1)...")
        r40 = dev.subcmd(0x40, b"\x01")
        result["sub40"] = _hex(r40) if r40 else None
        print("0x41(03 00 01 01)...")
        r41 = dev.subcmd(0x41, IMU_SENS_DEFAULT)
        result["sub41"] = _hex(r41) if r41 else None
        print("0x03(0x30)...")
        r03 = dev.subcmd(0x03, b"\x30")
        result["sub03"] = _hex(r03) if r03 else None
        print("0x30を500件収集...")
        result["report030"] = collect_030_stats(dev)
        if args.yaw:
            input("手でyaw回転させながらEnterを押してください (10秒収集)...")
            result["imu_yaw"] = collect_imu_csv(dev)
        else:
            print("IMU静止100件収集...")
            result["imu_rest"] = collect_imu_csv(dev)
        print("SPI読出し...")
        if args.spi:
            addrs = [int(x, 16) for x in args.spi.split(",")]
        else:
            addrs = list(range(0x6000, 0x6100, 0x1D)) + list(range(0x8000, 0x8100, 0x1D))
        result["spi"] = scan_spi(dev, addrs)
        with open(args.out, "w", encoding="utf-8") as fp:
            json.dump(result, fp, indent=2)
        print(f"保存: {args.out}")
        return 0
    finally:
        dev.close()


def cmd_diff(args: argparse.Namespace) -> int:
    a = json.load(open(args.a, encoding="utf-8"))
    b = json.load(open(args.b, encoding="utf-8"))
    print(f"A={args.a} B={args.b}")
    print("--- handshake80 ---")
    for k in sorted(set(a.get("handshake80", {})) | set(b.get("handshake80", {}))):
        va = a.get("handshake80", {}).get(k)
        vb = b.get("handshake80", {}).get(k)
        mark = " same" if va == vb else " DIFF"
        print(f"{k}:{mark}")
        if va != vb:
            print(f"  A={str(va)[:80]}")
            print(f"  B={str(vb)[:80]}")
    for key in ("sub40", "sub41", "sub03"):
        va, vb = a.get(key), b.get(key)
        mark = " same" if va == vb else " DIFF"
        print(f"{key}:{mark}")
        if va != vb:
            print(f"  A={str(va)[:100]}")
            print(f"  B={str(vb)[:100]}")
    print("--- report030 ---")
    for side, d in (("A", a.get("report030", {})), ("B", b.get("report030", {}))):
        print(f"{side}: count={d.get('count')} lens={d.get('report_lens')} "
              f"gap={d.get('gap_ms')} hist={d.get('timer_diff_hist')}")
    print("--- spi ---")
    for k in sorted(set(a.get("spi", {})) | set(b.get("spi", {}))):
        da = (a.get("spi", {}).get(k) or {}).get("data")
        db = (b.get("spi", {}).get(k) or {}).get("data")
        mark = " same" if da == db else " DIFF"
        print(f"{k}:{mark}")
        if da != db:
            print(f"  A={str(da)[:80]}")
            print(f"  B={str(db)[:80]}")
    for key in ("imu_rest", "imu_yaw"):
        if key in a or key in b:
            ra = (a.get(key) or [None])[0]
            rb = (b.get(key) or [None])[0]
            print(f"{key} first row: A={ra} B={rb}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="ProCon/Pico USB probe (§2)")
    sub = ap.add_subparsers(dest="cmd")
    p = sub.add_parser("probe", help="デバイスを調べてjsonへ保存 (既定)")
    p.add_argument("--vid", type=lambda x: int(x, 16), default=NINTENDO_VID)
    p.add_argument("--pid", type=lambda x: int(x, 16), default=PROCON_PID)
    p.add_argument("--out", required=True)
    p.add_argument("--yaw", action="store_true", help="yaw回転中のIMUを取る")
    p.add_argument("--spi", default="", help="読むSPI addrの16進カンマ列")
    d = sub.add_parser("diff", help="2つのjsonの差分表")
    d.add_argument("a")
    d.add_argument("b")
    # 後方互換: サブコマンドなしはprobe扱い。
    if len(sys.argv) > 1 and sys.argv[1] not in ("probe", "diff", "-h", "--help"):
        sys.argv.insert(1, "probe")
    args = ap.parse_args()
    if args.cmd == "diff":
        return cmd_diff(args)
    if args.cmd == "probe" or args.cmd is None:
        if args.cmd is None:
            ap.print_help()
            return 1
        return cmd_probe(args)
    ap.print_help()
    return 1


if __name__ == "__main__":
    sys.exit(main())
