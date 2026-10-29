#!/usr/bin/env bash
# 端到端一体验证：MQTT 协议（32 条）与 UDP 图传**同时**跑，互不干扰
#
#   1) 协议：C++ 服务器(自带 broker) ↔ C++ 客户端，32 条逐字段比对
#   2) 图传：C++ 发送端(合成图案 -> HEVC -> UDP) ↔ C++ 接收端(重组 -> 解码 -> 画面)
#
# 用法: tests/run_integrated.sh [host] [mqtt端口] [图传端口]
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
OUT="$ROOT/tests/out"; mkdir -p "$OUT"

HOST="${1:-127.0.0.1}"
PORT="${2:-13336}"
VIDEO_PORT="${3:-13337}"

echo "=== 端到端一体验证 (MQTT $HOST:$PORT, 图传 $VIDEO_PORT) ==="
rm -f "$OUT/ig_"*.ppm

# ---- 图传接收端先起来（和协议并行）----
"$ROOT/build/rm_video_rx" --port "$VIDEO_PORT" --duration 11 --stats-interval 0 \
    --dump "$OUT/ig" --dump-every 20 --dump-max 3 \
    > "$OUT/integrated_video_rx.out" 2> "$OUT/integrated_video_rx.err" &
VIDEO_RX_PID=$!

# ---- 协议服务器 ----
"$ROOT/build/rm_server" \
    --host "$HOST" --port "$PORT" \
    --scenario "$ROOT/tests/scenarios/all_down.json" \
    --interval 60 --start-after 1 --wait-clients 15000 --linger 2000 \
    > "$OUT/integrated_server.out" 2> "$OUT/integrated_server.err" &
SERVER_PID=$!

cleanup() {
    kill "$SERVER_PID" "$VIDEO_RX_PID" 2>/dev/null
    wait "$SERVER_PID" "$VIDEO_RX_PID" 2>/dev/null
}
trap cleanup EXIT
sleep 0.8

# ---- 图传发送端（与协议同时跑）----
"$ROOT/build/rm_video_tx" --port "$VIDEO_PORT" --width 1280 --height 720 --fps 30 \
    --bitrate 4000 --gop 30 --duration 9 > "$OUT/integrated_video_tx.out" 2>/dev/null &
VIDEO_TX_PID=$!

# ---- 协议客户端（跑 10 秒，期间图传一直在流）----
"$ROOT/build/rm_client" \
    --host "$HOST" --port "$PORT" --client-id 3 \
    --subscribe all \
    --publish-scenario "$ROOT/tests/scenarios/all_up.json" \
    --publish-interval 60 --duration 10 \
    > "$OUT/integrated_client.out" 2> "$OUT/integrated_client.err"
echo "客户端已退出"

wait "$VIDEO_TX_PID" 2>/dev/null
wait "$VIDEO_RX_PID" 2>/dev/null
cleanup
trap - EXIT

echo
echo "--- 1/2 MQTT 协议（32 条逐字段比对）---"
python3 "$HERE/check_records.py" \
    --expect-down "$ROOT/tests/scenarios/all_down.json" \
    --expect-up "$ROOT/tests/scenarios/all_up.json" \
    --client "$OUT/integrated_client.out" \
    --server "$OUT/integrated_server.out"
PROTO_RC=$?

echo
echo "--- 2/2 UDP 图传（与协议同时跑，画面逐像素校验）---"
python3 "$HERE/check_video_pattern.py" "$OUT/ig_"*.ppm
VIDEO_RC=$?

echo
echo "--- 图传统计 ---"
tail -1 "$OUT/integrated_video_rx.out" | python3 -c "
import json,sys
d=json.load(sys.stdin); rx=d['rx']; dec=d['decode']
print('  %s %dx%d 收帧=%d 不完整=%d 解码=%d 错误=%d' % (
    d['codec'], d['width'], d['height'], rx['framesComplete'], rx['framesIncomplete'],
    dec['frames'], dec['errors']))
ok = rx['framesComplete'] > 60 and rx['framesIncomplete'] == 0 and dec['errors'] == 0
sys.exit(0 if ok else 1)
" || VIDEO_RC=1

echo
if [[ $PROTO_RC -eq 0 && $VIDEO_RC -eq 0 ]]; then
    echo "✅ 协议与图传同时在跑，两者都通过"
    exit 0
fi
echo "❌ 协议 rc=$PROTO_RC 图传 rc=$VIDEO_RC"
exit 1
