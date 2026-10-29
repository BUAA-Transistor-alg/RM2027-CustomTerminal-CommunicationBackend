#!/usr/bin/env bash
# PyQt5 控制面板验证（离屏渲染，无需显示器）
#   - 服务器/客户端两个窗口互联
#   - 默认频率取自官方文档；频率 0 -> 「触发发送」按钮
#   - 按频率自动发送、触发发送、停止、接收实时显示
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"

echo "=== PyQt5 控制面板（离屏） ==="
QT_QPA_PLATFORM=offscreen python3 "$ROOT/python/gui_headless_test.py" 2>/dev/null
