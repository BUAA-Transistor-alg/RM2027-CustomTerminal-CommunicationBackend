"""RM2027 自定义终端协议 V1.0.0 的 Python 接口。

底层是纯 C 接口 librm2027c.so（由 rm2027/api/* 的 C++ 门面导出），
本模块用 ctypes 封装成 Python 对象。

指令内容统一用 dict（键名 = 官方 proto 字段名 snake_case），
内部转成 JSON 字符串过 C 边界。

    from rm2027c import Client, Server, messages

    cli = Client("127.0.0.1", 3333, "3")
    cli.on_message(lambda name, data, cid: print(name, data))
    cli.start()
    cli.send("DartCommand", {"target_id": 1, "open": True, "launch_confirm": True})

注意：回调是在 C++ 的工作线程里调用的（不是 Python 主线程）。
     做 Qt 界面时请用信号把它转到主线程，见 gui/ 下的实现。
"""

from __future__ import annotations

import ctypes
import json
import os
import threading
from ctypes import (CFUNCTYPE, c_char_p, c_double, c_int, c_size_t, c_uint16,
                    c_uint64,
                    c_void_p)
from dataclasses import dataclass, field
from typing import Callable, Dict, List, Optional

__all__ = ["MessageInfo", "messages", "message_names", "message_info",
           "Client", "Server", "library_path", "version",
           "VideoSender", "VideoReceiver", "VideoFrame", "VideoSenderStats",
           "VideoReceiverStats", "CODEC_AUTO", "CODEC_H265", "CODEC_H264", "CODEC_JPEG",
           "TIMEOUT_NONE", "TIMEOUT_GOT_FRAME", "TIMEOUT_NO_FRAME"]

try:
    import numpy as np
    _HAS_NUMPY = True
except ImportError:            # 图传接口需要 numpy；协议接口不需要
    np = None
    _HAS_NUMPY = False

_LIB_ENV = "RM2027C_LIB"


def _find_library() -> str:
    env = os.environ.get(_LIB_ENV)
    if env and os.path.exists(env):
        return os.path.abspath(env)
    here = os.path.dirname(os.path.abspath(__file__))
    candidates = [
        os.path.join(here, "..", "build", "librm2027c.so"),
        os.path.join(here, "librm2027c.so"),
        os.path.join(here, "..", "..", "third_party", "librm2027c.so"),
    ]
    for c in candidates:
        if os.path.exists(c):
            return os.path.abspath(c)
    raise RuntimeError(
        "找不到 librm2027c.so。请先在仓库根目录构建（bash build.sh，"
        "或 cmake -S . -B build && make -C build），或设置环境变量 %s 指向该库。" % _LIB_ENV)


library_path = _find_library()
_lib = ctypes.CDLL(library_path)

# ---------------------------------------------------------------- 原型声明
_lib.rm_message_count.restype = c_int
_lib.rm_message_name.restype = c_char_p
_lib.rm_message_name.argtypes = [c_int]
_lib.rm_message_direction.restype = c_char_p
_lib.rm_message_direction.argtypes = [c_int]
_lib.rm_message_triggered.restype = c_int
_lib.rm_message_triggered.argtypes = [c_int]
_lib.rm_message_default_hz.restype = c_double
_lib.rm_message_default_hz.argtypes = [c_int]
_lib.rm_message_max_hz.restype = c_double
_lib.rm_message_max_hz.argtypes = [c_int]
_lib.rm_message_fields_json.restype = c_char_p
_lib.rm_message_fields_json.argtypes = [c_int]
_lib.rm_message_index.restype = c_int
_lib.rm_message_index.argtypes = [c_char_p]

_lib.rm_client_new.restype = c_void_p
_lib.rm_client_new.argtypes = [c_char_p, c_uint16, c_char_p]
_lib.rm_client_free.argtypes = [c_void_p]
_lib.rm_client_start.restype = c_int
_lib.rm_client_start.argtypes = [c_void_p]
_lib.rm_client_stop.argtypes = [c_void_p]
_lib.rm_client_is_connected.restype = c_int
_lib.rm_client_is_connected.argtypes = [c_void_p]
_lib.rm_client_send.restype = c_int
_lib.rm_client_send.argtypes = [c_void_p, c_char_p, c_char_p]
_lib.rm_client_received.restype = c_size_t
_lib.rm_client_received.argtypes = [c_void_p]
_lib.rm_client_sent.restype = c_size_t
_lib.rm_client_sent.argtypes = [c_void_p]

