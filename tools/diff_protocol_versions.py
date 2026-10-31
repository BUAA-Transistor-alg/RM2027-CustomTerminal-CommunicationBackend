#!/usr/bin/env python3
"""把官方 RM2027 通信协议 PDF 第 2.2 节的 message 代码块与本项目 proto 做「字面」逐字段比对。

用法::

    python3 tools/diff_protocol_versions.py <pdf文本.txt> [proto目录] [--strict]
    # 例：python3 tools/diff_protocol_versions.py p2027.txt proto

参数
----
<pdf文本.txt>
    pdftotext -layout -enc UTF-8 的产物（官方 PDF 全文）。
[proto目录]
    本项目的 proto 目录，默认 ../proto（相对本脚本）。
--strict
    严格模式：只要有「任何」字面不一致（含下方列出的已知偏离）就返回 1。
    默认模式下「只有已知偏离」返回 0。

比对范围与局限（重要）
----------------------
* 只比对第 2.2 节里的 message 代码块（含官方紧邻写出的辅助 message
  RadarSingleRobotInfo）。字段比对 key 为
  (类型, 字段名, 字段号, 是否 repeated, optional/repeated 标签, 是否 [packed])，
  即 optional / repeated 标签与 [packed] 选项都参与比对，解析时不做任何归一化。
* 不比对第 2.2 节各小节的表格：枚举值域、字段长度 / 字节上限、发送频率、
  发送方向、qos、字段语义等都不在本工具覆盖范围内；它们的核对方式见
  docs/V1.0.0-变更分析.md 与 README。
* 因此本工具不能用来声称「与官方 100% 一致」。当前事实是：
  31/32 条协议消息字面一致 + 1 条有意偏离（RadarInfoToClient 的线格式双模式，
  见下）。偏离会原样打印，不隐藏、不改写。

已知偏离
--------
RadarInfoToClient：官方 PDF 第 2.2.19 节的代码块字面写作::

    message RadarInfoToClient {
         repeated uint32 RadarSingleRobotInfo;
    }

——该声明缺少字段号、字段名使用了结构体名 RadarSingleRobotInfo，本身不是合法的 proto 声明。本项目据此
给出两种「都实现、可切换」的线格式：

* NestedMessage（默认）：repeated RadarSingleRobotInfo radar_single_robot_info = 1;
  （嵌套 message，与 ACE / ClearWei 等第三方实现一致；JSON / 日志始终用这种形式）；
* FlatPackedUint32：proto/rm2027_radar_flat.proto 里
  repeated uint32 radar_single_robot_info = 1;
  （补上官方缺失的字段号，8 目标 x 3 值 = 24 个 packed uint32，严格对应官方字面）。

切换方式：C++ rm2027::setRadarWireFormat()；C rm_set_radar_wire_format(0|1)；
Python rm.set_radar_wire_format("nested"|"flat")；CLI
--radar-wire-format nested|flat（rm_server / rm_client）。
默认选嵌套的理由与实测见根目录 README.md 的「雷达线格式」一节。

退出码
------
0   仅存在上述已知偏离，且消息集合无缺失 / 多余；
1   存在其它字面不一致，或消息集合有缺失 / 多余（--strict 下任何字面不一致都算 1）；
2   用法错误。
"""
import argparse
import os
import re
import sys

# 官方代码块里并列写出的辅助 message（不是 MQTT topic，本项目实现为嵌套定义）。
AUX_MESSAGES = {"RadarSingleRobotInfo"}

# 唯一被认可的「有意偏离」：官方字面 vs 本项目双线格式。
KNOWN_DEVIATIONS = {"RadarInfoToClient"}

# 另一种线格式的 proto 文件名特征：不参与 32 条主比对，单独说明。
VARIANT_PROTO_MARK = "flat"

# 字段：可选 label + 类型（含 map<...> 占位）+ 名 + 可选字段号 + 可选 [options] + ;
FIELD_RE = re.compile(
    r"^(?:(optional|required|repeated)\s+)?"
    r"([A-Za-z_][\w.]*(?:<[^>]*>)?)\s+"
    r"([A-Za-z_]\w*)\s*"
    r"(?:=\s*(\d+))?\s*"
    r"(?:\[([^\]]*)\])?\s*;"
)
# 官方 PDF 里出现 message RadarSingleRobotInfo 后另起一行 { 的写法。
MESSAGE_RE = re.compile(r"^message\s+([A-Za-z_]\w*)\s*(\{)?\s*$")


