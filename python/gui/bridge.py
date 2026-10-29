"""把 C++ 工作线程里的回调安全地转到 Qt 主线程。

ctypes 回调发生在 C++ 的线程里，直接操作 Qt 控件会崩。这里统一用信号转发。
"""
from __future__ import annotations

from PyQt5.QtCore import QObject, pyqtSignal


class QtBridge(QObject):
    # (指令名, 数据 dict, 发送方 clientId)
    messageReceived = pyqtSignal(str, dict, str)
    # (事件名, 详情)
    eventReceived = pyqtSignal(str, str)
    # (是否成功, 文本)
    status = pyqtSignal(bool, str)

    def __init__(self, parent=None):
        super().__init__(parent)

    def attach(self, obj) -> None:
        """给 rm2027c.Client / Server 挂上转发回调"""
        obj.on_message(lambda name, data, cid: self.messageReceived.emit(name, data, cid))
        obj.on_event(lambda ev, detail: self.eventReceived.emit(ev, detail))