_lib.rm_server_new.restype = c_void_p
_lib.rm_server_new.argtypes = [c_char_p, c_uint16]
_lib.rm_server_free.argtypes = [c_void_p]
_lib.rm_server_start.restype = c_int
_lib.rm_server_start.argtypes = [c_void_p]
_lib.rm_server_stop.argtypes = [c_void_p]
_lib.rm_server_is_running.restype = c_int
_lib.rm_server_is_running.argtypes = [c_void_p]
_lib.rm_server_send.restype = c_int
_lib.rm_server_send.argtypes = [c_void_p, c_char_p, c_char_p]
_lib.rm_server_client_count.restype = c_size_t
_lib.rm_server_client_count.argtypes = [c_void_p]
_lib.rm_server_subscribed_client_count.restype = c_size_t
_lib.rm_server_subscribed_client_count.argtypes = [c_void_p]
_lib.rm_server_topic_subscriber_count.restype = c_size_t
_lib.rm_server_topic_subscriber_count.argtypes = [c_void_p, c_char_p]
_lib.rm_server_received.restype = c_size_t
_lib.rm_server_received.argtypes = [c_void_p]
_lib.rm_server_sent.restype = c_size_t
_lib.rm_server_sent.argtypes = [c_void_p]

_lib.rm_set_message_cb.argtypes = [c_void_p, c_void_p, c_void_p]
_lib.rm_set_event_cb.argtypes = [c_void_p, c_void_p, c_void_p]
_lib.rm_last_error.restype = c_char_p
_lib.rm_version.restype = c_char_p

# ---------------------------------------------------------------- 图传
CODEC_AUTO = "auto"   # 默认：探针自动探测码流格式
CODEC_H265 = "h265"
CODEC_H264 = "h264"
CODEC_JPEG = "jpeg"

GET_FRAME_GOT = 1        # get_frame 内部返回码：拿到新帧
GET_FRAME_NONE = 0       # 没有新帧
GET_FRAME_ERROR = -1     # 出错

_VIDEO_FRAME_CB = CFUNCTYPE(None, c_void_p, c_void_p, c_int, c_int, c_int,
                            c_uint64, c_int)

_lib.rm_video_sender_new.restype = c_void_p
_lib.rm_video_sender_new.argtypes = [c_char_p, c_uint16]
_lib.rm_video_sender_free.argtypes = [c_void_p]
_lib.rm_video_sender_start.restype = c_int
_lib.rm_video_sender_start.argtypes = [c_void_p]
_lib.rm_video_sender_stop.argtypes = [c_void_p]
_lib.rm_video_sender_is_running.restype = c_int
_lib.rm_video_sender_is_running.argtypes = [c_void_p]
_lib.rm_video_sender_config.restype = c_int
_lib.rm_video_sender_config.argtypes = [c_void_p, c_char_p]
_lib.rm_video_sender_fault.restype = c_int
_lib.rm_video_sender_fault.argtypes = [c_void_p, c_char_p]
_lib.rm_video_sender_send_bgr.restype = c_int
_lib.rm_video_sender_send_bgr.argtypes = [c_void_p, c_void_p, c_int, c_int, c_int]
_lib.rm_video_sender_flush.restype = c_int
_lib.rm_video_sender_flush.argtypes = [c_void_p]
_lib.rm_video_sender_stats.restype = c_int
_lib.rm_video_sender_stats.argtypes = [c_void_p, c_char_p, c_int]

