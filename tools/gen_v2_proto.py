#!/usr/bin/env python3
"""从官方 V1.0.0 协议 PDF 文本生成 proto 文件。

处理官方文档中的格式细节与歧义：
  * `bytes data=1；`                        —— 全角分号
  * `repeated uint32 RadarSingleRobotInfo;` —— 字段号未给出、类型写作 uint32。
      官方 PDF §2.2.19 同时给出并列的 `message RadarSingleRobotInfo`，存在两种合理解释：
        A. 嵌套 message（本项目默认）：repeated RadarSingleRobotInfo radar_single_robot_info = 1;
        B. 官方字面：repeated uint32 radar_single_robot_info = 1（packed，8 目标 × 3 值 = 24 个 uint32）
      因此本工具同时生成两份 proto：
        rm2027_client_down.proto  —— 默认 A（嵌套），其余 31 条消息也在这里
        rm2027_radar_flat.proto   —— 备用 B（官方字面），仅 RadarInfoToClient 一条消息
      运行时用 rm2027::setRadarWireFormat() 切换（见 README「雷达线格式」）。

用法:
    python3 tools/gen_v2_proto.py <2027协议文本.txt> [输出目录]
"""
import json
import os
import re
import sys

UPSTREAM = [
    "KeyboardMouseControl", "CustomControl", "MapClickCmd", "AssemblyCommand",
    "RobotPerformanceSelectionCommand", "CommonCommand",
    "RuneActivateCommand", "DartCommand", "SentryCtrlCommand",
]

# 官方文档里紧跟 RadarInfoToClient 的辅助结构，不是独立 topic
AUX = {"RadarSingleRobotInfo"}

FIELD_RE = re.compile(
    r"^(?:(optional|required|repeated)\s+)?(\w+)\s+(\w+)\s*(?:=\s*(\d+))?\s*(?:\[([^\]]*)\])?\s*;")


def extract_section(path):
    raw = open(path, encoding="utf-8").read()
    start = raw.rindex("2.2 详细协议定义")
    m = re.search(r"附录一[：:]", raw[start:])
    end = start + m.start() if m else len(raw)
    text = raw[start:end]
    text = re.sub(r"^.*©\s*\d{4}\s*大疆\s*版权所有.*$", "", text, flags=re.M)
    text = re.sub(r"^\s*\d{1,3}\s*$", "", text, flags=re.M)
    return text


def parse(text):
    msgs, cur, order = {}, None, []
    for ln in text.split("\n"):
        s = ln.strip().replace("；", ";").replace("，", ",")
        m = re.match(r"^message\s+([A-Za-z0-9_]+)\s*\{?", s)
        if m:
            cur = m.group(1)
            if cur not in msgs:
                msgs[cur] = []
                order.append(cur)
            continue
        if cur is None:
            continue
        if s == "}":
            cur = None
            continue
        f = FIELD_RE.match(s)
        if f:
            label, ftype, name, num, opts = f.groups()
            msgs[cur].append({
                "label": label or "",
                "type": ftype,
                "name": name,
                "num": int(num) if num else None,
                "opts": opts or "",
            })
    return msgs, order


def field_line(f, indent=""):
    label = (f["label"] + " ") if f["label"] else ""
    opts = f" [{f['opts']}]" if f["opts"] else ""
    return f"{indent}{label}{f['type']} {f['name']} = {f['num']}{opts};"


def render(name, fields, indent=""):
    out = [f"{indent}message {name} {{"]
    for f in fields:
        out.append(field_line(f, indent + "    "))
    out.append(f"{indent}}}")
    return out


def render_radar(inner):
    return [
        "message RadarInfoToClient {",
        "    // 默认线格式（NestedMessage）：嵌套 message。",
        "    // 官方 PDF §2.2.19 的字面写法（repeated uint32）见 rm2027_radar_flat.proto，",
        "    // 运行时用 rm2027::setRadarWireFormat() 切换。",
        "    repeated RadarSingleRobotInfo radar_single_robot_info = 1;",
        "",
        "    // 元素结构：一次发送 8 个目标，顺序为「对方 1/3/4/7 号，己方 1/3/4/7 号」",
        "    // 坐标单位 cm",
        "    message RadarSingleRobotInfo {",
        *[field_line(f, "        ") for f in inner],
        "    }",
        "}",
    ]


