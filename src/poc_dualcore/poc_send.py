#!/usr/bin/env python3
"""pico-bcon dual-core PoC load generator (Task 2 only).

PoC FW (src/poc_dualcore) の UART1 (GP4/5・1Mbps) へ v3 STATE を送り続ける。
Task 5 の pc/bcon_send.py (本番送信ラッパ) とは別物。負荷試験専用。

使い方:
    pip install pyserial
    python poc_send.py COM5 --baud 1000000 --hz 1000 --secs 600
    python poc_send.py COM5 --baud 115200 --hz 500 --secs 600  # derated試験
    python poc_send.py COM5 --baud 115200 --sweep  # 系統的ボタン確認用

仕様 (spec/protocol_v3.md の抜粋):
- Frame: [SYNC=0xAB][TYPE][LEN][PAYLOAD][SEQ][CRC8/SMBUS over TYPE..SEQ]
- STATE: TYPE=0x01 LEN=8。BTN u32-LE (bit22-31は0固定) + LX LY RX RY。
- SEQは方向独立・mod256。BTNはカウンタ下位22bitを流し、bit変化を起こす。
- 1フレーム1 write() (PC側指針の単一write遵守)。

終了時に送信数・実効Hzを表示する。FW側のPOCログ行と突き合わせて判定する。
"""

import argparse
import struct
import sys
import time

SYNC = 0xAB
T_STATE = 0x01
BTN_MASK = 0x003FFFFF  # 22bit使用・予約bitは0送信


