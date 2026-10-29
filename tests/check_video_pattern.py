#!/usr/bin/env python3
"""校验 rm_video_rx 落盘的解码帧是否与 rm_video_tx 的合成图案一致。

图案定义必须与 src/video/main_tx.cpp 的 makePattern() 保持一致：
  * 8 条竖向彩条（BGR），按 x/(w/8) 取色
  * 下半屏加纵向渐变：dim = 255 - (y - h/2)*255/(h/2)，上半屏 dim = 255
  * 一个白色方块，位置随帧号移动：x0=(f*7)%(w-w/8), y0=(f*3)%(h-h/8)
发送端帧号 = 图案序号 + 1（帧号从 1 开始）。

用法: check_video_pattern.py <ppm文件...>
"""
import sys

import numpy as np

BARS = [(255, 255, 255), (0, 255, 255), (255, 255, 0), (0, 255, 0),
        (255, 0, 255), (0, 0, 255), (255, 0, 0), (64, 64, 64)]  # BGR


def expected(w, h, frame):
    bar_w = w // 8
    xs = np.arange(w)
    idx = (xs // bar_w) % 8
    img = np.zeros((h, w, 3), np.float64)
    for i, c in enumerate(BARS):
        img[:, idx == i] = c
    dim = np.full(h, 255.0)
    half = h // 2
    dim[half:] = 255.0 - (np.arange(h - half) * 255 // half)
    img = (img * dim[:, None, None] / 255.0).astype(np.uint8)
    box_w, box_h = w // 8, h // 8
    x0 = (frame * 7) % (w - box_w)
    y0 = (frame * 3) % (h - box_h)
    img[y0:y0 + box_h, x0:x0 + box_w] = 255
    return img


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    parts = data.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    rgb = np.frombuffer(parts[3], np.uint8).reshape(h, w, 3)
    return rgb[:, :, ::-1].copy()  # RGB -> BGR


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    fail = 0
    for p in argv:
        try:
            fno = int(p.rsplit("_f", 1)[1].split(".")[0])
        except (IndexError, ValueError):
            print("跳过（文件名里没有帧号）:", p)
            continue
        got = read_ppm(p)
        h, w = got.shape[:2]
        exp = expected(w, h, fno - 1)
        mad = float(np.abs(got.astype(int) - exp.astype(int)).mean())
        ok = mad < 12.0   # 有损压缩，允许一定误差
        print("%-26s %dx%d 帧号=%-4d 平均绝对误差=%5.2f  %s"
              % (p.rsplit("/", 1)[-1], w, h, fno, mad, "✅" if ok else "❌"))
        if not ok:
            fail = 1
    mads = []
    bad = 0
    for p in argv:
        try:
            fno = int(p.rsplit("_f", 1)[1].split(".")[0])
        except (IndexError, ValueError):
            continue
        got = read_ppm(p)
        h, w = got.shape[:2]
        mads.append(float(np.abs(got.astype(int) - expected(w, h, fno - 1).astype(int)).mean()))
    if mads:
        print("SUMMARY count=%d min=%.2f max=%.2f bad=%d"
              % (len(mads), min(mads), max(mads), sum(1 for m in mads if m >= 12.0)))
    else:
        print("SUMMARY count=0 min=0 max=0 bad=0")
    print()
    print("✅ 解码画面与发送图案一致" if not fail else "❌ 画面校验失败")
    return fail


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
