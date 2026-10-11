"""phase0_lib.py - Phase 0 実験支援の純ロジック (UI・I/Oなし)。

parser・照合・角度入力・各実験の判定を集める。FW・ビルドは触らない。
書式の根拠: G1(main.c stats_print)、G2(imu_dummy.c §6相当)。
"""

from __future__ import annotations

import math
import re
from dataclasses import dataclass, field

# ---------------------------------------------------------------------------
# 受信行 -> 型付きイベント
# ---------------------------------------------------------------------------

# BCON t=%lus ... in30=%lu ... (G1。sofは行に出ない)
_RE_STATS = re.compile(r"^BCON t=(\d+)s\b.*?in30=(\d+)\b")
# IMUD build mode=%d axis=%d g=%d T=%lu fill=%u acc=%d,%d,%d force=%d interval=%u git=%s dirty=%s
_RE_BUILD = re.compile(
    r"^IMUD build mode=(-?\d+) axis=(-?\d+) g=(-?\d+) T=(\d+) fill=(\d+) "
    r"acc=(-?\d+),(-?\d+),(-?\d+) force=(-?\d+) interval=(\d+) git=(\S+) dirty=(\S+)"
)
_RE_SUB40 = re.compile(r"^IMUD sub40 arg=([0-9A-Fa-f]{2}) en=(\d+)( \(short\))?")
_RE_SUB03 = re.compile(r"^IMUD sub03 mode=([0-9A-Fa-f]{2})( \(short\))?")
_RE_SUB41 = re.compile(
    r"^IMUD sub41 args=([0-9A-Fa-f]{2}) ([0-9A-Fa-f]{2}) ([0-9A-Fa-f]{2}) ([0-9A-Fa-f]{2}) len=(\d+)( \(short\))?"
)
_RE_SUB11 = re.compile(
    r"^IMUD sub11 addr=([0-9A-Fa-f]{8}) size=([0-9A-Fa-f]{2}) len=(\d+)( \(short\))?"
)
_RE_STEP_START = re.compile(
    r"^IMUD step#(\d+) t0=(\d+) axis=(-?\d+) g=(-?\d+) T=(\d+) fill=(\d+) acc=(-?\d+),(-?\d+),(-?\d+)"
)
_RE_STEP_END = re.compile(
    r"^IMUD step#(\d+) end N_actual=(\d+) T=(\d+) g=(-?\d+) axis=(-?\d+) fill=(\d+)"
)
# BCON t=%lus ... mnt=%d ... in30=%lu ... (G1。sofは行に出ない)
_RE_STATS = re.compile(r"^BCON t=(\d+)s\b.*?mnt=(\d+)\b.*?in30=(\d+)\b")
@dataclass(frozen=True)
class Build:
    mode: int
    axis: int
    g: int
    t_ms: int
    fill: int
    acc: tuple[int, int, int]
    force: int
    interval: int
    git: str
    dirty: str


@dataclass(frozen=True)
class Stats:
    t_s: int
    in30: int
    mnt: int = 0


@dataclass(frozen=True)
class Sub40:
    arg: int
    en: int
    short: bool


@dataclass(frozen=True)
class Sub03:
    mode: int
    short: bool


@dataclass(frozen=True)
class Sub41:
    args: tuple[int, int, int, int]
    len: int
    short: bool


@dataclass(frozen=True)
class Sub11:
    addr: str
    size: int
    len: int
    short: bool


@dataclass(frozen=True)
class StepStart:
    seq: int
    t0: int
    axis: int
    g: int
    t_ms: int
    fill: int
    acc: tuple[int, int, int]


@dataclass(frozen=True)
class StepEnd:
    seq: int
    n_actual: int
    t_ms: int
    g: int
    axis: int
    fill: int


@dataclass(frozen=True)
class Other:
    line: str


Event = Build | Stats | Sub40 | Sub03 | Sub41 | Sub11 | StepStart | StepEnd | Other


