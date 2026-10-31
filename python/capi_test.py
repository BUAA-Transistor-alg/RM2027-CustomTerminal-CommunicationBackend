#!/usr/bin/env python3
"""纯 C 接口（经 ctypes）的功能验证：元数据 + 双向收发 + 嵌套消息 + 字段名 + 协议约束。"""
import base64
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rm2027c as rm

PORT = int(os.environ.get("CAPI_TEST_PORT", "13710"))
FAIL = []

# 官方 §2.2.17/2.2.18：MapClick robot_id 固定 7 字节（不足补 0）
ROBOT_ID_7B = base64.b64encode(bytes([1, 2, 3, 0, 0, 0, 0])).decode("ascii")


def check(cond, msg):
    print(("  ✅ " if cond else "  ❌ ") + msg)
    if not cond:
        FAIL.append(msg)


def expect_reject(desc, fn, *keys):
    """发送不合规消息必须抛 RuntimeError，且错误信息里带校验原因关键词。"""
    try:
        fn()
    except RuntimeError as e:
        text = str(e)
        missing = [k for k in keys if k not in text]
        check(not missing,
              "%s 被拒绝，原因含 %s（%s）" % (desc, "/".join(keys), text[:110]))
    else:
        check(False, "%s 应当被拒绝（RuntimeError）" % desc)