def render_radar_flat():
    return [
        "message RadarInfoToClient {",
        "    // 官方 PDF §2.2.19 字面声明：原文为 `repeated uint32 RadarSingleRobotInfo;`",
        "    // （官方原文未给出字段号、字段名使用了结构体名）。这里补字段号 = 1，语义等价：",
        "    // 字段 1 = packed repeated uint32，8 个目标 × 3 个值 = 24 个 uint32。",
        "    repeated uint32 radar_single_robot_info = 1;",
        "}",
    ]


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    here = os.path.dirname(os.path.abspath(__file__))
    outdir = sys.argv[2] if len(sys.argv) > 2 else os.path.join(here, "..", "proto")

    msgs, order = parse(extract_section(sys.argv[1]))
    topics = [m for m in order if m not in AUX]
    print(f"解析到 {len(order)} 个 message，其中 topic {len(topics)} 个，辅助结构 {sorted(AUX)}")

    upstream = [m for m in topics if m in UPSTREAM]
    downlink = [m for m in topics if m not in UPSTREAM]
    print(f"上行 {len(upstream)} 条, 下行 {len(downlink)} 条")

    header = [
        'syntax = "proto3";',
        "// RoboMaster 2027 机甲大师高校系列赛 自定义终端通信协议",
        "// 版本: V1.0.0 (20261029) —— 依据官方 PDF §2.2 提取",
        "// 官方要求 protobuf >= 3.15（proto3 optional）",
        "",
    ]

    def write(fn, pkg, names, title):
        lines = list(header) + [f"// ===== {title} =====", f"package {pkg};", ""]
        for n in names:
            if n == "RadarInfoToClient":
                lines += render_radar(msgs["RadarSingleRobotInfo"])
            else:
                lines += render(n, msgs[n])
            lines.append("")
        open(os.path.join(outdir, fn), "w", encoding="utf-8").write("\n".join(lines))
        print(f"  写出 {fn}: {len(names)} 条")

    write("rm2027_client_up.proto", "rm_client_up", upstream, "上行：自定义终端 -> 服务器")
    write("rm2027_client_down.proto", "rm_client_down", downlink, "下行：服务器 -> 自定义终端")

    # 另一种线格式：官方 §2.2.19 的字面声明（只含 RadarInfoToClient）
    flat_header = [
        'syntax = "proto3";',
        "// RoboMaster 2027 自定义终端通信协议 —— RadarInfoToClient 的**另一种线格式**",
        "// 官方 PDF §2.2.19 原文：`repeated uint32 RadarSingleRobotInfo;`（字段号未给出）",
        "// 这里补字段号 = 1，与官方字面线格式完全一致（packed repeated uint32）。",
        "// 仅在 rm2027::setRadarWireFormat(RadarWireFormat::FlatPackedUint32) 时使用；",
        "// 默认线格式是 rm2027_client_down.proto 中的嵌套 message 写法。",
        "// 生成器：tools/gen_v2_proto.py；说明：README「雷达线格式」。",
        "",
        "package rm_radar_flat;",
        "",
    ]
    flat_lines = flat_header + render_radar_flat() + [""]
    open(os.path.join(outdir, "rm2027_radar_flat.proto"), "w",
         encoding="utf-8").write("\n".join(flat_lines))
    print("  写出 rm2027_radar_flat.proto: 1 条（RadarInfoToClient，官方字面线格式）")

    # 机器可读字段表（供 gen_scenario.py / check_records.py 使用）
    spec = {}
    for n in topics:
        if n == "RadarInfoToClient":
            inner = [{"name": f["name"], "type": f["type"], "num": f["num"],
                      "repeated": f["label"] == "repeated"} for f in msgs["RadarSingleRobotInfo"]]
            spec[n] = [{"name": "radar_single_robot_info", "type": "RadarSingleRobotInfo",
                        "num": 1, "repeated": True, "fields": inner}]
        else:
            spec[n] = [{"name": f["name"], "type": f["type"], "num": f["num"],
                        "repeated": f["label"] == "repeated"} for f in msgs[n]]
    spec_path = os.path.join(os.path.dirname(os.path.abspath(outdir)), "docs", "spec-v1.0.0-fields.json")
    os.makedirs(os.path.dirname(spec_path), exist_ok=True)
    json.dump(spec, open(spec_path, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    print(f"  写出字段表 {spec_path} ({len(spec)} 条消息)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
