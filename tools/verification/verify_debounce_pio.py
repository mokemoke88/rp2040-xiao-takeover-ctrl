# SPDX-License-Identifier: MIT
# src/debounce.pio(pioasm が生成した build/debounce.pio.h の命令語)を1サイクル単位で模擬実行し、
# チャタリング除去の時間(push までのサイクル数)、ノイズ除去、同着のまとめ方、起動時の最初の push を検算する。
# 実機でしか確認できない PIO の机上検証を再現するためのスクリプト。
#
# 使い方: python tools/verification/verify_debounce_pio.py [build/debounce.pio.h]
# 対応する命令は debounce.pio が使うもの(jmp / in pins / mov / push)だけで、それ以外は例外にする。
# 期待するサイクル数は、生成ヘッダの debounce_CLOCK_HZ / debounce_REREAD_DELAY と、
# 命令列に含まれる `in` の数(= 検出 1 回 + 読み直し N 回)から計算する。
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BUTTON_INPUT_C = os.path.join(HERE, "..", "..", "src", "button_input.c")


def read_define(text, name, path):
    m = re.search(r"#define " + name + r" (0x[0-9a-fA-F]+|\d+)u?\b", text)
    if not m:
        raise ValueError(f"{name} not found in {path}")
    return int(m.group(1), 0)


def load_program(path):
    text = open(path, encoding="utf-8").read()
    words = [int(w, 16) for w in re.findall(r"^\s+0x([0-9a-f]{4}),", text, re.M)]
    return {
        "words": words,
        "wrap_target": read_define(text, "debounce_wrap_target", path),
        "wrap": read_define(text, "debounce_wrap", path),
        "start": read_define(text, "debounce_offset_start", path),
        "clock_hz": read_define(text, "debounce_CLOCK_HZ", path),
        "reread_delay": read_define(text, "debounce_REREAD_DELAY", path),
    }


def load_initial_x():
    text = open(BUTTON_INPUT_C, encoding="utf-8").read()
    return read_define(text, "BUTTON_X_INITIAL_UNKNOWN", BUTTON_INPUT_C)


def simulate(prog, initial_x, pins_at, cycles):
    """pins_at(cycle) -> 2ビットの生の値。push した (cycle, value) の一覧を返す。"""
    words, wrap_target, wrap = prog["words"], prog["wrap_target"], prog["wrap"]
    x, y, isr, osr = initial_x, 0, 0, 0
    pc, cycle, pushes = prog["start"], 0, []
    while cycle < cycles:
        w = words[pc]
        op = w >> 13
        delay = (w >> 8) & 0x1F  # side-set を使わないプログラムなので 5 ビットすべてが遅延
        next_pc = wrap_target if pc == wrap else pc + 1
        if op == 0b000:  # jmp
            cond, addr = (w >> 5) & 7, w & 0x1F
            if cond == 0b000:
                next_pc = addr
            elif cond == 0b101:
                if x != y:
                    next_pc = addr
            else:
                raise ValueError(f"unsupported jmp condition {cond} in {w:#06x}")
        elif op == 0b010:  # in
            src, count = (w >> 5) & 7, w & 0x1F
            if src != 0b000 or count != 2:
                raise ValueError(f"only `in pins, 2` is supported: {w:#06x}")
            isr = ((isr << 2) | (pins_at(cycle) & 0b11)) & 0xFFFFFFFF
        elif op == 0b101:  # mov
            dst, mov_op, src = (w >> 5) & 7, (w >> 3) & 3, w & 7
            if mov_op != 0:
                raise ValueError(f"mov with invert/bit-reverse is not supported: {w:#06x}")
            sources = {0b001: x, 0b010: y, 0b011: 0, 0b110: isr, 0b111: osr}
            if src not in sources:
                raise ValueError(f"unsupported mov source {src} in {w:#06x}")
            val = sources[src]
            if dst == 0b001:
                x = val
            elif dst == 0b010:
                y = val
            elif dst == 0b110:
                isr = val
            elif dst == 0b111:
                osr = val
            else:
                raise ValueError(f"unsupported mov destination {dst} in {w:#06x}")
        elif op == 0b100:  # push / pull
            is_pull, if_full, block = (w >> 7) & 1, (w >> 6) & 1, (w >> 5) & 1
            if is_pull or if_full or not block or (w & 0x1F):
                raise ValueError(f"only `push block` is supported: {w:#06x}")
            pushes.append((cycle, isr))  # RX FIFO は CPU が読むので満杯にならない前提
            isr = 0
        else:
            raise ValueError(f"unsupported instruction {w:#06x}")
        pc = next_pc
        cycle += 1 + delay
    return pushes


