# UDP 图传包头字节序调研结论

**问题（最初）**：2026 早期版本的官方协议只写了「每个 udp 包的码流数据的前 8 个字节固定为：
帧编号 2B / 当前帧内分片序号 2B / 当前帧总字节数 4B」，**没有写字节序**。
这直接决定图传链路能不能用（字节序不一致时重组结果是 0 帧）。

**结论：大端（Big-Endian / 网络字节序）。**

> ### ✅ 2026-10-29 复核：官方 2027 V1.0.0 §2 明文确认（与 RM2026 V2.0.0 逐字相同）
> 官方 **RM2027 V1.0.0（20261029）** §2 正文明确写道：
> 「每个 udp 包的码流数据的前 8 个字节固定为：帧编号（递增）：2 byte / 当前帧内分片序号：2 byte /
> 当前帧总字节数：4 byte。**此额外添加的 8 个字节为大端传输。**
> 每个 udp 包最大为 1400 个字节，除去 8 个固定字节外，大小为 1392。」
>
> 即：**大端**、单包上限 **1400 B**、负载 **1392 B** —— 与本项目实现完全一致。
> 另补充规格：**60 fps / 1920×1080 / HEVC（标准 H265）/ 无重传**。
> 该段规则自 RM2026 V2.0.0（20260626）起已由官方写明，2027 V1.0.0 **逐字沿用**，未作调整。
> 结论已从「9 个实现的一致性推断」升级为「官方规定」。以下调研过程保留作为佐证。

**（原调研结论）** 现有可查的独立实现 **9 个全部是大端，0 个小端**。

---

## 1. 官方文档：2026 早期版本确实没写

| 文档 | 版本 | 该段是否提到字节序 |
| :-- | :-- | :-- |
| RoboMaster 2026 通信协议 | V1.2.0（20260209） | ❌ 未提及 |
| RoboMaster 2026 通信协议 | V1.3.1（20260519） | ❌ 未提及（措辞与 V1.2.0 完全一致） |
| RoboMaster 2026 通信协议 | V2.0.0（20260626） | ✅ 明文「此额外添加的 8 个字节为大端传输」 |
| RoboMaster 2027 通信协议 | V1.0.0（20261029） | ✅ 明文（与 V2.0.0 逐字相同） |

在 2026 早期的 V1.2.0 / V1.3.1 两份 PDF 全文里检索 `大端 / 小端 / 字节序 / endian / network order`，
命中的唯一一处是 §1.5 关于「操作手自定义控制器模拟键鼠」时字符编码的大小端提示
（V1.2.0 文本第 2044 行；该节在 2027 已随自定义控制器并入自定义终端而删除），**与图传无关**。

> 备注：`JFCY2718/rmuc-custom-client-ui` 的文档中最早登记过一条转述证据，称官方 §2 正文
> 「UDP_HEVC_VIDEO_STREAM ... 证据类型 OFFICIAL_PDF_TEXT ... §2 正文 (p.44) ...
> 每包前 8 字节为帧编号(2B)+分片序号(2B)+总字节数(4B)**大端头**，单包最大 1400B(负载 1392B)」
> —— 该团队引用的是 **RM2026 V2.0.0（20260626）§2**（p.44 正是 2026 版第 2 章起始页）。
> 当时因官方 wiki 为纯前端渲染、抓不到正文，只能记为转述；现在 2026 V2.0.0 与 2027 V1.0.0 的
> PDF 原件均可直接核对，**该转述已被证实**，2027 版逐字沿用同一段规则（见上）。
> 它所给出的一切数值（1400B 总包 / 1392B 负载 / 大端）与下述所有实现完全吻合。

## 2. 第三方独立实现：9/9 全部大端

