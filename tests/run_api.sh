#!/usr/bin/env bash
# 对外 API 验证：examples/api_demo.cpp 只用公开接口（rm2027/api/*.h）
#   服务器：按指令名/结构体类型绑定回调收上行，按名字发下行
#   客户端：按结构体类型绑定回调收下行，按名字发上行
# 判据：双方都收到预期的指令与字段值。
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
OUT="$ROOT/tests/out"; mkdir -p "$OUT"
PORT="${1:-13520}"

echo "=== 对外 API 验证 (examples/api_demo, port $PORT) ==="

"$ROOT/build/rm_api_demo" server "$PORT" > "$OUT/api_server.log" 2> "$OUT/api_server.err" &
SRV=$!
sleep 0.6
"$ROOT/build/rm_api_demo" client "$PORT" > "$OUT/api_client.log" 2> "$OUT/api_client.err"
wait $SRV 2>/dev/null

echo "--- 客户端收到的下行 ---"
cat "$OUT/api_client.log"
echo "--- 服务器收到的上行 ---"
cat "$OUT/api_server.log"

FAIL=0
check() { # 文件 期望子串 说明
    if grep -qF "$2" "$1"; then
        echo "✅ $3"
    else
        echo "❌ $3  (未在 $1 中找到: $2)"
        FAIL=1
    fi
}

echo
echo "--- 判定 ---"
check "$OUT/api_client.log" "[client] GameStatus         stage=4 red=100 blue=80 result=255" "客户端按类型回调收到 GameStatus 且字段正确"
check "$OUT/api_client.log" "[client] RobotPosition      robot_id=3 x=1.5 y=-2.5"              "客户端按类型回调收到 RobotPosition 且字段正确"
check "$OUT/api_client.log" "[client] 统计: 收 6 发 2"                                        "客户端收 6 发 2"
check "$OUT/api_server.log" "[server] DartCommand        target_id=1 open=1 launch_confirm=1" "服务器按类型回调收到 DartCommand 且字段正确"
check "$OUT/api_server.log" "[server] CommonCommand      cmd_type=1 param=20"                 "服务器按名字回调收到 CommonCommand 且字段正确"
check "$OUT/api_server.log" "[server] 统计: 收 2 发 6"                                        "服务器收 2 发 6"

echo
[[ $FAIL -eq 0 ]] && echo "✅ 对外 API 验证通过" || echo "❌ 对外 API 验证失败"
exit $FAIL
