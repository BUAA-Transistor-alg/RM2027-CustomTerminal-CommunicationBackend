#!/usr/bin/env bash
# 兼容旧调用的薄壳：真正的实现在 third_party/fetch_protobuf.sh
# （下载 + sha256 校验 + 编译安装 protobuf 到 third_party/protobuf-21.12）
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$HERE/../third_party/fetch_protobuf.sh" "$@"