def parse_line(line: str) -> Event:
    """1行をイベントへ。壊れた行・未知行は Other。"""
    s = line.strip()
    m = _RE_BUILD.match(s)
    if m:
        g = m.groups()
        return Build(
            mode=int(g[0]), axis=int(g[1]), g=int(g[2]), t_ms=int(g[3]),
            fill=int(g[4]), acc=(int(g[5]), int(g[6]), int(g[7])),
            force=int(g[8]), interval=int(g[9]), git=g[10], dirty=g[11],
        )
    m = _RE_STATS.match(s)
    if m:
        return Stats(t_s=int(m.group(1)), mnt=int(m.group(2)), in30=int(m.group(3)))
    m = _RE_SUB40.match(s)
    if m:
        return Sub40(arg=int(m.group(1), 16), en=int(m.group(2)), short=m.group(3) is not None)
    m = _RE_SUB03.match(s)
    if m:
        return Sub03(mode=int(m.group(1), 16), short=m.group(2) is not None)
    m = _RE_SUB41.match(s)
    if m:
        return Sub41(
            args=(int(m.group(1), 16), int(m.group(2), 16), int(m.group(3), 16), int(m.group(4), 16)),
            len=int(m.group(5)), short=m.group(6) is not None,
        )
    m = _RE_SUB11.match(s)
    if m:
        return Sub11(addr=m.group(1).upper(), size=int(m.group(2), 16),
                     len=int(m.group(3)), short=m.group(4) is not None)
    m = _RE_STEP_START.match(s)
    if m:
        g = m.groups()
        return StepStart(seq=int(g[0]), t0=int(g[1]), axis=int(g[2]), g=int(g[3]),
                         t_ms=int(g[4]), fill=int(g[5]),
                         acc=(int(g[6]), int(g[7]), int(g[8])))
    m = _RE_STEP_END.match(s)
    if m:
        g = m.groups()
        return StepEnd(seq=int(g[0]), n_actual=int(g[1]), t_ms=int(g[2]),
                       g=int(g[3]), axis=int(g[4]), fill=int(g[5]))
    return Other(line=s)


# ---------------------------------------------------------------------------
# build行の照合
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class Expect:
    """そのrunで期待する構成。"""

    mode: int
    axis: int
    g: int
    t_ms: int
    fill: int
    acc: tuple[int, int, int]
    force: int
    interval: int
    git: str = "68531ce"
    dirty: str = "0"


def check_build(got: Build, want: Expect) -> list[str]:
    """不一致の項目名を返す。空なら全一致。"""
    bad: list[str] = []
    if got.mode != want.mode:
        bad.append(f"mode({got.mode}!={want.mode})")
    if got.axis != want.axis:
        bad.append(f"axis({got.axis}!={want.axis})")
    if got.g != want.g:
        bad.append(f"g({got.g}!={want.g})")
    if got.t_ms != want.t_ms:
        bad.append(f"T({got.t_ms}!={want.t_ms})")
    if got.fill != want.fill:
        bad.append(f"fill({got.fill}!={want.fill})")
    if got.acc != want.acc:
        bad.append(f"acc({got.acc}!={want.acc})")
    if got.force != want.force:
        bad.append(f"force({got.force}!={want.force})")
    if got.interval != want.interval:
        bad.append(f"interval({got.interval}!={want.interval})")
    if got.git != want.git:
        bad.append(f"git({got.git}!={want.git})")
    if got.dirty != want.dirty:
        bad.append(f"dirty({got.dirty}!={want.dirty})")
    return bad


# ---------------------------------------------------------------------------
# 角度入力 ("1470" / "4+30" / "-2+90")
# ---------------------------------------------------------------------------

_RE_ANGLE = re.compile(r"^\s*(-?\d+)\s*(?:\+\s*(-?\d+))?\s*$")


def parse_angle(text: str) -> float:
    """周+度の入力を度へ。"4+30"->1470。"-2+90"->-630。不正はValueError。"""
    m = _RE_ANGLE.match(text)
    if not m:
        raise ValueError(f"角度の形式が違います: {text!r} (例: 1470 / 4+30 / -2+90)")
    turns = int(m.group(1))
    deg = int(m.group(2)) if m.group(2) is not None else 0
    if m.group(2) is None:
        # 単独の数は度として扱う。周回表記と区別しない。
        return float(turns)
    if turns < 0:
        # "-2+90" = 負方向へ2周回って90°手前 = -(720-90)。
        return float(turns * 360 + abs(deg))
    return float(turns * 360 + deg)

