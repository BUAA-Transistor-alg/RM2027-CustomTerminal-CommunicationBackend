#!/usr/bin/env bash
# 雷达线格式双模式验证（官方 §2.2.19 字面 vs 嵌套 message）
#   1) nested 模式：抓原始 MQTT PUBLISH，断言字段 1 = 8 个嵌套子消息
#   2) flat   模式：抓原始 MQTT PUBLISH，断言字段 1 = packed 24 个 uint32
#   3) flat 端到端：server + client 都用 flat，跑完整 32 条协议往返（JSON 仍是嵌套形式）
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
OUT="$ROOT/tests/out"; mkdir -p "$OUT"
SCEN="$ROOT/tests/scenarios/all_down.json"

run_server() {
    local mode="$1" port="$2"
    "$ROOT/build/rm_server" --host 127.0.0.1 --port "$port" --scenario "$SCEN" \
        --interval 20 --start-after 1 --wait-clients 15000 --linger 4000 \
        --radar-wire-format "$mode" \
        > "$OUT/radar_$mode.server.out" 2> "$OUT/radar_$mode.server.err" &
    echo $!
}

PASS=0; FAIL=0
check_mode() {
    local mode="$1" port="$2"
    echo "=== 雷达线格式: $mode ==="
    local pid; pid=$(run_server "$mode" "$port")
    sleep 0.6
    if python3 "$HERE/radar_wire_probe.py" --port "$port" --mode "$mode" --scenario "$SCEN"; then
        PASS=$((PASS+1))
    else
        FAIL=$((FAIL+1))
        echo "--- server stderr ---"; tail -20 "$OUT/radar_$mode.server.err"
    fi
    kill "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null
}

check_mode nested 13450
check_mode flat 13451

echo "=== flat 模式端到端（server + client 都是 flat）==="
if bash "$HERE/run_protocol.sh" 127.0.0.1 13452 flat; then
    PASS=$((PASS+1))
else
    FAIL=$((FAIL+1))
fi

echo
if [[ $FAIL -eq 0 ]]; then
    echo "✅ 雷达线格式双模式验证通过（$PASS 项）"
    exit 0
fi
echo "❌ 雷达线格式验证失败（通过 $PASS / 失败 $FAIL）"
exit 1
