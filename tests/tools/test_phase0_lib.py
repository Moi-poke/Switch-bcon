"""phase0_lib の検証 (実機不要)。"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))

import phase0_lib as L
import pytest


# --- parser ---

BCON = ("BCON t=12s hs=1 mnt=1 cfg=1 cid=0 wired=1 cap=0/0/0 res=0 err=00 rum=0+0 "
        "ibdrop=0 obdrop=0 frames=0 crc=0 drop=0 ovr=0 rx80=1 tx81=0 tx21=5 in30=1500 "
        "iters=0 pm=0 baud=3L")
BUILD = ("IMUD build mode=1 axis=2 g=5899 T=4000 fill=7 acc=0,0,4096 force=0 "
         "interval=8 git=68531ce dirty=0")


def test_parse_stats() -> None:
    ev = L.parse_line(BCON)
    assert isinstance(ev, L.Stats)
    assert (ev.t_s, ev.in30) == (12, 1500)
    assert ev.mnt == 1


def test_parse_build() -> None:
    ev = L.parse_line(BUILD)
    assert isinstance(ev, L.Build)
    assert ev.g == 5899
    assert ev.acc == (0, 0, 4096)
    assert ev.git == "68531ce"
    assert ev.dirty == "0"


def test_parse_subs() -> None:
    a = L.parse_line("IMUD sub40 arg=01 en=1")
    assert isinstance(a, L.Sub40) and (a.arg, a.en, a.short) == (1, 1, False)
    b = L.parse_line("IMUD sub03 mode=30")
    assert isinstance(b, L.Sub03) and (b.mode, b.short) == (0x30, False)
    c = L.parse_line("IMUD sub41 args=11 22 33 44 len=16")
    assert isinstance(c, L.Sub41) and c.args == (0x11, 0x22, 0x33, 0x44)
    d = L.parse_line("IMUD sub11 addr=00804000 size=1D len=16")
    assert isinstance(d, L.Sub11) and (d.addr, d.size) == ("00804000", 0x1D)
    e = L.parse_line("IMUD sub40 arg=00 en=0 (short)")
    assert isinstance(e, L.Sub40) and e.short is True


def test_parse_steps() -> None:
    s = L.parse_line("IMUD step#3 t0=1000 axis=2 g=1475 T=4000 fill=7 acc=0,0,4096")
    assert isinstance(s, L.StepStart) and s.seq == 3
    e = L.parse_line("IMUD step#3 end N_actual=500 T=4000 g=1475 axis=2 fill=7")
    assert isinstance(e, L.StepEnd) and (e.seq, e.n_actual) == (3, 500)


def test_parse_broken_is_other() -> None:
    assert isinstance(L.parse_line("IMUD build mode=1 axis="), L.Other)
    assert isinstance(L.parse_line("BCON t=xx"), L.Other)
    assert isinstance(L.parse_line("hello"), L.Other)
    assert isinstance(L.parse_line(""), L.Other)


# --- build照合 ---

EXP = L.Expect(mode=1, axis=2, g=5899, t_ms=4000, fill=7, acc=(0, 0, 4096),
               force=0, interval=8)


def _build(**kw) -> L.Build:
    d = dict(mode=1, axis=2, g=5899, t_ms=4000, fill=7, acc=(0, 0, 4096),
             force=0, interval=8, git="68531ce", dirty="0")
    d.update(kw)
    return L.Build(**d)  # type: ignore[arg-type]


def test_check_build_all_match() -> None:
    assert L.check_build(_build(), EXP) == []


def test_check_build_one_mismatch() -> None:
    bad = L.check_build(_build(g=1475), EXP)
    assert len(bad) == 1 and bad[0].startswith("g(")


def test_check_build_dirty_and_git() -> None:
    bad = L.check_build(_build(git="abc1234", dirty="1"), EXP)
    assert len(bad) == 2


# --- 角度入力 ---

def test_parse_angle() -> None:
    assert L.parse_angle("1470") == 1470.0
    assert L.parse_angle("4+30") == 1470.0
    assert L.parse_angle("-2+90") == -630.0
    assert L.parse_angle("0+90") == 90.0
    with pytest.raises(ValueError):
        L.parse_angle("abc")
    with pytest.raises(ValueError):
        L.parse_angle("4++30")


# --- S1 ---

def test_rate_stats() -> None:
    ss = [L.Stats(t_s=i, in30=125 * (i + 1)) for i in range(12)]
    st = L.rate_stats(ss)
    assert st is not None
    assert (st.avg, st.min, st.max, st.n) == (125.0, 125.0, 125.0, 11)
    assert L.rate_stats([L.Stats(t_s=0, in30=0)]) is None


def test_rate_stats_skips_gaps() -> None:
    # 電源断の空白 (t=5→20) と再起動の巻き戻しを除外する。
    ss = [L.Stats(t_s=i, in30=125 * (i + 1)) for i in range(5)]
    ss += [L.Stats(t_s=20 + i, in30=125 * (i + 1)) for i in range(5)]
    st = L.rate_stats(ss)
    assert st is not None
    assert st.n == 8
    assert st.avg == 125.0


# --- S3 ---

def test_judge_e4_ok() -> None:
    r = L.judge_e4({0: ("上チルト", "下チルト"), 1: ("時計回りロール", "反時計回りロール"),
                    2: ("右パン", "左パン")})
    assert r.yaw_axis == 2
    assert r.yaw_positive == "右"
    assert r.warnings == ()


def test_judge_e4_two_candidates() -> None:
    r = L.judge_e4({0: ("左パン", "右パン"), 1: ("動かない", "動かない"),
                    2: ("右パン", "左パン")})
    assert r.yaw_axis is None


def test_judge_e4_polarity() -> None:
    r = L.judge_e4({0: ("上チルト", "上チルト"), 1: ("動かない", "動かない"),
                    2: ("右パン", "左パン")})
    assert r.yaw_axis == 2
    assert any("逆向きでない" in w for w in r.warnings)


# --- S4/S5 ---

def test_pick_r() -> None:
    assert L.pick_r(2.0, 2.0) == (2.0, False)
    adopted, differ = L.pick_r(2.0, 2.2)
    assert differ is True and adopted == 2.2


def test_c_phys() -> None:
    assert L.c_phys(1440.0, 2.0, 500, 5899) == pytest.approx(1440.0 / (2.0 * 500 * 5899))


def test_check_linearity() -> None:
    assert L.check_linearity(360.0, 1440.0) is True
    assert L.check_linearity(360.0, 1000.0) is False


def test_nearest_hypothesis() -> None:
    assert L.nearest_hypothesis(360.0, 1440.0) == "時間基準 k=0.06103"
    assert L.nearest_hypothesis(774.0, 3097.0) == "slot基準 Δt=5ms k=0.070"


# --- S6/S7 ---

def test_judge_e3a() -> None:
    assert L.judge_e3a(1440.0, 500, 1440.0, 250) == "時間基準"
    assert L.judge_e3a(1440.0, 500, 720.0, 250) == "slot基準"
    assert L.judge_e3a(1440.0, 500, 1000.0, 250) == "判定不能"


def test_judge_e3b_ratio() -> None:
    assert "全slot" in L.judge_e3b_ratio(3.0)
    assert "そのslotだけ" in L.judge_e3b_ratio(1.0)


# --- S8 ---

def test_e5_vector() -> None:
    assert L.e5_vector(2, 4096) == (2048, 0, 3547)
    assert L.e5_vector(2, 8192) == (4096, 0, 7094)
    assert L.e5_vector(2, 16384) == (8192, 0, 14189)
    assert L.e5_vector(0, 4096) == (3547, 2048, 0)
    assert L.e5_vector(1, 4096) == (2048, 3547, 0)
