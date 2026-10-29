#!/usr/bin/env bash
# 纯 C 接口验证：通过 Python ctypes 调用 librm2027c.so
#   - 元数据（32 条指令 = 上行 9 + 下行 23、官方频率、字段表）
#   - 双向收发、嵌套消息、bytes 字段、字段名统一为官方 snake_case
#   - 错误处理
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"

if [[ ! -f "$ROOT/build/librm2027c.so" ]]; then
    echo "❌ 找不到 librm2027c.so，请先构建"
    exit 1
fi
echo "=== 纯 C 接口 (librm2027c.so + ctypes) ==="
python3 "$ROOT/python/capi_test.py" 2>/dev/null
