/* ===========================================================================
 * RM2027 自定义终端协议 V1.0.0 —— 纯 C 接口
 *
 * 设计约定
 *   * 指令内容一律用 JSON 字符串传递（键名 = 官方 proto 字段名，snake_case）。
 *     这样 C 侧不需要任何结构体布局约定，Python/ctypes 也能直接构造。
 *   * 回调里的名字与 JSON 指针只在回调期间有效，需要留存请自行拷贝。
 *   * 元数据（rm_message_*）返回的字符串是静态存储，永久有效，无需释放。
 *   * 所有返回 int 的函数：0 = 成功，非 0 = 失败（用 rm_last_error() 取原因）。
 *   * 发送前会按官方 §2.2 校验字段长度/值域（CustomControl ≤60B、CustomByteBlock
 *     ≤300B、MapClick robot_id 固定 7B、robot_health/radar 非空须 8 个等），
 *     不符合时发送被拒绝，rm_last_error() 里带具体原因。
 *   * RadarInfoToClient 有两种可切换线格式，见下方 rm_set_radar_wire_format()。
 * =========================================================================== */
#ifndef RM2027_C_H
#define RM2027_C_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *rm_handle;

/* 收到一条指令：name=指令名, json=规范化 JSON, client_id=发送方(仅服务器侧有意义) */
typedef void (*rm_message_cb)(void *user, const char *name, const char *json,
                              const char *client_id);
/* 事件通知：event = connected / connection-lost / client-connected / ... */
typedef void (*rm_event_cb)(void *user, const char *event, const char *detail);

/* ---------------------------------------------------------------------------
 * 元数据
 * ------------------------------------------------------------------------- */
int         rm_message_count(void);
const char *rm_message_name(int index);          /* 未知 index 返回 NULL */
const char *rm_message_direction(int index);     /* "up" / "down" */
int         rm_message_triggered(int index);     /* 1 = 触发式发送 */
double      rm_message_default_hz(int index);    /* 0 = 触发式发送 */
double      rm_message_max_hz(int index);        /* 触发式频率上限，0 = 未规定 */
const char *rm_message_fields_json(int index);   /* 字段表 JSON 数组 */
const char *rm_message_display(int index);       /* 显示名（目前同指令名） */

/* 按名字查（未知返回 -1 / 0 / NULL） */
int         rm_message_index(const char *name);
int         rm_message_triggered_by_name(const char *name);
double      rm_message_default_hz_by_name(const char *name);
double      rm_message_max_hz_by_name(const char *name);
const char *rm_message_fields_json_by_name(const char *name);

/* ---------------------------------------------------------------------------
 * 雷达线格式（只影响 RadarInfoToClient 一条消息；进程级全局设置）
 *
 *   0 = nested（默认）：repeated RadarSingleRobotInfo（嵌套 message），
 *       与 ACE / ClearWei 等第三方实现的线格式一致；
 *   1 = flat          ：严格按官方 PDF §2.2.19 字面
 *       `repeated uint32 RadarSingleRobotInfo;`（补字段号 = 1）：
 *       字段 1 为 packed repeated uint32，8 目标 × 3 值 = 24 个 uint32。
 *
 * 收发两端必须设置成同一模式；MQTT topic / qos / JSON 形式不变
 * （JSON 始终是嵌套形式，与线格式无关）。返回 0 = 成功，非 0 = 失败。 */
int rm_set_radar_wire_format(int format);
int rm_get_radar_wire_format(void);

/* ---------------------------------------------------------------------------
 * 客户端
 * ------------------------------------------------------------------------- */
rm_handle rm_client_new(const char *host, uint16_t port, const char *client_id);
void      rm_client_free(rm_handle h);
int       rm_client_start(rm_handle h);   /* 连接并订阅（默认全部下行） */
void      rm_client_stop(rm_handle h);
int       rm_client_is_connected(rm_handle h);
int       rm_client_send(rm_handle h, const char *name, const char *json);
size_t    rm_client_received(rm_handle h);
size_t    rm_client_sent(rm_handle h);

/* ---------------------------------------------------------------------------
 * 服务器（自带 MQTT broker）
 * ------------------------------------------------------------------------- */
rm_handle rm_server_new(const char *host, uint16_t port);
void      rm_server_free(rm_handle h);
int       rm_server_start(rm_handle h);
void      rm_server_stop(rm_handle h);
int       rm_server_is_running(rm_handle h);
int       rm_server_send(rm_handle h, const char *name, const char *json);
size_t    rm_server_client_count(rm_handle h);
size_t    rm_server_subscribed_client_count(rm_handle h); /* 已订阅的客户端数 */
size_t    rm_server_topic_subscriber_count(rm_handle h, const char *topic); /* 订了该 topic 的客户端数 */
size_t    rm_server_received(rm_handle h);
size_t    rm_server_sent(rm_handle h);

/* ---------------------------------------------------------------------------
 * 回调注册（收发两个方向共用）
 * ------------------------------------------------------------------------- */
void rm_set_message_cb(rm_handle h, rm_message_cb cb, void *user);
void rm_set_event_cb(rm_handle h, rm_event_cb cb, void *user);

