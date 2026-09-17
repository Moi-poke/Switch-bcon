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
  LEN=12拡張あり：BTN u32-LE + LX LY RX RY をu16LE×4（0-4095・中央0x0800）。
  --hold12 で送信（例：--hold12 0x2 0x800 0x800 0x800 0x800）。
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


def build_state12(btn: int, lx: int, ly: int, rx: int, ry: int, seq: int) -> bytes:
    """STATE LEN=12 (12bit sticks, u16LE x4, center 0x0800). Masked to 16bit wire size."""
    payload = struct.pack("<IHHHH", btn & BTN_MASK,
                          lx & 0xFFFF, ly & 0xFFFF, rx & 0xFFFF, ry & 0xFFFF)
    body = bytes((T_STATE, len(payload))) + payload + bytes((seq & 0xFF,))
    return bytes((SYNC,)) + body + bytes((crc8_smbus(body),))


def build_key_delete(seq: int) -> bytes:
    """KEY_DELETE LEN=0 (tombstones BCHO + drops classic keys on FW side)."""
    body = bytes((T_KEY_DELETE, 0, seq & 0xFF))
    return bytes((SYNC,)) + body + bytes((crc8_smbus(body),))


T_PING = 0x03
T_PONG = 0x21
T_KEY_DELETE = 0x33
T_HELLO = 0x10
T_HELLO_ACK = 0x11
T_STATUS = 0x20
T_STATUS_REQ = 0x35
T_BAUD_SET = 0x36
T_BOOTSEL = 0x37
BOOTSEL_MAGIC = 0x5A

# B §3 / B-0 共有レート表 (src/proto/baud.c と同一)。
BAUD_TABLE = {0: 115200, 1: 460800, 2: 921600, 3: 1000000, 4: 2000000}


def build_baudset(idx: int, seq: int) -> bytes:
    """BAUD_SET LEN=1 (B §3: rate index 0-4)。"""
    body = bytes((T_BAUD_SET, 1, idx & 0xFF, seq & 0xFF))
    return bytes((SYNC,)) + body + bytes((crc8_smbus(body),))


def build_bootsel(seq: int) -> bytes:
    """BOOTSEL LEN=1 magic 0x5A (dev only: USB BOOTSEL reboot)."""
    body = bytes((T_BOOTSEL, 1, BOOTSEL_MAGIC, seq & 0xFF))
    return bytes((SYNC,)) + body + bytes((crc8_smbus(body),))


def build_ping(seq: int) -> bytes:
    body = bytes((T_PING, 0, seq & 0xFF))
    return bytes((SYNC,)) + body + bytes((crc8_smbus(body),))


def build_hello(ver: int = 4, flags: int = 1, seq: int = 0) -> bytes:
    body = bytes((T_HELLO, 2, ver & 0xFF, flags & 0xFF, seq & 0xFF))
    return bytes((SYNC,)) + body + bytes((crc8_smbus(body),))


def scan_frames(ser, secs: float):
    """受信frameを走査し (TYPE, SEQ, payload) を列挙するgenerator."""
    import time as _t
    buf = bytearray()
    t_end = _t.perf_counter() + secs
    while _t.perf_counter() < t_end:
        chunk = ser.read(64)
        if chunk:
            buf += chunk
        while len(buf) >= 5:
            try:
                i = buf.index(SYNC)
            except ValueError:
                buf.clear()
                break
            if i > 0:
                del buf[:i]
            if len(buf) < 3:
                break
            ln = buf[2]
            if ln > 32:
                del buf[0]
                continue
            if len(buf) < 3 + ln + 2:
                break
            body = bytes(buf[1:3 + ln + 1])
            if crc8_smbus(body) != buf[3 + ln + 1]:
                del buf[0]
                continue
            yield (buf[1], buf[3 + ln], bytes(buf[3:3 + ln]))
            del buf[:3 + ln + 2]


