# 移植说明：RM2026 SDK → RM2027 通信协议后端

本目录由 [`rm2026-custom-client-protocol-sdk`](../../rm2026-custom-client-protocol-sdk) 整体移植而来，
协议基准从《RoboMaster 2026 通信协议 **V2.0.0**（20260626）》切换为
《RoboMaster 2027 通信协议 **V1.0.0**（20261029）》。

- 功能范围不变：MQTT 协议层（自带 broker、客户端、C/C++/Python API、PyQt5 面板）+ UDP 图传
  （HEVC 编解码、分片重组、压力测试）全部保留。
- 协议内容重生成：proto、C++ 原生结构体、字段表、频率表、测试场景均由 `tools/gen_*.py`
  依据官方 2027 PDF 重新生成，**不保留 RM2026 系列兼容**（按需求）。
- 与 2026 的逐条协议差异见 [V1.0.0-变更分析.md](V1.0.0-变更分析.md)，
  页码级证据见 [rule-documents 对比报告](../../rule-documents/RM2026-vs-RM2027-自定义终端协议-对比报告.md)。

---

## 1. 命名迁移对照

| 项目 | RM2026 版 | 本仓库 |
| :-- | :-- | :-- |
| 目录 | `rm2026-custom-client-protocol-sdk/` | `RM2027-CustomTerminal-CommunicationBackend/` |
| CMake 工程名 / 目标 | `rm2026_custom_client_cpp` / `rm2026_*` | `rm2027_customterminal_communicationbackend` / `rm2027_*` |
| 头文件 | `include/rm2026/` | `include/rm2027/` |
| C++ 命名空间 | `rm2026::` | `rm2027::` |
| proto 文件 | `rm2026_*.proto`（package `rm_client_*` 不变） | `rm2027_*.proto` |
| C 接口 | `librm2026c.so` / `rm2026_c.h` | `librm2027c.so` / `rm2027_c.h` |
| Python 封装 | `python/rm2026c.py` | `python/rm2027c.py` |
| 字段表 | `docs/spec-v2.0.0-fields.json` | `docs/spec-v1.0.0-fields.json` |
| 变更分析 | `docs/V2.0.0-变更分析.md`（讲 2026 内部演进） | `docs/V1.0.0-变更分析.md`（讲 2026→2027） |

术语同步官方 2027 口径：自定义客户端 → **自定义终端**、工程/英雄机器人 → **重装机器人**、
空中机器人 → **空中系统**、云台手 → **指挥手**、发射机构 → **测速模块**、飞坡 → **高地**、
前哨站 → **R 前哨 / B 前哨**。

## 2. 协议数据的重新生成

三个生成器构成唯一事实来源，禁止手改产物：

```bash
# 0) 官方 PDF → 文本（保留版式）
pdftotext -layout -enc UTF-8 \
  "../rule-documents/RoboMaster 2027 机甲大师高校系列赛通信协议 V1.0.0（20261029）.pdf" p2027.txt

# 1) proto + 字段表：proto/rm2027_client_{up,down}.proto、docs/spec-v1.0.0-fields.json
python3 tools/gen_v2_proto.py p2027.txt proto

# 2) 频率表：docs/frequencies.json（会用 PDF 表 2-1 交叉校验，防抄错）
python3 tools/gen_freq.py p2027.txt

# 3) 对外 API：include/rm2027/api/messages.h、src/api/messages.cpp
python3 tools/gen_api.py

# 4) 测试场景（依赖第 1 步的字段表）
python3 tests/gen_scenario.py
```

产物可重复生成，内容与仓库内文件一致。

## 3. 2027 协议要点（详见变更分析）

| 维度 | 2026 V2.0.0 | 2027 V1.0.0 |
| :-- | :-- | :-- |
| 第 2 章 | 自定义**客户端**协议 | 自定义**终端**协议 |
| 指令数 | 36（上行 11 / 下行 25） | **32（上行 9 / 下行 23）** |
| 删除指令 | — | `HeroDeployModeEventCommand`、`DeployModeStatusSync`、`AirSupportCommand`、`AirSupportStatusSync`（无新增） |
| 字段变化 | — | `GlobalUnitStatus`：`outpost_R_health/status`、`outpost_B_health/status`、新增 `total_CP_ally/enemy`（15/16）；`RobotDynamicStatus` 4 → `reserved`；`RobotModuleStatus` 11 → `modular_42mm` |
| 语义变化 | — | `base_shield`/`enemy_base_shield` → 基地**阵地控制状态**；`robot_health` 10 → **8** 元素；`end_reason` 7 项重编号；`event_id` 16 项重排；`mechanism_id` 6 项且时间带符号；雷达每方 4 台（1,3,4,7）；性能体系枚举收敛为 {1,2} |
| 字节上限 | `CustomControl` 30 字节 / `0x0311` 30 字节 | **60 字节** / **60 字节** |
| 传输层 | MQTT `192.168.12.1:3333`、UDP `3334`、Protobuf ≥3.15、qos=1 | **完全不变** |
| 频率 / 方向 / qos | — | 全部保留指令**逐行一致** |

