#!/usr/bin/env python3
"""把某版官方协议 PDF（pdftotext -layout 的产物）的 §2.2 与本项目 proto 做逐字段比对。

用法:
    python3 tools/diff_protocol_versions.py <pdf文本.txt> [proto目录]
默认 proto 目录为 ../proto。
"""
import os
import re
import sys


# 官方文档把辅助结构写成 `repeated uint32 RadarSingleRobotInfo;`（缺字段号、类型写错），
# 本项目把它实现为 RadarInfoToClient 的嵌套 message。比对时按语义归一化。
AUX_TYPES = {"RadarSingleRobotInfo"}


def parse_messages(text):
    """从（已清理页眉页脚的）文本里解析 message 块。"""
    msgs, cur = {}, None
    for ln in text.split("\n"):
        s = ln.strip().replace("；", ";").replace("，", ",")
        m = re.match(r"^message\s+([A-Za-z0-9_]+)\s*\{", s)
        if m:
            cur = m.group(1)
            msgs[cur] = []
            continue
        if cur is not None:
            if s == "}":
                cur = None
                continue
            f = re.match(r"^(?:(optional|required|repeated)\s+)?(\w+)\s+(\w+)\s*(?:=\s*(\d+))?\s*(?:\[([^\]]*)\])?\s*;", s)
            if f:
                lbl, ftype, fname, fnum = f.group(1) or "", f.group(2), f.group(3), f.group(4)
                if fname in AUX_TYPES or ftype in AUX_TYPES:   # 官方笔误归一化
                    if fname in AUX_TYPES:
                        ftype, fname = fname, "radar_single_robot_info"
                    fnum, lbl = "1", "repeated"
                msgs[cur].append({
                    "label": lbl,
                    "repeated": lbl == "repeated",
                    "type": ftype,
                    "name": fname,
                    "num": int(fnum) if fnum else 0,
                })
    return msgs


def clean(text):
    text = re.sub(r"^.*©\s*\d{4}\s*大疆\s*版权所有.*$", "", text, flags=re.M)
    text = re.sub(r"^\s*\d{1,3}\s*$", "", text, flags=re.M)
    return text


def extract_pdf_section(path):
    raw = open(path, encoding="utf-8").read()
    start = raw.rindex("2.2 详细协议定义")
    m = re.search(r"附录一[：:]", raw[start:])
    end = start + m.start() if m else len(raw)
    return clean(raw[start:end])


def load_proto_dir(d):
    text = ""
    for fn in sorted(os.listdir(d)):
        if fn.endswith(".proto") and "scenario" not in fn:
            body = open(os.path.join(d, fn), encoding="utf-8").read()
            body = re.sub(r"//.*", "", body)
            text += body + "\n"
    return parse_messages(text)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    here = os.path.dirname(os.path.abspath(__file__))
    proto_dir = sys.argv[2] if len(sys.argv) > 2 else os.path.join(here, "..", "proto")

    spec = parse_messages(extract_pdf_section(sys.argv[1]))
    repo = load_proto_dir(proto_dir)

    print(f"官方 PDF 消息数: {len(spec)}    本项目 proto 消息数: {len(repo)}")

    spec = {k: v for k, v in spec.items() if k not in AUX_TYPES}
    repo = {k: v for k, v in repo.items() if k not in AUX_TYPES}
    missing = [m for m in spec if m not in repo]
    extra = [m for m in repo if m not in spec]
    print(f"\n【A. 官方有、本项目缺失（新增或改名）】{len(missing)} 条")
    for m in missing:
        print("   •", m)
    print(f"\n【B. 本项目有、官方已无（旧名/已删）】{len(extra)} 条")
    for m in extra:
        print("   •", m)

    print("\n【C. 同名字段的差异】")
    clean_msgs = 0
    for m in spec:
        if m not in repo:
            continue
        s, r = spec[m], repo[m]
        key = lambda fs: [(f["type"], f["name"], f["num"], f["repeated"]) for f in fs]
        if key(s) == key(r):
            clean_msgs += 1
            continue
        print(f"\n   ✗ {m}")
        rn = {f["name"]: f for f in r}
        sn = {f["name"]: f for f in s}
        rnum = {f["num"]: f for f in r}
        for f in s:
            if f["name"] not in rn:
                c = rnum.get(f["num"])
                tag = f"   ⟵ 编号#{f['num']}已被本项目字段 {c['name']} 占用" if c else ""
                print(f"      [缺] #{f['num']:>2} {f['type']:<12}{f['name']}{tag}")
            else:
                g = rn[f["name"]]
                if (g["type"], g["num"]) != (f["type"], f["num"]):
                    print(f"      [异] {f['name']}: 官方={f['type']} #{f['num']} | 本项目={g['type']} #{g['num']}")
        for f in r:
            if f["name"] not in sn:
                print(f"      [多] #{f['num']:>2} {f['type']:<12}{f['name']}  (官方已无)")
    print(f"\n完全一致的消息: {clean_msgs} 条")

    # 顺带核对 UDP 图传参数
    raw = open(sys.argv[1], encoding="utf-8").read()
    print("\n【D. UDP 图传参数（官方原文）】")
    for kw in ["大端传输", "1400", "1392"]:
        n = raw.count(kw)
        print(f"   '{kw}' 出现 {n} 次")
    return 0


if __name__ == "__main__":
    sys.exit(main())