_lib.rm_video_receiver_new.restype = c_void_p
_lib.rm_video_receiver_new.argtypes = [c_char_p, c_uint16]
_lib.rm_video_receiver_free.argtypes = [c_void_p]
_lib.rm_video_receiver_start.restype = c_int
_lib.rm_video_receiver_start.argtypes = [c_void_p]
_lib.rm_video_receiver_stop.argtypes = [c_void_p]
_lib.rm_video_receiver_is_running.restype = c_int
_lib.rm_video_receiver_is_running.argtypes = [c_void_p]
_lib.rm_video_receiver_config.restype = c_int
_lib.rm_video_receiver_config.argtypes = [c_void_p, c_char_p]
_lib.rm_video_receiver_set_codec_mode.restype = c_int
_lib.rm_video_receiver_set_codec_mode.argtypes = [c_void_p, c_char_p]
_lib.rm_video_receiver_codec_mode.restype = c_char_p
_lib.rm_video_receiver_codec_mode.argtypes = [c_void_p]
_lib.rm_video_receiver_get_frame.restype = c_int
_lib.rm_video_receiver_get_frame.argtypes = [
    c_void_p, c_void_p, c_int, ctypes.POINTER(c_int), ctypes.POINTER(c_int),
    ctypes.POINTER(c_int), ctypes.POINTER(c_uint64), ctypes.POINTER(c_int),
    ctypes.POINTER(c_uint64)]
_lib.rm_video_receiver_set_frame_cb.restype = None
_lib.rm_video_receiver_set_frame_cb.argtypes = [c_void_p, _VIDEO_FRAME_CB, c_void_p]
_lib.rm_video_receiver_stats.restype = c_int
_lib.rm_video_receiver_stats.argtypes = [c_void_p, c_char_p, c_int]

_MSG_CB = CFUNCTYPE(None, c_void_p, c_char_p, c_char_p, c_char_p)
_EVT_CB = CFUNCTYPE(None, c_void_p, c_char_p, c_char_p)


def version() -> str:
    return _lib.rm_version().decode("utf-8", "replace")


def _last_error() -> str:
    p = _lib.rm_last_error()
    return p.decode("utf-8", "replace") if p else "未知错误"


# ---------------------------------------------------------------- 元数据
@dataclass
class MessageInfo:
    name: str
    direction: str          # "up" = 客户端->服务器, "down" = 服务器->客户端
    triggered: bool         # 官方文档：触发式发送
    default_hz: float       # 官方文档频率；触发式为 0
    max_hz: float           # 触发式发送的频率上限；0 = 未规定
    fields: List[dict] = field(default_factory=list)

    @property
    def is_uplink(self) -> bool:
        return self.direction == "up"

    @property
    def frequency_text(self) -> str:
        if self.triggered:
            return ("触发式(≤%gHz)" % self.max_hz) if self.max_hz > 0 else "触发式"
        return "%gHz" % self.default_hz


_message_cache: Optional[List[MessageInfo]] = None


def messages() -> List[MessageInfo]:
    """全部 32 条指令的元数据（上行 9 / 下行 23，含官方发送频率与字段表）"""
    global _message_cache
    if _message_cache is not None:
        return _message_cache
    out: List[MessageInfo] = []
    n = _lib.rm_message_count()
    for i in range(n):
        name = _lib.rm_message_name(i).decode("utf-8", "replace")
        direction = _lib.rm_message_direction(i).decode("utf-8", "replace")
        triggered = bool(_lib.rm_message_triggered(i))
        hz = float(_lib.rm_message_default_hz(i))
        max_hz = float(_lib.rm_message_max_hz(i))
        raw = _lib.rm_message_fields_json(i)
        fields = json.loads(raw.decode("utf-8", "replace")) if raw else []
        out.append(MessageInfo(name, direction, triggered, hz, max_hz, fields))
    _message_cache = out
    return out


def message_names(direction: Optional[str] = None) -> List[str]:
    return [m.name for m in messages() if direction is None or m.direction == direction]


def message_info(name: str) -> Optional[MessageInfo]:
    for m in messages():
        if m.name == name:
            return m
    return None


# ---------------------------------------------------------------- 键名转换
# protobuf 的 JSON 映射用 camelCase，而官方文档/协议用 snake_case。
# 对外统一成官方 snake_case：发送时接受两者，接收时转回 snake_case。

def _camel(name: str) -> str:
    out, up = [], False
    for ch in name:
        if ch == "_":
            up = True
        elif up:
            out.append(ch.upper())
            up = False
        else:
            out.append(ch)
    return "".join(out)


