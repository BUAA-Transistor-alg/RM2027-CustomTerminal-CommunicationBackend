#!/usr/bin/env python3
"""由官方 V1.0.0 字段表生成对外 API 层的 C++ 原生结构体与 protobuf 转换代码。

产物:
    include/rm2027/api/messages.h   —— 36 个指令对应的 C++ 结构体
    src/api/messages.cpp            —— 结构体 <-> protobuf 的转换与消息注册表

用法:
    python3 tools/gen_api.py
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SPEC = os.path.join(ROOT, "docs", "spec-v1.0.0-fields.json")
FREQ = os.path.join(ROOT, "docs", "frequencies.json")

UPSTREAM = [
    "KeyboardMouseControl", "CustomControl", "MapClickCmd", "AssemblyCommand",
    "RobotPerformanceSelectionCommand", "CommonCommand",
    "RuneActivateCommand", "DartCommand", "SentryCtrlCommand",
]

SCALAR_CPP = {
    "int32": "int32_t", "sint32": "int32_t", "sfixed32": "int32_t",
    "uint32": "uint32_t", "fixed32": "uint32_t",
    "int64": "int64_t", "sint64": "int64_t", "sfixed64": "int64_t",
    "uint64": "uint64_t", "fixed64": "uint64_t",
    "float": "float", "double": "double",
    "bool": "bool",
    "string": "std::string",
    "bytes": "std::string",   # 二进制安全
}


def cpp_type(field):
    if "fields" in field:
        base = field["type"]
    else:
        base = SCALAR_CPP[field["type"]]
    return f"std::vector<{base}>" if field.get("repeated") else base


def emit_struct(name, fields, indent="", top_level=True):
    """top_level=True 生成继承 Message 的指令结构体；嵌套消息则是普通结构体"""
    head = f"{indent}struct {name}" + (" : Message {" if top_level else " {")
    out = [head]
    if top_level:
        out.append(f'{indent}    static constexpr const char *kName = "{name}";')
    for f in fields:
        if "fields" in f:
            out.append("")
            out += emit_struct(f["type"], f["fields"], indent + "    ", top_level=False)
            out.append("")
        out.append(f"{indent}    {cpp_type(f)} {f['name']}{{}};")
    if top_level:
        out.append("")
        out.append(f"{indent}    const char *name() const override {{ return kName; }}")
    out.append(f"{indent}}};")
    return out


# ---------------------------------------------------------------- 转换代码

def conv_field(dst, src, f, pkg, prefix=""):
    """生成一组赋值/取值语句。dst 是 proto 对象，src 是原生结构体（或反向）"""
    pass


def acc(field_name):
    """protobuf 生成的 C++ 访问器名：protoc 会把字段名整体转小写。
    例：官方字段 outpost_R_health -> 访问器 outpost_r_health()。
    结构体成员仍保留官方写法的原始大小写，便于与 PDF 对照。"""
    return field_name.lower()


def proto_setter(p, n, f):
    """原生 -> proto 的单字段语句"""
    name = f["name"]
    a = acc(name)
    if "fields" in f:
        inner = f["type"]
        if f.get("repeated"):
            return [f"    for (const auto &e : n.{name}) {{",
                    f"        auto *x = p->add_{a}();",
                    *[f"        x->set_{acc(g['name'])}(e.{g['name']});" for g in f["fields"]],
                    "    }"]
        return [f"    {{", f"        auto *x = p->mutable_{a}();",
                *[f"        x->set_{acc(g['name'])}(n.{name}.{g['name']});" for g in f["fields"]],
                "    }"]
    if f.get("repeated"):
        if f["type"] == "bytes" or f["type"] == "string":
            return [f"    for (const auto &e : n.{name}) p->add_{a}(e);"]
        if f["type"] == "bool":
            return [f"    for (bool e : n.{name}) p->add_{a}(e);"]
        return [f"    for (const auto &e : n.{name}) p->add_{a}(e);"]
    return [f"    p->set_{a}(n.{name});"]


def proto_getter(p, n, f, outer=""):
    """proto -> 原生 的单字段语句"""
    name = f["name"]
    a = acc(name)
    if "fields" in f:
        inner = f"{outer}::{f['type']}" if outer else f["type"]
        if f.get("repeated"):
            return [f"    n->{name}.clear();",
                    f"    n->{name}.reserve(p.{a}_size());",
                    f"    for (const auto &e : p.{a}()) {{",
                    f"        {inner} x{{}};",
                    *[f"        x.{g['name']} = e.{acc(g['name'])}();" for g in f["fields"]],
                    f"        n->{name}.push_back(std::move(x));",
                    "    }"]
        return [f"    {{", f"        const auto &e = p.{a}();",
                *[f"        n->{name}.{g['name']} = e.{acc(g['name'])}();" for g in f["fields"]],
                "    }"]
    if f.get("repeated"):
        return [f"    n->{name}.assign(p.{a}().begin(), p.{a}().end());"]
    return [f"    n->{name} = p.{a}();"]


def main():
    if not os.path.exists(SPEC):
        print("找不到字段表: %s" % SPEC, file=sys.stderr)
        return 1
    spec = json.load(open(SPEC, encoding="utf-8"))
    freq = json.load(open(FREQ, encoding="utf-8"))
    names = list(spec.keys())
    missing_freq = [n for n in names if n not in freq]
    if missing_freq:
        print("频率表缺少: %s" % missing_freq, file=sys.stderr)
        return 1
    print("消息 %d 条" % len(names))

    # ---------------- messages.h ----------------
    h = [
        "// RM2027 自定义终端协议 V1.0.0 —— 对外 API 的原生结构体",
        "//",
        "// 本文件由 tools/gen_api.py 从官方 PDF 字段表自动生成，请勿手工修改。",
        "//",
        "// 每个结构体对应一条指令（MQTT topic = 结构体的 static kName）。",
        "// 发送与回调都以 std::unique_ptr<具体结构体> 传递（移动语义）。",
        "#pragma once",
        "",
        "#include <cstdint>",
        "#include <memory>",
        "#include <string>",
        "#include <vector>",
        "",
        '#include "rm2027/api/message.h"',
        "",
        "namespace rm2027 {",
        "",
    ]
    for n in names:
        h.append(f"// ---- {n} ----")
        h += emit_struct(n, spec[n])
        h.append("")

    h += ["} // namespace rm2027", ""]
    os.makedirs(os.path.join(ROOT, "include", "rm2027", "api"), exist_ok=True)
    open(os.path.join(ROOT, "include", "rm2027", "api", "messages.h"), "w",
         encoding="utf-8").write("\n".join(h))
    print("  写出 include/rm2027/api/messages.h")

    # ---------------- messages.cpp ----------------
    c = [
        "// RM2027 自定义终端协议 V1.0.0 —— 消息注册表与 protobuf 转换",
        "// 由 tools/gen_api.py 自动生成，请勿手工修改。",
        '#include "rm2027/api/messages.h"',
        "",
        "#include <google/protobuf/descriptor.h>",
        "#include <google/protobuf/message.h>",
        "",
        '#include "rm2027/api/detail/registry.h"',
        '#include "rm2027_client_down.pb.h"',
        '#include "rm2027_client_up.pb.h"',
        "",
        "namespace rm2027 {",
        "namespace {",
        "",
    ]

    # 每个消息一对转换函数 + 注册项
    entries_up, entries_down = [], []
    for n in names:
        pkg = "rm_client_up" if n in UPSTREAM else "rm_client_down"
        pf = f"{pkg}::{n}"
        c.append(f"// ================= {n} =================")
        c.append(f"void encode_{n}(const Message &m, {pf} *p) {{")
        c.append(f"    const auto &n = static_cast<const {n} &>(m);")
        for f in spec[n]:
            c += proto_setter("p", "n", f)
        c.append("}")
        c.append("")
        c.append(f"std::unique_ptr<Message> decode_{n}(const {pf} &p) {{")
        c.append(f"    auto n = std::make_unique<{n}>();")
        for f in spec[n]:
            c += proto_getter("p", "n", f, n)
        c.append("    return n;")
        c.append("}")
        c.append("")
        fdef = freq[n]
        fields_json = json.dumps(spec[n], ensure_ascii=False, separators=(",", ":"))
        fields_json_c = fields_json.replace("\\", "\\\\").replace('"', '\\"')
        entry = (f'    {{"{n}", Direction::{"ClientToServer" if n in UPSTREAM else "ServerToClient"},'
                 f'\n     {pf}::descriptor(),'
                 f'\n     {fdef["hz"]:g}, {fdef["max_hz"]:g}, {1 if fdef["triggered"] else 0},'
                 f'\n     "{fields_json_c}",'
                 f'\n     [](const Message &m, std::string *out) {{'
                 f'\n         {pf} p;'
                 f'\n         encode_{n}(m, &p);'
                 f'\n         return p.SerializeToString(out);'
                 f'\n     }},'
                 f'\n     [](const google::protobuf::Message &p) -> std::unique_ptr<Message> {{'
                 f'\n         return decode_{n}(static_cast<const {pf} &>(p));'
                 f'\n     }},'
                 f'\n     []() -> std::unique_ptr<Message> {{ return std::make_unique<{n}>(); }}}},')
        (entries_up if n in UPSTREAM else entries_down).append(entry)

    c += ["const MessageDesc kMessages[] = {"]
    c += entries_up
    c += entries_down
    c += ["};", "",
          "} // namespace", "",
          "const MessageDesc *messageTable(std::size_t *count) {",
          "    *count = sizeof(kMessages) / sizeof(kMessages[0]);",
          "    return kMessages;",
          "}", "",
          "} // namespace rm2027", ""]
    os.makedirs(os.path.join(ROOT, "src", "api"), exist_ok=True)
    open(os.path.join(ROOT, "src", "api", "messages.cpp"), "w",
         encoding="utf-8").write("\n".join(c))
    print("  写出 src/api/messages.cpp")
    return 0


if __name__ == "__main__":
    sys.exit(main())
