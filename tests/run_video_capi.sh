#!/usr/bin/env bash
# 图传接口验证：纯 C 接口 + Python(ctypes) 接口
#   * 纯 C：不用任何 C++ 语法，验证 librm2027c.so 的图传接口完整可用
#   * Python：验证 numpy BGR 进出、统计、解码格式模式切换、逐帧回调
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
OUT="$ROOT/tests/out"; mkdir -p "$OUT"

echo "=== 1/2 纯 C 接口（librm2027c.so 图传）==="
gcc -O1 -o "$ROOT/build/capi_video_test" "$HERE/capi_video_test.c" \
    -I"$ROOT/include" -L"$ROOT/build" -lrm2027c -Wl,-rpath,"$ROOT/build" || {
    echo "❌ 编译 C 测试失败"; exit 1; }
if ! timeout 90 "$ROOT/build/capi_video_test" 2>/dev/null; then
    echo ">>> ❌ 纯 C 图传接口测试失败"; exit 1
fi

echo
echo "=== 2/2 Python 接口（ctypes + numpy）==="
if ! timeout 120 python3 "$ROOT/python/video_test.py" 2>/dev/null; then
    echo ">>> ❌ Python 图传接口测试失败"; exit 1
fi
