#!/usr/bin/env bash
# 交叉验证：C++ 服务器 ↔ Node 客户端脚本（mqtt.js + protobufjs，完全不同的技术栈）
# 目的：用第三方 MQTT 栈与 protobuf 实现验证 C++ 侧的 broker 与编解码是否正确。
#
# 用法: tests/run_crosscheck.sh [host] [port]
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
OUT="$ROOT/tests/out"; mkdir -p "$OUT"

HOST="${1:-127.0.0.1}"
PORT="${2:-13401}"

echo "=== 交叉验证: C++ 服务器 ↔ Node(mqtt.js+protobufjs) 客户端 ($HOST:$PORT) ==="

"$ROOT/build/rm_server" \
    --host "$HOST" --port "$PORT" \
    --scenario "$ROOT/tests/scenarios/all_down.json" \
    --interval 50 --start-after 1 --wait-clients 15000 \
    > "$OUT/cross_server.out" 2> "$OUT/cross_server.err" &
SERVER_PID=$!
cleanup() { kill "$SERVER_PID" 2>/dev/null; wait "$SERVER_PID" 2>/dev/null; }
trap cleanup EXIT
sleep 0.8
kill -0 "$SERVER_PID" 2>/dev/null || { echo "❌ 服务器启动失败"; tail -20 "$OUT/cross_server.err"; exit 1; }

node "$HERE/node/crosscheck.js" \
    --host "$HOST" --port "$PORT" \
    --down "$ROOT/tests/scenarios/all_down.json" \
    --up "$ROOT/tests/scenarios/all_up.json" \
    --duration 8 \
    > "$OUT/cross_client.out" 2> "$OUT/cross_client.err"
echo "Node 客户端已退出"

sleep 0.4
cleanup; trap - EXIT

python3 "$HERE/check_records.py" \
    --expect-down "$ROOT/tests/scenarios/all_down.json" \
    --expect-up "$ROOT/tests/scenarios/all_up.json" \
    --client "$OUT/cross_client.out" \
    --server "$OUT/cross_server.out"
RC=$?
echo "记录: $OUT/cross_client.out / $OUT/cross_server.out"
exit $RC