def hello_check(ser, secs: float = 6.0) -> int:
    """HELLO→HELLO_ACK＋自動STATUSの確認 (Step 3用)。"""
    ser.reset_input_buffer()
    ser.write(build_hello())
    print("HELLO sent (ver=4 flags=1: auto STATUS on)", flush=True)
    n_status = 0
    for typ, seq, pay in scan_frames(ser, secs):
        if typ == T_HELLO_ACK and len(pay) == 4:
            print(f"HELLO_ACK ver={pay[0]} fw={pay[1]}.{pay[2]} "
                  f"result=0x{pay[3]:02X} seq=0x{seq:02X}", flush=True)
        elif typ == T_STATUS and len(pay) == 7:
            n_status += 1
            print(f"STATUS flags=0x{pay[0]:02X} last=0x{pay[1]:02X} "
                  f"crc={pay[2] | (pay[3] << 8)} drop={pay[4] | (pay[5] << 8)} "
                  f"err=0x{pay[6]:02X} seq=0x{seq:02X}", flush=True)
        elif typ == T_PONG:
            print(f"(PONG echo=0x{pay[0]:02X})", flush=True)
    print(f"hello check done status_rx={n_status}", flush=True)
    return 0


def ping_test(ser, count: int, timeout: float = 0.5) -> int:
    """PING→PONG RTT計測 (Step 3用)。PONG payload=送信SEQのエコーで照合。"""
    import time as _t
    ser.reset_input_buffer()
    ok = 0
    rtts = []
    seq = 0
    for _ in range(count):
        ser.write(build_ping(seq))
        t0 = _t.perf_counter()
        buf = bytearray()
        got = None
        while _t.perf_counter() - t0 < timeout:
            chunk = ser.read(64)
            if chunk:
                buf += chunk
            # frame走査: SYNC..TYPE..LEN..PAYLOAD..SEQ..CRC
            while len(buf) >= 5:
                try:
                    i = buf.index(SYNC)
                except ValueError:
                    buf.clear()
                    break
                if i > 0:
                    del buf[:i]
                if len(buf) < 3:
                    break
                ln = buf[2]
                if ln > 32:
                    del buf[0]
                    continue
                if len(buf) < 3 + ln + 2:
                    break
                body = bytes(buf[1:3 + ln + 1])
                if crc8_smbus(body) != buf[3 + ln + 1]:
                    del buf[0]
                    continue
                typ, rxseq = buf[1], buf[3 + ln]
                pay = bytes(buf[3:3 + ln])
                del buf[:3 + ln + 2]
                if typ == T_PONG and ln == 1 and pay[0] == seq:
                    got = (_t.perf_counter() - t0) * 1000.0
                    break
        if got is None:
            print(f"ping seq=0x{seq:02X} TIMEOUT", flush=True)
        else:
            print(f"ping seq=0x{seq:02X} rtt={got:.2f}ms", flush=True)
            ok += 1
            rtts.append(got)
        seq = (seq + 1) & 0xFF
    if rtts:
        print(f"ping done ok={ok}/{count} min={min(rtts):.2f} "
              f"avg={sum(rtts)/len(rtts):.2f} max={max(rtts):.2f}ms", flush=True)
    else:
        print(f"ping done ok=0/{count}", flush=True)
    return 0 if ok == count else 1


def wait_status(ser, secs: float) -> bool:
    """STATUS frame arrival (BAUD_SET ACK待ち用)。"""
    for typ, _seq, pay in scan_frames(ser, secs):
        if typ == T_STATUS and len(pay) == 7:
            print(f"ACK STATUS flags=0x{pay[0]:02X} seq=0x{_seq:02X}", flush=True)
            return True
    return False


