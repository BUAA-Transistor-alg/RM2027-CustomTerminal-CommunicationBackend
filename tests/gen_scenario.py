#!/usr/bin/env python3
"""由官方 V1.0.0 字段表生成确定性的测试场景 JSON。

字段取值规则（每条消息、每个字段都有独一无二的值，便于定位字段号/类型错误）：
    uint32/int32/int64/uint64 -> field_number * 1000 + msg_salt
    float/double              -> field_number * 1.5 + msg_salt
    bool                      -> True
    string                    -> "s<field_number>"
    bytes                     -> 0x<field_number> 0xAB 的 base64
    repeated                  -> [v, v]
    嵌套消息(repeated)         -> [obj, obj]

用法:
    python3 tests/gen_scenario.py            # 生成 tests/scenarios/*.json
"""
import base64
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SPEC = os.path.join(ROOT, "docs", "spec-v1.0.0-fields.json")

# 官方 V1.0.0 表 2-1：自定义终端 -> 服务器（9 条）
# 2027 删除了 HeroDeployModeEventCommand / AirSupportCommand（共 36 -> 32 条）
UPSTREAM = [
    "KeyboardMouseControl", "CustomControl", "MapClickCmd", "AssemblyCommand",
    "RobotPerformanceSelectionCommand", "CommonCommand",
    "RuneActivateCommand", "DartCommand", "SentryCtrlCommand",
]

FLOAT_TYPES = {"float", "double"}
INT_TYPES = {"int32", "uint32", "int64", "uint64", "sint32", "sint64",
             "fixed32", "fixed64", "sfixed32", "sfixed64"}


def value_for(field, salt):
    num = field["num"]
    t = field["type"]
    if "fields" in field:                      # 嵌套消息
        obj = {f["name"]: value_for(f, salt) for f in field["fields"]}
        return [obj, dict(obj)] if field.get("repeated") else obj
    if t == "bool":
        v = True
    elif t == "string":
        v = "s%d" % num
    elif t == "bytes":
        v = base64.b64encode(bytes([num & 0xFF, 0xAB])).decode()
    elif t in FLOAT_TYPES:
        v = round(num * 1.5 + salt, 4)
    else:
        v = num * 1000 + salt
    return [v, v] if field.get("repeated") else v


def build(name, spec, salt):
    return {f["name"]: value_for(f, salt) for f in spec[name]}


def main():
    if not os.path.exists(SPEC):
        print("找不到字段表: %s" % SPEC, file=sys.stderr)
        print("请先执行: python3 tools/gen_v2_proto.py <2027协议文本.txt> proto", file=sys.stderr)
        return 1
    spec = json.load(open(SPEC, encoding="utf-8"))

    down = [m for m in spec if m not in UPSTREAM]
    up = [m for m in spec if m in UPSTREAM]
    missing = [m for m in UPSTREAM if m not in spec]
    if missing:
        print("字段表缺少上行消息: %s" % missing, file=sys.stderr)
        return 1

    def scenario(names):
        return {"steps": [{"topic": n, "data": build(n, spec, (i + 1) * 100)}
                          for i, n in enumerate(names)]}

    outdir = os.path.join(HERE, "scenarios")
    os.makedirs(outdir, exist_ok=True)
    files = {
        "all_down.json": scenario(down),
        "all_up.json": scenario(up),
    }
    for fn, data in files.items():
        with open(os.path.join(outdir, fn), "w", encoding="utf-8") as fh:
            json.dump(data, fh, ensure_ascii=False, indent=2)
        print("生成 %-16s %2d 步" % (fn, len(data["steps"])))
    print("下行 %d 条, 上行 %d 条, 合计 %d 条" % (len(down), len(up), len(down) + len(up)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
