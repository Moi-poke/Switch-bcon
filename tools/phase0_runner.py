"""phase0_runner.py - Phase 0 実機実験の対話式案内ツール。

使い方:  python tools/phase0_runner.py [--simulate]
LOG UART (115200 8N1) に繋ぎ、1手順ずつ案内する。FWは変更しない。

--simulate: 実機なしで手順全体を通す仮想モード。
"""

from __future__ import annotations

import csv
import datetime as dt
import json
import os
import queue
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import phase0_lib as L

BAUD = 115200
# BOOTSEL時のボリュームラベル候補 (G3。Pico 2 WはRP2350)。
BOOT_LABELS = ("RP2350", "RPI-RP2")
WARN_CAL = "注意: 本体設定の「モーションコントロールの補正」は実行しないこと。"


def now_iso() -> str:
    return dt.datetime.now().isoformat(timespec="milliseconds")


def today() -> str:
    return dt.date.today().isoformat()


# ---------------------------------------------------------------------------
# シリアル層
# ---------------------------------------------------------------------------

class SerialLink:
    """受信スレッド付きのシリアル口。抜き差しで閉じない。"""

    def __init__(self) -> None:
        self._ser = None
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None
        self.events: queue.Queue[tuple[str, L.Event]] = queue.Queue()
        self._log_fp = None
        self._log_lock = threading.Lock()

    def open(self, port: str) -> None:
        import serial

        self._ser = serial.Serial(port, BAUD, timeout=0.2)
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self._thread.start()

    def set_log(self, path: str) -> None:
        with self._log_lock:
            if self._log_fp is not None:
                self._log_fp.close()
            self._log_fp = open(path, "a", encoding="utf-8")

    def _loop(self) -> None:
        assert self._ser is not None
        while not self._stop.is_set():
            try:
                raw = self._ser.readline()
            except Exception:
                time.sleep(0.5)
                continue
            if not raw:
                continue
            try:
                text = raw.decode("utf-8", errors="replace").rstrip("\r\n")
            except Exception:
                continue
            stamp = now_iso()
            with self._log_lock:
                if self._log_fp is not None:
                    self._log_fp.write(f"{stamp} {text}\n")
                    self._log_fp.flush()
            self.events.put((stamp, L.parse_line(text)))

    def send_step(self) -> None:
        """G4: LF単独で行確定する。"""
        assert self._ser is not None
        self._ser.write(b"step\n")

    def drain(self) -> list[tuple[str, L.Event]]:
        out = []
        while True:
            try:
                out.append(self.events.get_nowait())
            except queue.Empty:
                return out

    def close(self) -> None:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(2.0)
        with self._log_lock:
            if self._log_fp is not None:
                self._log_fp.close()
                self._log_fp = None
        if self._ser is not None:
            try:
                self._ser.close()
            except Exception:
                pass


class SimLink(SerialLink):
    """--simulate用の仮想シリアル。build/sub/stepの列を流す。"""

    def __init__(self, script: list[str]) -> None:
        super().__init__()
        self._script = list(script)

    def open(self, port: str) -> None:
        _ = port

    def send_step(self) -> None:
        pass

    def feed(self, lines: list[str]) -> None:
        for text in lines:
            stamp = now_iso()
            with self._log_lock:
                if self._log_fp is not None:
                    self._log_fp.write(f"{stamp} {text}\n")
                    self._log_fp.flush()
            self.events.put((stamp, L.parse_line(text)))

    def close(self) -> None:
        with self._log_lock:
            if self._log_fp is not None:
                self._log_fp.close()
                self._log_fp = None


# ---------------------------------------------------------------------------
# 状態
# ---------------------------------------------------------------------------

class State:
    def __init__(self, path: str) -> None:
        self.path = path
        self.d: dict = {}
        if os.path.exists(path):
            try:
                with open(path, encoding="utf-8") as fp:
                    self.d = json.load(fp)
            except Exception:
                self.d = {}

    def save(self) -> None:
        tmp = self.path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as fp:
            json.dump(self.d, fp, ensure_ascii=False, indent=2)
        os.replace(tmp, self.path)

    def get(self, key: str, default=None):
        return self.d.get(key, default)

    def set(self, key: str, value) -> None:
        self.d[key] = value
        self.save()


# ---------------------------------------------------------------------------
# 対話
# ---------------------------------------------------------------------------

class UI:
    def __init__(self, sim: bool = False, answers: list[str] | None = None) -> None:
        self.sim = sim
        self.answers = list(answers or [])

    def ask(self, prompt: str, default: str = "") -> str:
        if self.answers:
            ans = self.answers.pop(0)
            print(f"{prompt}[{ans}] (simulate)")
            return ans or default
        suffix = f" [{default}]" if default else ""
        try:
            ans = input(f"{prompt}{suffix}: ").strip()
        except EOFError:
            ans = ""
        return ans or default

    def choose(self, prompt: str, options: tuple[str, ...] | list[str]) -> str:
        print(prompt)
        for i, o in enumerate(options, 1):
            print(f"  {i}. {o}")
        while True:
            ans = self.ask("番号", "1" if self.answers else "")
            if self.answers is not None and not ans:
                ans = "1"
            try:
                idx = int(ans) - 1
                if 0 <= idx < len(options):
                    return options[idx]
            except ValueError:
                pass
            print("番号で選んでください。")

    def confirm(self, prompt: str) -> bool:
        ans = self.ask(f"{prompt} (y/n)", "y" if self.sim else "")
        return ans.lower().startswith("y")

    def red(self, text: str) -> None:
        print(f"\033[91m{text}\033[0m")

    def green(self, text: str) -> None:
        print(f"\033[92m{text}\033[0m")