def _convert_keys(value, fields: List[dict], to_wire: bool):
    """to_wire=True: snake->camel（发出去）；False: camel->snake（收进来）"""
    if isinstance(value, list):
        return [_convert_keys(v, fields, to_wire) for v in value]
    if not isinstance(value, dict):
        return value
    by_snake = {f["name"]: f for f in fields}
    by_camel = {_camel(f["name"]): f for f in fields}
    out = {}
    for k, v in value.items():
        f = by_snake.get(k) or by_camel.get(k)
        if f is None:
            out[k] = v
            continue
        key = _camel(f["name"]) if to_wire else f["name"]
        out[key] = _convert_keys(v, f["fields"], to_wire) if "fields" in f else v
    return out


# ---------------------------------------------------------------- 基类
class _Base:
    def __init__(self, handle):
        self._handle = handle
        self._msg_cb = None      # 保持引用，否则会被 GC 回收导致崩溃
        self._evt_cb = None
        self._lock = threading.Lock()
        self._message_handlers: List[Callable[[str, dict, str], None]] = []
        self._event_handlers: List[Callable[[str, str], None]] = []

        def _on_message(_user, name, jsonstr, client_id):
            try:
                nm = name.decode("utf-8", "replace")
                data = json.loads(jsonstr.decode("utf-8", "replace"))
                cid = client_id.decode("utf-8", "replace") if client_id else ""
                info = message_info(nm)          # 转回官方 snake_case 字段名
                if info:
                    data = _convert_keys(data, info.fields, False)
            except Exception:
                return
            for h in list(self._message_handlers):
                try:
                    h(nm, data, cid)
                except Exception:
                    pass

        def _on_event(_user, event, detail):
            try:
                ev = event.decode("utf-8", "replace")
                dt = detail.decode("utf-8", "replace") if detail else ""
            except Exception:
                return
            for h in list(self._event_handlers):
                try:
                    h(ev, dt)
                except Exception:
                    pass

        self._msg_cb = _MSG_CB(_on_message)
        self._evt_cb = _EVT_CB(_on_event)
        _lib.rm_set_message_cb(self._handle, self._msg_cb, None)
        _lib.rm_set_event_cb(self._handle, self._evt_cb, None)

    # ---- 回调注册 ----
    def on_message(self, cb: Callable[[str, dict, str], None]) -> None:
        """cb(name, data:dict, client_id) —— 在 C++ 工作线程中调用"""
        self._message_handlers.append(cb)

    def on_event(self, cb: Callable[[str, str], None]) -> None:
        self._event_handlers.append(cb)

    # ---- 发送 ----
    def send(self, name: str, data) -> bool:
        if isinstance(data, str):
            payload = data
        else:
            info = message_info(name)
            wire = _convert_keys(data, info.fields, True) if info else data
            payload = json.dumps(wire, ensure_ascii=False)
        rc = self._send_impl(name, payload)
        if rc != 0:
            raise RuntimeError("发送 %s 失败: %s" % (name, _last_error()))
        return True

    def _send_impl(self, name: str, payload: str) -> int:
        raise NotImplementedError


# ---------------------------------------------------------------- 客户端
class Client(_Base):
    """自定义终端：订阅下行指令，发送上行指令"""

    def __init__(self, host: str = "127.0.0.1", port: int = 3333, client_id: str = "1"):
        h = _lib.rm_client_new(host.encode(), port, client_id.encode())
        if not h:
            raise RuntimeError("创建客户端失败: " + _last_error())
        self.host, self.port, self.client_id = host, port, client_id
        super().__init__(h)

    def _send_impl(self, name: str, payload: str) -> int:
        return _lib.rm_client_send(self._handle, name.encode(), payload.encode())

    def start(self) -> None:
        rc = _lib.rm_client_start(self._handle)
        if rc != 0:
            raise RuntimeError("连接失败: " + _last_error())

    def stop(self) -> None:
        if self._handle:
            _lib.rm_client_stop(self._handle)

    def close(self) -> None:
        self.stop()
        if self._handle:
            _lib.rm_client_free(self._handle)
            self._handle = None

    @property
    def connected(self) -> bool:
        return bool(_lib.rm_client_is_connected(self._handle))

    @property
    def received(self) -> int:
        return int(_lib.rm_client_received(self._handle))

    @property
    def sent(self) -> int:
        return int(_lib.rm_client_sent(self._handle))

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, *exc):
        self.close()


