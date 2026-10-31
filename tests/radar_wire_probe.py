#!/usr/bin/env python3
"""雷达线格式原始报文探针（零第三方依赖）。

直接连到 rm_server 自带的 MQTT broker，订阅 RadarInfoToClient，抓一条 PUBLISH，
手工解析 protobuf 线格式，验证：
  nested 模式：字段 1 是 8 个 length-delimited 子消息（每个含字段 1/2/3，uint32）
  flat   模式：字段 1 是 1 个 packed repeated uint32，共 24 个值（8 目标 × 3）
并与场景 all_down.json 里 RadarInfoToClient 的第 1 个目标值对照。

用法: radar_wire_probe.py --port 13450 --mode nested|flat --scenario tests/scenarios/all_down.json
"""
import argparse
import json
import socket
import struct
import sys
import time


def enc_str(s: str) -> bytes:
    b = s.encode()
    return struct.pack("!H", len(b)) + b


def mqtt_packet(header: int, body: bytes) -> bytes:
    n = len(body)
    rem = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            rem.append(b | 0x80)
        else:
            rem.append(b)
            break
    return bytes([header]) + bytes(rem) + body


def recv_exact(sock: socket.socket, n: int) -> bytes:
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise EOFError("连接被关闭")
        buf += chunk
    return buf


def read_packet(sock: socket.socket):
    first = recv_exact(sock, 1)[0]
    mult = 1
    rem = 0
    while True:
        b = recv_exact(sock, 1)[0]
        rem += (b & 0x7F) * mult
        if not (b & 0x80):
            break
        mult *= 128
    body = recv_exact(sock, rem) if rem else b""
    return first, body


def read_varint(buf: bytes, pos: int):
    mult = 1
    out = 0
    while True:
        if pos >= len(buf):
            raise ValueError("varint 截断")
        b = buf[pos]
        pos += 1
        out += (b & 0x7F) * mult
        if not (b & 0x80):
            return out, pos
        mult *= 128


def parse_top(buf: bytes):
    """返回 [(field_no, wire_type, value)]；只接受 wire 0/2。"""
    fields = []
    pos = 0
    while pos < len(buf):
        tag, pos = read_varint(buf, pos)
        fn, wt = tag >> 3, tag & 7
        if wt == 0:
            v, pos = read_varint(buf, pos)
            fields.append((fn, wt, v))
        elif wt == 2:
            ln, pos = read_varint(buf, pos)
            if pos + ln > len(buf):
                raise ValueError("length-delimited 越界")
            fields.append((fn, wt, buf[pos:pos + ln]))
            pos += ln
        else:
            raise ValueError("不支持的 wire type %d" % wt)
    return fields


def unpack_varints(buf: bytes):
    out, pos = [], 0
    while pos < len(buf):
        v, pos = read_varint(buf, pos)
        out.append(v)
    return out


def find_radar_expected(scenario_path):
    data = json.load(open(scenario_path, encoding="utf-8"))
    for step in data["steps"]:
        if step.get("topic") == "RadarInfoToClient":
            first = step["data"]["radar_single_robot_info"][0]
            return (int(first["target_pos_x"]), int(first["target_pos_y"]),
                    int(first["is_high_light"]))
    raise SystemExit("场景里没有 RadarInfoToClient: %s" % scenario_path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, required=True)
    ap.add_argument("--mode", choices=["nested", "flat"], required=True)
    ap.add_argument("--scenario", required=True)
    ap.add_argument("--timeout", type=float, default=15.0)
    args = ap.parse_args()

    exp = find_radar_expected(args.scenario)

    sock = socket.create_connection(("127.0.0.1", args.port), timeout=args.timeout)
    sock.settimeout(args.timeout)

    # CONNECT（MQTT 3.1.1, clean session）
    body = enc_str("MQTT") + bytes([4, 0x02]) + struct.pack("!H", 30) + enc_str("radar-probe")
    sock.sendall(mqtt_packet(0x10, body))
    first, _ = read_packet(sock)
    if first >> 4 != 2:
        raise SystemExit("CONNACK 失败: 0x%02x" % first)

    # SUBSCRIBE RadarInfoToClient qos1
    body = struct.pack("!H", 1) + enc_str("RadarInfoToClient") + bytes([1])
    sock.sendall(mqtt_packet(0x82, body))
    first, _ = read_packet(sock)
    if first >> 4 != 9:
        raise SystemExit("SUBACK 失败: 0x%02x" % first)

    # 等 PUBLISH
    payload = None
    deadline = time.time() + args.timeout
    while time.time() < deadline:
        first, body = read_packet(sock)
        ptype = first >> 4
        if ptype == 3:  # PUBLISH
            qos = (first >> 1) & 0x03
            tlen = struct.unpack("!H", body[:2])[0]
            topic = body[2:2 + tlen].decode()
            pos = 2 + tlen
            pid = None
            if qos > 0:
                pid = struct.unpack("!H", body[pos:pos + 2])[0]
                pos += 2
            if topic == "RadarInfoToClient":
                payload = body[pos:]
                if qos == 1:
                    sock.sendall(mqtt_packet(0x40, struct.pack("!H", pid)))
                break
        elif ptype == 13:  # PINGRESP
            continue
    sock.close()
    if payload is None:
        print("❌ 超时未收到 RadarInfoToClient PUBLISH")
        return 1

    fields = parse_top(payload)
    if args.mode == "nested":
        elems = [v for (fn, wt, v) in fields if fn == 1 and wt == 2]
        if len(elems) != 8:
            print("❌ nested：字段 1 应出现 8 次（8 个目标），实际 %d 次" % len(elems))
            return 1
        first_vals = None
        for e in elems:
            sub = parse_top(e)
            if [f[0] for f in sub] != [1, 2, 3] or any(f[1] != 0 for f in sub):
                print("❌ nested：子消息字段不是 1/2/3 varint: %r" % (sub,))
                return 1
            vals = (sub[0][2], sub[1][2], sub[2][2])
            if first_vals is None:
                first_vals = vals
        if first_vals != exp:
            print("❌ nested：第 1 个目标值 %r != 场景期望 %r" % (first_vals, exp))
            return 1
        print("✅ nested：8 个子消息 / 每个 3 个 uint32，共 %d 字节，首目标 %r" %
              (len(payload), first_vals))
        return 0

    # flat：字段 1 只出现一次，payload 为 24 个 packed varint
    elems = [v for (fn, wt, v) in fields if fn == 1 and wt == 2]
    if len(elems) != 1:
        print("❌ flat：字段 1 应只出现 1 次（packed），实际 %d 次" % len(elems))
        return 1
    vals = unpack_varints(elems[0])
    if len(vals) != 24:
        print("❌ flat：packed 载荷应为 24 个 uint32（8×3），实际 %d 个" % len(vals))
        return 1
    if tuple(vals[:3]) != exp:
        print("❌ flat：第 1 个目标值 %r != 场景期望 %r" % (tuple(vals[:3]), exp))
        return 1
    print("✅ flat：字段 1 packed / 24 个 uint32，共 %d 字节，首目标 %r" %
          (len(payload), tuple(vals[:3])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