def clean_pdf_text(text):
    """去掉页脚版权行与单独成行的页码，避免打断 message 代码块。"""
    text = re.sub(r"^.*©\s*\d{4}\s*大疆\s*版权所有.*$", "", text, flags=re.M)
    text = re.sub(r"^\s*\d{1,3}\s*$", "", text, flags=re.M)
    return text


def extract_pdf_section(path):
    """截取官方 PDF 的第 2.2 节（详细协议定义）到附录一之间的正文。"""
    raw = open(path, encoding="utf-8").read()
    start = raw.rindex("2.2 详细协议定义")
    m = re.search(r"附录一[：:]", raw[start:])
    end = start + m.start() if m else len(raw)
    return clean_pdf_text(raw[start:end])


def parse_messages(text):
    """解析 proto 文本里的 message 定义（支持嵌套 message）。

    返回 dict（按 Python 3.7+ 插入顺序）：
        key   = 限定名（顶层为消息名，嵌套为 父.子）
        value = {
            "name": 简单名,
            "qual": 限定名,
            "parent": 父限定名或 None,
            "fields": [字段 dict, ...],
            "raw": [代码块内的原始行（已 strip），...],
        }

    对官方文本里的 message X 换行再写 { 也兼容。
    """
    messages = {}
    order = []
    stack = []
    awaiting_brace = False

    for line in text.splitlines():
        s = line.strip()
        if not s:
            continue
        m = MESSAGE_RE.match(s)
        if m:
            name = m.group(1)
            qual = ".".join(stack + [name]) if stack else name
            messages[qual] = {
                "name": name,
                "qual": qual,
                "parent": ".".join(stack) if stack else None,
                "fields": [],
                "raw": [s],
            }
            order.append(qual)
            stack.append(name)
            awaiting_brace = "{" not in s
            continue
        if awaiting_brace:
            if s == "{":
                awaiting_brace = False
                continue
            awaiting_brace = False
        if not stack:
            continue
        if s.startswith("}"):
            messages[".".join(stack)]["raw"].append("}")
            stack.pop()
            continue
        messages[".".join(stack)]["raw"].append(s)
        fm = FIELD_RE.match(s)
        if fm:
            label, ftype, fname, fnum, options = fm.groups()
            messages[".".join(stack)]["fields"].append({
                "label": label or "",
                "repeated": (label or "") == "repeated",
                "type": ftype,
                "name": fname,
                "num": int(fnum) if fnum else None,
                "packed": bool(re.search(r"packed\s*=\s*true", options or "", re.I)),
            })

    return {k: messages[k] for k in order}


def load_protos(proto_dir):
    """读取 proto 目录。

    返回 (main, variants)：
        main     = 主 proto（rm2027_client_up.proto / rm2027_client_down.proto 等）里的 message 定义；
        variants = [{"file": 文件名, "messages": {...}}, ...] 另一种线格式变体
                   （文件名含 flat 的 proto，不参与 32 条主比对）。
    """
    main = {}
    main_order = []
    variants = []
    for filename in sorted(os.listdir(proto_dir)):
        if not filename.endswith(".proto") or "scenario" in filename:
            continue
        body = open(os.path.join(proto_dir, filename), encoding="utf-8").read()
        body = re.sub(r"//.*", "", body)
        parsed = parse_messages(body)
        if VARIANT_PROTO_MARK in filename.lower():
            variants.append({"file": filename, "messages": parsed})
            continue
        for qual, msg in parsed.items():
            if qual in main:
                print(f"警告: proto 里重复定义 message {qual}（{filename}），以先出现者为准")
                continue
            main[qual] = msg
            main_order.append(qual)
    return {k: main[k] for k in main_order}, variants


def field_key(fields):
    """字面比对键：类型 / 字段名 / 字段号 / repeated / label / [packed]。"""
    return [
        (f["type"], f["name"], f["num"], f["repeated"], f["label"], f["packed"])
        for f in fields
    ]


def fields_equal(a, b):
    return field_key(a) == field_key(b)


def fmt_field(f):
    """把字段格式化成 proto 风格的一行，便于人工核对。"""
    label = f["label"] + " " if f["label"] else ""
    num = f"= {f['num']}" if f["num"] is not None else "（字段号未给出）"
    packed = " [packed = true]" if f["packed"] else ""
    return f"{label}{f['type']} {f['name']} {num}{packed}"