# ---------------------------------------------------------------- 服务器
class Server(_Base):
    """协议服务器（自带 MQTT broker）：发布下行指令，接收上行指令"""

    def __init__(self, host: str = "0.0.0.0", port: int = 3333):
        h = _lib.rm_server_new(host.encode(), port)
        if not h:
            raise RuntimeError("创建服务器失败: " + _last_error())
        self.host, self.port = host, port
        super().__init__(h)

    def _send_impl(self, name: str, payload: str) -> int:
        return _lib.rm_server_send(self._handle, name.encode(), payload.encode())

    def start(self) -> None:
        rc = _lib.rm_server_start(self._handle)
        if rc != 0:
            raise RuntimeError("启动失败: " + _last_error())

    def stop(self) -> None:
        if self._handle:
            _lib.rm_server_stop(self._handle)

    def close(self) -> None:
        self.stop()
        if self._handle:
            _lib.rm_server_free(self._handle)
            self._handle = None

    @property
    def running(self) -> bool:
        return bool(_lib.rm_server_is_running(self._handle))

    @property
    def client_count(self) -> int:
        return int(_lib.rm_server_client_count(self._handle))

    @property
    def subscribed_client_count(self) -> int:
        return int(_lib.rm_server_subscribed_client_count(self._handle))

    def topic_subscriber_count(self, topic: str) -> int:
        """订阅了指定 topic 的客户端数（判断「要发的这个 topic」是否就绪）。"""
        return int(_lib.rm_server_topic_subscriber_count(self._handle, topic.encode()))

    @property
    def received(self) -> int:
        return int(_lib.rm_server_received(self._handle))

    @property
    def sent(self) -> int:
        return int(_lib.rm_server_sent(self._handle))

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, *exc):
        self.close()


# ===========================================================================
# 图传（UDP 3334）
#
# 两端都只跟「图片」打交道，编解码都在 C++ 里完成：
#   发送端：喂 numpy BGR 图像 -> C++ 编码 HEVC -> UDP
#   接收端：UDP -> C++ 组帧解码 -> numpy BGR 图像（最新帧槽位）+ 统计
#
# 图像是 BGR24（与 cv2 一致），numpy 数组要求 C 连续。
# ===========================================================================

MAX_FRAME_BYTES = 1920 * 1080 * 3          # 单帧缓冲上限（1080p BGR24）


@dataclass
class VideoFrame:
    """一帧解码后的图像及其元信息。"""
    image: "np.ndarray"      # (h, w, 3) uint8，BGR 排列
    frame_number: int
    keyframe: bool

    @property
    def width(self) -> int:
        return int(self.image.shape[1])

    @property
    def height(self) -> int:
        return int(self.image.shape[0])


@dataclass
class VideoSenderStats:
    running: bool = False
    frames: int = 0
    packets: int = 0
    bytes: int = 0
    encode_errors: int = 0
    fps: float = 0.0
    mbps: float = 0.0
    width: int = 0
    height: int = 0

    @classmethod
    def from_json(cls, d: dict) -> "VideoSenderStats":
        tx = d.get("tx", {})
        return cls(running=d.get("running", False), frames=tx.get("frames", 0),
                   packets=tx.get("packets", 0), bytes=tx.get("bytes", 0),
                   encode_errors=tx.get("encodeErrors", 0), fps=tx.get("fps", 0.0),
                   mbps=tx.get("mbps", 0.0), width=d.get("width", 0),
                   height=d.get("height", 0))

    def __str__(self) -> str:
        return ("已发 %d 帧 / %d 包 / %.1f KB  %.1f fps  %.2f Mbps%s"
                % (self.frames, self.packets, self.bytes / 1024.0, self.fps, self.mbps,
                   "" if self.encode_errors == 0 else "  编码错误 %d" % self.encode_errors))


