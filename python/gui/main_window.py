"""服务器 / 客户端共用的主窗口。

区别只在：
    * 本地「发送」方向（客户端=上行9条，服务器=下行23条）
    * 本地「接收」方向
    * 连接参数的默认值
"""
from __future__ import annotations

import time
from typing import Dict, List

from PyQt5.QtCore import Qt, QTimer
from PyQt5.QtWidgets import (QCheckBox, QComboBox, QGroupBox, QHBoxLayout,
                             QLabel, QLineEdit, QMainWindow, QMessageBox,
                             QPushButton, QScrollArea, QSpinBox, QSplitter,
                             QStatusBar, QTabWidget, QVBoxLayout, QWidget)

import rm2027c as rm
from . import theme
from .bridge import QtBridge
from .message_panel import MessagePanel
from .receive_view import ReceiveView
from .video_panel import VideoReceivePanel, VideoSendPanel


class MainWindow(QMainWindow):
    def __init__(self, role: str, host: str, port: int, client_id: str = "1", parent=None):
        super().__init__(parent)
        assert role in ("client", "server")
        self.role = role
        self.obj = None                 # rm2027c.Client / Server
        self.bridge = QtBridge(self)
        self.bridge.messageReceived.connect(self._on_message)
        self.bridge.eventReceived.connect(self._on_event)

        self.setStyleSheet(theme.build_stylesheet())
        self.setWindowTitle(
            "RM2027 %s —— 官方协议 V1.0.0（32 条指令）"
            % ("自定义终端" if role == "client" else "协议服务器")
        )
        self.resize(1480, 880)

        # ---------------------------------------------------------- 连接栏
        bar = QGroupBox("连接")
        bl = QHBoxLayout(bar)

        bl.addWidget(QLabel("地址"))
        self.host_edit = QLineEdit(host)
        self.host_edit.setFixedWidth(130)
        bl.addWidget(self.host_edit)

        bl.addWidget(QLabel("端口"))
        self.port_spin = QSpinBox()
        self.port_spin.setRange(1, 65535)
        self.port_spin.setValue(port)
        bl.addWidget(self.port_spin)

        self.id_label = QLabel("clientID(机器人ID)")
        self.id_edit = QLineEdit(client_id)
        self.id_edit.setFixedWidth(60)
        self.id_edit.setToolTip("官方规定：MQTT clientID 必须填所连接机器人对应的 ID 编号")
        bl.addWidget(self.id_label)
        bl.addWidget(self.id_edit)

        self.start_btn = QPushButton("启动")
        self.start_btn.clicked.connect(self._toggle_start)
        bl.addWidget(self.start_btn)

        self.state_label = QLabel("未启动")
        self._paint_state(False)
        bl.addWidget(self.state_label)

        bl.addStretch(1)
        self.stats_label = QLabel("收 0 / 发 0")
        bl.addWidget(self.stats_label)

        if role == "client":
            self.id_label.setVisible(True)
            self.id_edit.setVisible(True)
        else:
            self.id_label.setVisible(False)
            self.id_edit.setVisible(False)

        # ---------------------------------------------------------- 主体
        split = QSplitter(Qt.Horizontal)

        # 左侧：发送面板
        left = QWidget()
        ll = QVBoxLayout(left)
        ll.setContentsMargins(0, 0, 0, 0)

        send_infos = self._send_infos()
        ctrl = QHBoxLayout()
        ctrl.addWidget(QLabel("发送指令 %d 条：" % len(send_infos)))
        b_all_on = QPushButton("全部启用")
        b_all_off = QPushButton("全部停止")
        b_reset = QPushButton("频率恢复官方默认")
        b_all_on.clicked.connect(lambda: self._set_all(True))
        b_all_off.clicked.connect(lambda: self._set_all(False))
        b_reset.clicked.connect(self._reset_freqs)
        ctrl.addWidget(b_all_on)
        ctrl.addWidget(b_all_off)
        ctrl.addWidget(b_reset)
        ctrl.addStretch(1)
        ll.addLayout(ctrl)

        self.search_edit = QLineEdit()
        self.search_edit.setPlaceholderText("按指令名过滤…")
        self.search_edit.textChanged.connect(self._apply_filter)
        ll.addWidget(self.search_edit)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        holder = QWidget()
        hl = QVBoxLayout(holder)
        hl.setContentsMargins(2, 2, 2, 2)
        hl.setSpacing(3)
        self.panels: List[MessagePanel] = []
        for info in send_infos:
            p = MessagePanel(info)
            p.sendRequested.connect(self._do_send)
            p.stateChanged.connect(self._refresh_stats)
            hl.addWidget(p)
            self.panels.append(p)
        hl.addStretch(1)
        scroll.setWidget(holder)
        ll.addWidget(scroll, 1)
        split.addWidget(left)

        # 右侧：接收显示
        recv_infos = self._recv_infos()
        self.receive = ReceiveView(recv_infos)
        right = QWidget()
        rl = QVBoxLayout(right)
        rl.setContentsMargins(0, 0, 0, 0)
        head = QHBoxLayout()
        head.addWidget(QLabel("接收指令 %d 条" % len(recv_infos)))
        b_clear = QPushButton("清空")
        b_clear.clicked.connect(self.receive.clear)
        head.addWidget(b_clear)
        head.addStretch(1)
        rl.addLayout(head)
        rl.addWidget(self.receive, 1)
        split.addWidget(right)

        split.setSizes([780, 700])

        proto_page = QWidget()
        cl = QVBoxLayout(proto_page)
        cl.setContentsMargins(6, 6, 6, 6)
        cl.addWidget(bar)
        cl.addWidget(split, 1)

        # 图传页：服务器侧发图传，客户端侧收图传 + 实时预览
        if self.role == "server":
            self.video = VideoSendPanel(host if host else "127.0.0.1")
        else:
            self.video = VideoReceivePanel("0.0.0.0")
        video_page = QWidget()
        vl = QVBoxLayout(video_page)
        vl.setContentsMargins(6, 6, 6, 6)
        vl.addWidget(self.video)
        vl.addStretch(1)

        self.tabs = QTabWidget()
        self.tabs.addTab(proto_page, "协议指令")
        self.tabs.addTab(video_page, "图传")
        self.setCentralWidget(self.tabs)

        self.setStatusBar(QStatusBar())
        self.statusBar().showMessage("就绪")

        # 自动发送定时器
        self.timer = QTimer(self)
        self.timer.setInterval(10)
        self.timer.timeout.connect(self._tick)
        self.timer.start()

        self._stats_timer = QTimer(self)
        self._stats_timer.setInterval(500)
        self._stats_timer.timeout.connect(self._refresh_stats)
        self._stats_timer.start()

    def refresh_theme(self) -> None:
        """主题变化时重建所有自定义样式（面板底、次要文字、状态色）"""
        self.setStyleSheet(theme.build_stylesheet())
        for p in self.panels:
            p.refresh_theme()
        self.receive.refresh_theme()
        if getattr(self, "video", None) is not None:
            self.video.refresh_theme()
        self._paint_state(self.obj is not None)

    def _paint_state(self, running: bool) -> None:
        color = theme.success_color() if running else theme.error_color()
        self.state_label.setStyleSheet("color:%s;font-weight:bold;" % color)

    def changeEvent(self, ev):
        # 系统切换明暗主题时重建样式，避免写死的颜色对不上
        from PyQt5.QtCore import QEvent
        if ev.type() == QEvent.ApplicationPaletteChange:
            self.refresh_theme()
        super().changeEvent(ev)

    # ---------------------------------------------------------------- 方向
    def _send_infos(self):
        return [m for m in rm.messages()
                if (m.direction == "up") == (self.role == "client")]

    def _recv_infos(self):
        return [m for m in rm.messages()
                if (m.direction == "down") == (self.role == "client")]

    # ---------------------------------------------------------------- 启动
    def _toggle_start(self) -> None:
        if self.obj is not None:
            self._stop()
            return
        host = self.host_edit.text().strip() or "127.0.0.1"
        port = int(self.port_spin.value())
        try:
            if self.role == "client":
                self.obj = rm.Client(host, port, self.id_edit.text().strip() or "1")
            else:
                self.obj = rm.Server(host, port)
            self.bridge.attach(self.obj)
            self.obj.start()
        except Exception as exc:
            self.obj = None
            QMessageBox.critical(self, "启动失败", str(exc))
            self.statusBar().showMessage("启动失败: %s" % exc)
            return
        self.start_btn.setText("停止")
        self.host_edit.setEnabled(False)
        self.port_spin.setEnabled(False)
        self.id_edit.setEnabled(False)
        self.state_label.setText("运行中")
        self._paint_state(True)
        self.statusBar().showMessage("已启动 %s:%d" % (host, port))

    def _stop(self) -> None:
        try:
            if self.obj is not None:
                self.obj.close()
        except Exception:
            pass
        self.obj = None
        self.start_btn.setText("启动")
        self.host_edit.setEnabled(True)
        self.port_spin.setEnabled(True)
        self.id_edit.setEnabled(True)
        self.state_label.setText("已停止")
        self._paint_state(False)
        self.statusBar().showMessage("已停止")

    # ---------------------------------------------------------------- 发送
    def _do_send(self, name: str, data: dict) -> None:
        try:
            if self.obj is None:
                raise RuntimeError("尚未启动")
            self.obj.send(name, data)
        except Exception as exc:
            self.statusBar().showMessage("发送 %s 失败: %s" % (name, exc))
            return
        self.statusBar().showMessage("已发送 %s" % name)

    def _tick(self) -> None:
        if self.obj is None:
            return
        now = time.monotonic()
        for p in self.panels:
            if p.should_auto_send(now):
                p.mark_sent(now)
                self._do_send(p.info.name, p.data())

    # ---------------------------------------------------------------- 批量
    def _set_all(self, on: bool) -> None:
        for p in self.panels:
            if p.frequency() > 0.0:
                p.enable_box.setChecked(on)

    def _reset_freqs(self) -> None:
        for p in self.panels:
            p.freq_box.setValue(p.info.default_hz)

    def _apply_filter(self, text: str) -> None:
        text = text.strip().lower()
        for p in self.panels:
            p.setVisible(not text or text in p.info.name.lower())

    # ---------------------------------------------------------------- 接收
    def _on_message(self, name: str, data: dict, client_id: str) -> None:
        self.receive.on_message(name, data, client_id)

    def _on_event(self, event: str, detail: str) -> None:
        # 客户端连接状态变化即时反映；其它事件走状态栏
        if event == "connected":
            self._refresh_stats()
            self.statusBar().showMessage("已连接 broker")
            return
        if event == "disconnected":
            self._refresh_stats()
            self.statusBar().showMessage("连接断开，正在自动重连…")
            return
        self.statusBar().showMessage("事件: %s %s" % (event, detail))

    # ---------------------------------------------------------------- 状态
    def _refresh_stats(self) -> None:
        if self.obj is None:
            self.stats_label.setText("收 0 / 发 0")
            return
        if self.role == "client":
            extra = "已连接" if self.obj.connected else "未连接"
        else:
            extra = "客户端 %d（已订阅 %d）" % (self.obj.client_count,
                                                self.obj.subscribed_client_count)
        auto = sum(1 for p in self.panels if p.auto_enabled())
        self.stats_label.setText("收 %d / 发 %d ｜ %s ｜ 自动发送 %d 条"
                                 % (self.obj.received, self.obj.sent, extra, auto))

    def closeEvent(self, ev):
        """关闭窗口：先收尾协议连接与图传线程。

        注意：这个类历史上**定义过两个 closeEvent**，后一个覆盖前一个，
        导致图传清理成了死代码（关窗口后显示线程还在跑）。
        现在只保留这一个，两件事都做。

        图传的 stop() 在超时后会保留资源（不强杀线程，见 video_panel 的说明），
        此时**不能放行关闭** —— 否则线程还在用已被销毁的界面对象。
        改为拒绝本次关闭并提示，用户可以重试或直接杀进程。
        """
        self._stop()
        video = getattr(self, "video", None)
        if video is not None and hasattr(video, "stop"):
            video.stop()
            if getattr(video, "has_live_worker", False):
                self.statusBar().showMessage(
                    "图传线程未能在超时内退出，已取消关闭（可稍后重试或直接结束进程）")
                ev.ignore()
                return
        super().closeEvent(ev)