def crc8_smbus(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def build_state(btn: int, seq: int) -> bytes:
    payload = struct.pack("<I", btn & BTN_MASK) + bytes((0x80, 0x80, 0x80, 0x80))
    body = bytes((T_STATE, len(payload))) + payload + bytes((seq & 0xFF,))
    return bytes((SYNC,)) + body + bytes((crc8_smbus(body),))


def build_state_full(btn: int, lx: int, ly: int, rx: int, ry: int, seq: int) -> bytes:
    payload = struct.pack("<I", btn & BTN_MASK) + bytes((lx, ly, rx, ry))
    body = bytes((T_STATE, len(payload))) + payload + bytes((seq & 0xFF,))
    return bytes((SYNC,)) + body + bytes((crc8_smbus(body),))


# (bit, name) の順。GR/GL/C/HeadsetはSwitch1輸送で落とされる。
# 注意: Homeは確認画面から抜けるため sweep では最後尾に回す (SWEEP_TAIL)。
SWEEP_BUTTONS = [
    (0, "B"), (1, "A"), (2, "Y"), (3, "X"),
    (4, "R"), (5, "ZR"), (6, "Plus"), (7, "Rstick"),
    (8, "Down"), (9, "Right"), (10, "Left"), (11, "Up"),
    (12, "L"), (13, "ZL"), (14, "Minus"), (15, "Lstick"),
    (17, "Capture"),
]

# Homeは画面遷移するため最後。GR/GL/C/HeadsetはSwitch1で無視される。
SWEEP_TAIL = [
    (18, "GR(ignored on Switch1)"),
    (19, "GL(ignored on Switch1)"),
    (20, "C(ignored on Switch1)"),
    (21, "Headset(ignored on Switch1)"),
    (16, "Home(EXITs test screen: last)"),
]

SWEEP_DIAGONALS = [
    ("Up+Right", (1 << 11) | (1 << 9)),
    ("Right+Down", (1 << 9) | (1 << 8)),
    ("Down+Left", (1 << 8) | (1 << 10)),
    ("Left+Up", (1 << 10) | (1 << 11)),
]

SWEEP_STICKS = [
    ("LX min", 0x00, 0x80, 0x80, 0x80),
    ("LX max", 0xFF, 0x80, 0x80, 0x80),
    ("LY min", 0x80, 0x00, 0x80, 0x80),
    ("LY max", 0x80, 0xFF, 0x80, 0x80),
    ("RX min", 0x80, 0x80, 0x00, 0x80),
    ("RX max", 0x80, 0x80, 0xFF, 0x80),
    ("RY min", 0x80, 0x80, 0x80, 0x00),
    ("RY max", 0x80, 0x80, 0x80, 0xFF),
]

def sweep(ser, hz: float, dwell: float, gap: float) -> int:
    """全22bitを1つずつ順送り (画面の点灯と突き合わせ用)。戻り値は送信数。"""
    import time as _t
    seq = 0
    n = 0
    period = 1.0 / hz

    def hold(btn: int, lx: int, ly: int, rx: int, ry: int, secs: float) -> None:
        nonlocal seq, n
        t_end = _t.perf_counter() + secs
        while _t.perf_counter() < t_end:
            t0 = _t.perf_counter()
            ser.write(build_state_full(btn, lx, ly, rx, ry, seq))
            seq = (seq + 1) & 0xFF
            n += 1
            wait = t0 + period - _t.perf_counter()
            if wait > 0:
                _t.sleep(wait)

    def neutral(secs: float) -> None:
        hold(0, 0x80, 0x80, 0x80, 0x80, secs)

    print(f"--- sweep: {len(SWEEP_BUTTONS)} buttons, dwell={dwell}s, gap={gap}s ---", flush=True)
    for bit, name in SWEEP_BUTTONS:
        print(f"[bit{bit:2d}] {name}", flush=True)
        hold(1 << bit, 0x80, 0x80, 0x80, 0x80, dwell)
        neutral(gap)
    print("--- sweep: diagonals ---", flush=True)
    for name, btn in SWEEP_DIAGONALS:
        print(f"diagonal {name}", flush=True)
        hold(btn, 0x80, 0x80, 0x80, 0x80, dwell)
        neutral(gap)
    print("--- sweep: sticks ---", flush=True)
    for name, lx, ly, rx, ry in SWEEP_STICKS:
        print(f"stick {name}", flush=True)
        hold(0, lx, ly, rx, ry, dwell)
        neutral(gap)
    print("--- sweep: tail (Home LAST: exits test screen) ---", flush=True)
    for bit, name in SWEEP_TAIL:
        print(f"[bit{bit:2d}] {name}", flush=True)
        hold(1 << bit, 0x80, 0x80, 0x80, 0x80, dwell)
        neutral(gap)
    print(f"sweep done sent={n}", flush=True)
    return n


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("port", help="serial port (e.g. COM5 or /dev/ttyUSB0)")
    ap.add_argument("--baud", type=int, default=1000000)
    ap.add_argument("--hz", type=float, default=1000.0)
    ap.add_argument("--secs", type=float, default=600.0)
    ap.add_argument("--sweep", action="store_true",
                    help="systematic button sweep (bits 0-21 in order, then diagonals, then sticks)")
    ap.add_argument("--dwell", type=float, default=0.6,
                    help="seconds to hold each sweep step (short: Home exits test screen)")
    ap.add_argument("--gap", type=float, default=0.3,
                    help="neutral seconds between sweep steps")
    args = ap.parse_args()

    try:
        import serial
    except ImportError:
        print("need pyserial: pip install pyserial", file=sys.stderr)
        return 2

    period = 1.0 / args.hz
    line_bytes = args.baud / 10.0  # 8N1 = 10シンボル/B
    need = args.hz * 13.0  # STATE=13B/frame
    if need > line_bytes * 0.8:
        print(f"warn: {args.hz}Hz needs ~{need:.0f}B/s, line ~{line_bytes:.0f}B/s;"
              " lower --hz (e.g. 500 @115200)", file=sys.stderr)
    ser = serial.Serial(args.port, args.baud, timeout=1)
    # FTDI系は latency timer 1ms 推奨 (spec参照)。設定できなければ警告のみ。
    try:
        ser.set_latency_timer(1)  # type: ignore[attr-defined]
    except Exception as e:
        print(f"warn: latency timer unchanged ({e})", file=sys.stderr)

    n = 0
    seq = 0
    t0 = time.perf_counter()
    if args.sweep:
        # sweepは目視用。既定1000Hzのままでは速すぎるため60Hzに落とす
        # (--hz明示時はそちらを尊重)。
        sweep_hz = 60.0 if args.hz >= 1000 else args.hz
        try:
            sweep(ser, sweep_hz, args.dwell, args.gap)
        finally:
            ser.close()
        return 0
    t_end = t0 + args.secs
    t_mark = t0
    try:
        while True:
            now = time.perf_counter()
            if now >= t_end:
                break
            ser.write(build_state(n, seq))
            n += 1
            seq = (seq + 1) & 0xFF
            if now - t_mark >= 1.0:
                print(f"sent={n} seq=0x{seq:02X}", flush=True)
                t_mark = now
            target = t0 + n * period
            wait = target - time.perf_counter()
            if wait > 0:
                # sub-ms精度のためsleep+busy混合
                if wait > 0.002:
                    time.sleep(wait - 0.001)
                while time.perf_counter() < target:
                    pass
    except KeyboardInterrupt:
        pass
    finally:
        dt = time.perf_counter() - t0
        print(f"done sent={n} secs={dt:.1f} eff_hz={n / dt:.1f}")
        ser.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