# ---------------------------------------------------------------------------
# S1 周期実測
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class RateStat:
    avg: float
    min: float
    max: float
    n: int


def rate_stats(samples: list[Stats]) -> RateStat | None:
    """in30の1秒ごとの差分の平均・最小・最大。2行未満はNone。

    抜き差しによる電源断の空白(dt>2の区間)は除外する。
    in30が巻き戻った区間(再起動をまたいだ)も除外する。
    """
    if len(samples) < 2:
        return None
    diffs: list[float] = []
    for a, b in zip(samples, samples[1:]):
        dt = b.t_s - a.t_s
        if dt <= 0 or dt > 2:
            continue
        if b.in30 < a.in30:
            continue
        diffs.append((b.in30 - a.in30) / dt)
    if not diffs:
        return None
    return RateStat(avg=sum(diffs) / len(diffs), min=min(diffs), max=max(diffs), n=len(diffs))


# ---------------------------------------------------------------------------
# S3 E4判定
# ---------------------------------------------------------------------------

# 回転方向メニュー (runnerの表示と対で保つ)。
DIRECTIONS = ("左パン", "右パン", "上チルト", "下チルト", "時計回りロール", "反時計回りロール", "動かない")

# p/mで逆になるべき対。
_OPPOSITE = {
    "左パン": "右パン", "右パン": "左パン",
    "上チルト": "下チルト", "下チルト": "上チルト",
    "時計回りロール": "反時計回りロール", "反時計回りロール": "時計回りロール",
}


@dataclass(frozen=True)
class E4Result:
    yaw_axis: int | None
    yaw_positive: str | None  # yaw軸の正方向 ("左" or "右")
    warnings: tuple[str, ...] = ()


def judge_e4(obs: dict[int, tuple[str, str]]) -> E4Result:
    """軸 -> (p方向, m方向) からyaw軸を決める。

    yaw軸 = p/mとも左右パンで向きが逆の軸。1つに決まらなければyaw_axis=None。
    """
    warnings: list[str] = []
    for axis, (p, m) in sorted(obs.items()):
        if p == "動かない" or m == "動かない":
            warnings.append(f"軸{axis}: 動かない方向がある(p={p}, m={m})")
        elif _OPPOSITE.get(p) != m:
            warnings.append(f"軸{axis}: p/mが逆向きでない(p={p}, m={m})")
    cands = [a for a, (p, m) in obs.items()
             if p in ("左パン", "右パン") and _OPPOSITE.get(p) == m]
    if len(cands) != 1:
        warnings.append(f"yaw軸が1つに決まらない(候補={cands})")
        return E4Result(yaw_axis=None, yaw_positive=None, warnings=tuple(warnings))
    yaw = cands[0]
    positive = "左" if obs[yaw][0] == "左パン" else "右"
    return E4Result(yaw_axis=yaw, yaw_positive=positive, warnings=tuple(warnings))


# ---------------------------------------------------------------------------
# S4 R / S5 E2
# ---------------------------------------------------------------------------

def ratio_r(theta_game: float, theta_phys: float = 720.0) -> float:
    """R = θ_game / θ_phys。"""
    return theta_game / theta_phys


def pick_r(r4: float, r8: float, tol: float = 0.05) -> tuple[float, bool]:
    """2回のRから採用値を決める。差がtol超なら8秒側。戻値は(採用R, 差あり)。"""
    base = abs(r8) if r8 != 0 else 1.0
    if abs(r4 - r8) / base > tol:
        return r8, True
    return (r4 + r8) / 2.0, False


def c_phys(theta_game: float, r: float, n_actual: int, g_raw: int) -> float:
    """C_phys = θ / (R × N × g)。"""
    return theta_game / (r * n_actual * g_raw)


# ID008予測角表 (8ms、yaw、FILL_ALL、T=4000)。(仮説名, 90dps角, 360dps角)
PREDICT_TABLE: tuple[tuple[str, float, float], ...] = (
    ("時間基準 k=0.06103", 360.0, 1440.0),
    ("時間基準 k=0.070", 413.0, 1652.0),
    ("slot基準 Δt=5ms k=0.06103", 675.0, 2700.0),
    ("slot基準 Δt=5ms k=0.070", 774.0, 3097.0),
)