# ---------------------------------------------------------------------------
# 実験の進行
# ---------------------------------------------------------------------------

class Runner:
    def __init__(self, root: str, ui: UI, link: SerialLink, state: State) -> None:
        self.root = root
        self.ui = ui
        self.link = link
        self.state = state
        self.logdir = os.path.join(root, "log")
        os.makedirs(self.logdir, exist_ok=True)
        self.runs: list[dict] = state.get("runs", [])
        self.subs: dict[str, list[str]] = {}  # run -> sub系ログ行

    # -- 共通 --
    def _record_run(self, rec: dict) -> None:
        self.runs.append(rec)
        self.state.set("runs", self.runs)

    def _start_log(self, run: str) -> None:
        path = os.path.join(self.logdir, f"phase0-{today()}-{run}.txt")
        self.link.set_log(path)
        self.subs[run] = []

    def _collect(self, secs: float) -> list[tuple[str, L.Event]]:
        """secs秒だけ受信を集める。"""
        end = time.time() + secs
        out: list[tuple[str, L.Event]] = []
        while time.time() < end:
            out += self.link.drain()
            time.sleep(0.1)
        out += self.link.drain()
        return out

    def _note_subs(self, run: str, evs: list[tuple[str, L.Event]]) -> None:
        for stamp, ev in evs:
            if isinstance(ev, (L.Sub40, L.Sub03, L.Sub41, L.Sub11)):
                self.subs.setdefault(run, []).append(f"{stamp} {ev}")

    def _wait_build(self, run: str, timeout: float) -> L.Build | None:
        end = time.time() + timeout
        while time.time() < end:
            for _stamp, ev in self.link.drain():
                if isinstance(ev, L.Build):
                    self._note_subs(run, [(_stamp, ev)])
                    return ev
                self._note_subs(run, [(_stamp, ev)])
            time.sleep(0.2)
        return None

    def _wait_step_end(self, run: str, seq: int, timeout: float) -> L.StepEnd | None:
        end = time.time() + timeout
        while time.time() < end:
            for _stamp, ev in self.link.drain():
                self._note_subs(run, [(_stamp, ev)])
                if isinstance(ev, L.StepEnd) and ev.seq == seq:
                    return ev
            time.sleep(0.2)
        return None

    def _check_subs_ready(self) -> bool:
        """Sub40(en=1)とSub03(0x30-0x33)の受信済み確認。"""
        # 直近のrunログから探す (stateに保存されたsub系)。
        seen40 = seen03 = False
        for lines in self.subs.values():
            for ln in lines:
                if "Sub40(" in ln and "en=1" in ln:
                    seen40 = True
                if "Sub03(" in ln:
                    try:
                        mode = int(ln.split("mode=")[1].split(")")[0].split(",")[0])
                        if 0x30 <= mode <= 0x33:
                            seen03 = True
                    except (ValueError, IndexError):
                        pass
        return seen40 and seen03

    def _flash(self, spec: L.RunSpec, prev_interval: int | None) -> L.Build | None:
        """UF2書換え→ドック挿し替え→ハンドシェイク待ち。失敗はNone。

        書換え直後のbuild行(PC接続時)で構成を照合し、その後ドック挿し替え
        による再起動後の2回目のbuild行を待つ。さらにSub40(en=1)と
        Sub03(0x30-0x33)の受信(ハンドシェイク完了)を待ってから返す。
        """
        print(f"--- {spec.name}: {spec.uf2} を書き込みます ---")
        if isinstance(self.link, SimLink):
            b = L.Build(mode=spec.expect.mode, axis=spec.expect.axis, g=spec.expect.g,
                         t_ms=spec.expect.t_ms, fill=spec.expect.fill, acc=spec.expect.acc,
                         force=spec.expect.force, interval=spec.expect.interval,
                         git=spec.expect.git, dirty=spec.expect.dirty)
            print("(simulate: build行を仮定)")
            return b
        print("BOOTSELを押しながらPicoをPCに挿してください。")
        drive = self._wait_drive(120.0)
        if drive is None:
            self.ui.red("ドライブが現れませんでした(120秒)。")
            return None
        import shutil

        src = os.path.join(self.logdir, spec.uf2)
        if not os.path.exists(src):
            self.ui.red(f"UF2がありません: {src}")
            return None
        print(f"{src} をコピーします...")
        shutil.copyfile(src, os.path.join(drive, "firmware.uf2"))
        print("書き込み完了を待ちます(ドライブが消えるまで)...")
        if not self._wait_drive_gone(drive, 60.0):
            self.ui.red("ドライブが消えませんでした。")
            return None
        if prev_interval is not None and prev_interval != spec.expect.interval:
            print("INTERVALが変わりました。Switchドックに挿す前に抜き差しして再列挙させてください。")
            if not self.ui.confirm("抜き差ししましたか"):
                return None
        print("Picoが起動しました。構成を確認します...")
        b = self._wait_build(spec.name, 60.0)
        if b is None:
            self.ui.red("build行が届きませんでした(60秒)。")
            return None
        want = self._expect_with_state_hash(spec.expect)
        bad = L.check_build(b, want)
        if bad:
            self.ui.red(f"構成が一致しません: {', '.join(bad)}")
            self.ui.red(f"受信: {b}")
            # gitのみの不一致なら、期待hashの切替えを提案する
            # (§0の旧UF2とdiag UF2の混在用)。
            if len(bad) == 1 and bad[0].startswith("git("):
                if self.ui.confirm(f"期待hashを {b.git} に切り替えますか"):
                    self.state.set("fw_commit", b.git)
                    return self._flash(spec, prev_interval)
            return None
        self.ui.green("構成一致。")
        print("Switchドックに挿し替えてください。挿し替え後の再起動を待ちます...")
        b2 = self._wait_build(spec.name, 120.0)
        if b2 is None:
            self.ui.red("再起動後のbuild行が届きませんでした(120秒)。")
            return None
        bad = L.check_build(b2, want)
        if bad:
            self.ui.red(f"再起動後の構成が一致しません: {', '.join(bad)}")
            return None
        print("Switchとのハンドシェイクを待ちます(Sub40/Sub03)...")
        if not self._wait_subs_ready(spec.name, 90.0):
            self.ui.red("Sub40(en=1)/Sub03(0x30-0x33)が揃いませんでした(90秒)。")
            print("ドックに挿さっているか、有線通信がONか確認してください。")
            if not self.ui.confirm("それでも続行しますか"):
                return None
        else:
            self.ui.green("ハンドシェイク確認。")
        return b2

    def _expect_with_state_hash(self, expect: L.Expect) -> L.Expect:
        """期待構成のgitをstate保存のhashで上書きする。

        stateのfw_commitが空なら既定のまま。ps1の-Commitと対で使う。
        """
        import dataclasses

        want_hash = self.state.get("fw_commit", "") or expect.git
        if want_hash == expect.git:
            return expect
        return dataclasses.replace(expect, git=want_hash)

    def _wait_subs_ready(self, run: str, timeout: float) -> bool:
        """Sub40(en=1)とSub03(0x30-0x33)が揃うまで待つ。"""
        seen40 = seen03 = False
        end = time.time() + timeout
        while time.time() < end:
            for _stamp, ev in self.link.drain():
                self._note_subs(run, [(_stamp, ev)])
                if isinstance(ev, L.Sub40) and ev.en == 1 and not ev.short:
                    seen40 = True
                if isinstance(ev, L.Sub03) and 0x30 <= ev.mode <= 0x33 and not ev.short:
                    seen03 = True
            if seen40 and seen03:
                return True
            time.sleep(0.2)
        return False

    def _wait_drive(self, timeout: float) -> str | None:
        import string

        try:
            import ctypes

            get_vol = ctypes.windll.kernel32.GetVolumeInformationW
        except Exception:
            get_vol = None  # type: ignore[assignment]
        end = time.time() + timeout
        while time.time() < end:
            for letter in string.ascii_uppercase:
                root = f"{letter}:\\"
                if not os.path.exists(root):
                    continue
                if get_vol is not None:
                    buf = ctypes.create_unicode_buffer(256)
                    try:
                        ok = get_vol(root, buf, 256, None, None, None, None, 0)
                    except Exception:
                        ok = 0
                    if ok and buf.value in BOOT_LABELS:
                        return root
                else:
                    # ラベルを読めない環境ではINFO_UF2.TXTで判定する。
                    info = os.path.join(root, "INFO_UF2.TXT")
                    if os.path.exists(info):
                        try:
                            with open(info, encoding="utf-8", errors="replace") as fp:
                                head = fp.read(512)
                            if "RP2350" in head or "RPI-RP2" in head:
                                return root
                        except OSError:
                            pass
            time.sleep(1.0)
        return None

    def _wait_drive_gone(self, drive: str, timeout: float) -> bool:
        end = time.time() + timeout
        # コピー直後はまだ見える。消えるまで待つ。
        time.sleep(2.0)
        while time.time() < end:
            if not os.path.exists(drive):
                return True
            time.sleep(1.0)
        return False

    def _ask_angle(self, prompt: str) -> float:
        while True:
            try:
                return L.parse_angle(self.ui.ask(prompt + " (度、または 周+度)"))
            except ValueError as e:
                print(e)

    # -- S0 --
    def s0_setup(self) -> None:
        if self.state.get("setup_done"):
            g = self.state.get("game", {})
            print(f"設定済み: {g}")
            if not self.state.get("fw_commit"):
                fw = self.ui.ask("FWコミット(short hash)", "f05ecee")
                self.state.set("fw_commit", fw)
            return
        print("=== S0 準備 ===")
        print(WARN_CAL)
        game = self.ui.ask("ゲーム名")
        ver = self.ui.ask("バージョン")
        sens = self.ui.ask("ジャイロ感度")
        fw = self.ui.ask("FWコミット(short hash)", "f05ecee")
        self.state.set("game", {"name": game, "version": ver, "sens": sens})
        self.state.set("fw_commit", fw)
        self.state.set("setup_done", True)

    # -- S1 --
    def s1_rate(self) -> None:
        if self.state.get("s1_done"):
            print("S1 済み。")
            return
        print("=== S1 周期実測 (on8-acc4096) ===")
        print(WARN_CAL)
        spec = L.RunSpec(name="s1-rate", uf2="switch-bcon-imu-dummy-on8-acc4096.uf2",
                          expect=L.Expect(mode=1, axis=2, g=5899, t_ms=4000, fill=7,
                                          acc=(0, 0, 4096), force=0, interval=8))
        self._start_log(spec.name)
        if isinstance(self.link, SimLink):
            base = int(time.time())
            self.link.feed([f"BCON t={base + i}s hs=1 mnt=1 cfg=1 cid=0 wired=1 "
                            f"cap=0/0/0 res=0 err=00 rum=0+0 ibdrop=0 obdrop=0 "
                            f"frames=0 crc=0 drop=0 ovr=0 rx80=0 tx81=0 tx21=0 "
                            f"in30={125 * (i + 1)} iters=0 pm=0 baud=3L"
                            for i in range(12)])
            b = L.Build(mode=1, axis=2, g=5899, t_ms=4000, fill=7, acc=(0, 0, 4096),
                        force=0, interval=8, git=self.state.get("fw_commit", "68531ce"), dirty="0")
        else:
            b = self._flash(spec, None)
            if b is None:
                return
            # _flash内で挿し替え再起動とハンドシェイク待ち済み。
            # Stats行が12行集まるまで待つ (最大40秒)。抜き差しの空白は
            # rate_stats側で除外するため、行数ベースで十分。
            print("Stats行を集めます(12行)...")
            stats: list[L.Stats] = []
            end = time.time() + 40.0
            while time.time() < end and len(stats) < 12:
                for _, ev in self.link.drain():
                    self._note_subs(spec.name, [(_, ev)])
                    if isinstance(ev, L.Stats):
                        stats.append(ev)
                        print(f"  Stats {len(stats)}/12 (in30={ev.in30})")
                time.sleep(0.2)
            st = L.rate_stats(stats)
            if st is None or st.n < 10:
                self.ui.red(f"有効な区間が足りません(n={st.n if st else 0})。再試行しますか?")
                if self.ui.confirm("再試行"):
                    self.s1_rate()
                return
            print(f"in30/s: 平均={st.avg:.1f} 最小={st.min:.1f} 最大={st.max:.1f} (n={st.n})")
            if not 110.0 <= st.avg <= 140.0:
                self.ui.red("110〜140/sの範囲外です。配線・周期を確認してください。")
                if not self.ui.confirm("続行しますか"):
                    return
            self._record_run({"run": spec.name, "build": str(b),
                              "rate": {"avg": st.avg, "min": st.min, "max": st.max, "n": st.n}})
            self.state.set("s1_done", True)
            self.state.set("prev_interval", 8)
            return
        # simulate
        evs = self._collect(0.5)
        stats = [ev for _, ev in evs if isinstance(ev, L.Stats)]
        st = L.rate_stats(stats)
        assert st is not None
        print(f"in30/s: 平均={st.avg:.1f} (simulate)")
        self._record_run({"run": spec.name, "build": str(b),
                          "rate": {"avg": st.avg, "min": st.min, "max": st.max, "n": st.n}})
        self.state.set("s1_done", True)
        self.state.set("prev_interval", 8)

    # -- S2 --
    def s2_e1(self) -> None:
        if self.state.get("s2_done"):
            print("S2 済み。")
            return
        print("=== S2 E1 受理確認 ===")
        print(WARN_CAL)
        for uf2acc, nm in (("sine-acc4096", "e1-sine4096"), ("sine-acc16384", "e1-sine16384")):
            acc = (0, 0, 4096) if "4096" in uf2acc else (0, 0, 16384)
            spec = L.RunSpec(name=nm, uf2=f"switch-bcon-imu-dummy-{uf2acc}.uf2",
                              expect=L.Expect(mode=0, axis=2, g=5899, t_ms=4000, fill=7,
                                              acc=acc, force=0, interval=8))
            self._start_log(spec.name)
            if isinstance(self.link, SimLink):
                b = L.Build(mode=0, axis=2, g=5899, t_ms=4000, fill=7, acc=acc,
                            force=0, interval=8, git=self.state.get("fw_commit", "68531ce"), dirty="0")
            else:
                b = self._flash(spec, self.state.get("prev_interval"))
                if b is None:
                    return
                self._collect(3.0)
            ans = self.ui.choose("ジャイロカメラは反応しましたか", ("反応あり", "なし"))
            self._record_run({"run": spec.name, "build": str(b), "e1": ans,
                              "subs": self.subs.get(spec.name, [])})
            if ans == "反応あり":
                self.state.set("s2_done", True)
                self.state.set("prev_interval", 8)
                return
            if nm == "e1-sine4096":
                print("acc4096で無反応。切り分け: sub40/sub03の受信を確認します。")
                if not self._check_subs_ready():
                    self.ui.red("Sub40(en=1)/Sub03(0x30-0x33)が揃っていません。報告して終了します。")
                    self.state.set("aborted", "e1-no-sub")
                    return
                print("sub受信あり。次はsine-acc16384で試します。")
                continue
            # 両方無反応
            self.ui.red("両方無反応。-Force 1での再ビルドを提案します。")
            if self.ui.confirm("Force=1で再ビルドして試しますか"):
                print("手順: .\\tools\\phase0_build.ps1 -Mode 0 -Force 1 -Name sine-force")
                print("ビルド後に手動で書き込み、反応を確認してください。")
                ans2 = self.ui.choose("Force=1での結果", ("反応あり", "なし"))
                if ans2 == "なし":
                    self.state.set("aborted", "e1-no-response")
                    return
                self.state.set("s2_done", True)
                self.state.set("prev_interval", 8)
                return
            self.state.set("aborted", "e1-no-response")
            return

    # -- S3 --
    def s3_e4(self) -> None:
        if self.state.get("s3_done"):
            print("S3 済み。")
            return
        print("=== S3 E4 軸符号 (6run) ===")
        print(WARN_CAL)
        obs: dict[int, tuple[str, str]] = {}
        done = {r["run"]: r for r in self.runs if r.get("run", "").startswith("e4ax")}
        for spec in L.e4_specs():
            if spec.name in done:
                r = done[spec.name]
                obs.setdefault(spec.expect.axis, ("", ""))
                continue
            self._start_log(spec.name)
            if isinstance(self.link, SimLink):
                # yaw=2想定: 軸2はp=右パン/m=左パン、他はチルト/ロール。
                simdir = {0: ("上チルト", "下チルト"), 1: ("時計回りロール", "反時計回りロール"),
                          2: ("右パン", "左パン")}[spec.expect.axis]
                want = simdir[0] if spec.expect.g > 0 else simdir[1]
                self.link.feed([
                    f"IMUD sub40 arg=01 en=1", "IMUD sub03 mode=30",
                    f"IMUD step#1 t0=1000 axis={spec.expect.axis} g={spec.expect.g} "
                    f"T=4000 fill=7 acc=0,0,4096",
                    f"IMUD step#1 end N_actual=500 T=4000 g={spec.expect.g} "
                    f"axis={spec.expect.axis} fill=7",
                ])
                b = L.Build(mode=1, axis=spec.expect.axis, g=spec.expect.g, t_ms=4000,
                            fill=7, acc=(0, 0, 4096), force=0, interval=8,
                            git=self.state.get("fw_commit", "68531ce"), dirty="0")
                seq: int | None = 1
                end: L.StepEnd | None = None
                for _, ev in self._collect(0.5):
                    if isinstance(ev, L.StepEnd):
                        end = ev
                d = self.ui.choose(f"{spec.name}: 回転方向 (想定: {want})", L.DIRECTIONS)
            else:
                if not self._check_subs_ready():
                    print("Sub40/Sub03が未受信です。")
                    if not self.ui.confirm("続行しますか"):
                        return
                b = self._flash(spec, self.state.get("prev_interval"))
                if b is None:
                    return
                self._collect(2.0)
                print("stepを送信します。")
                self.link.send_step()
                # StepStartを待つ (送信禁止期間の開始)。
                seq = None
                end = time.time() + 5.0
                while time.time() < end and seq is None:
                    for _, ev in self.link.drain():
                        if isinstance(ev, L.StepStart):
                            seq = ev.seq
                            print(f"STEP区間開始 (seq={seq})。終了まで送信しません。")
                    time.sleep(0.2)
                if seq is None:
                    self.ui.red("StepStartが来ませんでした。")
                    return
                end = self._wait_step_end(spec.name, seq, 15.0)
                if end is None:
                    self.ui.red("StepEndが来ませんでした。")
                    return
                print(f"N_actual={end.n_actual}")
                d = self.ui.choose(f"{spec.name}: 回転方向", L.DIRECTIONS)
            self._record_run({"run": spec.name, "build": str(b),
                              "n_actual": end.n_actual if end else None,
                              "dir": d, "subs": self.subs.get(spec.name, [])})
            self.state.set("prev_interval", 8)
        # 判定
        for r in self.runs:
            if r.get("run", "").startswith("e4ax"):
                ax = int(r["run"][4])
                pm = r["run"][5]
                p, m = obs.get(ax, ("", ""))
                if pm == "p":
                    p = r["dir"]
                else:
                    m = r["dir"]
                obs[ax] = (p, m)
        res = L.judge_e4(obs)
        for w in res.warnings:
            print(f"警告: {w}")
        if res.yaw_axis is None:
            self.ui.red("yaw軸が決まりません。報告して停止します。")
            self.state.set("aborted", "e4-no-yaw")
            return
        print(f"yaw軸={res.yaw_axis} (正方向: {res.yaw_positive})")
        self.state.set("yaw_axis", res.yaw_axis)
        self.state.set("yaw_positive", res.yaw_positive)
        self.state.set("s3_done", True)

    # -- S4 --
    def s4_r(self) -> None:
        if self.state.get("s4_done"):
            print("S4 済み。")
            return
        print("=== S4 R 基準比 (純正ProCon) ===")
        print(WARN_CAL)
        rs = []
        for label, secs, beep_every in (("約4秒で2周", 4.0, 0.5), ("約8秒で2周", 8.0, 1.0)):
            print(f"{label}: ビープ1回ごとに90°回します。")
            if not isinstance(self.link, SimLink):
                try:
                    import winsound

                    n = int(secs / beep_every)
                    for _ in range(n):
                        winsound.Beep(880, 120)
                        time.sleep(max(0.0, beep_every - 0.12))
                except Exception as e:
                    print(f"ビープなしで進めます ({e})。")
                    time.sleep(1.0)
            th = self._ask_angle(f"{label}のθ_game")
            r = L.ratio_r(th)
            print(f"R = {r:.4f}")
            rs.append(r)
        adopted, differ = L.pick_r(rs[0], rs[1])
        if differ:
            print("2回の差が5%超。8秒側を採用します。")
        self.state.set("r4", rs[0])
        self.state.set("r8", rs[1])
        self.state.set("r_adopted", adopted)
        self.state.set("r_differ", differ)
        self.state.set("s4_done", True)

    # -- ビルド連携 --
    def _build_uf2(self, name: str, **kw: int) -> bool:
        if isinstance(self.link, SimLink):
            print(f"(simulate: {name} をビルドしたことにする {kw})")
            return True
        cmd = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
               "-File", os.path.join(self.root, "tools", "phase0_build.ps1")]
        for k, v in kw.items():
            cmd += [f"-{k}", str(v)]
        cmd += ["-Name", name]
        print("ビルド: " + " ".join(cmd))
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
        if r.returncode != 0:
            self.ui.red(f"ビルド失敗:\n{(r.stderr or '')[-800:]}")
            return False
        print("ビルドOK。")
        return True

    def _step_run(self, spec: L.RunSpec, what: str) -> dict | None:
        """step送信→StepEnd→θ入力の1run。失敗はNone。"""
        self._start_log(spec.name)
        if isinstance(self.link, SimLink):
            n = 500 if spec.expect.interval == 8 else 250
            self.link.feed([
                "IMUD sub40 arg=01 en=1", "IMUD sub03 mode=30",
                f"IMUD step#1 t0=1000 axis={spec.expect.axis} g={spec.expect.g} "
                f"T={spec.expect.t_ms} fill={spec.expect.fill} acc=0,0,4096",
                f"IMUD step#1 end N_actual={n} T={spec.expect.t_ms} g={spec.expect.g} "
                f"axis={spec.expect.axis} fill={spec.expect.fill}",
            ])
            b = L.Build(mode=spec.expect.mode, axis=spec.expect.axis, g=spec.expect.g,
                         t_ms=spec.expect.t_ms, fill=spec.expect.fill, acc=spec.expect.acc,
                         force=spec.expect.force, interval=spec.expect.interval,
                         git=self.state.get("fw_commit", "68531ce"), dirty="0")
        else:
            b = self._flash(spec, self.state.get("prev_interval"))
            if b is None:
                return None
            self._collect(2.0)
            print("stepを送信します。")
            self.link.send_step()
            seq = None
            end = time.time() + 5.0
            while time.time() < end and seq is None:
                for _, ev in self.link.drain():
                    if isinstance(ev, L.StepStart):
                        seq = ev.seq
                time.sleep(0.2)
            if seq is None:
                self.ui.red("StepStartが来ませんでした。")
                return None
            se = self._wait_step_end(spec.name, seq, 20.0)
            if se is None:
                self.ui.red("StepEndが来ませんでした。")
                return None
            n = se.n_actual
            print(f"N_actual={n}")
        th = self._ask_angle(f"{what}のθ_game")
        rec = {"run": spec.name, "build": str(b), "n_actual": n, "theta": th,
               "subs": self.subs.get(spec.name, [])}
        self._record_run(rec)
        self.state.set("prev_interval", spec.expect.interval)
        return rec

    # -- S5 --
    def s5_e2(self) -> None:
        if self.state.get("s5_done"):
            print("S5 済み。")
            return
        print("=== S5 E2 定数 ===")
        print(WARN_CAL)
        yaw = self.state.get("yaw_axis", 2)
        recs = {}
        for nm, g in (("e2yaw90", 1475), ("e2yaw360", 5899)):
            if any(r.get("run") == nm for r in self.runs):
                recs[nm] = next(r for r in self.runs if r.get("run") == nm)
                continue
            if not self._build_uf2(nm, Axis=yaw, GRaw=g):
                return
            spec = L.RunSpec(name=nm, uf2=f"switch-bcon-imu-dummy-{nm}.uf2",
                              expect=L.Expect(mode=1, axis=yaw, g=g, t_ms=4000, fill=7,
                                              acc=(0, 0, 4096), force=0, interval=8))
            rec = self._step_run(spec, nm)
            if rec is None:
                return
            recs[nm] = rec
        r = self.state.get("r_adopted")
        c90 = L.c_phys(recs["e2yaw90"]["theta"], r, recs["e2yaw90"]["n_actual"], 1475)
        c360 = L.c_phys(recs["e2yaw360"]["theta"], r, recs["e2yaw360"]["n_actual"], 5899)
        print(f"C_phys(90)={c90:.6f} C_phys(360)={c360:.6f} [度/(raw・report)]")
        if L.check_linearity(recs["e2yaw90"]["theta"], recs["e2yaw360"]["theta"]):
            adopted = (c90 + c360) / 2.0
            print(f"線形。採用C={(c90 + c360) / 2.0:.6f}")
            linear = True
        else:
            adopted = c90
            print("非線形。90dps側を採用します。")
            linear = False
        print(f"最も近い仮説: {L.nearest_hypothesis(recs['e2yaw90']['theta'], recs['e2yaw360']['theta'])}")
        self.state.set("c90", c90)
        self.state.set("c360", c360)
        self.state.set("c_adopted", adopted)
        self.state.set("linear", linear)
        self.state.set("s5_done", True)

    # -- S6 --
    def s6_e3a(self) -> None:
        if self.state.get("s6_done"):
            print("S6 済み。")
            return
        print("=== S6 E3a 積分基準 ===")
        print(WARN_CAL)
        yaw = self.state.get("yaw_axis", 2)
        rec8 = next((r for r in self.runs if r.get("run") == "e2yaw360"), None)
        if rec8 is None:
            self.ui.red("e2yaw360がありません。S5を先に完了してください。")
            return
        if not any(r.get("run") == "e3a16" for r in self.runs):
            if not self._build_uf2("e3a16", Axis=yaw, GRaw=5899, IntervalMs=16):
                return
            spec = L.RunSpec(name="e3a16", uf2="switch-bcon-imu-dummy-e3a16.uf2",
                              expect=L.Expect(mode=1, axis=yaw, g=5899, t_ms=4000, fill=7,
                                              acc=(0, 0, 4096), force=0, interval=16))
            rec16 = self._step_run(spec, "e3a16")
            if rec16 is None:
                if self.ui.confirm("16msで反応がありませんか(不受理として記録)"):
                    self.state.set("e3a", "16msは不受理(判定持越し)")
                    self.state.set("s6_done", True)
                return
        else:
            rec16 = next(r for r in self.runs if r.get("run") == "e3a16")
        verdict = L.judge_e3a(rec8["theta"], rec8["n_actual"], rec16["theta"], rec16["n_actual"])
        print(f"E3a判定: {verdict}")
        self.state.set("e3a", verdict)
        self.state.set("s6_done", True)

    # -- S7 --
    def s7_e3b(self) -> None:
        if self.state.get("s7_done"):
            print("S7 済み。")
            return
        print("=== S7 E3b slot使用 ===")
        print(WARN_CAL)
        yaw = self.state.get("yaw_axis", 2)
        rec_all = next((r for r in self.runs if r.get("run") == "e2yaw360"), None)
        if rec_all is None:
            self.ui.red("e2yaw360がありません。")
            return
        for nm, fill in (("e3b-first-360", 1), ("e3b-mid-360", 2), ("e3b-last-360", 4)):
            if any(r.get("run") == nm for r in self.runs):
                continue
            if not self._build_uf2(nm, Axis=yaw, GRaw=5899, Fill=fill):
                return
            spec = L.RunSpec(name=nm, uf2=f"switch-bcon-imu-dummy-{nm}.uf2",
                              expect=L.Expect(mode=1, axis=yaw, g=5899, t_ms=4000, fill=fill,
                                              acc=(0, 0, 4096), force=0, interval=8))
            if self._step_run(spec, nm) is None:
                return
        base = rec_all["theta"]
        out = {}
        for nm in ("e3b-first-360", "e3b-mid-360", "e3b-last-360"):
            rec = next(r for r in self.runs if r.get("run") == nm)
            if rec["theta"] == 0:
                verdict = "そのslotは使われていない(動かない)"
            else:
                verdict = L.judge_e3b_ratio(base / rec["theta"])
            print(f"{nm}: {verdict}")
            out[nm] = verdict
        self.state.set("e3b", out)
        self.state.set("s7_done", True)

    # -- S8 --
    def s8_e5(self) -> None:
        if self.state.get("s8_done"):
            print("S8 済み。")
            return
        print("=== S8 E5 重力受理 ===")
        print(WARN_CAL)
        print("STEPは使いません(step送信なし)。")
        yaw = self.state.get("yaw_axis", 2)
        game = self.ui.ask("加速度で絶対的な傾きを読むゲーム/モード")
        if not self.ui.confirm(f"純正ProConで傾けて「{game}」がそのとおりに反応することを確認しましたか"):
            print("確認してから進んでください。")
            return
        self.state.set("e5_game", game)
        out = {}
        for m in (4096, 8192, 16384):
            nm = f"e5-{m}"
            if any(r.get("run") == nm for r in self.runs):
                continue
            v = L.e5_vector(yaw, m)
            if not self._build_uf2(nm, Axis=yaw, AccX=v[0], AccY=v[1], AccZ=v[2]):
                return
            spec = next(s for s in L.e5_specs(yaw) if s.name == nm)
            self._start_log(spec.name)
            if isinstance(self.link, SimLink):
                b = L.Build(mode=1, axis=yaw, g=1475, t_ms=4000, fill=7, acc=v,
                            force=0, interval=8, git=self.state.get("fw_commit", "68531ce"), dirty="0")
            else:
                b = self._flash(spec, self.state.get("prev_interval"))
                if b is None:
                    return
                self._collect(3.0)
            ans = self.ui.choose(f"{nm} (acc={v}): 約30°傾いた状態として反応しますか",
                                 ("反応あり", "なし", "不明"))
            self._record_run({"run": spec.name, "build": str(b), "e5": ans,
                              "subs": self.subs.get(spec.name, [])})
            out[nm] = ans
            self.state.set("prev_interval", 8)
        print(f"E5結果: {out}")
        self.state.set("e5", out)
        self.state.set("s8_done", True)

    # -- 出力 --
    def write_outputs(self) -> None:
        game = self.state.get("game", {})
        results = {
            "game": game,
            "runs": self.runs,
            "rate": next((r.get("rate") for r in self.runs if "rate" in r), None),
            "yaw_axis": self.state.get("yaw_axis"),
            "yaw_positive": self.state.get("yaw_positive"),
            "r4": self.state.get("r4"), "r8": self.state.get("r8"),
            "r_adopted": self.state.get("r_adopted"), "r_differ": self.state.get("r_differ"),
            "c90": self.state.get("c90"), "c360": self.state.get("c360"),
            "c_adopted": self.state.get("c_adopted"), "linear": self.state.get("linear"),
            "e3a": self.state.get("e3a"), "e3b": self.state.get("e3b"),
            "e5": self.state.get("e5"), "e5_game": self.state.get("e5_game"),
            "aborted": self.state.get("aborted"),
        }
        with open(os.path.join(self.logdir, "phase0_results.json"), "w", encoding="utf-8") as fp:
            json.dump(results, fp, ensure_ascii=False, indent=2)
        with open(os.path.join(self.logdir, "phase0_results.csv"), "w", encoding="utf-8", newline="") as fp:
            w = csv.writer(fp)
            w.writerow(["run", "git", "ACC_Z", "構成", "N_actual", "θ", "備考"])
            for r in self.runs:
                b = r.get("build", "")
                git = dirty = accz = ""
                m = L.parse_line(b) if isinstance(b, str) else None
                if isinstance(m, L.Build):
                    git = m.git
                    accz = m.acc[2]
                note = ",".join(k for k in ("e1", "dir", "e5") if k in r)
                if r.get("run") == "s1-rate" and "rate" in r:
                    note = f"avg={r['rate']['avg']:.1f}"
                w.writerow([r.get("run"), git, accz, b, r.get("n_actual", ""),
                            r.get("theta", ""), note])
        with open(os.path.join(self.logdir, "phase0_summary.md"), "w", encoding="utf-8") as fp:
            fp.write("# Phase 0 実験要約\n\n")
            fp.write(f"- ゲーム: {game.get('name')} {game.get('version')} 感度={game.get('sens')}\n")
            rate = results["rate"]
            if rate:
                fp.write(f"- 周期実測: in30/s avg={rate['avg']:.1f} min={rate['min']:.1f} "
                         f"max={rate['max']:.1f} (n={rate['n']})\n")
            fp.write(f"- yaw軸: {results['yaw_axis']} (正方向: {results['yaw_positive']})\n")
            fp.write(f"- R: 4秒={results['r4']} 8秒={results['r8']} "
                     f"採用={results['r_adopted']} (差あり={results['r_differ']})\n")
            fp.write(f"- C_phys: 90={results['c90']} 360={results['c360']} "
                     f"採用={results['c_adopted']} (線形={results['linear']})\n")
            fp.write(f"- E3a: {results['e3a']}\n")
            fp.write(f"- E3b: {results['e3b']}\n")
            fp.write(f"- E5 ({results['e5_game']}): {results['e5']}\n")
            fp.write(f"- SPI 0x6020: {L.SPI_6020_HEX}\n")
            if results["aborted"]:
                fp.write(f"- 中断: {results['aborted']}\n")
            fp.write("\n手で判断した箇所・判定不能は上記の値とrunsを見ること。\n")
        print("出力: phase0_results.json / .csv / phase0_summary.md")


