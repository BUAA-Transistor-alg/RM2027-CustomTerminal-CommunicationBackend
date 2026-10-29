#!/usr/bin/env bash
# 下载 + 校验 + 编译安装 protobuf 到本目录（third_party/protobuf-21.12），无需 sudo。
#
# 为什么是这个版本（v21.12 = 3.21.12）：
#   * 官方 V1.0.0 协议用了 proto3 的 optional 标签，需要 protobuf >= 3.15；
#     系统自带的 3.12.4 编不过。
#   * v21.12 是最后一个【不依赖 abseil】的版本 —— CMake 构建干净，不用再拉一堆依赖。
#     v22+ 起必须连带构建 abseil，复杂度和体积都大得多。
#   * 必须开 -fPIC：静态库要被链进 librm2027c.so（Python ctypes 要 dlopen 它）。
#
# 用法:
#   third_party/fetch_protobuf.sh              # 装到 third_party/protobuf-21.12
#   third_party/fetch_protobuf.sh <安装前缀>
#   RM_PROTOBUF_TARBALL=/path/to.tar.gz third_party/fetch_protobuf.sh   # 用本地包，跳过下载
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"

VER=21.12
TARBALL_NAME="protobuf-cpp-3.21.12.tar.gz"
SRCDIR="protobuf-3.21.12"
# 官方发布包的 sha256（v21.12）
SHA256="4eab9b524aa5913c6fffb20b2a8abf5ef7f95a80bc0701f3a6dbb4c607f73460"
URL="https://github.com/protocolbuffers/protobuf/releases/download/v${VER}/${TARBALL_NAME}"

PREFIX="${1:-$HERE/protobuf-21.12}"
WORK="$HERE/build-protobuf"

# 已经装好就不重复折腾
if [[ -x "$PREFIX/bin/protoc" && -f "$PREFIX/lib/libprotobuf.a" ]]; then
    echo "==> 已经装好了: $PREFIX"
    "$PREFIX/bin/protoc" --version
    exit 0
fi

mkdir -p "$WORK"
cd "$WORK"

# ---- 1. 取得源码包 ----
if [[ -n "${RM_PROTOBUF_TARBALL:-}" ]]; then
    echo "==> 使用本地源码包: $RM_PROTOBUF_TARBALL"
    cp -f "$RM_PROTOBUF_TARBALL" "$TARBALL_NAME"
elif [[ ! -f "$TARBALL_NAME" ]]; then
    echo "==> 下载 $TARBALL_NAME"
    echo "    $URL"
    if ! curl -fL --retry 3 --connect-timeout 20 -o "$TARBALL_NAME.part" "$URL"; then
        rm -f "$TARBALL_NAME.part"
        cat >&2 <<EOF

❌ 下载失败。可以手动下载后重试：
     curl -fL -o $WORK/$TARBALL_NAME \\
          $URL
   然后重新运行本脚本（会跳过下载）。

   如果网络需要代理，先导出 https_proxy / http_proxy 环境变量。
EOF
        exit 1
    fi
    mv "$TARBALL_NAME.part" "$TARBALL_NAME"
fi

# ---- 2. 校验 sha256 ----
echo "==> 校验 sha256"
GOT="$(sha256sum "$TARBALL_NAME" | awk '{print $1}')"
if [[ "$GOT" != "$SHA256" ]]; then
    echo "❌ 校验失败！" >&2
    echo "   期望: $SHA256" >&2
    echo "   实际: $GOT" >&2
    echo "   文件可能下载不完整或被篡改。删除后重试：" >&2
    echo "   rm $WORK/$TARBALL_NAME" >&2
    exit 1
fi
echo "    ✅ $GOT"

# ---- 3. 解压 ----
if [[ ! -d "$SRCDIR" ]]; then
    echo "==> 解压"
    tar xzf "$TARBALL_NAME"
fi

# ---- 4. 编译安装 ----
cd "$SRCDIR"
echo "==> 配置（静态库 + PIC，安装到 $PREFIX）"
cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -Dprotobuf_BUILD_TESTS=OFF \
    -Dprotobuf_BUILD_EXAMPLES=OFF \
    -Dprotobuf_BUILD_CONFORMANCE=OFF \
    -Dprotobuf_WITH_ZLIB=OFF \
    > /dev/null

echo "==> 编译（jobs=$(nproc)）"
cmake --build build -j "$(nproc)"

echo "==> 安装"
cmake --install build > /dev/null

# ---- 5. 把许可证一起装过去（BSD-3-Clause 要求随二进制保留版权声明）----
if [[ -f "$HERE/LICENSE.protobuf" ]]; then
    mkdir -p "$PREFIX"
    cp -f "$HERE/LICENSE.protobuf" "$PREFIX/LICENSE"
fi

echo
echo "✅ 完成: $PREFIX"
"$PREFIX/bin/protoc" --version
echo
echo "提示：构建产物（protobuf-21.12/ 与 build-protobuf/）已在 .gitignore 里，不入库。"