def ping_consecutive(ser, need: int, seq_start: int,
                     per_ping: float = 1.2) -> bool:
    """PONG echo連続need回でTrue (ladder採用判定用)。"""
    import time as _t
    seq = seq_start & 0xFF
    run = 0
    t_end = _t.perf_counter() + per_ping * (need + 2)
    ser.reset_input_buffer()
    while _t.perf_counter() < t_end and run < need:
        ser.write(build_ping(seq))
        t0 = _t.perf_counter()
        buf = bytearray()
        hit = False
        while _t.perf_counter() - t0 < per_ping:
            chunk = ser.read(64)
            if chunk:
                buf += chunk
            while len(buf) >= 5:
                try:
                    i = buf.index(SYNC)
                except ValueError:
                    buf.clear()
                    break
                if i > 0:
                    del buf[:i]
                if len(buf) < 3:
                    break
                ln = buf[2]
                if ln > 32:
                    del buf[0]
                    continue
                if len(buf) < 3 + ln + 2:
                    break
                body = bytes(buf[1:3 + ln + 1])
                if crc8_smbus(body) != buf[3 + ln + 1]:
                    del buf[0]
                    continue
                typ, rxseq = buf[1], buf[3 + ln]
                pay = bytes(buf[3:3 + ln])
                del buf[:3 + ln + 2]
                if typ == T_PONG and ln == 1 and pay[0] == seq:
                    hit = True
                    break
        if hit:
            run += 1
            print(f"ladder pong {run}/{need} seq=0x{seq:02X}", flush=True)
        else:
            run = 0
        seq = (seq + 1) & 0xFF
    return run >= need


