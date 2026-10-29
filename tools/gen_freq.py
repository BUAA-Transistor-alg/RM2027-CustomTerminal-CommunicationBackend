#!/usr/bin/env python3
"""官方 V1.0.0「表 2-1 指令概览」的发送频率表（文档原文摘录）+ 自动交叉校验。

PDF 里频率列的「触发式发送，发送频率最高为 10Hz」会被 pdftotext 折成 3 行并排到
指令名之前，纯文本解析不可靠，因此这里以人工核对过的字面表为准，
再用脚本在 PDF 文本中逐条回查，防止抄错。

输出 docs/frequencies.json:
    { "GameStatus": {"hz":5, "max_hz":5, "triggered":false, "raw":"5Hz"}, ... }
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SPEC = os.path.join(ROOT, "docs", "spec-v1.0.0-fields.json")
OUT = os.path.join(ROOT, "docs", "frequencies.json")

# 文档 表 2-1 原文（Hz）；0 表示「触发式发送」
FIXED = {
    "KeyboardMouseControl": 75, "CustomControl": 75,
    "GameStatus": 5, "GlobalUnitStatus": 1, "GlobalLogisticsStatus": 1,
    "GlobalSpecialMechanism": 1, "RobotInjuryStat": 1, "RobotRespawnStatus": 1,
    "RobotStaticStatus": 1, "RobotDynamicStatus": 10, "RobotModuleStatus": 1,
    "RobotPosition": 1, "Buff": 1, "CustomByteBlock": 50,
    "TechCoreMotionStateSync": 1, "RobotPerformanceSelectionSync": 1,
    "RuneStatusSync": 1, "SentryStatusSync": 1,
    "DartSelectTargetStatusSync": 1,
}
# 「触发式发送，发送频率最高为 10Hz」
TRIGGERED_MAX10 = {
    "AssemblyCommand", "RobotPerformanceSelectionCommand", "CommonCommand",
    "RuneActivateCommand", "DartCommand", "SentryCtrlCommand",
}
# 纯「触发式发送」
TRIGGERED = {
    "Event", "PenaltyInfo", "RobotPathPlanInfo", "MapClickInfo", "MapClickCmd",
    "RadarInfoToClient", "SentryCtrlResult",
}


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    raw = open(sys.argv[1], encoding="utf-8").read()
    spec = json.load(open(SPEC, encoding="utf-8"))
    names = list(spec.keys())

    start = raw.rindex("2.1 指令概览")
    end = raw.index("2.2 详细协议定义", start)
    text = re.sub(r"^.*©.*版权所有.*$", "", raw[start:end], flags=re.M)
    lines = [l.strip() for l in text.split("\n")]

    def find_row(name):
        for i, l in enumerate(lines):
            if l.startswith(name) and (len(l) == len(name) or l[len(name)].isspace()):
                return i
        return -1

    result, problems = {}, []
    for n in names:
        if n in FIXED:
            hz = float(FIXED[n]); max_hz = hz; trig = False; rawtxt = f"{hz:g}Hz"
        elif n in TRIGGERED_MAX10:
            hz = 0.0; max_hz = 10.0; trig = True; rawtxt = "触发式发送，发送频率最高为10Hz"
        elif n in TRIGGERED:
            hz = 0.0; max_hz = 0.0; trig = True; rawtxt = "触发式发送"
        else:
            problems.append(f"{n}: 字面表里没有")
            continue
        result[n] = {"hz": hz, "max_hz": max_hz, "triggered": trig, "raw": rawtxt}

        # ---- 回查 PDF，防止抄错 ----
        i = find_row(n)
        if i < 0:
            problems.append(f"{n}: 在表 2-1 中定位不到该行")
            continue
        window = re.sub(r"\s+", " ", " ".join(lines[max(0, i - 6): i + 7]))
        if trig:
            if "触发式" not in window and "最高为" not in window:
                problems.append(f"{n}: 标为触发式，但 PDF 邻近行未见「触发式/最高为」 -> {window[:70]}")
            if max_hz > 0:
                flat = window.replace(" ", "")
                # 2027 版表格列更窄，频率列会与「指令用途」列交错换行，
                # 因此只校验「最高」与「<n>Hz」同窗口出现，不要求字面连续。
                if "最高" not in flat or f"{max_hz:g}Hz" not in flat:
                    problems.append(f"{n}: 期望「最高为{max_hz:g}Hz」，PDF 邻近行未见 -> {window[:80]}")
        else:
            if f"{hz:g}Hz" not in window.replace(" ", ""):
                problems.append(f"{n}: 期望「{hz:g}Hz」，PDF 邻近行未见 -> {window[:80]}")

    json.dump(result, open(OUT, "w", encoding="utf-8"), ensure_ascii=False, indent=1,
              sort_keys=True)

    print(f"指令 {len(names)} 条：固定频率 {len(FIXED)}，触发式(限速10Hz) {len(TRIGGERED_MAX10)}，"
          f"纯触发式 {len(TRIGGERED)}  ->  {OUT}")
    print()
    for n in names:
        r = result.get(n)
        if r:
            print(f"  {n:<38} {r['raw']}")
    if problems:
        print("\n❌ PDF 交叉校验发现问题:")
        for p in problems:
            print("   -", p)
        return 1
    print("\n✅ 全部与 PDF 表 2-1 交叉校验通过")
    return 0


if __name__ == "__main__":
    sys.exit(main())
