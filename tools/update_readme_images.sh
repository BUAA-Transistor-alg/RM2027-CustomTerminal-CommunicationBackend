#!/usr/bin/env bash
# 把测试跑出来的界面截图同步进 docs/images/，供 README 引用。
#
# 为什么需要这个：
#   README 里的图必须入库，而截图是 python/out/ 下的测试产物（该目录不入库）。
#   所以定稿的截图放在 docs/images/，测试重跑后用它同步一次。
#
# 用法:
#   tools/update_readme_images.sh            # 同步
#   tools/update_readme_images.sh --check    # 只检查是否有差异（CI 可用），有差异返回 1
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
SRC="$ROOT/python/out"
DST="$ROOT/docs/images"

# README 里用到的图（改这里就够，README 的路径规则是 docs/images/<名字>）
IMAGES=(
    gui_server_live.png     # 服务器面板（浅色，含发送图传）
    gui_client_live.png     # 客户端面板（浅色，含接收图传）
    gui_server_dark.png     # 深色主题
)

CHECK=0
[[ "${1:-}" == "--check" ]] && CHECK=1

mkdir -p "$DST"
missing=0
changed=0

for name in "${IMAGES[@]}"; do
    if [[ ! -f "$SRC/$name" ]]; then
        echo "❌ 缺少测试产物: python/out/$name"
        echo "   先跑一次界面测试：bash tests/run_gui.sh"
        missing=1
        continue
    fi
    if [[ -f "$DST/$name" ]] && cmp -s "$SRC/$name" "$DST/$name"; then
        echo "  = $name（无变化）"
        continue
    fi
    if [[ $CHECK -eq 1 ]]; then
        echo "  ✗ $name 与测试产物不一致"
        changed=1
    else
        cp -f "$SRC/$name" "$DST/$name"
        echo "  ↑ $name 已更新"
    fi
done

if [[ $missing -eq 1 ]]; then
    exit 1
fi
if [[ $CHECK -eq 1 ]]; then
    if [[ $changed -eq 1 ]]; then
        echo
        echo "docs/images/ 与最新测试产物不一致。"
        echo "如果这是有意更新（界面改了），跑一次 tools/update_readme_images.sh 并提交。"
        exit 1
    fi
    echo
    echo "✅ README 图片与测试产物一致"
    exit 0
fi

echo
echo "✅ 已同步到 docs/images/（记得 git add 后提交）"
echo "   提示：图片变了通常是界面改了，请顺便确认 README 里的描述还对得上。"