| 项目 | 语言 | 证据（文件:行） | 字节序 |
| :-- | :-- | :-- | :-- |
| [stydxm/RMMock](https://github.com/stydxm/RMMock) | Go | `pkg/stream/udp_client.go:26-28` `binary.BigEndian.PutUint16/PutUint32` | 大端 |
| [JNU-SHARK/SharkClient](https://github.com/JNU-SHARK/SharkClient) | Rust/Tauri | `src-tauri/src/udp_bridge.rs:654-656` `u16/u32::from_be_bytes` | 大端 |
| [afterrain2006/BNGUClient](https://github.com/afterrain2006/BNGUClient) | TS+Rust | `tools/rm2026-mock-server/src/video/packetizer.ts:39-41` `readUInt16BE/readUInt32BE`；`src-tauri/src/udp_bridge.rs:660-662` `from_be_bytes`；README：「UDP **大端**分片重组」 | 大端 |
| [Justin186/gxu_rm_custom_client](https://github.com/Justin186/gxu_rm_custom_client) | C++/Qt | `src/network/VideoReceiver.cpp:78-80` `qFromBigEndian<quint16/quint32>` | 大端 |
| [ClearWei/RM26_Client](https://github.com/ClearWei/RM26_Client) | C++ | `src/network/VideoReceiver.cpp:393-399` `qFromBigEndian`；`tools/video_sender.py` 头注释表：「大端序 \| 大端序 \| 大端序」 | 大端 |
| [dkjsiogu/rm-client](https://github.com/dkjsiogu/rm-client) | JS | `server/udp-receiver.js:100-102` `readUInt16BE/readUInt32BE`；README：「UDP 视频帧重组（**大端序** 8 字节头）」 | 大端 |
| [PnX-HKUSTGZ/RMSynapse](https://github.com/PnX-HKUSTGZ/RMSynapse) | C++ | `plugins/rm_video_decoder/src/video_core.cpp:101-102` `ntohs(...)`，源码注释：「按网络传输习惯，字段通常使用网络字节序（**大端**）」 | 大端 |
| [JFCY2718/rmuc-custom-client-ui](https://github.com/JFCY2718/rmuc-custom-client-ui) | TS+Rust | `src-tauri/src/udp_video_runtime.rs:35-37` `from_be_bytes`、`:488-490` `to_be_bytes`；`simulator/simulator.py:132` 注释明确 `big-endian` | 大端 |
| [JNU-SHARK/SharkDataSever](https://github.com/JNU-SHARK/SharkDataSever) | JS | `js/udp-video-streamer.js:272-274` `writeUInt16BE/writeUInt32BE` | 大端 |

反查「小端」写法（`LittleEndian` / `from_le_bytes` / `readUInt16LE` / `qFromLittleEndian`）：
在上述 9 个仓库中 **仅命中共享内存 IPC 桥接等无关代码，无一用于 UDP 图传包头**。

## 3. 其他被印证的一致参数

除了字节序，这些实现在以下参数上也完全一致，可一并作为实现依据：

* 单包最大总长度 **1400 字节**，其中包头 8 字节，**单包最大负载 1392 字节**
  （RMMock `1400 - 8`、SharkDataSever `maxPacketSize = 1400`、rmuc-custom-client-ui 文档同值）。
* 自定义终端监听 **UDP 3334**，服务器（赛事引擎）向其发送；服务器 **MQTT 192.168.12.1:3333**。
* 编码 **HEVC (H.265)** 裸流，**无重传机制**。
* 官方视频规格（据 rmuc-custom-client-ui 对 2026 V2.0.0 的摘录，2027 V1.0.0 沿用）：**60fps / 1920×1080 / H.265**。

## 4. 对本项目的处置

官方明文 + 9/9 实现一致，因此本项目**只实现大端**，不提供字节序开关：

* `src/video/udp_header.cpp` 的 `put16/put32/get16/get32` 固定按大端读写 8 字节包头，
  既没有字节序相关的编译选项，也没有运行期切换；
  `include/rm2027/video/udp_header.h` 的注释直接引用官方 2027 V1.0.0 §2 原文。
* `tests/run_video.sh` 校验包头为大端。
* 已实测：字节序混用时接收端 **0 帧可重组**，所以这是必须一次对齐的参数。

> 调研过程（第 1–3 节）保留作为佐证：即便官方早期版本未写明，9 个独立实现也全部是大端，
> 与现在官方 2026 V2.0.0 / 2027 V1.0.0 的明文一致。

### 尚存的唯一不确定性

~~官方 2026 早期版本 V1.2.0 / V1.3.1 正文均未写明字节序。~~
**已解决**：自 RM2026 V2.0.0（20260626）起，官方 §2 正文明确「此额外添加的 8 个字节为大端传输」，
并给出 1400 B 单包上限 / 1392 B 负载；RM2027 V1.0.0（20261029）逐字沿用。
官方明文与 9 个实现全部吻合，无剩余不确定性。

---

*调研日期：2026-10-04；RM2027 V1.0.0 适用性复核：2026-10-29。引用的仓库均已 clone 到 `../.refs/` 供复核（各仓库 commit 见上表对应版本快照）。*
