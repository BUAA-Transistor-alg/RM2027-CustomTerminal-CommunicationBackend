#!/usr/bin/env bash
# 协议层验证：C++ 服务器(自带 MQTT broker) ↔ C++ 客户端，覆盖官方 V1.0.0 全部 32 条消息
#   下行 23 条：服务器按场景发布 -> 客户端接收解码 -> 逐字段比对
#   上行 9 条：客户端按场景发送 -> 服务器接收解码 -> 逐字段比对
#
# 用法: tests/run_protocol.sh [host] [port] [radar-wire-format]
#   radar-wire-format: nested（默认）| flat —— 见 README「雷达线格式双模式」
#   JSON 记录始终是嵌套形式，所以两种线格式下 check_records.py 的期望文件相同。
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
OUT="$ROOT/tests/out"; mkdir -p "$OUT"

HOST="${1:-127.0.0.1}"
PORT="${2:-13400}"
RADAR_MODE="${3:-nested}"

echo "=== 协议验证: C++ 服务器 ↔ C++ 客户端 (V1.0.0, $HOST:$PORT) ==="

"$ROOT/build/rm_server" \
    --host "$HOST" --port "$PORT" --radar-wire-format "$RADAR_MODE" \
    --scenario "$ROOT/tests/scenarios/all_down.json" \
    --interval 50 --start-after 1 --wait-clients 15000 \
    > "$OUT/protocol_server.out" 2> "$OUT/protocol_server.err" &
SERVER_PID=$!
cleanup() { kill "$SERVER_PID" 2>/dev/null; wait "$SERVER_PID" 2>/dev/null; }
trap cleanup EXIT
sleep 0.8
kill -0 "$SERVER_PID" 2>/dev/null || { echo "❌ 服务器启动失败"; tail -20 "$OUT/protocol_server.err"; exit 1; }

"$ROOT/build/rm_client" \
    --host "$HOST" --port "$PORT" --client-id 3 --radar-wire-format "$RADAR_MODE" \
    --subscribe all \
    --publish-scenario "$ROOT/tests/scenarios/all_up.json" \
    --publish-interval 50 --duration 9 \
    > "$OUT/protocol_client.out" 2> "$OUT/protocol_client.err"
echo "客户端已退出"

sleep 0.4
cleanup; trap - EXIT

python3 "$HERE/check_records.py" \
    --expect-down "$ROOT/tests/scenarios/all_down.json" \
    --expect-up "$ROOT/tests/scenarios/all_up.json" \
    --client "$OUT/protocol_client.out" \
    --server "$OUT/protocol_server.out"
RC=$?
echo "记录: $OUT/protocol_client.out / $OUT/protocol_server.out"
exit $RC