@dataclass
class VideoReceiverStats:
    running: bool = False
    codec: str = "unknown"
    codec_mode: str = "auto"
    width: int = 0
    height: int = 0
    waiting_keyframe: bool = False
    # 组帧
    packets: int = 0
    bytes: int = 0
    frames_complete: int = 0
    frames_incomplete: int = 0
    duplicate_fragments: int = 0
    out_of_order_frames: int = 0
    bad_packets: int = 0
    frame_gaps: int = 0
    rx_fps: float = 0.0
    rx_mbps: float = 0.0
    # 解码
    frames_decoded: int = 0
    decode_errors: int = 0
    dropped_by_slot: int = 0
    keyframe_waits: int = 0
    decoder_resets: int = 0
    decode_fps: float = 0.0

    @classmethod
    def from_json(cls, d: dict) -> "VideoReceiverStats":
        rx = d.get("rx", {})
        dec = d.get("decode", {})
        return cls(
            running=d.get("running", False), codec=d.get("codec", "unknown"),
            codec_mode=d.get("codecMode", "auto"), width=d.get("width", 0),
            height=d.get("height", 0), waiting_keyframe=d.get("waitingKeyframe", False),
            packets=rx.get("packets", 0), bytes=rx.get("bytes", 0),
            frames_complete=rx.get("framesComplete", 0),
            frames_incomplete=rx.get("framesIncomplete", 0),
            duplicate_fragments=rx.get("duplicateFragments", 0),
            out_of_order_frames=rx.get("outOfOrderFrames", 0),
            bad_packets=rx.get("badPackets", 0), frame_gaps=rx.get("frameGaps", 0),
            rx_fps=rx.get("fps", 0.0), rx_mbps=rx.get("mbps", 0.0),
            frames_decoded=dec.get("frames", 0), decode_errors=dec.get("errors", 0),
            dropped_by_slot=dec.get("droppedBySlot", 0),
            keyframe_waits=dec.get("keyframeWaits", 0),
            decoder_resets=dec.get("decoderResets", 0), decode_fps=dec.get("fps", 0.0))

    def __str__(self) -> str:
        return ("%s/%s %dx%d  收 %d 帧(完整) 解 %d 帧  %.1f fps  %.2f Mbps%s"
                % (self.codec, self.codec_mode, self.width, self.height,
                   self.frames_complete, self.frames_decoded, self.rx_fps, self.rx_mbps,
                   "" if self.decode_errors == 0 else "  解码错误 %d" % self.decode_errors))


def _bgr_pointer(image) -> "tuple":
    """校验并返回 numpy BGR 图像的 (指针, 宽, 高, stride)。"""
    import numpy as np
    if not isinstance(image, np.ndarray):
        raise TypeError("图像必须是 numpy 数组（BGR）")
    if image.dtype != np.uint8:
        raise TypeError("图像 dtype 必须是 uint8，收到 %s" % image.dtype)
    if image.ndim != 3 or image.shape[2] != 3:
        raise ValueError("图像形状必须是 (h, w, 3)，收到 %s" % (image.shape,))
    if not image.flags["C_CONTIGUOUS"]:
        image = np.ascontiguousarray(image)
    h, w = image.shape[:2]
    return image.ctypes.data_as(c_void_p), w, h, w * 3


