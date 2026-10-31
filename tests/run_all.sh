#!/usr/bin/env bash
# 一键跑全部验证（官方协议 V1.0.0）。任何一项失败都会以非 0 退出。
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
cd "$ROOT"

PASS=0; FAIL=0
run() {
    local name="$1"; shift
    echo
    echo "################################################################"
    echo "# $name"
    echo "################################################################"
    if "$@"; then
        echo ">>> ✅ $name 通过"; PASS=$((PASS+1))
    else
        echo ">>> ❌ $name 失败"; FAIL=$((FAIL+1))
    fi
}

run "对外 API: examples/api_demo (只用公开接口)"          bash "$HERE/run_api.sh"
run "纯 C 接口: librm2027c.so + Python ctypes"           bash "$HERE/run_capi.sh"
run "PyQt5 控制面板 (离屏渲染)"                          bash "$HERE/run_gui.sh"
run "协议: C++ 服务器 ↔ C++ 客户端 (V1.0.0 全部 32 条)"  bash "$HERE/run_protocol.sh"
run "连接健壮性: 自动重试 + 断线重连 + 重订阅"        bash "$HERE/run_reconnect.sh"
run "交叉验证: C++ 服务器 ↔ Node(mqtt.js+protobufjs)"   bash "$HERE/run_crosscheck.sh"
run "图传: 编码->分片->UDP->重组->解码 (画面逐像素校验)"   bash "$HERE/run_video.sh"
run "图传接口: 纯 C + Python(ctypes/numpy)"              bash "$HERE/run_video_capi.sh"
run "图传压力测试: 18 种编码方式 / 故障注入"              bash "$HERE/stress_video.sh"
run "雷达线格式: nested / flat 双模式 + flat 端到端"      bash "$HERE/run_radar_wire.sh"
run "端到端一体 (协议 + 图传)"                          bash "$HERE/run_integrated.sh"

echo
echo "================================================================"
echo "总计: 通过 $PASS 项, 失败 $FAIL 项"
echo "================================================================"
[[ $FAIL -eq 0 ]]