def nearest_hypothesis(theta90: float, theta360: float) -> str:
    """実測に最も近い仮説名。距離は2点の相対誤差の和。"""
    best = ""
    best_d = math.inf
    for name, p90, p360 in PREDICT_TABLE:
        d = abs(theta90 - p90) / p90 + abs(theta360 - p360) / p360
        if d < best_d:
            best_d = d
            best = name
    return best


def check_linearity(theta90: float, theta360: float, tol: float = 0.10) -> bool:
    """θ360/θ90 が 4±tol か。"""
    if theta90 == 0:
        return False
    return abs(theta360 / theta90 - 4.0) <= tol * 4.0


# ---------------------------------------------------------------------------
# S6 E3a / S7 E3b
# ---------------------------------------------------------------------------

def judge_e3a(theta8: float, n8: int, theta16: float, n16: int, tol: float = 0.10) -> str:
    """積分基準の判定。「時間基準」/「slot基準」/「判定不能」。"""
    if n8 <= 0 or n16 <= 0 or theta8 == 0:
        return "判定不能"
    r_theta = theta16 / theta8
    r_norm = (theta16 / n16) / (theta8 / n8)
    if abs(r_theta - 1.0) <= tol and abs(r_norm - 2.0) <= tol * 2.0:
        return "時間基準"
    if abs(r_theta - 0.5) <= tol and abs(r_norm - 1.0) <= tol:
        return "slot基準"
    return "判定不能"


def judge_e3b_ratio(ratio: float, tol: float = 0.15) -> str:
    """ALL/各runの角度比の判定文。"""
    if abs(ratio - 3.0) <= tol * 3.0:
        return "全slotが同等(比≈3)"
    if abs(ratio - 1.0) <= tol:
        return "そのslotだけ使用(比≈1)"
    return f"中間値(比={ratio:.2f})"


# ---------------------------------------------------------------------------
# S8 E5成分計算
# ---------------------------------------------------------------------------

def e5_vector(yaw_axis: int, m: int) -> tuple[int, int, int]:
    """大きさMを重力軸から30°傾けたベクトル。

    傾け先 = yaw軸以外の軸のうち番号の小さい方。
    重力軸成分 = round(M×0.8660)、傾け先 = round(M×0.5)。
    """
    tilt = min(a for a in (0, 1, 2) if a != yaw_axis)
    v = [0, 0, 0]
    v[yaw_axis] = round(m * 0.8660)
    v[tilt] = round(m * 0.5)
    return (v[0], v[1], v[2])


# ---------------------------------------------------------------------------
# 実験の構成表
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class RunSpec:
    """1runの定義。uf2名と期待構成。"""

    name: str
    uf2: str
    expect: Expect


def e4_specs() -> list[RunSpec]:
    """E4の6run。Acc=(0,0,4096)。"""
    out: list[RunSpec] = []
    for axis in (0, 1, 2):
        for g, suf in ((1475, "p"), (-1475, "m")):
            nm = f"e4ax{axis}{suf}"
            out.append(RunSpec(
                name=nm, uf2=f"switch-bcon-imu-dummy-{nm}.uf2",
                expect=Expect(mode=1, axis=axis, g=g, t_ms=4000, fill=7,
                              acc=(0, 0, 4096), force=0, interval=8),
            ))
    return out


def e5_specs(yaw_axis: int) -> list[RunSpec]:
    """E5の3run (M=4096/8192/16384)。STEP不使用だが構成はSTEP既定。"""
    out: list[RunSpec] = []
    for m in (4096, 8192, 16384):
        v = e5_vector(yaw_axis, m)
        nm = f"e5-{m}"
        out.append(RunSpec(
            name=nm, uf2=f"switch-bcon-imu-dummy-{nm}.uf2",
            expect=Expect(mode=1, axis=yaw_axis, g=1475, t_ms=4000, fill=7,
                          acc=v, force=0, interval=8),
        ))
    return out


#: SPI 0x6020 の既知24B (要約用)。
SPI_6020_HEX = "00 00 00 00 00 01 00 40 00 40 00 40 00 00 00 00 00 00 3B 34 3B 34 3B 34"
