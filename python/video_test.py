#!/usr/bin/env python3
"""Python 图传接口测试：numpy 进出、统计、解码格式模式切换、回调。"""
from __future__ import annotations

import sys
import time

import numpy as np

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import rm2027c as rm

FAIL = 0


def check(cond, msg):
    global FAIL
    print(("  ✅ " if cond else "  ❌ ") + msg)
    if not cond:
        FAIL += 1
    return cond


def pattern(w, h, frame):
    """和 C++ 测试工具一致的合成图案，便于校验画面。"""
    bars = [(255, 255, 255), (0, 255, 255), (255, 255, 0), (0, 255, 0),
            (255, 0, 255), (0, 0, 255), (255, 0, 0), (64, 64, 64)]
    xs = np.arange(w)
    idx = (xs // (w // 8)) % 8
    img = np.zeros((h, w, 3), np.float64)
    for i, c in enumerate(bars):
        img[:, idx == i] = c
    dim = np.full(h, 255.0)
    half = h // 2
    dim[half:] = 255.0 - (np.arange(h - half) * 255 // half)
    img = (img * dim[:, None, None] / 255.0).astype(np.uint8)
    bw, bh = w // 8, h // 8
    img[(frame * 3) % (h - bh):(frame * 3) % (h - bh) + bh,
        (frame * 7) % (w - bw):(frame * 7) % (w - bw) + bw] = 255
    return img


def main():
    print("== Python 图传接口测试 ==")
    print("  库版本:", rm.version())

    W, H, FPS = 640, 360, 30
    PORT = 14850

    rx = rm.VideoReceiver(port=PORT, codec_mode=rm.CODEC_AUTO)
    check(rx.codec_mode == "auto", "默认解码格式模式 = auto（探针自动）")
    check(rx.start() is rx, "接收端启动")
    check(rx.running, "接收端 running")

    tx = rm.VideoSender("127.0.0.1", PORT, width=W, height=H, fps=FPS,
                        bitrate_kbps=2000, gop_size=15)
    check(tx.start() is tx, "发送端启动")

    # ---- 收发交错 ----
    frames, sent, worst = [], 0, 0.0
    t0 = time.time()
    while time.time() - t0 < 2.5:
        if sent < 60 and (time.time() - t0) > sent / FPS:
            tx.send_frame(pattern(W, H, sent))
            sent += 1
        f = rx.get_frame()
        if f is not None:
            frames.append(f)
            expect = pattern(W, H, max(0, f.frame_number - 1))
            mad = float(np.abs(f.image.astype(int) - expect.astype(int)).mean())
            worst = max(worst, mad)
        time.sleep(0.003)
    tx.flush()

    check(sent >= 60, "发送 %d 帧" % sent)
    check(len(frames) > 20, "取到 %d 帧解码画面" % len(frames))
    if frames:
        check(frames[0].image.shape == (H, W, 3), "图像形状 %s" % (frames[0].image.shape,))
        check(frames[0].image.dtype == np.uint8, "图像 dtype = uint8（BGR24）")
        check(worst < 12.0, "画面与发送图案一致（最差平均绝对误差 %.2f）" % worst)

    st = rx.stats
    print("     接收端统计:", st)
    check(st.frames_complete > 20, "收帧统计正常")
    check(st.frames_decoded > 20, "解码统计正常")
    check(st.codec == "h265", "自动模式探测到 h265")
    check(st.codec_mode == "auto", "统计里 codecMode = auto")

    txs = tx.stats
    print("     发送端统计:", txs)
    check(txs.frames >= 60, "发送端统计正常")

    # ---- 运行中切换解码格式模式 ----
    check(rx.set_codec_mode(rm.CODEC_H265) is None and rx.codec_mode == "h265",
          "运行中切换到手动固定 h265")
    check(rx.stats.codec == "h265", "固定 h265 后仍正常解码")
    rx.set_codec_mode(rm.CODEC_AUTO)
    check(rx.codec_mode == "auto", "切回自动探测")
    try:
        rx.set_codec_mode("bogus")
        check(False, "非法模式应报错")
    except RuntimeError as e:
        check(True, "非法模式被拒绝：%s" % str(e)[:40])

    # ---- 逐帧回调 ----
    cb_frames = []
    rx.set_frame_handler(lambda f: cb_frames.append(f.frame_number))
    time.sleep(0.1)
    for i in range(30):
        tx.send_frame(pattern(W, H, 100 + i))
        time.sleep(1.0 / FPS)
    tx.flush()
    time.sleep(0.4)
    rx.set_frame_handler(None)
    check(len(cb_frames) > 0, "逐帧回调收到 %d 帧" % len(cb_frames))

    # ---- 配置接口 ----
    tx.stop(); rx.stop()
    tx.config(fps=15, bitrate_kbps=1000)
    rx.config(frame_timeout_ms=500, au_queue_depth=32)
    check(True, "停止后可改配置")
    try:
        tx.send_frame(pattern(W, H, 0))
        check(False, "停止后发送应报错")
    except RuntimeError:
        check(True, "停止后发送被拒绝")

    tx.close(); rx.close()
    check(True, "资源释放")
    print()
    print("✅ Python 图传接口测试全部通过" if FAIL == 0 else "❌ 有 %d 项失败" % FAIL)
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