def probe_ladder(ser, base_baud: int, candidates, need: int = 3) -> int:
    """B用ladder (HW未検証): 高→低にBAUD_SET→PONG need連続で採用。
    前提はrendezvous済み (hunt確定/BREAK直後)。全滅時はbase_baudに復帰。
    """
    import time as _t
    cur = base_baud
    seq = 0x40
    for idx in candidates:
        if idx not in BAUD_TABLE:
            print(f"ladder idx{idx}: unknown, skip", flush=True)
            continue
        want = BAUD_TABLE[idx]
        if want != cur:
            ser.write(build_baudset(idx, seq))
            seq = (seq + 1) & 0xFF
            print(f"ladder BAUD_SET idx{idx}={want} sent at {cur}", flush=True)
            if not wait_status(ser, 2.0):
                print(f"ladder idx{idx}={want}: no ACK, skip", flush=True)
                continue
            _t.sleep(0.25)  # Pico guard 100ms + margin
            ser.baudrate = want
            _t.sleep(0.05)
        else:
            print(f"ladder idx{idx}={want}: already here, verify only", flush=True)
        if ping_consecutive(ser, need, seq):
            print(f"ladder ADOPTED idx{idx}={want}", flush=True)
            return 0
        print(f"ladder idx{idx}={want}: failed, revert to {cur}", flush=True)
        ser.baudrate = cur
        _t.sleep(2.6)  # Pico 2s revert + margin
        seq = (seq + need + 2) & 0xFF
    print(f"ladder FAILED, staying at {cur}", flush=True)
    return 1


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
    ap.add_argument("--ping", type=int, default=0, metavar="N",
                    help="PING->PONG RTT measurement, N times")
    ap.add_argument("--hold12", nargs=5, metavar=("BTN", "LX", "LY", "RX", "RY"),
                    help="hold one 12-bit STATE (BTN hex-ok, sticks 0..4095 center 0x800)"
                    " for --secs at --hz (e.g. --hold12 0x2 0x800 0x800 0x800 0x800)")
    ap.add_argument("--key-delete", action="store_true",
                    help="send KEY_DELETE x3 (SEQ 0,1,2) for tombstone-bank prelude;"
                    " expect 'keys deleted' on log UART, then power-cycle and check host=0")
    ap.add_argument("--sweep12", action="store_true",
                    help="left-stick circle at 12-bit resolution for --secs (smoothness check)")
    ap.add_argument("--rev", type=float, default=4.0,
                    help="seconds per revolution for --sweep12")
    ap.add_argument("--hello", action="store_true",
                    help="HELLO->HELLO_ACK + auto STATUS check")
    ap.add_argument("--break_", type=float, default=0.0, metavar="SECS",
                    help="send serial BREAK (re-hunt request) for SECS seconds"
                    " at --baud, then exit (e.g. --break_ 0.1)")
    ap.add_argument("--probe-ladder", action="store_true",
                    help="B ladder (HW unverified): BAUD_SET high->low from"
                    " --baud (rendezvous), adopt on N consecutive PONGs")
    ap.add_argument("--probe-need", type=int, default=3, metavar="N",
                    help="consecutive PONGs to adopt a candidate (default 3)")
    ap.add_argument("--probe-cands", type=str, default="3,2,1,0",
                    help="candidate indexes high->low (default 3,2,1,0;"
                    " add 4 for 2M opt-in)")
    ap.add_argument("--bootsel", action="store_true",
                    help="send BOOTSEL magic (TYPE 0x37 magic 0x5A) once,"
                    " then exit (dev only: FW reboots to USB BOOTSEL in ~500ms)")
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
    if args.break_ > 0:
        try:
            ser.send_break(duration=args.break_)
            print(f"break sent {args.break_}s at {args.baud}", flush=True)
            return 0
        finally:
            ser.close()
    if args.bootsel:
        try:
            f = build_bootsel(seq)
            ser.write(f)
            print(f"bootsel sent seq={seq} {f.hex(' ')}", flush=True)
            return 0
        finally:
            ser.close()
    if args.probe_ladder:
        try:
            cands = [int(v.strip()) for v in args.probe_cands.split(",")
                     if v.strip() != ""]
            return probe_ladder(ser, args.baud, cands, args.probe_need)
        finally:
            ser.close()
    if args.hello:
        try:
            return hello_check(ser)
        finally:
            ser.close()
    if args.ping > 0:
        try:
            return ping_test(ser, args.ping)
        finally:
            ser.close()
    if args.sweep:
        # sweepは目視用。既定1000Hzのままでは速すぎるため60Hzに落とす
        # (--hz明示時はそちらを尊重)。
        sweep_hz = 60.0 if args.hz >= 1000 else args.hz
        try:
            sweep(ser, sweep_hz, args.dwell, args.gap)
        finally:
            ser.close()
        return 0
    if args.hold12:
        try:
            btn12 = int(args.hold12[0], 0) & BTN_MASK
            st12 = [int(v, 0) & 0xFFFF for v in args.hold12[1:5]]
            t_end12 = time.perf_counter() + args.secs
            while time.perf_counter() < t_end12:
                t0 = time.perf_counter()
                ser.write(build_state12(btn12, st12[0], st12[1], st12[2], st12[3], seq))
                seq = (seq + 1) & 0xFF
                n += 1
                wait = t0 + period - time.perf_counter()
                if wait > 0:
                    time.sleep(wait)
            print(f"hold12 done sent={n}", flush=True)
        finally:
            ser.close()
        return 0
    if args.key_delete:
        try:
            for kseq in (0, 1, 2):
                f = build_key_delete(kseq)
                ser.write(f)
                print(f"key-delete sent seq={kseq} {f.hex(' ')}", flush=True)
                time.sleep(0.1)
        finally:
            ser.close()
        return 0
    if args.sweep12:
        import math as _m
        try:
            t_start12 = time.perf_counter()
            while True:
                now12 = time.perf_counter()
                el = now12 - t_start12
                if el >= args.secs:
                    break
                ph = 2 * _m.pi * el / args.rev
                lx12 = int(0x800 + 0x7F0 * _m.cos(ph)) & 0xFFF
                ly12 = int(0x800 + 0x7F0 * _m.sin(ph)) & 0xFFF
                ser.write(build_state12(0, lx12, ly12, 0x800, 0x800, seq))
                seq = (seq + 1) & 0xFF
                n += 1
                wait = now12 + period - time.perf_counter()
                if wait > 0:
                    time.sleep(wait)
            print(f"sweep12 done sent={n}", flush=True)
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
