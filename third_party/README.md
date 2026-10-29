# third_party —— 第三方依赖

本目录存放本项目的第三方依赖。

**约定：只入库「获取方式」，不入库二进制。** 构建产物都在 `.gitignore` 里
（`protobuf-21.12/`、`build-protobuf/`），`git clone` 之后跑一次脚本即可。

---

## 1. 已在此目录内的依赖

### protobuf v21.12（= 3.21.12）

| | |
| :-- | :-- |
| 用途 | 官方协议用 proto3 `optional`，需要 protobuf ≥ 3.15；本项目用它生成并序列化 32 条指令 |
| 许可证 | **BSD-3-Clause**（见 [`LICENSE.protobuf`](LICENSE.protobuf)） |
| 可否再分发 | ✅ 允许（保留版权声明与许可证即可） |
| 为什么不用系统自带的 | Ubuntu 22.04 只有 3.12.4，**编不过 proto3 的 `optional`** |
| 为什么是这个版本 | v21.12 是最后一个**不依赖 abseil** 的版本，CMake 构建干净、不用再拉一堆依赖；v22+ 必须连带构建 abseil |
| 官方发布包 sha256 | `4eab9b524aa5913c6fffb20b2a8abf5ef7f95a80bc0701f3a6dbb4c607f73460`（`protobuf-cpp-3.21.12.tar.gz`） |

**获取方式**：

```bash
third_party/fetch_protobuf.sh          # 下载 + 校验 sha256 + 编译安装，约 2 分钟
```

装好后是 `third_party/protobuf-21.12/{bin,include,lib}`，CMake 会自动优先用它。
脚本会把 `LICENSE.protobuf` 一并复制进安装目录（BSD-3-Clause 要求随二进制保留版权声明）。

网络不通时可以手动下载后重跑：

```bash
curl -fL -o third_party/build-protobuf/protobuf-cpp-3.21.12.tar.gz \
  https://github.com/protocolbuffers/protobuf/releases/download/v21.12/protobuf-cpp-3.21.12.tar.gz
third_party/fetch_protobuf.sh
```

如果 `third_party/protobuf-21.12` 不存在，CMake 会**回退到系统 protobuf**——
但系统版本低于 3.15 时编译会失败，所以请先跑脚本。

---

## 2. 未入库的依赖（需自行安装）

这些是系统库，**本项目不内置也不重新分发**，请用发行版包管理器安装：

| 依赖 | 本项目验证过的版本 | 许可证 | 用途 | 安装 |
| :-- | :-- | :-- | :-- | :-- |
| Paho MQTT C | 1.3.9 | EPL-2.0 | MQTT 传输 | `apt install libpaho-mqtt-dev` |
| Paho MQTT C++ | 1.2.0 | EPL-2.0 | MQTT 传输（C++ 封装） | `apt install libpaho-mqttpp-dev` |
| FFmpeg（libavcodec / libswscale / libavutil） | 4.4.2 | LGPL-2.1+ | 图传 HEVC 编解码 | `apt install libavcodec-dev libswscale-dev libavutil-dev` |
| x265 | 3.5 | **GPL-2+** | FFmpeg 的 HEVC 编码后端 | `apt install libx265-dev` |
| OpenCV | 4.5.4 | Apache-2.0 AND BSD-3-Clause | 仅测试/GUI 侧读视频 | `apt install libopencv-dev` |
| PyQt5 | 5.15.6 | **GPL-3** | 控制面板界面 | `apt install python3-pyqt5` |
| numpy | 2.1.3 | BSD-3-Clause | 图传图像进出 | `pip install numpy` |
| Node + mqtt.js + protobufjs | — | MIT | **仅交叉验证测试**用 | `npm install`（见 `tests/node/`） |

> `protoc` 由第 1 节的脚本提供，不用单独装。

---

## 3. ⚠️ 发布前必须注意的许可证问题

**这一节请务必看完再决定要不要公开发布二进制。**

本项目的代码本身不包含上述库的代码，但**链接关系**会带来义务：

### 3.1 x265 是 GPL-2+，会让整个图传编码部分落入 GPL

FFmpeg 的 `libx265` 封装会链接 x265。我们图传发送端的 HEVC 编码正是走
`libx265`（见 `src/video/hevc_encoder.cpp`）。**链接 GPL 库后，由此产生的
可执行文件/动态库整体按 GPL-2+ 分发**，需要一并提供对应源码。

可选规避方式：

* 分发时**不带编码功能**，只保留解码（解码走 FFmpeg 内置的 HEVC 解码器，
  不依赖 x265）；或
* 整个项目就按 GPL-2+ 兼容的方式发布。

> 说明：RM 官方客户端接收的是 **H.265 解码**，这部分不涉及 x265；
> 只有「我们的服务器/模拟器要把画面编成 H.265 发出去」这一步才用到。

### 3.2 PyQt5 是 GPL-3，不是 LGPL

PyQt5 采用 GPL-3 / 商业双授权。**分发链接了 PyQt5 的界面程序会触发 GPL-3 义务**
（提供完整对应源码、保留声明）。若不想被 GPL 约束，可选：

* 换成 **PySide6 / PySide2**（Qt 官方的 **LGPL** 绑定，API 与 PyQt5 高度相似）；
* 或购买 PyQt 商业授权；
* 或**只以源码形式发布**、不分发二进制，由使用者自行安装依赖。

### 3.3 EPL-2.0（Paho）与 LGPL（FFmpeg 核心）

* Paho 是 EPL-2.0，属弱 copyleft，动态链接使用一般没问题，但**分发时需保留声明**。
* FFmpeg 若以 LGPL 方式构建，动态链接可满足要求；**但如果系统 FFmpeg 是
  以 `--enable-gpl` 构建的，则整体变 GPL**。Ubuntu 官方包把 libx265 独立成
  单独包正是为了隔离这一点。自行分发时请确认构建配置。

### 3.4 结论

| 场景 | 是否可行 |
| :-- | :-- |
| 只发布**源码**，依赖由使用者自行安装 | ✅ 最省事，推荐 |
| 发布二进制且**含编码 + PyQt5 界面** | ⚠️ 需要按 GPL-2+/GPL-3 履行义务（或换 PySide / 去掉编码） |
| 发布二进制，**只含解码 + 换成 PySide** | ✅ 可保持宽松许可 |

> 以上是基于各依赖官方许可证的**技术性说明，不构成法律意见**。
> 正式对外发布前建议按你所在组织的流程确认。

---

## 4. 本项目自身还没有许可证文件

仓库根目录目前**没有 `LICENSE`**。要公开发布的话需要先选定一个——
注意它必须与你选择的依赖分发方式相容（见第 3 节）。

如果希望尽量宽松，建议：**源码用 MIT/Apache-2.0，同时不分发二进制**，
并在 README 里说明运行时依赖各自的许可证。
