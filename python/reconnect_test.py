#!/usr/bin/env python3
"""连接健壮性测试：自动重试连接 + 断线重连 + 重订阅完整性。

覆盖两个曾经的问题：
  1. 服务端没起时客户端 start() 直接失败（应改为一直后台重试）
  2. 断线重连后部分 topic 漏订，导致「服务端还在发、客户端收不到某些消息」
"""
from __future__ import annotations

import signal
import subprocess
import sys
import threading
import time

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import rm2027c as rm

ROOT = __file__.rsplit("/python/", 1)[0]
PORT = 15210
FAIL = []


def check(cond, msg):
    print(("  ✅ " if cond else "  ❌ ") + msg, flush=True)
    if not cond:
        FAIL.append(msg)


def make_server():
    return subprocess.Popen(
        [ROOT + "/build/rm_server", "--host", "127.0.0.1", "--port", str(PORT),
         "--scenario", ROOT + "/tests/scenarios/all_down.json", "--interval", "40",
         "--repeat", "0", "--start-after", "1", "--wait-clients", "20000"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def main():
    print("== 连接健壮性测试（自动重试 + 重连 + 重订阅）==")

    # ---- 1. 服务端没起时也能启动 ----
    print("-- 1/3 服务端未启动时客户端 start() 不失败 --")
    cli = rm.Client("127.0.0.1", PORT, "3")
    t0 = time.time()
    try:
        cli.start()
    except RuntimeError as e:
        check(False, "服务端没起时 start() 不应抛错：%s" % e)
        cli.close()
        return 1
    check(True, "服务端未启动时 start() 返回正常")
    check(not cli.connected, "此时确实未连接")

    # ---- 2. 服务端起来后自动连上 ----
    print("-- 2/3 服务端起来后自动连接 --")
    srv = make_server()
    t0 = time.time()
    while time.time() - t0 < 15 and not cli.connected:
        time.sleep(0.05)
    check(cli.connected, "服务端起来 %.1fs 后自动连上" % (time.time() - t0))

    topics, lock = set(), threading.Lock()

    def on_msg(name, data, cid):
        with lock:
            topics.add(name)

    cli.on_message(on_msg)

    def wait_all(timeout):
        t0 = time.time()
        while time.time() - t0 < timeout:
            with lock:
                if len(topics) >= 23:
                    return True
            time.sleep(0.05)
        return False

    check(wait_all(6), "首次连接后收齐 23 种下行指令（%d 种）" % len(topics))

    # ---- 3. 断线重连 + 重订阅完整性 ----
    print("-- 3/3 断线重连后重新订阅全部 topic --")
    srv.send_signal(signal.SIGKILL)
    time.sleep(0.8)
    with lock:
        topics.clear()
    srv = make_server()
    t0 = time.time()
    while time.time() - t0 < 15 and not cli.connected:
        time.sleep(0.05)
    check(cli.connected, "重连耗时 %.1fs" % (time.time() - t0))
    check(wait_all(8), "重连后收齐 23 种下行指令（%d 种）" % len(topics))

    srv.kill()
    cli.close()

    print()
    if FAIL:
        print("❌ 连接健壮性测试失败 %d 项:" % len(FAIL))
        for f in FAIL:
            print("   -", f)
        return 1
    print("✅ 连接健壮性测试全部通过")
    return 0


if __name__ == "__main__":
    sys.exit(main())