def choose_port(ui: UI, state: State, sim: bool) -> str:
    if sim:
        return "SIM"
    try:
        from serial.tools import list_ports
    except ImportError:
        return ui.ask("COMポート (例: COM3)")
    ports = [(p.device, p.description or "") for p in list_ports.comports()]
    if not ports:
        return ui.ask("COMポート (一覧が空)")
    default = state.get("port", "")
    print("COMポート:")
    for i, (dev, desc) in enumerate(ports, 1):
        mark = " (前回)" if dev == default else ""
        print(f"  {i}. {dev} {desc}{mark}")
    while True:
        ans = ui.ask("番号またはポート名", default)
        try:
            idx = int(ans) - 1
            if 0 <= idx < len(ports):
                return ports[idx][0]
        except ValueError:
            pass
        if ans:
            return ans
        print("選んでください。")


def main() -> int:
    sim = "--simulate" in sys.argv
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    state = State(os.path.join(root, "log", "phase0_state.json"))
    ui = UI(sim=sim)
    if sim:
        # 全ステップを通す回答列。
        ui.answers = [
            "TestGame", "1.0", "中", "f05ecee",  # S0
            "1",  # S2 E1 反応あり
            # S3 E4 6run (軸2がyaw: p=右パン/m=左パン)
            "3", "4",  # ax0: 上/下
            "5", "6",  # ax1: 時計/反時計
            "2", "1",  # ax2: 右/左
            "1440", "1440",  # S4 R (R=2.0)
            "1440", "5760",  # S5 E2 (線形4倍)
            "2880",  # S6 E3a 16ms (simulateはN=500固定のためslot基準相当: θ半減・θ/N同じ)
            "1920", "1920", "1920",  # S7 E3b (比3)
            "傾きゲーム", "y", "1", "1", "1",  # S8 E5
        ]
        link: SerialLink = SimLink([])
    else:
        port = choose_port(ui, state, False)
        state.set("port", port)
        link = SerialLink()
        try:
            link.open(port)
        except Exception as e:
            print(f"開けません: {e}")
            return 1
    run = Runner(root, ui, link, state)
    try:
        run.s0_setup()
        run.s1_rate()
        if not state.get("s1_done"):
            print("S1が未完了のため停止します。再起動するとS1から再開します。")
            return 0
        run.s2_e1()
        if state.get("aborted"):
            run.write_outputs()
            return 0
        if not state.get("s2_done"):
            print("S2が未完了のため停止します。")
            return 0
        run.s3_e4()
        if state.get("aborted"):
            run.write_outputs()
            return 0
        if not state.get("s3_done"):
            print("S3が未完了のため停止します。")
            return 0
        run.s4_r()
        run.s5_e2()
        run.s6_e3a()
        run.s7_e3b()
        run.s8_e5()
        run.write_outputs()
    finally:
        link.close()
    print("完了。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