class VideoSender:
    """图传发送端：喂 BGR 图像，C++ 内部编码 HEVC 并发 UDP。

        tx = VideoSender("127.0.0.1", 3334, width=1920, height=1080, fps=60)
        tx.start()
        tx.send_frame(frame)      # frame 是 (h, w, 3) uint8 BGR
        tx.flush(); tx.stop()

    编码参数（也可用 config() 改，需先 stop）：
        width height fps bitrate_kbps gop_size bframes slices aud repeat_headers
        pix_fmt preset tune intra_refresh
    故障注入（fault()，用来压测对端）：
        drop_every_packet duplicate_every_packet shuffle_fragments
        corrupt_frame_total frame_number_step rotate_every_frames max_datagram
    """

    def __init__(self, host: str = "127.0.0.1", port: int = 3334,
                 width: int = 1920, height: int = 1080, fps: int = 60,
                 bitrate_kbps: int = 8000, **encoder_opts):
        if not _HAS_NUMPY:
            raise RuntimeError("图传接口需要 numpy")
        self._h = _lib.rm_video_sender_new(host.encode(), port)
        if not self._h:
            raise MemoryError("创建图传发送端失败")
        cfg = {"width": width, "height": height, "fps": fps,
               "bitrate_kbps": bitrate_kbps}
        cfg.update(encoder_opts)
        self.config(**cfg)
        self._sent = 0

    # -- 配置 --
    def config(self, **kwargs) -> None:
        """设置编码参数（键名见类文档）。运行中调用会报错，请先 stop()。"""
        self._check(rm=_lib.rm_video_sender_config(self._h,
                                                   json.dumps(kwargs).encode()))

    def fault(self, **kwargs) -> None:
        """故障注入参数（用来压测对端）。运行中调用会报错。"""
        self._check(_lib.rm_video_sender_fault(self._h, json.dumps(kwargs).encode()))

    # -- 生命周期 --
    def start(self) -> "VideoSender":
        self._check(_lib.rm_video_sender_start(self._h))
        return self

    def stop(self) -> None:
        if self._h:
            _lib.rm_video_sender_stop(self._h)

    @property
    def running(self) -> bool:
        return bool(_lib.rm_video_sender_is_running(self._h))

    # -- 发送 --
    def send_frame(self, image) -> None:
        """喂一帧 BGR 图像（(h,w,3) uint8）。编码 + 打包 + 发送同步完成。"""
        ptr, w, h, stride = _bgr_pointer(image)
        self._check(_lib.rm_video_sender_send_bgr(self._h, ptr, w, h, stride))
        self._sent += 1

    def flush(self) -> None:
        """发送结束前调用一次，把编码器里缓存的 AU 冲出来（含 B 帧时必需）。"""
        self._check(_lib.rm_video_sender_flush(self._h))

    @property
    def stats(self) -> VideoSenderStats:
        return VideoSenderStats.from_json(self._stats_json())

    def _stats_json(self) -> dict:
        buf = ctypes.create_string_buffer(8192)
        n = _lib.rm_video_sender_stats(self._h, buf, len(buf))
        if n < 0:
            raise RuntimeError("统计缓冲区太小")
        return json.loads(buf.value.decode() or "{}")

    # -- 资源 --
    def close(self) -> None:
        if self._h:
            _lib.rm_video_sender_stop(self._h)
            _lib.rm_video_sender_free(self._h)
            self._h = None

    def _check(self, rm: int) -> None:
        if rm != 0:
            raise RuntimeError(_last_error())

    def __enter__(self):
        return self.start()

    def __exit__(self, *exc):
        self.close()

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass


