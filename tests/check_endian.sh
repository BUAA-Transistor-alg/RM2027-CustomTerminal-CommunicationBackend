#!/usr/bin/env bash
# UDP 包头字节序编译开关验证
#
# 官方 V1.0.0 明文规定 8 字节包头「为大端传输」。为了确认这个约定在代码里
# 真的生效（而不是碰巧两边一致），编译一个**小端包头**的版本交叉验证：
#   * 小端发 -> 小端收：应当正常（同版本内部自洽）
#   * 大端发 -> 小端收：应当收不到完整帧（证明字节序确实参与了解析）
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
OUT="$ROOT/tests/out"; mkdir -p "$OUT"

PORT_LE=$((14600 + RANDOM % 100))
PORT_MIX=$((14700 + RANDOM % 100))

echo "=== 编译小端包头版本（build-le）==="
cmake -S "$ROOT" -B "$ROOT/build-le" -DRM_UDP_HEADER_BIG_ENDIAN=OFF > "$OUT/le_cmake.log" 2>&1 || {
    echo "❌ cmake 配置失败"; tail -5 "$OUT/le_cmake.log"; exit 1; }
cmake --build "$ROOT/build-le" --target rm_video_tx rm_video_rx -j"$(nproc)" > "$OUT/le_build.log" 2>&1 || {
    echo "❌ 编译失败"; tail -10 "$OUT/le_build.log"; exit 1; }
echo "  ✅ 小端版本编译完成"

recv_frames() { # 统计文件  -> 打印收到的完整帧数
    python3 - "$1" <<'PY'
import json, sys
lines = [l for l in open(sys.argv[1]) if l.strip().startswith('{')]
d = json.loads(lines[-1]) if lines else {}
print(d.get('rx', {}).get('framesComplete', 0))
PY
}

echo
echo "=== 用例 1：小端发 -> 小端收（应当正常）==="
"$ROOT/build-le/rm_video_rx" --port $PORT_LE --duration 4 --stats-interval 0 \
    > "$OUT/le1.rxout" 2>/dev/null &
RX=$!; sleep 0.6
"$ROOT/build-le/rm_video_tx" --port $PORT_LE --width 640 --height 360 --fps 30 \
    --gop 15 --duration 2 >/dev/null 2>&1
wait $RX 2>/dev/null
N1=$(recv_frames "$OUT/le1.rxout")
if [[ "$N1" -gt 0 ]]; then echo "  ✅ 小端版本内部自洽（收到 $N1 帧）"; else
    echo "  ❌ 小端版本自身都不通"; exit 1; fi

echo
echo "=== 用例 2：大端发 -> 小端收（应当收不到）==="
"$ROOT/build-le/rm_video_rx" --port $PORT_MIX --duration 4 --stats-interval 0 \
    > "$OUT/le2.rxout" 2>/dev/null &
RX=$!; sleep 0.6
"$ROOT/build/rm_video_tx" --port $PORT_MIX --width 640 --height 360 --fps 30 \
    --gop 15 --duration 2 >/dev/null 2>&1
wait $RX 2>/dev/null
N2=$(recv_frames "$OUT/le2.rxout")
if [[ "$N2" -eq 0 ]]; then
    echo "  ✅ 字节序不匹配时确实收不到完整帧 —— 说明包头字节序真的参与解析"
else
    echo "  ❌ 字节序不匹配却收到了 $N2 帧，说明字节序开关没生效"; exit 1
fi

echo
echo "=== 用例 3：大端发 -> 大端收（正式配置，应当正常）==="
"$ROOT/build/rm_video_rx" --port $((PORT_MIX+1)) --duration 4 --stats-interval 0 \
    > "$OUT/le3.rxout" 2>/dev/null &
RX=$!; sleep 0.6
"$ROOT/build/rm_video_tx" --port $((PORT_MIX+1)) --width 640 --height 360 --fps 30 \
    --gop 15 --duration 2 >/dev/null 2>&1
wait $RX 2>/dev/null
N3=$(recv_frames "$OUT/le3.rxout")
if [[ "$N3" -gt 0 ]]; then echo "  ✅ 正式大端配置正常（收到 $N3 帧）"; else
    echo "  ❌ 大端配置不通"; exit 1; fi

rm -rf "$ROOT/build-le"
echo
echo "✅ 字节序开关验证通过（官方约定 = 大端）"