def main():
    print("== 元数据（来自官方 PDF 字段表 / 表 2-1）==")
    ms = rm.messages()
    check(len(ms) == 32, "指令总数 32（实际 %d）" % len(ms))
    check(len([m for m in ms if m.is_uplink]) == 9, "上行 9 条")
    check(len([m for m in ms if not m.is_uplink]) == 23, "下行 23 条")
    for gone in ("HeroDeployModeEventCommand", "DeployModeStatusSync",
                 "AirSupportCommand", "AirSupportStatusSync"):
        check(rm.message_info(gone) is None, "2027 已删除指令 %s 不在消息表中" % gone)
    check(rm.message_info("GameStatus").default_hz == 5.0, "GameStatus 官方频率 5Hz")
    check(rm.message_info("KeyboardMouseControl").default_hz == 75.0, "KeyboardMouseControl 75Hz")
    check(rm.message_info("DartCommand").triggered, "DartCommand 为触发式发送")
    check(rm.message_info("DartCommand").max_hz == 10.0, "DartCommand 触发式上限 10Hz")
    check(len(rm.message_info("GameStatus").fields) == 10, "GameStatus 字段数 10")
    check(len(rm.message_info("GlobalUnitStatus").fields) == 16, "GlobalUnitStatus 字段数 16")
    check(len(rm.message_info("RobotModuleStatus").fields) == 11, "RobotModuleStatus 字段数 11")

    print("== 双向收发 ==")
    srv = rm.Server("127.0.0.1", PORT)
    up = []
    srv.on_message(lambda n, d, cid: up.append((n, d, cid)))
    srv.start()

    cli = rm.Client("127.0.0.1", PORT, "3")
    down = []
    cli.on_message(lambda n, d, cid: down.append((n, d)))
    cli.start()
    # start() 现在立即返回（后台异步连接），轮询等它连上
    t0 = time.time()
    while time.time() - t0 < 5 and not cli.connected:
        time.sleep(0.02)
    check(cli.connected, "客户端已连接")

    cli.send("DartCommand", {"target_id": 1, "open": True, "launch_confirm": True})
    cli.send("CommonCommand", {"cmd_type": 1, "param": 20})
    cli.send("MapClickCmd", {"is_send_all": 2, "robot_id": ROBOT_ID_7B, "mode": 1,
                             "enemy_id": 11, "map_x": 1.5, "map_y": -2.5})

    # 等客户端把「要发的这几个 topic」订阅好再发。客户端是逐条订阅的，
    # subscribed_client_count>0 只代表第一个 topic 订好了，不代表这几个都订好。
    for _topic in ("GameStatus", "RuneStatusSync", "RobotModuleStatus", "RadarInfoToClient",
                   "GlobalUnitStatus", "RobotDynamicStatus"):
        t0 = time.time()
        while time.time() - t0 < 5 and srv.topic_subscriber_count(_topic) == 0:
            time.sleep(0.02)
        check(srv.topic_subscriber_count(_topic) >= 1, "服务器检测到 %s 已订阅" % _topic)

    srv.send("GameStatus", {"current_round": 1, "total_rounds": 3, "current_stage": 4,
                            "red_score": 123, "game_result": 255})
    srv.send("RuneStatusSync", {"rune_status": 2, "activated_arms": 3, "average_rings": 4.5})
    srv.send("RobotModuleStatus", {"power_manager": 1, "main_controller": 2,
                                   "modular_42mm": 1,
                                   "video_transmission": 2})
    srv.send("GlobalUnitStatus", {"base_shield": 2, "outpost_R_health": 4200,
                                  "outpost_B_status": 3, "total_CP_ally": 77})
    srv.send("RobotDynamicStatus", {"current_health": 500, "reserved": 1})
    # 官方 §2.2.19：雷达每方 1/3/4/7 号，共 8 个目标（发送路径校验 8 个）
    radar_targets = [{"target_pos_x": 100 + i, "target_pos_y": 200 + i,
                      "is_high_light": i % 2} for i in range(8)]
    srv.send("RadarInfoToClient", {"radar_single_robot_info": radar_targets})
    time.sleep(1.0)

    names_up = [n for n, _, _ in up]
    for want in ("DartCommand", "CommonCommand", "MapClickCmd"):
        check(want in names_up, "服务器收到上行 %s" % want)
    check(all(cid == "3" for _, _, cid in up), "上行回调带上了发送方 clientID=3")

    got = dict(down)
    for want in ("GameStatus", "RuneStatusSync", "RobotModuleStatus", "RadarInfoToClient",
                 "GlobalUnitStatus", "RobotDynamicStatus"):
        check(want in got, "客户端收到下行 %s" % want)

    print("== 字段名与取值 ==")
    check(got["GameStatus"]["red_score"] == 123, "GameStatus.red_score = 123（snake_case）")
    check(got["GameStatus"]["game_result"] == 255, "game_result = 255")
    check(abs(got["RuneStatusSync"]["average_rings"] - 4.5) < 1e-6, "float 字段 average_rings = 4.5")
    check(got["RobotModuleStatus"]["modular_42mm"] == 1, "2027 新字段 modular_42mm（仅重装，0/1）")
    check("laser_detection_module" not in got["RobotModuleStatus"],
          "旧字段 laser_detection_module 已不存在")
    gu = got["GlobalUnitStatus"]
    check(gu["outpost_R_health"] == 4200 and gu["total_CP_ally"] == 77,
          "GlobalUnitStatus 2027 字段 outpost_R_health / total_CP_ally")
    check("outpost_health" not in gu and "outpost_status" not in gu,
          "旧字段 outpost_health / outpost_status 已不存在")
    rd = got["RobotDynamicStatus"]
    check("reserved" in rd and "current_chassis_energy" not in rd,
          "RobotDynamicStatus 字段 4 = reserved（旧字段 current_chassis_energy 已删除）")
    rinfo = got["RadarInfoToClient"]["radar_single_robot_info"]
    check(len(rinfo) == 8, "嵌套 repeated 8 个元素（官方雷达 8 目标）")
    check(rinfo[0]["target_pos_x"] == 100 and rinfo[0]["target_pos_y"] == 200
          and rinfo[0]["is_high_light"] == 0, "首元素字段值正确")
    check(rinfo[-1]["target_pos_x"] == 107 and rinfo[-1]["target_pos_y"] == 207
          and rinfo[-1]["is_high_light"] == 1, "末元素字段值正确")

    print("== bytes 字段 ==")
    mp = {n: d for n, d, _ in up}.get("MapClickCmd", {})
    rid_roundtrip = base64.b64decode(mp.get("robot_id", ""))
    check(mp.get("robot_id") == ROBOT_ID_7B and len(rid_roundtrip) == 7,
          "bytes robot_id 往返一致且为 7 字节（base64 %s -> %s）"
          % (ROBOT_ID_7B, rid_roundtrip.hex()))

    print("== 协议约束校验（只在发送路径生效）==")
    # a) CustomControl.data 上限 60 字节：61 字节必须被拒绝
    cc61 = base64.b64encode(b"\x3d" * 61).decode("ascii")
    expect_reject("CustomControl.data 61 字节",
                  lambda: cli.send("CustomControl", {"data": cc61}),
                  "CustomControl", "长度", "61", "60")
    # b) MapClickCmd.robot_id 固定 7 字节：6 字节必须被拒绝
    bad_rid = base64.b64encode(bytes([1, 2, 3, 0, 0, 0])).decode("ascii")
    expect_reject("MapClickCmd.robot_id 6 字节",
                  lambda: cli.send("MapClickCmd", {"robot_id": bad_rid}),
                  "robot_id", "长度", "6", "7")
    # c) RadarInfoToClient 非空必须 8 个目标：7 个必须被拒绝
    expect_reject("RadarInfoToClient 7 个元素",
                  lambda: srv.send("RadarInfoToClient", {"radar_single_robot_info": [
                      {"target_pos_x": i} for i in range(7)]}),
                  "radar_single_robot_info", "长度", "7", "8")
    # d) 边界值 60 字节必须成功（并真的到达服务器）
    cc60 = base64.b64encode(b"\x3c" * 60).decode("ascii")
    cli.send("CustomControl", {"data": cc60})
    t0 = time.time()
    while time.time() - t0 < 3 and not any(
            n == "CustomControl" and d.get("data") == cc60 for n, d, _ in up):
        time.sleep(0.02)
    check(any(n == "CustomControl" and d.get("data") == cc60 for n, d, _ in up),
          "CustomControl.data 60 字节边界值发送成功")
    # 被拒绝的消息不能「报错但已发出」：对端各只应收到 1 条合法消息
    check(not any(n == "CustomControl" and d.get("data") == cc61 for n, d, _ in up),
          "被拒绝的 61 字节 CustomControl 未到达服务器")
    check(sum(1 for n, _, _ in up if n == "MapClickCmd") == 1,
          "被拒绝的 6 字节 robot_id MapClickCmd 未到达服务器（服务器只收到 1 条）")
    check(sum(1 for n, _ in down if n == "RadarInfoToClient") == 1,
          "被拒绝的 7 元素 RadarInfoToClient 未到达客户端（客户端只收到 1 条）")

    print("== 错误处理 ==")
    try:
        cli.send("NoSuchMessage", {})
        check(False, "未知指令应当报错")
    except RuntimeError as e:
        check("NoSuchMessage" in str(e) or "未知" in str(e), "未知指令被拒绝: %s" % str(e)[:60])

    print()
    print("统计: 服务器 收%d 发%d | 客户端 收%d 发%d" % (srv.received, srv.sent,
                                                       cli.received, cli.sent))
    cli.close()
    srv.close()

    if FAIL:
        print("❌ C API 测试失败 %d 项:" % len(FAIL))
        for f in FAIL:
            print("   -", f)
        return 1
    print("✅ 纯 C 接口（ctypes）验证通过")
    return 0


if __name__ == "__main__":
    sys.exit(main())