class VideoReceiver:
    """图传接收端：UDP -> C++ 组帧解码 -> BGR 图像（最新帧槽位）+ 统计。

        rx = VideoReceiver(port=3334, codec_mode=CODEC_AUTO)
        rx.start()
        while True:
            frame = rx.get_frame()          # None = 没有新帧
            if frame: show(frame.image)

    解码格式（codec_mode）：
        CODEC_AUTO —— 默认，用探针自动探测码流格式
        CODEC_H265 / CODEC_H264 / CODEC_JPEG —— 手动固定，完全跳过探测
      也可以用 set_codec_mode() 在**运行中**随时切换。
    """

    def __init__(self, bind: str = "0.0.0.0", port: int = 3334,
                 codec_mode: str = CODEC_AUTO, **opts):
        if not _HAS_NUMPY:
            raise RuntimeError("图传接口需要 numpy")
        self._h = _lib.rm_video_receiver_new(bind.encode(), port)
        if not self._h:
            raise MemoryError("创建图传接收端失败")
        self._since = ctypes.c_uint64(0)
        self._buf = np.empty(MAX_FRAME_BYTES, dtype=np.uint8)
        self._cb_ref = None          # 持有回调引用，防止被 GC
        self._on_frame = None
        if codec_mode != CODEC_AUTO or opts:
            cfg = dict(opts)
            cfg["codec_mode"] = codec_mode
            self.config(**cfg)

    # -- 配置 --
    def config(self, **kwargs) -> None:
        """设置参数：max_frame_bytes / frame_timeout_ms / max_pending_frames /
        au_queue_depth / bind_host / port / codec_mode。需先 stop()。"""
        self._check(_lib.rm_video_receiver_config(self._h,
                                                  json.dumps(kwargs).encode()))

    def set_codec_mode(self, mode: str) -> None:
        """设置解码格式模式。**运行中也可调用**，下一个 access unit 起生效。

            rx.set_codec_mode(CODEC_AUTO)    # 交回探针自动选择
            rx.set_codec_mode(CODEC_H265)    # 手动固定为 H.265
        """
        self._check(_lib.rm_video_receiver_set_codec_mode(self._h, str(mode).encode()))

    @property
    def codec_mode(self) -> str:
        return (_lib.rm_video_receiver_codec_mode(self._h) or b"auto").decode()

    # -- 生命周期 --
    def start(self) -> "VideoReceiver":
        self._check(_lib.rm_video_receiver_start(self._h))
        return self

    def stop(self) -> None:
        if self._h:
            _lib.rm_video_receiver_stop(self._h)

    @property
    def running(self) -> bool:
        return bool(_lib.rm_video_receiver_is_running(self._h))

    # -- 收帧 --
    def get_frame(self) -> Optional[VideoFrame]:
        """取最新解码帧。返回 None 表示自上次调用以来没有新帧。

        注意：这是「最新帧槽位」语义 —— UI 跟不上时会自动丢旧帧，
        不会阻塞网络和解码线程。这正是实时预览想要的。
        """
        w = c_int(0); h = c_int(0); stride = c_int(0)
        fno = ctypes.c_uint64(0); key = c_int(0)
        r = _lib.rm_video_receiver_get_frame(
            self._h, self._buf.ctypes.data_as(c_void_p), MAX_FRAME_BYTES,
            ctypes.byref(w), ctypes.byref(h), ctypes.byref(stride),
            ctypes.byref(fno), ctypes.byref(key), ctypes.byref(self._since))
        if r == 0:
            return None
        if r < 0:
            raise RuntimeError(_last_error())
        hh, ww = h.value, w.value
        # stride 一般等于 w*3；如果是（未对齐的）更大值需要按行取
        if stride.value == ww * 3:
            img = self._buf[:hh * ww * 3].reshape(hh, ww, 3).copy()
        else:
            rows = self._buf[:stride.value * hh].reshape(hh, stride.value)
            img = np.ascontiguousarray(rows[:, :ww * 3]).reshape(hh, ww, 3).copy()
        return VideoFrame(image=img, frame_number=int(fno.value), keyframe=bool(key.value))

    def set_frame_handler(self, fn: Optional[Callable[[VideoFrame], None]]) -> None:
        """注册「每解出一帧就回调」的处理器（一帧不漏）。

        预览建议用 get_frame() 主动拉取；需要逐帧处理（如录像）时用这个。
        注意：回调在 C++ 的解码线程里执行。
        """
        if fn is None:
            _lib.rm_video_receiver_set_frame_cb(self._h, _VIDEO_FRAME_CB(), None)
            self._cb_ref = None
            self._on_frame = None
            return
        self._on_frame = fn

        def _trampoline(_user, data, w, h, stride, fno, key):
            try:
                arr = ctypes.cast(data, ctypes.POINTER(ctypes.c_uint8))
                buf = np.ctypeslib.as_array(arr, shape=(h * stride,))
                img = buf[:h * w * 3].reshape(h, w, 3).copy()
                fn(VideoFrame(image=img, frame_number=int(fno), keyframe=bool(key)))
            except Exception:
                pass

        self._cb_ref = _VIDEO_FRAME_CB(_trampoline)
        _lib.rm_video_receiver_set_frame_cb(self._h, self._cb_ref, None)

    @property
    def stats(self) -> VideoReceiverStats:
        return VideoReceiverStats.from_json(self._stats_json())

    def _stats_json(self) -> dict:
        buf = ctypes.create_string_buffer(8192)
        n = _lib.rm_video_receiver_stats(self._h, buf, len(buf))
        if n < 0:
            raise RuntimeError("统计缓冲区太小")
        return json.loads(buf.value.decode() or "{}")

    # -- 资源 --
    def close(self) -> None:
        if self._h:
            _lib.rm_video_receiver_stop(self._h)
            _lib.rm_video_receiver_free(self._h)
            self._h = None

    def _check(self, rm: int) -> None:
        if rm != 0:
            raise RuntimeError(_last_error())

    def __enter__(self):
        return self.start()

    def __exit__(self, *exc):
        self.close()

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass
