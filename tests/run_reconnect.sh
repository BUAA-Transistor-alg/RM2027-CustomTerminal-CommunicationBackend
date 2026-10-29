#!/usr/bin/env bash
# 连接健壮性验证：客户端自动重试连接 + 断线重连 + 重订阅完整性
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
pkill -x rm_server 2>/dev/null || true
sleep 0.3
timeout 60 python3 "$ROOT/python/reconnect_test.py" 2>/dev/null
RC=$?
pkill -x rm_server 2>/dev/null || true
exit $RC