def print_message_diff(official, repo, indent="    "):
    """打印一条消息的逐字段差异（按字段名对齐，缺失/多余也列出）。"""
    off_by_name = {f["name"]: f for f in official}
    rep_by_name = {f["name"]: f for f in repo}
    for f in official:
        if f["name"] in rep_by_name:
            g = rep_by_name[f["name"]]
            if field_key([f]) != field_key([g]):
                print(f"{indent}[异] {f['name']}: 官方=({fmt_field(f)}) | 本项目=({fmt_field(g)})")
        else:
            print(f"{indent}[缺] {fmt_field(f)}")
    for f in repo:
        if f["name"] not in off_by_name:
            print(f"{indent}[多] {fmt_field(f)}（官方字面中不存在该字段）")


def print_known_deviation(spec, repo, variants):
    """对唯一已知偏离 RadarInfoToClient 单独打印，原样展示两边声明。"""
    print("\n" + "=" * 78)
    print("【C. 已知偏离（1 条，有意为之，原样列出、不隐藏、不改写）】")
    print("=" * 78)
    name = "RadarInfoToClient"
    off = spec[name]
    rep = repo[name]
    print("\n官方 PDF 第 2.2.19 节代码块原文：")
    for line in off["raw"]:
        print("    " + line)
    print("    ↑ 说明：该声明缺少字段号、字段名使用了结构体名，本身不是合法的 proto 声明；")
    print("      若按字面线格式解释，则是 packed repeated uint32。")
    print("\n本项目声明（主 proto，默认线格式 NestedMessage）：")
    for line in rep["raw"]:
        print("    " + line)
    print("\n字段级字面差异：")
    print_message_diff(off["fields"], rep["fields"], indent="    ")
    print("\n说明：线格式存在「官方原文字面」与「嵌套 message」两种解释，本项目两种都实现、可切换：")
    print("    • NestedMessage（默认）：repeated RadarSingleRobotInfo radar_single_robot_info = 1；")
    print("      与 ACE / ClearWei 等第三方实现的线格式一致，JSON / 日志也始终用这种形式。")
    print("    • FlatPackedUint32：严格按官方字面，字段 1 为 packed repeated uint32，")
    print("      8 目标 x 3 值 = 24 个 uint32。")
    print("    切换方式：C++ rm2027::setRadarWireFormat()；C rm_set_radar_wire_format(0|1)；")
    print("              Python rm.set_radar_wire_format(\"nested\"|\"flat\")；")
    print("              CLI --radar-wire-format nested|flat（rm_server / rm_client）。")
    print("    默认选嵌套的理由与实测见根目录 README.md 的「雷达线格式」一节。")
    if variants:
        print("\n另一种线格式定义（proto 目录里的变体文件，不参与 32 条主比对）：")
        for variant in variants:
            for msg in variant["messages"].values():
                if msg["name"] != name:
                    continue
                print(f"    {variant['file']}:")
                for line in msg["raw"]:
                    print("        " + line)
                print("    ↑ 补上了官方缺失的字段号 = 1，字段类型 / repeated 与官方字面一致。")
    else:
        print("\n（未找到另一种线格式 proto 文件；仅默认嵌套实现。）")


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="与官方 RM2027 协议 PDF 第 2.2 节的 message 代码块做字面逐字段比对",
    )
    parser.add_argument("pdf_text", help="pdftotext -layout 的官方协议 PDF 文本")
    parser.add_argument(
        "proto_dir", nargs="?", default=None,
        help="本项目 proto 目录（默认脚本旁的 ../proto）",
    )
    parser.add_argument(
        "--strict", action="store_true",
        help="严格模式：任何字面不一致（含已知偏离）都返回 1",
    )
    args = parser.parse_args(argv)

    here = os.path.dirname(os.path.abspath(__file__))
    proto_dir = args.proto_dir or os.path.join(here, "..", "proto")
    if not os.path.isfile(args.pdf_text):
        print(f"错误: 找不到官方文本 {args.pdf_text}", file=sys.stderr)
        return 2
    if not os.path.isdir(proto_dir):
        print(f"错误: 找不到 proto 目录 {proto_dir}", file=sys.stderr)
        return 2

    spec = parse_messages(extract_pdf_section(args.pdf_text))
    main_msgs, variants = load_protos(proto_dir)

    repo_by_simple = {}
    for msg in main_msgs.values():
        if msg["name"] in repo_by_simple:
            print(f"警告: 多个 message 同名 {msg['name']}，按先出现者参与比对")
            continue
        repo_by_simple[msg["name"]] = msg
    variant_defs = sum(len(v["messages"]) for v in variants)

    official_protocol = [n for n in spec if n not in AUX_MESSAGES]
    repo_top = [m for m in repo_by_simple.values() if m["parent"] is None]

    print("=" * 78)
    print("官方 PDF 第 2.2 节 message 代码块 vs 本项目 proto —— 字面比对")
    print("=" * 78)
    print(f"官方第 2.2 节 message 代码块: {len(spec)}"
          f"（{len(official_protocol)} 条协议消息 + {len(spec) - len(official_protocol)} 条并列辅助 message）")
    print(f"本项目 proto message 定义: {len(repo_by_simple)}"
          f"（{len(repo_top)} 条顶层 + {len(repo_by_simple) - len(repo_top)} 条嵌套"
          f"{f'；另有 {variant_defs} 个另一种线格式变体，见【D】' if variants else ''}）")
    print("\n比对 key = (类型, 字段名, 字段号, 是否 repeated, optional/repeated 标签, 是否 [packed])")
    print("本工具只比对第 2.2 节的 message 代码块；该节表格里的枚举值域、字段长度、")
    print("发送频率 / 方向 / qos / 语义等不在比对范围（见 docs/V1.0.0-变更分析.md 与 README）。")

    # A. 消息集合
    missing = [n for n in spec if n not in repo_by_simple]
    extra = [n for n in repo_by_simple if n not in spec]
    print("\n" + "-" * 78)
    print("【A. 消息集合】")
    print("-" * 78)
    print(f"官方有、本项目缺失: {len(missing)}")
    for n in missing:
        print("    •", n)
    print(f"本项目有、官方无:   {len(extra)}")
    for n in extra:
        print("    •", n)
    for aux in AUX_MESSAGES:
        if aux not in spec:
            continue
        if aux not in repo_by_simple:
            print(f"    辅助 message {aux}: 本项目未找到")
            continue
        same = fields_equal(spec[aux]["fields"], repo_by_simple[aux]["fields"])
        where = "嵌套定义" if repo_by_simple[aux]["parent"] else "顶层定义"
        print(f"    辅助 message {aux}: 官方在第 2.2 节中并列写出，本项目为{where}；"
              f"字段字面{'一致' if same else '不一致'}")

    # B. 逐条字面比对
    print("\n" + "-" * 78)
    print("【B. 逐条字面比对】")
    print("-" * 78)
    equal, diffs = [], []
    for name in official_protocol:
        if name not in repo_by_simple:
            continue
        if fields_equal(spec[name]["fields"], repo_by_simple[name]["fields"]):
            equal.append(name)
        else:
            diffs.append(name)
    print(f"字面一致: {len(equal)}/{len(official_protocol)} 条协议消息")
    known_diffs = [n for n in diffs if n in KNOWN_DEVIATIONS]
    unexpected = [n for n in diffs if n not in KNOWN_DEVIATIONS]
    print(f"已知偏离: {len(known_diffs)} 条" + (f"（{', '.join(known_diffs)}）" if known_diffs else ""))
    print(f"非预期不一致: {len(unexpected)} 条")
    for name in unexpected:
        print(f"\n  ✗ {name}")
        print_message_diff(spec[name]["fields"], repo_by_simple[name]["fields"])

    # C. 已知偏离详情
    if "RadarInfoToClient" in spec and "RadarInfoToClient" in repo_by_simple:
        print_known_deviation(spec, repo_by_simple, variants)

    # D. 另一种线格式变体
    if variants:
        print("\n" + "-" * 78)
        print("【D. 另一种线格式变体（不参与 32 条主比对）】")
        print("-" * 78)
        for variant in variants:
            print(f"  {variant['file']}:")
            for msg in variant["messages"].values():
                for line in msg["raw"]:
                    print("    " + line)

    # 结论 / 退出码
    print("\n" + "=" * 78)
    print("【结论】")
    print("=" * 78)
    conclusion = f"  {len(equal)}/{len(official_protocol)} 条协议消息字面一致"
    if known_diffs:
        conclusion += f" + {len(known_diffs)} 条有意偏离（{', '.join(known_diffs)}，双线格式）"
    if unexpected:
        conclusion += f"；{len(unexpected)} 条非预期不一致"
    if missing or extra:
        conclusion += f"；消息集合缺失 {len(missing)} / 多余 {len(extra)}"
    else:
        conclusion += "；消息集合 0 缺失 0 多余"
    print(conclusion)
    if known_diffs:
        print("  已知偏离不是缺陷：RadarInfoToClient 的线格式双模式见上方【C】与 README「雷达线格式」。")
    ok = not missing and not extra and not unexpected
    if args.strict:
        ok = ok and not known_diffs
        print("  --strict：任何字面不一致（含已知偏离）都视为失败。")
    print("  退出码:", 0 if ok else 1)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
