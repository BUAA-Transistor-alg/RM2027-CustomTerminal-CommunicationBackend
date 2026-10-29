#!/usr/bin/env python3
"""RM2027 C++ 实现验证比对器（官方协议 V1.0.0）。

把「期望值（场景文件）」与「实际记录（客户端/服务器 stdout 的 JSON 行）」逐字段比对。

用法:
    check_records.py --expect-down tests/scenarios/all_down.json \
                     --expect-up   tests/scenarios/all_up.json \
                     --client      client.out \
                     [--server server.out] \
                     [--node-uplink history.json]

判定规则:
    * 整数/布尔/字符串/bytes -> 精确相等（整数允许 "2200" 这类字符串形式）
    * float/double          -> 相对误差 1e-5 内视为相等
    * 嵌套消息              -> 递归比对
    * 任一消息缺失、任一字段不等 -> 失败并打印定位信息
"""
import argparse
import json
import os
import sys

FLOAT_TYPES = {"float", "double"}
INT_TYPES = {"int32", "uint32", "int64", "uint64", "sint32", "sint64",
             "fixed32", "fixed64", "sfixed32", "sfixed64"}


def camel(name):
    out, up = [], False
    for ch in name:
        if ch == "_":
            up = True
        elif up:
            out.append(ch.upper())
            up = False
        else:
            out.append(ch)
    return "".join(out)


def load_spec(path):
    """message -> {camelField: fieldDef}（fieldDef 可能含嵌套的 fields）"""
    raw = json.load(open(path, encoding="utf-8"))
    table = {}
    for msg, fields in raw.items():
        table[msg] = {camel(f["name"]): f for f in fields}
    return table


def camelize_keys(v):
    """递归把 snake_case 键名转成 protobuf JSON 的 camelCase（嵌套消息同样处理）"""
    if isinstance(v, list):
        return [camelize_keys(x) for x in v]
    if isinstance(v, dict):
        return {camel(k): camelize_keys(x) for k, x in v.items()}
    return v


def load_scenario(path):
    if not path:
        return {}
    d = json.load(open(path, encoding="utf-8"))
    return {s["topic"]: camelize_keys(s["data"]) for s in d["steps"]}


def read_records(path):
    recs = []
    if not path or not os.path.exists(path):
        return recs
    for line in open(path, encoding="utf-8", errors="replace"):
        line = line.strip()
        if not line.startswith("{"):
            continue
        try:
            recs.append(json.loads(line))
        except json.JSONDecodeError:
            continue
    return recs


def read_node_uplink(path):
    """SharkDataSever /api/uplink-history 返回格式"""
    if not path or not os.path.exists(path):
        return []
    raw = json.load(open(path, encoding="utf-8"))
    if isinstance(raw, dict):
        raw = raw.get("messages", [])
    return [{"dir": "up", "topic": i.get("topic") or i.get("messageType"),
             "data": i.get("data", {})} for i in raw]


def by_topic(records, direction):
    out = {}
    for r in records:
        if r.get("dir") != direction:
            continue
        out.setdefault(r["topic"], []).append(r)
    return out


def compare_scalar(path, ftype, expect, actual, errors):
    if ftype in FLOAT_TYPES:
        try:
            e, a = float(expect), float(actual)
        except (TypeError, ValueError):
            errors.append(f"{path}: 无法转 float (期望 {expect!r} 实得 {actual!r})")
            return
        tol = max(1e-4, abs(e) * 1e-5)
        if abs(e - a) > tol:
            errors.append(f"{path}: float 不等 期望 {e} 实得 {a} (tol {tol:g})")
    elif ftype in INT_TYPES:
        try:
            if int(expect) != int(actual):
                errors.append(f"{path}: 整数不等 期望 {expect} 实得 {actual}")
        except (TypeError, ValueError):
            errors.append(f"{path}: 无法转整数 (期望 {expect!r} 实得 {actual!r})")
    elif ftype == "bool":
        if bool(expect) != bool(actual):
            errors.append(f"{path}: bool 不等 期望 {expect} 实得 {actual}")
    else:  # string / bytes
        if str(expect).rstrip("=") != str(actual).rstrip("="):
            errors.append(f"{path}: 不等 期望 {expect!r} 实得 {actual!r}")


def compare_field(path, fdef, expect, actual, errors):
    """fdef 来自字段表；含 'fields' 表示嵌套消息"""
    if "fields" in fdef:
        sub = {camel(f["name"]): f for f in fdef["fields"]}
        if fdef.get("repeated"):
            if not isinstance(expect, list) or not isinstance(actual, list):
                errors.append(f"{path}: 嵌套 repeated 类型不符")
                return
            if len(expect) != len(actual):
                errors.append(f"{path}: 元素个数不等 期望 {len(expect)} 实得 {len(actual)}")
                return
            for i, (e, a) in enumerate(zip(expect, actual)):
                for k, v in e.items():
                    if k not in a:
                        errors.append(f"{path}[{i}].{k}: 实际缺少该字段")
                        continue
                    compare_field(f"{path}[{i}].{k}", sub[k], v, a[k], errors)
        else:
            for k, v in expect.items():
                if k not in actual:
                    errors.append(f"{path}.{k}: 实际缺少该字段")
                    continue
                compare_field(f"{path}.{k}", sub[k], v, actual[k], errors)
        return

    if fdef.get("repeated"):
        if not isinstance(expect, list) or not isinstance(actual, list):
            errors.append(f"{path}: repeated 类型不符 期望 {expect!r} 实得 {actual!r}")
            return
        if len(expect) != len(actual):
            errors.append(f"{path}: repeated 长度不等 期望 {len(expect)} 实得 {len(actual)}")
            return
        for i, (e, a) in enumerate(zip(expect, actual)):
            compare_scalar(f"{path}[{i}]", fdef["type"], e, a, errors)
    else:
        compare_scalar(path, fdef["type"], expect, actual, errors)


def compare_message(msg, expects, actuals, spec, errors):
    fields = spec.get(msg, {})
    for f, want in expects.items():
        fdef = fields.get(f)
        if fdef is None:
            errors.append(f"{msg}.{f}: 字段表里没有这个字段（场景与协议不一致？）")
            continue
        if f not in actuals:
            errors.append(f"{msg}.{f}: 实际记录中缺少该字段（期望 {want!r}）")
            continue
        compare_field(f"{msg}.{f}", fdef, want, actuals[f], errors)
    extra = [k for k in actuals if k not in expects]
    if extra:
        errors.append(f"{msg}: 实际多出字段 {extra}（期望里没有）")


def check_direction(label, expects, records, spec, out):
    errors, checked = [], 0
    if not expects:
        return 0, []
    got = by_topic(records, "down" if label.startswith("down") else "up")
    for topic, want in expects.items():
        recs = got.get(topic)
        if not recs:
            errors.append(f"[{label}] 完全没收到消息: {topic}")
            continue
        err_rec = [r for r in recs if r.get("error")]
        if err_rec:
            errors.append(f"[{label}] {topic} 处理出错: {err_rec[0].get('error')}")
            continue
        compare_message(topic, want, recs[0].get("data", {}), spec, errors)
        checked += 1
    out.append(f"  [{label}] 比对 {checked}/{len(expects)} 条消息")
    return checked, errors


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--spec")
    ap.add_argument("--expect-down")
    ap.add_argument("--expect-up")
    ap.add_argument("--client")
    ap.add_argument("--server")
    ap.add_argument("--node-uplink")
    args = ap.parse_args()

    here = os.path.dirname(os.path.abspath(__file__))
    spec_path = args.spec or os.path.join(here, "..", "docs", "spec-v1.0.0-fields.json")
    spec = load_spec(spec_path)

    exp_down = load_scenario(args.expect_down)
    exp_up = load_scenario(args.expect_up)

    client_recs = read_records(args.client)
    server_recs = read_records(args.server) + read_node_uplink(args.node_uplink)

    lines, all_errors = [], []

    print("=" * 78)
    print("RM2027 C++ 协议实现验证 (官方协议 V1.0.0)")
    print("=" * 78)

    lines.append("客户端 (C++) 接收下行:")
    _, errs = check_direction("down", exp_down, client_recs, spec, lines)
    all_errors += errs

    if server_recs:
        lines.append("服务器接收上行:")
        _, errs2 = check_direction("up", exp_up, server_recs, spec, lines)
        all_errors += errs2
    else:
        lines.append("服务器接收上行: (未提供记录，跳过)")

    if exp_up:
        lines.append("客户端发送上行(自检):")
        _, errs3 = check_direction("up-sent", exp_up, client_recs, spec, lines)
        all_errors += errs3

    for l in lines:
        print(l)

    for r in client_recs:
        if r.get("ev") == "summary":
            print(f"\n客户端统计: 收到 {r.get('received')} 发送 {r.get('published')} "
                  f"未知topic {r.get('unknownTopic')} 解码失败 {r.get('decodeFailed')}")
            if r.get("unknownTopic") or r.get("decodeFailed"):
                all_errors.append("客户端出现未知 topic 或解码失败")

    if all_errors:
        print(f"\n❌ 失败：发现 {len(all_errors)} 处不一致")
        for e in all_errors[:60]:
            print("   -", e)
        if len(all_errors) > 60:
            print(f"   ... 另有 {len(all_errors) - 60} 处")
        return 1

    print("\n✅ 通过：所有期望字段逐项一致")
    return 0


if __name__ == "__main__":
    sys.exit(main())