def wave(*changes, initial=0b11):
    """(cycle, value) の列から pins_at 関数を作る。"""

    def pins_at(c):
        v = initial
        for t, val in changes:
            if c >= t:
                v = val
        return v

    return pins_at


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "build/debounce.pio.h"
    prog = load_program(path)
    initial_x = load_initial_x()
    words = prog["words"]
    ms = prog["clock_hz"] / 1000  # 1 ms あたりのサイクル数
    d = prog["reread_delay"]
    n_reread = sum(1 for w in words if w >> 13 == 0b010) - 1  # `in` の数 - 検出の 1 回

    # 期待値の計算(debounce.pio 冒頭コメントの式)
    first_reread = 6 + d  # 検出の in の後: mov y, jmp, mov osr, mov x, mov isr [d]
    interval = 4 + d  # in の後: mov y, jmp, mov isr [d]
    last_reread = first_reread + (n_reread - 1) * interval
    push_delay = last_reread + 3  # in の後: mov y, jmp, push
    boot_push = 1 + push_delay  # 最初の検出の in はサイクル 1(アドレス 1 の次)
    first_idle_in = boot_push + 3  # push の後: jmp start, mov isr, in
    print(f"instructions={len(words)} clock={prog['clock_hz']}Hz REREAD_DELAY={d} rereads={n_reread} "
          f"initial X={initial_x}")
    print(f"expected: last reread {last_reread} cycles ({last_reread / ms} ms), push {push_delay} cycles "
          f"({push_delay / ms} ms) after detection, boot push at {boot_push} ({boot_push / ms} ms)")

    ok = True

    def check(name, cond, detail):
        nonlocal ok
        ok &= bool(cond)
        print(f"{'PASS' if cond else 'FAIL'}: {name}: {detail}")

    check("instruction count <= 32", len(words) <= 32, len(words))
    check("initial X differs from any pin value", initial_x > 0b11, initial_x)

    def run(pins, n=2000):
        return simulate(prog, initial_x, pins, n)

    def events(p, boot_value=0b11):
        """起動時の push を確認して取り除き、残り(状態変化)を返す。"""
        assert p and p[0] == (boot_push, boot_value), f"boot push expected {(boot_push, boot_value)}, got {p[:1]}"
        return p[1:]

    # 1. 何も押していなくても、起動後の最初の push で「両方離し」が届く。それ以降は push しない
    p = run(wave())
    check("boot push (released) then idle", p == [(boot_push, 0b11)], p)

    # 2. 起動時から押されている -> 最初の push がその状態
    p = run(wave(initial=0b10))
    check("boot push (LB pressed)", p == [(boot_push, 0b10)], p)

    # 3. 起動直後にチャタリングしてから押下で安定 -> 最初の push は安定後の値
    changes = [(i * 3, 0b10 if i % 2 == 0 else 0b11) for i in range(20)] + [(60, 0b10)]
    p = run(wave(*changes))
    check("boot push after bouncing", len(p) == 1 and p[0][1] == 0b10 and p[0][0] >= 60 + push_delay, p)

    # 4. LB だけを押す(生の値 0b11 -> 0b01)。エッジから push までの遅れを監視ループの全位相で測る
    delays = []
    for edge in range(first_idle_in + 8, first_idle_in + 12):
        ev = events(run(wave((edge, 0b01))))
        assert len(ev) == 1 and ev[0][1] == 0b01, (edge, ev)
        delays.append(ev[0][0] - edge)
    check("LB press latency", min(delays) == push_delay and max(delays) == push_delay + 3,
          f"{min(delays)}..{max(delays)} cycles = {min(delays) / ms}..{max(delays) / ms} ms")

    # 5. 検出した読み取りから push まで push_delay サイクル
    detect = first_idle_in + 4 * 4  # アドレス 2 の in が実行されるサイクル(4 サイクル周期)
    ev = events(run(wave((detect, 0b01))))
    check("detect->push", ev and ev[0][0] - detect == push_delay, ev)

    # 6. 最後の読み直しの直前で戻るノイズは捨てる
    ev = events(run(wave((detect, 0b01), (detect + last_reread - 1, 0b11))))
    check(f"glitch {(last_reread - 1) / ms} ms rejected", ev == [], ev)

    # 7. 最後の読み直しの後に戻っても見えない = 標本化の限界
    ev = events(run(wave((detect, 0b01), (detect + last_reread + 1, 0b11), (detect + last_reread + 2, 0b01))))
    check("glitch between samples is not seen (sampling limit)", len(ev) == 1, ev)

    # 8. チャタリング: 0.75 ms ごとに反転した後に安定 -> 不一致で捨てる動作を経て、push は安定後の1回だけ
    start = first_idle_in + 8
    stable = start + 42
    changes = [(start + i * 3, 0b01 if i % 2 == 0 else 0b11) for i in range(14)] + [(stable, 0b01)]
    ev = events(run(wave(*changes)))
    check("bounce -> single push after settling", len(ev) == 1 and ev[0][1] == 0b01 and ev[0][0] >= stable + push_delay,
          ev)

    # 9. LB の 5 ms 後に RB(同じ除去窓の中) -> 両方押し 0b00 が1件だけ届く
    ev = events(run(wave((start, 0b01), (start + int(5 * ms), 0b00))))
    check("same window -> one event 0b00", [v for _, v in ev] == [0b00], ev)

    # 10. LB の 30 ms 後に RB -> 2件、発生順に届く
    ev = events(run(wave((start, 0b01), (start + int(30 * ms), 0b00))))
    check("separate -> two events in order", [v for _, v in ev] == [0b01, 0b00], ev)

    # 11. 解放も同じように除去される(押下 -> 100 ms 後に解放)
    ev = events(run(wave((start, 0b01), (start + int(100 * ms), 0b11))))
    check("press then release", [v for _, v in ev] == [0b01, 0b11], ev)

    # 12. 片方(RB)が除去時間より短い周期で暴れ続けると、もう片方(LB)の押下も確定しない(危険側ではない)。
    #     反転間隔は 2.5 ms(10 サイクル)。読み直し間隔(interval)の約数だと毎回同じ位相を読んでしまい
    #     「安定」と見えるので、そうならない値にしている
    toggle = 10
    lb_press = start + 4
    end = start + 400 * toggle

    def noisy(c):
        rb = 1 if c < start or ((c - start) // toggle) % 2 == 0 else 0
        lb = 0 if c >= lb_press else 1
        return (lb << 1) | rb

    ev = events(run(noisy, n=end))
    check(f"chattering RB ({toggle / ms} ms) blocks LB change while it lasts", ev == [], ev)

    print("OK" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