/* =========================================================================
 * 图传（UDP 3334，HEVC/H.265）
 *
 * 两端都只跟「图片」打交道：
 *   * 发送端：编码在 C++ 内部完成，调用方喂 BGR24 图像
 *   * 接收端：组帧 + 解码在 C++ 内部完成，调用方取 BGR24 图像 + 统计
 *
 * 图像统一是 **BGR24 紧凑排列**（与 OpenCV / Qt 一致），每行 stride 字节。
 * ========================================================================= */

/* 解码格式模式字符串
 *   RM_VIDEO_CODEC_AUTO —— 默认，用探针自动探测码流格式
 *   也可传 "h265" / "h264" / "jpeg" 手动固定（会完全跳过探测）
 * 还接受 "hevc" / "H.265" / "mjpeg" 等写法（大小写、点、横线都不敏感） */
#define RM_VIDEO_CODEC_AUTO "auto"

/* 收帧回调：bgr 指针只在回调期间有效，需要留存请自行拷贝 */
typedef void (*rm_video_frame_cb)(void *user, const uint8_t *bgr, int width, int height,
                                  int stride, uint64_t frame_no, int keyframe);

/* ---- 发送端（喂图片） ---- */
rm_handle rm_video_sender_new(const char *host, uint16_t port);
void      rm_video_sender_free(rm_handle h);
int       rm_video_sender_start(rm_handle h);          /* 0 = 成功 */
void      rm_video_sender_stop(rm_handle h);
int       rm_video_sender_is_running(rm_handle h);     /* 1/0 */

/* 编码参数（JSON，键名见 EncoderConfig）：
 *   width height fps bitrate_kbps gop_size bframes slices aud repeat_headers
 *   pix_fmt preset tune intra_refresh x265_lookahead extra_x265_params
 * 只在停止状态下生效；运行中返回非 0。 */
int rm_video_sender_config(rm_handle h, const char *json);

/* 网络/编码故障注入（JSON）—— 用来压测对端：
 *   drop_every_packet duplicate_every_packet shuffle_fragments
 *   corrupt_frame_total frame_number_step rotate_every_frames max_datagram
 *   target_host target_port */
int rm_video_sender_fault(rm_handle h, const char *json);

/* 喂一帧 BGR24 图像（stride 字节/行）。编码 + 打包 + 发送在本调用内同步完成。 */
int rm_video_sender_send_bgr(rm_handle h, const uint8_t *bgr, int width, int height, int stride);
/* 发送结束前调用一次，把编码器里缓存的 AU 冲出来（含 B 帧时必需） */
int rm_video_sender_flush(rm_handle h);
/* 统计 JSON 写入 buf，返回实际长度（不含结尾 0）；buf 太小返回 -1 */
int rm_video_sender_stats(rm_handle h, char *buf, int buflen);

/* ---- 接收端（收图片） ---- */
rm_handle rm_video_receiver_new(const char *bind_host, uint16_t port);
void      rm_video_receiver_free(rm_handle h);
int       rm_video_receiver_start(rm_handle h);
void      rm_video_receiver_stop(rm_handle h);
int       rm_video_receiver_is_running(rm_handle h);   /* 1/0 */

/* 可调参数（JSON）：
 *   max_frame_bytes frame_timeout_ms max_pending_frames au_queue_depth codec_mode
 * 只在停止状态下生效。codec_mode 等价于 rm_video_receiver_set_codec_mode。 */
int rm_video_receiver_config(rm_handle h, const char *json);

/* 设置解码格式模式：RM_VIDEO_CODEC_AUTO 或 "h265"/"h264"/"jpeg"。
 * **运行中也可调用**，下一个 access unit 起生效。无法识别返回非 0。 */
int         rm_video_receiver_set_codec_mode(rm_handle h, const char *mode);
/* 返回当前模式字符串（线程局部缓冲，下次调用前有效） */
const char *rm_video_receiver_codec_mode(rm_handle h);

/* 取最新解码帧（拷贝进调用方缓冲 bgr，长度 buflen 字节）。
 * 返回  1 = 拿到新帧（*width/*height/*stride/*frame_no/*keyframe 被填好）
 *       0 = 自 *since 以来没有新帧（不写缓冲）
 *      -1 = 出错或缓冲太小
 * *since 传入上次取到的序号（初值 0），拿到新帧时被更新。
 * 注意：这个返回值约定与其它接口的「0 = 成功」不同 —— 这里必须区分「没有新帧」。 */
int rm_video_receiver_get_frame(rm_handle h, uint8_t *bgr, int buflen, int *width, int *height,
                                int *stride, uint64_t *frame_no, int *keyframe, uint64_t *since);

/* 可选：每解出一帧同步回调（一帧不漏）。预览建议用 get_frame 主动拉取。 */
void rm_video_receiver_set_frame_cb(rm_handle h, rm_video_frame_cb cb, void *user);

/* 统计 JSON 写入 buf，返回实际长度（不含结尾 0）；buf 太小返回 -1 */
int rm_video_receiver_stats(rm_handle h, char *buf, int buflen);

/* ---------------------------------------------------------------------------
 * 工具
 * ------------------------------------------------------------------------- */
const char *rm_last_error(void); /* 线程局部缓冲；下次调用前有效 */
const char *rm_version(void);    /* 静态字符串 */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* RM2027_C_H */
