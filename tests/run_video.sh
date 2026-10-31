#!/usr/bin/env bash
# UDP 图传验证：C++ 编码发送 -> UDP 分片 -> 重组 -> C++ 解码 -> 逐像素比对
#
# 新的图传链路两端都只跟「图片」打交道，编解码都在 C++ 内部完成。
# 判据是**画面**：解码出来的帧必须和发送端生成的合成图案一致。
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
OUT="$ROOT/tests/out"; mkdir -p "$OUT"
PORT="${1:-14300}"
DUR=3

rm -f "$OUT/vid_"*.ppm "$OUT/vid_"*.out "$OUT/vid_"*.err

echo "=== UDP 图传验证（端口 $PORT）==="
# 官方 2027 V1.0.0 §2 明文规定 8 字节包头为大端，本实现只支持大端（无开关）。
echo "包头: 大端（官方 2027 V1.0.0 §2 规定；实现固定大端，无字节序开关）"

"$ROOT/build/rm_video_rx" --port "$PORT" --duration 5 --stats-interval 0 \
    --dump "$OUT/vid" --dump-every 12 --dump-max 4 \
    > "$OUT/vid_rx.out" 2> "$OUT/vid_rx.err" &
RX=$!
sleep 0.7
"$ROOT/build/rm_video_tx" --port "$PORT" --width 1280 --height 720 --fps 30 \
    --bitrate 4000 --gop 30 --duration "$DUR" > "$OUT/vid_tx.out" 2>/dev/null
wait $RX 2>/dev/null

echo "--- 接收端统计 ---"
tail -1 "$OUT/vid_rx.out" | python3 -c "
import json,sys
d=json.load(sys.stdin); rx=d['rx']; dec=d['decode']
print('  codec=%s %dx%d 收帧=%d(完整) 不完整=%d 解码=%d 错误=%d' % (
    d['codec'], d['width'], d['height'], rx['framesComplete'], rx['framesIncomplete'],
    dec['frames'], dec['errors']))
"

# 画面校验
echo "--- 画面校验（与发送端合成图案逐像素比对）---"
if ! python3 "$HERE/check_video_pattern.py" "$OUT/vid_"*.ppm; then
    echo ">>> ❌ 图传画面校验失败"; exit 1
fi

# 统计判据
python3 - "$OUT/vid_rx.out" <<'PY'
import json, sys
lines = [l for l in open(sys.argv[1]) if l.strip().startswith('{')]
d = json.loads(lines[-1]) if lines else {}
rx, dec = d.get('rx', {}), d.get('decode', {})
ok = True
if rx.get('framesIncomplete', 1) != 0:
    print("  ❌ 有不完整帧: %s" % rx.get('framesIncomplete')); ok = False
else:
    print("  ✅ 无丢包、无不完整帧")
if dec.get('frames', 0) < 30:
    print("  ❌ 解码帧数过少: %s" % dec.get('frames')); ok = False
else:
    print("  ✅ 解码 %d 帧" % dec['frames'])
if dec.get('errors', 1) != 0:
    print("  ❌ 解码错误: %s" % dec.get('errors')); ok = False
else:
    print("  ✅ 解码零错误")
sys.exit(0 if ok else 1)
PY
