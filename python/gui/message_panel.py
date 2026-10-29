"""每条指令一个面板：启用开关 + 发送频率 + 立即/触发发送 + 字段编辑。"""
from __future__ import annotations

import json
from typing import Any, Dict, List

from PyQt5.QtCore import Qt, pyqtSignal
from PyQt5.QtWidgets import (QCheckBox, QDoubleSpinBox, QFrame, QHBoxLayout,
                             QLabel, QPushButton, QScrollArea, QSizePolicy,
                             QToolButton, QVBoxLayout, QWidget)

from . import theme
from .field_form import FieldEditor, build_form, empty_message


class MessagePanel(QFrame):
    """一条指令的发送面板"""

    sendRequested = pyqtSignal(str, dict)   # 指令名, 数据
    stateChanged = pyqtSignal()

    def __init__(self, info, parent=None):
        super().__init__(parent)
        self.info = info
        self.editors: List[FieldEditor] = []
        self._next_ts = 0.0
        self.setFrameShape(QFrame.StyledPanel)
        self.setObjectName("messagePanel")

        root = QVBoxLayout(self)
        root.setContentsMargins(8, 4, 8, 4)
        root.setSpacing(4)

        # ---------------- 头部 ----------------
        head = QHBoxLayout()
        head.setSpacing(8)

        self.expand_btn = QToolButton()
        self.expand_btn.setText("▸")
        self.expand_btn.setCheckable(True)
        self.expand_btn.setAutoRaise(True)
        self.expand_btn.toggled.connect(self._on_expand)
        head.addWidget(self.expand_btn)

        self.enable_box = QCheckBox("启用")
        self.enable_box.setToolTip("勾选后按下面的频率自动发送")
        self.enable_box.stateChanged.connect(self._on_state_changed)
        head.addWidget(self.enable_box)

        title = QLabel("<b>%s</b>" % info.name)
        title.setMinimumWidth(158)
        title.setMaximumWidth(235)
        title.setToolTip(info.name)
        head.addWidget(title)

        head.addWidget(QLabel("频率"))
        self.freq_box = QDoubleSpinBox()
        self.freq_box.setDecimals(2)
        # 触发式指令官方规定最高 10Hz；固定频率指令给一个宽松上限
        self.freq_box.setRange(0.0, info.max_hz if info.max_hz > 0 else 200.0)
        self.freq_box.setSingleStep(1.0)
        self.freq_box.setSuffix(" Hz")
        self.freq_box.setFixedWidth(104)
        self.freq_box.setValue(info.default_hz)
        self.freq_box.setToolTip("设为 0 = 触发式发送（用右侧按钮手动触发）")
        self.freq_box.valueChanged.connect(self._on_freq_changed)
        head.addWidget(self.freq_box)

        self.doc_label = QLabel("官方 %s" % info.frequency_text)
        self.doc_label.setStyleSheet("color:%s;font-size:11px;" % theme.muted_color())
        self.doc_label.setToolTip("官方协议表 2-1 规定的发送频率")
        self.doc_label.setMinimumWidth(70)
        head.addWidget(self.doc_label)

        head.addStretch(1)

        self.send_btn = QPushButton("触发发送")
        self.send_btn.setFixedWidth(88)
        self.send_btn.clicked.connect(self._on_send_clicked)
        head.addWidget(self.send_btn)

        self.json_btn = QToolButton()
        self.json_btn.setText("{ }")
        self.json_btn.setToolTip("查看/编辑该指令的 JSON")
        self.json_btn.setCheckable(True)
        self.json_btn.toggled.connect(self._on_json_toggled)
        head.addWidget(self.json_btn)

        root.addLayout(head)

        # ---------------- 字段区 ----------------
        self.body = QWidget()
        body_lay = QVBoxLayout(self.body)
        body_lay.setContentsMargins(24, 2, 0, 2)
        body_lay.setSpacing(2)
        self.editors = build_form(info.fields)
        for e in self.editors:
            body_lay.addWidget(e)

        self.json_edit = QWidget()
        json_lay = QVBoxLayout(self.json_edit)
        json_lay.setContentsMargins(24, 2, 0, 2)
        from PyQt5.QtWidgets import QPlainTextEdit
        self.json_text = QPlainTextEdit()
        self.json_text.setFixedHeight(90)
        json_lay.addWidget(self.json_text)

        root.addWidget(self.body)
        root.addWidget(self.json_edit)
        self.json_edit.hide()
        self.body.hide()

        self._refresh_button_text()
        self.set_data(empty_message(info.fields))

    def refresh_theme(self) -> None:
        """系统切换明暗主题后重新上色（样式在构造时就写进了控件）"""
        self.doc_label.setStyleSheet("color:%s;font-size:11px;" % theme.muted_color())

    # ------------------------------------------------------------ 展开/折叠
    def _on_expand(self, on: bool) -> None:
        self.body.setVisible(on)
        self.expand_btn.setText("▾" if on else "▸")

    def _on_json_toggled(self, on: bool) -> None:
        if on:
            self.json_text.setPlainText(
                json.dumps(self.data(), ensure_ascii=False, indent=2))
            self.json_edit.show()
            self.body.hide()
            self.expand_btn.setChecked(False)
            self.expand_btn.setEnabled(False)
        else:
            try:
                self.set_data(json.loads(self.json_text.toPlainText()))
            except Exception as exc:  # JSON 写坏了就忽略
                self.json_text.setPlainText("解析失败: %s" % exc)
                return
            self.json_edit.hide()
            self.expand_btn.setEnabled(True)

    # ------------------------------------------------------------ 数据
    def data(self) -> Dict[str, Any]:
        out: Dict[str, Any] = {}
        for e in self.editors:
            try:
                out[e.name] = e.value()
            except Exception:
                out[e.name] = 0 if e.type not in ("string", "bytes") else ""
        return out

    def set_data(self, data: Dict[str, Any]) -> None:
        for e in self.editors:
            if e.name in data:
                try:
                    e.set_value(data[e.name])
                except Exception:
                    pass

    # ------------------------------------------------------------ 发送控制
    def frequency(self) -> float:
        return float(self.freq_box.value())

    def auto_enabled(self) -> bool:
        return self.enable_box.isChecked() and self.frequency() > 0.0

    def _refresh_button_text(self) -> None:
        if self.frequency() > 0.0:
            self.send_btn.setText("发送一次")
            self.enable_box.setEnabled(True)
            self.send_btn.setToolTip("立即发送一次；启用后会按频率自动发送")
        else:
            self.send_btn.setText("触发发送")
            self.enable_box.setEnabled(False)
            self.send_btn.setToolTip("频率为 0 = 触发式发送，点此按钮触发一次")

    def _on_freq_changed(self, _v) -> None:
        self._refresh_button_text()
        self._next_ts = 0.0
        self.stateChanged.emit()

    def _on_state_changed(self, _s) -> None:
        self._next_ts = 0.0
        self.stateChanged.emit()

    def _on_send_clicked(self) -> None:
        self.sendRequested.emit(self.info.name, self.data())

    def should_auto_send(self, now: float) -> bool:
        return self.auto_enabled() and now >= self._next_ts

    def mark_sent(self, now: float) -> None:
        hz = self.frequency()
        self._next_ts = now + (1.0 / hz if hz > 0 else 1e9)
