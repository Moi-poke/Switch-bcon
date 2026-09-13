#!/usr/bin/env python3
"""pico-bcon dual-core PoC load generator (Task 2 only).

PoC FW (src/poc_dualcore) の UART1 (GP4/5・1Mbps) へ v3 STATE を送り続ける。
Task 5 の pc/bcon_send.py (本番送信ラッパ) とは別物。負荷試験専用。

使い方:
    pip install pyserial
    python poc_send.py COM5 --baud 1000000 --hz 1000 --secs 600
    python poc_send.py COM5 --baud 115200 --hz 500 --secs 600  # derated試験

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


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("port", help="serial port (e.g. COM5 or /dev/ttyUSB0)")
    ap.add_argument("--baud", type=int, default=1000000)
    ap.add_argument("--hz", type=float, default=1000.0)
    ap.add_argument("--secs", type=float, default=600.0)
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