## 4. 移植中做的技术判断（与 2026 版实现的差异点）

1. **protoc 访问器大小写**：官方 2027 新增字段写作 `outpost_R_health` / `total_CP_ally`，
   protoc 生成的 C++ 访问器会整体小写（`outpost_r_health()`）。`tools/gen_api.py` 增加
   `acc()` 映射：proto 访问器用小写名，而 C++ 结构体成员与 proto 字段名保留官方原始大小写，
   便于与 PDF 逐字对照。
2. **生成器常量表随协议收缩**：4 条指令删除后，`gen_api.py` / `gen_v2_proto.py` 的 `UPSTREAM`
   由 11 条改为 9 条，`gen_freq.py` 的固定频率表与触发式表同步删除对应条目。
3. **频率交叉校验适配 2027 版式**：2027 表格列更窄，"触发式发送，发送频率最高为 10Hz" 会与
   其它列交错换行，原「字面连续匹配」会误报；改为只要求「最高」与「<n>Hz」出现在同一窗口。
4. **clientID 文档纠正**：旧 README/CLI 写「蓝方 11..17」，与官方附录二（蓝方 101..107）矛盾；
   本仓库按「clientID = 官方机器人 ID」统一（红 1/3-5/6/7，蓝 101/103-105/106/107，2/102 为保留位）。
5. **Node 交叉验证的 JSON 键名**：protobufjs 默认 camelCase 转换与 C++ protobuf 的
   `ToJsonName` 对含大写字母的字段名结果不同（如 `outpost_R_health`）。`tests/node/crosscheck.js`
   改为 `keepCase: true` 解析、输出时按 C++ 规则转键名；**不影响线上 wire 格式**。
6. **文档计数校正**：README 原写「35 个 MQTT 符号 + 21 个图传符号」，实测导出 36 + 21 = 57 个，
   已更正；目录树里不存在的 `include/rm2027/server/rm_server.h`、`client/rm_client.h`
   改为实际的 `api/server.h`、`api/client.h`。

## 5. 验证矩阵（全部通过）

| 验证项 | 命令 | 结果 |
| :-- | :-- | :-- |
| 构建（全量重编） | `bash build.sh`（CMake 生成 Unix Makefiles；等价 `cmake -S . -B build && make -C build -j`） | 0 error / 0 warning |
| 与官方 PDF 逐字段一致 | `python3 tools/diff_protocol_versions.py p2027.txt proto` | **32/32 条消息完全一致** |
| 协议全量（32 条逐字段） | `bash tests/run_protocol.sh` | 下行 23/23、上行 9/9 ✅ |
| 纯 C 接口 | `bash tests/run_capi.sh` | ✅（含被删指令/旧字段的负向断言） |
| 对外 API 示例 | `bash tests/run_api.sh` | ✅ |
| Node 交叉验证 | `bash tests/run_crosscheck.sh` | 23 收 / 9 发 ✅ |
| 连接健壮性 | `bash tests/run_reconnect.sh` | 重连后收齐 23 种下行 ✅ |
| GUI 离屏 | `bash tests/run_gui.sh` | ✅ |
| 图传链路 | `bash tests/run_video.sh`、`run_video_capi.sh`、`check_endian.sh` | ✅ |
| 图传压力（18 种编码/故障注入） | `bash tests/stress_video.sh` | ✅ 全部通过 |
| 端到端（协议 + 图传同时跑） | `bash tests/run_integrated.sh` | 协议 32 条 + 图传 271 帧 ✅ |

一键全跑：`bash tests/run_all.sh`。

## 6. 边界与未做

- **图传层未改动**：UDP 封帧（8 字节大端包头 / 1400B 分片 / HEVC / 无重传）两版协议完全相同。
- **不保留 2026 兼容**：消息表、字段名、枚举语义均按 2027 单一版本实现，不做运行时版本分支。
- 协议**业务语义**（按机器人状态自动生成下行、解析 `CommonCommand` 改变比赛状态）仍属"未实现"，
  与 2026 版一致，本项目定位是协议层 + 图传层的正确实现与可复现验证。
- `third_party/protobuf-21.12` 是本地预编译的 protobuf（23MB），随仓库复制以支持离线构建；
  它被 `.gitignore` 忽略，可通过 `third_party/fetch_protobuf.sh` 重建。
- 仓库未继承 2026 版的 `.git` 历史（新目录按新项目起步）。
